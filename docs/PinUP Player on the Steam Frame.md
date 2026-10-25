# PinUP Player (PuP) on the Steam Frame: how it works

Oct 24, 2026 · @SPD13

Technical notes on the PuP support of the fork (branch `pup`): how a PuP table is played in the headset, which code does what, the
rules the fork follows to choose a pack's layout and place its screens, the hardware video decoding and how it recovers from a blocked
decoder, and how to diagnose a PuP table. The user side is in `docs/Steam Frame Branch.md`, section 15.

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
| Library, upload, web (fork) | `src/steamframe/TableLibrary.cpp`, `WebServerLibrary.cpp`, `Launcher.cpp`, `src/assets/web/` | Pack import from uploads, `PuP` badge, `PuP packs` tab, Pack options panel |
| Tests | `tests/test-table-library.cpp` | Imports, layouts, `screens.pup` rules (compiled alone, see section 6) |

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
`PupPacks::FindPackFolder` from the names of its script (`PuPlayer.Init`, `B2SInit`, `cPuPPack`, then the ROM `cGameName`, which the
plugin also uses to start a pack by itself).

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

## 8. Open points

- Why the iris decoder stops answering is not known: no kernel message accompanies it. Blade Runner 2049 logged `qcom-iris
  aa00000.video-codec: session error received 0x4000003: fatal error` once, without any visible effect.
- The surfaces are placed before the script runs: a table moving its VR DMD at init (The Matrix) gets its PuP DMD under the backglass.
- Packs imported before a change of the layout rules keep their files until a layout is applied from the Pack options panel.
- Aliens: the pack on the Frame has no `.pup` file at its root (imported before packs without them always got a layout).
