# PinUP Player (PuP) on the Steam Frame: how it works

Oct 24, 2026 · @SPD13

Technical notes on the PuP support of the fork: how a PuP table is played in the headset, which code does what, the rules the fork
follows to choose a pack's layout and place its screens, the hardware video decoding and how it recovers from a blocked decoder, how to
diagnose a PuP table, and what is left to do. The user side is in `docs/Steam Frame Branch.md`, section 15.

It was developed on the branch `pup` (Oct 23 and 24, 2026) and is in `master` since Oct 24, 2026, as an Alpha: the web page says so on
the PuP packs tab. Sections 8 to 11 are the reference for continuing the work: the code file by file, the changes to the upstream
plugin, how to build and test, and the state of the packs on the Frame.

## 1. The parts

PinUP Player itself is a closed Windows program that draws its screens in desktop windows. The fork does not use it: upstream's PUP
plugin (`plugins/pup/`) is a rewrite that the table scripts drive as if it were the real one, and that renders its screens through the
plugin API instead of windows. The fork adds what the headset needs around it.

| Part | Where | Role |
|---|---|---|
| PUP plugin (upstream) | `plugins/pup/` | `PinUpPlayer.PinDisplay` for the scripts (`PUPPinDisplay`), the pack (`PUPManager`: screens, playlists, triggers, labels), the video players (`PUPMediaPlayer`), rendering into the ancillary windows (`PUPManager::Render`) |
| Hardware decoding (fork, generic) | `plugins/pup/PUPMediaPlayer.cpp`, `PUPPlugin.cpp` (`HardwareDecoding` setting), `LibAv.h` | FFmpeg's V4L2 memory to memory decoders, their software fallback and their stall recovery (section 5) |
| Cabinet surfaces (fork) | `src/steamframe/CabinetDisplays.h/.cpp`, `src/parts/flasher.h/.cpp` (`RenderCabinetDisplay`) | Flashers on the VR cabinet where PuP's backglass, DMD and topper are drawn (section 3) |
| Packs (fork) | `src/steamframe/PupPacks.h/.cpp` | Which pack a table asks for, and where it is |
| Pack layouts (fork) | `src/steamframe/PupPackOptions.h/.cpp` | Reading the pack's batch files, choosing and applying a layout, reading `screens.pup` (section 4) |
| Library, upload, web (fork) | `src/steamframe/TableLibrary.cpp`, `WebServerLibrary.cpp`, `Launcher.cpp`, `lib/src/WebServer.cpp`, `src/assets/web/` | Pack import from uploads, shared pack folder, `PuP` badge, `PuP packs` tab, Pack options panel, uploads up to 64 GB |
| Table picker (fork) | `src/steamframe/TablePickerPage.cpp`, `vrui/LegacyTiles.cpp`, `vrui/PageAdapter.cpp`, `src/ui/live/ingameui/InGameUIItem.h` | `PuP` badge on the tiles of both VR menus, pack line in a table's page |
| Build (fork) | `steamframe/steamframe.cmake` | The new files, and `VPX_STEAM_FRAME` for the `PUPPlugin` target |
| Tests | `tests/test-table-library.cpp` | Imports, layouts, `screens.pup` rules (compiled alone, see section 6) |

Every part is described file by file in section 8.

## 2. From the script to the headset

1. The script creates `PinUpPlayer.PinDisplay` and starts the pack, usually `PuPlayer.B2SInit "", cPuPPack`. The plugin looks for the
   pack folder ignoring case: `pupvideos/<pack>` along the table, then `<PinUP Player folder>/pupvideos/<pack>`, which the launcher
   points to `<tables>/pinupsystem` (`Launcher::SetupSharedPupFolder`) unless the user defined one.
