# The xr-sim branch: VR footage made on a Mac

This branch (`xr-sim` on the fork) holds the tools used to film the VR version of the Steam Frame Edition without a headset: the VR
trailer and the video tutorials (`docs/tutorials` on master). It is **never merged into master**: it is master plus a few commits, brought
up to date when footage is needed. This page says what is on it, how to keep it current, how to build it, and how the tutorial tools
outside of the repository use it.

## What the branch adds to master

| Commit | What |
|---|---|
| Steam Frame: simulated OpenXR runtime for the macOS build | `src/steamframe/xrsim/`: an OpenXR runtime linked into the app, a headset and two Steam Frame controllers whose poses and inputs come from a script. See [XR simulator](XR%20simulator.md). |
| XR simulator: scripts run every frame and in time order, waitfile, ... | The script runner called from `Player::PrepareFrame`, `waitfile`, `autoflip`, the controller models, the bgfx Metal patch (`steamframe/patches/bgfx-macos/`). |
| XR simulator: a new player is recognized ... | Fix of the scenes when a table is played after another. |
| XR simulator: frame recorder and balls script command | The **frame recorder** (below) and the `balls` command. |

Everything is compiled only with `-DVPX_XR_SIM=ON` (macOS, BGFX), except the frame recorder, which is not guarded (`TEMP-TRAILER` in
`src/renderer/RenderDevice.cpp`): another reason to keep this branch apart from master.

### Hooks in upstream files

`VRDevice.cpp` (include, Metal renderer, `XR_KHR_metal_enable`, backend), `Renderer.cpp/.h` (`RenderXRSimMirror` at the end of
`Render3DScene`), `player.cpp` (`XRSim::UpdateScript` in `PrepareFrame`), `RenderDevice.cpp` (the recorder: a block in
`OnScreenshotCaptured` and the screenshot request in `BGFXOpenXRRenderLoop`). The fork files touched: `steamframe/steamframe.cmake`,
`src/steamframe/vrui/Panel.cpp` (`XRSim::SetUIPanel`). These are where a rebase can conflict.

### The frame recorder

With `VPX_REC_FIFO=<fifo>`, every `VPX_REC_EVERY`-th frame (default 1) of the OpenXR render loop is captured with a bgfx screenshot of
the desktop window (the left eye, UI panel included, see `XRSimMirror.cpp`), and a thread writes it as a BMP to the FIFO. ffmpeg reads the
FIFO and encodes the video. `VPX_REC_DOWNSCALE` scales sources whose size is a multiple of 3 by 2/3. Frames are dropped when 3 are
waiting.

Every 300 frames the log gets `TRAILER-REC frame N dropped D WxH epoch <ms>`: the epoch of frame 1 is time 0 of the video, and the script
marks (`XRSIM-MARK <epoch ms> <label>`) are placed on the video with it.

## Keeping the branch current

Footage shows the menus and the web pages of the branch, not master's. Bring it up to date before filming anything that changed on
master (in October 2026, the names tutorial needed the new thumbnail dialog of master: it was copied into the built app by hand).

The weekly syncs rewrite master (its fixups are squashed), so the branch is **rebased**, not merged:

```sh
cd vpinball-xrsim                      # worktree of the branch
git fetch fork
git rebase fork/master                 # the commits of the branch that master already has are dropped
# conflicts: the hooks above; RenderDevice.cpp and player.cpp are CRLF, keep their line endings
git push --force-with-lease fork xr-sim  # ask the owner first: it rewrites the branch on the fork
```

Then rebuild. If upstream changed the OpenXR backend interface of `VRDevice`, `XRSimBackend.h` no longer compiles: that is found here,
since no other build compiles the simulator.

## Build

```sh
cmake -S . -B build -DPLATFORM=macos -DARCH=arm64 -DRENDERER=BGFX -DCMAKE_BUILD_TYPE=Release -DVPX_XR_SIM=ON
cmake --build build --target vpinball
```

- Only the `vpinball` target is built. The plugins and `Resources/scripts` of the app were cloned from a normal macOS build of master
  (`cp -c`).
- The bgfx Metal library must be built with the patch of `steamframe/patches/bgfx-macos/` (stereo shaders writing the layer index into a
  single layer target), then copied to `third-party/runtime-libs` and into the app's `Frameworks`. Remove the old file before copying: a
  library overwritten in place while loaded makes macOS kill the app without a message.
- Web pages and other assets are copied into the app at build time: after an asset-only change, copy the files into
  `VPinballX_BGFX.app/Contents/Resources/assets/` (or relink).

