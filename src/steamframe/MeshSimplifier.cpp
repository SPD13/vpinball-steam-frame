// license:GPLv3+

#include "core/stdafx.h"
#include "MeshSimplifier.h"

#include "core/player.h"
#include "parts/primitive.h"
#include "renderer/Renderer.h"
#include "renderer/IndexBuffer.h"
#include "ThreadPool.h"
#include "meshoptimizer/meshoptimizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace VPX
{

// Error bounds of the levels, in VP units (1 VPU = 0.54 mm). Each level is simplified from the previous one kept, within what is left of its
// bound, so that the error of a level is the sum of the steps. Steps of x1.41 up to 4 VPU, where the parts of the table are allowed 1 to 3
// VPU from the lockbar (x2 steps gave 0.1 ms less on Addams), then x2 for the far ones (rooms).
static constexpr float kBounds[] = { 0.5f, 0.71f, 1.f, 1.41f, 2.f, 2.83f, 4.f, 8.f, 16.f };
// A level is kept when it has at most 90 % of the triangles of the previous one (or of the mesh)
static constexpr float kKeepRatio = 0.9f;
// Meshes under this are not worth it
static constexpr size_t kMinTriangles = 500;
// Nor are levels under this
static constexpr size_t kMinLevelTriangles = 12;
// Weight of the normals and texture coordinates against the positions: 10 keeps thin round parts (wire guides) and the placement of
// printed artwork, where 1 visibly thins and shifts them (docs/Table Geometry on the Steam Frame.md, section 6)
static constexpr float kAttributeWeight = 10.f;
// The reflection probe is blurred and dim: it allows this much more
static constexpr float kProbeFactor = 2.f;
// A part goes to a coarser level only when that level is within this share of the allowed error, so that it does not switch back and forth
// at the distance where two levels meet
static constexpr float kCoarserMargin = 0.8f;
// Vertex positions of coinciding meshes differ by less than this, in VP units
static constexpr float kCoincidence = 0.25f;
// A mesh with this many vertices per triangle (a closed mesh that shares them has 0.5) is an export of separate triangles, usually each with its
// own face normal. The simplifier cannot collapse anything on it, as every vertex position then has more wedges than it can follow, and each of
// its triangles costs up to 3 vertex shader runs. Its levels are made from a welded copy (see Weld) and use only one vertex of each welded group.
static constexpr float kUnweldedVerticesPerTriangle = 1.5f;
// Welded vertices are within these of each other, in VP units and texture coordinates
static constexpr float kWeldPosition = 0.01f, kWeldUV = 0.0005f;
// Vertices with normals further apart than this stay apart: a hard edge of the mesh (cos 30 deg)
static constexpr float kWeldCrease = 0.866f;
// Change of the normal, in radians, that a level of a welded mesh may make when it uses all of its error bound
static constexpr float kWeldNormalChange = 0.15f;
// Options of the simplifier for a welded mesh. Permissive: it may collapse across seams of the texture, the attribute error above deciding. These
// exports are cut in many small islands of the texture, which would otherwise keep 4 to 5 times more triangles.
static constexpr unsigned int kWeldOptions = meshopt_SimplifyPermissive;
// Texture coordinates beyond this are garbage of the export (seen up to 1e24): kept out of the simplifier's error
static constexpr float kWeldMaxUV = 100.f;
// A layer (additive part or light map) lies on a part when at least this share of its vertices are at places of the part's vertices (within
// kCoincidence). Tables baked with light maps have clearly two kinds: at 100 % the layers of a part, under 40 % the neighbours that touch it.
static constexpr float kLayerShare = 0.5f;
// Vertices of each layer tested against the parts
static constexpr size_t kLayerSamples = 64;

struct MeshSimplifier::Group
{
   vector<Primitive*> parts; // The first one is simplified, the others (coinciding, not layers) share its levels
   size_t triangles = 0;
   vector<Primitive*> layers; // Layers lying on the first part, coinciding with it or made of some of its triangles: simplified with it
   Matrix3D matrix; // Placement on the table of the first part, then of each layer, taken when the table loads
   vector<Matrix3D> layerMatrices;
};

MeshSimplifier& MeshSimplifier::Get()
{
   static MeshSimplifier instance;
   return instance;
}

static bool IsGrouped(const Primitive* p)
{
   for (const Collection* collection : p->m_vCollection)
      if (collection->m_groupElements)
         return true;
   return false;
}

// Additive parts and light maps: drawn over the surface of another part, without writing depth
static bool IsLayer(const Primitive* p)
{
   return p->m_d.m_addBlend || !p->m_d.m_szLightmap.empty();
}

// Primitives whose mesh may get levels of its own
static bool IsCandidate(const Primitive* p)
{
   return p->m_d.m_use3DMesh && !p->IsPlayfield() && p->m_mesh.m_animationFrames.empty() && p->m_mesh.NumIndices() >= 3 * kMinTriangles && !IsGrouped(p);
}

// Grid of the places looked for, cells of twice the distance looked for: a place and the box around it touch at most 2 cells per axis
static int CellOf(float x)
{
   return static_cast<int>(floorf(x / (2.f * kCoincidence)));
}
static uint64_t CellKey(int x, int y, int z)
{
   return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) | (static_cast<uint64_t>(y & 0x1FFFFF) << 21) | static_cast<uint64_t>(z & 0x1FFFFF);
}

// Places of vertices: one id per exact position (meshoptimizer collapses the vertices at a position together)
static size_t PlacesOf(const float* pos, size_t stride, size_t count, vector<unsigned int>& place)
{
   vector<unsigned int> order(count);
   std::iota(order.begin(), order.end(), 0u);
   const auto at = [pos, stride](unsigned int i) { return std::tie(pos[i * stride], pos[i * stride + 1], pos[i * stride + 2]); };
   std::sort(order.begin(), order.end(), [&at](unsigned int a, unsigned int b) { return at(a) < at(b); });
   place.resize(count);
   size_t n = 0;
   for (size_t i = 0; i < count; i++)
   {
      if (i > 0 && at(order[i]) != at(order[i - 1]))
         n++;
      place[order[i]] = static_cast<unsigned int>(n);
   }
   return count ? n + 1 : 0;
}

// What the layers of a group cover of its part: for each triangle of the part, the layers drawn on it, each with its vertices at the triangle's corners
struct LayerCover
{
   struct Entry
   {
      unsigned int layer; // In Group::layers
      unsigned int v[3]; // Vertices of the layer at the corners of the part's triangle, in their order
   };
   vector<unsigned int> start; // Entries of triangle t of the part: start[t] to start[t + 1]
   vector<Entry> entries;
};

