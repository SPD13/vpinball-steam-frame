// license:GPLv3+

#include "core/stdafx.h"
#include "MeshTiles.h"
#include "MeshSimplifier.h"
#include "LightMapPruning.h"

#include "core/player.h"
#include "parts/primitive.h"
#include "math/ModelViewProj.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace VPX
{

bool MeshTiles::s_enabled = true;
int MeshTiles::s_levelMode = MeshTiles::LevelsByDistanceAndGaze;
int MeshTiles::s_parts = 0;
size_t MeshTiles::s_tilesTotal = 0;

namespace
{
// Triangles drawn and left out by the tiles, per second (SelectRuns is called by the logic thread while it prepares a frame)
struct TileStats
{
   unsigned int frame = ~0u;
   uint64_t drawn = 0, culled = 0;
   std::chrono::steady_clock::time_point windowStart;
   float savedShare = NAN;
   bool logged = false;
};
TileStats s_stats;

// The places of the parts tiled with levels by tile, made once per table (GetPlaces)
std::mutex s_placesMutex;
std::unordered_map<const Primitive*, std::shared_ptr<const MeshTiles::Places>> s_places;

// Levels by tile: the error allowed grows away from the gaze, from kGazeFullAngle (the tile's closest point to the gaze, in radians: the tile may hold the
// gaze a frame or two later, and the gaze ray is not exact), by 1 per kGazeSlope, up to kGazeMaxFactor. Visual acuity falls faster than this (about
// a fifth at 10 degrees from the fovea), but the errors of the levels show as shifted shading and outlines, which the eye notices further out.
constexpr float kGazeFullAngle = 0.21f; // 12 degrees
constexpr float kGazeSlope = 0.14f; // 8 degrees
constexpr float kGazeMaxFactor = 4.f;
// Most levels a part draws at once: each adds runs to its draws and to those of each light map on it. The levels between are drawn at the next
// finer level kept.
constexpr size_t kMaxDrawnLevels = 3;

// A cell of the grid of a level (x and y within +-1e6): sorting the keys sorts the cells by level, then by row from the top of the table, then from
// the left
constexpr int kCellBits = 21, kCellOffset = 1 << 20;
uint64_t CellKey(int level, int x, int y)
{
   return (static_cast<uint64_t>(level) << (2 * kCellBits)) | (static_cast<uint64_t>(y + kCellOffset) << kCellBits) | static_cast<uint64_t>(x + kCellOffset);
}
}

void MeshTiles::ResetStats()
{
   {
      const std::lock_guard<std::mutex> lock(s_placesMutex);
      s_places.clear();
   }
   s_parts = 0;
   s_tilesTotal = 0;
   s_stats = TileStats();
   s_stats.windowStart = std::chrono::steady_clock::now();
}

void MeshTiles::CountTiled(size_t tiles)
{
   s_parts++;
   s_tilesTotal += tiles;
}

float MeshTiles::GetSavedShare()
{
   return s_stats.savedShare;
}

void MeshTiles::CountFrame(size_t drawn, size_t culled)
{
   if (!s_stats.logged)
   {
      // Once the index buffers are made
      s_stats.logged = true;
      PLOGI << "Culling by tiles: " << s_parts << " parts of at least " << kMinTriangles << " triangles in " << s_tilesTotal << " tiles of " << kTileSize << " VPU"
            << (s_enabled ? "" : " (off)");
   }
   if (g_pplayer && g_pplayer->m_overall_frames != s_stats.frame)
   {
      s_stats.frame = g_pplayer->m_overall_frames;
      const auto now = std::chrono::steady_clock::now();
      if (now - s_stats.windowStart >= std::chrono::seconds(1))
      {
         const uint64_t total = s_stats.drawn + s_stats.culled;
         s_stats.savedShare = total ? static_cast<float>(static_cast<double>(s_stats.culled) / static_cast<double>(total)) : 0.f;
         s_stats.drawn = s_stats.culled = 0;
         s_stats.windowStart = now;
      }
   }
   s_stats.drawn += drawn;
   s_stats.culled += culled;
}

uint64_t MeshTiles::Places::GridKey(int x, int y, int z)
{
   return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) | static_cast<uint64_t>(z & 0x1FFFFF);
}

