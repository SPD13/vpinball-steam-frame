# Upstream Feature Pull Request Candidates

2026-11-18, checked again after the upstream sync of 2026-11-20

The features of the Steam Frame Edition that could be proposed to upstream Visual Pinball (vpinball/vpinball), one pull request per
feature, ranked by how deep the integration is (how many upstream files a pull request would change) and how big it is (lines of code).
It completes the "Candidates for merge upstream" section of `.github/README.md`, which lists the fixes already proposed.

## 1. How it was measured

- First measured on `fork/master` at `754d33675`, against its merge base with upstream, `28dbb634a` (the sync of 2026-11-13).
- Measured again on 2026-11-20 after the sync of that day: branch `steamframe-squash` of `vpinball-sync/` at `b1bdd6810`, rebased on
  upstream `28919b5df` (151 upstream commits more, Visual Pinball 10.9.0), not yet published to `fork/master` (Frame test pending).
  Every candidate kept its size within a few lines; what the sync changed is in section 7.
- The fork commits are named by the hashes of `fork/master` before that sync; a sync rewrites them, so look them up by their subject
  (`git log --format='%h %s' origin/master..HEAD | grep '<subject>'`).
- **Files**: the upstream files the pull request would change (files that exist upstream), without the build lists
  (`make/CMakeLists_sources.txt`, `make/vpx-core.vcxitems`).
- **Lines**: the lines of our own code, added (+) and removed (−), in upstream files and in the new files the pull request would bring.
  Not counted: vendored third-party code (meshoptimizer, cgltf), documentation, the fork's menu entries in `src/steamframe/vrui/Screens.cpp`.
- Most features have a commit of their own, measured with `git show --numstat`. Those inside the squashed area commits of the fork
  (`8e9ea8018` OpenXR, `05ff3ef89` renderer, `f9073682c` in-game UI, `e635e9c0c` core hooks) were split by function and keyword: their
  numbers (marked ~) are within about 20 %.
- For reference, the fork changes 101 upstream files (+9,787 −597 lines after the sync of 2026-11-20), mostly in
  `src/renderer/VRDevice.cpp` (+1,934 −238).

What every pull request needs on top of the numbers below:

- The `VPX_STEAM_FRAME` guards removed, and the code moved out of `src/steamframe/` into upstream's folders.
- Its setting in upstream's in-game pages (`VRSettingsPage`, `DisplaySettingsPage`...) instead of the fork's VR menu: about 10 to 40 lines each.
- A rebase on upstream `master` and a build of upstream alone (no fork code).
- Settings that follow upstream's live and save flow of the in-game UI (`24acb03e0`, "move graphic settings to the standard live/save flow").

## 2. The rules upstream applies

Upstream's `CONTRIBUTING.md` (last changed 2026-10-26, still the same on 2026-11-20) says that the use of AI is discouraged and that contributions made
**entirely by AI are prohibited**; its "AI-Generated Fixes for Identified Issues" section allows fixes for identified issues, as draft pull
requests that say so. The maintainers have since been more open to AI-generated features: before sending features, have that agreement
written down (an issue or a discussion) and link it from each pull request.

State of the pull requests already sent (checked with `gh` on 2026-11-18 and again on 2026-11-20):

| Pull request | State |
|---|---|
| vpinball#4431 B2S: no crash when the backglass file fails to load | merged 2026-11-14 |
| vpinball#4437 PuP: a screen positioned on a screen drawn by a window is not drawn in a second window | merged 2026-11-15 |
| vpinball#4438 PuP: GetRoot ends with a path separator | merged 2026-11-15 |
| vpinball#4439 PuP: a background of a higher priority replaces a looping media | merged 2026-11-15 |
| vpinball#4449 B2S: backglass GI string IDs are 1-based | merged 2026-11-17 |
| pinmame#838 libpinmame: the nibble of 4-bit memory before the mask | merged |
| vpinball#4435 PuP: label draw order of positioned screens | draft, open. Upstream `b394442fa` (draw screens and labels in window stacking order, 2026-11-15) replaced the render passes it reordered, and the sync of 2026-11-20 dropped the fork's commit for upstream's code. Close it, or rework it, once City Hunter and Tomb Raider are checked on the Frame |
| vpinball#4493 PinMAME plugin: Dip gives the value of the machine until the plugin has its DIP switches (row 1) | draft, open since 2026-11-20, branch `upstream-pr/pinmame-dip-machine-value` of the fork |
| pinmame#833 (another contributor) memory maps on banked memory | open, no longer a draft (2026-11-20); the fork keeps its patch until it is merged |

The **Status** lines of `.github/README.md` were brought up to date on 2026-11-20.

