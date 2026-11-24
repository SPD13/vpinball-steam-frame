# Culling by tiles: large parts drawn only where the view sees them

*2026-11-23. Technical notes on the branch `mesh-tiles` (from fork `master` 09c703db8, merged into `master` the same day). Built for the Mac
and the Frame, checked on the Mac (section 6), measured on the Frame and tried in the headset by the owner (section 7). The study that led to it:
`docs/Performance on the Steam Frame.md`, and the test tools of the close-up study in the documentation repository
(`test-hooks/lean-*`, `cull-hooks.py`). Paths are relative to the `vpinball` clone.*

## 1. Summary

- **What it does.** While a table loads, each large part (a 3D primitive of at least 4,000 triangles) gets its triangles grouped by
  tile: the cells of a grid of 200 VP units (10.8 cm) laid on the table. Each frame, the part tests its tiles against the view and
  draws only the runs of its index buffer whose tiles may be seen. A part fully in the view draws its range at once, as before.
- **Why.** Leaning in to look closely at the playfield (the rules card of Medieval Madness, an insert) made the GPU time jump and the
  frames stutter. The parts of a baked table span the whole table: the eyes see a corner of them, but they were drawn whole, at their
  full mesh. Counted on the Frame, 10 cm above the middle of the playfield, 16 % of the triangles drawn were in the view.
- **Native resolution.** The owner's direction: lowering the resolution is the fallback, the work should keep the full clarity of the
  native resolution. This change leaves out only what cannot be seen: nothing changes on screen (identical captures, section 6).
- **Size.** A new class `VPX::MeshTiles` (`src/steamframe/MeshTiles.h/.cpp`, about 330 lines with comments), and small changes in
  `Primitive::RenderSetup` and `Primitive::Render`, `MeshSimplifier::CreateIndexBuffer`, `LightMapPruning` (one accessor), the
  settings, the menu and the build list.
- **Setting.** `Player.MeshTiles` (on by default), switched live in Device Performance › Simplified meshes, *Culling by tiles*, with a
  status line: the tiled parts, their tiles, and the share of their triangles left out over the last second.
- **Measured on the Frame** (Medieval Madness, 2160 x 2160, section 7). Leaning in over the rules card: 8.79 to 6.57 ms of GPU time
  per frame, half the triangles left out, the same time as at the lockbar. Over the middle of the playfield: about 1 ms, 18 % of the
  triangles left out, as the eyes still see the back half of the table; most of what leaning in costs there is per pixel, which
  culling cannot remove, and the owner still felt the drop in the headset. Nothing changes at the lockbar.

## 2. The problem

The close-up study (2026-11-22, Frame, Medieval Madness VPW, headless runs at a fixed 2160 x 2160, dynamic resolution off, eyes pinned
by `lean-hooks.py`):

| Eyes | GPU per frame | Triangles per frame | Draws |
|---|---|---|---|
| Lockbar | 6.70 ms | 585 k | 451 |
| 10 cm above the rules card | 8.82 ms | 1,119 k | 148 |
| 10 cm above the middle of the playfield | 12.66 ms | 1,160 k | 215 |

The target is 11.8 ms (85 % of the 13.9 ms period at 72 Hz). Moving the eyes from the lockbar to the middle and back, the GPU time stays
above the target for 10 to 16 frames at each lean (peaks around 14.4 ms) while dynamic resolution steps down: the stutter.

Two questions of the owner, answered by `cull-hooks.py` (one frame every 5 s, the triangles each part draws, and those whose 3 corners
are not all beyond the same plane of the view of either eye):

**Do the meshes get more detail when leaning in?** Yes. The simplified level of a part is picked from the distance between the eyes
and the closest point of its bounding box (`MeshSimplifier::SelectRange`). At the lockbar `BM_Parts` (the baked playfield and toys,
337,524 triangles at its full mesh) draws its first level, 135 k triangles; up close it draws its full mesh, of which 30 to 43 k are
in the view. Its light maps follow it (they are simplified with it and draw the level it draws).

