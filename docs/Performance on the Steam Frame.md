# Performance on the Steam Frame: where the frame time goes, and what to do about it

*2026-10-19, fork `master` at 53c794380 (upstream sync of 2026-10-18). Measurements on the Steam Frame (Adreno, Turnip, SteamVR 2.17) at
72 Hz (13.9 ms per frame), Standard FXAA, specular AA, foveated rendering, two tables: the Ghostbusters VR room (light, no ROM) and the
Addams Family VR room (heavy, PinMAME WPC).*

## 1. Summary

- **With the desktop mirror on (the previous default), neither table held the native 2160² per eye.** Ghostbusters needed 17.0 ms of
  GPU per frame there, Addams 18.0 ms: both dropped to 36–40 fps. Dynamic resolution kept 72 fps by settling at 1568² (Ghostbusters) and
  at its 70 % floor, 1512² (Addams, 12.1 ms against the 11.8 ms target: no margin left).
- **The desktop mirror was the largest avoidable cost.** The default *Desktop display* ("Playfield and other displays") renders the scene
  a third time, with its own playfield reflection, for a 1080p window nobody sees on the Frame. The Frame build now always keeps it off
  (section 6), which takes **Addams from 18.0 to 11.6 ms per frame at 2160²** (4.5–6.6 ms saved, 35–40 % of the GPU frame): Addams
  now holds 72 fps at the native resolution, and dynamic resolution settles at 94 % instead of its 70 % floor.
- **The scene pass is geometry-bound, not pixel-bound.** On Addams, 2.8× the pixels cost only 1.4× the time (about 3.8 ms fixed + 0.6 ms
  per million pixels per eye). Lowering the resolution gains little; the next gains are in geometry and draw calls, and in the passes
  that render the scene again (playfield reflection, desktop mirror).
- **The CPU is not the bottleneck at 72 Hz, but it is close on some tables.** The render thread uses 56 % of a core on Ghostbusters
  (7.5–9 ms per frame to submit), leaving about 2 ms; at 90 Hz it would limit before the GPU. PinMAME uses 71–74 % of a core on Addams,
  on its own core.
- **Heat is the other limit.** The CPU peaked at 90–96 °C and the GPU at 83–86 °C in 2-minute runs; earlier measurements showed the same
  frame costing 2–3 ms more after ten minutes. Every millisecond saved also lowers the heat.

## 2. Method

### 2.1 Headless runs

SteamVR tells the application not to render while nobody wears the headset (`shouldRender` false), and VPX obeys
(`src/renderer/VRDevice.cpp`, `if (!sessionActive || !frameState.shouldRender) rendered = false`): unworn, the GPU idles at 231 MHz. A
temporary test hook (`Doc/test-hooks/perf-hooks.py`, never committed) adds:

- `VPX_PERF_FORCE_RENDER=1`: render anyway, with both eyes pinned at a player's place (25 cm behind and 50 cm above the front of the
  playfield, 64 mm apart, looking 40 % up the playfield), so every run sees the same view;
- `VPX_PERF_TEST=<seconds>`: autoplay (credits, start, launch and flips every 5 s, a new game every 60 s), the sliding averages of the
  logic and render thread profilers logged every 5 s (`PERF-TEST` lines), and quit after the given time.

`VPX_GPU_PROFILE=1` adds bgfx's GPU time per render target to the status line logged every 5 s. Next to VPX, `perf-sampler.py` records
every second the CPU per thread, the load and clock of every core, the GPU clock, the GPU busy time of VPX and of the compositor (DRM
`fdinfo`, `drm-engine-gpu`) and the temperatures. `perf-series.sh` runs the variants with a rest between them, each with its own
`-PrefPath` (a copy of the user's settings with the variant's keys), from a copy of the developer deploy; `perf-report.py` summarizes a
results folder.

### 2.2 Validity

| Ghostbusters, default settings | Render size | Runtime GPU per frame | fps | Foveation |
|---|---|---|---|---|
| Worn | 1548² | 10.5 ms | 71.6 | eye-tracked |
| Headless | 1568² | 11.3 ms | 71.3 | fixed (no gaze) |

The headless runs reproduce the worn experience within about 0.7 ms of GPU time, the cost of the fixed foveation profile used without a
gaze. The runtime's GPU counter (`XR_META_performance_metrics`) still counts frames it does not display, so dynamic resolution works
headless. Limits: the compositor does not display the frames (its 6–8 % of GPU time is about the same as worn); single 2-minute runs;
the device heats over a series (temperatures are in the results); the bgfx profiler adds a little to the variants that use it.