## 3. Tier 1: shallow and small (1 to 4 files, under about 120 lines)

Mostly fixes, quick to review: the best ones to start with.

| # | Feature | Files | Lines | Fork commit | Notes |
|---|---|---|---|---|---|
| 1 | PinMAME plugin: `Dip` gives the value of the machine until the plugin has its DIP switches | 1 (`plugins/pinmame/Controller.cpp`) | +4 −2 | `bed4bc097` | **Proposed: draft #4493 (2026-11-20).** Bug fix: `core.vbs` takes `Dip` as the default of its options and writes them back, so the DIP switches were zeroed and saved in the NVRAM (Star Wars Sonic 1987 takes no coin) |
| 2 | VR visibility mask drawn before the opaque parts | 1 (`src/renderer/RenderView.cpp`) | ~+10 −3 | in `05ff3ef89` | Bug fix: its sort key (z 200000, depth bias 0) put it after every opaque part, so it never masked anything; `Z_LESSEQUAL` instead of `Z_ALWAYS` keeps the hierarchical depth rejection. Still upstream's code on 2026-11-18 |
| 3 | A mesh that fails to load is dropped | 2 (`src/parts/primitive.cpp/.h`) | ~+44 | `0634aafff` (`Primitive::DropInvalidMesh` only) | Fix of a crash in the physics setup (Dragon Ball Super Saiyan Edition). The lobby message stays in the fork |
| 4 | ImGui's font texture never evicted by the texture manager | 2 (`src/renderer/TextureManager.cpp/.h`) | ~+25 | part of `b45a3da3e` (`LoadPinnedTexture`) | Fix. The GPU memory budget taken from the free memory stays in the fork (shared memory of the Frame) |
| 5 | Parts entirely outside the view not drawn | 2 | +43 | `fa6fb68c6` | Draft of the pull request in `docs/Culling Parts Outside the View.md`, section 7 |
| 6 | Refraction probes copy only the area of interest of their parts | 3 | +38 −1 | `a7d78c255` | 2.8 ms less GPU per frame on Medieval Madness on the Frame. One line refers to dynamic resolution: drop it. Draft in `docs/steam-frame/Refraction Copies Limited to Their Area.md` |
| 7 | Reflection probe: parts that cannot land on the parts showing the probe are not rendered | 3 | +74 | `0a8369b66` | `RenderProbe::IsBoxReflected` |
| 8 | Specular anti-aliasing of the material shader | 2 shaders (`material.sh`, `fs_ball.sc`) | ~+55 −7 | in `05ff3ef89` | Every bgfx backend; `docs/Image Quality on the Steam Frame.md`, section 6. Upstream reworked the environment lighting of the same shaders (`e9da74138`, `28919b5df`, 2026-11-19/20): the change was rebased on it at the sync; compare the chrome parts on the Frame before proposing it |
| 9 | Log file and console written by a thread of their own | 1 (`src/utils/Logger.cpp`) | +115 | `f7fcb10d1` | Remove its 4 guards |
| 10 | glibc: mmap threshold of malloc at 1 MB | 1 (would move to `src/core/main.cpp`) | ~14 | `084d1df17` | Linux only: 430 MB less, no growth from table to table, tables load 3 to 5 % slower (to explain in the pull request) |
| 11 | Preload of the images named by the table script (`Player.PreloadScriptImages`) | 4 | +71 | `a80d97c77` | No stop when the script first shows an image (World Poker Tour: 1.9 s to 0.1 s). Its setting goes to upstream's settings page |
| 12 | Valve Frame controller interaction profile (`XR_VALVE_frame_controller_interaction`) | ~3 (`XRInputHandler.h`, `VRDevice.cpp`, `InputAction.cpp`) | ~+80 | in `8e9ea8018` | The d-pad, bumpers and view button for anyone playing upstream's VPX with a Frame on SteamVR |
| 13 | No controller layout prompt for SteamVR's virtual gamepad | 3 (`SDLInputHandler.h`, `InputManager.h/.cpp`) | ~+40 | in `e635e9c0c` | |

## 4. Tier 2: few upstream files, more code (mostly new files)