// Finds each triangle of each layer among the triangles of the part, by the places of their vertices on the table (within kCoincidence, winding
// kept). False when a layer has a triangle the part does not have: the layer is not made of the part's triangles.
static bool CoverOfLayers(const Primitive* part, const Matrix3D& matrix, const vector<Primitive*>& layers, const vector<Matrix3D>& layerMatrices, LayerCover& cover)
{
   const Mesh& mesh = part->m_mesh;
   const size_t nv = mesh.NumVertices(), nt = mesh.NumIndices() / 3;
   vector<unsigned int> place;
   const size_t nPlaces = PlacesOf(&mesh.m_vertices.data()->x, sizeof(Vertex3D_NoTex2) / sizeof(float), nv, place);
   vector<Vertex3Ds> placePos(nPlaces);
   for (size_t i = 0; i < nv; i++)
   {
      const Vertex3D_NoTex2& v = mesh.m_vertices[i];
      placePos[place[i]] = matrix.MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
   }
   std::unordered_map<uint64_t, vector<unsigned int>> grid;
   for (size_t i = 0; i < nPlaces; i++)
      if (std::isfinite(placePos[i].x) && std::isfinite(placePos[i].y) && std::isfinite(placePos[i].z))
         grid[CellKey(CellOf(placePos[i].x), CellOf(placePos[i].y), CellOf(placePos[i].z))].push_back(static_cast<unsigned int>(i));

   // The triangles of the part by their places, from the smallest one
   struct Key
   {
      unsigned int a, b, c;
      bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; }
   };
   struct KeyHash
   {
      size_t operator()(const Key& k) const { return (static_cast<size_t>(k.a) * 0x9E3779B97F4A7C15ull) ^ (static_cast<size_t>(k.b) * 0xC2B2AE3D27D4EB4Full) ^ k.c; }
   };
   const auto keyOf = [](unsigned int a, unsigned int b, unsigned int c)
   {
      return (a < b && a < c) ? Key { a, b, c } : (b < c) ? Key { b, c, a } : Key { c, a, b };
   };
   std::unordered_map<Key, unsigned int, KeyHash> triangleOf;
   triangleOf.reserve(nt);
   for (size_t t = 0; t < nt; t++)
   {
      const unsigned int i0 = mesh.m_indices[t * 3], i1 = mesh.m_indices[t * 3 + 1], i2 = mesh.m_indices[t * 3 + 2];
      if (i0 >= nv || i1 >= nv || i2 >= nv)
         return false;
      const unsigned int p0 = place[i0], p1 = place[i1], p2 = place[i2];
      if (p0 != p1 && p1 != p2 && p0 != p2)
         triangleOf.emplace(keyOf(p0, p1, p2), static_cast<unsigned int>(t));
   }

   // Layers placed as the part (baked light maps are) have their vertices at the very positions of the part's: found without the grid
   const auto bitsOf = [](const Vertex3D_NoTex2& v)
   {
      Key k;
      memcpy(&k.a, &v.x, sizeof(float));
      memcpy(&k.b, &v.y, sizeof(float));
      memcpy(&k.c, &v.z, sizeof(float));
      return k;
   };
   std::unordered_map<Key, unsigned int, KeyHash> exactPlace;

   vector<std::pair<unsigned int, LayerCover::Entry>> found;
   for (size_t l = 0; l < layers.size(); l++)
   {
      const Mesh& layer = layers[l]->m_mesh;
      const size_t lnv = layer.NumVertices();
      vector<int> layerPlace(lnv, -1);
      const bool samePlacement = memcmp(&layerMatrices[l], &matrix, sizeof(Matrix3D)) == 0;
      if (samePlacement && exactPlace.empty())
      {
         exactPlace.reserve(nPlaces);
         for (size_t i = 0; i < nv; i++)
            exactPlace.emplace(bitsOf(mesh.m_vertices[i]), place[i]);
      }
      for (size_t i = 0; i < lnv; i++)
      {
         const Vertex3D_NoTex2& v = layer.m_vertices[i];
         if (const auto exact = samePlacement ? exactPlace.find(bitsOf(v)) : exactPlace.end(); exact != exactPlace.end())
         {
            layerPlace[i] = static_cast<int>(exact->second);
            continue;
         }
         const Vertex3Ds p = layerMatrices[l].MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
         if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            continue;
         float best = FLT_MAX;
         for (int x = CellOf(p.x - kCoincidence); x <= CellOf(p.x + kCoincidence); x++)
            for (int y = CellOf(p.y - kCoincidence); y <= CellOf(p.y + kCoincidence); y++)
               for (int z = CellOf(p.z - kCoincidence); z <= CellOf(p.z + kCoincidence); z++)
               {
                  const auto cell = grid.find(CellKey(x, y, z));
                  if (cell == grid.end())
                     continue;
                  for (const unsigned int id : cell->second)
                  {
                     const Vertex3Ds& q = placePos[id];
                     const float dx = fabsf(q.x - p.x), dy = fabsf(q.y - p.y), dz = fabsf(q.z - p.z);
                     if (dx <= kCoincidence && dy <= kCoincidence && dz <= kCoincidence && dx + dy + dz < best)
                     {
                        best = dx + dy + dz;
                        layerPlace[i] = static_cast<int>(id);
                     }
                  }
               }
      }
      for (size_t i = 0; i + 2 < layer.NumIndices(); i += 3)
      {
         const unsigned int a = layer.m_indices[i], b = layer.m_indices[i + 1], c = layer.m_indices[i + 2];
         if (a >= lnv || b >= lnv || c >= lnv || layerPlace[a] < 0 || layerPlace[b] < 0 || layerPlace[c] < 0)
            return false;
         const unsigned int pa = layerPlace[a], pb = layerPlace[b], pc = layerPlace[c];
         if (pa == pb || pb == pc || pa == pc)
            continue; // Draws nothing
         const auto t = triangleOf.find(keyOf(pa, pb, pc));
         if (t == triangleOf.end())
            return false;
         LayerCover::Entry e { static_cast<unsigned int>(l), { 0, 0, 0 } };
         for (int k = 0; k < 3; k++)
         {
            const unsigned int pk = place[mesh.m_indices[t->second * 3 + k]];
            e.v[k] = pk == pa ? a : pk == pb ? b : c;
         }
         found.emplace_back(t->second, e);
      }
   }

   // By triangle, then layer (a layer drawing a triangle twice counts once)
   std::stable_sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
   cover.start.assign(nt + 1, 0);
   cover.entries.clear();
   cover.entries.reserve(found.size());
   for (size_t i = 0; i < found.size(); i++)
   {
      if (i > 0 && found[i].first == found[i - 1].first && found[i].second.layer == found[i - 1].second.layer)
         continue;
      cover.entries.push_back(found[i].second);
      cover.start[found[i].first + 1]++;
   }
   for (size_t t = 0; t < nt; t++)
      cover.start[t + 1] += cover.start[t];
   return true;
}