With the headset worn, the system load is higher: all cores 35–45 % busy instead of about 20 %, vrserver at 15 % of a core instead of 5 %,
and the logic thread's "misc" time 4.5 ms per frame instead of 1.1 ms. CPU margins measured headless are therefore optimistic.

## 3. GPU (desktop mirror on, as before)

Sections 3 to 5 were measured with the desktop mirror on; section 6 gives the figures for Addams with it off.

### 3.1 Cost by resolution (runtime GPU time per frame, gameplay)

| Table | 1296² (0.75) | 1728² (1.0, recommended) | 2160² (1.25, native) | Default (dynamic resolution) |
|---|---|---|---|---|
| Ghostbusters | 9.3 ms | 11.7 ms | **17.0 ms, 40 fps** | 1568², 11.3 ms, 71 fps |
| Addams Family | 11.7 ms | **15.7 ms, 53 fps** | **18.0 ms, 36 fps** | 1512² (floor), 12.1 ms, 72 fps |

### 3.2 Cost by pass (bgfx GPU time per render target, ms per frame)

| Pass | GB 1296² | GB 1728² | GB 2160² | Addams 1296² | Addams 1728² | Addams 2160² |
|---|---|---|---|---|---|---|
| Scene (BackBuffer) | 2.47 | 3.21 | 4.33 | 4.81 | 5.98 | 6.58 |
| Playfield reflection probe | 2.00 | 2.40 | 2.86 | 1.90 | 2.31 | 3.83 |
| **Desktop mirror** (DesktopBuffer + its bloom) | **1.99** | **2.32** | **2.12** | **3.37** | **4.02** | **3.48** |
| Tonemap (PostProcess) | 0.60 | 1.06 | 1.65 | 0.57 | 1.09 | 1.57 |
| FXAA to the headset swapchain (VRSwapchain) | 0.63 | 1.05 | 1.92 | 0.58 | 1.11 | 1.55 |
| Bloom | 0.35 | 0.43 | 0.90 | 0.08 | 0.13 | 0.19 |
| Sum of the passes | 8.18 | 10.66 | 14.01 | 11.37 | 14.69 | 17.26 |

The runtime's figure is 0.3–3 ms above the sum of the passes (work outside the profiled views, and gaps between them). The "Playfield
reflection probe" row includes the probe rendered for the desktop mirror's own scene render: with the mirror off it drops to 1.1–1.3 ms
on Addams (section 6).

### 3.3 Reading

- **Scene pass**: fitting the three Addams points gives about 3.8 ms fixed + 0.59 ms per million pixels per eye (Ghostbusters: 1.4 ms +
  0.62 ms). The fixed part, which does not depend on the resolution, is vertex and draw work for the geometry of the table and of its VR
  room (both eyes go to one 2-layer target; how the views are instanced was not looked at). This is why Addams stays at 11.7 ms at 1296², and
  why dynamic resolution reaches its floor there without reaching the target.
- **Passes that render the scene again**: the desktop mirror (in "Playfield and other displays" mode, a separate non-stereo render of the
  scene at the window size) and the playfield reflection probe together cost 4–7 ms, about as much as the scene itself on Addams. Both
  carry the same geometry cost, which is why the desktop mirror costs more on Addams (3.4–4.0 ms) than on Ghostbusters (about 2 ms), and
  does not scale with the headset resolution.
- **Full-screen passes** (tonemap, FXAA, bloom) scale with the pixels: 1.3 ms at 1296², 3.5–4.5 ms at 2160². The tonemap writes a
  full-resolution intermediate that FXAA reads again (see *Image Quality on the Steam Frame.md*, section 4).

## 4. CPU

Logic and render thread profilers (ms per frame, sliding averages) and CPU per thread (% of one core), default settings:

| | Ghostbusters | Addams Family |
|---|---|---|
| Logic thread | 30 %: script 0.2, physics 0.1, frame preparation 2.8, sleep 9.9 | 35 %: script 1.7, physics 0.3, frame preparation 1.5, sleep 9.1 |
| Render thread | **56 %**: waits 2.9 for the logic thread, submits 7.6, waits 2.9 for the display | 25 %: waits 1.8, submits 6.3, waits 5.5 for the display |
| PinMAME emulation | (no ROM) | **71–74 %** (WPC CPU and DCS sound board) |
| Audio | 10 % | 6 % |
| Compositor, vrserver | 8 %, 5 % | 7 %, 5 % |

