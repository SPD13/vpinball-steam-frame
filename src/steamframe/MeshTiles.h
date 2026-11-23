// license:GPLv3+

#pragma once

// Steam Frame Edition: large parts culled by tiles (Settings Player/MeshTiles, on by default in the build of the Steam Frame, switched live in the
// Simplified meshes page of the Device Performance screen; docs/steam-frame/Culling by Tiles.md).
//
// Parts are culled whole (Primitive::Render skips a part whose bounding box is outside the view), and their simplified level is picked from the
// distance to their closest point. A baked table draws most of its playfield and its toys as a few parts spanning the whole table, each with dozens
// of light maps spanning it too (Medieval Madness: BM_Parts, 337 k triangles at its full mesh, and 84 light maps): leaning in to read the rules
// card, the eyes see a corner of them, and every triangle is drawn at the full mesh. Counted on the Frame (2026-11-22, cull-hooks.py), 10 cm above
// the card 154 k of the 763 k triangles drawn were in the view (20 %), 10 cm above the middle of the playfield 138 k of 830 k (16 %).
//
// While the index buffer of a large part is made (Primitive::RenderSetup), the triangles of each range it may draw (its mesh, each simplified level)
// are grouped by the cell of a grid laid on the table, from their center (cells of kTileSize VP units, the same grid for every part, so that a light
// map and the part under it share their cells). Each group, a tile, gets the box of its triangles. Each frame the tiles of the part are tested
// against the view, and the part draws only the runs of its index range whose tiles may be seen: a few draws instead of one, merging the tiles that
// follow each other in the buffer, and the small gaps between them. A part fully in the view draws its range at once as before.
//
// Every tile of a part draws the same level, the one picked for the whole part (MeshSimplifier::SelectRange): mixing levels between tiles would
// open cracks along their borders, and make light maps overlap there. Light maps sorted by light (LightMapPruning) keep their order: the tiles are
// grouped within each share of the sort that a level of pruning keeps, so that the triangles kept at each level are still the start of the range.
// Only the index buffer is reordered: the mesh of the part (physics, editor, table file) is untouched.

#include "math/Mesh.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class ModelViewProj;
class Primitive;

namespace VPX
{

struct MeshLevels;

class MeshTiles final
{
public:
   // Grid cell size, in VP units (1 VPU = 0.54 mm): 200 VPU = 10.8 cm, a 5 x 11 grid on a standard playfield
   static constexpr float kTileSize = 200.f;
   // Parts whose mesh has fewer triangles than this are not tiled
   static constexpr size_t kMinTriangles = 4000;
   // Most draws a part makes: beyond, the runs separated by the fewest triangles are merged
   static constexpr int kMaxRuns = 8;
   // Runs separated by fewer triangles than this are merged: drawing a few hidden triangles costs less than a draw
   static constexpr uint32_t kMergeTriangles = 1024;

   struct Run
   {
      uint32_t first, count; // In indices of the part's index buffer
   };

   // Groups by tile the triangles of each range of a part's index buffer ('indices': the mesh then its levels, at the positions given by
   // MeshLevels::Level::first), keeping the triangles kept by each level of light map pruning at the start of each range. Returns null for a
   // part that is not tiled (small, animated, on a single tile).
   static std::shared_ptr<MeshTiles> Build(Primitive* part, const Mesh& mesh, const MeshLevels* levels, std::vector<unsigned int>& indices);

   // The runs of the range [first, first + count) (a range of Build, possibly shortened by light map pruning) whose tiles may be seen by the
   // view of 'mvp' (whose model matrix places the part), returning their number: one run when the part is fully in the view (or the range is
   // not one of the part's), none when no tile is. Fills a caller's array, as the render of a part may render others (probes) before it draws.
   int SelectRuns(const ModelViewProj& mvp, uint32_t first, uint32_t count, Run (&runs)[kMaxRuns]);

   size_t GetTileCount() const { return m_tiles.size(); }

   // Live switch (Player/MeshTiles), and what the tiles left out
   static bool IsEnabled() { return s_enabled; }
   static void SetEnabled(bool enabled) { s_enabled = enabled; }
   static void ResetStats(); // When a table starts
   static void CountTiled(size_t tiles); // A part got tiles
   static int GetTiledPartCount() { return s_parts; }
   static size_t GetTileTotal() { return s_tilesTotal; }
   static float GetSavedShare(); // Share of the triangles of the tiled parts left out over the last second (NaN before the first second)

private:
   struct Tile
   {
      Vertex3Ds lo, hi; // Box of its triangles (of all ranges), in the part's mesh space
   };
   struct Entry
   {
      uint32_t start, count; // Indices of the range, from its start
      uint32_t tile;
   };
   struct Range
   {
      uint32_t first, count;
      std::vector<Entry> entries; // In the order of the buffer, covering the range
   };
   std::vector<Tile> m_tiles;
   Vertex3Ds m_lo, m_hi; // Box of all the tiles
   std::vector<Range> m_ranges;
   std::vector<uint8_t> m_visible; // Scratch (Primitive::Render is called by the logic thread only)

   static void CountFrame(size_t drawn, size_t culled);
   static bool s_enabled;
   static int s_parts;
   static size_t s_tilesTotal;
};

}