| # | Feature | Files | Lines | Fork commit | Notes |
|---|---|---|---|---|---|
| 14 | FXAA with the luma in the alpha channel | 5 | +114 −63 | `5f01bb91b` | The FXAA pass 23 % cheaper on the Frame, same image. Two shaders and the renderer |
| 15 | Compact scene buffer (RG11B10F), switched live | 4 | ~190 | `06dd91ef4` | `SceneBufferFormat` moves into the renderer. −0.88 to −1.52 ms on the Frame |
| 16 | Light-map pruning (Off, Low, Medium, High) | 3 | +12, 414 new | `8a8e78316` | The faintest triangles of the light maps left out; tables with light maps take longer to load (the sort) |
| 17 | Reflections settings: extra mirrors, room left out, reflection size | 6 | +64, 255 new | `19004bf0f`, `ef204a621` | |
| 18 | PuP hardware video decoding, then recovery from a blocked decoder | 4 (plugin only) | ~+260 −25 | `1434bb36b`, `5223bb6e5`, `a4d2afc58` | Two pull requests. V4L2 memory to memory decoders: also useful on upstream's Raspberry Pi and RK3588 builds. Upstream split the plugin into its 1.5 and 2.0 engines on 2026-11-16: the three commits were rebased on it at the sync, same size; build and test them on upstream alone |
| 19 | Vibrations on the controller that started the game | 6 | +35, ~150 new | `8dd5ac5e6`, then the PinOne DOF work | Since 2026-11-18 `Vibrations.cpp` also leaves out a controller driven by DOF, through a new `DOF`/`GetTableConfig` message of the DOF plugin (`7427e46a8`, `plugins/dof/DOFPlugin.cpp` +52 −2): propose that message with it, or the vibrations alone |
| 20 | Controller layouts in the controller's page (Set default mapping) | 3 to 4 | ~+70, ~320 new | `ecd04ed5f`, `bc182a96f` | Grew with the PinOne work (layout pop-ups back in the lobby, menu buttons, no service buttons on the D-pad, 200 % nudge). The generic part is the layout buttons; the PinOne layouts and their device detection (`PinOneDevices`, `PinOneLayoutPopup`) could come as data, or stay in the fork |
| 21 | VR room option turned on in a headset when the script leaves it off | 1 | +4, 141 new, 78 test | `9beb73ee9` | It changes constants of the table script: agree on the approach with the maintainers first |
| 22 | Plugin API `ShowMessage` and `ReportMissingFile`, message page | ~5 | ~150 | in `f9073682c`, `e635e9c0c` | The message of a missing ROM |
| 23 | Controller models from the OpenXR runtime (`XR_EXT_render_model`, `XR_EXT_interaction_render_model`) | ~7 | ~+250, ~620 new | in `8e9ea8018`, `915623159` | cgltf is already in bgfx's `3rdparty`. The animated parts were never seen moving: SteamVR gives every node hidden |
| 24 | Simplified meshes of heavy tables | 4 to 5 | ~+60, ~2,500 new | `5b6e33dc7`, `856a0fe09`, `31ca1d2d2`, `b49e1f198` | Three pull requests: the simplifier; coarse levels for far parts and their disk cache; flasher outlines kept whole. meshoptimizer is already in bgfx's `3rdparty` |
| 25 | Web server: pairing, Windows support (network address, UTF-8 paths), upload progress | 2 C++, 3 web | ~150 C++, plus the web pages | in `a596e953f` | RAR and 7z through libarchive as a separate pull request: it adds a dependency to five `external.sh` (`ZipUtils` +177) |

## 5. Tier 3: deep (the whole renderer or the OpenXR session)

Open an issue or a discussion before writing these: they change the design of upstream code that its maintainers are working on.

| # | Feature | Files | Lines | Notes |
|---|---|---|---|---|
| 26 | Dynamic resolution | ~18 | ~650 | Three pull requests: (a) scaled rendering, inert at scale 1 (14 renderer files and 2 shaders, ~350 lines); (b) the VR controller on `XR_META_performance_metrics` (~300, with `44545a5c9`, `9ed897636`, `c92da8ceb`, `e8b3eb8a9`); (c) the desktop controller (`7f47cd81f`: 6 files, +105, 186 new) |
| 27 | Eye-tracked foveated rendering | ~12, plus bgfx | ~900, plus 1,116 lines of bgfx patches | bgfx must take the fragment density map and shading rate patches first. Foveated post-processing follows (`ce65f90e4`, `e8c05985a`: 7 files, +210) |
| 28 | VR menu panel standing in the room, and the laser pointer | ~6 (`LiveUI`, `NotificationOverlay`, `vs_imgui.sc`, `VRDevice`) | ~575 (320 new) | Competes with upstream's head-locked UI layer. The scroll with the stick was fixed on 2026-11-18 (`cc9085e5f`) |
| 29 | One OpenXR session for all the tables, loading message in a layer | ~8 | ~+540 −250, 500 new | `57444d628`, `9628158fa`, `src/steamframe/XRBridge.cpp`. To be built again on upstream's `Player::SetTable` / `TakeTableSwitch` |
| 30 | Passthrough (alpha blended) and cabinet positioning mode | ~10 | ~+215, 950 new | `329959f9f`, `d1ae5bfe8` and their follow-ups. Upstream has its own `XR_FB_passthrough` and keeps reworking the reference spaces (section 7): wait. The sync of 2026-11-20 already dropped the fork's fix of the reflection probe in cabinet space (`20e8a6ded`) for upstream's refactor |