uint64_t MeshTiles::Places::TripleKey(uint32_t a, uint32_t b, uint32_t c)
{
   if (a > b) std::swap(a, b);
   if (b > c) std::swap(b, c);
   if (a > b) std::swap(a, b);
   return (static_cast<uint64_t>(a) << 42) | (static_cast<uint64_t>(b) << 21) | static_cast<uint64_t>(c);
}

uint32_t MeshTiles::Places::Find(float x, float y, float z) const
{
   const int gx = static_cast<int>(floorf(x / kPlaceDistance)), gy = static_cast<int>(floorf(y / kPlaceDistance)), gz = static_cast<int>(floorf(z / kPlaceDistance));
   uint32_t best = kNone;
   float bestD = kPlaceDistance * kPlaceDistance;
   for (int dz = -1; dz <= 1; dz++)
      for (int dy = -1; dy <= 1; dy++)
         for (int dx = -1; dx <= 1; dx++)
         {
            const uint64_t key = GridKey(gx + dx, gy + dy, gz + dz);
            for (auto it = std::lower_bound(m_grid.begin(), m_grid.end(), std::make_pair(key, 0u)); it != m_grid.end() && it->first == key; ++it)
            {
               const float* q = &m_pos[it->second * 3];
               const float d = (q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y) + (q[2] - z) * (q[2] - z);
               if (d <= bestD)
               {
                  bestD = d;
                  best = m_placeOf[it->second];
               }
            }
         }
   return best;
}

uint64_t MeshTiles::Places::TriangleAt(uint32_t a, uint32_t b, uint32_t c) const
{
   if (a == kNone || b == kNone || c == kNone)
      return kBorder;
   const auto it = m_triangles.find(TripleKey(a, b, c));
   return it == m_triangles.end() ? kBorder : it->second;
}

uint64_t MeshTiles::TriangleCell(const PinTable* table, const Vertex3Ds (&p)[3])
{
   // The grids start at the top left corner of the playfield: the same cells for all parts
   const float left = min(table->m_left, table->m_right), top = min(table->m_top, table->m_bottom);
   const float cx = (p[0].x + p[1].x + p[2].x) / 3.f, cy = (p[0].y + p[1].y + p[2].y) / 3.f;
   const float length = max(max(p[0].x, max(p[1].x, p[2].x)) - min(p[0].x, min(p[1].x, p[2].x)), max(p[0].y, max(p[1].y, p[2].y)) - min(p[0].y, min(p[1].y, p[2].y)));
   int level = 0;
   while (level < kMaxLevel && !(length <= kTileSize * static_cast<float>(1 << level))) // Not finite: the coarsest
      level++;
   const float size = kTileSize * static_cast<float>(1 << level);
   const float fx = std::isfinite(cx) ? std::clamp((cx - left) / size, -1e6f, 1e6f) : 0.f;
   const float fy = std::isfinite(cy) ? std::clamp((cy - top) / size, -1e6f, 1e6f) : 0.f;
   return CellKey(level, static_cast<int>(floorf(fx)), static_cast<int>(floorf(fy)));
}