## The tools outside the repository

The scripts that drive the simulator and make the videos run from the workspace, next to the checkouts. A copy is kept in the Doc repo
(`vp_vr-steam-frame-docs`, `video-tools/`, refreshed by its `export.sh`) with the settings each tutorial starts from and the list of its
tables and ROMs:

| Folder | What |
|---|---|
| `vp_vr/trailer-vr/tools/` | Shared: `cdp.py` (headless Chrome driven over DevTools: screencast, cursor, clicks, typing), `gfx.py`, `segments_v1.py` (compositor basics), `music.py` (backing track), `obj2glb.py` (Frame controller models to glTF), `rec-hooks.py` (applied the recorder before it was committed). `trailer-vr/controllers/` has the models, `trailer-vr/venv/` the Python (Pillow, numpy, websocket-client). |
| `vp_vr/tutorials/01-tables-roms/tools/tutorial.py` | The segments of the tutorials: `VRSeg`, `WebSeg`, `Card`, `StepBadge`, subtitles, zooms. Every tutorial's compositor imports it. |
| `vp_vr/tutorials/04-controls/tools/blender/render.py` | Close-ups of the controller models rendered with Blender (EEVEE), from shot descriptions (`shots.py`). |
| `vp_vr/tutorials/NN-name/` | One tutorial: `base/` (its library and settings), `tools/`, `rec/` (takes), `render/`, `edit/` (stills, music, thumbnail), the final MP4. |

A tutorial's `tools/`:

| File | Role |
|---|---|
| `reset.sh` | Back to the start: copies `base/library` and `base/prefs` (an isolated `-PrefPath`, VR on, `DesktopDisplay = 3`). |
| `shot.py` | The headset script, written with `vrscript.py` (`scene`, `at`, `wait`, `mark`, `point`, `click`, `rest`) and `vr.py` (screen pixel to UI panel position), into `tutorial.txt`. |
| `rec.sh` | Runs the simulator build with the script and records it: `EYE=2160x2160 tools/rec.sh tutorial.txt ../rec/<take>.mkv <max seconds>`. Also writes `<take>.mkv.vpx.log` and `<take>.mkv.marks.log`. |
| `web.py` | The browser half when there is one: Chrome on the web server of the take, waits for marks of the headset (`waitfile` the other way), screencast to `rec/<take>-web.mp4` with its marks. |
| `take.sh` | `reset.sh`, `shot.py`, then `rec.sh` (and `web.py` beside it). |
| `<name>_video.py` | The compositor: segments cut from the takes by their marks, subtitles, badges, cards. `--timeline`, `--stills t1,t2`, or render. |
| `finish.sh` | Renders the video and adds the backing track. |
| `thumbnail.py`, `screens.py` | The YouTube thumbnail, and the screenshots of the doc (`docs/tutorials/img`). |

The finished pages are on master in `docs/tutorials/` (one page and one index row per video).

## Pitfalls met so far

- **Stop VPX by process id only**: other sessions run their own instances. `rec.sh` stops its own app (SIGTERM only closes the table, so
  it kills again, then -9).
- **Square eyes** (`EYE=2160x2160`) like the Frame's; `vr.py` assumes the UI panel placed 1.5 m ahead, 2.8 m wide, its top at 2.95 m.
- **Rail of the menu**: in a table it has one more icon (This table) than in the lobby, so the icons below Tables are lower.
- **Clicks right after `menu`** come before the panel is placed: wait about 1.5 s.
- **The right stick in the menu** switches it to button navigation; the next trigger press only switches back to pointing, it does not click.
- **A mark on the frame of a table switch is lost**: compute it from the script's times. A table that quits before its first frame gets
  no scene.
- **Controllers in view**: they are low; the head must look down about 40 degrees to see them at a lockbar.
- **`tutorial.py` reads the marks of take5 of the first tutorial when imported** (the compositors set `sys.argv` for it): that take must
  stay in `tutorials/01-tables-roms/rec`.
- **Browser**: rows below the visible part must be scrolled into view before a click; the cursor must be moved into an open `<dialog>`
  (top layer) and nudged so that the screencast shows it; the screencast only sends frames when the page changes (append a last frame);
  `Browser.setDownloadBehavior` needs an absolute path; a headless Chrome left by a failed take keeps its DevTools port (use another).
- **First start of a ROM** may show a notice or initialize (Lost World: "First launch"): run it once in the setup, or pick another table.