## 6. Not candidates for now

- **They depend on the fork's table library and launcher**: the table library, table picker and lobby; scores and leaderboards; backup and
  restore; PinMAME ROM aliases; table and ROM fixes; the default VR cabinet; the cabinet launcher; the performance panel (drawn by the
  fork's VR UI).
- **Specific to the Steam Frame**: thread placement, PinMAME's Cheat setting on by default, the GPU memory budget from the free memory,
  Steam Input's virtual pads, `TU_AUTOTUNE_ALGO` for Turnip.
- **For other projects**: the bgfx patches (descriptor pools without the free-set flag, fragment density maps, fragment shading rate) go to
  bgfx; the soundfield rotator fix to libspatialaudio (issue videolan/libspatialaudio#97); the memory maps added by the fork to
  tomlogic/pinmame-nvram-maps; the banked memory fix is proposed by another contributor (pinmame#833); the libdof patch that finds a
  PinOne in its Bluetooth Xbox mode (`steamframe/patches/libdof/`, 2026-11-18, not yet tested on hardware) goes to libdof once the
  PinOne firmware with its DOF report (philipellisis/PinOne#11) is merged.
- **PinOne support** (2026-11-18 to 20: DOF to the PinOne, its identity, its layouts, the DOF Config Tool files of the PinOne Mini):
  specific to that controller and to the fork's upload and backup pages, except the DOF plugin message of row 19.

## 7. What the upstream sync of 2026-11-20 changed

The sync took the 151 upstream commits from `28dbb634a` to `28919b5df` (2026-11-20; Visual Pinball is 10.9.0 since `ab2a6c2a1`). None of
them implements a candidate of this list: upstream has no dynamic resolution, foveation, controller models, view culling of the parts,
mesh simplification, preload of the script images or refraction copy limit (none of their functions or settings is in its code).

- **Tier 1 still applies.** Upstream's code is still the one rows 1 and 2 fix: `Controller::GetDip` still reads only the cached state
  (`23f893e38` changed other PinMAME options), and the VR visibility mask is still drawn at z 200000 with no depth bias and `Z_ALWAYS`.
  The fork commits of rows 1, 3, 5 to 7, 9 to 11 and 14 came through the sync with the same size, so they apply to the new upstream.
- **Specular anti-aliasing (row 8)**: upstream improved the environment lighting, diffuse and glossy, and added an edge term to the glossy
  environment lookup (`e9da74138`, `28919b5df`: `material.sh` and `fs_ball.sc`). The fork's change sits on top of it now: check the image
  again before proposing it.
- **PuP plugin**: split into separate 1.5 and 2.0 engines on 2026-11-16 and 17, with several PuP fixes by other contributors. The commits
  of row 18 were rebased on it; the fork's label draw order commit (#4435) was dropped for upstream's window stacking order (section 2).
- **VR reference spaces and renderer**: the cabinet space was merged into the playfield space and the cabinet feet space dropped
  (`cca3bd8c8`, `047089400`), the room and playfield layout refactored (`3cca6b7dc`, `b2997057f`, `603b1f830`), the view jitter reworked
  (`800794dd0`, `a9e06e6f0`) and `RenderView` cleaned up (`a1121aaef`): about 870 changed lines in `Renderer.cpp`, 255 in `RenderView.cpp`,
  97 in `VRDevice.cpp`. The fork's reflection probe fix in cabinet space was dropped for it. Rows 26 to 30 touch these files most: the
  more upstream moves there, the more a pull request of tier 3 has to be rebuilt, which is one more reason to discuss them first.
- **In-game UI**: the graphic settings follow a standard live and save flow (`24acb03e0`): the settings of the pull requests should use it.

Since the first version of this document the fork also added the PinOne support (2026-11-18 to 20) and a fix of the VR pointer scroll:
rows 19, 20 and 28 and section 6 were updated for them.

## 8. Suggested order

1. Rows 1 to 4: fixes, the quickest to review.
2. Rows 5 to 8 and 14: small performance gains with measurements on the Frame.
3. The rest of tier 1, then tier 2, one pull request at a time.
4. Tier 3: a discussion first, then the pull requests in the order given in its rows (the scaled rendering before the controllers).