// The vertices given to the simplifier for a part with layers: a copy of a vertex for each set of layer vertices it meets (the layers on the triangle
// and their texture coordinates at the corner), so that a simplified triangle tells which layers draw it and with which of their vertices. The
// places where the triangles do not all meet the same set (the edges of a layer on the part, the seams of a layer's texture) are seams for the
// simplifier: their copies stay apart, so that a simplified triangle never mixes two sets, but a seam may get shorter, collapsing along itself.
// meshoptimizer keeps the seams of a mesh by itself; a welded mesh, simplified with the Permissive option that crosses the seams of its own
// texture, gets meshopt_SimplifyVertex_Protect there. Locking those places instead kept most of the triangles of parts with many light maps
// (Medieval Madness, 2026-11-03: 1.27 M triangles left at 1 VPU against 1.18 M, and 779 k against 463 k at 16 VPU).
struct LayerSplit
{
   vector<unsigned int> source; // Vertex of the simplifier's input that each copy repeats
   vector<unsigned char> lock; // meshopt_SimplifyVertex_* of each copy
   vector<unsigned int> start; // Layer vertices of copy s: start[s] to start[s + 1] in 'layer' and 'vertex', by layer
   vector<unsigned int> layer, vertex;
};

static void SplitForLayers(const LayerCover& cover, const vector<Primitive*>& layers, const vector<unsigned int>* triangles, const vector<float>& pos,
   vector<unsigned int>& indices, LayerSplit& split)
{
   vector<unsigned int> place;
   const size_t nPlaces = PlacesOf(pos.data(), 3, pos.size() / 3, place);
   struct SetHash
   {
      size_t operator()(const vector<uint32_t>& v) const
      {
         return std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(uint32_t)));
      }
   };
   std::unordered_map<vector<uint32_t>, unsigned int, SetHash> sets;
   vector<uint32_t> set;
   vector<unsigned int> placeSet(nPlaces, ~0u);
   vector<unsigned char> placeLocked(nPlaces, 0);
   std::unordered_map<uint64_t, unsigned int> copyOf;
   copyOf.reserve(pos.size() / 3);
   split.source.clear();
   split.layer.clear();
   split.vertex.clear();
   split.start.assign(1, 0);
   for (size_t j = 0; j + 2 < indices.size(); j += 3)
   {
      const size_t t = triangles ? (*triangles)[j / 3] : j / 3;
      for (int k = 0; k < 3; k++)
      {
         const unsigned int v = indices[j + k];
         set.clear();
         for (unsigned int e = cover.start[t]; e < cover.start[t + 1]; e++)
         {
            const LayerCover::Entry& entry = cover.entries[e];
            const Vertex3D_NoTex2& lv = layers[entry.layer]->m_mesh.m_vertices[entry.v[k]];
            uint32_t u, w;
            memcpy(&u, &lv.tu, sizeof(u));
            memcpy(&w, &lv.tv, sizeof(w));
            set.push_back(entry.layer);
            set.push_back(u);
            set.push_back(w);
         }
         const unsigned int setId = sets.try_emplace(set, static_cast<unsigned int>(sets.size())).first->second;
         const unsigned int p = place[v];
         if (placeSet[p] == ~0u)
            placeSet[p] = setId;
         else if (placeSet[p] != setId)
            placeLocked[p] = 1;
         const auto [copy, added] = copyOf.try_emplace((static_cast<uint64_t>(v) << 32) | setId, static_cast<unsigned int>(split.source.size()));
         if (added)
         {
            split.source.push_back(v);
            for (unsigned int e = cover.start[t]; e < cover.start[t + 1]; e++)
            {
               split.layer.push_back(cover.entries[e].layer);
               split.vertex.push_back(cover.entries[e].v[k]);
            }
            split.start.push_back(static_cast<unsigned int>(split.layer.size()));
         }
         indices[j + k] = copy->second;
      }
   }
   split.lock.resize(split.source.size());
   for (size_t s = 0; s < split.source.size(); s++)
      split.lock[s] = placeLocked[place[split.source[s]]] && triangles ? meshopt_SimplifyVertex_Protect : 0;
}

// The parts are placed the same way and their vertices (scaled by their size) are at the same places
static bool Coincide(const Primitive* a, const Primitive* b)
{
   const auto close = [](float x, float y) { return fabsf(x - y) <= 1e-3f * max(1.f, max(fabsf(x), fabsf(y))); };
   if (!close(a->m_d.m_vPosition.x, b->m_d.m_vPosition.x) || !close(a->m_d.m_vPosition.y, b->m_d.m_vPosition.y) || !close(a->m_d.m_vPosition.z, b->m_d.m_vPosition.z)
      || !close(a->m_d.m_vSize.x, b->m_d.m_vSize.x) || !close(a->m_d.m_vSize.y, b->m_d.m_vSize.y) || !close(a->m_d.m_vSize.z, b->m_d.m_vSize.z))
      return false;
   for (int i = 0; i < 9; i++)
      if (!close(a->m_d.m_aRotAndTra[i], b->m_d.m_aRotAndTra[i]))
         return false;
   const Vertex3Ds& s = a->m_d.m_vSize;
   const vector<Vertex3D_NoTex2>& va = a->m_mesh.m_vertices;
   const vector<Vertex3D_NoTex2>& vb = b->m_mesh.m_vertices;
   for (size_t i = 0; i < va.size(); i++)
      if (fabsf((va[i].x - vb[i].x) * s.x) > kCoincidence || fabsf((va[i].y - vb[i].y) * s.y) > kCoincidence || fabsf((va[i].z - vb[i].z) * s.z) > kCoincidence)
         return false;
   return true;
}

