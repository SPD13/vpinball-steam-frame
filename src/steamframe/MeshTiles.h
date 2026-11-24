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
// map and the part under it share their cells). A triangle longer than a cell (a rail, the side of a ramp) goes to a cell of a coarser grid, the
// first whose cells are as long as it (kTileSize << level): otherwise it would stretch the box of the small tile of its center across the table, and
// that tile would always be seen (Medieval Madness, 10 cm above the middle of the playfield, before the coarser grids: 28 of the 64 tiles of
// BM_Parts outsized, 313 k of its 337 k triangles drawn, 43 k in the view). Each group, a tile, gets the box of its triangles, at most about twice
// its cell. Each frame the tiles of the part are tested against the view, and the part draws only the runs of its index range whose tiles may be
// seen: a few draws instead of one, merging the tiles that follow each other in the buffer, and the small gaps between them. A part fully in the
// view draws its range at once as before.
//
// Levels by tile (Settings Player/TileLevels, Distance and gaze by default; docs/steam-frame/Culling by Tiles.md, section 8): a part whose levels
// were made with its tiles in mind draws each tile at its own level, picked from the distance of the tile instead of that of the part's closest
// point, and allowed up to 4 times more error away from where the eyes look when the headset tracks them. Mixing levels between tiles would open
// cracks along their borders, and make light maps overlap there, unless the levels never change the borders: while a large part is simplified
// (MeshSimplifier::Make), its places on the border of a cell (vertices within kPlaceDistance, whose triangles lie in more than one cell, see Places)
// are locked, and the places next to them as well. Then each collapse stays within a cell, each level keeps the borders of the mesh exactly, and
// every triangle a collapse made has a corner inside its cell. The cell of a triangle of any range is the cell of its first corner inside a cell,
// or, with all its corners on borders, that of the triangle of the mesh at the same places (it is one, unchanged): the same cells for a part's mesh,
// its levels, and the light maps simplified with it, which draw the levels their part draws, tile by tile. A part draws at most kMaxDrawnLevels
// levels at once (the draws of its light maps multiply with them); the reflections draw one level for the whole part, as before.
// Medieval Madness on the Frame at 2160 x 2160 (2026-11-24, headless, the gaze held at the view center): three quarters of the way from the lockbar
// to 10 cm above the middle, 16.1 ms per frame without, 14.4 by distance, 13.3 with the gaze; 10 cm above the middle 11.1, 10.2, 9.7; at the lockbar
// 6.6, 6.4, 6.2. The locked borders cost nothing measurable when the setting is off (6.6 ms at the lockbar, as before).
// Light maps sorted by light (LightMapPruning) keep their order: the tiles are grouped within each share of the sort that a level of pruning keeps,
// so that the triangles kept at each level are still the start of the range.
// Only the index buffer is reordered: the mesh of the part (physics, editor, table file) is untouched.

#include "math/Mesh.h"
#include "math/matrix.h"

#include <array>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <vector>

class ModelViewProj;
class PinTable;
class Primitive;

namespace VPX
{

struct MeshLevels;

class MeshTiles final
{
public:
   // Grid cell size, in VP units (1 VPU = 0.54 mm): 200 VPU = 10.8 cm, a 5 x 11 grid on a standard playfield
   static constexpr float kTileSize = 200.f;
   // Coarser grids for the longer triangles: cells of kTileSize << level, up to 6400 VPU (3.5 m)
   static constexpr int kMaxLevel = 5;
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

   // Levels by tile (Player/TileLevels, in the order of its options)
   enum LevelMode { LevelsOff = 0, LevelsByDistance = 1, LevelsByDistanceAndGaze = 2 };

   // The places of a part's vertices: its vertices within kPlaceDistance of each other (scaled by the part's size, as the simplifier sees them; a welded
   // copy merges vertices within 0.01 VPU), with the cell of the triangles around each, made from its mesh and its placement on the table when it
   // loads. A place whose triangles lie in several cells is a border.
   class Places final
   {
   public:
      static constexpr float kPlaceDistance = 0.02f;
      static constexpr uint64_t kBorder = ~0ull; // A place on a border, or not a place of the part
      static constexpr uint64_t kLocked = 1ull << 62; // Flag of a place next to a border (its cell is still its cell)
      static constexpr uint32_t kNone = ~0u;
      uint32_t OfVertex(size_t vertex) const { return m_placeOf[vertex]; } // Place of a vertex of the part
      uint32_t Find(float x, float y, float z) const; // Place of a position (of a layer lying on the part, scaled by its size), or kNone
      uint64_t Value(uint32_t place) const { return place == kNone ? kBorder : m_value[place]; } // Its cell, possibly flagged kLocked, or kBorder
      // The cell of a triangle of the part's mesh with corners at these places (all on borders), or kBorder when the mesh has none
      uint64_t TriangleAt(uint32_t a, uint32_t b, uint32_t c) const;

