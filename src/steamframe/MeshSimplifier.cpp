// license:GPLv3+

#include "core/stdafx.h"
#include "MeshSimplifier.h"

#include "core/player.h"
#include "parts/primitive.h"
#include "renderer/Renderer.h"
#include "renderer/IndexBuffer.h"
#include "ThreadPool.h"
#include "meshoptimizer/meshoptimizer.h"

#include <chrono>
#include <string_view>
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

struct MeshSimplifier::Group
{
   vector<Primitive*> parts; // The first one is simplified, the others share its levels
   size_t triangles = 0;
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

void MeshSimplifier::Enqueue(progschj::ThreadPool& pool, PinTable* table)
{
   m_enabled = table->GetSettings().GetPlayer_SimplifyMeshes();
   m_allowedPixels = table->GetSettings().GetPlayer_SimplifyMeshesError();
   m_nMeshes = m_nShared = m_nSkipped = 0;
   m_savedShare = NAN;
   m_windowSaved = m_windowTriangles = m_frameSaved = 0;
   m_windowStart = std::chrono::steady_clock::now();
   m_trisIn = 0;
   m_trisPerLevel.assign(std::size(kBounds), 0);
   m_cpuMs = 0.;
   m_start = std::chrono::steady_clock::now();
   m_end = m_start;

   vector<Primitive*> candidates;
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemPrimitive)
         continue;
      Primitive* const p = static_cast<Primitive*>(part);
      p->m_meshLevels = nullptr;
      p->m_meshLevel = -1;
      if (p->m_d.m_use3DMesh && !p->IsPlayfield() && p->m_mesh.m_animationFrames.empty() && p->m_mesh.NumIndices() >= 3 * kMinTriangles && !IsGrouped(p))
         candidates.push_back(p);
   }
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

   // Additive parts and light maps are only simplified with a coinciding part that is not one: alone, they lie on top of another part's
   // surface, which would not be simplified the same way
   const auto isLayer = [](const Primitive* p) { return p->m_d.m_addBlend || !p->m_d.m_szLightmap.empty(); };
   std::erase_if(groups,
      [this, &isLayer](const std::shared_ptr<Group>& g)
      {
         const auto base = std::find_if(g->parts.begin(), g->parts.end(), [&isLayer](const Primitive* p) { return !isLayer(p); });
         if (base == g->parts.end())
         {
            m_nSkipped += static_cast<int>(g->parts.size());
            return true;
         }
         std::iter_swap(g->parts.begin(), base);
         return false;
      });

   // The largest first, so that they do not end the loading alone
   std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a->triangles > b->triangles; });
   for (const auto& group : groups)
      pool.enqueue_block([this, group]() { Simplify(*group); });
}

void MeshSimplifier::Simplify(const Group& group)
{
   const auto t0 = std::chrono::steady_clock::now();
   const Primitive* const p = group.parts[0];
   const Mesh& mesh = p->m_mesh;
   const size_t nv = mesh.NumVertices();

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
   static constexpr float weights[5] = { kAttributeWeight, kAttributeWeight, kAttributeWeight, kAttributeWeight, kAttributeWeight };

   auto levels = std::make_shared<MeshLevels>();
   levels->loadSize = p->m_d.m_vSize;
   levels->sourceIndices = mesh.NumIndices();
   levels->sourceVertices = nv;
   // The simplifier works on all the vertices it is given: each level is simplified from the vertices the previous one still uses ('local'
   // indices over 'ids', the vertices of the mesh), which makes the coarser levels cheap
   vector<unsigned int> ids(nv), local(mesh.m_indices);
   for (size_t i = 0; i < nv; i++)
      ids[i] = static_cast<unsigned int>(i);
   vector<float> localPos = pos, localAttr = attr;
   vector<int> remap(nv);
   size_t sourceCount = mesh.NumIndices();
   float sourceError = 0.f;
   vector<unsigned int> dst(sourceCount);
   for (const float bound : kBounds)
   {
      if (bound <= sourceError)
         continue;
      float stepError = 0.f;
      const size_t n = meshopt_simplifyWithAttributes(dst.data(), local.data(), sourceCount, localPos.data(), ids.size(), sizeof(float) * 3, localAttr.data(),
         sizeof(float) * 5, weights, 5, nullptr, 3, bound - sourceError, meshopt_SimplifyErrorAbsolute, &stepError);
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
   }

   const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   const std::lock_guard<std::mutex> lock(m_statsMutex);
   m_cpuMs += ms;
   m_end = std::chrono::steady_clock::now();
   if (levels->levels.empty())
   {
      m_nSkipped += static_cast<int>(group.parts.size());
      return;
   }
   for (Primitive* part : group.parts)
      part->m_meshLevels = levels;
   const size_t tris = mesh.NumIndices() / 3;
   m_nMeshes += static_cast<int>(group.parts.size());
   m_nShared += static_cast<int>(group.parts.size()) - 1;
   m_trisIn += tris * group.parts.size();
   for (size_t b = 0; b < std::size(kBounds); b++)
   {
      size_t t = tris;
      for (const MeshLevels::Level& level : levels->levels)
         if (level.error <= kBounds[b])
            t = level.indices.size() / 3;
      m_trisPerLevel[b] += t * group.parts.size();
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
   PLOGI << "Mesh simplification: " << m_nMeshes << " meshes (" << m_nShared << " sharing the levels of a coinciding part), " << m_nSkipped
         << " skipped; " << m_trisIn << " triangles, at most " << perLevel.str() << "; " << static_cast<int>(m_cpuMs) << " ms of CPU, done "
         << static_cast<int>(std::chrono::duration<double, std::milli>(m_end - m_start).count()) << " ms after the start of the loading";
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

void MeshSimplifier::SelectRange(const MeshLevels* levels, const Mesh& mesh, const Matrix3D& fullMatrix, const ModelViewProj& mvp, int renderHeight, bool isReflectionPass,
   int& currentLevel, uint32_t& first, uint32_t& count) const
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
      Vertex3Ds c((i & 1) ? mesh.m_minAABound.x : mesh.m_maxAABound.x, (i & 2) ? mesh.m_minAABound.y : mesh.m_maxAABound.y, (i & 4) ? mesh.m_minAABound.z : mesh.m_maxAABound.z);
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
