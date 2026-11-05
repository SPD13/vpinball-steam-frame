# Cabinet launcher

Branch `cabinet-launcher` of the fork (not merged into `master`): a start screen for a pinball cabinet, made of the same table library
as the Steam Frame Edition, driven by the cabinet's buttons. It runs in the desktop standalone builds (macOS, Linux, windows-mingw).

```
VPinballX_BGFX -Cabinet
```

`-Cabinet` is the launcher mode of `-Launcher` with another start screen: tables are played in the same process, closing a table gets
back to the launcher, and the web server, the scores, the table picker and the backups work as in the Steam Frame Edition. A table
library can be copied from a Frame, or restored from one of its backups, as it is.

## What it shows

- **Playfield screen**: a reel of the tables at the bottom, in the order of their names, the selected table magnified in the middle like
  through a magnifying glass (the tiles swell as they slide to the middle and shrink as they leave it). Its name, how many times and
  when it was played are written above the reel. Behind, the whole screen shows a picture of its playfield.
- **Backglass screen**: the backglass of the selected table.
- **Options band**, under the reel:
  - **Search**: a keyboard driven by the buttons; the reel shows the tables matching what is typed, the best match first, while typing.
  - **Options**: the full table picker of the Steam Frame Edition (tabs, favorites, scores, Wi-Fi upload, library and application
    settings). Back returns to the reel.
  - **Quit**: leaves Visual Pinball.
  - **Shutdown**: asks for a confirmation, then turns the computer off once Visual Pinball has closed (`shutdown /s` on Windows,
    `systemctl poweroff` on Linux, System Events on macOS).

## Buttons

| Button | Default key | In the reel | In the options band |
|---|---|---|---|
| Left / right flipper (UI Left / UI Right) | Left / right Shift | Previous / next table; held, faster and faster | Previous / next option |
| Start | 1 | Play the table | Select the option |
| Launch Ball | Enter | To the options band | Back to the reel |
| Left / right magna-save (UI Up / UI Down) | Left / right Ctrl | Down: to the options band; Up: back to the keyboard while searching | Up: back to the reel |
| Credit / Exit (Back) | 5 / Escape | Closes the keyboard, then clears the search | Back to the reel |

The launcher is never closed with Back: it is the start screen. A mouse works too (click a table to select it, click it again to play
it; the wheel scrolls the reel).

## Screens

The playfield screen is set up as for playing in a cabinet: `Player.BGSet` = Cabinet, the playfield window on the playfield screen.
The launcher turns with the playfield like the tables do (window layout: a landscape screen shows the playfield turned by 270 degrees;
the global `ViewCabRotation` override of the cabinet view, when set, applies too). The backglass picture is drawn on the backglass
window when `Backglass.BackglassOutput` is Floating (a window of its own); B2S, PuP and the DMD take the screens back when a table
starts.

## Pictures of the tables

| Picture | File, next to the table | Made |
|---|---|---|
| Tile of the reel | `<table>.png` or `.jpg` | The image of the library: the thumbnail uploaded from a browser, or the capture taken when the table was first closed |
| Background | `<table>.playfield.jpg` | Captured when the table is closed in cabinet mode and has none: the playfield window, as it was |
| Backglass | `<table>.backglass.png` or `.jpg` | Extracted once from the table's `.directb2s` (its main image, as stored), when the table is selected; for the tables without one, the backglass window captured when the table is closed, if something was drawn on it |

Until a table has been played on the cabinet, its background is its tile image, enlarged and dimmed. The pictures are deleted with the
table. Replace one by deleting it: it is made again.

## Files

```
src/steamframe/CabinetLauncherPage.h, .cpp   the start screen (in-game UI page 'cabinet/launcher')
src/steamframe/CabinetBackglass.h, .cpp      pictures of the tables, .directb2s extraction, backglass window renderer
src/steamframe/TableImageCache.h, .cpp       images as ImGui textures: decoded on worker threads, uploaded one per frame, released beyond a budget
src/steamframe/Launcher.h, .cpp              cabinet mode: lobby without a room, the launcher opened in it, quit, shutdown
src/steamframe/PlayerTableImage.cpp          captures of the playfield and backglass windows when a table closes
src/core/AppCommands.cpp                     '-Cabinet' option (guarded VPX_STEAM_FRAME)
src/core/player.h                            CaptureTableImages (several windows), in the fork's block
src/ui/live/ingameui/InGameUIPage.h          HandleBack, UpdateOpenAnimation (guarded VPX_STEAM_FRAME)
src/ui/live/ingameui/InGameUI.cpp            Back offered to the page first (guarded VPX_STEAM_FRAME)
src/steamframe/TableLibrary.cpp              the pictures are deleted with their table
```

`VPX_CABINET_SHUTDOWN_DRYRUN=1` logs the shutdown command instead of running it, for tests.

## Status and known limits (2026-11-04)

Tested on macOS (arm64) with a copy of the Frame library (25 tables), the playfield window 1600x900 (the UI turned by 270 degrees) and a
floating backglass window, driven by a scripted test hook (not committed): the reel, its magnification while scrolling, held flippers,
wrapping, the options band, Search with the button keyboard (live filtering, Back to browse the results, clear), Options to the table picker
and Back, Shutdown with Cancel and with Yes (dry run: the command is logged, the application closes), Quit, a table played and closed (back on
the launcher, its playfield picture captured), the backglass extracted from a .directb2s and shown on the backglass window.

Not tested yet: Windows (windows-mingw build), a real cabinet (two screens, cabinet buttons), the real shutdown.

- Screenshots fail on HDR displays: with the window on an HDR10 display (a MacBook Pro screen), the back buffer is RGB10A2 and
  `RenderDevice::OnScreenshotCaptured` only saves 8-bit formats, so no picture is captured when a table closes (this also applies to the
  table images of the existing launcher). Cabinet screens are SDR.
- The upright copy of a cabinet capture used as the library image (`Cabinet::SaveUpright`) could not be checked on this Mac for that reason.
- Options shows the table picker over the empty lobby (grey), not over the launcher's picture.
- `.directb2s` files whose base64 data is wrapped with XML character references (`&#xD;&#xA;`) are handled (seen in Mushroom World).
