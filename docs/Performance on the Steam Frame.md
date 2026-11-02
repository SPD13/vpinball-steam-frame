# Performance on the Steam Frame: where the frame time goes, and what to do about it

*A summary of everything implemented, with the settings of the menu: [Performance of the Steam Frame Edition](steam-frame/Performance%20of%20the%20Steam%20Frame%20Edition.md).
This document is the engineering log behind it.*

*2026-10-19, fork `master` at 53c794380 (upstream sync of 2026-10-18). Measurements on the Steam Frame (Adreno, Turnip, SteamVR 2.17) at
72 Hz (13.9 ms per frame), Standard FXAA, specular AA, foveated rendering, two tables: the Ghostbusters VR room (light, no ROM) and the
Addams Family VR room (heavy, PinMAME WPC). Updated 2026-10-20 with the measurements made after the desktop mirror was turned off
(section 7), 2026-10-29 with a review of the GPU opportunities left on Addams (section 8, items 10 to 15) and the simplified meshes
(section 8.2), 2026-10-30 with the series that measured items 2, 10, 11, 12, 14 and 15 on Addams and Ghostbusters (section 8.3), and the same day with
the welding of meshes exported as separate triangles and the culling of the reflection, from Albator78 (section 8.2), and 2026-11-01 with the
tables still below the native size (section 8.6), generic rules for the reflections (section 8.8), the Reflections settings built from them (section 8.9) and the placement of the threads on the cores (section 8.10), and 2026-11-02 with the wait of the logic thread on PuP's decoders (section 8.11), the sensitivity of dynamic resolution and the missed frames under it (section 8.12), and the reflection at half size (section 8.8).*

## 1. Summary

- **With the desktop mirror on (the previous default), neither table held the native 2160² per eye.** Ghostbusters needed 17.0 ms of
  GPU per frame there, Addams 18.0 ms: both dropped to 36–40 fps. Dynamic resolution kept 72 fps by settling at 1568² (Ghostbusters) and
  at its 70 % floor, 1512² (Addams, 12.1 ms against the 11.8 ms target: no margin left).
- **The desktop mirror was the largest avoidable cost.** The default *Desktop display* ("Playfield and other displays") renders the scene
  a third time, with its own playfield reflection, for a 1080p window nobody sees on the Frame. The Frame build now always keeps it off
  (section 6), which takes **Addams from 18.0 to 11.6 ms per frame at 2160²** (4.5–6.6 ms saved, 35–40 % of the GPU frame): Addams
  now holds 72 fps at the native resolution, and dynamic resolution settles at 93–94 % instead of its 70 % floor. Ghostbusters gains
  4.4–6.9 ms and runs at its 2160² ceiling in 10.1 ms (section 7.1).
- **The scene pass is geometry-bound on Addams, pixel-bound on Ghostbusters.** On Addams, 2.8× the pixels cost only 1.4× the time
  (about 3.8 ms fixed + 0.6 ms per million pixels per eye): it draws 1.9 million triangles per frame, 7× Ghostbusters. Ghostbusters'
  scene pass is about 1.0 ms fixed + 0.66 ms per million pixels, and a fifth of its frame is the VR room: turning the room off saves
  2.1–2.3 ms at 2160², the table's "minimal room" only 0.4–0.5 ms (section 7.2).
- **The CPU is not the bottleneck at 72 Hz.** With the mirror on, the render thread used 56 % of a core on Ghostbusters (7.5–9 ms per
  frame to submit); with it off, 34 % (4–6 ms). Ghostbusters submits 1000 draws and 29 000 shader parameter changes per frame, 3× Addams.
  PinMAME uses 71–76 % of a core on Addams, on its own core.
- **Heat did not throttle a 15-minute run.** Addams with dynamic resolution, from cool: the GPU clock held 903 MHz throughout, the
  temperatures levelled at 85–86 °C (GPU) and 89–91 °C (CPU) after five minutes, and the resolution held 2000–2010² at 10.7 ms
  (section 7.4). The earlier observation of 2–3 ms more after ten minutes was made with the mirror on.
- **A frame pacing stall can make the runtime report a full frame period of GPU time.** At fixed 2160² on Ghostbusters, every run started
  in a state where the render thread blocks 11.9 ms per frame in submission and the runtime's GPU counter reads 13.9 ms, while the GPU
  work is unchanged (9.4 ms of passes) and 72 fps hold; a single missed frame ends it. The cause was the hidden desktop window, still
  presented every frame into gamescope's 60 Hz Xwayland: the Frame build no longer draws into it, which ends the stall (0 of 7 runs
  instead of most table starts) and gives the render thread 2 ms per frame back (section 7.6). Dynamic resolution also holds its scale
  on such readings, as a safety net (section 7.5).
- **Two post-processing changes save a quarter of the frame (2026-10-30).** Shading the tonemap and FXAA passes at the foveated rate
  saves 1.35 ms on Addams and 1.53 ms on Ghostbusters at 2160²; a scene buffer in RG11B10F instead of RGBA16F saves 0.66 and 1.28 ms;
  both together 2.12 and 2.69 ms (10.56 → 8.44 ms, 9.94 → 7.25 ms), and 12 °C less on the GPU. Both are now in the Frame build: the
  foveated post-processing on by default, the compact scene buffer on by default too, after the owner's check in the headset (0.88 and 1.52 ms on top
  of the first, measured 2026-10-31, section 8.4). A lighter playfield reflection only pays
  when it leaves out the dynamic parts: coarser meshes, a lower resolution or fewer parts in the probe save 0.1–0.5 ms of its
  1.05–1.23 ms. With the simplified meshes, both tables now hold 2160² with dynamic resolution (section 8.3).
- **A table with meshes exported as separate triangles was the worst case for the simplified meshes (2026-10-30).** Albator78 lost only
  9 % in its simplified meshes and ran at 36 fps (19.8 ms at 2160²): 8 flat-shaded meshes hold 1.30 M triangles the simplifier could not
  touch. Welding them first and leaving out of the reflection what cannot land on the playfield gives 11.1–12.1 ms and 72 fps, for
  4.5 s more loading (section 8.2).
- **The tables still below native have different causes (2026-11-01).** Robot Invasion: three wall mirrors, 11–12 ms of its 21 ms. WildWest:
  its large VR room, 5.7 ms, of which 2.7 ms in the playfield reflection. Darkest Dungeon: 0.3 ms over the target, kept 350 pixels under
  what that allows by frames over the period that do not depend on the size. Missed frames and an over-reading GPU counter come with
  a render thread busy more than about 10 ms per frame, which makes it a limit for the first time (section 8.6). Two settings now leave the
  extra mirrors and the room out of the reflections, off by default on the Frame: Robot Invasion 22.7 → 9.4 ms, 2001 14.5 → 8.3 ms, WildWest 13.9 → 10.9 ms, Maverick 16.4 → 11.1 ms (section 8.9).
- **A game started by Steam has cores 3 to 7; the headless runs before 2026-11-01 had all eight (2026-11-01).** Dynamic resolution figures from those runs are low where the render thread is busy: Darkest Dungeon 1858² then, 2130² with Steam's cores. Choosing the cores of the threads gained almost nothing headless with Steam's cores,
  but in the headset Darkest Dungeon spends 62 % of its time at full resolution with it (62.3 and 62.5 %, two plays) against 1.9 to 19.4 % without (three plays), still rated yellow. It is on by default in the Frame build, and the owner heard no problem with the sound (section 8.10).
- **On PuP tables, the largest slow frames were a wait for a video decoder being closed (2026-11-02).** Each new video closed the previous hardware decoder under a lock that the logic thread needs to draw the PuP screens, stopping it for about 10 ms every 4 to 5 s on Darkest Dungeon. The decoder is now closed outside that lock. Headless, the waits are gone and missed frames look lower, but the time at full resolution does not move. Worn, one play of 111 s reached 74.7 % at full resolution against 62 % before, still rated yellow (section 8.11).
- **Dynamic resolution has a sensitivity setting, Medium by default (2026-11-02).** It sets how many frames over the whole period, close together, lower the resolution at once: Low one, as before; Medium 2 within 3 frames; High 3 within 5. Headless, Darkest Dungeon goes from 58 % of its time at full resolution (Low) to 66 % (Medium) and 72 % (High), for 0.03–0.04 missed frames per second instead of none; Robot Invasion with its mirrors, at its limit, misses 0.13–0.14 frames per second instead of 0.06. Worn, Darkest Dungeon at Medium with the target at 90 % reached 87.7 % at full resolution (74.7 % at Low and 85 %), for 0.42 missed frames per second instead of 0.15. 90 % was the default for a few hours, then back to 85 %: on Robot Invasion with its mirrors it means a missed frame per second instead of 0.14 (section 8.12).
- **Reflections at half size by default, and missed frames under the sensitivity (2026-11-02).** The playfield reflection at half size per axis saves about 0.45 ms on Darkest Dungeon at 2160², halves its readings over the period and takes its missed frames from 0.12 to about 0.01 per second, headless; it is the setting *Reflection size*, Half by default in the Frame build, its look not checked in the headset yet (section 8.8). Worn after a restart of the Frame (a system service had spun a core since boot), Darkest Dungeon at Medium and 90 % rated 90.3 % over 137 s, the smoothest play so far for the owner, at both sizes; its remaining dips came from frames handed over late, not from the GPU (80–84 % busy). A missed frame now counts under the sensitivity like a reading over the period instead of lowering the resolution at once: no difference headless, not played yet (section 8.12).

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
the resolution. With the mirror off, a 15-minute headless Addams run did not slow down (section 7.4).

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
device dynamic resolution will lower it (a 15-minute run did not need to: section 7.4). Ghostbusters with the mirror off: section 7.1.

One of two runs at 1728² measured exactly one frame period (13.9 ms) on the runtime's counter for the whole run, while the passes summed
to 8.7 ms and the render thread spent 12.3 ms per frame in submission: a frame pacing stall, not GPU work (72 fps held). The second run
measured 9.1 ms. Section 7.5 reproduces it on Ghostbusters.

## 7. Measurements with the mirror off (2026-10-20)

Same method (section 2), same build plus a counter line in the test hook: the render device's counters of the last frame (draws,
triangles, state and shader parameter changes, all passes and both eyes).

### 7.1 Ghostbusters by resolution

| Ghostbusters, runtime GPU per frame | 1296² | 1728² | 2160² | Default (dynamic resolution) |
|---|---|---|---|---|
| Mirror on (section 3.1) | 9.3 ms | 11.7 ms | 17.0 ms, 40 fps | 1568², 11.3 ms |
| **Mirror off** | **4.9 ms** | **7.2 ms** | **10.1 ms**, 72 fps (see 7.5) | **2158²** (its ceiling), 10.1 ms |
| Saved | 4.4 ms | 4.5 ms | 6.9 ms | |

Per pass, mirror off (ms per frame, 1296² / 1728² / 2160²): scene 2.09 / 2.96 / 4.05, playfield reflection 0.98 / 1.09 / 1.23, tonemap
0.60 / 1.07 / 1.66, FXAA 0.56 / 1.02 / 1.53, bloom 0.35 / 0.56 / 0.87; sum 4.57 / 6.69 / 9.34. As on Addams, the saving is larger than the
mirror's own pass because its scene render had its own reflection (the reflection row falls from 2.0–2.9 ms to 1.0–1.2 ms). The scene
pass fits about 1.0 ms fixed + 0.66 ms per million pixels per eye: Ghostbusters is limited by pixels, Addams (3.8 ms fixed) by geometry.

Dynamic resolution now reaches the `ResFactor` ceiling (125 %, 2160²) with about 1.7 ms left under its 11.8 ms target. The render thread
uses 34 % of a core (submission 4.1–4.3 ms per frame at fixed resolutions) instead of 56 %.

### 7.2 Cost of the VR room (Ghostbusters, 2160²)

The table's script has a `VRRoom` option (0 off, 1 full 360° room, the default, 2 minimal room). The variants ran from hard links of the
table with a `<table>.vbs` next to them, which VPX loads in place of the embedded script.

| Room | Runtime GPU | Sum of passes | Scene | FXAA | Bloom | Draws | Triangles |
|---|---|---|---|---|---|---|---|
| Full (default) | 10.1 ms | 9.34 ms | 4.05 | 1.53 | 0.87 | 999 | 272 600 |
| Minimal | 9.6 ms | 8.92 ms | 3.62 | 1.57 | 0.85 | 996 | 270 900 |
| Off | **7.8 ms** | **7.22 ms** | **2.61** | 1.14 | 0.68 | 987 | 270 400 |