   private:
      friend class MeshTiles;
      static uint64_t GridKey(int x, int y, int z);
      static uint64_t TripleKey(uint32_t a, uint32_t b, uint32_t c);
      std::vector<float> m_pos; // Of each vertex, scaled
      std::vector<uint32_t> m_placeOf; // Of each vertex
      std::vector<uint64_t> m_value; // Of each place
      std::vector<std::pair<uint64_t, uint32_t>> m_grid; // Vertices by cell of a grid of kPlaceDistance, sorted
      std::unordered_map<uint64_t, uint64_t> m_triangles; // Cell of the triangles of the mesh with all their corners on borders, by their places
   };
   // The places of a part tiled with levels by tile (at least kMinTriangles, not animated), made once and kept until the next table (the simplifier
   // and the index buffers of the part and of its layers use them). 'matrix': its placement on the table. Null for a part that is not tiled.
   static std::shared_ptr<const Places> GetPlaces(const Primitive* part, const Matrix3D& matrix);
   // The cell of a triangle from its corners on the table: in the grid of the first level whose cells are as long as the triangle, the cell of its center
   static uint64_t TriangleCell(const PinTable* table, const Vertex3Ds (&p)[3]);

   // Groups by tile the triangles of each range of a part's index buffer ('indices': the mesh then its levels, at the positions given by
   // MeshLevels::Level::first), keeping the triangles kept by each level of light map pruning at the start of each range. Returns null for a
   // part that is not tiled (small, animated, on a single tile).
   // A part whose levels were made with locked borders (MeshLevels::tileOwner), or a layer simplified with such a part, gets the cells of the places
   // of that part, whatever its size.
   static std::shared_ptr<MeshTiles> Build(Primitive* part, const Mesh& mesh, const MeshLevels* levels, std::vector<unsigned int>& indices);

   // The runs of the range [first, first + count) (a range of Build, possibly shortened by light map pruning) whose tiles may be seen by the
   // view of 'mvp' (whose model matrix places the part), returning their number: one run when the part is fully in the view (or the range is
   // not one of the part's), none when no tile is. Fills a caller's array, as the render of a part may render others (probes) before it draws.
   int SelectRuns(const ModelViewProj& mvp, uint32_t first, uint32_t count, Run (&runs)[kMaxRuns]);

   // Levels by tile, on the tiles of the part that owns the levels (the part simplified, whose layers follow it), for the main view: picks the level
   // of each tile from the error each level makes ('errors', from the finest, in VP units at the current size) against 'allowedPerDistance' (the
   // error allowed per VP unit of distance from the closest eye, or in VP units for an orthographic view), times the allowance of the gaze
   // ('gazeTangents': per eye, the tangents of the gaze in the view space of the eye, x right and y up; null without eye tracking). Once per frame.
   void ChooseLevels(const std::vector<float>& errors, const Matrix3D& fullMatrix, const ModelViewProj& mvp, float allowedPerDistance, bool orthographic,
      const float (*gazeTangents)[2], unsigned int frame, float coarserMargin);
   // The finest level chosen for a tile in the view (-1: the full mesh), after ChooseLevels
   int GetFinestChosen() const { return m_finestChosen; }
   // The runs of a part drawing the levels chosen by 'owner' (its own tiles, or those of the part it lies on), each range shortened to 'counts'
   // (its indices drawn: light map pruning), in the order of the ranges. Returns their number (0: nothing to draw), in a scratch array of this part.
   int SelectLevelRuns(const ModelViewProj& mvp, const MeshTiles& owner, const std::vector<uint32_t>& counts, const Run*& runs, size_t& drawnIndices);
   // The ranges of the buffer (the mesh, then each level): first index and count
   size_t GetNumRanges() const { return m_ranges.size(); }
   uint32_t GetRangeFirst(size_t r) const { return m_ranges[r].first; }
   uint32_t GetRangeIndices(size_t r) const { return m_ranges[r].count; }
   bool HasLevelsByTile() const { return m_levelsByTile; } // Its triangles were grouped by the cells of the places (levels made with locked borders)

   size_t GetTileCount() const { return m_tiles.size(); }

   // Live switch (Player/MeshTiles), and what the tiles left out
   static bool IsEnabled() { return s_enabled; }
   static void SetEnabled(bool enabled) { s_enabled = enabled; }
   static int GetLevelMode() { return s_levelMode; }
   static void SetLevelMode(int mode) { s_levelMode = mode; }
   static void ResetStats(); // When a table starts
   static void CountTiled(size_t tiles); // A part got tiles
   static int GetTiledPartCount() { return s_parts; }
   static size_t GetTileTotal() { return s_tilesTotal; }
   static float GetSavedShare(); // Share of the triangles of the tiled parts left out over the last second (NaN before the first second)

private:
   struct Tile
   {
      Vertex3Ds lo, hi; // Box of its triangles (of all ranges), in the part's mesh space
      uint64_t cell;
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
   bool m_levelsByTile = false;

   // Levels by tile, of the part owning the levels: the level of each tile in the main view (-1: the full mesh), kept between frames so that a
   // tile only gets coarser with some margin, and the boxes of the tiles on the table for the last placement of the part
   std::vector<int8_t> m_tileLevel, m_drawLevel;
   unsigned int m_levelsFrame = ~0u;
   int m_finestChosen = -1;
   Matrix3D m_boxMatrix;
   bool m_boxesValid = false;
   std::vector<Vertex3Ds> m_tableLo, m_tableHi;
   // A part drawing the levels of another (a layer): the owner's tile of each of its tiles, found by cell at the first draw
   const MeshTiles* m_mappedOwner = nullptr;
   std::vector<int> m_ownerTile;
   std::vector<Run> m_levelRuns; // Scratch

   static void CountFrame(size_t drawn, size_t culled);
   static bool s_enabled;
   static int s_levelMode;
   static int s_parts;
   static size_t s_tilesTotal;
};

}