// Builds the welded copy of a mesh: 'pos' and 'attr' hold on return one vertex per group of mesh vertices at the same place with the same texture
// coordinates and normals within kWeldCrease, its normal the average of the group. 'indices' is the mesh over these, without the triangles that
// vanish, and 'toMesh' gives for each the vertex of the mesh that draws it: the one of the group with the closest normal. As the simplifier compares
// attributes with the positions scaled to the mesh extent, 'normalWeight' (to divide by the mesh extent and multiply by a level's bound) and
// 'uvWeight' make the weights that do not depend on the size of the mesh: a texture coordinate may then shift the artwork by the level's bound on
// the surface, whatever the size of the part. 'triangles' gives for each triangle of 'indices' the triangle of the mesh it comes from. Returns
// false for a mesh that holds invalid values, which is then simplified as it is.
static bool Weld(const vector<unsigned int>& meshIndices, vector<float>& pos, vector<float>& attr, vector<unsigned int>& indices, vector<unsigned int>& toMesh,
   vector<unsigned int>& triangles, float& normalWeight, float& uvWeight)
{
   const size_t nv = pos.size() / 3;
   struct Key
   {
      int64_t x, y, z;
      int32_t u, v;
      unsigned int id;
   };
   vector<Key> keys(nv);
   for (size_t i = 0; i < nv; i++)
   {
      if (!std::isfinite(pos[i * 3]) || !std::isfinite(pos[i * 3 + 1]) || !std::isfinite(pos[i * 3 + 2]) || !std::isfinite(attr[i * 5]) || !std::isfinite(attr[i * 5 + 1]) || !std::isfinite(attr[i * 5 + 2]))
         return false;
      float& u = attr[i * 5 + 3];
      float& v = attr[i * 5 + 4];
      u = std::isfinite(u) ? clamp(u, -kWeldMaxUV, kWeldMaxUV) : 0.f;
      v = std::isfinite(v) ? clamp(v, -kWeldMaxUV, kWeldMaxUV) : 0.f;
      keys[i] = Key { std::llround(pos[i * 3] / kWeldPosition), std::llround(pos[i * 3 + 1] / kWeldPosition), std::llround(pos[i * 3 + 2] / kWeldPosition),
         static_cast<int32_t>(std::lround(u / kWeldUV)), static_cast<int32_t>(std::lround(v / kWeldUV)), static_cast<unsigned int>(i) };
   }
   std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return std::tie(a.x, a.y, a.z, a.u, a.v, a.id) < std::tie(b.x, b.y, b.z, b.u, b.v, b.id); });

   const auto normal = [&attr](unsigned int i)
   {
      Vertex3Ds n(attr[i * 5], attr[i * 5 + 1], attr[i * 5 + 2]);
      n.NormalizeSafe();
      return n;
   };
   vector<unsigned int> group(nv); // The welded vertex of each vertex of the mesh
   vector<Vertex3Ds> sum; // Sum of the normals of each welded vertex
   vector<Vertex3Ds> first; // Normal of its first vertex, what the others are compared to
   for (size_t a = 0, b; a < nv; a = b)
   {
      b = a + 1;
      while (b < nv && std::tie(keys[a].x, keys[a].y, keys[a].z, keys[a].u, keys[a].v) == std::tie(keys[b].x, keys[b].y, keys[b].z, keys[b].u, keys[b].v))
         b++;
      const size_t base = sum.size();
      for (size_t i = a; i < b; i++)
      {
         const Vertex3Ds n = normal(keys[i].id);
         size_t g = base;
         while (g < sum.size() && first[g].Dot(n) < kWeldCrease)
            g++;
         if (g == sum.size())
         {
            sum.push_back(Vertex3Ds(0.f, 0.f, 0.f));
            first.push_back(n);
         }
         sum[g] += n;
         group[keys[i].id] = static_cast<unsigned int>(g);
      }
   }

   const size_t nw = sum.size();
   vector<float> weldedPos(nw * 3), weldedAttr(nw * 5), closest(nw, -2.f);
   toMesh.resize(nw);
   for (Vertex3Ds& n : sum)
      n.NormalizeSafe();
   for (size_t i = 0; i < nv; i++)
   {
      const unsigned int g = group[i];
      const float d = sum[g].Dot(normal(static_cast<unsigned int>(i)));
      if (d <= closest[g])
         continue;
      closest[g] = d;
      toMesh[g] = static_cast<unsigned int>(i);
      std::copy_n(&pos[i * 3], 3, &weldedPos[g * 3]);
      weldedAttr[g * 5 + 0] = sum[g].x;
      weldedAttr[g * 5 + 1] = sum[g].y;
      weldedAttr[g * 5 + 2] = sum[g].z;
      weldedAttr[g * 5 + 3] = attr[i * 5 + 3];
      weldedAttr[g * 5 + 4] = attr[i * 5 + 4];
   }

   // Size of a texture on the surface, in VP units per unit of texture coordinates: the median over the triangles, as a few have garbage ones
   vector<float> scales;
   scales.reserve(meshIndices.size() / 3);
   vector<unsigned int> weldedIndices, weldedTriangles;
   weldedIndices.reserve(meshIndices.size());
   weldedTriangles.reserve(meshIndices.size() / 3);
   for (size_t i = 0; i + 2 < meshIndices.size(); i += 3)
   {
      const unsigned int i0 = meshIndices[i], i1 = meshIndices[i + 1], i2 = meshIndices[i + 2];
      const unsigned int g0 = group[i0], g1 = group[i1], g2 = group[i2];
      if (g0 == g1 || g1 == g2 || g0 == g2)
         continue;
      weldedIndices.push_back(g0);
      weldedIndices.push_back(g1);
      weldedIndices.push_back(g2);
      weldedTriangles.push_back(static_cast<unsigned int>(i / 3));
      const float* const p0 = &pos[i0 * 3], * const p1 = &pos[i1 * 3], * const p2 = &pos[i2 * 3];
      const Vertex3Ds e1(p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]), e2(p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]);
      const float* const t0 = &attr[i0 * 5 + 3], * const t1 = &attr[i1 * 5 + 3], * const t2 = &attr[i2 * 5 + 3];
      const float uvArea = fabsf((t1[0] - t0[0]) * (t2[1] - t0[1]) - (t1[1] - t0[1]) * (t2[0] - t0[0]));
      if (uvArea > 1e-10f && fabsf(t0[0]) < kWeldMaxUV && fabsf(t0[1]) < kWeldMaxUV && fabsf(t1[0]) < kWeldMaxUV && fabsf(t1[1]) < kWeldMaxUV && fabsf(t2[0]) < kWeldMaxUV && fabsf(t2[1]) < kWeldMaxUV)
         scales.push_back(sqrtf(CrossProduct(e1, e2).Length() / uvArea));
   }
   if (weldedIndices.size() < 3 * kMinLevelTriangles)
      return false;

   Vertex3Ds lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
   for (size_t i = 0; i < nw; i++)
   {
      lo.x = min(lo.x, weldedPos[i * 3]); lo.y = min(lo.y, weldedPos[i * 3 + 1]); lo.z = min(lo.z, weldedPos[i * 3 + 2]);
      hi.x = max(hi.x, weldedPos[i * 3]); hi.y = max(hi.y, weldedPos[i * 3 + 1]); hi.z = max(hi.z, weldedPos[i * 3 + 2]);
   }
   const float extent = max(max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z), 1e-6f);
   normalWeight = 1.f / (extent * kWeldNormalChange);
   if (scales.empty())
      uvWeight = 0.f;
   else
   {
      std::nth_element(scales.begin(), scales.begin() + scales.size() / 2, scales.end());
      uvWeight = scales[scales.size() / 2] / extent;
   }
   pos.swap(weldedPos);
   attr.swap(weldedAttr);
   indices.swap(weldedIndices);
   triangles.swap(weldedTriangles);
   return true;
}