The room is only 12 draws and about 2 000 triangles: its 2.1–2.3 ms are the shading of the walls, floor and ceiling that fill most of the
view, plus FXAA and bloom over them (FXAA is cheaper on the flat black background). The minimal room still fills the view, so it saves
only 0.4–0.5 ms. On a pixel-bound table, the room is the largest single cost after the table itself. The follow-up investigation
(what the room's cost is made of, the rooms of the library, the options tried) is `VR Rooms on the Steam Frame.md`.

### 7.3 Draws and triangles per frame

| Per frame (all passes, both eyes) | Ghostbusters | Addams Family |
|---|---|---|
| Draws | 1 000 | 332 |
| Triangles | 273 000 | 1 900 000 |
| Shader parameter changes | 29 000 | 9 600 |
| Render state changes | 19 | 21 |
| Render thread CPU (mirror off) | 34 % of a core | 15 % |

Addams' 7× more triangles explain its fixed scene cost; Ghostbusters' 3× more draws and parameter changes explain its higher submission
cost on the render thread (opportunity 6).

### 7.4 Fifteen minutes on Addams (dynamic resolution, from cool)

Started with the GPU at 36 °C, headless, mirror off. Per minute:

| Minute | 0 | 1 | 2 | 3 | 5 | 8 | 11 | 14 |
|---|---|---|---|---|---|---|---|---|
| Render size | 2004² | 2004² | 2009² | 2010² | 2008² | 2008² | 2009² | 2008² |
| Runtime GPU | 10.73 ms | 10.74 | 10.73 | 10.83 | 10.71 | 10.71 | 10.71 | 10.80 |
| GPU clock | 888 MHz | 903 | 903 | 903 | 903 | 903 | 903 | 903 |
| Max GPU / CPU temperature | 70 / 73 °C | 77 / 81 | 82 / 84 | 84 / 89 | 85 / 90 | 86 / 91 | 85 / 89 | 85 / 90 |

Over the whole run: 72.0 fps, 2005² average (93 % of 2160²), GPU 10.7 ms (10.0–11.9), CPU clusters at 2.2–2.7 GHz with no drop; PinMAME
76 % of a core, logic thread 29 %, render thread 15 %. No throttling at this load: the temperatures level off after five minutes and the
frame cost does not drift. The resolution stays under 2160² because dynamic resolution only raises the scale while the GPU time is under
90 % of the target (10.6 ms), and Addams sits at 10.7 ms. The CPU runs hotter worn (section 2.2), so a long worn session remains worth a
check.

Since 2026-10-28, the scale follows the GPU time averaged over about 5 frames (a frame over the whole period still lowers it at once), and
rises when the time expected at the next step, the cost following the pixels, stays under 97 % of the target, a quarter of a second after
a step down, by 0.3 % to 1 % per frame depending on the room left. A first version (no averaging, a second's wait, 0.3 % steps) still kept
Addams under the full size in play: single frames over the target, more than one a second, lowered the scale and restarted the wait, and it
only rose while the player looked away from the table (at the performance panel). Addams, headless, 3 minutes per run, alternated:

| Rule | Average size | Status lines at 2160² | Frames missed | Runtime GPU |
|---|---|---|---|---|
| Steps on each frame, 1 s wait (two runs) | 2092² (97 %) | 33 %, 27 % | 0, 0 | 10.75, 10.74 ms |
| Averaged, 0.25 s wait, larger steps (two runs) | 2150² (99.5 %) | 88 %, 91 % | 7, 3 | 11.23, 11.02 ms |

The table now stays at 2160² after its first 15 s, at the cost of a few frames missed per 3 minutes (1 in 2000 to 4000), with the GPU at
the same clock and temperature (902 MHz, 90–92 °C).

### 7.5 Frame pacing stall

At fixed 2160² on Ghostbusters, three runs out of three started in a second, stable state of the pipeline:

| Ghostbusters 2160², per frame | Runtime GPU | Sum of passes | Render thread: submission | Render thread: flip |
|---|---|---|---|---|
| Normal | 10.1 ms | 9.2–9.5 ms | 5.5–6.8 ms | 5.1–6.5 ms |
| Stalled | **13.9 ms** | 9.2–9.5 ms | **11.7–12.0 ms** | 0.2–0.3 ms |

The first run stayed stalled for its whole 120 s, the second for 70 s, a run without the bgfx profiler for 115 s (so the profiler is not
the cause); each left the state with one long frame, then stayed normal. The Addams run at 1728² of section 6 was the same state. In
the stalled state the GPU work and the frame rate are unchanged (72 fps, GPU 72 % busy in both states) and the render thread's CPU use
is the same: the time it used to wait in flip (for the runtime's next frame) moves into submission, where it waits for the GPU, and
the runtime's GPU counter then reads a full frame period. No run with dynamic resolution (Ghostbusters, Addams 2 and 15 minutes) entered it.

The risk is dynamic resolution: it steers by that counter, lowering the scale by about 2.4 % per frame at 13.9 ms, so a session that fell
into the stall would drop to its floor within a fraction of a second, and could stay there while the stall lasts.

**Fix (2026-10-20), in `VRDevice::UpdateDynamicResolution`:**

- *Telling the stall from real load.* A real overload makes frames miss their display; in the stall every frame is on time (no frame
  missed in any stalled interval measured). Missed frames are read from the runtime's predicted display time, which moves by more than
  one frame period after a miss. A reading between 95 % and 105 % of the frame period with no frame missed in the last three frames is
  *held*: the scale is kept, neither lowered nor raised.
- *Ending the stall.* Each stall measured ended with one missed frame. When at least 90 % of the readings of one second were held, the
  render thread waits one frame period once, so that one frame misses its display; at most three times per table, in case a load right at
  the frame period was taken for a stall.
- The status line (VR settings page and log) says when the scale is held; the log adds the missed frames, held readings and stall breaks
  of each 5 s.

Measured with Ghostbusters at 2160² (`drpin`: dynamic resolution on with its minimum at 100 %, so it behaves like a fixed size while its
decisions are logged):

| Build | Runs | Started stalled | How long the stall lasted |
|---|---|---|---|
| Before the fix (fixed 2160² and `drpin`) | 9 | 6 | 60 s to the whole 120 s run |
| Hold only | 3 `drpin` | 1 | the whole run: 341–358 of the ~360 readings of each 5 s held, the rest leaking through (enough to reach the floor in 10–20 s with a normal minimum) |
| Hold and break | 3 `drpin` | 3 | 1.3–2.2 s, ended by one break each time |

The stall also happens with dynamic resolution on: the runs of section 7 that had it on happened not to start in it. In normal play 0–9
readings per 5 s fall in the band without a missed frame (single heavy frames); they are held, which costs nothing, and stay far from the
90 % that triggers a break. No regression: default 2158², 9.8 ms; a real overload (`drhigh`, `ResFactor` 1.75, 3024² ceiling) still comes
down to 2331² at 10.7 ms within five seconds; neither had a stall break. The cause is in section 7.6; the guard stays as a safety net.

### 7.6 Cause of the stall: the desktop window

The render thread's "submit" section is VPX's recording of the frame into bgfx plus bgfx's own CPU time for the frame
(`FrameProfiler::AdjustBGFXSubmit` moves it out of "flip"), and bgfx runs on the render thread itself (single-threaded, `bgfx::renderFrame()`
before `bgfx::init`). On Linux the desktop window is bgfx's main swapchain (`init.swapChain.nwh` is the X11 window). With the mirror off,
the "Desktop Display" pass of `Renderer` still cleared that window every frame, so every frame bgfx acquired one of its images (blocking,
and the frame's queue submission waits on the acquire semaphore, `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT`) and presented it (`IMMEDIATE`, vsync
off). On the Frame that window lives in gamescope's Xwayland (`gamescope --backend openvr`, 1280×720 at **59.86 Hz**): a 72 Hz headset
frame tied to a 60 Hz presentation engine, which explains why the stall locks in, why one late frame shifts the phase out of it, and why
the GPU work is unchanged while the runtime counts a full period.

Experiment, same binary with the desktop pass skipped by a temporary switch (Ghostbusters, fixed 2160², alternated):

| Ghostbusters 2160², fixed | Stalled | Submit (normal state) | Flip (idle) | VPX CPU |
|---|---|---|---|---|
| Desktop window drawn (4 runs) | **4 of 4** (3 for the whole run, 1 for 10 s) | 6.2 ms | 5.9 ms | 70–72 % |
| Desktop window not drawn (4 runs) | **0 of 4** | **4.1 ms** | 8.0 ms | 65–66 % |

The GPU passes are the same in both (9.15–9.27 ms). **Fix**: the Frame build (`VPX_STEAM_FRAME_HEADSET`) no longer draws into the
desktop window (`Renderer`, the "Desktop Display" pass), so bgfx neither acquires nor presents it. Validation with the fix: 7 runs
(Ghostbusters fixed 2160² on four copies of the table, default, `drhigh`; Addams default), **no stall, no stall break**; the render
thread's submission is 4.1 ms per frame on Ghostbusters (6.2 ms before in the normal state, 11.9 ms stalled) and 1.7 ms on Addams
(3.3 ms before); Addams settles at 2027², 10.7 ms. Nothing else draws into that window on the Frame: the passes that render the scene
for it only run with the mirror on.

The coupling is generic: any VR session whose desktop window is presented by a display slower than the headset (a 60 Hz monitor and a
90 Hz PC headset, for example) shares the frame's submission with that presentation. Upstream, with a desktop mirror that is wanted,
the fix would be to present the window without blocking the headset frame (acquire with a zero timeout and skip the window when no
image is free, as bgfx already does for secondary windows), which needs a change in bgfx: not tested here.

## 8. Opportunities

Ranked by gain for the effort; items 8 and 9 come from the measurements of section 7, items 10 to 15 from the review of section 8.1.
Gains are per frame at 72 Hz; on Addams, a millisecond saved lets dynamic resolution raise the size by about 4–5 % per axis.

| # | Opportunity | Gain | Effort | Status |
|---|---|---|---|---|
| 1 | Desktop mirror off on the Frame | 4.5–6.6 ms on Addams, 4.4–6.9 ms on Ghostbusters | Forced off in the Frame build | **Done** (sections 6, 7.1) |
| 2 | Lighter playfield reflection on heavy tables | 1.1 ms on Addams, 1.3 ms on Ghostbusters with "Balls Only" (the whole probe); 0.1–0.5 ms for the steps that keep the dynamic parts | Per-table setting (`PFReflection = 1`, "Balls Only"; in VR "Static & Balls" still draws the static parts every frame) | **Measured** 2026-10-30 (8.3): a lower resolution, coarser meshes and fewer parts in the probe save little |
| 3 | Fuse FXAA into the tonemap pass | Bound: FXAA off saves 1.52 ms on Addams, 1.56 ms on Ghostbusters (the tonemap then writes the swapchain); about 0.9 ms once item 10 is done | Shader and pass change | Bound measured 2026-10-30 (8.3) |
| 4 | Reduce the scene pass's fixed geometry cost | Up to 3.8 ms on Addams | Investigation, engine work | Measured 2026-10-25: 2.2 ms per million triangles; simplification without visible loss 1.2–1.6 ms on Addams, culling 0.6 ms on LOTR (`Table Geometry on the Steam Frame.md`); culling done (`Culling Parts Outside the View.md`) |
| 5 | Overlap frame preparation with submission | 1.1–1.6 ms of render thread time (mirror off); frame rate at 90 Hz | Threading change | Idea |
| 6 | Reduce the render thread's submission cost | 4–6 ms of CPU per frame on Ghostbusters | Investigation | Counted (section 7.3) |
| 7 | Dynamic resolution floor and target per table | Steadier frame rate on heavier tables | Settings | Available now |
| 8 | Remove the frame pacing stall | Avoids a fall to the floor resolution; 2 ms of render thread per frame | Desktop window not drawn in the Frame build; guard in `VRDevice` | **Done** (sections 7.5, 7.6) |
| 9 | Cheaper VR room on pixel-bound tables | 2.1–2.3 ms on Ghostbusters with the room off (1.95 ms, 2026-10-23) | Engine work | Investigated: 3 options dropped, 3 open (`VR Rooms on the Steam Frame.md`) |
| 10 | Foveate the tonemap and FXAA passes (shading rate image on their targets) | **−1.35 ms on Addams, −1.53 ms on Ghostbusters at 2160²** (each pass 1.6 → 0.9 ms) | Small to medium: the rate attachment of `03-fragment-shading-rate.patch` on two more framebuffers, sized for each target | **Done** (2026-10-30, in master 2026-10-31; `PlayerVR/FoveateTonemapping`, on by default, and `PlayerVR/FoveateAntialiasing`, Automatic by default since 2026-10-31): −1.67 ms on Addams, −1.79 ms on Ghostbusters as implemented |
| 11 | Cheaper FXAA: luma written in alpha by the tonemap and read by gathers, then tonemap and FXAA in one pass | Bound as item 3; Fast FXAA is slower than Standard on the Frame (+0.13 to +0.19 ms) | Small (first step), medium (fusion); upstream shaders | **First step done** 2026-10-31 (8.5): FXAA pass 0.67 → 0.51 ms, frame −0.12 ms on Addams; the fusion is bounded by the 0.5 ms left |
| 12 | Scene buffer in RG11B10F (4 bytes per pixel) instead of RGBA16F (8) | **−0.66 ms on Addams, −1.28 ms on Ghostbusters**; with item 10, −2.12 and −2.69 ms | One format in the Frame build, after checking that nothing reads the alpha, that no value is negative, and banding | **Done** 2026-10-31, a live switch on by default in the Frame build (`Player.CompactSceneBuffer`, Steam Frame > Scene buffer): −0.88 ms on Addams, −1.52 ms on Ghostbusters with item 10 on (8.4); checked worn by the owner |
| 13 | Render-only mesh simplification, with a coarser mesh in the reflection probe | **Addams −1.29 ms at 2160² (11.86 → 10.57 ms), native 95 % of the time instead of 45 % with dynamic resolution**; Iron Man −0.5, Attack from Mars −0.2, LOTR −0.24 | Medium | **Done** (in master since 2026-10-29; 8.2, measured on the Frame 2026-10-29); meshes exported as separate triangles welded first, 2026-10-30: Albator78 19.8 → 11.1 ms |
| 14 | Hardware multiview: one vertex shader run for both eyes | Unknown: one eye drawn saves 4.16 ms on Addams, 3.06 ms on Ghostbusters, but that includes the second eye's pixels, which multiview keeps | Large: bgfx has no multiview, and VPX's varyings are in view space | Bound measured 2026-10-30 (8.3), too loose to decide |
| 15 | Dynamic resolution target above 85 % of the period | The GPU time left idle each frame (about 2 ms) | Settings | Measured 2026-11-02 (8.12): 90 % per table for tables just under their full size (Darkest Dungeon worn 48 → 87.7 % at full resolution, Medium sensitivity); the default stays 85 %, as 90 % makes Robot Invasion with its mirrors miss a frame per second (0.14 at 85 %). It was the default for a few hours that day (7c44b22e7) |
| 16 | Dynamic resolution ignores what the size does not cause: frames over the period independent of the size, and a counter reading over the GPU's busy time | Darkest Dungeon sits about 2 ms under the target at 1805²; 12 ms at 2160² | Small | **Done in part** 2026-11-02: one cause of these frames removed, item 21 (8.11); the readings over the period left out by the setting *Dynamic resolution sensitivity*, Medium by default, and missed frames under the same setting since the evening (8.12, not played yet); the counter reading over the GPU's busy time remains |
| 17 | VR room left out of the playfield reflection | **WildWest −2.7 to −3.3 ms, 2031² → 2160²; Maverick −5 ms, 36 → 72 fps**; little on other tables, nothing on Darkest Dungeon | Small per table (recipe), medium as a rule | **Done** 2026-11-01, setting *Room reflection* (8.9); the same result worn with a recipe, since dropped: green, 98.5 % at full size |
| 18 | Mirrors other than the playfield | **Robot Invasion −11 to −12 ms, 1559² → 2155²; 2001 −5.2 ms**; Last Action Hero nothing; 4 tables of 94 have such mirrors | Small per table (recipe) or a step of the same rule | **Done** 2026-11-01, setting *Extra mirrors* (8.9); the same result worn with a recipe, since dropped: green, 100 % at full size |
| 20 | Threads on chosen cores while a table is played: render on core 7, logic on 5 and 6, the others on 2 to 4 | **Worn, Darkest Dungeon 1.9–19.4 % → 62 % at full resolution** (3 plays without, 2 with), mean size about 1770² → 2070²; headless with Steam's cores, little | A small module | **Done** 2026-11-01, `Player.ThreadPlacement`, on by default, no menu entry (8.10); sound fine according to the owner; World Joker Tour worn 6.3–12.7 % → 33 % (one play with it); long plays not measured |
| 19 | Simplify the parts of "group elements" collections that are not really merged | WildWest −0.6 ms, no change with dynamic resolution; Addams and LOTR nothing | One condition | Measured 2026-11-01 (8.6): low value |
| 21 | PuP's decoder closed outside the lock that drawing its screens takes | **Worn, Darkest Dungeon 62 % → 74.7 % at full resolution** (one play of 111 s, two of 171 and 188 s before); headless, waits 18–22 → 0 in 90 s, missed frames 0.07–0.17 → 0.04–0.05/s, time at full size unchanged | A few lines | **Done** 2026-11-02, in master 0659ce1e7 (8.11); PuP tables only; a longer play to confirm |

Details:

1. **Desktop mirror**: see sections 6 and 7.1.
2. **Playfield reflection**: the probe renders the reflected scene every frame (1.0–1.3 ms with the mirror off). Today,
   "Static & Balls" for a heavy table removes most of it at the cost of the dynamic reflections. Engine options: render the probe at a
   lower resolution, update it every other frame, or leave the VR room out of the reflection. Each needs a visual check in the headset.
   Measured on 2026-10-30 (section 8.3): in VR the static parts are not prerendered (the view moves), so "Static & Balls" still draws
   them every frame; only "Balls Only" removes the probe's cost. A lower resolution (−0.2 to −0.5 ms at 50 %), coarser simplified
   meshes (−0.2 ms at 8 times the error) and leaving small parts out (nothing on Addams, −0.14 ms on Ghostbusters under 64 px) save
   little. Updating every other frame was not tried: the reflection would lag behind a moving head.
3. **Tonemap + FXAA**: the tonemap writes a full-resolution intermediate that FXAA reads; doing FXAA in the same pass, or tonemapping
   straight into the swapchain when FXAA is off, removes one full-screen write and read per eye. The code is upstream's, shared by
   desktop and VR, so the change belongs upstream. FXAA reads the neighbours of each pixel after tonemapping, so a fused pass would
   tonemap every sample it reads: the gain is uncertain until measured.
4. **Scene geometry**: Addams draws 1.9 million triangles per frame in 332 draws (section 7.3). Next: split the table from the room with
   the room hidden, as done for Ghostbusters in section 7.2; then per-eye frustum culling, merging static meshes, simpler room meshes,
   levels of detail for distant room geometry. Measured on 2026-10-25 in `Table Geometry on the Steam Frame.md`:
   the cost is linear in the triangles; culling parts outside the view is done (`Culling Parts Outside the View.md`), a render-only
   simplification pass run by VPX is the remaining option.
5. **Frame preparation and submission**: the render thread waits 1.1–1.6 ms per frame for the logic thread with the mirror off (1.8–3.5 ms
   with it on). Preparing frame N+1 while frame N is submitted would give the render thread that time back, which matters most for
   90 Hz.
6. **Submission cost**: 4–6 ms of render thread time per frame on Ghostbusters (34 % of a core) against about 3 ms on Addams (15 %);
   since the desktop window is no longer drawn (section 7.6), 4.1 ms and 1.7 ms.
   Ghostbusters submits 1 000 draws and 29 000 shader parameter changes per frame; Addams 332 and 9 600. Fewer parameter uploads (only the
   ones that changed, or per-material uniform buffers) are the first thing to look at.
7. **Dynamic resolution settings**: with the mirror off, Addams runs at 93 % and no longer reaches the 70 % floor; a lower floor only
   matters for heavier tables or a hot device, at a visible cost in sharpness. Per table, with the settings of the VR page.
8. **Pacing stall**: caused by the desktop window presented along the headset frames, no longer drawn in the Frame build (section 7.6).
   Dynamic resolution also holds on stalled readings and breaks a stall with one missed frame (section 7.5), for other setups. Upstream
   candidate: present the desktop mirror without blocking the headset frame.
9. **VR room**: on Ghostbusters the full room costs about a fifth of the GPU frame, in pixels rather than geometry, and the table's
   minimal room saves little of it. Investigated on 2026-10-23 (`VR Rooms on the Steam Frame.md`): the lighting math is a small share
   (0.2 ms), the read of the sky texture the largest part that can be cut (0.65 ms), and about 1.3 ms is the cost of covering the pixels.
   Drawing the room later, leaving FXAA off on far pixels and handing the sky to the runtime (no panorama layer on the Frame) are ruled
   out; a cheaper read of the sky texture, half-rate shading of sky-like parts and skipping parts outside the view are open. Hidden rooms
   cost no GPU time.
10. **Foveated post-processing**: the shading rate image is attached to the scene buffers only (`Renderer::SetFragmentDensityMap`,
   `BackBuffer1/2`), so the periphery is shaded coarsely in the scene, then tonemapped and anti-aliased at full rate (3.2 ms of the
   Addams frame at 2160²). The rate attachment of `steamframe/patches/bgfx/03-fragment-shading-rate.patch` works on any framebuffer
   in Turnip's direct path (*Foveated Rendering on the Steam Frame.md*, section 11), so it can go on `PostProcess1` (tonemap) and on the
   framebuffer of the headset swapchain (FXAA), and on the reflection probe. The rate image must match each target's size under dynamic
   resolution, as for the scene buffers. The fragment shader then runs once per coarse block but every pixel is still written, so the
   gain is less than the share of coarse pixels. Visual cost: in the periphery, edges become as blocky as the shading already is there.
11. **Cheaper FXAA**: the Standard FXAA of `fs_pp_fxaa.sc` computes the luma of 9 RGB reads per pixel (lines 51–58). First step: the
   tonemap writes the luma into the alpha of `PostProcess1`, and FXAA reads the neighbours' luma with `textureGather` (as in FXAA 3.11),
   which removes most of the reads. Second step: tonemap and FXAA in one pass (item 3), which removes a full-resolution write and read
   (2 × 37 MB per frame at 2160²). Both are upstream shaders. The first step is done (section 8.5).
12. **RG11B10F scene buffer**: VPX asks bgfx for `RGB16F`, which bgfx renders as RGBA16F (`RenderTarget.cpp:143`), 8 bytes per pixel.
   The scene pass runs direct (not tiled), so every scene fragment written or blended goes through memory, and the tonemap, bloom and
   probe read it again. RG11B10F is 4 bytes, renderable on Adreno, and already used by the texture compressor. To check first: that no
   pass reads the back buffer's alpha (screen-space transparency, refraction probes) and that no value is negative; then dark gradients
   for banding (6-bit mantissa against 10). Done on 2026-10-31 (section 8.4): no alpha read, no negative value stored, banding left to
   the check worn.
13. **Render-only simplification**: the measured gain of item 4 (weight 10, bound by distance): Addams 11.67 → 10.41 ms (0.5 VPU) or
   10.03 ms (1 VPU), its probe included; a coarser mesh in the probe adds about 0.3 ms more (probe 1.02 ms against 0.75 ms with
   strict 2 VPU). Iron Man and LOTR about 0.5 ms, Attack from Mars 0.2, light tables nothing. Approach in section 8.2.
14. **Hardware multiview**: both eyes are one instanced draw, and the vertex shader runs once per eye (`gl_Layer = gl_InstanceID`,
   `vs_basic.sc`). With `VK_KHR_multiview`, Turnip can run the vertex shader once and output one position per view, but only when
   nothing but the position depends on the view (to verify in Mesa's `tu_shader.cc`). VPX's shaders pass `v_worldPos` and `v_normal` in
   view space, so they would have to light in world space, and bgfx would need multiview render passes and `gl_ViewIndex`. Before any of
   this, measure the bound: the same frame with one eye drawn (instance count 1), which gives the per-eye share of the 2.2 ms per
   million triangles.
15. **Dynamic resolution target**: the scale aims at 85 % of the period (11.8 ms of 13.9), so about 2 ms of GPU time is idle in every
   frame. The compositor's own GPU work (6–8 %) runs in that time, which is the reason for the margin. A run at 88 % and 90 %, counting
   missed frames, says whether part of it can become resolution. *Measured 2026-11-02 (section 8.12): 90 % suits a table just under its
   full size (Darkest Dungeon), but makes a table that cannot hold it miss a frame per second; the default stays 85 %.*

For sharpness (*Image Quality on the Steam Frame.md*): with the mirror off, heavy tables like Addams reach about the native 2160², and light
tables like Ghostbusters reach the 2160² ceiling with about 1.7 ms to spare, so a *Render resolution* slightly above 125 % may fit them
(not measured). Beyond that, the GPU time freed by opportunities 2 to 4 and 9 is what dynamic resolution can turn into resolution, or
into headroom for the heat.

### 8.1 Review of 2026-10-29: what keeps Addams below the full size

Since the climb rule of 2026-10-28 (section 7.4), Addams spends 88–91 % of its time at 2160², at 11.0–11.2 ms against the 11.8 ms
target, with a few frames missed per 3 minutes. Holding the full size all the time needs 1–2 ms less GPU time, not a new renderer. At
2160², mirror off, its passes are: scene 6.26 ms (mostly triangles), tonemap 1.57, FXAA 1.65, probe 1.33 (mostly triangles), bloom 0.19.
The full-screen passes are a third of the frame and are the part not studied so far; the geometry was (`Table Geometry on the Steam
Frame.md`).

Already ruled out, not to retry: forced tiled or direct rendering, the density map (both in *Foveated Rendering*), vertex cache
reordering, `TU_DEBUG=sysmem` (Table Geometry, section 7), and the room options of `VR Rooms on the Steam Frame.md` (room drawn later,
FXAA left out on far pixels, runtime sky layer). Low value: bgfx's discard flags (never used by VPX, so every depth buffer is stored and
every pass that does not clear loads its target, but the passes that matter run direct, where loads and stores cost nothing), the depth
format, quantized vertices.

Proposed measurement series, before any implementation: Addams, fixed 2160², headless, temporary hooks, the base run repeated in the
series. Variants: the current master as the base; post-processing foveated (item 10); RG11B10F (item 12); FXAA off, as the bound of
item 11; one eye drawn, as the bound of item 14; dynamic resolution at 88 % and 90 % (item 15). Run on 2026-10-30, with Ghostbusters
and the steps of a lighter playfield reflection (item 2): section 8.3.

### 8.2 Render-only simplification: approach under discussion (2026-10-29)

Item 13 is the largest measured gain on the tables that need it (Addams, Attack from Mars, Iron Man: tables built from scans or
exports), and nothing on light tables. Points of design, with a proposal for each:

- **Where it runs.** At load, before the GPU buffers are built (`Primitive::RenderSetup`), with the result cached per table in
  `cache/<title>/meshes/` like the texture compressor (key: hash of the mesh, settings and format version), and the same pass run when a
  table is imported into the library so that the first play is already fast. No tool rewrites the author's file (Table Geometry,
  section 8).
- **What it changes.** meshoptimizer's simplifier collapses edges onto existing vertices: its result is a new index list over the same
  vertices. A simplified level is therefore only a second index buffer; positions, normals and texture coordinates of the kept vertices
  are exact, physics and the editor keep the original mesh.
- **Distance.** The measured runs scaled the bound by the distance from a pinned head at the lockbar. A player leaning in brings a part
  closer than that. Two ways: (a) one simplified level built for a nominal head position, as measured; (b) two or three levels per mesh
  (full, 0.5 VPU, 1–2 VPU), and each frame each part draws the level matching its distance from the actual head, the probe taking the
  coarsest. (b) costs a distance test per part per frame (the culling of `Culling Parts Outside the View.md` already transforms the part's box) and a few index
  buffers, follows the player, and gives the coarser probe mesh at no extra work. Proposal: (b).
- **Which parts.** Static parts and parts that move (the bound is in the part's own space); not vertex-animated meshes, light maps,
  additive parts, meshes under about 500 triangles, nor parts whose mesh the script rewrites. Grouped parts are merged at load and need checking separately.
  Meshes that coincide (a part and its "off" or light map layer, as Addams' GI pairs and LOTR's `wireramps`) are simplified once and
  share the index list, so the layers stay exactly on top of each other. Since 2026-11-02, light maps made of some of their part's
  triangles (tables baked with VLM) are simplified with the part, from the same collapses, the edges of the light maps and the seams of
  their textures kept as seams that may only get shorter (locked until 2026-11-03, see section 8.13; `docs/Steam Frame Branch.md`, section 19).
- **Settings.** On by default in the Frame build with a per-table switch (as texture compression has), off by default elsewhere; an
  upstream pull request later, as an option.
- **Validation.** The Mac captures of Table Geometry, section 6 (wire guides, printed artwork), the owner's check in the headset with
  head motion (specular highlights on re-triangulated surfaces, a level switching while leaning in), and a headless series on Addams,
  Attack from Mars, Iron Man and LOTR against the current master.

Open questions for the owner: (a) or (b); on by default or per table; which tables besides Addams to validate in the headset.
**Decided 2026-10-29: (b).**

**Follow-up: levels chosen by foveation.** Foveated shading lowers the fragment work in the periphery but not the geometry, which is
paid per triangle wherever it lands; the level selection of (b) can take the periphery into account by raising the error allowed for a
part by its angle from the gaze (eye-tracked) or from the lens centre (fixed foveation), with the full allowance in the clear zone. It
reuses the same levels and only changes the rule that picks one. Risks: a part is drawn once and may span the clear zone and the
periphery (use its closest point to the gaze); the eye moves fast and the gaze arrives a few frames late, so a coarse level could be seen
right where the eye lands (a wide clear zone, hysteresis); in play the eye mostly rests on the playfield, where Addams' heavy parts are,
so the gain is smaller than the distance rule's. Plan: build (b) with distance only, then count on the Mac, along the scripted head path,
the triangles a rule by angle would remove (fixed foveation first: the lens blurs the periphery anyway, and there is no gaze latency); try
it in the headset only if the count is worth it, off by default.

**Implementation (2026-10-29, branch `mesh-simplify`).** As decided above: levels made while the table loads (no cache), chosen per frame
by distance, the setting on by default on the Frame with a per-table override and a live switch (`Steam Frame Branch.md`, section 19). One
change from the discussion: the allowed error is given in pixels of the view (`Player.SimplifyMeshesError`, default 1) rather than in VP
units at 0.5 m, so that it follows the projection: 1 pixel at 2160² on the Frame is close to 1 VPU at 0.5 m, and a zoomed desktop camera
gets finer levels. First results on the Mac (Addams): 1.43 M triangles in the 114 simplified meshes, 1.10 M at 0.5 VPU, 0.84 M at
2 VPU; desktop view 1.33 → 0.93 M triangles per frame; close-up captures at the headset's pixel size magnified 4×: no change on the
flippers and apron, single silhouette pixels of round parts on the upper playfield (0.02 %); loading 0.3 s longer.

**Forecast in the simulated headset (2026-10-29).** The `xr-sim` branch, rebased locally onto `mesh-simplify` (not pushed), 2160×2160 per
eye at 72 Hz, a scripted head (`Doc/test-hooks/simplify/headpath.txt`): 10 s at the lockbar, leaning over the flippers (30 cm forward,
25 cm down), leaning toward the upper playfield (55 cm forward), looking at the backglass, left, right, back at the lockbar. A temporary
counter (`simplify/temp-hooks.py`, never committed) logs the triangles of every frame, by level, in the main view and the probe; the
GPU time is forecast at 2.2 ms per million triangles (the linear fit of `Table Geometry on the Steam Frame.md`, section 5.1). Without
simplification the simulator draws what the Frame draws at its pinned pose (Addams 1.91 M triangles, Attack from Mars 0.72 M, Iron Man
0.50 M, against 1.90 M, 0.72 M, 0.50 M), so its view and parts are the Frame's.

| Lockbar, 1 pixel | Field of view per eye | Triangles without | With | Saved | Forecast on the Frame |
|---|---|---|---|---|---|
| Addams Family | 90° | 1.91 M | 1.38 M | 28 % | −1.16 ms |
| Addams Family | 100° | 1.91 M | 1.36 M | 29 % | −1.21 ms |
| Addams Family | 110° | 1.91 M | 1.31 M | 31 % | −1.31 ms |
| Addams Family, 1.5 pixels | 100° | 1.91 M | 1.24 M | 35 % | −1.46 ms |
| Attack from Mars | 100° | 0.72 M | 0.55 M | 24 % | −0.38 ms |
| Iron Man | 100° | 0.50 M | 0.29 M | 41 % | −0.45 ms |

- The Frame's field of view per eye is not known here (90° is the simulator's default); the forecast moves by 0.15 ms between 90° and
  110°. Leaning in or looking around saves less (Addams −0.6 to −1.1 ms): fewer triangles in view.
- The hook runs of `Table Geometry on the Steam Frame.md` measured −1.64 ms with a bound of 1 VPU at 0.5 m growing with the distance;
  1 pixel at 100° is about the same angle, but levels are discrete: a part allowed 1.9 VPU draws the level under it. Steps of ×1.41
  instead of ×2 up to 4 VPU took Addams from −1.07 to −1.16 ms at 90° (CPU at load 3.4 → 4.5 s on a Mac).
- A first run drew 60 % fewer triangles, nearly all at coarse levels even when leaning in: in VR the projection carries the scale of
  the table (its y term was 0.00066), which the pixel size must divide out (ratio to the w term). Fixed before these runs.
- Level switches: none while the head holds a pose (with a small sway), 9 to 30 over the two seconds of each move, out of 114 parts:
  the margin works. Clips of the moves for the owner's review: `vp_vr/mesh-simplify-review/` (left eye at 1920×1080, not the headset's
  pixel density).
- Next: the Linux build and a Frame series (Addams, Attack from Mars, Iron Man, LOTR: simplification off/on, fixed 2160²), and the
  owner's check worn, which also gives the real field of view.

**Measured on the Frame (2026-10-29, headless).** The branch built for the Frame with the test hooks of section 9, a copy of the developer
deploy with that executable (`~/perf-test/vpx-simplify`), `Doc/test-hooks/simplify/simplify-series.sh`: 120 s per run, 40 s rests, the
eyes pinned at the lockbar (section 2.1); `off`/`on`/`on15` at fixed 2160² with bgfx's GPU profiler, `droff`/`dron` with dynamic
resolution as the player has it. Results on the Frame in `~/perf-test/results-simplify`.

| Table, fixed 2160² | Runtime GPU off | On (1 px) | Saved | Triangles per frame | Scene pass | Reflection probe |
|---|---|---|---|---|---|---|
| Addams Family (2 runs each) | 11.94, 11.77 ms | 10.58, 10.56 ms | **−1.29 ms** | 1.90 → 1.36 M | 6.34 → 5.45 ms | 1.34 → 1.05 ms |
| Addams Family, 1.5 px | | 10.32 ms | −1.54 ms | 1.25 M | 5.25 ms | 1.03 ms |
| Iron Man | 10.23 ms | 9.73 ms | −0.50 ms | 0.50 → 0.29 M | 4.43 → 4.20 ms | 1.16 → 0.91 ms |
| Attack from Mars | 9.86 ms | 9.66 ms | −0.20 ms | 0.72 → 0.56 M | 4.69 → 4.70 ms | 1.32 → 1.13 ms |
| Lord of the Rings (36 fps either way) | 17.32 ms | 17.08 ms | −0.24 ms | 1.34 → 1.22 M | 8.59 → 8.50 ms | 1.01 → 0.88 ms |

| Addams Family, dynamic resolution | Status lines at 2160² (after 20 s) | Average size | Runtime GPU |
|---|---|---|---|
| Off | 45 % | 2135² | 11.49 ms |
| **On** | **95 %** (all of them after 30 s) | 2155² | 10.48 ms |

- The forecast of the simulated headset held: Addams −1.21 ms forecast, −1.29 ms measured; the triangle counts match (1.36 M on both).
  Per million triangles left out: Addams 2.4 ms, Iron Man 2.5 ms, LOTR 1.9 ms, Attack from Mars 1.2 ms (its scene pass did not move: one
  run each, ±0.2 ms of noise, and the triangles it loses are mostly in the probe).
- **Addams now holds the native resolution**: with dynamic resolution, 1.4 ms under the 11.8 ms target instead of 0.3 ms over it.
  At fixed 2160², the frames missed during the first 25 s of a game fell from 20–26 per 5 s to 2–4 (the burst of the first 5 s after the
  start, 12 to 50 frames, is the same with and without).
- **Loading**: the texture phase (textures from the cache) takes longer while the levels are made: Addams 0.5 → 1.4 s (7.1–7.5 s of CPU
  over the cores), Attack from Mars +0.4 s, LOTR +0.3 s, Iron Man +0.15 s. A cache of the levels (section 8 of `Table Geometry on the
  Steam Frame.md`) would remove it from the second start on; not done.
- LOTR stays at 36 fps at fixed 2160² (it needs dynamic resolution); 92 of its meshes were simplified, 15 sharing the levels of a
  coinciding layer.
- Still to do: the owner's check worn (image up close, level switches while leaning in, the real field of view). The branch, built
  without the test hooks, is in the developer slot of the Frame since 2026-10-29 (package `steam-frame-package-simplify/`, its own folder;
  the libraries left in the build volume by older builds, SDL3 0.4.16, winevbs 0.8.5, ffmpeg 62 in the plugins, removed from it).

**Albator78: meshes exported as separate triangles (2026-10-30).** The simplifier removed about 9 % on this table (595 626 → 503 806
triangles at 0.5 VPU, 41 meshes simplified, 18 skipped) and the table was red. Eight meshes (`FEMME ALBATOR` 484 820 triangles, `Primitive026`
294 072, `FEMME ROBE`, `Primitive044`, `Primitive027`, `ALBATOR`, `Moonbase`, `FEMME ROSE`) hold 1.30 M of the 1.78 M candidate triangles
and are flat-shaded triangle soup, about 3 vertices per triangle: each position has several unrelated vertices, the simplifier's seams
lock everything. They also cost 2.44 vertex shader runs per triangle, against 0.97 on Addams. Welding alone is not enough: the weight of
the attributes, relative to the size of the mesh, is far too strict on a statue of 3500 VPU (positions only: `FEMME ALBATOR` → about 11 000
triangles at 4 VPU). What the GPU spends (headless, fixed 2160², `~/perf-test/albator-series.sh` on the Frame, parts hidden by a sidecar
script; 13.9 ms is the budget):

| Run | Runtime GPU | Triangles per frame | Note |
|---|---|---|---|
| Base (2 runs) | 19.43, 19.64 ms | 2.64 M | 36 fps; reflection probe 5.5 ms, against 1.05 on Addams |
| Simplified meshes off | 19.79 ms | 2.81 M | |
| PuP off | 19.02 ms | | |
| Reflection off | 14.46 ms | 1.40 M | |
| The 8 meshes out of the reflection only | 17.09 ms | | noisy (14.69 to 28.40); the probe itself 5.5 → 1.23 ms |
| The 8 meshes hidden | 10.32 ms | 0.38 M | |
| Room geometry hidden | 10.79 ms | 0.54 M | |
| Room geometry and sky hidden | 8.56 ms | | the sky costs about 2 ms |
| Dynamic resolution | 10.22 ms | | 1526² on average, near the floor |

Two changes, described in `docs/Steam Frame Branch.md` section 19. **Welding**: a mesh with at least 1.5 vertices per triangle is
simplified from a welded copy, with weights independent of its size and meshoptimizer's permissive mode, and its levels draw one existing
vertex per place. **Reflection culling**: a part is left out of a horizontal reflection when the lines from the mirrored eye through its
box cannot land on the parts showing the probe. In the simulated headset the culling saves 133 000 of 635 000 triangles at the lockbar,
modest now that the statues are cheap; it has no switch, so the Frame's numbers below are of both together.

| Fixed 2160², headless (`~/perf-test/results-weld`) | Before | After |
|---|---|---|
| Albator78, runtime GPU (2 runs, alternated) | 19.84, 20.27 ms | **11.14, 12.06 ms** |
| Frame rate | 36.0 fps | 71.8, 71.6 fps |
| Triangles per frame | 2.64 M | 0.57 M |
| Scene pass / reflection probe | 9.27 / 5.61 ms | 4.66 / 1.47 ms |
| Frames missed per 5 s, late in the run | 180 | 0–2 |
| Time from the table loaded to the start | 8.4 s | 12.9 s (simplification 13.8 → 34.5 s of CPU) |
| Simplified meshes | 595 626 → 502 918 at 1 VPU, 18 skipped | 1 703 684 → 798 017 at 1 VPU, 10 skipped |
| Addams, runtime GPU | 10.61 ms | 10.39 ms |
| Addams, triangles per frame / start | 1.36 M / 3.10 s | 1.29 M / 3.26 s |

- With dynamic resolution Albator78 now averages 2022² (mostly 2050 to 2160, 0–2 frames missed per 5 s), against 1526².
- The GPU estimate from hiding the 8 meshes held: 10.3 ms hidden, 11.1–12.1 ms simplified.
- Visual check in the simulated headset (6 poses, up to 3× crops, against the simplification off): the statues, ship, room and reflection
  look the same, without smeared textures. The skull on the playfield loses its facets and comes out smoothly shaded, with slightly softer
  teeth; a few tiny dark specks may show on the statue at 3×. The owner played the build in the headset on 2026-10-30: it works.
- On Addams, 14 small meshes (82 000 triangles: `RGP10_steelbrushed*`, `Thing`, habit trails, static targets) also take the welded path;
  the close-ups of the wire ramps and `Thing` look right.
- Not settled: the runs last 100 s, with the GPU at 89–92 °C and the CPU at 93–97 °C at 72 fps, so a long play is not measured. Two 5 s
  readings around 16 ms in one of the fixed runs, and one fall of dynamic resolution to the 1512² floor, remain unexplained, as does the
  bloom pass at 1.5–2.8 ms instead of 1.0 ms. The same source gives fewer triangles on the Frame than on the Mac (798 017 against 859 966
  at 1 VPU; not investigated), so the Mac's visual check is close to the Frame's meshes, not identical. Other tables of the library with
  such meshes now take this path and were not surveyed. The levels stop at 16 VPU while parts as far as `Primitive026` (about 33 000 VPU)
  could take about 36, and the sky's 2 ms is untouched.

**Working without the Frame (2026-10-29).** The macOS build with the simulated headset (branch `xr-sim`, `docs/XR Simulator Branch.md`)
does not give timings that transfer to the Frame: the Apple GPU and bgfx's Metal backend handle geometry and memory differently (more
vertex throughput, far more bandwidth, the scene kept in tile memory, where the Frame renders the scene pass direct), and the Metal build
has no shading rate image, no multiview path of Turnip, no SteamVR compositor. What it gives reliably, and what is left for the Frame:

| Item | On the Mac | On the Frame |
|---|---|---|
| 13 Simplification | The implementation; triangles per pass with and without, along a scripted head path (leaning in, looking around), turned into a forecast with the Frame's 2.2–2.4 ms per million triangles (linear, measured on five tables); the visual check, including a level switching while the scripted head leans in, recorded frame by frame against a run without simplification; load time, stacked layers, the whole Mac copy of the library as a robustness test | One series (Addams, Attack from Mars, Iron Man, LOTR) and the owner's check in the headset |
| 11 Cheaper FXAA | Same image (luma in alpha, fused pass); possibly Adreno shader statistics, with Turnip compiling on Mesa's `drm-shim` in the Linux ARM64 container (not verified) | Timing |
| 12 RG11B10F | Which passes read the back buffer's alpha; banding | Timing |
| 10, 14, 15 | Feasibility only | Everything |

`xr-sim` is behind master: for this work, a local copy of it is rebased onto the work branch (no push).

### 8.3 GPU series of 2026-10-30: post-processing, scene buffer format, reflection probe

*Fork `master` at 80ea732db (the simplified meshes in, upstream sync of 2026-10-30). Addams Family and Ghostbusters VR rooms, headless
(`VPX_PERF_FORCE_RENDER`, section 2.1), 2 minutes per run, the window from 40 s, bgfx's GPU profiler on; each variant switched by an
environment variable of temporary hooks (`Doc/test-hooks/gpu-series-hooks.py`, never committed). The base run was repeated at the start,
middle and end of each table: Addams 10.55–10.58 ms, Ghostbusters 9.90–10.01 ms, so the heat of the series does not weigh on the
differences, which are runtime GPU time per frame at fixed 2160².*

At the base, the passes are, on Addams: scene 5.47 ms, tonemap 1.57, FXAA 1.57, playfield reflection probe 1.05, bloom 0.19; on
Ghostbusters: scene 3.89, tonemap 1.66, FXAA 1.53, probe 1.23, bloom 0.88.

| Variant | Addams | Ghostbusters | What changed |
|---|---|---|---|
| Base | 10.56 ms | 9.94 ms | |
| **Foveated tonemap and FXAA** (item 10) | **−1.35** | **−1.53** | Addams: tonemap 1.57 → 0.89 ms, FXAA 1.57 → 0.90 ms; Ghostbusters: 1.66 → 0.89, 1.53 → 0.85 |
| **Scene buffer in RG11B10F** (item 12) | **−0.66** | **−1.28** | Scene −0.58 / −0.81 ms; on Ghostbusters the bloom −0.34 ms |
| **Both** | **−2.12** (8.44 ms) | **−2.69** (7.25 ms) | The gains add up |
| FXAA off (bound of items 3 and 11) | −1.52 | −1.56 | The tonemap writes the swapchain: the FXAA pass is gone |
| Fast FXAA | +0.19 | +0.13 | Slower than Standard FXAA |
| One eye drawn (bound of item 14) | −4.16 | −3.06 | Scene and probe halve: per-eye vertex and pixel work |
| Probe: simplified meshes at 4 / 8 times the error (default 2) | −0.09 / −0.20 | ≈ 0 / – | 41 000 / 94 000 triangles less on Addams |
| Probe: parts under 16 / 64 px left out | 0 / 0 | – / −0.14 | Addams has no part that small in the probe |
| Probe at 75 % / 50 % of its size | −0.17 / −0.19 | −0.15 / −0.48 | Probe pass, bgfx: Addams 1.05 → 0.90 / 0.78 ms, Ghostbusters 1.23 → 1.08 / 0.96 ms |
| Reflection Quality "Balls Only" / "Disable Reflections" | −1.13 / −1.11 | −1.29 / −1.34 | The probe pass is gone; 300 000 / 100 000 triangles less |
| Dynamic resolution, target 85 / 88 / 90 % | 2160², 0 missed | 2160², 0 missed | Both tables at the ceiling at any target |

- **Foveated post-processing is the largest gain for the effort**, and it applies to every table, light or heavy. With the shading rate
  image on `PostProcess1` (tonemap) and on the frame buffers of the headset swapchain (FXAA), each pass runs in 0.9 ms instead of 1.6.
  In the hooks the rate image is the scene's, which fits only because the three targets have the same size at fixed 2160² and 100 %
  render resolution: the implementation must size a rate image for each target (the swapchain does not follow the scene's size when the
  render resolution is not 100 %), and follow dynamic resolution as the scene's does. Visual cost to check worn: in the periphery, the
  edges become as blocky as the shading already is there, and FXAA works on coarse blocks.
- **RG11B10F is the second**: about 0.6 ms on the scene pass, and on Ghostbusters a third of the bloom. Before using it: no pass may read
  the back buffer's alpha (screen-space transparency, refraction), no value may be negative, and dark gradients must not band (6 bits of
  mantissa against 10). The bloom buffer, also asked as RGB16F, changed format with it.