std::shared_ptr<const MeshTiles::Places> MeshTiles::GetPlaces(const Primitive* part, const Matrix3D& matrix)
{
   {
      const std::lock_guard<std::mutex> lock(s_placesMutex);
      if (const auto it = s_places.find(part); it != s_places.end())
         return it->second;
   }
   const Mesh& mesh = part->m_mesh;
   const size_t nv = mesh.NumVertices(), ni = mesh.NumIndices();
   bool tiled = ni / 3 >= kMinTriangles && mesh.m_animationFrames.empty() && nv > 0 && nv < (1u << 21) && part->m_ptable != nullptr;
   for (size_t i = 0; i < nv && tiled; i++)
      tiled = std::isfinite(mesh.m_vertices[i].x) && std::isfinite(mesh.m_vertices[i].y) && std::isfinite(mesh.m_vertices[i].z)
         && fabsf(mesh.m_vertices[i].x * part->m_d.m_vSize.x) < 1e4f && fabsf(mesh.m_vertices[i].y * part->m_d.m_vSize.y) < 1e4f && fabsf(mesh.m_vertices[i].z * part->m_d.m_vSize.z) < 1e4f;
   for (size_t i = 0; i < ni && tiled; i++)
      tiled = mesh.m_indices[i] < nv;
   std::shared_ptr<Places> places;
   if (tiled)
   {
      places = std::make_shared<Places>();
      Places& pl = *places;
      // The vertices as the simplifier sees them (scaled by the size), on a grid of kPlaceDistance
      const Vertex3Ds& size = part->m_d.m_vSize;
      pl.m_pos.resize(nv * 3);
      pl.m_grid.resize(nv);
      for (size_t i = 0; i < nv; i++)
      {
         const Vertex3D_NoTex2& v = mesh.m_vertices[i];
         float* q = &pl.m_pos[i * 3];
         q[0] = v.x * size.x;
         q[1] = v.y * size.y;
         q[2] = v.z * size.z;
         pl.m_grid[i] = { Places::GridKey(static_cast<int>(floorf(q[0] / Places::kPlaceDistance)), static_cast<int>(floorf(q[1] / Places::kPlaceDistance)),
                            static_cast<int>(floorf(q[2] / Places::kPlaceDistance))), static_cast<uint32_t>(i) };
      }
      std::sort(pl.m_grid.begin(), pl.m_grid.end());
      // Places: the vertices within kPlaceDistance of each other, chained (union-find)
      vector<uint32_t> parent(nv);
      std::iota(parent.begin(), parent.end(), 0u);
      const auto root = [&parent](uint32_t i)
      {
         while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
         return i;
      };
      constexpr float d2 = Places::kPlaceDistance * Places::kPlaceDistance;
      for (size_t i = 0; i < nv; i++)
      {
         const float* q = &pl.m_pos[i * 3];
         const int gx = static_cast<int>(floorf(q[0] / Places::kPlaceDistance)), gy = static_cast<int>(floorf(q[1] / Places::kPlaceDistance)), gz = static_cast<int>(floorf(q[2] / Places::kPlaceDistance));
         for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
               for (int dx = -1; dx <= 1; dx++)
               {
                  const uint64_t key = Places::GridKey(gx + dx, gy + dy, gz + dz);
                  for (auto it = std::lower_bound(pl.m_grid.begin(), pl.m_grid.end(), std::make_pair(key, 0u)); it != pl.m_grid.end() && it->first == key; ++it)
                  {
                     if (it->second <= i)
                        continue;
                     const float* o = &pl.m_pos[it->second * 3];
                     if ((o[0] - q[0]) * (o[0] - q[0]) + (o[1] - q[1]) * (o[1] - q[1]) + (o[2] - q[2]) * (o[2] - q[2]) <= d2)
                        parent[root(it->second)] = root(static_cast<uint32_t>(i));
                  }
               }
      }
      pl.m_placeOf.resize(nv);
      vector<uint32_t> idOfRoot(nv, Places::kNone);
      uint32_t nPlaces = 0;
      for (size_t i = 0; i < nv; i++)
      {
         const uint32_t r = root(static_cast<uint32_t>(i));
         if (idOfRoot[r] == Places::kNone)
            idOfRoot[r] = nPlaces++;
         pl.m_placeOf[i] = idOfRoot[r];
      }
      // The cells of the triangles around each place: a border where they differ
      constexpr uint64_t kUnset = Places::kBorder - 1;
      pl.m_value.assign(nPlaces, kUnset);
      vector<uint64_t> triangleCell(ni / 3);
      for (size_t t = 0; t < ni / 3; t++)
      {
         Vertex3Ds p[3];
         for (int k = 0; k < 3; k++)
         {
            const Vertex3D_NoTex2& v = mesh.m_vertices[mesh.m_indices[t * 3 + k]];
            p[k] = matrix.MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
         }
         const uint64_t cell = TriangleCell(part->m_ptable, p);
         triangleCell[t] = cell;
         for (int k = 0; k < 3; k++)
         {
            uint64_t& value = pl.m_value[pl.m_placeOf[mesh.m_indices[t * 3 + k]]];
            value = value == kUnset ? cell : value != cell ? Places::kBorder : value;
         }
      }
      // The places next to a border are locked too: then a collapse never moves a place next to a border, and every triangle it makes keeps a corner
      // inside its cell (the one collapsed onto). The triangles with all their corners on borders are kept by their places.
      for (size_t t = 0; t < ni / 3; t++)
      {
         const uint32_t a = pl.m_placeOf[mesh.m_indices[t * 3]], b = pl.m_placeOf[mesh.m_indices[t * 3 + 1]], c = pl.m_placeOf[mesh.m_indices[t * 3 + 2]];
         const bool ba = pl.m_value[a] == Places::kBorder, bb = pl.m_value[b] == Places::kBorder, bc = pl.m_value[c] == Places::kBorder;
         if (ba || bb || bc)
         {
            if (!ba) pl.m_value[a] |= Places::kLocked;
            if (!bb) pl.m_value[b] |= Places::kLocked;
            if (!bc) pl.m_value[c] |= Places::kLocked;
         }
         if (ba && bb && bc)
            pl.m_triangles.emplace(Places::TripleKey(a, b, c), triangleCell[t]);
      }
   }
   const std::lock_guard<std::mutex> lock(s_placesMutex);
   return s_places.emplace(part, places).first->second;
}

