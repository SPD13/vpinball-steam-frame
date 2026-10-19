# Performance on the Steam Frame: where the frame time goes, and what to do about it

*2026-10-19, fork `master` at 53c794380 (upstream sync of 2026-10-18). Measurements on the Steam Frame (Adreno, Turnip, SteamVR 2.17) at
72 Hz (13.9 ms per frame), Standard FXAA, specular AA, foveated rendering, two tables: the Ghostbusters VR room (light, no ROM) and the
Addams Family VR room (heavy, PinMAME WPC). Updated 2026-10-20 with the measurements made after the desktop mirror was turned off
(section 7).*

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

Ranked by gain for the effort; items 8 and 9 come from the measurements of section 7. Gains are per frame at 72 Hz; on Addams, a
millisecond saved lets dynamic resolution raise the size by about 4–5 % per axis.

| # | Opportunity | Gain | Effort | Status |
|---|---|---|---|---|
| 1 | Desktop mirror off on the Frame | 4.5–6.6 ms on Addams, 4.4–6.9 ms on Ghostbusters | Forced off in the Frame build | **Done** (sections 6, 7.1) |
| 2 | Lighter playfield reflection on heavy tables | 1.1–1.3 ms on Addams, 1.0–1.2 ms on Ghostbusters | Per-table setting today (`PFReflection = 3`, "Static & Balls"); engine options below | To evaluate |
| 3 | Fuse FXAA into the tonemap pass | Part of 1.1–3.2 ms (one full-resolution write and read less) | Shader and pass change | Idea (*Image Quality*, section 4) |
| 4 | Reduce the scene pass's fixed geometry cost | Up to 3.8 ms on Addams | Investigation, engine work | Measured 2026-10-25: 2.2 ms per million triangles; simplification without visible loss 1.2–1.6 ms on Addams, culling 0.6 ms on LOTR (`Table Geometry on the Steam Frame.md`); culling done (`Culling Parts Outside the View.md`) |
| 5 | Overlap frame preparation with submission | 1.1–1.6 ms of render thread time (mirror off); frame rate at 90 Hz | Threading change | Idea |
| 6 | Reduce the render thread's submission cost | 4–6 ms of CPU per frame on Ghostbusters | Investigation | Counted (section 7.3) |
| 7 | Dynamic resolution floor and target per table | Steadier frame rate on heavier tables | Settings | Available now |
| 8 | Remove the frame pacing stall | Avoids a fall to the floor resolution; 2 ms of render thread per frame | Desktop window not drawn in the Frame build; guard in `VRDevice` | **Done** (sections 7.5, 7.6) |
| 9 | Cheaper VR room on pixel-bound tables | 2.1–2.3 ms on Ghostbusters with the room off (1.95 ms, 2026-10-23) | Engine work | Investigated: 3 options dropped, 3 open (`VR Rooms on the Steam Frame.md`) |

Details:

1. **Desktop mirror**: see sections 6 and 7.1.
2. **Playfield reflection**: the probe renders the reflected scene every frame (1.0–1.3 ms with the mirror off). Today,
   "Static & Balls" for a heavy table removes most of it at the cost of the dynamic reflections. Engine options: render the probe at a
   lower resolution, update it every other frame, or leave the VR room out of the reflection. Each needs a visual check in the headset.
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

For sharpness (*Image Quality on the Steam Frame.md*): with the mirror off, heavy tables like Addams reach about the native 2160², and light
tables like Ghostbusters reach the 2160² ceiling with about 1.7 ms to spare, so a *Render resolution* slightly above 125 % may fit them
(not measured). Beyond that, the GPU time freed by opportunities 2 to 4 and 9 is what dynamic resolution can turn into resolution, or
into headroom for the heat.

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

Without `VPX_PERF_FORCE_RENDER`, someone has to wear the headset. Other variants: `native` (2160² without the bgfx profiler), and a
table option changed by a `<table>.vbs` next to a hard link of the table (section 7.2). Keep the idle gaps between runs at a minute or
less: the Frame goes to sleep when no VPX has run for a few minutes, which ends the series (`systemd-inhibit` needs an interactive login).

The results are on the Frame: `~/perf-test/results` (mirror off: Ghostbusters by resolution, VR room and stall runs, Addams by
resolution and the 15-minute run `-default-hot15`), `~/perf-test/results-prev` (the Ghostbusters default and fixed2160 runs of section 7.1),
`~/perf-test/results-guard` and `~/perf-test/results-break` (section 7.5), `~/perf-test/results-nodesk` and `~/perf-test/results-fix` (section 7.6), and `~/perf-test/results-before` (mirror on).
