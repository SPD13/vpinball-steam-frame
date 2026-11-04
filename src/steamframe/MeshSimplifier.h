// license:GPLv3+

#pragma once

// Steam Frame Edition: simplified render meshes for the primitives of heavy tables (Settings Player/SimplifyMeshes, on by default in the build
// of the Steam Frame, switched live in the Steam Frame screen of the VR menu, see docs/Performance on the Steam Frame.md, section 8.2).
//
// The GPU time of heavy tables on the Frame follows their triangle count (about 2.2 ms per million triangles per frame), and tables built from
// scans or exports carry far more triangles than the headset can show. While the table loads, each large primitive gets a few simplified
// levels of its mesh (meshoptimizer), each an index list over the primitive's own vertices: positions, normals and texture coordinates of the
// kept vertices are exact, the physics and the editor keep the original mesh, and the table's file is untouched. Each frame, each primitive
// draws the coarsest level whose error, seen from the closest eye, stays under a share of a pixel (Player/SimplifyMeshesError, in pixels of
// the view at its full render resolution: about 1 VP unit at 0.5 m on the Frame at 2160x2160); the reflection probe allows more. Leaning
// in brings back the full mesh.
//
// Not simplified: animated meshes, grouped parts, small meshes, and additive parts and light maps that do not lie on a simplified part.
// Meshes that coincide (same index list and vertex positions, like a part and its 'off' copy) are simplified once and share the result, so
// that stacked parts stay exactly on top of each other. Meshes exported as separate triangles are simplified from a welded copy, and their
// levels use a single vertex per place: they also lose the 3 vertex shader runs per triangle of the full mesh.
// Additive parts and light maps (layers) are drawn on the very surface of the part under them, without writing depth: a layer made of
// triangles of its part (tables baked with light maps trim them to the lit triangles) is simplified with that part, from the same collapses,
// and draws the level the part draws. The edges of the layers on the part and the seams of their textures stay edges: they only get shorter,
// collapsing along themselves, so that each simplified triangle still has the same layers at its three corners. A part under a layer
// that is not made of its triangles is not simplified.

#include "math/Mesh.h"
#include "math/matrix.h"

#include <chrono>
#include <mutex>

class IndexBuffer;
class ModelViewProj;
class PinTable;
class Primitive;
class RenderDevice;
namespace progschj { class ThreadPool; }

namespace VPX
{

// Simplified levels of a primitive's mesh, from the finest to the coarsest
struct MeshLevels final
{
   struct Level
   {
      vector<unsigned int> indices; // Over the vertices of the primitive's mesh
      float error = 0.f; // Largest deviation from the mesh, in VP units, for the size of the primitive at load (loadSize)
      uint32_t first = 0; // Position in the primitive's index buffer, set up by Primitive::RenderSetup
   };
   vector<Level> levels;
   Vertex3Ds loadSize; // Size of the primitive when it was simplified (of the leader for a layer)
   size_t sourceIndices = 0, sourceVertices = 0; // Mesh the levels were made from (the levels are dropped if it changed)
   // A layer simplified with the part under it: that part, whose bounds, placement and level choice the layer follows, so that both always
   // draw the same level (the layer's levels are the part's levels, on the layer's own vertices)
   Primitive* leader = nullptr;
};

class MeshSimplifier final
{
public:
   static MeshSimplifier& Get();

   // Queues the simplification of the table's primitives on the loading thread pool, when enabled by the table's settings
   void Enqueue(progschj::ThreadPool& pool, PinTable* table);
   // Logs what was done (once the pool is done)
   void LogStats();

   // Index buffer of a primitive: its mesh followed by its levels (whose positions it sets). Drops levels made from another mesh.
   static std::shared_ptr<IndexBuffer> CreateIndexBuffer(RenderDevice* rd, const Mesh& mesh, std::shared_ptr<MeshLevels>& levels);

   // Simplified meshes in use (on for the table when it loaded with the setting on; switched live by the settings page)
   bool IsEnabled() const { return m_enabled; }
   void SetEnabled(bool enabled) { m_enabled = enabled; }
   // Allowed error, in pixels (Player/SimplifyMeshesError, read when the table loads, changed live by the settings page)
   float GetAllowedPixels() const { return m_allowedPixels; }
   void SetAllowedPixels(float pixels) { m_allowedPixels = pixels; }
   // Parts of the table that got simplified levels when it loaded
   int GetSimplifiedPartCount() const { return m_nMeshes; }
   // Share of the triangles of a frame (all views) that the levels left out, over the last second (0 when off, NaN before the first second)
   float GetSavedShare() const { return m_savedShare; }

   // Picks the index range a primitive draws: the full mesh (first 0, count of the mesh) or one of its levels. 'currentLevel' keeps the level
   // of the main view between frames (-1 for the full mesh), so that a level only gets coarser with some margin.
   // 'renderHeight' is the height of the view in pixels at its full render resolution. 'boundsMesh', 'fullMatrix' and 'currentLevel' are those
   // of the levels' leader for a layer simplified with the part under it.
   void SelectRange(const MeshLevels* levels, const Mesh& mesh, const Mesh& boundsMesh, const Matrix3D& fullMatrix, const ModelViewProj& mvp, int renderHeight,
      bool isReflectionPass, int& currentLevel, uint32_t& first, uint32_t& count) const;

private:
   MeshSimplifier() = default;

   struct Group; // Coinciding primitives simplified together, with the layers lying on them
   void Simplify(const Group& group);
   void AttachLayers(vector<std::shared_ptr<Group>>& groups, const vector<Primitive*>& layers);

   bool m_enabled = false;
   float m_allowedPixels = 0.f; // Allowed error, in pixels

   // Triangles left out, per frame and over one second (SelectRange is called by the logic thread while it prepares a frame)
   void CountFrame(size_t saved) const;
   mutable unsigned int m_statsFrame = ~0u;
   mutable uint64_t m_frameSaved = 0, m_windowSaved = 0, m_windowTriangles = 0;
   mutable std::chrono::steady_clock::time_point m_windowStart;
   mutable float m_savedShare = NAN;

   std::mutex m_statsMutex;
   int m_nMeshes = 0, m_nShared = 0, m_nCandidates = 0, m_nCandidatesDone = 0;
   int m_nLayers = 0, m_nLayersWithPart = 0, m_nUnderLayers = 0, m_nUnmatched = 0;
   size_t m_trisIn = 0, m_trisUnderLayers = 0, m_trisLayersWithPart = 0;
   double m_layersMs = 0.;
   vector<size_t> m_trisPerLevel;
   double m_cpuMs = 0.;
   std::chrono::steady_clock::time_point m_start, m_end;
};

}
