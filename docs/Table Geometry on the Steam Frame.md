# Table geometry on the Steam Frame: fewer polygons, culling, and a table optimization pass

*2026-10-25, written on branch `table-geometry` (from fork `master` 4f249e902), now on `master`. Follows opportunity 4 ("scene geometry") of
`Performance on the Steam Frame.md`. The question: what makes heavy tables like Addams Family expensive, can their polygon count be cut
with no visible loss, and should that be a tool run before the table reaches the headset or a pass run by VPX itself. All GPU numbers are
headless runs on the Steam Frame (method of that document, section 2) at a fixed 2160² per eye, with temporary test code that is never
committed (`Doc/test-hooks/geom-hooks.py`, `perf-hooks.py`); the visual checks are lossless captures from the Mac build with the same code.
Paths are relative to the `vpinball` clone unless they start with `Doc/`.*

## 1. Summary

- **The scene pass of heavy tables is paid per triangle, and it is linear.** Every primitive triangle goes through the vertex shader once
  per eye (instanced stereo), every frame (no static prepass in VR), and again in the playfield reflection probe if the part reflects.
  Across five tables the GPU time follows the triangle count at **2.2–2.4 ms per million triangles** (as counted per frame, both eyes
  included in that cost). Addams draws 1.90 million per frame: about 4.2 ms of its 11.7 ms.
- **Polygons can be cut without a visible change, but less than a plain decimation suggests.** meshoptimizer's simplifier, bounded by a
  distance on the table (0.27–0.54 mm), halves Addams (11.7 → 9.5 ms) but visibly thins thin round parts (wire guides) and shifts printed
  artwork by about a texel up close. Weighting normals and texture coordinates 10× more than the default, and letting the bound grow with
  the distance from the player's head, passes a close-up check at a finer pixel density than the headset's, and still takes
  **1.2–1.6 ms off Addams** (30–41 % fewer triangles), 0.5 ms off Iron Man and LOTR, 0.2 ms off Attack from Mars, nothing worth it on
  light tables.
- **Culling parts outside the view is free where rooms surround the player.** Addams' and Attack from Mars' full rooms are hidden by
  their scripts, so nothing is outside the view there; on LOTR, 242 000 triangles of the room are behind or beside the player, and skipping
  them saves 0.6 ms with no visual change at all. It is the one optimization that is exact, and it is now implemented (section 11).
- **Neither reordering the meshes nor Turnip's rendering mode is a lever.** Vertex cache reordering would save at most 2 % of the vertex
  work (the meshes are already in good order) and there are no duplicate vertices to weld; Turnip's direct rendering (`TU_DEBUG=sysmem`)
  costs the same as its tiled default.
- **A first-run pass in VPX is better than a tool that rewrites tables.** Simplifying all of Addams takes 1.4 s of one core on the Frame;
  done at load or at import, cached next to the texture cache, it needs no change to the author's file, follows table updates by itself,
  and can be turned off per table. Section 8 sketches it.

## 2. Where the triangles go

### 2.1 How a primitive is drawn in VR

| Fact | Where |
|---|---|
| Both eyes are drawn by one instanced draw into a 2-layer target: the vertex shader runs once per eye (`gl_Layer = gl_InstanceID`) | `src/shaders/bgfx/vs_basic.sc:46`, `src/renderer/RenderCommand.cpp:230,267` |
| The triangle counter counts a draw once (not per eye) | `src/renderer/RenderCommand.cpp:219` |
| No static prepass in VR: static parts are drawn every frame like the others | `src/renderer/Renderer.cpp:2074` |
| No frustum or bounding volume culling: every visible part is drawn | `Renderer::RenderItem`, `Primitive::Render` |
| The playfield reflection probe redraws every part that has reflections enabled, static ones included | `src/renderer/RenderProbe.cpp:494,498` |
| Vertices are 32 bytes (position, normal, UV as floats); primitive indices are 32-bit | `Vertex3D_NoTex2`, `src/renderer/IndexBuffer.cpp:104` |
| No level of detail for primitives ("Detail level" only affects balls, ramps and rubbers); only Forsyth reordering, at OBJ import | `third-party/include/forsyth.h` |

