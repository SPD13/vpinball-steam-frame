# Steam Frame Edition: how the fork is organized

This fork follows [upstream Visual Pinball](https://github.com/vpinball/vpinball) (`origin`, branch `master`) and takes its
changes every week. To keep that cheap, the fork is a thin layer on top of upstream: its code lives in its own files, and
upstream's files only get small, guarded hooks into it.

## Where things go

| What | Where |
|---|---|
| Code of the fork (table library, lobby and launcher mode, scores, web routes, table images) | `src/steamframe/` |
| Build of the fork: sources, compile definitions, libraries | `steamframe/steamframe.cmake`, included by one line of the root `CMakeLists.txt` |
| Dependencies the fork adds (libzip for the web server, libarchive) | `steamframe/deps/*.sh`, run by one line of each `platforms/*/external.sh` |
| Patches of bgfx (Steam Frame Vulkan driver, foveated rendering) | `steamframe/patches/bgfx/`, applied in order by `platforms/linux-aarch64/external.sh` |
| Patches of libspatialaudio (soundfield rotation fade, see `docs/Spatial Audio Crackle When Turning the Head.md`) | `steamframe/patches/libspatialaudio/`, applied by the `external.sh` of macos-arm64, macos-x64, linux-aarch64, linux-x64 and windows-mingw-x64 |
| Tools to sync with upstream and regenerate the shaders; offline test of the spatial audio rotator | `steamframe/tools/` |
| Lobby rooms: Blender scenes, bake and export to VPX tables, credits of their CC0 assets (the tables are `src/assets/lobby*.vpx`) | `steamframe/lobby-room/` |
| README of the fork (shown by GitHub before the root one, which stays upstream's) | `.github/README.md` |
| Fixes of the tables and ROMs that do not work as they are: the recipes, and their record | `src/assets/fixes/fixes.json`, `docs/Table and ROM Fixes.md` |
| Documentation of the fork | `docs/` (new files only) |

## Rules for upstream files

1. **A hook, not the feature.** In an upstream file, call into `src/steamframe/` (for example `g_launcher->OnTableLoaded(...)`)
   instead of writing the feature there. Members of upstream classes can be defined in our own files: `Player`'s table image
   members are in `src/steamframe/PlayerTableImage.cpp`, `WebServer`'s library routes in `src/steamframe/WebServerLibrary.cpp`.
2. **Guarded by `VPX_STEAM_FRAME`.** It is defined by `steamframe/steamframe.cmake` for the desktop standalone builds (macOS,
   Linux, windows-mingw). Without it, the file builds as upstream's: the mobile and Windows editor builds don't see the fork.
3. **Upstream lines stay as they are.** To change a line, keep it in an `#else` (or `#elif`) branch rather than editing it, so that
   an upstream change to it shows as a conflict on a small hunk:
   ```cpp
   #if defined(VPX_STEAM_FRAME)
      m_player->SetCloseState(g_launcher->m_launcherMode ? Player::CS_CLOSE_CAPTURE_SCREENSHOT : Player::CS_CLOSE_APP);
   #elif defined(__STANDALONE__)
      m_player->SetCloseState(g_isMobile ? Player::CS_CLOSE_CAPTURE_SCREENSHOT : Player::CS_CLOSE_APP); // Upstream's line
   #else
   ```
4. **Generic changes go upstream.** Fixes and features that are not specific to the Steam Frame (OpenXR on Linux, foveated
   rendering, dynamic resolution, controller models, web server pairing, PinMAME messages...) are not guarded: they are kept as
   their own commits and proposed to upstream as pull requests. Once merged upstream, our commit becomes empty at the next sync and
   is dropped.
5. **Generated files are never merged.** The shader headers (`src/shaders/bgfx_*.h`) are regenerated from the shader sources
   (`steamframe/tools/regen-shaders.sh`). Change the `.sc`/`.sh` sources, never the headers by hand.
6. **Line endings.** `src/core/player.cpp` uses CRLF: scripted edits must keep it, or the whole file shows as changed.

## Syncing with upstream

The fork follows upstream **master**, not upstream releases: upstream's only release branch (`10.8.0`) predates the standalone, bgfx and
OpenXR code the fork builds on, and its 10.8.1 builds are pre-releases made from master, where fixes land first. The cost is that master
can carry regressions (libwinevbs on 2026-10-18), hence a Frame test before every publication.

The fork is a short series of commits rebased on upstream at every sync, **one commit per area** (build, OpenXR, renderer, in-game UI,
core hooks, PinMAME, web, launcher, documentation and tools, regenerated shader headers), each file belonging to one commit only, so
that an upstream change to a file conflicts with one of our commits at most. The rebase rewrites the history: the fork's `master` is
force-pushed after each sync, and the previous state stays reachable through the `pre-sync-<date>` tags (and `pre-squash-20261018`, the
history before the squash). A change of the fork made between two syncs is committed with `git commit --fixup=<the commit of its area>`,
then folded in with `git rebase -i --autosquash origin/master` (no editor needed: `GIT_SEQUENCE_EDITOR=true`); a new file goes to the
commit of its area.

```sh
steamframe/tools/upstream-report.sh                 # optional: preview the upstream commits to review (VR/XR first)
steamframe/tools/sync-upstream.sh                   # fetch, rebase our commits on origin/master, regenerate the shaders, report
# on conflicts: resolve, git add, then run the --continue command the script prints (it runs from a copy in the git folder)
<git folder>/steamframe-sync/sync-upstream.sh --continue    # or --abort
```

- The branch before the sync is kept as a local tag `pre-sync-<date>`; the script prints the `git range-diff` command to review
  what changed in our commits.
- `rerere` is turned on: a conflict resolved once is resolved the same way at the next sync.
- Upstream changes can break the fork without any conflict (upstream's own VR work for the Quest and OpenXR especially: on 2026-10-18,
  `ResFactor` became relative to the recommended resolution and the Frame rendered at 455x455). The sync ends with a report of the upstream
  commits that touch the VR, renderer, in-game UI, player, settings, web, PinMAME and build areas or the upstream files we change, VR and XR
  first (`steamframe/tools/upstream-report.sh`, kept in `<git folder>/steamframe-upstream-report.txt`): review them and test them on the
  Frame. Run alone, the script previews what the next sync brings in.

### Weekly routine

The workspace folder holds, side by side:

| Folder | What |
|---|---|
| `vpinball/` | main checkout, `master` tracking `fork/master`; remotes `origin` (upstream) and `fork` (github.com/SPD13/vpinball-steam-frame) |
| `vpinball-sync/` | worktree of `vpinball/` for the syncs, branch `steamframe-squash`, built apart so that a sync never breaks the main build |
| `vpinball-linux/`, `vpinball-sync-linux/` | copies used by the Linux build of each, with the container volumes `vpx-linux-*` and `vpx-sync-*` |
| `Doc/` | github.com/SPD13/vp_vr-steam-frame-docs: Frame build, deploy and release scripts (`steam-frame-build/`), plans |
| `steam-frame-package/` | the last Frame build, as deployed |

1. **Start from the published state.** In `vpinball-sync/`, with a clean tree: `git fetch fork origin && git reset --hard fork/master`
   (the branch is `steamframe-squash`). `steamframe/tools/upstream-report.sh` previews what comes in.
2. **Sync.** `steamframe/tools/sync-upstream.sh`. Resolve conflicts with the rules above (our lines in the `VPX_STEAM_FRAME` branch,
   upstream's line kept as it is in the `#else`), keeping CRLF files CRLF, then `--continue`.
3. **Review.** The report, VR and XR first, and `git range-diff`. When upstream built something the fork has too, decide as below.
4. **Build for macOS** (Homebrew's bison first in the `PATH`: `export PATH="$(brew --prefix bison)/bin:/opt/homebrew/bin:$PATH"`):
   `platforms/macos-arm64/external.sh` (rebuilds only the dependencies whose version changed), then `cmake --build build -j10`.
5. **Check on macOS**, with a settings folder of its own (`-PrefPath <folder>`, holding a `VPinballX.ini` with `TablesPath` set to a
   folder of tables with their ROMs, `WebServerAlwaysOn = 1`, `WebServerPairing = 0`):
   - `build/VPinballX_BGFX.app/Contents/MacOS/VPinballX_BGFX -PrefPath <folder> -Launcher`: the lobby reaches `Startup done` in
     `<folder>/vpinball.log`, and `http://127.0.0.1:<WebServerPort>/` with `/info`, `/tables`, `/scores` and `/missing-roms` answer 200;
   - the same with `-play <table.vpx>` on a PinMAME table using `core.vbs` (Attack from Mars): no `OnScriptError` in the log;
   - stop each run by its process id (other VPX instances may be running).
6. **Build for the Frame**: `VPX_SRC=vpinball-sync VPX_LINUX=vpinball-sync-linux VPX_VOLUMES=vpx-sync Doc/steam-frame-build/build-linux-arm64.sh`
   (dependencies, application, then `steam-frame-package/`).
7. **Deploy**: the Frame awake (`ssh steamos@frame.local`; it drops off the network when it sleeps) and VPX not running on it
   (`pgrep -a VPinballX`), then `Doc/steam-frame-build/deploy-to-frame.sh`; check that the `sha1sum` of
   `~/devkit-game/vpx_frame/VPinballX_BGFX` on the Frame is the one of `steam-frame-package/VPinballX_BGFX`.
8. **Test on the Frame** (the owner, in the headset): the checklist below, plus the VR and XR commits of the report. Afterwards, on the
   Frame: `coredumpctl list` (no VPX crash) and `~/.local/share/VPinballX/10.8/vpinball.log` (`Startup done` and `Table switch` for each
   table, no `OnScriptError`). `Doc/steam-frame-build/frame-run.py` starts and stops the game without the headset on.
9. **Publish, with the owner's approval**: `git push --force-with-lease=master:<fork master before> fork steamframe-squash:master`, then,
   in `vpinball/` if its tree is clean, `git fetch fork && git reset --hard fork/master`. A release for users is a separate decision
   (`Doc/steam-frame-build/make-release.sh <major.minor.patch>`): numbered since 0.5.0 (the minor number for new features, the patch number
   for fixes only), tagged `v<version>` and titled "Visual Pinball X: Steam Frame Edition <version>" on GitHub.

### When upstream builds what the fork has

Upstream develops VR for the Quest and for OpenXR on the desktop: it may add a feature the fork already has, differently. Git sees no
conflict there; the sync decides:

| Case | Decision | Example |
|---|---|---|
| Upstream's version is as good on the Frame | Take it, drop ours: one feature less to carry | OpenXR on Linux, the loading screen |
| Ours is better for the Frame | Keep ours; turn upstream's off on our builds only, with `#ifndef VPX_STEAM_FRAME`, without deleting it | The VR menu panel standing in the room, instead of upstream's head-locked UI layer |
| Ours is generic and better | Keep it, and propose it upstream so that both become one | Foveated rendering, dynamic resolution, controller models (to propose) |

Behaviour changes without conflict (a setting that changes meaning, a dependency regression) are found by the report and the Frame
test: add each one found to the checklist.

### Frame test checklist

- A ROM table using `core.vbs` starts and plays (Attack from Mars: libwinevbs `TypeName` regression, 2026-10-18).
- Back to the lobby after a table, then another table (race in the OpenXR render loop, 2026-10-18).
- Image sharpness as before: render resolution (`ResFactor` semantics, 2026-10-18), foveated rendering and dynamic resolution.
- VR menu, in a table and in the lobby; the table picker (imgui updates).
- Scores recorded after a game; sound.
- Web upload of a table from a phone or a computer (table picker, "Add tables from a browser").

### Shader headers

`regen-shaders.sh` runs upstream's `src/shaders/bgfx/shaders.sh` for the headers listed in its `HEADERS` (the ones built from shader
sources that the fork changes), with a `shaderc` built once from the bgfx sources of `external/`. The DirectX blobs cannot be
compiled on macOS or Linux: they are copied from upstream's headers, so the Direct3D builds keep upstream's shaders until the
headers are regenerated on Windows (`src/shaders/bgfx/shaders.ps1`). On 2026-10-18 it reproduced `bgfx_basic.h` and `bgfx_ball.h`
exactly; `bgfx_imgui.h`, last generated with the Windows `shaderc.exe`, gets different but equivalent SPIR-V and Metal blobs from the
macOS `shaderc`: check the in-game menu on the Frame after the first sync that regenerates it.

### First sync (2026-10-18)

The history of the fork was squashed into one commit per area (tag `pre-squash-20261018` keeps the previous one), then rebased on
upstream (528 commits). What changed in the fork with it:

- OpenXR on Linux is upstream's (`ENABLE_XR` and the loader of `platforms/linux-*/external.sh`): `steamframe/deps/openxr-loader.sh` was dropped.
- Upstream no longer builds libzip for the desktop: `steamframe/deps/libzip.sh` builds it for the web server (macOS, Linux, windows-mingw).
- bgfx 1.166 renamed the attachment flags (`BGFX_RESOLVE_*` to `BGFX_ATTACHMENT_*`) and took the bits the patches used: the fragment density
  map and shading rate flags moved to free bits (`BGFX_TEXTURE_FRAGMENT_DENSITY_MAP` 0x0080000000000000, `..._SHADING_RATE` 0x0100000000000000,
  `BGFX_ATTACHMENT_FRAGMENT_DENSITY_MAP` 0x10, `..._SHADING_RATE` 0x20, `BGFX_CAPS_FRAGMENT_DENSITY_MAP` 0x0000000002000000).
- The VR UI stays our panel standing in the room: upstream's head-locked UI composition layer is not created on our builds
  (`#ifndef VPX_STEAM_FRAME` in `VRDevice::CreateSession`).
- The table loading screen of the launcher is drawn with upstream's `Renderer::RenderUIScene` (our `RenderLoadingFrame` was dropped).
- libwinevbs 11.20 made `TypeName` ask IProvideClassInfo first, which the ATL of libwinevbs did not implement: every VPX object was an
  'Object' to the scripts (core.vbs refused the trough kickers, Attack from Mars failed to start). Fixed upstream the next day (libwinevbs
  #43, vpinball 2ca6b8bd9), taken at the sync; the patch the fork carried until then was dropped.
- The PinMAME plugin looks for memory maps along the table, in its settings folder, then in the maps bundled with the application (they
  have maps added by the fork), then in the ones upstream ships along the plugin.

### Notes for the next sync

- Upstream's `Player::SetTable` / `TakeTableSwitch` switch tables without leaving the player: the launcher (`Launcher::PlaySelectedTables`)
  could use them instead of its own loop, which would remove most of the hooks of `PlayTableCommand`.
- Propose the memory maps added by the fork to tomlogic/pinball-memory-maps, then drop the bundled copy.