// A layer pass only draws where it is not behind the depth of the part it lies on, so that it needs the very surface of that part: a layer lying on
// a simplified part is simplified with it (Simplify). Finds the layers lying on each group's part, comparing the layers' vertices with the vertices
// of the parts on the table, as a layer and its part need not be placed the same way. Small parts are not concerned, but small layers are: all the
// layers of the table are looked at. A layer that lies on two parts, or that cannot draw a level (animated, drawn with its group), leaves them out.
void MeshSimplifier::AttachLayers(vector<std::shared_ptr<Group>>& groups, const vector<Primitive*>& layers)
{
   // The layers coinciding with a part are simplified with it as the others
   for (const auto& g : groups)
   {
      g->matrix = g->parts[0]->RecalculateMatrices();
      for (auto p = g->parts.begin() + 1; p != g->parts.end();)
         if (IsLayer(*p))
         {
            g->layers.push_back(*p);
            p = g->parts.erase(p);
         }
         else
            ++p;
   }
   m_nLayers = static_cast<int>(layers.size());
   if (layers.empty() || groups.empty())
      return;
   const auto t0 = std::chrono::steady_clock::now();

   struct Sample
   {
      Vertex3Ds pos;
      unsigned int layer;
   };
   vector<Sample> samples;
   vector<unsigned int> samplesOfLayer(layers.size(), 0);
   vector<Matrix3D> layerMatrices(layers.size());
   std::unordered_map<const Primitive*, unsigned int> indexOf;
   std::unordered_map<uint64_t, vector<unsigned int>> grid;
   Vertex3Ds lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
   for (size_t l = 0; l < layers.size(); l++)
   {
      indexOf[layers[l]] = static_cast<unsigned int>(l);
      layerMatrices[l] = layers[l]->RecalculateMatrices();
      const Mesh& mesh = layers[l]->m_mesh;
      const size_t step = max(mesh.NumIndices() / kLayerSamples, static_cast<size_t>(1));
      for (size_t i = 0; i < mesh.NumIndices(); i += step)
      {
         if (mesh.m_indices[i] >= mesh.NumVertices())
            continue;
         const Vertex3D_NoTex2& v = mesh.m_vertices[mesh.m_indices[i]];
         const Vertex3Ds pos = layerMatrices[l].MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
         if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
            continue;
         const unsigned int id = static_cast<unsigned int>(samples.size());
         samples.push_back(Sample { pos, static_cast<unsigned int>(l) });
         samplesOfLayer[l]++;
         lo.x = min(lo.x, pos.x); lo.y = min(lo.y, pos.y); lo.z = min(lo.z, pos.z);
         hi.x = max(hi.x, pos.x); hi.y = max(hi.y, pos.y); hi.z = max(hi.z, pos.z);
         for (int x = CellOf(pos.x - kCoincidence); x <= CellOf(pos.x + kCoincidence); x++)
            for (int y = CellOf(pos.y - kCoincidence); y <= CellOf(pos.y + kCoincidence); y++)
               for (int z = CellOf(pos.z - kCoincidence); z <= CellOf(pos.z + kCoincidence); z++)
                  grid[CellKey(x, y, z)].push_back(id);
      }
   }
   lo.x -= kCoincidence; lo.y -= kCoincidence; lo.z -= kCoincidence;
   hi.x += kCoincidence; hi.y += kCoincidence; hi.z += kCoincidence;

   // The groups each layer lies on
   vector<vector<unsigned int>> groupsOf(layers.size());
   vector<char> coinciding(layers.size(), 0);
   for (size_t gi = 0; gi < groups.size(); gi++)
      for (const Primitive* layer : groups[gi]->layers)
      {
         const unsigned int l = indexOf[layer];
         groupsOf[l].push_back(static_cast<unsigned int>(gi));
         coinciding[l] = 1;
      }
   vector<unsigned int> hits(layers.size(), 0), touched;
   vector<unsigned int> seenBy(samples.size(), ~0u);
   for (size_t gi = 0; gi < groups.size(); gi++)
   {
      const Primitive* const part = groups[gi]->parts[0];
      const Matrix3D& matrix = groups[gi]->matrix;
      touched.clear();
      for (const Vertex3D_NoTex2& v : part->m_mesh.m_vertices)
      {
         const Vertex3Ds pos = matrix.MultiplyVectorNoPerspective(Vertex3Ds(v.x, v.y, v.z));
         if (!(pos.x >= lo.x && pos.x <= hi.x && pos.y >= lo.y && pos.y <= hi.y && pos.z >= lo.z && pos.z <= hi.z))
            continue;
         const auto cell = grid.find(CellKey(CellOf(pos.x), CellOf(pos.y), CellOf(pos.z)));
         if (cell == grid.end())
            continue;
         for (const unsigned int id : cell->second)
         {
            const Sample& sample = samples[id];
            if (seenBy[id] == gi || fabsf(sample.pos.x - pos.x) > kCoincidence || fabsf(sample.pos.y - pos.y) > kCoincidence || fabsf(sample.pos.z - pos.z) > kCoincidence)
               continue;
            seenBy[id] = static_cast<unsigned int>(gi);
            if (hits[sample.layer]++ == 0)
               touched.push_back(sample.layer);
         }
      }
      for (const unsigned int l : touched)
      {
         if (static_cast<float>(hits[l]) >= kLayerShare * static_cast<float>(samplesOfLayer[l])
            && std::find(groupsOf[l].begin(), groupsOf[l].end(), static_cast<unsigned int>(gi)) == groupsOf[l].end())
            groupsOf[l].push_back(static_cast<unsigned int>(gi));
         hits[l] = 0;
      }
   }

   vector<char> leaveOut(groups.size(), 0);
   for (size_t l = 0; l < layers.size(); l++)
   {
      if (groupsOf[l].empty())
         continue;
      if (groupsOf[l].size() > 1 || !layers[l]->m_mesh.m_animationFrames.empty() || IsGrouped(layers[l]))
      {
         for (const unsigned int gi : groupsOf[l])
            leaveOut[gi] = 1;
         continue;
      }
      if (!coinciding[l])
         groups[groupsOf[l][0]]->layers.push_back(layers[l]);
   }
   size_t gi = 0;
   std::erase_if(groups,
      [&](const std::shared_ptr<Group>& g)
      {
         if (!leaveOut[gi++])
         {
            for (const Primitive* layer : g->layers)
               g->layerMatrices.push_back(layerMatrices[indexOf[layer]]);
            return false;
         }
         m_nUnderLayers += static_cast<int>(g->parts.size());
         m_trisUnderLayers += g->triangles * g->parts.size();
         return true;
      });
   m_layersMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void MeshSimplifier::Enqueue(progschj::ThreadPool& pool, PinTable* table)
{
   m_enabled = table->GetSettings().GetPlayer_SimplifyMeshes();
   m_allowedPixels = table->GetSettings().GetPlayer_SimplifyMeshesError();
   m_nMeshes = m_nShared = m_nCandidates = m_nCandidatesDone = 0;
   m_nLayers = m_nLayersWithPart = m_nUnderLayers = m_nUnmatched = 0;
   m_trisUnderLayers = m_trisLayersWithPart = 0;
   m_layersMs = 0.;
   m_savedShare = NAN;
   m_windowSaved = m_windowTriangles = m_frameSaved = 0;
   m_windowStart = std::chrono::steady_clock::now();
   m_trisIn = 0;
   m_trisPerLevel.assign(std::size(kBounds), 0);
   m_cpuMs = 0.;
   m_start = std::chrono::steady_clock::now();
   m_end = m_start;

   vector<Primitive*> candidates, layers;
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemPrimitive)
         continue;
      Primitive* const p = static_cast<Primitive*>(part);
      p->m_meshLevels = nullptr;
      p->m_meshLevel = -1;
      if (p->m_d.m_use3DMesh && p->m_mesh.NumIndices() >= 3 && IsLayer(p))
         layers.push_back(p);
      if (IsCandidate(p))
         candidates.push_back(p);
   }
   m_nCandidates = static_cast<int>(candidates.size());
   if (!m_enabled)
      return;

   // Coinciding meshes: same index list, then same places
   vector<std::shared_ptr<Group>> groups;
   std::unordered_map<size_t, vector<Group*>> byIndices;
   for (Primitive* p : candidates)
   {
      const std::string_view bytes(reinterpret_cast<const char*>(p->m_mesh.m_indices.data()), p->m_mesh.NumIndices() * sizeof(unsigned int));
      const size_t key = std::hash<std::string_view>()(bytes) ^ (p->m_mesh.NumVertices() * 0x9E3779B97F4A7C15ull);
      vector<Group*>& bucket = byIndices[key];
      Group* group = nullptr;
      for (Group* g : bucket)
         if (g->parts[0]->m_mesh.m_indices == p->m_mesh.m_indices && g->parts[0]->m_mesh.NumVertices() == p->m_mesh.NumVertices() && Coincide(g->parts[0], p))
         {
            group = g;
            break;
         }
      if (group == nullptr)
      {
         groups.push_back(std::make_shared<Group>());
         group = groups.back().get();
         group->triangles = p->m_mesh.NumIndices() / 3;
         bucket.push_back(group);
      }
      group->parts.push_back(p);
   }

   // Additive parts and light maps are only simplified with a part that is not one, under them: alone, they lie on top of another part's surface,
   // which would not be simplified the same way
   std::erase_if(groups,
      [](const std::shared_ptr<Group>& g)
      {
         const auto base = std::find_if(g->parts.begin(), g->parts.end(), [](const Primitive* p) { return !IsLayer(p); });
         if (base == g->parts.end())
            return true;
         std::iter_swap(g->parts.begin(), base);
         return false;
      });
   AttachLayers(groups, layers);

   // The largest first, so that they do not end the loading alone
   std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a->triangles > b->triangles; });
   for (const auto& group : groups)
      pool.enqueue_block([this, group]() { Simplify(*group); });
}