### 2.2 Addams Family, one frame (at the pinned pose, `VPX_PERF_GEOM_LOG`)

| | Triangles | Share |
|---|---|---|
| Scene pass, primitives | 1 444 164 | 76 % |
| Reflection probe, primitives | 445 297 | 23 % |
| Outside both eyes' view | 12 | 0 % |
| Counter (all parts, all passes) | 1 903 470 | |

The largest parts are the table's own: the Packard (64 000 triangles), then pairs of the same plastic or metal part lit two ways
(`RGP9_GI1_5` and `RGP9_GI1_5off`, `rgp7_switches_GI1_UVreduced` and `…UVredOFF`, `ChairPrim` and `ChairPrimOff`, 25 000–45 000 each).
Both layers of each pair are always drawn: the script never touches the "off" layers, and both materials are flagged transparent, the
"on" texture blending over the "off" one. This is how the table renders its general illumination; dropping a layer changes the look, so
it is the author's choice, not an optimization. The full VR room (117 toy parts, 640 000 triangles) is hidden by the script
(`VR_Room_Minimal`) and costs nothing (`VR Rooms on the Steam Frame.md`, section 4.6).

## 3. Method

1. **Survey** (`Doc/test-hooks/mesh-survey.py`): reads each `.vpx` without VPX, decompresses the primitive meshes, and asks meshoptimizer
   how many triangles survive a world-space error bound (VP units, 1 VPU ≈ 0.54 mm), with normals and UVs weighted 1 ("strict") or
   10, plus a vertex cache simulation.
2. **Hook build** (`Doc/test-hooks/geom-hooks.py`, `TEMP-PERF-GEOM`): compiles meshoptimizer (from the bgfx tree) into VPX and adds
   - `VPX_PERF_SIMPLIFY=<VPU>`: the GPU buffers of a static primitive are built from a simplified copy of its mesh at `RenderSetup`;
     physics, bounds and the mesh itself are untouched. Skips animated meshes, grouped parts, light maps and meshes under 500 triangles.
     `VPX_PERF_SIMPLIFY_W` weighs normals and UVs (default 1), `VPX_PERF_SIMPLIFY_DIST=1` multiplies the bound by the part's distance
     from the pinned head position divided by 0.5 m;
   - `VPX_PERF_CULL=1`: a part whose transformed bounding box is outside every eye's view (the mirrored view in the probe pass) is not
     drawn;
   - `VPX_PERF_GEOM_LOG=1`: triangles per pass of one frame, the share outside the view, the 30 largest parts, and the simplification
     summary (meshes, triangles, milliseconds).
3. **Frame runs** (`Doc/test-hooks/geom-series.sh`): fixed 2160², bgfx GPU profiler, autoplay, 120 s per run, 40 s rests, the base run
   repeated within each series. Run-to-run noise of the runtime GPU time: ±0.1 ms (Addams base 11.75 / 11.70 / 11.57 ms).
4. **Visual check** (Mac build, same hook): `-CaptureAttract 2 30 <table> noloop` writes lossless frames; a camera at 25° field of view
   on a 2880×1620 window puts about 0.18 mm in a pixel at the apron (the headset: about 0.4 mm at 0.5 m), one view on the flippers and
   apron, one on the upper playfield. Lamp timing differs from run to run, so two base runs give the noise floor; a pixel counts as
   changed when the variant differs from the first base by more than 24 levels where the two base runs agree within 6.

## 4. Library survey

Share of the visible primitive triangles kept, per error bound (light maps, animated parts and meshes under 500 triangles left as
they are):