std::shared_ptr<MeshTiles> MeshTiles::Build(Primitive* part, const Mesh& mesh, const MeshLevels* levels, vector<unsigned int>& indices)
{
   const size_t nv = mesh.NumVertices();
   // Levels by tile: the cells of the places of the part owning the levels
   std::shared_ptr<const Places> places;
   if (Primitive* const owner = levels ? levels->tileOwner : nullptr; owner && (owner == part || levels->leader == owner) && owner->m_ptable)
      places = GetPlaces(owner, owner->RecalculateMatrices());
   if ((places == nullptr && mesh.NumIndices() / 3 < kMinTriangles) || !mesh.m_animationFrames.empty() || nv == 0 || part->m_ptable == nullptr)
      return nullptr;
   for (const Vertex3D_NoTex2& v : mesh.m_vertices)
      if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
         return nullptr;

   // The ranges of the buffer: the mesh, then each level
   struct Span
   {
      uint32_t first, count;
      int level; // -1 for the mesh
   };
   vector<Span> spans { { 0, static_cast<uint32_t>(mesh.NumIndices()), -1 } };
   if (levels)
      for (size_t l = 0; l < levels->levels.size(); l++)
         spans.push_back({ levels->levels[l].first, static_cast<uint32_t>(levels->levels[l].indices.size()), static_cast<int>(l) });
   for (const Span& s : spans)
      if (static_cast<size_t>(s.first) + s.count > indices.size())
         return nullptr;

   // The cell of each triangle, from its center on the table, in the grid of the first level whose cells are as long as the triangle (TriangleCell). With
   // levels by tile, the cell of its first corner inside a cell of the owner's places instead, when it has one.
   const Matrix3D matrix = part->RecalculateMatrices();
   vector<uint32_t> vertexPlace;
   if (places)
   {
      vertexPlace.resize(nv);
      const Vertex3Ds& size = part->m_d.m_vSize;
      for (size_t i = 0; i < nv; i++)
      {
         const Vertex3D_NoTex2& v = mesh.m_vertices[i];
         vertexPlace[i] = part == levels->tileOwner ? places->OfVertex(i) : places->Find(v.x * size.x, v.y * size.y, v.z * size.z);
      }
   }
   std::unordered_map<uint64_t, uint32_t> cellIds;
   vector<uint64_t> cellOf; // Cell of each triangle of each span, in order
   size_t totalTriangles = 0;
   for (const Span& s : spans)
      totalTriangles += s.count / 3;
   cellOf.reserve(totalTriangles);
   for (const Span& s : spans)
      for (uint32_t i = s.first; i + 2 < s.first + s.count; i += 3)
      {
         uint64_t key = Places::kBorder;
         for (int k = 0; k < 3; k++)
         {
            const unsigned int vi = indices[i + k];
            if (vi >= nv)
               return nullptr;
            if (places && key == Places::kBorder)
               if (const uint64_t value = places->Value(vertexPlace[vi]); value != Places::kBorder)
                  key = value & ~Places::kLocked;
         }
         if (places && key == Places::kBorder) // All its corners on borders: a triangle of the mesh, unchanged by the levels
            key = places->TriangleAt(vertexPlace[indices[i]], vertexPlace[indices[i + 1]], vertexPlace[indices[i + 2]]);
         if (key == Places::kBorder)
         {
            Vertex3Ds p[3];
            for (int k = 0; k < 3; k++)
            {
               const Vertex3D_NoTex2& v = mesh.m_vertices[indices[i + k]];
               p[k] = matrix.MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
            }
            key = TriangleCell(part->m_ptable, p);
         }
         cellOf.push_back(key);
         cellIds.emplace(key, 0u);
      }
   if (cellIds.size() < 2 && places == nullptr)
      return nullptr;

   // Tiles in the order of their cells, by level, then by row from the top of the table, then from the left, so that a part of the view draws few runs
   vector<uint64_t> cells;
   cells.reserve(cellIds.size());
   for (const auto& [key, id] : cellIds)
      cells.push_back(key);
   std::sort(cells.begin(), cells.end());
   for (size_t t = 0; t < cells.size(); t++)
      cellIds[cells[t]] = static_cast<uint32_t>(t);

   auto tiles = std::make_shared<MeshTiles>();
   const size_t nTiles = cells.size();
   tiles->m_tiles.assign(nTiles, Tile { Vertex3Ds(FLT_MAX, FLT_MAX, FLT_MAX), Vertex3Ds(-FLT_MAX, -FLT_MAX, -FLT_MAX), 0 });
   for (size_t t = 0; t < nTiles; t++)
      tiles->m_tiles[t].cell = cells[t];
   tiles->m_visible.resize(nTiles);
   tiles->m_levelsByTile = places != nullptr;

   const LightMapPruning& pruning = LightMapPruning::Get();
   vector<uint32_t> tileOf, count(nTiles), offset(nTiles);
   vector<unsigned int> sorted;
   size_t t0 = 0; // First triangle of the span in cellOf
   for (const Span& s : spans)
   {
      const uint32_t nt = s.count / 3;
      tileOf.resize(nt);
      for (uint32_t t = 0; t < nt; t++)
      {
         const uint32_t tile = cellIds[cellOf[t0 + t]];
         tileOf[t] = tile;
         Tile& box = tiles->m_tiles[tile];
         for (int k = 0; k < 3; k++)
         {
            const Vertex3D_NoTex2& v = mesh.m_vertices[indices[s.first + t * 3 + k]];
            box.lo.x = min(box.lo.x, v.x); box.lo.y = min(box.lo.y, v.y); box.lo.z = min(box.lo.z, v.z);
            box.hi.x = max(box.hi.x, v.x); box.hi.y = max(box.hi.y, v.y); box.hi.z = max(box.hi.z, v.z);
         }
      }
      t0 += nt;

      // The shares of a light map's sort kept by each level of pruning stay whole (in triangles): the tiles are grouped within each
      vector<uint32_t> bounds { 0, nt };
      if (const auto* kept = pruning.GetCounts(part, s.level); kept)
         for (const uint32_t k : *kept)
            bounds.push_back(min(k / 3, nt));
      std::sort(bounds.begin(), bounds.end());
      bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

      Range& range = tiles->m_ranges.emplace_back();
      range.first = s.first;
      range.count = nt * 3;
      sorted.resize(static_cast<size_t>(nt) * 3);
      for (size_t b = 0; b + 1 < bounds.size(); b++)
      {
         const uint32_t from = bounds[b], to = bounds[b + 1];
         std::fill(count.begin(), count.end(), 0u);
         for (uint32_t t = from; t < to; t++)
            count[tileOf[t]]++;
         uint32_t at = from;
         for (size_t tile = 0; tile < nTiles; tile++)
         {
            offset[tile] = at;
            if (count[tile])
               range.entries.push_back(Entry { at * 3, count[tile] * 3, static_cast<uint32_t>(tile) });
            at += count[tile];
         }
         for (uint32_t t = from; t < to; t++) // Stable: the order of the triangles within a tile is kept
         {
            const uint32_t dst = offset[tileOf[t]]++;
            std::copy_n(&indices[s.first + t * 3], 3, &sorted[dst * 3]);
         }
      }
      std::copy(sorted.begin(), sorted.end(), indices.begin() + s.first);
   }

   tiles->m_lo = Vertex3Ds(FLT_MAX, FLT_MAX, FLT_MAX);
   tiles->m_hi = Vertex3Ds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
   for (const Tile& t : tiles->m_tiles)
   {
      tiles->m_lo.x = min(tiles->m_lo.x, t.lo.x); tiles->m_lo.y = min(tiles->m_lo.y, t.lo.y); tiles->m_lo.z = min(tiles->m_lo.z, t.lo.z);
      tiles->m_hi.x = max(tiles->m_hi.x, t.hi.x); tiles->m_hi.y = max(tiles->m_hi.y, t.hi.y); tiles->m_hi.z = max(tiles->m_hi.z, t.hi.z);
   }
   CountTiled(nTiles);
   return tiles;
}