void MeshSimplifier::Simplify(const Group& group)
{
   const auto t0 = std::chrono::steady_clock::now();
   Primitive* const p = group.parts[0];
   const Mesh& mesh = p->m_mesh;
   const size_t nv = mesh.NumVertices();
   const size_t tris = mesh.NumIndices() / 3;

   // Positions scaled by the part's size, so that the error is a distance on the table
   vector<float> pos(nv * 3), attr(nv * 5);
   for (size_t i = 0; i < nv; i++)
   {
      const Vertex3D_NoTex2& v = mesh.m_vertices[i];
      pos[i * 3 + 0] = v.x * p->m_d.m_vSize.x;
      pos[i * 3 + 1] = v.y * p->m_d.m_vSize.y;
      pos[i * 3 + 2] = v.z * p->m_d.m_vSize.z;
      attr[i * 5 + 0] = v.nx;
      attr[i * 5 + 1] = v.ny;
      attr[i * 5 + 2] = v.nz;
      attr[i * 5 + 3] = v.tu;
      attr[i * 5 + 4] = v.tv;
   }
   float weights[5] = { kAttributeWeight, kAttributeWeight, kAttributeWeight, kAttributeWeight, kAttributeWeight };

   // Exports of separate triangles: the levels come from a welded copy of the mesh
   vector<unsigned int> indices(mesh.m_indices), toMesh, triangles;
   float normalWeight = 0.f, uvWeight = 0.f;
   const bool welded = static_cast<float>(nv) >= kUnweldedVerticesPerTriangle * static_cast<float>(tris) && Weld(mesh.m_indices, pos, attr, indices, toMesh, triangles, normalWeight, uvWeight);
   if (welded)
      weights[3] = weights[4] = uvWeight;

   // Layers on the part: the simplifier gets a copy of a vertex per set of layer vertices it meets, and the edges of the layers are seams
   const auto leaveOut = [this, &group, tris, t0]()
   {
      const std::lock_guard<std::mutex> lock(m_statsMutex);
      m_cpuMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      m_end = std::chrono::steady_clock::now();
      m_nUnmatched++;
      m_nUnderLayers += static_cast<int>(group.parts.size());
      m_trisUnderLayers += tris * group.parts.size();
   };
   const bool withLayers = !group.layers.empty();
   LayerSplit split;
   if (withLayers)
   {
      LayerCover cover;
      if (!CoverOfLayers(p, group.matrix, group.layers, group.layerMatrices, cover))
      {
         leaveOut();
         return;
      }
      SplitForLayers(cover, group.layers, welded ? &triangles : nullptr, pos, indices, split);
      vector<float> splitPos(split.source.size() * 3), splitAttr(split.source.size() * 5);
      for (size_t s = 0; s < split.source.size(); s++)
      {
         std::copy_n(&pos[split.source[s] * 3], 3, &splitPos[s * 3]);
         std::copy_n(&attr[split.source[s] * 5], 5, &splitAttr[s * 5]);
      }
      pos.swap(splitPos);
      attr.swap(splitAttr);
   }
   const size_t nSimplified = pos.size() / 3;

   auto levels = std::make_shared<MeshLevels>();
   levels->loadSize = p->m_d.m_vSize;
   levels->sourceIndices = mesh.NumIndices();
   levels->sourceVertices = nv;
   // The simplifier works on all the vertices it is given: each level is simplified from the vertices the previous one still uses ('local'
   // indices over 'ids', the vertices given to the simplifier), which makes the coarser levels cheap
   vector<unsigned int> ids(nSimplified), local(indices);
   for (size_t i = 0; i < nSimplified; i++)
      ids[i] = static_cast<unsigned int>(i);
   vector<float> localPos = pos, localAttr = attr;
   vector<unsigned char> localLock = split.lock;
   vector<int> remap(nSimplified);
   size_t sourceCount = indices.size();
   float sourceError = 0.f;
   vector<unsigned int> dst(sourceCount);
   for (const float bound : kBounds)
   {
      if (bound <= sourceError)
         continue;
      if (welded)
         weights[0] = weights[1] = weights[2] = normalWeight * bound;
      float stepError = 0.f;
      const size_t n = meshopt_simplifyWithAttributes(dst.data(), local.data(), sourceCount, localPos.data(), ids.size(), sizeof(float) * 3, localAttr.data(),
         sizeof(float) * 5, weights, 5, withLayers ? localLock.data() : nullptr, 3, bound - sourceError, meshopt_SimplifyErrorAbsolute | (welded ? kWeldOptions : 0), &stepError);
      if (n < 3 * kMinLevelTriangles)
         break;
      if (static_cast<float>(n) > kKeepRatio * static_cast<float>(sourceCount))
         continue;
      MeshLevels::Level& level = levels->levels.emplace_back();
      level.indices.resize(n);
      for (size_t i = 0; i < n; i++)
         level.indices[i] = ids[dst[i]];
      level.error = sourceError + stepError;
      sourceCount = n;
      sourceError = level.error;

      // The vertices this level uses, for the next one
      std::fill(remap.begin(), remap.end(), -1);
      vector<unsigned int> nextIds;
      local.resize(n);
      for (size_t i = 0; i < n; i++)
      {
         const unsigned int v = level.indices[i];
         if (remap[v] < 0)
         {
            remap[v] = static_cast<int>(nextIds.size());
            nextIds.push_back(v);
         }
         local[i] = static_cast<unsigned int>(remap[v]);
      }
      ids.swap(nextIds);
      localPos.resize(ids.size() * 3);
      localAttr.resize(ids.size() * 5);
      for (size_t i = 0; i < ids.size(); i++)
      {
         std::copy_n(&pos[ids[i] * 3], 3, &localPos[i * 3]);
         std::copy_n(&attr[ids[i] * 5], 5, &localAttr[i * 5]);
      }
      if (withLayers)
      {
         localLock.resize(ids.size());
         for (size_t i = 0; i < ids.size(); i++)
            localLock[i] = split.lock[ids[i]];
      }
   }

   // The levels of the layers: the triangles of each level whose corners meet a layer, on the layer's vertices. All the corners of a triangle meet
   // the same layers, as only the vertices whose triangles all have the same layers move.
   vector<vector<vector<unsigned int>>> layerLevels(group.layers.size(), vector<vector<unsigned int>>(levels->levels.size()));
   if (withLayers)
   {
      bool consistent = true;
      for (size_t l = 0; l < levels->levels.size() && consistent; l++)
      {
         const vector<unsigned int>& li = levels->levels[l].indices;
         for (size_t i = 0; i + 2 < li.size() && consistent; i += 3)
         {
            const unsigned int a = split.start[li[i]], b = split.start[li[i + 1]], c = split.start[li[i + 2]];
            const unsigned int n = split.start[li[i] + 1] - a;
            if (split.start[li[i + 1] + 1] - b != n || split.start[li[i + 2] + 1] - c != n)
            {
               consistent = false;
               break;
            }
            for (unsigned int e = 0; e < n; e++)
            {
               const unsigned int layer = split.layer[a + e];
               if (split.layer[b + e] != layer || split.layer[c + e] != layer)
               {
                  consistent = false;
                  break;
               }
               vector<unsigned int>& out = layerLevels[layer][l];
               out.push_back(split.vertex[a + e]);
               out.push_back(split.vertex[b + e]);
               out.push_back(split.vertex[c + e]);
            }
         }
      }
      for (const auto& layer : layerLevels)
         for (const auto& level : layer)
            consistent &= !level.empty();
      if (!consistent)
      {
         leaveOut();
         return;
      }
      // The part's levels draw the vertices the copies repeat
      for (MeshLevels::Level& level : levels->levels)
         for (unsigned int& i : level.indices)
            i = split.source[i];
   }
   // The levels of a welded mesh draw vertices of the mesh
   if (welded)
      for (MeshLevels::Level& level : levels->levels)
         for (unsigned int& i : level.indices)
            i = toMesh[i];

   const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   const std::lock_guard<std::mutex> lock(m_statsMutex);
   m_cpuMs += ms;
   m_end = std::chrono::steady_clock::now();
   if (levels->levels.empty())
      return;
   for (Primitive* part : group.parts)
      part->m_meshLevels = levels;
   m_nMeshes += static_cast<int>(group.parts.size() + group.layers.size());
   m_nShared += static_cast<int>(group.parts.size()) - 1;
   m_nCandidatesDone += static_cast<int>(group.parts.size());
   m_trisIn += tris * group.parts.size();
   for (size_t b = 0; b < std::size(kBounds); b++)
   {
      size_t t = tris;
      for (const MeshLevels::Level& level : levels->levels)
         if (level.error <= kBounds[b])
            t = level.indices.size() / 3;
      m_trisPerLevel[b] += t * group.parts.size();
   }
   for (size_t layer = 0; layer < group.layers.size(); layer++)
   {
      Primitive* const lp = group.layers[layer];
      auto layerLevelsOf = std::make_shared<MeshLevels>();
      layerLevelsOf->loadSize = p->m_d.m_vSize;
      layerLevelsOf->sourceIndices = lp->m_mesh.NumIndices();
      layerLevelsOf->sourceVertices = lp->m_mesh.NumVertices();
      layerLevelsOf->leader = p;
      for (size_t l = 0; l < levels->levels.size(); l++)
      {
         MeshLevels::Level& level = layerLevelsOf->levels.emplace_back();
         level.indices = std::move(layerLevels[layer][l]);
         level.error = levels->levels[l].error;
      }
      lp->m_meshLevels = layerLevelsOf;
      const size_t layerTris = lp->m_mesh.NumIndices() / 3;
      m_nLayersWithPart++;
      m_nCandidatesDone += IsCandidate(lp) ? 1 : 0;
      m_trisIn += layerTris;
      m_trisLayersWithPart += layerTris;
      for (size_t b = 0; b < std::size(kBounds); b++)
      {
         size_t t = layerTris;
         for (const MeshLevels::Level& level : layerLevelsOf->levels)
            if (level.error <= kBounds[b])
               t = level.indices.size() / 3;
         m_trisPerLevel[b] += t;
      }
   }
}