| Table | Visible triangles | Strict 0.5 VPU | Strict 1 VPU | Weight 10, 0.5 VPU | Weight 10, 1 VPU |
|---|---|---|---|---|---|
| Addams Family (VR room) | 1 973 475 | 53 % | 42 % | 80 % | 71 % |
| Lord of the Rings (VPW) | 1 291 147 | 85 % | 77 % | 98 % | 94 % |
| Attack from Mars (minimal) | 330 096 | 53 % | 43 % | 84 % | 72 % |
| Street Fighter 2 (JP) | 218 522 | 93 % | 83 % | 98 % | 96 % |
| Iron Man (minimal) | 210 474 | 37 % | 31 % | 63 % | 50 % |
| Rocky | 176 690 | 74 % | 70 % | 85 % | 82 % |
| Mushroom World | 145 142 | 62 % | 55 % | 82 % | 74 % |
| Ghostbusters | 88 454 | 76 % | 66 % | 92 % | 88 % |
| Terminator 3 (JP) | 84 080 | 93 % | 86 % | 99 % | 98 % |
| Friday the 13th, Pokémon, Lost World (JP) | 67 000 each | 87–90 % | 74–80 % | 98–99 % | 96–97 % |

The tables built from detailed scans or exports (Addams, Attack from Mars, Iron Man) carry the most redundant geometry; JP's and VPW's
tables are already lean. Vertex cache reordering would shade 98 % of the vertices it shades today on Addams, 97 % on LOTR, ~100 %
elsewhere; no table has two identical vertices.

## 5. Measurements on the Frame

Runtime GPU time per frame (the compositor's `gpu_frametime`), triangles per frame (the render device's counter, all parts and passes),
and the bgfx passes that change. All at 72 Hz, fixed 2160².

### 5.1 Addams Family

| Run | Triangles | GPU | Scene | Probe | Load time of the pass |
|---|---|---|---|---|---|
| base (3 runs) | 1 903 470 | 11.67 | 6.26 | 1.34 | |
| strict 0.5 VPU | 941 148 | 9.49 | 4.54 | 0.92 | 1.44 s |
| strict 0.5 VPU, by distance | 784 819 | 9.31 | 4.37 | 0.84 | 1.44 s |
| strict 1 VPU | 748 460 | 9.13 | 4.30 | 0.82 | 1.39 s |
| strict 2 VPU | 628 818 | 8.90 | 4.16 | 0.75 | 1.47 s |
| **weight 10, 0.5 VPU, by distance** | 1 340 814 | **10.41** | 5.21 | 1.11 | 1.45 s |
| **weight 10, 1 VPU, by distance** | 1 131 456 | **10.03** | 4.91 | 1.02 | 1.44 s |
| cull | 1 903 487 | 11.85 | 6.26 | 1.34 | |
| cull + strict 0.5 VPU by distance | 784 535 | 9.13 | 4.30 | 0.84 | |
| `TU_DEBUG=sysmem` | 1 903 232 | 11.52 | 6.19 | 1.34 | |

The fit over the simplified runs: 7.5 ms that do not depend on triangles (post-processing 1.57, the compositor's swapchain pass 1.6,
bloom, the pixels of the scene) plus 2.2 ms per million triangles. The render thread's submission time drops a little with the
triangles (1.71 → 1.54 ms), the draw count does not change (330).

### 5.2 Other tables

| Table | Run | Triangles | GPU |
|---|---|---|---|
| Iron Man (minimal) | base | 498 940 | 10.13 |
| | strict 1 VPU | 216 500 | 9.47 |
| | weight 10, 0.5 VPU, by distance | 288 721 | 9.65 |
| | cull | 498 988 | 10.21 |
| Attack from Mars (minimal) | base (2 runs) | 719 423 | 9.69 |
| | strict 1 VPU | 356 901 | 9.01 |
| | strict 0.5 VPU, by distance | 390 840 | 9.16 |
| | weight 10, 0.5 VPU, by distance | 582 664 | 9.48 |
| | cull (nothing outside the view: same as base) | 720 740 | 9.82 |
| Lord of the Rings | base | 1 579 681 | 18.11 |
| | strict 0.5 VPU | 1 254 045 | 17.22 |
| | weight 10, 0.5 VPU, by distance | 1 423 748 | 17.61 |
| | cull (242 000 room triangles outside the view; single run, see section 11 for the A/B) | 1 338 346 | 17.21 |
| Ghostbusters | base | 273 087 | 9.95 |
| | weight 10, 0.5 VPU, by distance | 256 628 | 9.75 |
| | cull (98 triangles outside the view, in the probe) | 273 087 | 9.72 |

