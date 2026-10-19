# VR rooms on the Steam Frame: what they cost, and what can be done about it

*2026-10-23. Investigation notes, written so that a later session can continue it. Follows opportunity 9 of
`Performance on the Steam Frame.md` (section 7.2 there measured the first numbers). All measurements are headless runs on the Steam Frame
(method of that document, section 2) with temporary test code that is never committed: `Doc/test-hooks/room-hooks.py` and
`perf-hooks.py`. Paths are relative to the `vpinball` clone unless they start with `Doc/`; line numbers are those of fork `master` on
2026-10-23.*

## 1. Summary

- A VR room is ordinary table content: primitives (and lights) that the table's script shows or hides. VPX has no notion of a room; the
  engine sees parts like any other.
- **Ghostbusters** (the reference table, a sky sphere and a floor): the room costs **about 1.95 ms of GPU per frame** at 2160² (runtime
  GPU 9.6 ms with it, 7.6 ms without), or a fifth of the frame. Of that, 1.2 ms in the scene pass, 0.37 ms in FXAA, 0.19 ms in bloom,
  0.08 ms in the tonemap; nothing in the playfield reflection (section 4.3).
- **What the cost is made of** (section 4.3): the shader's lighting math is small (0.2 ms), reading the sky texture is the largest part
  that can be cut (0.65 ms), and about 1.3 ms is the cost of covering the pixels at all (rasterizing, depth testing and writing them,
  then FXAA, tonemap and bloom reading them), which only drawing fewer room pixels removes.
- **Ruled out by measurement**: drawing the room later in the frame (it already is, section 4.4); leaving FXAA off on far pixels (0.07 ms,
  section 4.5); a lighter shader for unlit rooms (0.2 ms at best). **Ruled out by the platform**: handing the sky to the runtime as a
  panorama layer (SteamVR on the Frame has no equirect, cube or cylinder layer, section 5).
- **Hidden rooms cost no GPU time** (verified on Addams, section 4.6): hidden primitives are not drawn, lights that are off are not drawn,
  and room parts are usually "toys" without physics. What remains is the script that hides them, 0.15 ms of the logic thread per frame on
  Addams, which does not limit anything.
- **Generic options still open** (section 6): half-rate shading for sky-like parts (up to about 0.5 ms on a Ghostbusters-type room);
  a cheaper read of the sky texture (to be checked first, section 7.1); skipping parts outside both eyes' view, for modelled rooms
  (unmeasured).
- **Tables differ** (section 2): of the 48 tables on the Frame, 24 have a simple room (sky sphere or a few boxes, under 25 000 triangles),
  7 a modelled room, 17 a heavy one (150 000 to 2 million triangles, counting every room variant a table holds). The Ghostbusters results
  apply directly to the simple rooms; the heavy ones are bound by geometry, which these experiments did not measure.

## 2. How VR rooms are built in tables

### 2.1 Ghostbusters (`944386878_VRROOMGhostbustersslimer2.1.vpx`)

The script's `VRRoom` variable (0 off, 1 full 360° room, the default, 2 minimal room) shows the collection `BigRoom` or `VRRoomMin`:

| Part | Collection | Triangles | Texture | Shading |
|---|---|---|---|---|
| `Primary_environment` | `BigRoom` | 1 104 | `creepy kid`, 3072×1536 JPEG | Sphere 50 000 units across (27 m), centre z −1000, unlit (disable lighting 1), material `colormaxnoreflectionhalf` (base 828282, no glossy, no clearcoat), static rendering |
| `Primary_Scifi Floor` | `BigRoom` | 98 | `green-grunge`, 800×800 | Lit (two scene lights), material `_noXtraShadingVR` (base 282828); its normal map `Wood_Wall_002_normal` is missing from the table, so it is not normal-mapped; reflection enabled; z −1670 |
| `VR_Floor`, `VR_Roof`, `VR_Wall_Left`, `VR_Wall_Right` | `VRRoomMin` | 12 each | 2000×2000 / 1000×1000 | Lit boxes, disable lighting below only |

The cabinet (`VRCab`, 10 parts) is always shown. The sky texture holds about 8.5 texels per degree (3072 over 360°), while the display
at 2160² shows roughly 20 pixels per degree: the sky is magnified about 2.5 times, it has less detail than the pixels it covers.

### 2.2 Addams Family (`VR ROOM Addams Family (Bally 1992) v2.0.vpx`)

Two rooms in the table: the full one, collection `VR_Stuff` (117 primitives, about 640 000 triangles: a furnished living room with sofa,
chairs, chandelier, lamp, fireplace, tables, rug, posters, windows and a Donkey Kong cabinet) with 10 lights in `Uplights`, and the
minimal one, `VR_Min` (4 boxes, 48 triangles). The choice is a constant, `Const VR_Room_Minimal = 1` (line 75 of the script), applied
by `GameTimer1_Timer` (10 ms interval) on every tick: it sets `visible` on all 121 parts and `state` on the 10 lights, 100 times a second.
The table's own F6 menu (Fester, Cousin It, Thing's box...) does not include the room. All 117 parts of the full room are toys (no
physics) and all three collections have "group elements" on (section 3.4).