void MeshSimplifier::LogStats()
{
   if (!m_enabled)
   {
      PLOGI << "Mesh simplification: off";
      return;
   }
   std::stringstream perLevel;
   for (size_t b = 0; b < std::size(kBounds); b++)
      perLevel << (b ? ", " : "") << kBounds[b] << " VPU " << m_trisPerLevel[b];
   PLOGI << "Mesh simplification: " << m_nMeshes << " meshes (" << m_nShared << " sharing the levels of a coinciding part, " << m_nLayersWithPart
         << " additive parts and light maps simplified with the part under them), " << max(m_nCandidates - m_nCandidatesDone, 0) << " large meshes skipped; "
         << m_trisIn << " triangles, at most " << perLevel.str() << "; " << static_cast<int>(m_cpuMs) << " ms of CPU, done "
         << static_cast<int>(std::chrono::duration<double, std::milli>(m_end - m_start).count()) << " ms after the start of the loading";
   if (m_nLayers > 0)
   {
      PLOGI << "Mesh simplification: " << m_nLayers << " additive parts and light maps on the table, " << m_nLayersWithPart << " simplified with the part under them ("
            << m_trisLayersWithPart << " triangles), found in " << static_cast<int>(m_layersMs) << " ms; " << m_nUnderLayers
            << " parts left out under a layer lying on two parts or not made of their triangles (" << m_trisUnderLayers << " triangles, " << m_nUnmatched << " not matched)";
   }
}