- **LOTR does not hold 72 Hz at 2160²** with or without these changes (36 fps, 845 draws, bloom 2.3 ms): dynamic resolution is what keeps
  it playable. It is the heaviest table of the library and the one where culling pays, because its room (Balrog, towers) surrounds the
  player. The bgfx profiler swaps the labels of its bloom and post-process passes between runs (2.32 / 1.65 against 1.25 / 2.55 ms); their
  sum is the same.
- Ghostbusters is limited by pixels (its sky room, `VR Rooms on the Steam Frame.md`), not triangles: 0.2 ms, within noise.

## 6. Visual check

Changed pixels against the first base run (method in section 3; the base against base row is 0 by construction):

| Variant | Addams, flippers and apron | Addams, upper playfield | LOTR, flippers and apron | LOTR, upper playfield |
|---|---|---|---|---|
| strict 0.5 VPU | 1.1 % | 2.6 % | 0.4 % | 0.7 % |
| strict 1 VPU | 1.4 % | 2.6 % | 0.8 % | 1.1 % |
| strict 2 VPU | 1.9 % | 1.6 % | 0.4 % | 1.7 % |
| weight 10, 0.5 VPU | 0.2 % | 1.6 % | 1.6 % | 1.9 % |
| weight 10, 0.5 VPU, by distance | 0.2 % | 1.7 % | 1.0 % | 3.1 % |
| weight 10, 1 VPU, by distance | 0.6 % | 1.7 % | 1.4 % | 1.8 % |

The percentages alone mislead: most changed pixels on the upper playfields and on LOTR are inserts, flashers and the score display caught
at another moment of the attract sequence (the "stable between base runs" mask cannot remove a lamp that both base runs caught off). The
difference images, looked at one by one, show what geometry did:

- **Strict weights, every bound:** the metal wire guides beside the slingshots come out thinner and less round (a 3 mm wire loses a
  visible share of its radius to a 0.27 mm bound), and the printed apron artwork (`The Addams Family` logo) moves by about a texel: the
  flat apron was re-triangulated and its texture mapping followed the new triangles. At 2 VPU the wire's bend changes shape. Not
  acceptable up close.
- **Weight 10, 0.5 and 1 VPU, by distance:** the wire guides and the logo differ only by single edge pixels at 0.18 mm per pixel;
  nothing is visible side by side. The rest of the changed pixels are lamps.
- **LOTR, light maps included** (`VPX_PERF_SIMPLIFY_LM=1`, 1 VPU): 1.0 % against 1.1 % without, so simplifying its few light maps
  (2 000 triangles) did not break their overlap with the base meshes. Coincident layers are a risk in general, though: LOTR's
  `wireramps` and `wireramps_off` are near-identical meshes that simplify to 15 970 and 15 938 triangles, so the two layers no longer
  match exactly. A production pass should simplify one mesh of such a pair and reuse the result for the other (section 8).

The headset check by the owner is still needed: the captures are a desktop camera at a finer pixel density than the headset, but without
head motion, where a re-triangulated surface could show in its specular highlights.

## 7. Options, ranked

| # | Option | Gain measured | Visual cost | Work | Upstream fit |
|---|---|---|---|---|---|
| 1 | Simplify primitive meshes for rendering, weight 10, bound by distance (0.5–1 VPU at 0.5 m) | Addams −1.3 to −1.6 ms, Iron Man −0.5, LOTR −0.5, AFM −0.2 | None seen up close; needs the worn check | Medium (section 8) | Good, as an option off by default |
| 2 | Cull parts outside every eye's view (bounding box per part per frame, probe included) | LOTR −0.6 ms (A/B); 0 where rooms are hidden | None (exact) | Small: one test in `Primitive::Render`, then other part types | Very good: generic, also helps desktop and cabinets with big rooms |
| 3 | Coarser mesh in the reflection probe only (strict 2 VPU there, as the probe is blurred and dim) | Addams probe 1.34 → 0.75 ms (strict 2 VPU run) | Low: reflections are faint; to check | Small once option 1 exists: a second index buffer per mesh | Good |
| 4 | Quantized vertices (positions 16-bit, normals and UVs half or octahedral: 32 → 16–20 bytes) and 16-bit indices where a mesh allows | Not measured; vertex fetch is part of the 2.2 ms per million triangles | None if done right | Medium: vertex layout and shaders, all part types | Upstream topic |
| 5 | Hardware clip plane in the probe instead of `discard` (`fs_basic.sc` `CLIP` variants) | Not measured; keeps early depth test in the probe | None | Small | Upstream |
| 6 | Strict simplification (weight 1) | Addams −2.2 to −2.8 ms | Visible on thin wires and printed artwork | | Only as a "performance" level, if ever |
| 7 | Vertex cache reordering, welding | ≤ 2 % of vertex work | None | | Not worth it |
| 8 | Turnip direct rendering (`TU_DEBUG=sysmem`) | 0 (11.52 against 11.67 ms) | | | Not a lever |