**Is the culling still active?** Yes, but per part: `Primitive::Render` skips a part whose box is outside the view (the draws fall from
451 to 128 to 215). The large parts of a baked table and their light maps span the table, a corner of them is always in the view, and
then every triangle is drawn:

| Eyes | Drawn | In the view | Worst parts |
|---|---|---|---|
| Lockbar | 474 k | 474 k (99 %) | |
| Above the card | 763 k | 154 k (20 %) | `BM_Parts` 337,524 drawn / 30,348 in view; `LM_GIM_Parts` 109,094 / 48; `LM_GIT_Parts` 107,361 / 0 |
| Above the middle | 830 k | 138 k (16 %) | `BM_Parts` 337,524 / 42,786; `LM_GIM_Parts` 109,094 / 30,859; `LM_GIT_Parts` 107,361 / 1,429; the `LM_GIB_*` light maps 10 to 18 k each, almost none in view |

(Counted per draw, after light map pruning: lower than the render device's triangles of the table above, which count every pass.)

The GPU transforms the vertices of every triangle drawn before it discards those outside the view: they cost vertex work and no
pixels. At the cost measured in the same study (211 k triangles fewer gave 0.8 ms, about 3.8 µs per 1,000), the 620 to 690 k triangles
drawn out of view cost 2.3 to 2.6 ms per frame, enough to bring the middle of the playfield from 12.7 ms to about 10 ms, under the
target, and the card from 8.8 to about 6.5 ms.

The resolution is the other half (2160 to 1512 per eye saved 4.3 ms above the middle), and is what dynamic resolution already trades.

## 3. How it works

### 3.1 While the table loads: the tiles

`Primitive::RenderSetup` makes the index buffer of each part with `MeshSimplifier::CreateIndexBuffer` (`src/steamframe/MeshSimplifier.cpp`):
the part's mesh, then each of its simplified levels, each a range of the buffer. For a part of at least `MeshTiles::kMinTriangles`
(4,000) triangles without animation, the buffer is now always built there (also when the part has no levels), and handed to
`MeshTiles::Build` (`src/steamframe/MeshTiles.cpp`) before it is uploaded:

1. **Cells.** The center of each triangle of each range is placed on the table (the part's matrix at load, `RecalculateMatrices`), and
   falls in a cell of a grid of `kTileSize` = 200 VP units starting at the top left corner of the playfield. The grid is the same for
   every part, so that a light map and the part under it put the same triangles in the same cell. Cells outside the playfield (rooms,
   cabinet) are cells like the others. A part on one cell is not tiled. A triangle longer than a cell (in x or y, on the table) goes
   instead to a cell of a coarser grid, the first whose cells are as long as it (200 << level VPU, levels up to `kMaxLevel` = 5,
   6400 VPU): its own small cell's box would otherwise stretch across the table and always be seen (section 7.1).
2. **Tiles.** The cells used by any range of the part, by level, then in the order of the rows of the table, then from the left, become
   its tiles. The box of each tile (at most about twice its cell), in the part's own mesh space, covers its triangles of every range (a simplified triangle has its own center, so a
   level may place a triangle in a cell next to the one of the mesh's triangles around it).
3. **Order.** Within each range the triangles are grouped by tile (a stable counting sort: their order within a tile is kept), and
   each range records its entries: for each tile, where its triangles start in the range and how many there are.

Only the copy uploaded to the GPU is reordered: `Mesh::m_indices` and `MeshLevels::Level::indices` keep their order (physics, editor,
picking, the light map pruning tables and the levels cache read them).

**Light map pruning.** `LightMapPruning::Prepare` sorts the triangles of each light map, in each range, from the one adding the most
light down, and each level of pruning draws the start of the range (`LightMapPruning::Trim` shortens the count). Grouping by tile would
break that order, so `Build` groups within each share of the sort: the counts kept by High, Medium and Low (`LightMapPruning::GetCounts`,
new) cut each range in up to four shares, and the tiles are grouped inside each share. The triangles kept by each level are still the
start of the range, `Trim` is unchanged, and a change of level in the menu still applies at the next frame.

The cost is a pass over the triangles of the large parts: no meshoptimizer work, nothing to cache. The log line `Culling by tiles: <n>
parts of at least 4000 triangles in <m> tiles of 200 VPU` is written at the first frame. Medieval Madness: 56 parts in 1,312 tiles (1,083 before the coarser cells).

### 3.2 Each frame: the runs

In `Primitive::Render`, after the part's own culling test, `SelectRange` (its level) and `Trim` (light map pruning), a tiled part
calls `MeshTiles::SelectRuns` with the view (`Renderer::GetMVP()`, whose model matrix is the part's) and its range:

1. **Fully in view.** If the box of all its tiles is inside the view of one eye (its 8 corners in front of the eye and within the 4 side
   planes), every tile is seen: one run, the whole range, as before. That is the lockbar case, and it costs one box test.
2. **Tiles.** Otherwise each tile's box is tested with `ModelViewProj::IsBoxVisible`, the test of the part culling: hidden only when,
   for each eye, its 8 corners are all beyond the same side plane or all behind the eye. Conservative: a tile is never left out while
   one of its triangles may show.
3. **Runs.** The entries of the range are walked in the order of the buffer up to the count kept by pruning, and the seen ones make
   runs. Two runs separated by fewer than `kMergeTriangles` (1,024) hidden triangles are merged: a draw costs more than a few hidden
   triangles. A part makes at most `kMaxRuns` (8) draws: beyond, the two runs separated by the fewest triangles are merged.
4. **Draws.** The draws of the part (additive light map, opaque part, the two passes of a transparent part with reflections, the front
   faces of a part drawn with its back faces) loop over the runs with the same state (`drawMesh` in `Primitive::Render`). A part with a
   refraction probe draws its runs as one span: the dependency of its draw on the probe's copy is attached to the next draw command only.
   No run at all: the part is skipped like a culled part.

The runs live in an array on the stack of `Render`, as rendering a part may render a probe first, and so other parts.

**Same level for all the tiles of a part.** The level is still picked for the whole part. Drawing the near tiles at the full mesh and
the far ones at a coarser level would save more, but the levels of a part are simplified as one mesh: next to each other, two levels
leave cracks along the border of their tiles (the background shows through), and the light maps of both overlap or miss there (brighter
or darker seams). Mixing levels needs levels simplified tile by tile with their borders locked (section 8).

### 3.3 Reflections, desktop, cabinet

The tiles work in every view that draws a part: the reflection probe of the playfield (with its mirrored view), the desktop and cabinet
views (where the table is usually entirely in view: one run), the static prerender of the desktop. They are independent of the simplified
meshes setting: a large part without levels is tiled too.

## 4. Settings, menu, log

- `Player.MeshTiles` (`src/core/Settings_properties.inl`, Frame build only, on by default). Not part of the performance profiles: it
  changes nothing on screen.
- Device Performance › Simplified meshes: *Culling by tiles* (live switch; the tiles are always built, so switching it on mid-game works)
  and *Culling by tiles status*: `56 parts in 1083 tiles, 77 % of their triangles left out`, or `drawn whole (off)`, or `No large part on
  this table` (`MeshTiles::GetTiledPartCount`, `GetTileTotal`, `GetSavedShare`, counted per second by `SelectRuns` on the logic thread).
- Log: `Culling by tiles: ...` at the first frame (section 3.1).

## 5. Files

| File | Change |
|---|---|
| `src/steamframe/MeshTiles.h/.cpp` | New: `Build`, `SelectRuns`, switch and statistics |
| `src/steamframe/MeshSimplifier.h/.cpp` | `CreateIndexBuffer` takes the part and returns its tiles, builds the buffer for any large part; `Enqueue` reads `Player.MeshTiles` and resets the statistics when a table starts |
| `src/steamframe/LightMapPruning.h/.cpp` | `GetCounts`: the counts of a range, for `Build` |
| `src/parts/primitive.h` | `m_meshTiles` (Frame build) |
| `src/parts/primitive.cpp` | `RenderSetup` passes the part and keeps its tiles; `Render` selects the runs and draws them (`drawMesh`) |
| `src/core/Settings_properties.inl` | `Player.MeshTiles` |
| `src/steamframe/vrui/Screens.cpp` | The switch and the status line in the Simplified meshes page |
| `steamframe/steamframe.cmake` | The new files |

`primitive.cpp`, `primitive.h` and `Settings_properties.inl` are upstream files with LF line endings; the changes are inside the
`VPX_STEAM_FRAME` blocks the fork already has there.

## 6. Checked on the Mac

Mac build (bgfx Metal), Medieval Madness VPW from the tutorial library, desktop camera placed with `TableOverride` keys of an isolated
`-PrefPath` (camera mode, eyes 5 to 12 cm above the playfield), light map pruning Medium and simplified meshes on (the levels and the
pruning shares were both exercised). Each view captured at frame 600 with the temporary hook `lmshot-hooks.py`, once with
`Player.MeshTiles = 1` and once with `0`, then compared pixel by pixel:

| View | Triangles of the tiled parts left out | Pixels different |
|---|---|---|
| 12 cm above the middle, looking at the castle (simplifier leaving out 22 %) | 19 % | 0 of 1,166,400 |
| 6 cm above the middle, looking down at the inserts (full meshes) | 77 % | 0 |
| Close to the apron (dark) | 94 % | 0 |

Same image to the bit, as expected from a culling that only leaves out triangles outside the view.

Not checked on the Mac: the GPU time (the Mac's GPU says nothing of the Frame's), the stereo views, the CPU cost of the tests.

## 7. Measured on the Frame (2026-11-23)

Headless runs (the close-up study's tools: `perf-hooks.py`, `lean-hooks.py` and `cull-hooks.py` applied to a worktree of the branch,
`lean-series.sh` with `TAG=-off INI_SET="Player.MeshTiles=0"`, then `-on`), Medieval Madness, GPU at its top clock (903 MHz) in every
run. Static views at 2160 x 2160 without dynamic resolution, 120 s each, the first 30 s left out:

| View | Off | On | Triangles drawn (off → on) |
|---|---|---|---|
| Lockbar | 6.82 ms | 6.89 ms | 580 k → 567 k |
| 10 cm above the rules card | 8.79 ms | **6.57 ms** | 1,081 k → 556 k |
| 10 cm above the middle of the playfield | 15.29 ms | 14.16 ms | 1,153 k → 948 k |

Moving runs with the owner's settings (dynamic resolution on), the eyes going from the lockbar in and out every 20 s:

| Leaning in to | Close, off | Close, on |
|---|---|---|
| the rules card | 8.95 ms, resolution 100 % | **6.71 ms**, 100 % (the lockbar: 6.77 ms) |
| the middle of the playfield | 11.96 ms, resolution 91 % on average, 1.3 % of the frames missed | 11.78 ms, 97 %, 1.2 % missed |

- **The rules card** (and the sides in general): most of the table is out of the view and the tiles leave half the triangles out.
  Leaning in there now costs what the lockbar costs.
- **The middle of the playfield**: the eyes, 10 cm above it and looking at it from the player's side, still see the back half of the
  table; the tiles leave out only 18 % of the triangles, about 1 ms. Static runs over the middle vary by about a millisecond from one
  run to the next (the attract mode's lights differ: the bloom pass alone went from 0.40 to 1.24 ms). Most of the extra cost of that
  view is per pixel: rendered at 1512 x 1512 (half the pixels) instead of 2160 x 2160, its two scene passes went from 7.1 to 4.6 ms
  in the close-up study, which puts about 4.9 ms on the pixels and 2.2 ms on the rest. Up close, the playfield and its light maps, added
  one over the other, fill the whole view, where at the lockbar the cabinet and the room take part of it.
- **In the headset** (the owner, same day): leaning in close still drops the performance. Next: what
  the pixels cost there, light map by light map (`Doc/test-hooks/diag-hooks.py`), `docs/Performance on the Steam Frame.md`.
- The log line at the first frame: "Culling by tiles: 56 parts of at least 4000 triangles in 1083 tiles of 200 VPU" (1312 since section 7.1).

### 7.1 Long triangles in coarser cells (fe36765f0, the same day)

The counts of `diag-hooks.py` over the middle showed why the tiles left so little out there: a triangle went to the tile of its center,
so a long one (a rail, the side of a ramp) stretched that tile's box across the table, and the tile was always seen. BM_Parts had 28
of its 64 tiles outsized, the largest box 951 x 2170 VPU; 313 k of its 337 k triangles were drawn, 43 k of them in the view. A
triangle longer than a cell now goes to a cell of a coarser grid, the first whose cells are as long as it (200 << level VPU, up to
6400): no box is larger than about twice its cell (section 3.1). Medieval Madness gets 1312 tiles instead of 1083.

- **Mac**: tiled triangles left out 19 → 31 % (12 cm above the middle), 77 → 99 % (6 cm above the inserts), 94 → 96 % (close to the
  apron). The pixel comparison of section 6 needed one change: two runs no longer catch the attract mode at the same moment, so the
  game is paused and the same moment captured with the tiles on, then off. Identical outside the desktop DMD, which kept running.
- **Frame**, tiles switched on and off every 20 s within one run (`VPX_DIAG_CYCLE`, mode X), which avoids the run-to-run noise above:

| View | Tiles off | Tiles on | Triangles drawn (off → on) |
|---|---|---|---|
| Lockbar | 6.72 ms | 6.75 ms | 589 k → 594 k |
| 10 cm above the rules card | 8.75 ms | **6.27 ms** | 1,117 k → 452 k |
| 10 cm above the middle of the playfield | 14.23 ms | **11.85 ms** | 1,194 k → 970 k |

The card view gained most from the change (BM_Parts drawn: 155 k → 72 k triangles) and is now cheaper than the lockbar. Over the middle
the change itself left out little more (all tiled parts: 679 k → 630 k triangles drawn, 147 k in the view); the 2.4 ms there are what
the tiles save in all, which the runs of the table above, compared from one run to the next, understated. BM_Parts still draws 312 k
triangles over the middle: 65 of its 92 tiles are seen, as the view from 10 cm above the playfield reaches the back of the table.

What the rest of that view costs, measured the same way (`docs/Performance on the Steam Frame.md`): the playfield reflection
0.50 ms at the lockbar and 1.81 ms over the middle (115 k then 300 k triangles drawn in it, already at half size), the light maps
1.28 then 1.85 ms, the transparent parts other than the playfield's reflection (playfield, ramps and their refraction copies) 0.71
then 1.13 ms. The reflection is the next target.

## 8. Limits and what could come next

- **Tile size.** 200 VPU is a guess between draws and culling: 5 x 11 tiles on a standard playfield. Smaller tiles cull closer to the
  view but make more runs; the merge rules bound the draws either way. To tune on the Frame with the counts of `cull-hooks.py`.
- **Level per tile.** Up close the tiles of the far end of the playfield, seen at a grazing angle, are drawn at the full mesh with the
  near ones. Levels made tile by tile, with the borders between tiles locked in the simplifier (`meshopt_SimplifyVertex_Lock` on the
  vertices shared by two tiles), could then be mixed without cracks, each tile drawing the level of its own distance. That changes the
  simplifier, its cache format and the light map pruning tables: a larger piece of work, worth it only if the Frame measurements leave
  close views over the target.
- **Level from the seen tiles.** The level could be picked from the closest seen tile instead of the closest point of the part (a part
  close to the eyes but out of view on that side). Each light map follows its part's level, so the part and its light maps would have to
  agree on the seen tiles within a frame: not done.
- **Upstream.** Nothing here is specific to the Frame but the place of the code: upstream has neither the simplified levels nor the
  pruning, so an upstream version would tile the mesh range only, in `Primitive`, and is simpler. Any table built from large baked
  parts gains when the camera is close (VR, and desktop free cameras).