### 2.3 The library on the Frame

`Doc/test-hooks/vpx-inspect.py survey` lists, for each table, the primitives outside the cabinet's volume (larger than 3000 units, or
entirely beyond the cabinet), and `script` gives the script, where the room choice was looked for. The counts include every room variant
stored in a table (the Ext2k conversions carry several, the full and minimal rooms of a table are both counted) and parts that the
script hides, so the triangles of the heavy rooms are upper bounds of what is drawn. Kinds: simple under 25 000 triangles, modelled under
150 000, heavy above. "Room choice": *table option* is a `Table1.Option("VR Room", ...)` (shown in the Frame's menu, This table → Table
Options, and applied live through `Table1_OptionEvent`, see `src/steamframe/vrui/Screens.cpp:49` and
`src/ui/live/ingameui/TableOptionsPage.cpp:37`); *own key* a room cycled by a MagnaSave key and saved with `SaveValue`; *script constant*
a value to edit in the script (or in a `<table>.vbs` next to the table, which VPX loads instead of the embedded script); *-* no choice
found (most of these tables have a single room).

| Table | Kind | Room parts | Room triangles | Lit | Normal-mapped | Glossy | Largest background part | Room choice |
|---|---|---|---|---|---|---|---|---|
| VR ROOM Super Mario Bros v 1.2 | simple | 5 | 1,072 | 3 | 0 | 0 | - | - |
| VR ROOM JPs Terminator 3 Rise of the Machines (Stern 2003) 1.1 | simple | 5 | 1,072 | 0 | 0 | 0 | - | - |
| JP's The Lost World Jurassic Park (Sega 1997) v4 VR Room | simple | 5 | 1,072 | 1 | 0 | 0 | - | - |
| JP's Pokemon Pinball VR Room | simple | 3 | 2,226 | 2 | 0 | 0 | `Primary_environment` (40,000 units, unlit, 3072x1536) | - |
| Godzilla (Sega 1998) VR Room | simple | 16 | 2,654 | 12 | 0 | 11 | - | - |
| onepiece | simple | 6 | 3,220 | 2 | 0 | 2 | - | - |
| BleachVR | simple | 6 | 3,220 | 2 | 0 | 1 | - | - |
| Twilight Zone SG1bsoN Mod V3 (Bally, 1993) (VR ROOM) Minimal | simple | 7 | 3,284 | 3 | 0 | 3 | - | - |
| 944386878_VRROOMGhostbustersslimer2.1 | simple | 7 | 3,394 | 6 | 0 | 0 | `Primary_environment` (50,000 units, unlit, 3072x1536) | script constant |
| JP's Friday the 13th VR | simple | 6 | 3,488 | 4 | 0 | 2 | - | - |
| Star Wars Trilogy Siggis MOD1.1 (Stern, 1997) VR ROOM Minimal v1.1 | simple | 7 | 3,956 | 2 | 0 | 2 | - | - |
| JP's Street Fighter 2 VR Room 1.1 | simple | 4 | 4,374 | 3 | 0 | 2 | `Primary_environment` (40,000 units, unlit, 5000x2500) | - |
| JohnWick (BABAYAGA Pinball edition, 2023) (VR ROOM Minimal) Sphere | simple | 9 | 4,500 | 4 | 0 | 3 | - | - |
| VR Room Pirates of the Caribbean Siggis Mod v1.0 | simple | 5 | 7,846 | 3 | 0 | 3 | - | - |
| VR Room Evel Knievel (Bally 1977) 1.0.2 | simple | 6 | 8,004 | 1 | 0 | 1 | - | - |
| VR ROOM Starship Troopers (Sega 1997) v2.1 | simple | 9 | 9,096 | 7 | 0 | 7 | - | - |
| VR Room - The Simpsons (Data East 1990) v1.04 | simple | 11 | 11,376 | 6 | 0 | 6 | - | - |
| VR Room Rocky (Gottlieb 1982) 1.0.1 | simple | 10 | 12,870 | 5 | 0 | 3 | `VR_Sphere` (40,000 units, unlit, 3072x1536) | own key (MagnaSave), saved |
| James Bond 007 (Gottlieb 1980) 1.1.0 | simple | 11 | 12,882 | 5 | 0 | 3 | `VR_Sphere` (40,000 units, unlit, 2048x1024) | own key (MagnaSave), saved |
| VR Room Iron Man Minimal (Stern 2010) v1.0 | simple | 8 | 13,718 | 6 | 0 | 2 | - | - |
| VR Room - Super Mario Bros. Mushroom World (Gottlieb 1992) v1.0.2 | simple | 20 | 17,334 | 12 | 0 | 8 | - | - |
| VR Room Stargate Minimal (Gottlieb 1995) v1.0 | simple | 26 | 19,173 | 24 | 0 | 24 | - | - |
| VRROOM Tmnt2-1.1 | simple | 14 | 21,754 | 9 | 0 | 8 | `Primary_environment` (50,000 units, unlit, 3072x1536) | script constant |
| VR ROOM Attack from Mars Minimal (Bally 1995) (g5k 1.1) v1.0 | simple | 26 | 24,284 | 21 | 0 | 1 | - | - |
| VR ROOM The Walking Dead LE Minimal v1.0 | modelled | 7 | 41,811 | 5 | 0 | 5 | - | - |
| Bram Stoker's Dracula (Williams 1993)_Bigus(MOD)2.0 VR | modelled | 20 | 54,644 | 20 | 0 | 7 | - | - |
| VR ROOM Robocop (Data East 1989)_drakkon(mod_1.2) | modelled | 15 | 59,072 | 11 | 0 | 3 | - | - |
| Symphony IX - The Machine of Joy (Original 2026) | modelled | 35 | 68,850 | 4 | 0 | 4 | `VR_SixSky` (1,800,000 units, unlit, 4096x2048) | table option |
| VR Room The Flintstones (Mod) v1.27 | modelled | 38 | 80,997 | 36 | 2 | 11 | - | - |
| Blade Runner 2049 (2021) nFozzy Flex Edition | modelled | 12 | 106,638 | 6 | 0 | 2 | `Primitive2` (484,157 units, lit, -) | - |
| South Park (Sega 1999)_Bigus(MOD)2.0 VR | modelled | 45 | 126,872 | 42 | 1 | 26 | - | - |
| Playboy 35th Anniversary (Data East 1989) v4.3 JP Salas, Ext2k VRROOM | heavy | 29 | 203,919 | 26 | 0 | 8 | - | - |
| VR ROOM Austin Powers (Stern 2001) V101 | heavy | 26 | 214,929 | 26 | 0 | 12 | - | - |
| Bond 60th Dr.No Marty02 Ext2k Conversion | heavy | 62 | 274,808 | 52 | 0 | 22 | - | - |
| The Mandalorian VR ROOM Mega Razor Crest | heavy | 20 | 275,703 | 15 | 0 | 3 | `VR_Sphere` (300,000 units, unlit, 3072x1536) | - |
| VR_ROOM_Indiana_Jones_-_The_Pinball_Adventure_(Williams_1993)_temple_r | heavy | 16 | 286,155 | 16 | 4 | 2 | - | - |
| Wrath of Olympus (Original 2022) JP v5.5.1 DT-FS-VR-MR Ext2k Conversio | heavy | 81 | 356,726 | 67 | 2 | 31 | `VR_Sphere_MR` (40,000 units, unlit, -) | table option |
| Apollo (Williams 1967) JP v55 DT-FS-VR-MR Ext2k Conversion | heavy | 82 | 360,990 | 67 | 2 | 29 | `VR_Sphere_MR` (40,000 units, unlit, -) | table option |
| Star Wars (Sonic 1987) mfuegemann DT-FS-VR-MR Ext2k Conversion v1.2.vp | heavy | 82 | 361,390 | 66 | 2 | 31 | `VR_Sphere_MR` (40,000 units, unlit, -) | table option |
| VR ROOM Ace Of Speed 1.1_nFozzy_Fleep | heavy | 234 | 407,926 | 231 | 0 | 49 | `pRampRight` (1,001,961 units, lit, 2048x2048 png) | - |
| JAWS 50Th Anniversary (Original 2025) Marty02 v3.2 DT-FS-VR-MR Ext2k C | heavy | 99 | 443,282 | 84 | 2 | 34 | `Primitive079` (100,209 units, lit, 4096x2048) | table option |
| VR ROOM Addams Family (Bally 1992) v2.0 | heavy | 117 | 644,707 | 110 | 0 | 36 | - | script constant |
| JP's Space Cadet (Original 2021) v5.7 DT-FS-VR-MR Ext2k Conversion | heavy | 73 | 674,266 | 59 | 11 | 13 | `Sky` (120,000 units, lit, 4096x2048) | table option |
| Scooby Doo 2022 v1.0 VR ROOM | heavy | 119 | 717,320 | 118 | 71 | 16 | `VR_Sphere1` (150,000 units, lit, 4096x2048) | - |
| Lord of the Rings (Stern 2003) VPW 1.6 | heavy | 127 | 740,477 | 59 | 0 | 6 | `VR_Sphere` (357,626 units, lit, 8192x4096 png) | table option |
| VR ROOM Maverick (Data East 1994)  v1.0 | heavy | 130 | 1,124,551 | 130 | 0 | 116 | `Floor2001` (39,375 units, lit, 736x920) | script constant |
| Ulysse 31 custom & MEGA VR ROOM | heavy | 15 | 1,573,008 | 12 | 1 | 0 | `VMR_Outpost` (1,878,156 units, lit, 4096x4096 png) | - |
| Masters of the Universe-custom (VR ROOM edition) | heavy | 32 | 1,986,740 | 32 | 1 | 19 | `plaforme` (267,767 units, lit, 2048x2048 png) | - |