- **With both, the GPU ran 12 °C cooler** (Addams 92 → 80 °C, Ghostbusters 86–88 → 76 °C, maximum of each run) and 15–20 points less
  busy. Both tables already hold 2160² with dynamic resolution, so on them the time would go to a render resolution above 100 % (sharper)
  or to the heat; on heavier tables (LOTR, 17 ms at 2160²) to the resolution.
- **A cheaper FXAA comes after**: with the passes foveated, the FXAA pass is 0.9 ms, the most a fusion with the tonemap could save. Fast
  FXAA is not an option: it is slower than Standard on the Frame.
- **The steps of a lighter reflection that keep it dynamic save little.** On Addams the probe costs 1.05 ms and the best of them, the probe
  at half size, saves 0.19 ms; coarser meshes 0.20 ms at 8 times the error. Leaving small parts out does nothing on Addams, whose parts
  are large meshes. Only leaving the dynamic parts out ("Balls Only") removes the whole cost, and "Static & Balls" does not (the static
  parts are not prerendered in VR). A ladder that lowers the reflection when the table needs the time is therefore not worth its
  complexity as designed; "Balls Only" stays a per-table choice for the heaviest tables.
- **Multiview remains unknown**: drawing one eye halves the scene and the probe, but that takes away the second eye's pixels as well as
  its vertices, and multiview keeps the pixels. A bound of the vertex share alone needs another measure (both eyes drawn with a trivial
  fragment shader, for example).