Options 1 and 2 add up: they remove different triangles. Option 3 matters on tables whose parts mostly reflect (Addams: the probe draws
23 % of the triangles).

## 8. The optimization tool: where it should run

| | (a) Offline tool rewriting the `.vpx` | (b) Pass in VPX, cached (recommended) | (c) Pass at import, in the library |
|---|---|---|---|
| How | Read the table (vpxtool, already used by `steamframe/lobby-room/tools/export_vpx.py`), simplify, write a new `.vpx` | At load, before the GPU buffers are built (`Primitive::RenderSetup`); the result cached in `cache/<title>/meshes/` | The same pass, run when a table is added (`TableLibrary::Import`, `src/steamframe/TableLibrary.cpp`), so the first play is already fast |
| Author's file | Changed (and its MD2 integrity hash) | Untouched | Untouched |
| Table updates | Re-run by hand | Automatic (cache key = hash of the mesh) | Automatic |
| Physics | Must keep the original mesh for collisions | Untouched: only the render buffers change | Untouched |
| Per-table control | Two files | A setting, and a per-table override on the loading page (as texture compression has) | Same |
| Cost | A Mac/PC step | 1.4 s of one core on Addams, once (0.2 s AFM, 0.15 s Iron Man, 0.9 s LOTR); spread over the loading thread pool, a fraction of the 4 s texture phase | Same, during import |

(a) gains nothing over (b) and adds a step, a second copy of every table and a modified author's file. (b) is the texture compressor's
pattern (`src/renderer/TextureCompressor.{h,cpp}`): a cache folder per table, one file per mesh keyed by the MD5 of its vertex and index
data plus the simplifier's settings and a format version, written to `.tmp` then renamed, files not used in a session cleaned up, one
statistics line in the log. (c) is a few lines on top of (b): call the same pass for every primitive of the imported table.

What the pass needs, from these runs:
- meshoptimizer (MIT, already in the bgfx tree) linked into VPX: `allocator`, `indexgenerator`, `simplifier`, `vcacheoptimizer`,
  `vfetchoptimizer`;
- weight 10 on normals and UVs, a bound of 0.5–1 VPU at 0.5 m from the head, growing with distance (the head position from the VR view
  setup, not a pinned guess);
- skip animated meshes, grouped parts (or simplify the group), light maps, additive parts and small meshes; keep the original mesh for
  physics, the editor and the probe's bounds;
- meshes that coincide (same index buffer, positions within a fraction of a VPU, as GI and light map layers do) are simplified once and
  share the result, so stacked layers stay exactly on top of each other;
- an index count kept apart from `m_mesh` (`Primitive::Render` draws `m_mesh.NumIndices()` today), and no re-upload of the original mesh
  over the simplified buffer.

## 9. Other findings

- **PinMAME crash.** One Attack from Mars run died after 28 s with SIGSEGV in PinMAME's DCS sound CPU (`adsp2100_execute`,
  `libpinmame.so.3.7.0`), not in VPX; the run was repeated twice without a crash, and the other runs of the table were fine. Worth a look if it happens in
  normal play.
- The headless runs keep the eyes at one place, so the share of a room outside the view is the share at a player's place looking at the
  playfield; looking around changes it frame by frame, which the culling test follows.

## 10. Reproduction