Addams appears among the heavy tables because of its hidden full room: its minimal room is 48 triangles, and its 1.9 million triangles
per frame are the table and the cabinet (section 4.6). The "largest background part" column only lists parts larger than 20 000 units;
an extent beyond a million units is a primitive scaled up, not necessarily a sky.

## 3. How VPX renders a room

### 3.1 Draw path of a room primitive

`Primitive::Render` (`src/parts/primitive.cpp`) returns at once for a hidden part (`if (!m_d.m_visible || m_skipRendering)`, line 889),
then draws it with the basic material shader (line 1034, "Default lit primitive rendering"): technique `basic_with_texture` or
`basic_without_texture`, with an alpha test variant when the texture has alpha (`_at`). There is no frustum culling anywhere: every
visible part is submitted every frame, for both eyes (one instanced draw for the two layers), whether it is in view or behind the player.

### 3.2 The material shader for an unlit part

`fs_basic.sc` and `lightLoop` (`src/shaders/bgfx/material.sh:193`). "Disable lighting" (1.0) does not make a part flat: the point light
loop is replaced by `float(iLightPointNum) * diffuse` (line 233, "old bug kept for backward compatibility"), but the environment
lighting (line 242, one more texture read, `DoEnvmapDiffuse`) is still added, and the glossy and clearcoat lookups run when the material
has them. The specular antialiasing (normal variation from derivatives), the normal flip and the energy conservation run for every pixel.
A lighter shader that keeps the image identical must keep the environment term.