int MeshTiles::SelectRuns(const ModelViewProj& mvp, uint32_t first, uint32_t count, Run (&runs)[kMaxRuns])
{
   runs[0] = Run { first, count };
   const Range* range = nullptr;
   for (const Range& r : m_ranges)
      if (r.first == first)
      {
         range = &r;
         break;
      }
   if (!s_enabled || range == nullptr || count == 0)
      return count ? 1 : 0;

   // Fully in the view of an eye: all the tiles are seen
   for (unsigned int eye = 0; eye < mvp.m_nEyes; eye++)
   {
      const Matrix3D& m = mvp.GetModelViewProj(eye);
      bool inside = true;
      for (int i = 0; i < 8 && inside; i++)
      {
         const float x = (i & 1) ? m_hi.x : m_lo.x, y = (i & 2) ? m_hi.y : m_lo.y, z = (i & 4) ? m_hi.z : m_lo.z;
         const float xp = m._11 * x + m._21 * y + m._31 * z + m._41;
         const float yp = m._12 * x + m._22 * y + m._32 * z + m._42;
         const float wp = m._14 * x + m._24 * y + m._34 * z + m._44;
         inside = wp > 0.f && xp >= -wp && xp <= wp && yp >= -wp && yp <= wp;
      }
      if (inside)
      {
         CountFrame(count / 3, 0);
         return 1;
      }
   }

   for (size_t t = 0; t < m_tiles.size(); t++)
      m_visible[t] = mvp.IsBoxVisible(m_tiles[t].lo, m_tiles[t].hi) ? 1 : 0;

   // Runs of the seen tiles, from the start of the range up to 'count', merging those separated by few triangles; beyond kMaxRuns, the two runs
   // separated by the fewest triangles are merged
   int n = 0;
   uint32_t drawn = 0;
   for (const Entry& e : range->entries)
   {
      if (e.start >= count)
         break;
      if (!m_visible[e.tile])
         continue;
      const uint32_t start = first + e.start, end = first + min(e.start + e.count, count);
      if (n > 0 && start - (runs[n - 1].first + runs[n - 1].count) <= kMergeTriangles * 3)
      {
         runs[n - 1].count = end - runs[n - 1].first;
         continue;
      }
      if (n == kMaxRuns)
      {
         // Merge the closest two among the runs and this one
         int best = n - 1;
         uint32_t bestGap = start - (runs[n - 1].first + runs[n - 1].count);
         for (int r = 0; r + 1 < n; r++)
         {
            const uint32_t gap = runs[r + 1].first - (runs[r].first + runs[r].count);
            if (gap < bestGap)
            {
               bestGap = gap;
               best = r;
            }
         }
         if (best == n - 1)
         {
            runs[n - 1].count = end - runs[n - 1].first;
            continue;
         }
         runs[best].count = runs[best + 1].first + runs[best + 1].count - runs[best].first;
         for (int r = best + 1; r + 1 < n; r++)
            runs[r] = runs[r + 1];
         n--;
      }
      runs[n++] = Run { start, end - start };
   }
   for (int r = 0; r < n; r++)
      drawn += runs[r].count;
   CountFrame(drawn / 3, (count - drawn) / 3);
   return n;
}