std::shared_ptr<IndexBuffer> MeshSimplifier::CreateIndexBuffer(RenderDevice* rd, const Mesh& mesh, std::shared_ptr<MeshLevels>& levels)
{
   if (levels && (levels->sourceIndices != mesh.NumIndices() || levels->sourceVertices != mesh.NumVertices()))
      levels = nullptr; // The mesh changed since it was simplified
   if (levels == nullptr)
      return std::make_shared<IndexBuffer>(rd, mesh.m_indices);
   // The mesh, then each level
   size_t total = mesh.NumIndices();
   for (const MeshLevels::Level& level : levels->levels)
      total += level.indices.size();
   vector<unsigned int> indices;
   indices.reserve(total);
   indices.insert(indices.end(), mesh.m_indices.begin(), mesh.m_indices.end());
   for (MeshLevels::Level& level : levels->levels)
   {
      level.first = static_cast<uint32_t>(indices.size());
      indices.insert(indices.end(), level.indices.begin(), level.indices.end());
   }
   return std::make_shared<IndexBuffer>(rd, indices);
}

void MeshSimplifier::SelectRange(const MeshLevels* levels, const Mesh& mesh, const Mesh& boundsMesh, const Matrix3D& fullMatrix, const ModelViewProj& mvp, int renderHeight,
   bool isReflectionPass, int& currentLevel, uint32_t& first, uint32_t& count) const
{
   first = 0;
   count = static_cast<uint32_t>(mesh.NumIndices());
   if (!m_enabled || levels == nullptr)
   {
      if (!isReflectionPass)
         currentLevel = -1;
      CountFrame(0);
      return;
   }

   // Bounds of the part on the table (the mesh's bounds are up to date: the culling test of Primitive::Render updates them)
   Vertex3Ds lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
   for (int i = 0; i < 8; i++)
   {
      Vertex3Ds c((i & 1) ? boundsMesh.m_minAABound.x : boundsMesh.m_maxAABound.x, (i & 2) ? boundsMesh.m_minAABound.y : boundsMesh.m_maxAABound.y,
         (i & 4) ? boundsMesh.m_minAABound.z : boundsMesh.m_maxAABound.z);
      fullMatrix.MultiplyVector(c);
      lo.x = min(lo.x, c.x); lo.y = min(lo.y, c.y); lo.z = min(lo.z, c.z);
      hi.x = max(hi.x, c.x); hi.y = max(hi.y, c.y); hi.z = max(hi.z, c.z);
   }

   // Distance to the closest eye (in the reflection probe, to the mirrored eye: the length of the reflected path)
   float distance = FLT_MAX;
   for (unsigned int eye = 0; eye < mvp.m_nEyes; eye++)
   {
      const vec4& c = mvp.GetCameraPos(eye);
      const float dx = max(max(lo.x - c.x, 0.f), c.x - hi.x), dy = max(max(lo.y - c.y, 0.f), c.y - hi.y), dz = max(max(lo.z - c.z, 0.f), c.z - hi.z);
      distance = min(distance, sqrtf(dx * dx + dy * dy + dz * dz));
   }

   // The levels' errors are for the size at load: scale them by the size now (a script may change it), the largest stretch of the axes
   const Vertex3Ds& s = levels->loadSize;
   const float scale = max(max(
      sqrtf(fullMatrix._11 * fullMatrix._11 + fullMatrix._12 * fullMatrix._12 + fullMatrix._13 * fullMatrix._13) / max(fabsf(s.x), 1e-6f),
      sqrtf(fullMatrix._21 * fullMatrix._21 + fullMatrix._22 * fullMatrix._22 + fullMatrix._23 * fullMatrix._23) / max(fabsf(s.y), 1e-6f)),
      sqrtf(fullMatrix._31 * fullMatrix._31 + fullMatrix._32 * fullMatrix._32 + fullMatrix._33 * fullMatrix._33) / max(fabsf(s.z), 1e-6f));
   // Pixels per unit of the tangent of the angle from the view axis (perspective) or per VP unit (orthographic). Taken at the full render
   // resolution, not the one of dynamic resolution, so that a lower resolution does not make the parts coarser, which would let it rise again.
   // The projection may carry a scale of the scene (in VR, the size of the table in the room scales the whole matrix): it cancels in the
   // divide by w, hence the ratio with the term that gives w.
   const Matrix3D& proj = mvp.GetProj(0);
   const bool orthographic = fabsf(proj._34) < 1e-6f * fabsf(proj._22);
   const float pixelScale = max(fabsf(proj._22 / (orthographic ? proj._44 : proj._34)) * 0.5f * static_cast<float>(renderHeight), 1e-6f);
   const float allowed = m_allowedPixels * (orthographic ? 1.f : distance) / pixelScale * (isReflectionPass ? kProbeFactor : 1.f) / max(scale, 1e-6f);

   int level = -1;
   for (int i = 0; i < static_cast<int>(levels->levels.size()) && levels->levels[i].error <= allowed; i++)
      level = i;
   if (!isReflectionPass)
   {
      // Finer at once, coarser with a margin
      if (level > currentLevel)
      {
         int coarser = currentLevel;
         for (int i = currentLevel + 1; i <= level && levels->levels[i].error <= allowed * kCoarserMargin; i++)
            coarser = i;
         level = coarser;
      }
      currentLevel = level;
   }
   if (level >= 0)
   {
      first = levels->levels[level].first;
      count = static_cast<uint32_t>(levels->levels[level].indices.size());
   }
   CountFrame((mesh.NumIndices() - count) / 3);
}

void MeshSimplifier::CountFrame(size_t saved) const
{
   // A new frame being prepared closes the previous one, measured against the triangles of the last frame rendered
   if (g_pplayer && g_pplayer->m_overall_frames != m_statsFrame)
   {
      m_statsFrame = g_pplayer->m_overall_frames;
      m_windowSaved += m_frameSaved;
      m_windowTriangles += g_pplayer->m_renderer->m_renderDevice->m_frameDrawnTriangles;
      m_frameSaved = 0;
      const auto now = std::chrono::steady_clock::now();
      if (now - m_windowStart >= std::chrono::seconds(1))
      {
         m_savedShare = m_windowSaved + m_windowTriangles > 0 ? static_cast<float>(m_windowSaved) / static_cast<float>(m_windowSaved + m_windowTriangles) : 0.f;
         m_windowSaved = m_windowTriangles = 0;
         m_windowStart = now;
      }
   }
   m_frameSaved += saved;
}

}