Blending: `Shader::SetMaterial` (`src/renderer/Shader.cpp:848`) only enables alpha blending when the texture has alpha or the material
opacity is below 0.999. The Ghostbusters sky has "opacity active" at 0.9999, so it is drawn opaque, with early depth testing.

### 3.3 Draw order

`RenderPass::SortCommands` (`src/renderer/RenderPass.cpp:64`) orders the opaque draws of a pass by technique first (line 152), then by
depth (line 163). That depth is the legacy key `depthBias - z` of the part's position (`RenderDevice::DrawMesh`,
`src/renderer/RenderDevice.cpp:2933`), not the view distance. A room using the same technique as most of the table and placed low (the
Ghostbusters sky at z −1000, floor at −1670) sorts after the other opaque parts; a room with untextured or alpha-tested parts sorts
earlier, and can be drawn then covered by the cabinet. The visibility mask (`Renderer::DrawBackground`, `src/renderer/Renderer.cpp:1102`)
is drawn first and rejects the pixels outside the lenses.

### 3.4 Grouped parts ("group elements")

`Primitive::RenderSetup` (line 741) merges, in a collection with "group elements" on, the dynamic mesh primitives that share the first
part's material and texture into one mesh, drawn by that first part; the others skip rendering (line 768 and after). Hiding the first
part hides the whole group; hiding another part of a group whose first part is shown does not hide it. Addams' collections are grouped,
and its script hides all of them, so nothing is drawn. For view culling (section 6), grouping makes the drawn meshes larger and fewer.

### 3.5 Lights, physics

A light at zero intensity is not drawn (`src/parts/light.cpp:615`; the bulb model is drawn only when the light shows one). Physics:
`Primitive::PhysicSetup` returns for toys (`src/parts/primitive.cpp:188`); a hidden but collidable part stays in the collision
structures, which costs nothing while the ball is far from it.

### 3.6 FXAA and the background

The FXAA pass (`Renderer.cpp`, "Post Process AA Pass", line 2920) receives the scene's depth buffer, and the three FXAA variants of
`fs_pp_fxaa.sc` return the pixel unfiltered where the depth is 1.0 (nothing drawn, lines 43, 97, 246). This is why a table without a room
pays almost nothing for FXAA on its background, and why a room makes FXAA pay for every pixel it covers.

### 3.7 Foveation on these runs

Every run of this document logged "fragment shading rate image attached to BackBuffer1/2" with eye tracking set but inactive (headless):
the scene buffers carry a fixed foveation shading rate image (`VRDevice::UpdateShadingRateMap`, `src/renderer/VRDevice.cpp:996`). A
per-draw shading rate (section 6) would combine with that image through the driver's combiner operations (Turnip reports
`fragmentShadingRateNonTrivialCombinerOps`).

## 4. Measurements

### 4.1 Method

