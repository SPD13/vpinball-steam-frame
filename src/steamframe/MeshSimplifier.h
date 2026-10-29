// license:GPLv3+

#pragma once

// Steam Frame Edition: simplified render meshes for the primitives of heavy tables (Settings Player/SimplifyMeshes, on by default in the build
// of the Steam Frame, switched live in the VR rendering settings, see docs/Performance on the Steam Frame.md, section 8.2).
//
// The GPU time of heavy tables on the Frame follows their triangle count (about 2.2 ms per million triangles per frame), and tables built from
// scans or exports carry far more triangles than the headset can show. While the table loads, each large primitive gets a few simplified
// levels of its mesh (meshoptimizer), each an index list over the primitive's own vertices: positions, normals and texture coordinates of the
// kept vertices are exact, the physics and the editor keep the original mesh, and the table's file is untouched. Each frame, each primitive
// draws the coarsest level whose error, seen from the closest eye, stays under a share of a pixel (Player/SimplifyMeshesError, in pixels of
// the view at its full render resolution: about 1 VP unit at 0.5 m on the Frame at 2160x2160); the reflection probe allows more. Leaning
// in brings back the full mesh.
//
// Not simplified: animated meshes, grouped parts, additive parts and light maps (unless they coincide with a simplified part), small meshes.
// Meshes that coincide (same index list and vertex positions, like a part and its 'off' layer) are simplified once and share the result, so
// that stacked layers stay exactly on top of each other.

#include "math/Mesh.h"
#include "math/matrix.h"

#include <chrono>
#include <mutex>

class IndexBuffer;
class ModelViewProj;
class PinTable;
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
   Vertex3Ds loadSize; // Size of the primitive when it was simplified
   size_t sourceIndices = 0, sourceVertices = 0; // Mesh the levels were made from (the levels are dropped if it changed)
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
   // 'renderHeight' is the height of the view in pixels at its full render resolution.
   void SelectRange(const MeshLevels* levels, const Mesh& mesh, const Matrix3D& fullMatrix, const ModelViewProj& mvp, int renderHeight, bool isReflectionPass,
      int& currentLevel, uint32_t& first, uint32_t& count) const;

private:
   MeshSimplifier() = default;

   struct Group; // Coinciding primitives simplified together
   void Simplify(const Group& group);

   bool m_enabled = false;
   float m_allowedPixels = 0.f; // Allowed error, in pixels

   // Triangles left out, per frame and over one second (SelectRange is called by the logic thread while it prepares a frame)
   void CountFrame(size_t saved) const;
   mutable unsigned int m_statsFrame = ~0u;
   mutable uint64_t m_frameSaved = 0, m_windowSaved = 0, m_windowTriangles = 0;
   mutable std::chrono::steady_clock::time_point m_windowStart;
   mutable float m_savedShare = NAN;

   std::mutex m_statsMutex;
   int m_nMeshes = 0, m_nShared = 0, m_nSkipped = 0;
   size_t m_trisIn = 0;
   vector<size_t> m_trisPerLevel;
   double m_cpuMs = 0.;
   std::chrono::steady_clock::time_point m_start, m_end;
};

}