- **Dynamic resolution above 85 %** cannot be measured on these two tables: since the simplified meshes and the 2026-10-30 sync, both run
  at 2160² with 1.2–1.9 ms below the 11.8 ms target, and no frame was missed at 85, 88 or 90 %.

Next: item 10 is implemented (on with the shading rate image, one image per target size, a live switch for each pass;
`docs/Foveated Rendering on the Steam Frame.md`, section 11.2) and measured as implemented on the same day (Addams −1.67 ms, Ghostbusters −1.79 ms at
2160², section 11.2 of that document); then item 12 after its checks (section 8.4).

### 8.4 Compact scene buffer: checks and measurements (2026-10-31)

Item 12 as implemented: `Player.CompactSceneBuffer` (Steam Frame > Scene buffer, live; on by default in the Frame build since the owner checked it in the headset on 2026-10-31, off in the desktop builds) makes the targets VPX asks as
`RGB16F` RG11B10F ones: the scene buffers, bloom, screen space reflection and motion blur buffers, static prerender, reflection and
refraction probes (`src/steamframe/SceneBufferFormat.h`; mechanism in `docs/Steam Frame Branch.md`, section 21).

Checks in the code:

- **Alpha**: no reader. Every shader samples these targets as `.rgb` (tonemap, bloom, blur, SSR, motion blur, AO, the probes in the
  material shader, the ball's reflection of the previous frame, copy, mirror, stereo); no blend state uses the destination alpha; the
  blits between them are between targets made together.
- **Negative values**: none stored. The lights and additive flashers output a negative color into a reverse subtract blend
  (dst · (1 − src) − src · α, `light.cpp` and `flasher.cpp`), which gives dst · (1 + |src|) + |src| · α ≥ 0: the trick needs the blend
  to take the shader output unclamped, which holds for floating-point attachments, RG11B10F included. The bloom clamps its output at 0.
- **Precision**: 6 bits of mantissa for red and green, 5 for blue (10 in half floats): a step of 1.6 % and 3.1 %, about one step of the
  8-bit output in mid-tones, under the tonemap's dither. What may show worn: banding in smooth blue gradients, and where many additive
  layers stack (each blend is rounded).

Measured on the Frame, headless at fixed 2160² with the foveated post-processing on (`Doc/test-hooks/gpu-series.sh`, runs `sboff`,
`sbon`, `sbtoggle`, results in `~/perf-test/results-sbf`):

| Table | Off | On | Gain | Per target |
|---|---|---|---|---|
| Addams | 8.82 ms | 7.94 ms | **−0.88 ms** | scene 5.19 → 4.60, post-processing 0.86 → 0.65, bloom 0.19 → 0.17, probe unchanged (1.02) |
| Ghostbusters | 8.23 ms | 6.71 ms | **−1.52 ms** | scene 3.72 → 2.90, bloom 0.87 → 0.55, post-processing 0.85 → 0.62, probe unchanged (1.22) |

More than the hook of 2026-10-30 (−0.66, −1.28): the tonemap reads half the bytes too. Switched live during a run (Addams, at frames
3000 and 6000): the targets are made again with their foveation image, one frame of 11.9 ms, no error; the same on the Mac (Metal) on
2001 and Star Trek LE, from a start in either format.

### 8.5 FXAA with the luma in the alpha channel (2026-10-31)

Item 11, first step. FXAA works on the luma of the pixels (`luma()` of `fs_pp_fxaa.sc`, 0.25/0.5/0.25 weights): the Standard and
Quality variants computed it from 9 color reads around each pixel, the Fast one too, and again for each read of the edge search. Now:

- **The tonemap writes the luma of its output in the alpha channel** (`OutputAlpha` in `fs_pp_tonemap.sc`, the 4th component of
  `bloom_dither_colorgrade`), in every output branch, when an FXAA pass reads its output next (`Renderer::IsFXAALumaInAlpha`: FXAA
  Fast, Standard or Quality, output `PostProcess1` in RGBA8, not the AO view). The alpha of `PostProcess1` was not used, and the
  headset image (whose alpha the compositor uses) is written by the FXAA pass with an alpha of 1. The embedded backglass regions,
  tonemapped again, and the partial tonemap of the ball motion blur write it too.
- **FXAA gathers it** when told so (`w_h_height.w`, which says whether depth is available, gets +2): two `textureGather` of the alpha
  channel centered on the corners of the middle texel give 7 of the 9 values, two reads the other corners (4 fetches instead of 9), and
  the edge search reads the luma from the alpha of its samples (`lumaNeighbourhood`, `lumaOf`). Without the flag the shader runs as
  before.

Checked on the Mac (Metal) with a temporary debug of the FXAA shader that compared, for each pixel, the 9 values from the alpha
channel with the 9 from the color and painted the pixel when they differed: no pixel apart by more than 0.003 with Fast, Standard and
Quality FXAA, against 225,682 painted pixels with N and W swapped on purpose, so the gather order is the one used (the order of the
four texels is the same in Vulkan, Direct3D, Metal and OpenGL). The luma of the alpha channel is the luma of the color before its 8-bit
rounding, which makes no visible difference.

Measured on the Frame, headless at fixed 2160² with the foveated post-processing and the compact scene buffer on, the build before
and after alternating (`gpu-series.sh`, run `sbon`; results in `~/perf-test/results-luma-base` and `results-luma-new`):

| Table | FXAA pass | Tonemap | Frame (runtime GPU) |
|---|---|---|---|
| Addams (two runs each) | 0.67 / 0.66 → 0.51 / 0.52 ms | 0.57 → 0.59 / 0.60 ms | 7.34 / 7.34 → 7.20 / 7.23 ms (**−0.12 ms**) |
| Ghostbusters | 0.64 → 0.49 ms | 0.56 → 0.59 ms | 6.41 → 6.32 ms (−0.09 ms) |

The FXAA pass is now about 0.5 ms: the most a fusion of the tonemap and FXAA (second step) could save, less the luma it would then
compute for the neighbours itself.

### 8.6 The tables below native (2026-11-01)

*Correction of 2026-11-01: these runs were started over ssh with all eight cores allowed, while Steam gives a game cores 3 to 7 (section 8.10), so their sizes with dynamic resolution are low on tables with a busy render thread: Darkest Dungeon 1805² here, 2130² with Steam's cores; World Joker Tour and Lord of the Rings hold 2160² there. Their GPU times at fixed sizes are not concerned, nor the comparisons within a run series.*

*Fork `master` at 294aa8410 for the code read; executable `~/perf-test/vpx-autoaa` (2026-10-31, all the work above, before the later fixes that
do not touch rendering). Headless (section 2.1), fixed foveation, 100 s per run, the window from 40 s, one run per variant, bgfx's GPU profiler at fixed
sizes, compact scene buffer and foveated tonemap and anti-aliasing on. Variants by script sidecars on hard links of the tables. Repeated base
runs vary by 0.5–1.2 ms on these tables (Darkest Dungeon 11.86–12.43, WildWest 13.47–13.97, Robot Invasion 20.58–21.81, Lord of the
Rings 11.94–12.84). Results on the Frame: `~/perf-test/results-heavy`, `results-heavy2`, `results-ri`, `results-rooms`, `results-group`.*

The ratings saved on the Frame from the owner's plays of 2026-10-31 give 12 yellow tables of 27 rated. The worst, with the cost of each pass (ms per frame):

| Table | Native % worn | Runtime GPU at 2160² | Scene | Reflections | Bloom | Tonemap + FXAA | Size with dynamic resolution | Triangles / draws |
|---|---|---|---|---|---|---|---|---|
| WildWest | 0 | 13.5 | 6.5 | 3.6 | 0.7 | 1.8 | 2031² | 1.65 M / 815 |
| Robot Invasion | 1 | 21 (36 fps) | 4.7 | 6.2 (4 mirrors) | 0.7 | 1.4 | 1559² | 0.70 M / 2 330 |
| World Joker Tour | 6 | 10.6 | 5.8 | 1.7 | 0.2 | 1.4 | 2133² | 0.85 M / 1 835 |
| Darkest Dungeon | 30 (15 s played) | 12.1 | 6.8 | 1.8 | 0.9 | 0.9 | 1805² | 1.67 M / 1 330 |
| Lord of the Rings | 41 | 12.4 | 7.4 | 0.7 | 0.7 | 1.4 | 2029² | 1.19 M / 734 |
| Blade Runner 2049 | 51 | 8.9 | 3.5 | 2.0 | 0.8 | 1.2 | not run | 0.37 M / 1 134 |
| CyberRace | 72 | 11.2 | 6.5 | 0.6 | 0.6 | 1.4 | not run | 1.28 M / 420 |

**Robot Invasion: the wall mirrors.** It has 300 000 visible triangles, but four planar probes that each draw the scene again, for the playfield and
three 2-triangle wall quads: `left` (strength 0.5, and 0.05 on the blades), `right` (0.05), `back` (1.0). All have static and dynamic parts in them,
and the culling of the reflection (`IsBoxReflected`) only works on horizontal mirrors. A probe is not drawn when no part shows it, so a sidecar
that clears the `ReflectionProbe` of the wall parts tests each:

| Robot Invasion | Runtime GPU | fps | Draws | Render thread: wait + submit | With dynamic resolution |
|---|---|---|---|---|---|
| As it is | 20.6–21.8 ms | 36 | 2 330 | 15 ms | 1559², 70.6 fps |
| Parts at 5 % without mirror (`right` gone) | 13.9 ms | 70 | 1 850 | 11.5 ms | 1702² |
| Back wall mirror only | 10.7 ms | 72 | 1 430 | 9.6 ms | |
| No wall mirror | 9.6 ms | 72 | 990 | 7.2 ms | **2155²** |
| No reflection at all | 7.8 ms | 72 | 703 | 5.5 ms | |

Four tables of 94 have such mirrors (also 2001, Last Action Hero, Game of Thrones), so this is a fix for few tables.

**WildWest: the room.** 723 000 of its triangles are in its VR room collections; the MEGA room, its default, costs 5.7 ms against Minimal (13.5 → 7.8 ms)
and 6.8 ms against the cabinet alone (6.7 ms). Its playfield reflection, the largest measured here (3.6 ms against 1.0 on Addams), is mostly that room:
`ReflectionEnabled` off on the parts of `VR_Mega` gives 10.8 ms (−2.7 ms) and 1.65 → 1.08 M triangles, and dynamic resolution then holds 2160²
(2031² before). The parts are large meshes spanning the room, whose boxes the culling of the reflection cannot leave out.

**Darkest Dungeon: the table itself, and dynamic resolution.** Its room costs 2.1 ms (Castle Ruins, the default, 12.1 ms; Cabinet 10.1; Sphere 11.0; Cathedral 12.4),
all in the scene pass: the reflection stays 1.8 ms in every room, and leaving the room out of it changes nothing, as its parts already do not reach it. That
reflection is the statues of the table (reflection off −2.7 ms, and 1.7 ms less submission on the render thread); bloom off −1.05 ms. The simplified meshes save only 0.8 ms: its 1.31 M
candidate triangles only fall to 1.12 M at 1 VPU. Scene pass about 3.6 ms fixed + 0.65 ms per million pixels per eye. At 2160² it is only 0.3 ms
over the target, yet dynamic resolution saws between 1666² and 2026² at 9–10 ms, about 2 ms under the target (WildWest, at the same target, stays at it). Darkest Dungeon misses
about as many frames at 1296² (0.8 per 5 s) as at 2160² (1.2–2.6 per 5 s), and a frame over the period lowers the scale at once: the size pays for what it does not cause. What these
frames are is not established. (2026-11-02: one kind is established, a wait for a PuP decoder being closed, section 8.11; the others are not.)

**World Joker Tour** gains 1.8 ms without reflection (872 of its 1 835 draws are in it). **Lord of the Rings** gains only 0.5 ms that way: it is its scene pass, 7.4 ms,
not split further here; it is also the hottest, GPU 93 °C and CPU 97 °C in 100 s.

**The render thread now counts.** The render thread's wait for the logic thread plus its submission: Addams 2.6 ms, WildWest 5.5, Darkest Dungeon 9.7, Robot Invasion 12–15. Over about 10 ms,
frames are missed whatever the size, and the runtime's counter reads more than the GPU's busy time (DRM `fdinfo`):

| Run | Wait + submit | Runtime GPU | GPU busy | Over | Missed per 5 s |
|---|---|---|---|---|---|
| Addams 2160² | 2.6 ms | 8.1 ms | 8.2 ms | 0 | 0 |
| WildWest 2160² | 5.5 | 13.5 | 13.5 | 0 | 2 |
| Robot Invasion, no wall mirror, dynamic | 7.5 | 9.4 | 9.4 | 0 | 0 |
| Darkest Dungeon 2160² | 9.8 | 12.1 | 12.0 | 0.1–0.6 | 1–3 |
| Robot Invasion 5 % parts out, dynamic | 10.8 | 9.6 | 7.5 | 2.1 | 2 |
| Robot Invasion 1296² | 12.5 | 13.1 | 8.6 | **4.5** | 10 |
| Robot Invasion, dynamic | 11.9 | 9.1 | 6.0 | 3.1 | 7 |

Dynamic resolution steers by that counter: Robot Invasion is lowered with the GPU 43 % busy. The cause is not established (bgfx has one queue submission per frame, so the GPU
should not idle inside the frame: possibly the work starting late, the counter then including the wait for the compositor's work). The wait is the frame preparation of the logic thread, 3–6 ms on these tables (item 5).

**Group elements: not the blind spot it looked like.** Grouped parts are left out of the simplified meshes and, when really merged, of the culling. 47 tables of 95 have parts in such collections, so the flag was cleared
in copies of three tables (`ungroup-table.py`; the hash mismatch logged is harmless): WildWest 13.5 → 12.9 ms, 2019² against 2031² with dynamic resolution; Addams 8.14 → 8.13 ms; Lord of the Rings within noise.
`Primitive::RenderSetup` only merges dynamic parts sharing the material and the texture of the first, so most parts of these collections were drawn alone and culled already (the draw count does not move);
only `MeshSimplifier`'s `IsGrouped` is too wide (WildWest 194 000 → 477 000 candidates, 6 s more CPU at load): item 19.

**What is not settled.**

- **Worn against headless.** World Joker Tour holds 2133² headless and 1984² worn (11.39 ms), Blade Runner fits at fixed 2160² and rates 51 %, Lord of the Rings 2029² and 41 %. Part of this may be a heavier
  state seen in 18 of 275 runs since October, including the first four of this series: compositor at 12–24 % of the GPU instead of 5–6 %, some passes up to 25 % slower (Darkest Dungeon 14.9 against 12.1 ms and 1591²
  against 1810²), for a reason not known. No worn run has the sampler that would tell.
- The ratings are of one play, some of them short (Darkest Dungeon 15 s).
- PuP: with the plugin off, Darkest Dungeon's script stalls (1.7 fps, 599 ms of script per frame); not measured on this table.
- The look of items 17 and 18.

### 8.7 Recommendations (2026-11-01)

| Order | Work | Why | Expected | Decides it |
|---|---|---|---|---|
| 1 | Extra mirrors and the room out of the reflections (items 18, 17) | The largest gains measured, and tables to come can have them too | With the recipes that came first, worn: Robot Invasion 1 % → 100 % of the time at full size (47 s played), WildWest 0 % → 98.5 % (53 s) | **Done** as settings (8.9), first recipes per table (dropped); the owner checked the build in the headset, 2026-11-01 |
| 2 | A run worn with `perf-sampler.py`, on World Joker Tour or Darkest Dungeon | Several tables rate well under what headless runs give; tells whether worn play is the heavier state | Tells how far the headless figures hold | |
| 3 | Dynamic resolution: count the frames that lower the scale, then ignore those the size does not cause (item 16) | Darkest Dungeon sits 2 ms under the target; also steers by a counter that reads high | Darkest Dungeon from 1805² towards 2100², not measured | A headless series, counting missed frames; one cause of these frames removed first, section 8.11. **Done in part** 2026-11-02: the PuP wait (8.11), the sensitivity setting and missed frames under it (8.12); worn, Darkest Dungeon 62 → 90.3 % at full resolution with the half reflection and a 90 % target |
| 4 | Overlap of frame preparation and submission (item 5) | The render thread is now a limit: 3–6 ms of wait per frame on these tables | Fewer missed frames over 10 ms of render thread; large work | After 1 and 3, which remove part of it |
| 5 | Scene pass of Lord of the Rings and CyberRace | The reflection does not explain them; not split yet | Unknown | A series by part, as for WildWest's room |

Dropped: clearing the group flag or merging less (no gain, section 8.6), and lowering bloom on Darkest Dungeon (−1.05 ms for a visible change).

**Which fixes can be generic:** measured in section 8.8.

### 8.8 Generic rules for the reflections (2026-11-01)

*Can rules applied to any table replace the recipes, and what else can the reflection give? Same method as section 8.6, parts chosen from the table files by `Doc/test-hooks/refl-rules.py`, not by hand, tried as script sidecars, one
run per variant. In this series the compositor took 6.5–7 % of the GPU, so compare inside the table only. Lower quality was measured with the hooks of the executable of 2026-10-30 (`vpx-gpu`), which predates the culling of the reflection: Darkest Dungeon draws 1.96 M triangles there, 1.67 M today, so its savings are upper bounds.*

**Step A, mirrors other than the playfield's**: no part shows them, so they are not drawn.

| Table | Runtime GPU, before → after | Frame rate | Render thread, wait + submit |
|---|---|---|---|
| Robot Invasion (section 8.6, the same parts) | 21 → 9.6 ms | 36 → 72 fps | 15 → 7.2 ms |
| **2001** | **13.5 → 8.3 ms** | 67 → 72 fps | 12.1 → 6.4 ms |
| Last Action Hero | 8.4 → 9.1 ms (no gain; its mirrors draw few parts) | 72 fps | 2.7 → 2.8 ms |

Game of Thrones, the fourth of these tables, was not run; its extra mirror is a horizontal one on an upper playfield.

**Step B, parts beyond the cabinet out of the reflection** (box over 3000 VPU, or entirely beyond the cabinet):

| Table | Runtime GPU, before → after | Reflection pass | Triangles per frame |
|---|---|---|---|
| **WildWest** | **14.2 → 10.8 ms**, the same as its recipe (10.8 ms) | 3.90 → 1.43 ms | 1.65 → 1.07 M |
| **Maverick** | **16.8 ms, 36 fps → 11.8 ms, 72 fps** | 5.33 → 1.94 ms | 2.40 → 1.70 M |
| Masters of the Universe | 10.6 → 10.4 ms | 1.66 → 1.36 ms | 1.46 → 1.36 M |
| Ace of Speed | 11.3 → 11.3 ms | 2.11 → 1.83 ms | 0.82 → 0.78 M |

The rule read from the files also catches parts of the cabinet (`PinCab_Shooter001` on WildWest, `PinCab_Housing` on World Joker Tour), with rotations ignored; in the engine the real transforms are available, but the test still has to be better than this.

**Other parts out, on the tables where the room is not the cost** (reflection pass, bgfx; runtime readings on Darkest Dungeon vary too much to separate these):

| Parts out of the reflection | Darkest Dungeon (1.78 ms) | World Joker Tour (1.68 ms) |
|---|---|---|
| Small, box under 60 VPU | 1.82 ms (51 parts) | 1.65 ms (75 parts) |
| High, over 100 VPU above the playfield | 1.65 ms (30 parts) | 1.66 ms (66 parts) |
| Heavy, over 10 000 triangles | 1.30 ms (26 parts, the statues gone) | 1.67 ms (2 parts) |

None of these is worth a step: only the statues move the pass, by 0.5 ms, at a visible cost.

**Lower quality** (2026-10-30 executable, with today's post-processing and format through its hooks):

| | Darkest Dungeon, reflection pass / GPU busy per frame | World Joker Tour, reflection pass / GPU busy per frame |
|---|---|---|
| Base | 2.75 / 13.44 ms | 1.72 / 11.16 ms |
| Simplified meshes in the reflection at 8 times the error of the view | 1.91 / 12.34 ms | 1.62 / 11.04 ms |
| Reflection at half size | 2.04 / 12.90 ms | 1.24 / 10.37 ms |

The half size gives about 0.5–0.8 ms on both, against 0.19 and 0.48 ms on Addams and Ghostbusters (section 8.3), softer reflections, and nothing on the render thread (submission unchanged). The coarser meshes depend on the table.

**Reading.**

- Steps A and B replace the recipes in the measured cost: the same parts for Robot Invasion, and the same 10.8 ms for WildWest. They also find 2001 and Maverick, which have no recipe, and leave Last Action Hero, Masters of the Universe and Ace of Speed at about what they were: where these parts are cheap, removing them gains little, so they should only go on a table that needs it.
- A step can therefore be applied by need: Always Off / Always On / Automatic, as for the foveated anti-aliasing. Automatic has a cost the recipes do not have: a table runs below native until the decision, a minute with the warm-up of the anti-aliasing. Remembering the decision per table (with the rating, `table-stats.json`) would make the next starts right from the first frame.
- Darkest Dungeon and World Joker Tour gain from neither step. Their way is item 16, and the half-size reflection as a possible third step (not measured on today's executable; measured on 2026-11-02 and made a setting, next bullet). "Balls Only" stays the last resort, by the player's choice.
- **Half-size reflection on Darkest Dungeon, measured again 2026-11-02** (`Doc/test-hooks/probe-scale-hooks.py`, `VPX_PROBE_SCALE=0.5`: the probe 1080² per eye instead of 2160²; the executable of section 8.12 with the target at 90 % and the sensitivity at Medium; headless, eye tracking not active, two runs each, alternated, `~/perf-test/results-probe`). At fixed 2160²: runtime GPU time 11.41 and 11.48 → 10.98 and 10.97 ms, bgfx's 10.62 and 10.67 → 10.20 and 10.19 ms, GPU busy 83 → 79.5 %, readings over the period 1.40 and 1.51 → 0.66 and 0.63 per second, missed frames 0.12 and 0.13 → 0.02 and 0 per second; the render thread's submission unchanged. With dynamic resolution as the player has it: time at full size 96.1 and 94.9 → 98.2 and 99.2 %, missed frames 0.25 and 0.19 → 0.01 and 0.06 per second. The price is a softer playfield reflection, not yet looked at in the headset. Made the setting *Reflection size* (`Player.ReflectionSize`, Steam Frame screen, Reflections section), Half by default in the Frame build on the owner's choice, for all tables: the other tables are not measured with it.

Not seen worn: Maverick and 2001 with these parts out, and any table with the rule itself. If the rule is built, the recipes are not needed.

### 8.9 The Reflections settings (2026-11-01)

The owner's decision: settings rather than recipes, for the tables to come, both **Off by default**, not Automatic: a table with such parts loses them whether it needs the time or not. `Player.ExtraMirrors` and `Player.RoomReflection`
(Steam Frame > Reflections, On / Off / Automatic, live, per table or for all; Off in the Frame build, On in the others; `src/steamframe/Reflections.h`). The recipes `robot-invasion-wall-mirrors` and `wildwest-room-reflection` were tried on the Frame and never shipped.

- **Extra mirrors**: a planar reflection probe other than the playfield's, unless horizontal, is not rendered (`RenderProbe::Render`), and its parts show no reflection. Horizontal ones stay: on Game of Thrones it is the reflection of an upper playfield.
- **Room reflection**: a primitive whose box reaches more than 300 VPU beside the playfield, 600 behind it or 700 in front of it, once transformed, is not drawn in a reflection. Height is not looked at, so a topper or a backbox stays. Parts merged by a group (drawn from one buffer) and animated meshes are not concerned, as for the culling.
- **Automatic**: as for the foveated anti-aliasing, 10 seconds below full resolution in the last 60, the first minute of the table not counted. The extra mirrors first, then the room after 10 more such seconds; for the rest of the table, not remembered from one play to the next. The anti-aliasing waits for them, and starts its own minute after.

Measured headless, build of the branch with the test hooks, fixed 2160² with the GPU profiler, and dynamic resolution without it (`Doc/test-hooks/reflections-build.sh`, `~/perf-test/results-refl`):

| Table | On | Off (default) | Saved | With dynamic resolution, Off | Left out |
|---|---|---|---|---|---|
| Robot Invasion | 22.7 ms, 36 fps | **9.4 ms, 72 fps** | 13.4 ms | 2155² (1559² before) | 3 mirrors |
| 2001 | 14.5 ms, 67 fps | **8.3 ms, 72 fps** | 6.2 ms | 2160² | 2 mirrors |
| Maverick | 16.4 ms, 36 fps | **11.1 ms, 72 fps** | 5.2 ms | 2141² | 80 room parts |
| WildWest | 13.9 ms | **10.9 ms** | 3.0 ms | 2146² (2031² before) | 18 room parts |
| Game of Thrones | 10.1 ms | 9.6 ms (no pass moved: noise) | 0 | | Nothing: `UpperPF` drawn in both |
| Last Action Hero | | 8.3 ms | | | Its 2 back mirrors, and `BM_underPF` |
| Addams | | 8.2 ms | | | Nothing |
| Darkest Dungeon | | 12.7 ms | | | Nothing |
| World Joker Tour | | 11.0 ms | | | Nothing |

The engine's test, with the real transforms, is more exact than the one read from the files in section 8.8: on WildWest it no longer takes the cabinet's `PinCab_Shooter001`. On Last Action Hero it takes `BM_underPF`, under the playfield, which is clipped from the reflection anyway.

Automatic, with dynamic resolution, 170 s: Robot Invasion stays at the 1512² floor until the extra mirrors go, then 2160² (9.5 ms); WildWest at about 2000² until the room goes, then 2160² (10.8 ms). Each lost its parts in the second half of the run, as designed.

The menu section was checked in a capture on the Frame (`docshot-hooks.py`): Off and Off, "3 extra mirrors left out" and "no part of it reaches the reflections" on Robot Invasion. The owner checked this build in the headset on 2026-11-01: it works.

### 8.10 The threads on the cores (2026-11-01)

*How the study went matters here, as its first result does not apply to real play. Traces: `Doc/test-hooks/dr-trace-hooks.py` (one line per frame), `dr-trace-report.py`. Placement without a rebuild: `thread-placer.py`, `placement-report.py`. Results on the Frame: `~/perf-test/results-trace`, `results-trace2`, `results-place`, `results-worn`, `results-worn-b`, `results-placed`. Headless runs of 130 s, the window from 40 s, one run per variant unless said.*

**The cores of the Frame are not alike** (`lscpu`, `cpu_capacity`):

| Cores | Type | Maximum clock | Capacity | What else may run there |
|---|---|---|---|---|
| 0, 1 | Cortex-A520 | 2.27 GHz | 216 | Most system threads, the eye tracking |
| 2, 3, 4 | Cortex-A720 | 3.15 GHz | 855 | Most system threads; head tracking on 2 and 3; the compositor's render thread on 4 |
| 5, 6 | not named by `lscpu` | 2.96 GHz | 803 | The compositor's render thread |
| 7 | not named by `lscpu` | 3.05 GHz | 1024 | Threads with no limit only |

The scheduler is energy-aware and places threads within what each is allowed. Steam starts a game with an affinity of `f8`, cores 3 to 7; it is not a cpuset, and a thread may widen it. VPX can choose its cores but not its priority (`RLIMIT_RTPRIO` and `RLIMIT_NICE` at 0, no capability).

**The slow frames.** On Darkest Dungeon the size sawed between 1666² and 2026², 2 ms under the target (section 8.6). Started with all eight cores, the traces show: readings over the frame period 5.7–6.1 times per second, but only 0.13–0.35 frames missed per second; bgfx's GPU time on these frames stays at 8.7 ms against 8.1 ms when the runtime's counter reads 16 ms, so they are not slow on the GPU; about 80 % of the falls of the scale follow them. In these frames the frame preparation takes 6.2 ms against 3.2 ms, the render thread waits 8 ms against 4.5 ms, and the logic thread's CPU time grows as much as its wall time (+2.9 ms), with twice the involuntary context switches: it is running slower, not blocked.

**All eight cores against the fast ones, which does not apply to real play.** Started over ssh, Darkest Dungeon went from 1858² and 4 % of its time at full size to 2127² and 79 % with the threads kept on cores 2 to 7. A Steam launch never had the slow cores for its threads, which the first worn session revealed.

**Layouts, started over ssh, dynamic resolution:**

| Layout | Darkest Dungeon: size, at full size, slow readings/s | Robot Invasion with its mirrors: size, slow readings/s, missed/s |
|---|---|---|
| All eight cores | 1858², 4 %, 5.85 | 1571², 8.21, 0.91 |
| Cores 2 to 7 | 2127², 79 %, 0.99 | 1823², 6.29, 0.04 |
| Cores 5 to 7 | 2127², 82 %, 0.52 | |
| Logic and render on 5 to 7, the others on 2 to 4 | 2132², 84 %, 0.54 | 1908², 5.33, 0.02 |
| **Render on 7, logic on 5 and 6, the others on 2 to 4** | **2131², 84 %, 0.53** | **1918², 3.41, 0.04** |
| Logic on 7, render on 5 and 6, the others on 2 to 4 | 2127², 82 %, 0.65 | |

Limiting VPX to cores 2 to 4, shared with SteamVR, was worse than cores 2 to 7 (2.28 readings per second against 1.02).

**In the headset, Steam launch, Darkest Dungeon, one play each:**

| | As started by Steam (`f8`) | Render on 7, logic on 5 and 6, others on 2 to 4 (`thread-placer.py`) |
|---|---|---|
| **Time at full resolution (the table's rating)** | **1.9 %**, 156 s | **62.3 %**, 171 s |
| Size, mean / median (status lines, every 5 s) | 1562² / 1512² | 2071² / 2160² |
| Status lines under 1600², from 40 s | 88 % | 0 % |
| On time | 99.6 % | 99.6 % |
| GPU busy | 82 % | 79 % |
| Compositor on the GPU | 5.8 % | 6.3 % |
| Peak temperature, GPU / CPU | 78 / 94 °C | 88 / 96 °C |

The owner saw the difference. The GPU was as busy at 1512² as at 2160²: much of its busy time as started by Steam was not work on pixels, which the temperatures confirm. The compositor stays at 6 %, so the heavier state of section 8.6 was not at play. At 2160², the size still dips to about 2020–2080² every 20 seconds, for a reason not known.

**As built: `Player.ThreadPlacement`** (`src/steamframe/ThreadPlacement.h`), on in the Frame build, no menu entry. From the end of the table's load to its close, a thread gives the threads of the process their cores by name, 4 times per second, since most threads are not VPX's and a thread takes the cores of the one that starts it. While a table loads, the threads keep what Steam gave them, for the workers that decompress textures and simplify meshes; the lobby is left alone. It only applies on this core layout, read from `cpu_capacity`.

**Measured as built, headless, with Steam's cores (`taskset -c 3-7`), dynamic resolution:**

| Table | Off: size, at full size, slow readings/s, missed/s | On | Render thread submission, off → on |
|---|---|---|---|
| Darkest Dungeon (PuP) | 2130², 82 %, 0.62, 0.07 | 2138², 86 %, 0.41, 0.03 | 4.91 → 4.41 ms |
| Addams Family (PinMAME) | 2160², 100 %, 0, 0 | 2160², 100 %, 0, 0 | 1.34 → 1.39 ms |
| World Joker Tour (PinMAME) | 2160², 100 %, 0.01, 0 | 2160², 100 %, 0.01, 0 | 4.46 → 3.66 ms |
| Lord of the Rings (PinMAME) | 2160², 100 %, 0, 0 | 2160², 100 %, 0.08, 0 | 2.31 → 2.03 ms |
| The Matrix (PuP) | 2146², 91 %, 0.27, 0.09 | 2144², 90 %, 0.28, 0.10 | 3.04 → 2.53 ms |
| Legend of Zelda (PuP) | 2146², 92 %, 0.43, 0.15 | 2145², 93 %, 0.30, 0.15 | 4.37 → 3.87 ms |

The load does not change (Darkest Dungeon's simplified meshes done at 3.10 s, 3.14 s without; Addams 1.71 s, 1.76 s), nor the temperatures (Lord of the Rings 97 °C on the CPU both ways).

**In the headset, all the rated plays of Darkest Dungeon of 2026-11-01** (Steam launches by the owner; `vpinball.log`, sizes from all the status lines of each play). The first four plays ran the same build; the last one is the build with `Player.ThreadPlacement`, which adds only that:

| Started | Threads | Time at full resolution (rating, yellow) | Played | On time | Size, mean / median |
|---|---|---|---|---|---|
| 14:44 | As started by Steam | 19.4 % | 47 s | 99.3 % | 1972² / 1969² |
| 15:38 | As started by Steam | 6.5 % | 57 s | 99.8 % | 1760² / 1734² |
| 17:41 | As started by Steam | 1.9 % | 156 s | 99.6 % | 1577² / 1512² |
| 17:50 | Placed, by `thread-placer.py` | 62.3 % | 171 s | 99.6 % | 2074² / 2160² |
| 21:43 | **Placed, as built** | **62.5 %** | 188 s | 99.7 % | 2063² / 2160² |

**Other tables worn, as built:**

| Table | Rating, time at full resolution | Played | Size, mean / median |
|---|---|---|---|
| Addams Family (PinMAME) | Green, 99.3 % | 82 s | 2108² / 2160² |
| Addams Family, again | Green, 100 % | 45 s | 2101² / 2160² |
| Ace of Speed (PinMAME) | Yellow, 92.8 % | 108 s | 2140² / 2160² |

The log has no play of these two tables without the placement to compare with. They are listed to show that the PinMAME tables keep their resolution and their sound with PinMAME on cores 2 to 4: the owner heard no problem with the sound on these plays and on Darkest Dungeon.

**World Joker Tour worn:**

| Date, started | Threads | Time at full resolution (rating, yellow) | Played | GPU per million pixels | Readings held per status line |
|---|---|---|---|---|---|
| 10-31, 22:05 | As started by Steam, earlier build | 6.3 % | 110 s | 2.77 ms | 41 |
| 11-01, 15:40 | As started by Steam, the build of the plays above | 12.7 % | 51 s | 2.80 ms | 46 |
| 11-01, about 22:02 | **Placed, as built** | **33.0 %** | 138 s | 2.55 ms | 19 |

The figures of the last play's two right columns come from its last minute only, as `vpinball.log` was cut at 5 MB during the play. Its GPU time at 2160² is 10.3–11.3 ms against 9.7 ms headless, under the target of 11.8 ms, and the size still falls to 2012–2146² about every other status line.

**Reading.** On Darkest Dungeon the gain is real and repeatable: the two plays with the placement agree within 0.2 points, and are three times the best play without it. The lowest play without it is the longest, so the two short ones probably flatter the state without placement. The comparison was not randomized: the plays without placement all came first, and none was played with the final build, the setting being its only difference from the build of the others. Headless with Steam's cores the placement does no harm and gains little: the render thread's submission is 0.5 ms shorter and the logic thread has half the context switches. Headless and worn disagree: Darkest Dungeon holds 2130² headless and 1577² worn in its longest play with the same cores, so what the placement protects the threads from is what the headset adds (tracking, passthrough, eye tracking, a load of 35–45 % on all cores against 20 %). Headless runs do not reproduce this state, so they cannot rule on this setting. Darkest Dungeon stays yellow: green needs 95 % of the time at full resolution.

**Not settled.** Which conflict the placement removes. World Joker Tour rose less than Darkest Dungeon, on one play: at full size it is under the GPU target, and what keeps it down is not known. Long plays and heat: the longest play with it lasted 188 s, and temperatures were only recorded for the 17:41 and 17:50 plays. The 20-second dips. The sound has no measurement, only the owner's ears, since the log does not record dropouts. To compare again, `ThreadPlacement = 0` in the `[Player]` section turns it off.

**For the next headless runs:** start them with `taskset -c 3-7`, as Steam does.

### 8.11 PuP's decoders and the logic thread (2026-11-02)

*Timers per step and thread state of each frame preparation: `Doc/test-hooks/dr-prep-hooks.py`, `dr-prep-report.py`. Stacks of the threads at the moment of a sleep: `dr-sleep-hooks.py`, `dr-sleep-report.py`. Built with `DRTRACE=1 DRRULE=1 DRPREP=1 DRSLEEP=1 reflections-build.sh <worktree> hooks`. Results on the Frame: `~/perf-test/results-prep`, `results-sleep1`, `results-sleep`, `results-stopfix`. Headless runs of 130 s, the window from 40 s, Darkest Dungeon with Steam's cores (`taskset -c 3-7`). The timer and stack runs had a test rule of dynamic resolution on (`VPX_DR_RULE=1`, not in master), the comparison of the fix has master's.*

**What the slow preparations are.** Of the preparations more than 2.5 ms over the median, 84 % are followed within 4 frames by a reading over the frame period (81 % with all eight cores, one run each), and nearly all their excess is in `Renderer::Render3DScene`: +6.62 of +6.91 ms with Steam's cores. With Steam's cores, 59 slow preparations in 90 s, against 3.8 ms for the others:

| Kind | Count | Preparation | CPU time | What happens |
|---|---|---|---|---|
| **Goes to sleep once** | 20 | **17.0 ms** | 4.7 ms | Asleep about 12 ms; the other frames never sleep |
| Taken off its core | 21 | 7.8 ms | 7.2 ms | About 2 interruptions |
| Neither | 18 | 7.0 ms | 7.0 ms | Runs longer with no switch; not explained |

With all eight cores there are 802 in 90 s, of which 769 are taken off their core about 8 times: this is what the placement of section 8.10 removes. The sleeps stay.

**Where it sleeps.** `perf record -e context-switches` cannot show it without root: on the Frame (`perf_event_paranoid` 2) it records no sample. A temporary thread in VPX therefore watches the logic thread during `Render3DScene`, and once it has found it asleep twice in a row, signals the threads, whose handlers take a backtrace. In two runs, 40 of the 41 sleeps caught are the same stack:

```
PUPMediaPlayer::Render          waits for the player's m_mutex
PUPScreen::Render
PUPManager::Render
Flasher::RenderCabinetDisplay
Flasher::Render
Renderer::RenderDynamics
Renderer::Render3DScene
```

A PuP command thread was running each time. Sampled every 1.5 ms during the wait, 34 of 43 samples of the PuP threads are in `Play` → `StopBlocking` → `avcodec_free_context`, closing the V4L2 decoder (`close`, buffers released by `ioctl` and `munmap`); 8 are opening the next decoder, which is outside the lock. Each new video on a screen stops the previous one, and `StopBlocking` closed its decoder holding `m_mutex`. The step took 8 to 23 ms in these frames (medians 14.2 and 12.1 ms, against 3.0 ms), about once every 4 to 5 s. The render thread is not involved: it is waiting for the logic thread. The plugin's prepare frame message, measured separately, costs nothing: the wait comes later, when the scene draws the PuP screens.

Videos of at least 640×360 open a hardware decoder (`OpenStream`), so tables with PuP videos of that size can have this wait. Tables without PuP cannot: this does not explain World Joker Tour (section 8.10), which is a PinMAME table.

**The fix** (`PUPMediaPlayer::StopBlocking`, in master 0659ce1e7). The format and codec contexts are swapped out under the lock and closed after it, as the loop path of `Run` already did. While they close, `Render` draws the held frame, as it does once `StopBlocking` has returned.

**Headless, the same rule of dynamic resolution as master, runs alternated:**

| | Before | Before | Fix | Fix |
|---|---|---|---|---|
| Size | 2099² | 2094² | 2102² | 2110² |
| Time at full size | 54.6 % | 54.9 % | 46.5 % | 55.1 % |
| Readings over the period per second | 1.04 | 1.01 | 1.20 | 1.09 |
| Missed per second | 0.07 | 0.17 | 0.04 | 0.05 |
| On time | 99.90 % | 99.76 % | 99.94 % | 99.93 % |
| Slow preparations per second | 0.54 | 0.68 | 0.50 | 0.40 |
| Mean slow preparation | 9.8 ms | 10.5 ms | 8.1 ms | 8.4 ms |
| Preparations asleep over 2 ms | 18 | 24 | 12 | 1 |

A report script ran on the Frame for a moment during the second run with the fix. A third run with the fix, with the sleep trace on, caught no wait on this lock in 5 sleeps: 4 are the "Controller models" line that `VRControllerModels::Render` logs every 5 s, waiting for the disk, and 1 was not asleep by the time of the backtrace. It is left out of the table, as it has the trace and a blocked decoder (below): 2113², 55.6 %, 0.92, 0.01.

Before the fix, 8 of the 23 missed frames came within 70 ms after a sleep, and about a quarter of the readings over the period. The sleeps were 0.2 to 0.27 per second against 1 reading over the period per second, so they cannot be most of them.

**In the headset, Darkest Dungeon, Steam launch, with the placement of section 8.10:**

| Started | Build | Time at full resolution (rating, yellow) | Played | On time | Size, mean / median |
|---|---|---|---|---|---|
| 11-01, 17:50 | Before, placed by `thread-placer.py` | 62.3 % | 171 s | 99.6 % | 2074² / 2160² |
| 11-01, 21:43 | Before, placed as built | 62.5 % | 188 s | 99.7 % | 2063² / 2160² |
| 11-02, 16:41 | **Fix** | **74.7 %** | **111 s** | **99.8 %** | **2089² / 2160²** |

Eye-tracked, no blocked decoder. Of the 23 status lines after the load, 18 are at 2160²; 4 of the 5 others are in the first 45 s. The executable is the one of the 21:43 play: only `plugin-pup.so` differs.

**Reading.** The wait is removed: that is measured, and the stack leaves no doubt about the cause. Missed frames look lower, on two runs each. Headless, the time at full size does not move, as the readings over the period stay at about 1 per second: this fix alone does not end item 16. Worn, the gain of 12 points is more than the 0.2 points between the two plays before, and more than the headless runs predict, for a reason not known. It is a single play, 60 to 77 s shorter than those it is compared with, a day later and not randomized, so it is not yet a measure of the gain. Darkest Dungeon stays yellow: green needs 95 %.

**Not settled.**

- A play of about 3 minutes, to compare with the two before.
- The look: whether videos change cleanly has not been checked by the log or the headless runs.
- The other PuP tables, The Matrix and Legend of Zelda, are not measured with the fix.
- The first run with the fix still has 12 preparations asleep over 2 ms, and no stacks: their cause is not known. The kinds "taken off its core" and "neither" remain.
- The third run with the fix logged a hardware decoder blocked for 2 s, recovered in software (`file=BG-Overlay.mp4`). This also happens without the fix (2 other logs of the 390 in `~/perf-test`, and The Matrix about every other run, according to the commit that added the recovery). The fix does not change the order of the work on the command thread, and that run was the only one of the five whose hook signalled the PuP threads, which later hung a worn play with such a thread spinning in FFmpeg's `v4l2_dequeue_v4l2buf` (a signal leaves `errno` at `EINTR`, and its `poll` with no timeout then loops forever). So the hook is the likely cause here too, not proven.
- The "Controller models" log line, on the logic thread every 5 s. Headless its output goes to a file; a Steam launch was not measured.
- These headless runs cannot be compared with those of 2026-11-01: since the Frame was restarted, the log says "Fixed: eye tracking not active" headless instead of eye-tracked, and GPU time at full size is 11.45 ms against 10.8 ms, at the same clock. The time at full size is 55 % against 85 %. The link between the two is likely, not proven. The worn play was eye-tracked.

### 8.12 The sensitivity of dynamic resolution (2026-11-02)

*Traces: `Doc/test-hooks/dr-trace-hooks.py`, `dr-rule-report.py`, `dr-replay.py`. Results on the Frame: `~/perf-test/results-dr` (the first rule, 2026-11-01), `results-sens`. Headless runs of 130 s, the window from 40 s, with Steam's cores (`taskset -c 3-7`), the placement of section 8.10 and the PuP fix of section 8.11; one run per level, two on Darkest Dungeon.*

**Why.** A reading over the whole frame period lowered the scale at once and blocked any rise for a quarter of a second. On Darkest Dungeon most of these readings are not GPU work: bgfx's GPU time stays the same on those frames, and they follow a late frame preparation on the logic thread (sections 8.10, 8.11). In a worn play of 105 s (2026-11-02, test build), the scale dipped 51 times, for 0.5 s each (median) and down to 1966², with 47 of the dips started by one such reading, while frames were on time (0.07 missed per second) and the GPU time at full size was 11.3 ms against the target of 11.8 ms. The table sat at full size 68 % of the time.

**The setting.** *Dynamic resolution sensitivity* (`PlayerVR/DynamicResolutionSensitivity`, Steam Frame screen and VR settings page, live): how many readings over the period, close together, it takes for them to count.

| Level | Readings over the period that count | |
|---|---|---|
| Low | Each one | The behaviour before |
| **Medium** (default) | 2 within 3 readings | The rule measured on 2026-11-01 |
| High | 3 within 5 readings | |

The others are left out, of the average too. The status line in the log counts the readings left out. Until the evening of 2026-11-02 a frame that missed its display always counted; since then a missed frame is a slow reading like one over the period (below).

**Measured.**

| Table | Low | Medium | High |
|---|---|---|---|
| Darkest Dungeon: size, time at full size | 2121², 59.5 and 55.6 % | 2135² and 2137², 62.2 and 69.1 % | 2139² and 2140², 70.4 and 74.0 % |
| Darkest Dungeon: missed frames per second, on time | 0, 100 % | 0.04 and 0.02, 99.94 and 99.97 % | 0.05 and 0.03, 99.93 and 99.96 % |
| Robot Invasion with its mirrors (at its limit): size, missed per second | 1870², 0.06 | 1867², 0.14 | 1885², 0.13 |
| Legend of Zelda: time at full size, missed per second | 96.7 %, 0 | 99.1 %, 0 | 100 %, 0.01 |

Darkest Dungeon at Medium with the target at 90 % instead of 85 %: 2156², 96.4 % of the time at full size, 0.21 missed frames per second (99.71 % on time), one run.

**Reading.** Each level gains on the table it was made for, and costs a few missed frames. On Darkest Dungeon headless, Medium gains 8 points and High 15, for 0.03–0.04 missed frames per second, about one every 30 seconds. On Robot Invasion, which cannot hold its full size, nothing is gained and the missed frames double, to about one every 7 seconds: where a table is really too heavy, the left-out readings were real. The 2026-11-01 runs of the first rule (the same as Medium) found the same on Robot Invasion (0.15 → 0.51 missed frames per second), and less gain than here on Darkest Dungeon, where their runs at Low already reached 85 %: those runs were eye-tracked and these are not, probably the reason (section 8.11). Worn, a replay of the play above gives 67 % at Low and 84 % with the readings left out, and with the target at 90 %, 78 and 93 %; the replay does not model the frames that would then miss. Medium is the default, as the owner chose.

**Worn, Darkest Dungeon, one play each (2026-11-02, Steam launches by the owner, eye-tracked, the PuP fix in):**

| Sensitivity | Time at full resolution (rating, yellow) | Played | On time | Mean size, status lines from the third | Lines at 2160² | Missed per second | Readings left out | GPU at 2160² |
|---|---|---|---|---|---|---|---|---|
| Low (16:41) | 74.7 % | 111 s | 99.8 % | 2109² | 17 of 22 | 0.15 | 0 | 11.05 ms |
| Medium (19:43) | 48.0 % | 86 s | 99.7 % | 2139² | 10 of 18 | 0.26 | 77 | 11.39 ms |
| Medium, target 90 % (19:48) | **87.7 %** | 124 s | 99.4 % | 2143² | 20 of 26 | **0.42** | 121 | 11.55 ms |

At Medium the deep dips are gone (lowest line 2074², against 1740², 1904² and 1967² at Low) and the mean size is higher, but for its first 30 s the table sat at 2074–2120², 96–98 %, with the GPU at 11.5–11.9 ms, at the target: the averaged rule, which the sensitivity does not change, held it just under the full size, and the rating counts only the full size. The GPU time at 2160² was 0.34 ms higher than in the play at Low, for a reason not known (another part of the game, the view). Missed frames rose from 0.15 to 0.26 per second. With the target at 90 % (11.5–12.0 ms at 2160²; the first three status lines, during the load, still at 85 %), the table held 2160² in 20 of 26 lines and never went under 2066², the best rating of the day, for 0.42 missed frames per second, one every 2.4 s. One play each, not randomized: they do not settle the defaults, and whether the missed frames show is the player's call. After these plays the owner set the default target to 90 % for all tables (7c44b22e7), then back to 85 % the same evening, once the runs below showed its cost on Robot Invasion: 90 % stays a per-table setting.

**A play before the restart, and the log writer (2026-11-02, evening).** The build with the 90 % default, the half reflection and an asynchronous log writer (the log file and console written by a `LogWriter` thread, `src/utils/Logger.cpp`, so that a line logged while a frame is prepared no longer waits for the disk: the "Controller models" line every 5 s had been caught 4 times in 90 s in `VRControllerModels::Render`) played worse, not better: Darkest Dungeon 15.2 % at full resolution over 113 s, the owner felt no difference, 45–140 readings held per status line all play (10–30 before), no stall break, GPU time per pixel about 19 % higher. No sampler ran, so the cause is not known. Found afterwards: `softapmanager`, a system service (root, cores 1–4), had spun one thread at 94 % CPU since the Frame started; after a restart it stayed at 0 %, and the next play is the one below. The log writer kept the logs complete and in order in 8 runs; its effect on the dips was not measured apart.

**Missed frames under the sensitivity (2026-11-02, evening).** Worn after a restart of the Frame, Darkest Dungeon at Medium and 90 %, with the half reflection then probably the full one from about 21:21:35 (GPU time at 2160² up by about 0.45 ms there; the owner played both, "a lot smoother both at half and full"), held 2160² with the GPU 80–84 % busy at 903 MHz and the compositor at 6–7 %, rated 90.3 % over 137 s, yet still dipped (2108², 2155², 1915²) with 1 to 4 missed frames and up to 47 readings held per 5 s: frames handed over late, which a missed frame then turned into a dip at once. Missed frames now follow the sensitivity like readings over the period. Headless, at Medium and 90 %, two runs each alternated with master (`~/perf-test/results-missed`):

| | Master: size, missed per second, on time | Missed frames under the sensitivity |
|---|---|---|
| Robot Invasion with its mirrors | 2013² and 2018², 0.91 and 1.08, 98.73 and 98.50 % | 1991² and 2003², 0.99 and 1.09, 98.62 and 98.49 % |
| Darkest Dungeon | 99.4 and 98.4 % at full size, 0.03 and 0.03, 99.96 % | 99.4 and 99.4 %, 0.07 and 0.01, 99.90 and 99.99 % |

No difference beyond the noise headless, where Darkest Dungeon hardly misses a frame: the change is for the late frames of worn play, not measured yet. These runs show another cost: at the 90 % target, Robot Invasion with its mirrors misses about one frame per second (98.5–98.7 % on time), against 0.14 per second at 85 % (the runs above): on a table that cannot hold its full size, 90 % runs the GPU at its limit. The default went back to 85 % after them.

**Not settled.** One worn play per level. One run per level per table, two on Darkest Dungeon: the two runs at Medium differ by 7 points. Robot Invasion's missed frames at Medium and High come from one run each. The target of 90 % with Medium gains most on Darkest Dungeon and misses a frame every 5 seconds headless; worn, two plays on that table only (87.7 and 90.3 %), none on other tables. Missed frames under the sensitivity with the 85 % default (fork master 57f88f23d) are on the dev slot since 2026-11-03, not played yet.

### 8.13 What the lights cost on a table baked with light maps (2026-11-03)

Medieval Madness (VPW 1.0.1) holds its full size in attract mode (2160², GPU 10.4–11.3 ms worn) and drops to 75–92 % as soon as a game starts and
its lights turn on (GPU held at about 11.5 ms, roughly 3–4 ms more at full size). A light map whose light is off is not drawn at all
(`Primitive::Render` skips an additive part with nothing to add), so the cost follows the lights.

Headless series on the Frame (fixed 2160², bgfx's GPU time per target, cores 3–7, `Doc/test-hooks/lightcost-*`; temporary switches that leave
out each kind of part, or force each family of light maps on at full brightness):

| Run (game window) | GPU (bgfx) | Light-map triangles drawn |
| --- | --- | --- |
| As it is (about 320 maps lit in an autoplayed game) | 8.2–8.5 ms | 520 k |
| No additive part drawn | 6.05 ms | 0 |
| All 726 maps of the view forced on | 11.2 ms | 1.1 M |
| All on, at 70 % of the size (1512²) | +3.7 ms over none at that size | 1.1 M |

- The light maps cost up to 5.2 ms, and half of that does not follow the pixels: halving the pixels removed only 1.3 ms of it. Dynamic
  resolution only scales the pixels, which is why it has to drop far to make up for the lights. By family at full brightness: flashers
  (`LM_FL`, 166 draws, 405 k triangles) 1.65 ms, general illumination 1.4 ms, inserts 1.0 ms, bumpers 0.46 ms: about 4 µs per thousand
  light-map triangles, the number of draws itself negligible. The `Light` and `Flasher` parts, the ball-shadow technique (0.2 ms) and the
  reflection (no light map is reflected) are not the cost; light maps under 2 % of their strength are too few to matter.
- The simplified levels stopped at a floor (1.27 M triangles at 1 VPU, still 779 k at 16 VPU): the places where the light maps on a part
  differ were locked, and `BM_Parts` alone carries 84 light maps. 90 % of the 44 k locked places were edges of light maps, not seams of their
  textures. Seams that may get shorter (`meshopt_SimplifyVertex_Protect` on welded meshes, plain seams otherwise) keep every light map
  consistent with its part and give 1.18 M at 1 VPU and 463 k at 16 VPU; all maps on: 11.31 → 10.21 ms (light-map triangles at the lockbar
  1,107 k → 1,021 k), an autoplayed game about −0.3 ms. On the Mac, all maps on at 4 px of allowed error, against the full meshes: 2,390
  pixels off by more than 32 levels, against 1,985 with the locks.
- Light maps that fade out past their own triangles, their edges free and their coverage an attribute of the simplifier, were built and
  measured too (kept aside, not merged): they did not draw fewer light-map triangles near the lockbar without leaking light (1,156 k
  at the weight that kept the image, 965 k with twice the strong differences, 861 k with light visibly leaking). The rest of the cost is
  the light maps themselves: about a million triangles and their pixels when the table is fully lit.
- Allowed error 2 px: no change; 4 px: −1 ms.

### 8.14 Light-map pruning (2026-11-04)

Most light-map triangles add almost nothing: the far edges of a light, the walls it barely reaches. On Medieval Madness a third of them
never add more than 0.002 (linear, at full strength) and carry no measurable light; the inserts are the extreme (57 % under 0.002). A
light map cannot simply be replaced by a classic light (110 of its 112 lights are hidden drivers of light maps, and GI or flashers on the
parts have no classic equivalent), so the triangles that add too little are left out instead.

- **Test.** The light of a triangle is the brightest texel within one texel of it in the light map's image (max pyramid, at the level
  where the triangle spans 64 texels at most), times the brightest channel of the part's color and its opacity. Conservative: a triangle
  touching one bright texel is kept. It runs after the simplification: pruning first cut new edges into the light maps, which the joint
  simplification with their part had to keep (12 % fewer triangles at 1 VPU instead of 21 %, more triangles at 16 VPU).
- **Levels.** Light-map triangles left out on Medieval Madness (all levels of all maps): 0.002 22 %, 0.005 29 %, 0.01 35 %, 0.02 43 %,
  0.03 49 %, 0.05 56 %, 0.08 62 %, 0.12 67 %. Weighted by the maps lit in a recorded game (the GI families are 70 % of the lit triangles
  in play and lose the least), with the cost of 8.13 taken as proportional to the triangles: about 0.4 / 0.54 / 0.69 / 0.87 / 1.0 / 1.18 /
  1.35 / 1.48 ms in play, 1.1 to 3.5 ms with every light on (estimates). Measured on the Frame (headless, fixed 2160², game window,
  bgfx's GPU time): Off 8.87 ms, Medium 7.82 / 8.08, High 7.82, no light map at all 6.61; every map forced on, Off 11.60 → Medium 9.92.
  Medium saves 0.9 ms in play and 1.7 ms with every light on; High adds only 0.1 ms more, within the noise of two runs (0.26 ms).
  With Medium, 4 px of allowed mesh error everywhere saves 0.28 ms only, so coarser meshes outside the gaze would gain 0.1–0.2 ms on this table.
- **Image.** Mac captures at 2880×1620 of a real game (autoplay, paused, the same frozen frame at every level): up to 0.01 the loss is
  under 9/255 and only on the dark inner walls of the cabinet; from 0.03 the faint glow on those walls and around the castle ends along
  hard triangle edges (27–43/255). The playfield, the inserts and the toys do not change at any level. Pruning also removes a few bright
  single pixels that bled from neighbouring light maps in the shared atlases.
- **Setting.** `Player.LightMapPruning`: Off, Low (0.02), Medium (0.05), High (0.08), switched live (Steam Frame screen, Light maps;
  `src/steamframe/LightMapPruning.h`). While the table loads, the triangles of each light map and of each of its levels are sorted from the
  brightest down, so a level is only a shorter draw. The sort decodes the light-map images again (4.9 s for the 7 atlases of Medieval
  Madness on a Mac, 3 at a time); Off when the table loads, nothing is sorted. In the headset (2026-11-04) Medium and High made Medieval Madness clearly crisper
  for little visible loss: Medium by default in the Frame build.

### 8.15 Copies of the scene for refraction (2026-11-04)

On Medieval Madness the runtime's GPU counter read about 4 ms more than bgfx's GPU time in every run, whatever was drawn (12.07 against
7.82 ms with light-map pruning on Medium), while the GPU's busy time (DRM `fdinfo`) matched the counter: real work, not idle time inside
the frame, and it shrank with the pixels (3.9 ms at 2160², 2.1 ms at 1512²). Turnip's own trace of the render stages
(`MESA_GPU_TRACES=print`, `MESA_GPU_TRACEFILE=<file>`; run `utrace` of `Doc/test-hooks/lightcost-series.sh`) showed where: 14 copies of
2160² images per frame, about 0.28 ms each, outside the views bgfx times.

- Three pairs of color and depth copies of both eyes: the three screen space transparency probes of the table (`Layer3`, `Layer4` and
  `LayerCaps Refractions`). `RenderProbe::RenderScreenSpaceTransparency` copied the whole render target for each, while their parts cover
  8.1, 5.3 and 0.8 % of the desktop view: 3.4 ms per frame.
- The depth of both eyes copied into the depth swapchain of OpenXR before the last pass: 0.6 ms. Left as it is.
- Every pass ran in system memory (Turnip's autotune), so the time is in the copies themselves, not in tile loads and stores.

`RenderProbe::LimitCopyToAreaOfInterest` now limits the copy to the area of interest of the parts using the probe (their bounds on screen
for both eyes, gathered while they draw, applied when the frame's area of interest is), with a margin of 0.1 in clip space for the
refraction offset and the blur kernel; the rest of the copy keeps an earlier frame, which nothing samples. bgfx renderer only, not guarded
(a candidate for upstream). On the Mac, with the light maps forced on so that the frames repeat, the image is the same as with the whole
copy (largest difference 3/255, on one pixel). On the Frame, headless at 2160², pruning on Medium:

| Game window | Whole copies | Copies of the area of interest |
| --- | --- | --- |
| Runtime's GPU counter | 11.86 / 12.07 / 12.22 ms | 9.16 / 9.07 / 9.17 ms |
| Every light map forced on | 14.31 ms, 12 frames missed per 5 s | 10.31 ms, none missed |
| Copies per frame (Turnip's trace) | 4.0 ms | 0.93 ms |

Medieval Madness then sits 2.7 ms under the target of dynamic resolution (11.8 ms at 72 Hz) in play, and under it with every light on.
The refractive ramps looked as before in the headset (owner, 2026-11-04). How it works and a draft of the upstream pull request:
`docs/steam-frame/Refraction Copies Limited to Their Area.md`.

## 9. Reproduction

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

The series of section 8.3: `python3 ../Doc/test-hooks/gpu-series-hooks.py apply` on top of `perf-hooks.py`, the executable in
`~/perf-test/vpx-gpu`, then on the Frame `sh gpu-series.sh results-gpu ad:base ad:fovpost ... ad:base+2` (the runs are listed in the
script, `Doc/test-hooks/gpu-series.sh`; it waits while another VPX runs). Section 8.6: runs `hbase`, `h1728`, `h1296`, `hnoprobe`, `hnobloom`, `hnosimp` of the same script, `variant-series.sh` for
hard links with a sidecar in `~/perf-test/tables`, `gpu-vs-busy.py` for the comparison with the GPU's busy time, `library-gpu-survey.py` for the mirrors and triangles of the library. These runs play the real tables: they
change saved credits and scores (`user/VPReg.ini`, PinMAME `nvram`), so back those up first.

Without `VPX_PERF_FORCE_RENDER`, someone has to wear the headset. Other variants: `native` (2160² without the bgfx profiler), and a
table option changed by a `<table>.vbs` next to a hard link of the table (section 7.2). Keep the idle gaps between runs at a minute or
less: the Frame goes to sleep when no VPX has run for a few minutes, which ends the series (`systemd-inhibit` needs an interactive login).

The results are on the Frame: `~/perf-test/results` (mirror off: Ghostbusters by resolution, VR room and stall runs, Addams by
resolution and the 15-minute run `-default-hot15`), `~/perf-test/results-prev` (the Ghostbusters default and fixed2160 runs of section 7.1),
`~/perf-test/results-gpu` (section 8.3), `~/perf-test/results-guard` and `~/perf-test/results-break` (section 7.5), `~/perf-test/results-nodesk` and `~/perf-test/results-fix` (section 7.6), and `~/perf-test/results-before` (mirror on).