Headless runs of 120 s (`perf-hooks.py`: autoplay, profiler lines every 5 s, `VPX_PERF_FORCE_RENDER=1` with the eyes pinned at a
player's place; `VPX_GPU_PROFILE=1` for bgfx's GPU time per render target), the report over the gameplay window from 40 s
(`perf-report.py`). Ghostbusters runs at a fixed 2160² (`prefs-fixed2160`), Addams with the default settings (dynamic resolution). Rests
of 40 s between runs (a longer idle lets the Frame sleep). The variants are environment variables of `room-hooks.py` (header of that
file) driven by `room-series.sh`. "Runtime GPU" is the runtime's `gpu_frametime`; "GPU busy" the sampler's DRM busy time of the VPX
process (1 % at 72 Hz is about 0.14 ms per frame). The pass names are bgfx's render targets: BackBuffer is the scene, VRSwapchain is
FXAA (written to the swapchain), PostProcess the tonemap, BloomBuffer the bloom, Playfield the playfield reflection probe.

Run-to-run noise: in the second series, the first and last full-room runs agree within 0.05 ms per pass. The first series was noisier
(0.3 ms between its two full-room runs, the GPU reaching 95 °C late in it) and was made with an older build of `master`; its relative
results agree with the second series, its FXAA figures do not (section 4.2).

### 4.2 Ghostbusters, first series (2026-10-23 15:16, `~/perf-test/results-room`)

| Run | Runtime GPU | Scene | FXAA | Tonemap | Bloom | GPU busy |
|---|---|---|---|---|---|---|
| Full room (start / end) | 10.48 / 10.87 ms | 4.00 / 3.70 | 2.00 / 2.18 | 1.75 / 1.74 | 0.95 / 1.53 | 76 / 77 % |
| Room off (`GB_room0.vbs`) | 7.87 | 2.48 | 1.26 | 1.62 | 0.73 | 57 % |
| Sky hidden, floor kept | 9.07 | 2.71 | 1.36 | 1.66 | 0.81 | 61 % |
| Floor hidden, sky kept | 10.21 | 3.50 | 1.79 | 1.70 | 0.89 | 73 % |
| Room unshaded (texture × color) | 10.18 | 3.61 | 1.85 | 1.88 | 0.85 | 74 % |
| Room drawn after every other opaque draw | 10.46 | 3.87 | 1.91 | 1.90 | 0.99 | 76 % |
| Drawn last + unshaded | 10.29 | 3.53 | 2.00 | 1.71 | 0.89 | 73 % |

From GPU busy: room about 2.7 ms, sky about 2.2 ms, floor about 0.5 ms, unshaded saves 0.35 ms, drawing last saves nothing. Its FXAA
difference (0.74 ms between full room and room off) is twice what section 7.2 of the performance document and the second series
measured (0.37–0.39 ms); it is not used for conclusions.

### 4.3 Ghostbusters, second series (2026-10-23 16:51, `~/perf-test/results-room2`)

GPU at 74–83 °C, a build of `master` ff85734c3 with the hooks.

| Run | Runtime GPU | Sum of passes | Scene | FXAA | Tonemap | Bloom | Reflection | GPU busy |
|---|---|---|---|---|---|---|---|---|
| Full room (start / end) | 9.62 / 9.58 ms | 8.94 / 8.90 | 3.73 / 3.73 | 1.45 / 1.44 | 1.64 / 1.64 | 0.87 / 0.86 | 1.23 | 69 / 69 % |
| Room off | 7.64 | 7.07 | 2.52 | 1.08 | 1.56 | 0.68 | 1.23 | 55 % |
| Flat color room (no texture, no lighting) | 8.95 | 8.18 | 3.21 | 1.42 | 1.60 | 0.70 | 1.24 | 64 % |
| Room unshaded (texture × color) | 9.41 | 8.74 | 3.58 | 1.51 | 1.63 | 0.79 | 1.24 | 68 % |
| FXAA left out beyond 3 m | 9.53 | 8.85 | 3.74 | 1.37 | 1.64 | 0.87 | 1.23 | 69 % |
| FXAA left out beyond 20 m | 9.63 | 8.95 | 3.75 | 1.44 | 1.64 | 0.88 | 1.24 | 69 % |

Reading:

- **The room**: 1.95 ms of runtime GPU (9.60 → 7.64), 1.85 ms in the passes: scene 1.21, FXAA 0.37, bloom 0.19, tonemap 0.08.
- **Shader math**: the unshaded room (one texture read, no lighting) saves 0.2 ms, 0.15 of it in the scene pass. For an unlit room, the
  lighting code is not where the time goes.
- **Texture read**: the flat color room saves 0.65 ms (scene 0.52, bloom 0.17: a flat dark room has no bright pixels to bloom). Without
  their textures, the room's scene cost drops from 1.21 to 0.69 ms: reading them (mostly the sky's 3072×1536 texture, which covers far
  more pixels than the floor's) is about 40 % of the room's scene cost.
  Why a magnified texture costs that much is not known (section 7.1).