- On Ghostbusters the render thread's submission is real CPU work (56 % of a core matches the 7.6 ms of submission per frame), and with
  the 2.9 ms spent waiting for the logic thread, it leaves about 2 ms of a 13.9 ms frame. On Addams the submission time includes waits
  (25 % of a core for 6.3 ms of submission). Why Ghostbusters costs more to submit (draw count, state changes, lights) is not measured yet.
- Each frame, the render thread waits 1.8–3.5 ms for the logic thread to finish preparing the frame: the preparation of frame N+1 does
  not overlap the submission of frame N.
- Script and physics are small on both tables. PinMAME keeps one core busy on WPC tables, which matters for the heat rather than the
  frame rate.

## 5. Heat

| Run (2 minutes) | GPU max | CPU max |
|---|---|---|
| Ghostbusters default, worn | 83 °C | 96 °C |
| Ghostbusters default, headless | 79 °C | 81 °C |
| Addams default, headless | 86 °C | 90 °C |
| Addams fixed 1728², headless | 86 °C | 91 °C |

The CPU runs hotter worn (the headset's own services). Earlier runs measured the same Addams frame 2–3 ms slower after ten minutes of
play (*Image Quality on the Steam Frame.md*, section 8.3): heat erodes the margins above, and dynamic resolution absorbs it by lowering
the resolution.

## 6. Desktop mirror always off on the Frame (2026-10-19)

In the build that runs on the Frame (`VPX_STEAM_FRAME_HEADSET`, defined by `steamframe/steamframe.cmake` for Linux ARM64 with OpenXR),
the desktop mirror is forced off: `Renderer::Renderer` ignores `PlayerVR/DesktopDisplay`, the VR settings page does not offer the
*Desktop Display* section, and the key that cycles the desktop display modes does nothing in VR. A value stored in a settings file
(saving the VR settings page wrote every item of the page, so many users have `DesktopDisplay = 1` written down) no longer matters.
Other builds keep upstream's behavior, since a desktop window is useful with a PC headset (spectators, streaming). On the Frame the window
still exists and is only cleared each frame. Nothing reads it: the table images of the library are captured from the headset view
(`Player::CaptureTableImage` uses the VR window), and the remote start tool goes through Steam.

**Measured gain** (Addams, same method, headless, 2026-10-19):

| Addams Family | Mirror on | Mirror off | Saved |
|---|---|---|---|
| 1296², runtime GPU per frame | 11.7 ms | 7.2 ms | 4.5 ms |
| 1728² | 15.7 ms, 53 fps | 9.1 ms, 72 fps | 6.6 ms |
| 2160² | 18.0 ms, 36 fps | **11.6 ms, 72 fps** | 6.4 ms |
| Default (dynamic resolution) | 1512² (70 % floor), 12.1 ms | **2023² (94 %)**, 10.7 ms, still climbing at the end of the run | |

Per pass, mirror off (ms per frame, 1296² / 1728² / 2160²): scene 4.50 / 5.30 / 6.26, playfield reflection 1.12 / 1.20 / 1.33, tonemap
0.57 / 1.00 / 1.57, FXAA 0.58 / 1.03 / 1.65, bloom 0.09 / 0.13 / 0.19. The saving is larger than the mirror's own pass (3.4–4 ms in
section 3.2) because its scene render also rendered a playfield reflection: the reflection row falls from 1.9–3.8 ms to 1.1–1.3 ms. The
CPU gains too: render thread 15 % of a core instead of 25 %, frame preparation 0.9 ms instead of 1.5 ms. Peak temperatures over the same
2-minute runs: GPU 68–87 °C, CPU 77–90 °C (the 2160² run being the hottest, as it now renders at full rate).

Addams now fits the native resolution within the 11.8 ms target of dynamic resolution (85 % of 13.9 ms), with little margin: on a hot
device dynamic resolution will lower it. Ghostbusters was not re-run; from section 3.2 it should gain at least the 2 ms of its mirror pass
plus part of its 2.0–2.9 ms reflection, which would bring its 2160² frame under the budget too.

One of two runs at 1728² measured exactly one frame period (13.9 ms) on the runtime's counter for the whole run, while the passes summed
to 8.7 ms and the render thread spent 12.3 ms per frame in submission: a frame pacing stall, not GPU work (72 fps held). The second run
measured 9.1 ms. Worth watching for in the headset.

## 7. Opportunities