void MeshTiles::ChooseLevels(const vector<float>& errors, const Matrix3D& fullMatrix, const ModelViewProj& mvp, float allowedPerDistance, bool orthographic,
   const float (*gazeTangents)[2], unsigned int frame, float coarserMargin)
{
   if (frame == m_levelsFrame)
      return;
   m_levelsFrame = frame;
   const size_t nTiles = m_tiles.size();
   if (m_tileLevel.size() != nTiles)
   {
      m_tileLevel.assign(nTiles, -1);
      m_drawLevel.assign(nTiles, -1);
   }

   // The boxes of the tiles on the table, for the placement of the part now
   if (!m_boxesValid || memcmp(&m_boxMatrix, &fullMatrix, sizeof(Matrix3D)) != 0)
   {
      m_boxesValid = true;
      m_boxMatrix = fullMatrix;
      m_tableLo.resize(nTiles);
      m_tableHi.resize(nTiles);
      for (size_t t = 0; t < nTiles; t++)
      {
         Vertex3Ds lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
         for (int i = 0; i < 8; i++)
         {
            const Vertex3Ds c = fullMatrix.MultiplyVectorNoPerspective(Vertex3Ds((i & 1) ? m_tiles[t].hi.x : m_tiles[t].lo.x, (i & 2) ? m_tiles[t].hi.y : m_tiles[t].lo.y,
               (i & 4) ? m_tiles[t].hi.z : m_tiles[t].lo.z));
            lo.x = min(lo.x, c.x); lo.y = min(lo.y, c.y); lo.z = min(lo.z, c.z);
            hi.x = max(hi.x, c.x); hi.y = max(hi.y, c.y); hi.z = max(hi.z, c.z);
         }
         m_tableLo[t] = lo;
         m_tableHi[t] = hi;
      }
   }

   const int nLevels = static_cast<int>(errors.size());
   std::array<bool, 128> used {};
   for (size_t t = 0; t < nTiles; t++)
   {
      if (!mvp.IsBoxVisible(m_tiles[t].lo, m_tiles[t].hi))
         continue; // Its level stays as it was, for when it is seen again
      const Vertex3Ds& lo = m_tableLo[t];
      const Vertex3Ds& hi = m_tableHi[t];
      // Distance from the closest eye to the closest point of the tile
      float distance = FLT_MAX;
      for (unsigned int eye = 0; eye < mvp.m_nEyes; eye++)
      {
         const vec4& c = mvp.GetCameraPos(eye);
         const float dx = max(max(lo.x - c.x, 0.f), c.x - hi.x), dy = max(max(lo.y - c.y, 0.f), c.y - hi.y), dz = max(max(lo.z - c.z, 0.f), c.z - hi.z);
         distance = min(distance, sqrtf(dx * dx + dy * dy + dz * dz));
      }
      float allowed = orthographic ? allowedPerDistance : allowedPerDistance * distance;
      // Away from the gaze: the angle between the gaze and the closest point of the tile's bounding sphere, for the eye that sees it closest to the gaze
      if (gazeTangents)
      {
         const Vertex3Ds center((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
         const float radius = 0.5f * sqrtf((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y) + (hi.z - lo.z) * (hi.z - lo.z));
         float angle = static_cast<float>(M_PI);
         for (unsigned int eye = 0; eye < mvp.m_nEyes && eye < 2; eye++)
         {
            const Vertex3Ds q = mvp.GetView(eye).MultiplyVectorNoPerspective(center);
            const float d = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z);
            if (d <= radius)
            {
               angle = 0.f;
               break;
            }
            // Direction of the center and of the gaze, in the view space of the eye (x right, y up, looking down -z in VR, whose projection is
            // right-handed, down +z on the desktop)
            const float forward = mvp.GetProj(eye)._34 < 0.f ? -1.f : 1.f;
            const float gx = gazeTangents[eye][0], gy = gazeTangents[eye][1];
            const float dot = (q.x * gx + q.y * gy + forward * q.z) / (d * sqrtf(gx * gx + gy * gy + 1.f));
            const float a = acosf(std::clamp(dot, -1.f, 1.f)) - asinf(radius / d);
            angle = min(angle, max(a, 0.f));
         }
         allowed *= std::clamp(1.f + (angle - kGazeFullAngle) / kGazeSlope, 1.f, kGazeMaxFactor);
      }
      int level = -1;
      for (int i = 0; i < nLevels && errors[i] <= allowed; i++)
         level = i;
      // Finer at once, coarser with a margin
      const int current = m_tileLevel[t];
      if (level > current)
      {
         int coarser = current;
         for (int i = current + 1; i <= level && errors[i] <= allowed * coarserMargin; i++)
            coarser = i;
         level = coarser;
      }
      m_tileLevel[t] = static_cast<int8_t>(level);
      used[static_cast<size_t>(level + 1)] = true;
   }

   // At most kMaxDrawnLevels levels drawn: the finest and the coarsest chosen, and those between, evenly spread; each tile draws the finest kept at or
   // under its own
   vector<int> kept;
   for (int l = -1; l < nLevels; l++)
      if (used[static_cast<size_t>(l + 1)])
         kept.push_back(l);
   if (kept.size() > kMaxDrawnLevels)
   {
      vector<int> spread;
      for (size_t i = 0; i < kMaxDrawnLevels; i++)
         spread.push_back(kept[i * (kept.size() - 1) / (kMaxDrawnLevels - 1)]);
      kept.swap(spread);
   }
   m_finestChosen = kept.empty() ? -1 : kept.front();
   for (size_t t = 0; t < nTiles; t++)
   {
      int draw = kept.empty() ? -1 : kept.front();
      for (const int l : kept)
         if (l <= m_tileLevel[t])
            draw = l;
      m_drawLevel[t] = static_cast<int8_t>(draw);
   }
}

int MeshTiles::SelectLevelRuns(const ModelViewProj& mvp, const MeshTiles& owner, const vector<uint32_t>& counts, const Run*& runs, size_t& drawnIndices)
{
   const size_t nTiles = m_tiles.size();
   // A layer: the owner's tile of each of its tiles (the tiles of both are sorted by cell)
   if (&owner != this && m_mappedOwner != &owner)
   {
      m_mappedOwner = &owner;
      m_ownerTile.assign(nTiles, -1);
      for (size_t t = 0; t < nTiles; t++)
      {
         const auto it = std::lower_bound(owner.m_tiles.begin(), owner.m_tiles.end(), m_tiles[t].cell, [](const Tile& a, uint64_t cell) { return a.cell < cell; });
         if (it != owner.m_tiles.end() && it->cell == m_tiles[t].cell)
            m_ownerTile[t] = static_cast<int>(it - owner.m_tiles.begin());
      }
   }
   // The tiles seen; with culling by tiles off, all of them, those out of the view at their last level
   for (size_t t = 0; t < nTiles; t++)
      m_visible[t] = !s_enabled || mvp.IsBoxVisible(m_tiles[t].lo, m_tiles[t].hi) ? 1 : 0;

   // In each range, the runs of the tiles drawing its level, merging those separated by few triangles of tiles not seen, never by triangles of a tile
   // seen at another level (it would be drawn twice)
   m_levelRuns.clear();
   drawnIndices = 0;
   uint32_t culledIndices = 0;
   for (size_t r = 0; r < m_ranges.size(); r++)
   {
      const Range& range = m_ranges[r];
      const int level = static_cast<int>(r) - 1;
      const uint32_t limit = r < counts.size() ? min(counts[r], range.count) : range.count;
      bool split = true; // The next run starts anew
      for (const Entry& e : range.entries)
      {
         if (e.start >= limit)
            break;
         const int ownerTile = &owner == this ? static_cast<int>(e.tile) : m_ownerTile[e.tile];
         const int tileLevel = ownerTile >= 0 && static_cast<size_t>(ownerTile) < owner.m_drawLevel.size() ? owner.m_drawLevel[ownerTile] : -1;
         if (!m_visible[e.tile])
         {
            if (tileLevel == level)
               culledIndices += min(e.start + e.count, limit) - e.start;
            continue;
         }
         if (tileLevel != level)
         {
            split = true;
            continue;
         }
         const uint32_t start = range.first + e.start, end = range.first + min(e.start + e.count, limit);
         if (!split && start - (m_levelRuns.back().first + m_levelRuns.back().count) <= kMergeTriangles * 3)
            m_levelRuns.back().count = end - m_levelRuns.back().first;
         else
            m_levelRuns.push_back(Run { start, end - start });
         split = false;
      }
   }
   for (const Run& run : m_levelRuns)
      drawnIndices += run.count;
   CountFrame(drawnIndices / 3, culledIndices / 3);
   runs = m_levelRuns.data();
   return static_cast<int>(m_levelRuns.size());
}

}
