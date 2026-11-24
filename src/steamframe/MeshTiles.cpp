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
#include <unordered_map>

namespace VPX
{

bool MeshTiles::s_enabled = true;
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

std::shared_ptr<MeshTiles> MeshTiles::Build(Primitive* part, const Mesh& mesh, const MeshLevels* levels, vector<unsigned int>& indices)
{
   const size_t nv = mesh.NumVertices();
   if (mesh.NumIndices() / 3 < kMinTriangles || !mesh.m_animationFrames.empty() || nv == 0 || part->m_ptable == nullptr)
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

   // The cell of each triangle, from its center on the table, in the grid of the first level whose cells are as long as the triangle (the grids start
   // at the top left corner of the playfield: the same cells for all parts)
   const PinTable* const table = part->m_ptable;
   const Matrix3D matrix = part->RecalculateMatrices();
   const float left = min(table->m_left, table->m_right), top = min(table->m_top, table->m_bottom);
   std::unordered_map<uint64_t, uint32_t> cellIds;
   vector<uint64_t> cellOf; // Cell of each triangle of each span, in order
   size_t totalTriangles = 0;
   for (const Span& s : spans)
      totalTriangles += s.count / 3;
   cellOf.reserve(totalTriangles);
   for (const Span& s : spans)
      for (uint32_t i = s.first; i + 2 < s.first + s.count; i += 3)
      {
         Vertex3Ds p[3];
         for (int k = 0; k < 3; k++)
         {
            const unsigned int vi = indices[i + k];
            if (vi >= nv)
               return nullptr;
            const Vertex3D_NoTex2& v = mesh.m_vertices[vi];
            p[k] = matrix.MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
         }
         const float cx = (p[0].x + p[1].x + p[2].x) / 3.f, cy = (p[0].y + p[1].y + p[2].y) / 3.f;
         const float length = max(max(p[0].x, max(p[1].x, p[2].x)) - min(p[0].x, min(p[1].x, p[2].x)), max(p[0].y, max(p[1].y, p[2].y)) - min(p[0].y, min(p[1].y, p[2].y)));
         int level = 0;
         while (level < kMaxLevel && !(length <= kTileSize * static_cast<float>(1 << level))) // Not finite: the coarsest
            level++;
         const float size = kTileSize * static_cast<float>(1 << level);
         const float fx = std::isfinite(cx) ? std::clamp((cx - left) / size, -1e6f, 1e6f) : 0.f;
         const float fy = std::isfinite(cy) ? std::clamp((cy - top) / size, -1e6f, 1e6f) : 0.f;
         const uint64_t key = CellKey(level, static_cast<int>(floorf(fx)), static_cast<int>(floorf(fy)));
         cellOf.push_back(key);
         cellIds.emplace(key, 0u);
      }
   if (cellIds.size() < 2)
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
   tiles->m_tiles.assign(nTiles, Tile { Vertex3Ds(FLT_MAX, FLT_MAX, FLT_MAX), Vertex3Ds(-FLT_MAX, -FLT_MAX, -FLT_MAX) });
   tiles->m_visible.resize(nTiles);

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

}