```sh
# Survey (any machine): a venv with olefile, numpy, meshoptimizer
python3 -m venv venv && venv/bin/pip install olefile numpy meshoptimizer
venv/bin/python Doc/test-hooks/mesh-survey.py <table.vpx>...

# Hook build, in the table-geometry worktree (never commit the hooks)
cd vpinball-geom && python3 ../Doc/test-hooks/geom-hooks.py apply && python3 ../Doc/test-hooks/perf-hooks.py apply
cd .. && VPX_SRC=vpinball-geom VPX_LINUX=vpinball-sync-linux VPX_VOLUMES=vpx-sync Doc/steam-frame-build/build-linux-arm64.sh app
# copy the build out of the volume into ~/perf-test/vpx-geom2 on the Frame (recipe of "VR Rooms on the Steam Frame.md", section 8),
# copy geom-series.sh, then on the Frame, with no VPX running:
sh geom-series.sh results-geom ad:base ad:w10d05 ad:w10d1 ad:cull ad:base+2 lotr:base lotr:cull
python3 Doc/test-hooks/perf-report.py <results folder> 40          # on the Mac
cd vpinball-geom && python3 ../Doc/test-hooks/geom-hooks.py remove && python3 ../Doc/test-hooks/perf-hooks.py remove

# Visual check on the Mac: the hook build of vpinball-geom (cmake --build build), a -PrefPath folder whose [TableOverride] sets
# ViewDTMode = 1, ViewDTFOV = 25, ViewDTLookAt = 10 (or 60), the camera keys to their defaults, and a 2880x1620 window; then
#   VPX_PERF_SIMPLIFY=0.5 VPX_PERF_SIMPLIFY_W=10 VPX_PERF_SIMPLIFY_DIST=1 VPinballX_BGFX -PrefPath <folder> -CaptureAttract 2 30 <table> noloop
# (frames in <table folder>/Capture, QOI), twice without the variables for the noise floor.
```

Gotchas met: the worktree needs `external/` (`cp -cR ../vpinball/external .`) and the git-ignored headers of `third-party/`
(`rsync -a --ignore-existing ../vpinball/third-party/ third-party/`) before its first Mac build; the hook's change to `primitive.h`
recompiles many files, and the Linux container (8 GB) ran out of memory once at `-j$(nproc)`: running the build again finishes it.

## 11. Culling, implemented (2026-10-25)

Commit 24a3b391b "Skip primitives entirely outside the view" on `master` (technical notes: `Culling Parts Outside the View.md`),
a generic change kept as its own commit for an upstream pull request (rule 4 of `steamframe/README.md`):

- `ModelViewProj::IsBoxVisible` (`src/math/ModelViewProj.h`): an object space box, placed by the current model matrix, is hidden only
  when, for every eye, its 8 corners are all beyond the same side plane of the view or all behind the eye. Conservative: a box crossing a
  corner of the view outside of it, or beyond the far plane, is kept.
- `Primitive::Render` calls it right after setting the part's matrix, with the bounds of the static mesh, and returns before any draw
  when the part is hidden. As it reads the current view, the mirrored view of a reflection probe is tested too. Animated meshes (their
  frames may leave the base bounds), grouped parts (drawn from a merged, pre-transformed buffer) and UI passes are always drawn.

A/B on the Frame (`cull-ab`: the previous build and this one alternated, two runs each per table, fixed 2160², same series):

| Table | Without culling: GPU, triangles | With culling: GPU, triangles | Difference |
|---|---|---|---|
| Lord of the Rings | 18.02 ms, 1 580 000 | 17.43 ms, 1 344 000 | −0.6 ms (scene pass 9.0 → 8.5 ms) |
| Addams Family | 11.56 ms | 11.72 ms | none: bgfx passes 10.90 against 10.91 ms (two spikes in the culled runs) |
| Iron Man | 10.17 ms | 10.12 ms | none |
| Ghostbusters | 9.75 ms | 9.77 ms | none |

The test costs nothing measurable on tables where it culls nothing (the render thread's times are unchanged). The single runs of
section 5.2 had suggested 0.9 ms on LOTR; the alternated A/B, less exposed to drift between runs, gives 0.6 ms. The visual check of
section 3 (Mac, desktop camera) shows no change beyond lamp timing; it still needs a check in the headset, looking around, where
parts leave and enter the view.