Ranked by gain for the effort. Gains are per frame at 72 Hz; on Addams, a millisecond saved lets dynamic resolution raise the size by
about 4–5 % per axis.

| # | Opportunity | Gain | Effort | Status |
|---|---|---|---|---|
| 1 | Desktop mirror off on the Frame | 4.5–6.6 ms on Addams; at least 2 ms on Ghostbusters | Forced off in the Frame build | **Done** (section 6) |
| 2 | Lighter playfield reflection on heavy tables | 1.1–1.3 ms on Addams once the mirror is off | Per-table setting today (`PFReflection = 3`, "Static & Balls"); engine options below | To evaluate |
| 3 | Fuse FXAA into the tonemap pass | Part of 1.1–3.1 ms (one full-resolution write and read less) | Shader and pass change | Idea (*Image Quality*, section 4) |
| 4 | Reduce the scene pass's fixed geometry cost | Up to 3.8 ms on Addams | Investigation, engine work | To measure first |
| 5 | Overlap frame preparation with submission | 1.8–3.5 ms of render thread time; frame rate at 90 Hz | Threading change | Idea |
| 6 | Reduce the render thread's submission cost | Several ms of CPU on Ghostbusters | Investigation | To measure first |
| 7 | Dynamic resolution floor and target per table | Steadier frame rate on heavy tables | Settings | Available now |

Details:

1. **Desktop mirror**: see section 6.
2. **Playfield reflection**: the probe renders the reflected scene every frame (1.1–1.3 ms on Addams with the mirror off). Today,
   "Static & Balls" for a heavy table removes most of it at the cost of the dynamic reflections. Engine options: render the probe at a
   lower resolution, update it every other frame, or leave the VR room out of the reflection. Each needs a visual check in the headset.
3. **Tonemap + FXAA**: the tonemap writes a full-resolution intermediate that FXAA reads; doing FXAA in the same pass, or tonemapping
   straight into the swapchain when FXAA is off, removes one full-screen write and read per eye.
4. **Scene geometry**: next measurement: log bgfx's draw and primitive counts per frame (`bgfx::Stats::numDraw`, `numPrims`) and compare
   the scene pass with the VR room hidden, to split table and room. Then: per-eye frustum culling, merging static meshes, simpler room
   meshes, levels of detail for distant room geometry.
5. **Frame preparation and submission**: the render thread waits 1.8–3.5 ms per frame for the logic thread. Preparing frame N+1 while
   frame N is submitted would give the render thread that time back, which matters most for 90 Hz and for tables like Ghostbusters.
6. **Submission cost**: 7.5–9 ms of render thread CPU per frame on Ghostbusters against about 3.5 ms on Addams: count draws and state
   changes per table to find what makes it expensive.
7. **Dynamic resolution settings**: on Addams the 70 % floor is reached; a lower floor (60 %) keeps 72 fps when the device is hot, at a
   visible cost in sharpness. Per table, with the settings of the VR page.

For sharpness (*Image Quality on the Steam Frame.md*): with the mirror off, heavy tables like Addams reach about the native 2160²; the
*Render resolution* setting above 125 % (supersampling) only fits light tables. Beyond that, the GPU time freed by opportunities 2 to 4
is what dynamic resolution can turn into resolution, or into headroom for the heat.

## 8. Reproduction

From `vpinball/`, with the Frame on the network:

```sh
python3 ../Doc/test-hooks/perf-hooks.py apply      # temporary hook (TEMP-PERF-TEST markers)
VPX_SRC=vpinball VPX_LINUX=vpinball-sync-linux VPX_VOLUMES=vpx-sync ../Doc/steam-frame-build/build-linux-arm64.sh app
python3 ../Doc/test-hooks/perf-hooks.py remove     # never commit the hook
# copy the executable out of the build volume into ~/perf-test/vpx on the Frame (a 'cp -al' copy of ~/devkit-game/vpx_frame),
# and perf-series.sh, perf-sampler.py into ~/perf-test, then on the Frame:
VPX_PERF_FORCE_RENDER=1 VARIANTS="default fixed2160 fixed1728 fixed1296" sh perf-series.sh 120 60 <table.vpx>...
# back on the Mac, after copying ~/perf-test/results:
python3 ../Doc/test-hooks/perf-report.py <results folder> 40
```

Without `VPX_PERF_FORCE_RENDER`, someone has to wear the headset. The results of these runs are on the Frame: `~/perf-test/results`
(Ghostbusters with the mirror on, Addams with it off) and `~/perf-test/results-before` (Addams with the mirror on).