- **Covering the pixels**: what remains with a flat color, about 1.3 ms (0.69 in the scene pass, 0.34 in FXAA, 0.04 in tonemap, plus
  bloom and the runtime's share), is the cost of having those pixels drawn at all.
- **FXAA beyond a distance**: 0.07 ms at 3 m, nothing at 20 m (section 4.5).

The minimal room (section 7.2 of the performance document, 2026-10-20) saved only 0.4–0.5 ms against the full room: its boxes are lit
and fill the same view.

### 4.4 Draw order (`VPX_PERF_LOG_ORDER`, full room)

The scene pass ("Render Scene") held 511 commands: 1 alpha-tested textured draw, 260 untextured opaque draws, 14 alpha-tested, 103
textured, then the sky (index 378, depth key 1000) and the floor (379, key 1670), then 4 balls, 10 light draws (`light_without_texture`,
`light_with_texture`), then the transparent parts (back to front). The room is already drawn after every opaque part except the balls and the inserts,
which is why sorting it last changed nothing (section 4.2). The floor is also drawn in the playfield reflection pass (437 commands,
index 293), at no measurable cost (the reflection is 1.23 ms with or without the room).

### 4.5 FXAA left out on far pixels (prototype, dropped)

`VPX_PERF_FXAA_FAR=<meters>`: the FXAA pass gets, in `w_h_height.w`, the depth of that distance instead of 1.0 (the VR projection is
right handed with z in [0, 1], near 0.1 m, far the scene size, `VRDevice.cpp:2457`: depth = (P._33 × −d + P._43) / d; logged
0.9687 for 3 m and 0.9971 for 20 m on Ghostbusters), and each FXAA variant returns the pixel unfiltered when its depth and the depths of
its 4 neighbours are beyond it, so that the edges against nearer parts keep their antialiasing. Result: 0.07 ms saved at 3 m, nothing at
20 m (the sky alone). FXAA's cost on room pixels is mostly reading the image around each pixel, and the test has to read the depth
around each pixel too. Not worth a visual check or a change of upstream's shader. Variants not tried: a single depth read (no
neighbours, loses the antialiasing of the room side of the cabinet's silhouette), a `textureGather` of the depth (one read for 4 texels).

### 4.6 Addams: is the hidden full room drawn? (2026-10-23 16:13, `~/perf-test/results-ad`)

Runs with `VPX_PERF_ROOM` naming the 117 parts of `VR_Stuff` and `VPX_PERF_LOG_ORDER`, which logs any pass that holds one of their
draws: none in any run. `AD_once` plays the table with a `.vbs` where the room choice is applied once (a flag around the block of
`GameTimer1_Timer`) instead of every tick:

| Run | Script (logic thread) | Logic thread CPU | Draws | Triangles | Runtime GPU | Resolution (dynamic) |
|---|---|---|---|---|---|---|
| `AD_base` (two runs) | 1.43 / 1.46 ms per frame | 32 / 33 % | 331 / 328 | 1 903 542 / 1 903 302 | 10.72 / 10.73 ms | 2016 / 2025 |
| `AD_once` (two runs) | 1.30 / 1.28 ms | 31 / 31 % | 329 / 328 | 1 902 557 / 1 902 329 | 10.70 / 10.73 ms | 2016 / 2025 |

The hidden room is not drawn; re-hiding it every tick costs about 0.15 ms of script per frame, on a logic thread that sleeps about
9.5 ms per frame, and nothing on the GPU. A hidden room still costs memory and loading time (its meshes are built at load, its textures
loaded with the table's), not measured.

## 5. Platform facts checked

- **Runtime layers**: SteamVR's OpenXR on the Frame (`/opt/steamvr/bin/linuxarm64/vrclient.so`, the active runtime) names only
  `XR_KHR_composition_layer_depth` among the composition layer extensions (`strings` of the library, which also lists
  `XR_FB_foveation_vulkan`): no `XR_KHR_composition_layer_equirect2`, cube or cylinder. A sky cannot be handed to the runtime as a
  panorama layer. To check again after SteamVR updates.
- **Shading rate**: Turnip (Mesa 26.3.0-devel, Adreno 750) reports `pipelineFragmentShadingRate`, `primitiveFragmentShadingRate` and
  `attachmentFragmentShadingRate`, a shading rate attachment texel of 8×8, `fragmentShadingRateNonTrivialCombinerOps` true,
  `layeredShadingRateAttachments` false (`vulkaninfo` on the Frame). bgfx's API only has a rate per view, `bgfx::setViewShadingRate`
  (`bgfx/include/bgfx/bgfx.h:4366` of the bundled source): a rate for some draws of a pass needs either a bgfx change (the fork patches
  bgfx already, `steamframe/patches/bgfx/`) or a separate view for those draws, which on a tiled GPU means splitting the pass (its tiles
  stored and loaded again) and is likely to cost more than it saves.

## 6. Options, and where they stand

| Option | Tables | Image | Gain measured or expected | Status |
|---|---|---|---|---|
| Draw the room after the other opaque parts | Rooms sorted early (untextured or alpha-tested parts) | Identical | Nothing on Ghostbusters (already last) | Dropped for Ghostbusters; the generic form is option "sort by view depth" below |
| Lighter shader for unlit parts | Unlit rooms, unlit table parts | Identical (must keep the environment term) | At most 0.2 ms on Ghostbusters (the unshaded bound) | Low priority |
| FXAA left out on far pixels | All rooms | Room very slightly sharper | 0.07 ms | Dropped (section 4.5) |
| Sky as a runtime panorama layer | Sky spheres | Very close (no bloom from the sky) | Up to the whole sky, about 1.5 ms (room 1.95 ms less the floor's ~0.5 ms of the first series) | Not possible on the Frame (section 5) |
| **Cheaper read of the sky texture** | Rooms with large magnified textures | To be determined | Part of the 0.65 ms of the texture read | **Next: check how the texture is stored and sampled (section 7.1)** |
| **Half-rate shading of sky-like parts** | Sky spheres, distant walls | Near-identical where the texture is magnified | Up to ~0.5 ms on Ghostbusters (¾ of the texture and shader share; the cost of covering the pixels stays) | **Open: needs a bgfx change (section 7.2)** |
| **Skip parts outside both eyes' view** | Modelled and heavy rooms | Identical | Unmeasured; nothing for a sky sphere (always in view) | **Open: measure first (section 7.3)** |
| Sort opaque draws by view depth | Tables whose room is drawn before the cabinet | Identical | Unknown, none on Ghostbusters | Only if a table shows the problem (upstream's comment in `RenderDevice::DrawMesh` already asks for it) |

Gains on a table limited by pixels become resolution through dynamic resolution (about 4–5 % per axis per millisecond); Ghostbusters is
already at its 125 % ceiling, so there they become headroom for the heat or a higher ceiling.

## 7. How to continue

### 7.1 Why the sky texture read costs 0.65 ms

First, without the Frame: find how `creepy kid` is uploaded and sampled. The texture compressor (`TextureCompressor`, its log line
"Texture compression: N compressed, N loaded from cache ... GPU memory 1258MB uncompressed -> 314MB compressed" on Addams) may or may not
compress it, and with which format; the sampler is `SamplerFilter::SF_UNDEFINED` for primitives, the default of the user's setting
(trilinear or anisotropic, `primitive.cpp`, "Use the default filtering of the sampler"); mipmaps; sRGB. Then on the Frame, variants of the
`flat`/`unshaded` runs: the same room with the texture forced to trilinear, to a smaller mip (a bias), to a compressed format. On a sphere
seen from its centre, the view is almost perpendicular to the surface everywhere, so anisotropic filtering should do nothing visible;
in the foveated periphery, coarser fragments read larger footprints.

### 7.2 Half-rate shading for sky-like parts

What it needs: a per-draw shading rate in bgfx's Vulkan backend (`vkCmdSetFragmentShadingRateKHR` as dynamic state with the combiner
keeping the coarser of the draw's rate and the foveation image's, or a pipeline state), an API to set it per draw from VPX, and a rule
for which parts qualify (unlit, very large and far away; a per-part setting would be more reliable but needs table authors). Measure it
before the visual check: the same `full`/`flat` comparison with the sky at 2×2. Check the Vulkan rules for using a pipeline rate while a
shading rate attachment is bound (and while a fragment density map is, for the density map path of the foveation).

### 7.3 Parts outside the view, on a heavy room

Measure first, on a heavy room shown by default (Lord of the Rings, whose `VR Room` table option offers 7 rooms, or Maverick): the room
on and off, the GPU time, and the share of the room's triangles outside both eyes' view (a debug count in `Primitive::Render` against the
union of the two frustums). If the vertex work of the hidden half is significant, a bounding volume test per part per frame in
`Renderer::RenderItem` or `Primitive::Render` (all part types, both eyes, the reflection probe's mirrored view too) is a generic,
upstream-worthy change.

### 7.4 Addams' full room

Its cost when shown is unmeasured: `vpx-inspect.py script` of the table, `VR_Room_Minimal = 0` in a `.vbs` next to a hard link of the
table (section 8), an `ad_base`-like run. Expected heavy: 640 000 more triangles on a table already limited by geometry, and 10 lights.

## 8. Reproduction

Tools (all in `Doc/test-hooks/`):

| File | What |
|---|---|
| `room-hooks.py` | The temporary code (`TEMP-PERF-ROOM` markers) for the variants of section 4: `VPX_PERF_ROOM`, `_LATE`, `_UNSHADED`, `_FLAT`, `VPX_PERF_HIDE`, `VPX_PERF_LOG_ORDER`, `VPX_PERF_FXAA_FAR` (header of the file). Edits `primitive.cpp`, `RenderCommand.h`, `RenderFrame.cpp`, `RenderPass.cpp`, `RenderDevice.cpp` (CRLF kept), `Renderer.cpp` and `fs_pp_fxaa.sc` |
| `perf-hooks.py` | Autoplay, profiler lines, force render with pinned eyes (`Performance on the Steam Frame.md`, section 2) |
| `room-series.sh` | The runs of sections 4.2–4.6 on the Frame: `sh room-series.sh <results folder> <run>...` (runs listed in its header; `full+2` repeats a run as `full2`) |
| `vpx-inspect.py` | Reads a `.vpx` without VPX (needs `olefile`): `parts`, `materials`, `script`, `survey` |
| `perf-report.py`, `perf-sampler.py` | Report and sampler of the performance document |

Recipe:

```sh
# On the Mac, from the workspace. Build in a temporary worktree: other sessions may have uncommitted work in vpinball/, which the
# build would pick up, and the hooks must never be committed
git -C vpinball worktree add --detach ../vpinball-perf HEAD
cd vpinball-perf
# Give unchanged files the mtimes of vpinball/, so that the build copy (rsync -a) only recompiles what differs
git ls-files -z | while IFS= read -r -d '' f; do [ -f "../vpinball/$f" ] && cmp -s "$f" "../vpinball/$f" && touch -r "../vpinball/$f" "$f"; done
python3 ../Doc/test-hooks/room-hooks.py && python3 ../Doc/test-hooks/perf-hooks.py apply
cd ..
VPX_SRC=vpinball-perf VPX_LINUX=vpinball-sync-linux VPX_VOLUMES=vpx-sync Doc/steam-frame-build/build-linux-arm64.sh app
# Copy the build out of the volume: colima only mounts the home folder, so into the workspace (not the scratchpad)
mkdir -p .perf-room-pkg
DOCKER_HOST=unix://$HOME/.colima/default/docker.sock DOCKER_CONFIG=$PWD/.docker-config docker run --rm -v vpx-sync-build:/src/build \
   -v $PWD:/workspace vpx-sniper-arm64 bash -c 'rsync -a --exclude CMakeFiles --exclude CMakeCache.txt --exclude "*.cmake" --exclude Makefile \
   --exclude "*.a" --exclude "*.o" --exclude _deps /src/build/ /workspace/.perf-room-pkg/ && cp -a /usr/lib/aarch64-linux-gnu/libgpiod.so.2* \
   /usr/lib/aarch64-linux-gnu/libcrypto.so.1.1 /workspace/.perf-room-pkg/'
cp Doc/steam-frame-build/run.sh .perf-room-pkg/
rsync -a --checksum --link-dest=/home/steamos/devkit-game/vpx_frame -e "ssh -o ConnectTimeout=8" .perf-room-pkg/ steamos@frame.local:perf-test/vpx-room3/
scp Doc/test-hooks/room-series.sh steamos@frame.local:perf-test/
rm -rf .perf-room-pkg; git -C vpinball worktree remove --force ../vpinball-perf
# On the Frame (ssh steamos@frame.local; no VPX running: pgrep -a VPinballX), detached so that the ssh session can end
cd ~/perf-test && (VPX_DIR=vpx-room3 setsid nohup sh room-series.sh results-room3 full flat unshaded off full+2 > series-room3.log 2>&1 < /dev/null &)
# Back on the Mac
scp -r steamos@frame.local:perf-test/results-room3 <scratch>/ && python3 Doc/test-hooks/perf-report.py <scratch>/results-room3
```

Table copies on the Frame, in `~/perf-test/tables` (hard links, no disk space): `GB_room0.vpx` + `GB_room0.vbs` (Ghostbusters' script
with `VRRoom = 0`; `GB_room2` the minimal room), `AD_base.vpx`, `AD_once.vpx` + `AD_once.vbs`, `cache/AD_base` and `cache/AD_once`
(hard linked copies of the table's texture cache, `cp -al`, else the first run recompresses every texture), `ad-room-names.txt`. A
`.vbs` must be made from the script's bytes (`vpx-inspect.py script`, CRLF and Windows-1252 kept), with only the intended lines changed.
The shared PinMAME folder of the library is found from any table folder. `vpx-inspect.py` runs on the Frame with olefile's package
folder copied next to it (`~/perf-test/tools`).

Gotchas met:

- The build copy `vpinball-sync-linux` keeps the hooks after such a build; the next build of `vpinball/` resyncs and recompiles them, but
  `build-linux-arm64.sh package` alone does not rebuild: run `app` before `package` for anything that is deployed.
- macOS's `/bin/sh` (bash 3.2) rejects `case ... esac` inside `$( )`; `sh -n` the series scripts on the Frame instead.
- Text read through Python with the default encoding turns the script's Windows-1252 bytes into UTF-8: write `.vbs` files as bytes.
- The first series ran while the GPU reached 95 °C: compare variants within one series, and repeat the first run at the end.

Results on the Frame: `~/perf-test/results-room` (first series), `results-room2` (second), `results-ad` (Addams); builds
`~/perf-test/vpx-room` and `vpx-room2`. They can be deleted once no longer needed.
