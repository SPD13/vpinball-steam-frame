# Steam Frame Edition: how the fork is organized

This fork follows [upstream Visual Pinball](https://github.com/vpinball/vpinball) (`origin`, branch `master`) and takes its
changes every week. To keep that cheap, the fork is a thin layer on top of upstream: its code lives in its own files, and
upstream's files only get small, guarded hooks into it.

## Where things go

| What | Where |
|---|---|
| Code of the fork (table library, lobby and launcher mode, scores, web routes, table images) | `src/steamframe/` |
| Build of the fork: sources, compile definitions, libraries | `steamframe/steamframe.cmake`, included by one line of the root `CMakeLists.txt` |
| Dependencies the fork adds (libarchive, the Linux OpenXR loader) | `steamframe/deps/*.sh`, run by one line of each `platforms/*/external.sh` |
| Patches of bgfx (Steam Frame Vulkan driver, foveated rendering) | `steamframe/patches/bgfx/`, applied in order by `platforms/linux-aarch64/external.sh` |
| Tools to sync with upstream and regenerate the shaders | `steamframe/tools/` |
| README of the fork (shown by GitHub before the root one, which stays upstream's) | `.github/README.md` |
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

```sh
steamframe/tools/sync-upstream.sh                   # fetch, rebase our commits on origin/master, regenerate the shaders
# on conflicts: resolve, git add, then (during the rebase, the script runs from a copy in the git folder)
.git/steamframe-sync/sync-upstream.sh --continue    # or --abort
```

- The branch before the sync is kept as a local tag `pre-sync-<date>`; the script prints the `git range-diff` command to review
  what changed in our commits.
- `rerere` is turned on: a conflict resolved once is resolved the same way at the next sync.
- After the sync: build for macOS and for the Steam Frame, test on the Frame, then `git push --force-with-lease fork HEAD` and tag
  the release (`frame-<date>`), so that a release can always be rebuilt.

### Shader headers

`regen-shaders.sh` runs upstream's `src/shaders/bgfx/shaders.sh` for the headers listed in its `HEADERS` (the ones built from shader
sources that the fork changes), with a `shaderc` built once from the bgfx sources of `external/`. The DirectX blobs cannot be
compiled on macOS or Linux: they are copied from upstream's headers, so the Direct3D builds keep upstream's shaders until the
headers are regenerated on Windows (`src/shaders/bgfx/shaders.ps1`). On 2026-10-18 it reproduced `bgfx_basic.h` and `bgfx_ball.h`
exactly; `bgfx_imgui.h`, last generated with the Windows `shaderc.exe`, gets different but equivalent SPIR-V and Metal blobs from the
macOS `shaderc`: check the in-game menu on the Frame after the first sync that regenerates it.

### Notes for the next sync

- Upstream added the OpenXR loader to its Linux builds (`linux: add OpenXR support for BGFX (#3837)`): drop
  `steamframe/deps/openxr-loader.sh` and its line in `platforms/linux-*/external.sh`, and take upstream's `ENABLE_XR` setup.
- Upstream moved the VR UI to its own composition layer (`XR: move UI to its composition layer`) and the loading screen to the
  renderer: prefer their versions over ours where they overlap.