2. `PUPManager` reads `screens.pup`, `playlists.pup` and `triggers.pup`. Triggers fire on the script's events
   (`PuPlayer.B2SData "E871",1`, sent by the tables' `pupevent 871`) and on PinMAME's DMD frames and switches, and play a playlist file on
   a screen.
3. Each screen is a canvas, or is drawn inside another screen when its `CustomPos` names one (`"5, 16.6, 5.1, 66.7, 66.8"`: inside
   screen 5, at 16.6 % / 5.1 %, 66.7 % x 66.8 % of it). The plugin creates a screen a `CustomPos` names and the file does not define. A
   later line of the same number replaces the earlier one (`Replacing previously defined PUP screen` in the log).
4. `PUPManager::Render` draws a tree of screens into each ancillary window of VPX:

   | Window | Root screen | Fallback |
   |---|---|---|
   | Topper | 0 | none |
   | Backglass | 2 | 6 |
   | ScoreView (the DMD) | 5 (FullDMD) | 1 (DMD) |

   A root is used when something visible is in its tree (itself, or a screen positioned on it, at any depth), and when it is a canvas:
   a screen that is itself positioned on another screen is a root only when other screens are positioned on it (`isUsableRoot`).
   **A screen that is off still hosts the screens positioned on it**: an off screen 5 with visible children is the ScoreView's root.
   Visible means any mode but `Off`, `Disable`, `JavaScript`, `MusicOnly` and `ForceHide` (the last two play sound only).
5. In VR, the ancillary windows are only seen through flashers whose render mode is *External renderer*. VR rooms are built for Windows,
   where PinUP Player has desktop windows of its own: they have at best a backglass surface, never a DMD or a topper one. The fork adds
   them (section 3).

## 3. The cabinet surfaces (`CabinetDisplays`)

Created when the table uses PuP and runs in VR (`VPX_CABINET_DISPLAYS=1` forces them on a desktop, to test on the Mac), in
`Player::InitTableSession`, after the parts are loaded and **before the table script runs**: what the script moves at init (DMD
flashers of the VR room, `VR_PupBackglass` made visible) is not seen yet.

- **Backglass**: the table's backglass flasher (*External renderer*, style Backglass) when it has one. Otherwise the glass of the VR
  cabinet, a primitive named like a backglass (`PinCab_Backglass`, `VR_Backglass`...), trimmed above a DMD standing on it and moved
  1.5 units toward the player; otherwise where VPX puts its implicit VR backglass. Hidden while the implicit VR backglass is shown (one
  surface per window, as PuP sizes its canvas after it).
- **DMD (ScoreView)**: in front of the table's DMD, a flasher showing a display or named like a DMD standing above the glass, or a
  primitive named like a DMD. Without one, under the backglass, half its width, 4:1 (`No DMD in the cabinet: DMD placed under the
  backglass`). **16:9 when PuP draws its FullDMD screen there** (see below), 4:1 otherwise; the height of the anchor is kept.
- **Topper**: the topper screen of the room (a `topper` primitive), otherwise above the backglass, on top of `PinCab_Backbox`.
- Each surface is drawn only while PuP draws on it (`[CabinetDisplays] Backglass shown: PuP draws on it`), and for a second after the
  last draw, so it does not blink between clips. PuP lays out its screens on a canvas of 960 pixels on the long side
  (`GetCanvasSize`), about the size of a backglass in each eye.
- While PuP draws the DMD, the table's DMD flashers found above are hidden (`Table DMD hidden: PuP shows its DMD`).
- The **PuP displays** page of the in-game menu (`vrui/cabinet-displays`) shows or hides each surface and adjusts its position, size,
  tilt and aspect, saved for the table in its `CabinetDisplays` settings.

**The full DMD rule** (`HasFullDMDScreen`, `PupPackOptions::DrawsFullDMD`). The DMD surface is 16:9 when the plugin will draw its
screen 5 in the ScoreView. It follows the plugin's root rule of section 2: screen 5 visible and not positioned elsewhere, or **any
visible screen positioned on screen 5, even when screen 5 is off**, or created by the plugin for such a screen. Screens defined by the
script are only seen through `pFullDMD` in the script, when `screens.pup` does not define screen 5. Before Oct 24, 2026, only screen 5's
own mode was read, so a pack hosting its videos on an off screen 5 had them squeezed into a 4:1 surface (The Matrix, section 7).

## 4. Packs and their layouts

**Import.** Packs uploaded from the browser (the `PuP packs` tab, or with a table in a bundle) are extracted to
`<tables>/pinupsystem/pupvideos/<pack>`, or kept in the table's own `pupvideos` folder. The pack of a table is found by
`PupPacks::FindPackFolder` from the names of its script (the quoted name given to `PuPlayer.Init`, the `cPuPPack` value, then the ROM
`cGameName`, which the plugin also uses to start a pack by itself). A `PuPlayer.B2SInit` call is not read: a script naming its pack
only there (not through `cPuPPack`), and whose ROM name is not a pack either, gets no badge, and its surfaces are placed without the
pack's `screens.pup` (the full DMD rule of section 3 then only sees `pFullDMD` in the script).

**Layouts.** A pack usually offers screen layouts as Windows batch files at its root ("3 Screen - VR.bat"), which copy a set of `.pup`
files from a folder of the pack and write the layout's number to a file the script reads. They are never run: `PupPackOptions` reads
them and does only `copy`/`xcopy`, `echo` into a file and `del`, inside the pack. The files a layout replaces are saved in
`.options-backup`, the applied layout in `.options-applied`. The Pack options panel of the web file manager lists, applies and restores
them.

**The layout chosen at import** (`PupPackOptions::List`, `ApplyRecommended`), when none was applied yet:

1. A single layout named for VR (`VR`, `-VR`): applied.
2. Otherwise each layout gets a score, compared in this order:
   1. the number of cabinet displays its `screens.pup` shows something on: topper (0), DMD (1 and 5, which share the DMD surface) and
      backglass (2), a screen counting for the display of the root it is drawn in (section 2);
   2. among those, how many are the topper and the backglass (since Oct 24, 2026): the cabinet's DMD surface stands in front of the
      table's DMD, so a layout playing the backglass videos on the full DMD screen suits it less than one playing them on the backglass;
   3. its name: "separate", "3 screen", "cabinet" score higher, "on BG", "no DMD", "desktop", "2 screen" lower (`CabinetScore`).
3. A pack whose `.pup` files are in its layout folders only plays nothing as uploaded: it gets the first of the best layouts. A pack
   with `.pup` files of its own already plays: it is changed only for a single best layout using two displays or more, or with a
   cabinet name.

Choices on the packs of the Frame (checked on Oct 24, 2026 with their `.pup` and `.bat` files):

| Pack | Layout chosen | Why |
|---|---|---|
| Darkest Dungeon | 3 Screen - VR | VR name |
| Futurama | Option 3 - Single Screen - Desktop - VR | VR name |
| Scarface | 1 - Desktop-VR | VR name |
| Aliens (`alienspupver`) | Option 1 - Topper BG with a FULLDMD Video - All Seperate | 3 displays |
| Austin Powers | Option 3 - BG Topper Separate | 2 displays, name; no `.pup` at the root |
| The Matrix | 4 Screen - Topper DMD Separate | 2 displays, both topper and backglass (the "3 Screen - FullDMD" layout also uses 2) |

Only packs imported after the change get it automatically: a pack already imported keeps its files until a layout is applied from the
Pack options panel, or the pack is uploaded again.

## 5. Video decoding

Each screen has its media players (`PUPMediaPlayer`), each with a command queue thread (`PUP.#<screen>.CmdQueue`) and, while a media
plays, a decoding thread (`Run`): it reads packets, decodes them, converts the frames to RGBA at the canvas size (`sws_scale`) into a
ring of three frames that the render thread uploads.

**Hardware decoding** (`HardwareDecoding` setting of the plugin, on in the fork's builds through `VPX_STEAM_FRAME`). Videos of 640 x 360
and more are decoded with FFmpeg's V4L2 memory to memory decoder of their codec (`h264_v4l2m2m`, `hevc_v4l2m2m`, `vp9_v4l2m2m`), which
on the Frame drive the Qualcomm iris decoder. On Darkest Dungeon (1080p videos, two or three at once) it took the decoding threads from
46 % to 30 % of a core; most of the rest is the RGBA conversion.

- These decoders do not resume after `avcodec_flush_buffers`: a looping video opens a new decoder at each loop.
- A video the decoder rejects (an error from `avcodec_send_packet`) goes on with the software decoder from the next key frame.

**A blocked hardware decoder** (fixed on Oct 24, 2026). FFmpeg's V4L2 decoders wait for the device with `poll()` and no timeout
(`v4l2_dequeue_v4l2buf(ctx, -1)`), and retry when a signal interrupts the wait. Even `avcodec_send_packet` waits there, as it decodes
a frame ahead. When the device stops answering, the call never returns. The Matrix showed it (section 7): the decoding thread stuck
in `poll`, the player waiting for it in `StopBlocking` (`std::thread::join`), and VPX unable to close the table. Nothing in the kernel
log, and the same videos decode well at other times: it is intermittent, about every other run of that table.

What the player does now:

- The decoding thread notes when it enters a call into a hardware decoder and when it leaves it (`m_hardwareCallStart`, in
  `SendVideoPacket` and `ReceiveVideoFrame`).
- **During play**, each render of the player checks it (`CheckHardwareStall`). A call lasting 2 s or more is taken as blocked: the
  command queue stops the device's queues (`UnblockHardwareDecoder`, `VIDIOC_STREAMOFF` on its output and capture queues). The waiting
  thread wakes up with an error, and the video goes on with a software decoder from the next key frame (`AfterHardwareCall`). After 2
  blocked decoders in a table, its next videos are decoded in software (`HardwareDecoding` itself is not changed: the next table tries
  again).
- **When a player stops**, it no longer waits blindly for the decoding thread: while the thread is still running, a call into a hardware
  decoder lasting more than 100 ms is unblocked the same way, so the thread sees that it must stop.
- **Finding the device.** FFmpeg does not expose the descriptor of its device. The private data of its V4L2 decoders starts with
  `{ const AVClass*, V4L2m2mContext* }` and `V4L2m2mContext` with `{ char devname[PATH_MAX]; int fd; }` (`libavcodec/v4l2_m2m.h`,
  unchanged since FFmpeg 4.0, checked in the bundled FFmpeg). The descriptor read there is used only when `/proc/self/fd/<fd>` points to
  that device, so another layout of FFmpeg is never acted upon (the decoder then stays blocked, with an error in the log).
- Stopping the queues of a working decoder costs nothing more than its video going on in software, which can only happen at the
  threshold or when the player stops anyway.

Log lines (errors of the PUP plugin):

```
[PUP:PUP::PUPMediaPlayer::UnblockHardwareDecoder] Hardware decoder h264_v4l2m2m blocked for 2008 ms, stopping its device: file=Intro_Enhancedv2_0.mp4
[PUP:PUP::PUPMediaPlayer::AfterHardwareCall] Hardware decoder h264_v4l2m2m stopped decoding, using software decoding: file=Intro_Enhancedv2_0.mp4
[PUP:PUP::PUPMediaPlayer::CheckHardwareStall] Hardware decoders stopped decoding 2 times: the next videos are decoded in software
```

Without hardware decoding (`[Plugin.PUP] HardwareDecoding = 0`), none of this happens.

## 6. Diagnosing a PuP table

**The log** (`~/.local/share/VPinballX/10.8/vpinball.log` on the Frame) tells most of it:

- `[PUP:...] PUP path:` the pack found; `Playlist folder ... is empty`, `PlayFile not found`: missing media.
- `Screen added: screen={mode=..., screenNum=..., m_customPos={sourceScreen=...}}` and `Replacing previously defined PUP screen`: the
  screens as the plugin understood them. Rebuild the trees of section 2 from them to know what each window shows.
- `[CabinetDisplays] ... placed ...`, `... surface created`, `... shown: PuP draws on it`, `Table DMD hidden`: the surfaces.
- An exit that stops at `PUP Manager stop` is the plugin unloading its screens and waiting for a player.

**The pack's layouts**: compare the `.pup` files at the pack's root with those of its layout folders (`md5sum`) to know which layout
is in place; `.options-applied` tells the one the fork applied.

**The table script**: `VPinballX_BGFX -ExtractVBS <table.vpx>` writes the `.vbs` **next to the real table, even through a symlink**,
where it then replaces the table's script: delete it afterwards.

**Headless runs on the Frame** (see `Doc/test-hooks/perf-hooks.py`): a copy of the deployed folder (`cp -al`, then replace the files
to change by new ones, never write through the hard links) with the instrumented executable, and `VPX_PERF_FORCE_RENDER=1
VPX_PERF_TEST=<seconds>` to play unworn and quit. Each run increments the table's play count in its `.info` file.

**Stacks of a hung VPX**: `kernel.yama.ptrace_scope` is 1 on the Frame, so only a parent may attach gdb. Starting VPX under gdb changes
the timing enough to hide the hang; instead, the shell script that started VPX replaces itself with gdb once VPX is stuck
(`exec gdb -p $pid -batch -ex "thread apply all bt"`), which keeps it the parent. `/proc/<pid>/task/*/wchan` shows which threads wait
on what without attaching (`poll_schedule_timeout` for a thread in `poll`).

**Tests**: `tests/test-table-library.cpp` is not part of `vpx-test`. Compile it alone with a doctest main:

```sh
printf '#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include "doctest.h"\n' > /tmp/main.cpp
clang++ -std=c++20 -I third-party/include -I tests /tmp/main.cpp tests/test-table-library.cpp src/steamframe/TableLibrary.cpp \
   src/steamframe/PupPackOptions.cpp src/steamframe/RomAliases.cpp -o /tmp/test-table-library && /tmp/test-table-library
```

(The case "the database survives a restart" fails on an old expectation about blank names, unrelated to PuP.)

## 7. Case study: The Matrix (VP Cooks, PuP events v1.0.14)

**Seen on Oct 24, 2026**: no PuP screen in the headset, and the Frame stuck when leaving the table (`Closing from signal: 2` after a
minute and a half, when it was stopped by hand).

**No PuP screen: the pack's layout, and the full DMD rule.**

- The pack ships with its "3screen - FullDMD" layout at its root: its backglass videos (screen 11), overlay (12) and topper videos (13)
  are positioned on screen 5, which the pack defines as `Music2`, off. Screen 2 only loops a still backglass (`Backglass_16_9.mp4`).
- In VR, the backglass then showed the still image (as if PuP were not running), the topper stayed empty (screen 0 off), and every video
  went to the DMD surface, which was 4:1, half the backglass wide, under the backglass, as the script moves the room's DMD only after
  the surfaces are placed.
- The import had applied nothing: "3 Screen - FullDMD" and "4 Screen - Topper DMD Separate" both used 2 displays and had the same name
  score, and a pack with its own `.pup` files is left alone on a tie.
- Fixed: the tie is broken for the layout using the topper and the backglass (section 4), and the DMD surface is 16:9 when screen 5
  hosts visible screens (section 3). The "4 Screen" layout sends the 33 topper triggers to screen 0 and the backglass videos onto
  screen 2, which is what the VR cabinet shows best.

**Stuck when leaving: a blocked hardware decoder.**

- gdb on the hung process: the main thread in `PUPManager::Unload` → `~PUPScreen` → `~PUPMediaPlayer` → `StopBlocking` → `join`, the
  decoding thread in `avcodec_send_packet` → `ff_v4l2_context_dequeue_frame` → `v4l2_dequeue_v4l2buf` → `poll(timeout = -1)`.
- Headless runs of 45 to 90 s: 2 of 4 runs hung with hardware decoding, 0 of 6 with `HardwareDecoding = 0`. The videos are ordinary
  (1080p H.264, like the packs that never hung), and the kernel logged no decoder error during those runs.
- It is not specific to the table: any player can meet a decoder that stops answering, The Matrix plays several 1080p videos at once
  and loops many of them, which makes it frequent there.
- The decoders also block during play, not only at the end: a blocked video stops on its last decoded frame until the table is
  closed, which adds to the missing screens.
- Fixed by the stall recovery of section 5. With it, 8 headless runs of 45 to 90 s with hardware decoding all closed the table normally.
  In 3 of them a decoder blocked during play (the attract video `Intro_Enhancedv2_0.mp4` twice, `BallSaved1v4.mp4` once), was stopped
  2 s later and its video went on in software; in one run that happened twice, so its next videos were decoded in software.

**Played in the headset on Oct 24, 2026**, with the pack set to "4 Screen - Topper DMD Separate" and hardware decoding on: the topper,
the backglass and the full DMD show their videos, and the table closes normally.

## 8. The code, file by file

Everything of the fork is guarded by `VPX_STEAM_FRAME` (defined for the standalone desktop builds), except the plugin changes of
section 9, which are generic.

### Hooks in upstream files

| File | Hook |
|---|---|
| `src/core/player.h/.cpp` | `Player::m_cabinetDisplays`; `InitTableSession` calls `CabinetDisplays::Create` after the implicit VR backglass is set up, before the parts are set up for rendering; `PrepareFrame` calls `Update` every frame, next to the score tracker |
| `src/parts/flasher.h/.cpp` | `m_cabinetSuppressed` (the table DMD flashers hidden while PuP draws the DMD: `Render` returns at once), `RenderCabinetDisplay` called first in the `EXT_RENDER` case (not for the desktop backdrop); it returns false for the flashers that are not ours, which then take upstream's path. Both are implemented in `CabinetDisplays.cpp` |
| `lib/src/WebServer.h/.cpp` | `MAX_UPLOAD_SIZE` 64 GB instead of 2 GB; at the first chunk of an upload (`offset == 0`), the free space of the destination must exceed the file size plus 256 MB, otherwise the answer is `507` (`Not enough free space to upload ...` in the log); declaration of `PupOptions` |
| `src/ui/live/ingameui/InGameUIItem.h` | `m_tileBadge`, a short text drawn in the bottom left corner of a tile's image |

### `CabinetDisplays` (`src/steamframe/CabinetDisplays.h/.cpp`)

- **`Create`** returns nothing outside VR (unless `VPX_CABINET_DISPLAYS=1`), when the script does not create `PinUpPlayer` and no pack
  is found for its names (`PupPacks::GetTableUse` on the table's script, `FindPackFolder` along the table and in
  `Launcher::GetSharedPupVideosPath`), or when no surface is needed. An `EXT_RENDER` flasher styled Backglass whose name contains
  `topper` is restyled Topper first (legacy topper images converted as backglasses).
- **Surfaces** are flashers created by the code (`CreateFlasher`), in `EXT_RENDER` mode with the window as style (Backglass, ScoreView,
  Topper). Their anchors:
  - DMD: `FindDMDAnchor`, a flasher showing a display, or named `*dmd*` and standing above the glass; then a primitive named `*dmd*`;
  - backglass: `FindCabinetBackglass`, the primitive `PinCab_Backglass` first, then any primitive whose name contains `backglass` and
    whose image is not `backglassimage` (`FitUprightPanel` fits it 16:9, `KeepAboveDMD` trims it above a DMD standing on the same
    glass); otherwise `GetDefaultBackglassRect`, where VPX puts its implicit VR backglass;
  - topper: `FindCabinetTopper`, a primitive named `*topper*` that shows `backglassimage` or is named `*screen*`; otherwise above the
    backglass, on top of `PinCab_Backbox` when there is one.
- **Rendering** (`Flasher::RenderCabinetDisplay`): PuP's renderer is the ancillary renderer whose id is `PUP` in
  `Player::m_ancillaryWndRenderers[window]` (B2S also registers for the ScoreView). While the surface is shown, a black quad is drawn
  first (dark green while the placement page is open and PuP draws nothing). The transform is upstream's external renderer one; the
  context comes from `Renderer::GetAncillaryRenderContext` with the canvas of `GetCanvasSize` (960 pixels on the long side). PuP
  claims its windows as soon as its screens exist, even when nothing plays, so whether it drew is told by the commands of the render
  pass (`RenderPass::GetCommandCount` before and after). `OnRendered` keeps the surface shown `kShownHoldMs` (1 s) after the last draw.
- **Settings** (section `CabinetDisplays` of the table settings, `GetPropIds`): `<Surface>Show` (Auto, Off), `<Surface>OffsetX`,
  `OffsetY`, `OffsetZ` (VPX units: right, up, toward the player), `<Surface>Size` (scale), `<Surface>Tilt` (degrees added to the X
  rotation), `<Surface>Aspect` (Auto, 4:1, 16:9, 4:3, 21:9, 32:9), with `<Surface>` = `Backglass`, `DMD` or `Topper`; and
  `HideTableDMD`. `Update` applies changes live.
- **Menu**: the page `vrui/cabinet-displays` (class `CabinetDisplaysPage`), registered by `Launcher::OnPlayerCreated` →
  `RegisterPage`, and its "PuP displays" entry in the table's menu, `Launcher::AddHomePageTableItems` → `AddMenuItem`.
- `HasFullDMDScreen` (section 3) reads the pack's `screens.pup` through `PupPackOptions::ReadScreens`.

### `PupPacks` (`src/steamframe/PupPacks.h/.cpp`)

- `GetTableUse(script)`: `usesPup` when the script contains `pinupplayer`; the candidate names, in order: the quoted name after
  `PuPlayer.Init`, the `cPuPPack` values, then every `cGameName` value; `rom` is the first `cGameName` not commented out (scripts keep
  the lines of other versions as comments); `games`, the names given to `.Games("...")` and the other `cGameName` values (used by the
  ROM aliases). Quotes in comments (`'` outside a string, `Rem`) are skipped.
- `ReadTableScript(vpx)`: the `.vbs` next to the table when there is one (it replaces the embedded script), otherwise the `GameData`
  stream of the `.vpx`, as `-ExtractVBS` reads it.
- `FindPackFolder(names, tableFolder, shared)`: for each name, `<tableFolder>/pupvideos/<name>` then `<shared>/<name>`, ignoring case,
  as the plugin's `PUPManager::FindGameDir`.

### `PupPackOptions` (`src/steamframe/PupPackOptions.h/.cpp`, standard library only)

- `Parse(script)`: the commands of a batch file, accepted only from the safe subset of section 4 and with paths inside the pack
  (`PackRelative`). A batch file with any other command is listed as unsupported, with the reason: `EditThisPuPPack.bat`, which many
  packs have, runs PinUP Player's editor and is one.
- `Apply(pack, name)`: the steps are run; the first time, every file a step overwrites or deletes is saved in `.options-backup`
  (`Backup`), and a file a step creates is recorded as absent (`BackupAbsent`), so that `Restore` puts back the pack as uploaded. The
  applied name is written in `.options-applied` (`GetApplied`).
- `List(pack)`: the batch files, sorted, with `supported`, `reason` and `recommended` (the scoring of section 4: `IsVrName`,
  `CabinetDisplayScore` from the `screens.pup` each layout ends with, `ScreensFileOf`, `CabinetScore` for the name). `HasPupFiles`
  tells a pack that already plays as uploaded.
- `ApplyRecommended(pack)`: applies the recommended layout when there is exactly one and none was applied yet. Called at import only.
- `IsPack(folder)`: a folder with `screens.pup`, `playlists.pup` or `triggers.pup`, or whose layouts copy them from its sub folders
  (`PackSources`: the pack is then the folder of the batch files, not the folders they copy from).
- `ReadScreens`, `DrawsFullDMD`: section 3.

### Library and import (`src/steamframe/TableLibrary.h/.cpp`)

- **Fields of a table** (`Table`): `usesPup`, `pupPacks` (candidate names), `pupScannedAt`, `rom`, and `hasPupPack` (computed). The
  first four are cached in `table-stats.json` (preferences folder, `~/.local/share/VPinballX/10.8/` on the Frame) with the modification
  date of the table they were read from: the script is read again only when the table changes. `UpdatePupPacks` computes `hasPupPack`
  at every scan, from the folders of the shared pack folder and of each table's `pupvideos` folder.
- **Configuration** (`Config`, filled by `Launcher::GetTableLibrary`): `sharedPupVideosPath` (the `PUPFolder` setting of the plugin, or
  `<tables>/pinupsystem`, plus `/pupvideos`), `readPupUse` (calls `PupPacks`), `applyVrPupOptions` (true; the tests turn it off),
  `romsPath`, and `pinupsystem` in `excludedPaths` (thousands of media files, never a table).
- **Import** (`Rescan`): the archives at the root of the tables folder **and of the shared pack folder** are imported by
  `ImportArchive`, extracted in a temporary folder:
  1. each table folder is moved into the library; its packs that are not in a `pupvideos` folder go to the shared folder
     (`MoveStrayTablePacks`, a bundle with `Jaws/Jaws.vpx` and `Jaws/jaws/screens.pup`), and the packs left in its `pupvideos` folder
     get their layout (`ApplyVrPupOptions`);
  2. the packs left (`MovePupPacks`) go to the shared folder, a pack at the root of the archive being named after the archive;
     `MovePupPack` replaces a pack of the same name ignoring case, then applies its layout;
  3. the ROMs of the bundle go to the ROM folder (`MoveBundleRoms`).

  An archive that brings no table, pack or ROM is left untouched and recorded in `rejected-archives.json` (its path, date and size),
  so it is not extracted again at the next rescan.

### Launcher (`src/steamframe/Launcher.h/.cpp`)

- `SetupSharedPupFolder`, called when the lobby is built, before the plugins are loaded: when the plugin's `[Plugin.PUP] PUPFolder` is
  empty or already is `<tables>/pinupsystem`, it creates `<tables>/pinupsystem/pupvideos` and sets the setting to `<tables>/pinupsystem`
  as a default that is not saved (`Set(..., false)`), reset by `~Launcher`, so it follows the tables folder. A folder the user chose is
  left alone. `GetSharedPupVideosPath` returns `<tables>/pinupsystem/pupvideos`.

### Web (`src/steamframe/WebServerLibrary.cpp`, `src/assets/web/`)

- `GET /pup-options?q=<folder, relative to the tables folder>` answers `{ isPack, applied, options: [{ name, supported, reason,
  recommended }], error }` (`isPack: false` alone for another folder). `POST /pup-options?q=...&option=<name>` applies a layout,
  `POST ...&restore=1` restores the pack; both answer the same JSON, with the error of the operation in `error`, and rescan the library
  (`SetLastUpdate`).
- `GET /tables` gives each table `usesPup`, `pupPack` (first candidate name) and `hasPupPack`.
- `app.js`: `PUP_FOLDER` (`pinupsystem/pupvideos`) and the PuP packs tab (`folder-switch-pup`); in that folder, `updateMissingPups`
  lists the tables with `usesPup` and without `hasPupPack` and shows the help and the Alpha warning (`pup-help`, `pup-alpha`); in a
  pack's folder, `updatePupOptions`, `renderPupOptions`, `applyPupOption` and `restorePupOptions` drive the Pack options panel
  (`pup-options`). Uploads are sent in chunks by the page; a test with `curl` must chunk too.
- `tables.js`: the PuP tag of the tiles (`table-tile-pup`, from `hasPupPack`). `styles.css`: `.pup-options`, `.table-tile-pup`,
  `.alpha-warning`.

### Table picker

`TablePickerPage::BuildPage` sets `m_tileBadge = "PuP"` on the tiles of the tables with `hasPupPack` (`[PuP]` after the name in the
list view); the tile is drawn by `vrui/LegacyTiles.cpp` (classic VR menu) and `vrui/PageAdapter.cpp` (new VR menu).
`TableActionsPage` says "PinUP Player pack installed", or names the missing pack and where to add it.

### Build

`steamframe/steamframe.cmake` lists the new files, and defines `VPX_STEAM_FRAME` for the `PUPPlugin` target too. The plugins are
defined after this file is included (`make/CMakeLists_plugins.txt`), so that is done by a call deferred to the end of the root folder
(`cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL steamframe_plugin_definitions)`).

## 9. Changes to the upstream PUP plugin

Generic fixes, not guarded (except the default of `HardwareDecoding`), kept as their own commits so that they can be proposed to
upstream; none was proposed yet (Oct 24, 2026). Commits are named by their subject, as their hashes change at each sync.

| Change | Files | Commit |
|---|---|---|
| `GetRoot` ends with a separator before a pack is loaded, as PinUP Player's `C:\PinUPSystem\PUPVideos\` (scripts append the pack to it) | `PUPPinDisplay.cpp` | "pup: GetRoot ends with a path separator before a pack is loaded, as PinUP Player's" |
| Label images whose caption starts with a separator (`\\Heros\\icon.png`) are relative to the pack, not absolute | `PUPLabel.cpp` | `PUP: label images whose caption starts with a separator ('\\Heros\\icon.png')` |
| Hardware decoding: `HardwareDecoding` setting, V4L2 decoders found by name (`avcodec_find_decoder_by_name`, optional in `LibAv.h`), a new decoder at each loop, software fallback on rejection | `PUPMediaPlayer.h/.cpp`, `PUPPlugin.cpp`, `LibAv.h` | "PUP: hardware video decoding (V4L2 memory to memory decoders)" |
| Hardware decoding on by default in the fork (`VPX_STEAM_FRAME`) | `PUPPlugin.cpp` | "Steam Frame: hardware decoding of the PinUP Player videos by default" (fork only) |
| A background of a higher priority replaces a looping main media (`BG REPLACES LOOP` in the debug log): a looping media never ends, so the background never came back. The Austin Powers intro (D0, Loop, priority 1) gives way to the game background (W54, SetBG, priority 2); Darkest Dungeon's side mode loops end with its 'Reset BG' (SetBG) | `PUPMediaManager.cpp` | "pup: a background of a higher priority replaces a looping media" |
| Stall recovery of the hardware decoders (section 5) | `PUPMediaPlayer.h/.cpp` | "PUP: a blocked hardware decoder is stopped, its video goes on in software" |

The canvas of 960 pixels is the fork's (`CabinetDisplays::GetCanvasSize`), not a plugin change: with 1280 pixels, VPX used 1.63 cores on
Darkest Dungeon, 1.37 with 960 (CPU 77 → 71 °C).

## 10. Building, testing and deploying

**On the Mac.** `cmake --build build`, then a settings folder of its own (`-PrefPath <folder>/`, a `VPinballX.ini` with `[Standalone]
WebServerAlwaysOn = 1`, `WebServerPairing = 0`, `WebServerPort = 2112`) and `-Launcher`. The web page is then on
`http://localhost:2112/vpx.html#pinupsystem/pupvideos`. The surfaces need VR: `VPX_CABINET_DISPLAYS=1` creates them on the desktop. A
test pack can be made from any ROM table: a folder `pinupsystem/pupvideos/<cGameName>` with a `screens.pup`, `playlists.pup`,
`triggers.pup` and videos made with `ffmpeg -f lavfi -i testsrc=...`. After editing only web files, copy `src/assets` into
`build/VPinballX_BGFX.app/Contents/Resources/assets` (the build copies them only when it links). Screenshots of the VPX window fail
while the Mac display sleeps; Chrome screenshots of the web page work.

**For the Frame.** `Doc/steam-frame-build/build-linux-arm64.sh` (`VPX_SRC`, `VPX_LINUX` and `VPX_VOLUMES` choose the checkout, its
Linux copy and the Docker volumes; `app` then `package` rebuild only the application). The regular developer install is
`Doc/steam-frame-build/deploy-to-frame.sh`. A second install, to test PuP changes without touching it, was used during the work:

```sh
H=Doc/steam-frame-build
PACKAGE_DIR="$PWD/steam-frame-package" ART_DIR="$PWD/$H/library-art" DEVKIT_DIR="$PWD/steamos-devkit/client/devkit-utils" \
   ART_TOOL="$PWD/$H/install-library-art.py" NAME_TOOL="$PWD/$H/set-library-name.py" \
   bash $H/release/install.sh --gameid vpx_pup --name 'Visual Pinball X - Steam Frame Edition (PuP test)' steamos@frame.local
```

It installs into `~/devkit-game/vpx_pup` on the Frame, with its own entry in the Steam library; the tables, packs and settings are
shared with the other install. Check the `sha1sum` of `VPinballX_BGFX` and `plugins/pup/plugin-pup.so` there against the package.

**Unit tests**: section 6. The PuP cases of `tests/test-table-library.cpp`: packs imported to the shared folder; a bundle keeping the
pack of its table, and a bundle with only a pack; whether a table's pack is installed; batch files applied without running them;
the VR layout applied at import; a pack with its `.pup` files in its option folders only; the layout using the most displays; between
layouts using as many, the one using the topper and the backglass (with `DrawsFullDMD`); packs left as uploaded when asked; the ROMs
and packs of bundles going where PinMAME and PuP look for them.

**Checking a pack's choice without the Frame**: copy its `.bat` and `.pup` files (not the media), keeping the folders, and run
`PupPackOptions::List` on it from a small program compiled like the tests.

## 11. Packs and tables on the Frame (Oct 24, 2026)

Packs in `~/Documents/VPinballX/Tables/pinupsystem/pupvideos`:

| Pack | Applied layout | In the headset |
|---|---|---|
| `DarkestDungeon` | 3 Screen - VR | Works: backglass on `PinCab_Backglass`, topper on `PinCab_Backbox`; measured unworn (`docs/Steam Frame Branch.md`, section 15) |
| `austin` | Option 3 - BG Topper Separate | Works. The VR table names `austinVR`, a small pack left zipped in the table's folder: while it is absent, the plugin starts `austin` from the ROM name; extracted, it would win |
| `TheMatrix` | 4 Screen - Topper DMD Separate | Works (section 7) |
| `alienspupver` | none (imported before the layout rules) | Not reported. Its PinMAME alias `alienspupver,sorcr_l1` and `sorcr_l1.zip` are in place (`docs/PinMAME ROM Aliases.md`) |
| `futurama` | Option 3 - Single Screen - Desktop - VR | Not reported |
| `scarface` | 1 - Desktop-VR | Not reported; no table of the library asks for it |
| `BladeRunner2049` | none: no batch file, played as uploaded | Not reported (the `qcom-iris` error of section 12 was seen with it) |
| `5th_Element_VR` | none: only `EditThisPuPPack.bat` | Not reported |
| `TopGun` | none: only `EditThisPuPPack.bat` | Not reported |

Tables of the library whose script uses PuP and whose pack is not on the Frame: BleachVR (`Bleachvr`), JAWS 50th Anniversary
(`JAWS_50Th_Anniversary`), John Wick (`johnwick`), Masters of the Universe (`motu`), One Piece (`onepiece`), StarCraft (`startrp2sc`).
The library's view of every table is in `table-stats.json` (`usesPup`, `pupPacks`).

## 12. Open points and next steps

- Why the iris decoder stops answering is not known: no kernel message accompanies it. Blade Runner 2049 logged `qcom-iris
  aa00000.video-codec: session error received 0x4000003: fatal error` once, without any visible effect.
- The surfaces are placed before the script runs: a table moving its VR DMD at init (The Matrix) gets its PuP DMD under the backglass.
  Placing them again after the script's init (or at the first frame PuP draws) would fix it.
- Packs imported before a change of the layout rules keep their files until a layout is applied from the Pack options panel.
- Aliens: the pack on the Frame has no layout applied (imported before packs without one always got it): apply one, or upload it again.
- `PuPlayer.B2SInit` is not read for the pack name (section 4).
- The packs of section 11 marked "Not reported", and the tables without their pack, are not tested in the headset yet.
- Propose the plugin changes of section 9 to upstream (drafted for the owner's review first).
- VP9 videos with alpha need libvpx, which the Frame build does not have: FFmpeg's own VP9 decoder ignores the alpha layer, so such
  videos play without their transparency.
- A report of the table picker's Refresh not showing a newly imported table until a restart (Aliens) was not reproduced: the library
  had the table.
