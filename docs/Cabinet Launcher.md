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
    settings). Cancel returns to the reel.
  - **Wi-Fi: On / Off**: switches Wi-Fi upload (adding and managing tables from a browser). On lasts until Visual Pinball is closed
    (the "ask every time" mode of the Wi-Fi upload screen; switching it here also clears "always on"); when it was started always on,
    it shows On. While it is on, a badge in the top right corner gives the address to open and the pairing code.
  - **Quit**: leaves Visual Pinball.
  - **Shutdown**: asks for a confirmation, then turns the computer off once Visual Pinball has closed (`shutdown /s` on Windows,
    `systemctl poweroff` on Linux, System Events on macOS).

## Buttons

The menus use the UI actions of the input settings (Settings, Controls), which a cabinet maps to buttons of its own, the other
buttons staying free for the tables:

| Button | Default key | In the reel | In the options band |
|---|---|---|---|
| UI: Next Item | Left Ctrl | Next table; held, faster and faster | Next option |
| UI: Previous Item | Right Ctrl | Previous table; held, faster and faster | Previous option |
| UI: Confirm | Right Shift | Play the table | Select the option |
| UI: Cancel | Left Shift | To the options band; while searching, clears the search first | Back to the reel |
| UI: Up / UI: Down | Up / Down arrow | Down: to the options band | Up: back to the reel |

The menus only listen to these actions, in VR too: Start, Credit, Launch Ball, Exit Game (Escape) and Toggle InGame UI (F12, which
still opens the menu while a table is played) do nothing in them. To go back with Escape, map it to UI: Cancel as well (a key mapped to two
actions with the same use counts once). The default keys of the UI actions are the ones of the flippers and magna-saves: a keyboard cabinet
without UI buttons navigates with them.

In the menu opened by Options, the buttons navigate on two levels. The menu opens with the focus on the left menu (one icon per screen);
Confirm enters the selected screen, Cancel gets back to the left menu:

| Button | In the left menu | In a screen | Editing a value |
|---|---|---|---|
| UI: Next Item / UI: Previous Item | Next / previous screen | Next / previous item (tiles in reading order) | Increase / decrease it (held: faster) |
| UI: Confirm | Enters the screen | Selects the item: opens a page, flips a switch, defines an input mapping, starts editing a slider or a choice | Keeps the value |
| UI: Cancel | Back to the reel | Back to the left menu | Restores the value it had |
| UI: Up / UI: Down | Previous / next screen | The tile above / below in a grid, otherwise the previous / next item | Increase / decrease it |

UI: Up and UI: Down are for a full D-pad: in the cycles of the menu (the left menu, the tab row, the lists) they do what Previous and Next
do, in a grid (the tables, the tiles, the on-screen keyboard) they move by rows, and in the launcher they go between the reel and the
options band.

A slider or a choice (a list of values) is edited in two steps: Confirm on it frames its row, Next/Previous then change the value, Confirm
keeps it, Cancel restores it.

The Tables screen adds a level: Next/Previous go through its tab row (All, Recent, Favorites...), the icon buttons at its end, the search
field, the button clearing the search and the letter filter, and Confirm gets to the first table of the list (on an icon button, Confirm
uses it; on the search field, Confirm opens the keyboard of the menu to type, and Done or Cancel closes it and gets to the first table
found; on the letter filter, Confirm edits the letter). In the grid, Next/Previous go from table to table in reading order. Cancel gets
from the tables to the tab row (or to the search field, when they were reached from it), then to the left menu. On a cabinet the menu's
on-screen keyboard is used, as in VR: Next/Previous go through its keys in reading order, Confirm presses one, Cancel closes it.

The same applies in VR when the buttons navigate (a pinball controller, the sticks and the D-pad of the VR controllers, whose up, down,
left and right are Next, Previous, Cancel and Confirm). While pointing, Cancel closes the menu at once, and a click in a screen or on an
icon moves the buttons' focus there.

The launcher is never closed with Cancel: it is the start screen. A mouse works too (click a table to select it, click it again to play
it; the wheel scrolls the reel).

## Screens

At the first start in cabinet mode, when no screen is chosen yet for the playfield or the backglass (`Player.PlayfieldDisplay`,
`Backglass.BackglassDisplay` empty) and there are at least two screens, the playfield goes on the largest screen (in pixels, the
primary one on a tie) and the backglass is turned on, borderless full screen, on the next largest (`SetupCabinetScreens` in
`Launcher.cpp`, saved once). The screens are then changed in the menu: Options, then Screens on the left (outside VR only), with one
page per window (playfield, backglass, score view, topper): on or off, screen, display mode, rotation, size and position. A change of
the playfield's screen or display mode applies at the next start.

The playfield screen is set up as for playing in a cabinet: `Player.BGSet` = Cabinet, the playfield window on the playfield screen.
The launcher turns with the playfield like the tables do (window layout: a landscape screen shows the playfield turned by 270 degrees;
the global `ViewCabRotation` override of the cabinet view, when set, applies too). The backglass picture is drawn on the backglass
window when `Backglass.BackglassOutput` is Floating (a window of its own); B2S, PuP and the DMD take the screens back when a table
starts.

## Performance panel

The performance panel of the VR build also works outside VR (Options, VPX Performance, Performance panel; `Standalone.PerfPanel`). It
rates the table every 30 seconds against the playfield window: its resolution at its refresh rate, or at the frame rate cap when that is
lower. Green: rendered at the window's resolution (or above, with supersampling) and on time; yellow: on time below it (`AAFactor` under
1); red: frames missed, "GPU bound" when the GPU time was over 90 % of the frame period. The frames come from the desktop render loop
after its present and its cap (`PerfPanel::OnDesktopFrame`, from `RenderDevice::BGFXDesktopRenderLoop`): the display periods are counted
from the time between two frames, the GPU time is bgfx's. The rating of a whole play is kept for the table page, as in VR.

The panel is laid out wide (1280 pixels, rating and details left, graphs right) with a see-through background, as high as the shape of
its space makes it (the graphs share the height) or as its content needs, then scaled to fit that space whole. It is drawn over the
backglass window when the backglass is a window of its own (fitted into it, turned like its content; a backglass window that nothing
else draws is shown for the panel, on black), else over the playfield, top left, under the menus. A score view (DMD) or topper window
placed over the backglass window keeps the panel out of its way: the panel is fitted into the largest band of the backglass left free
(above, below, left or right of them), or drawn over the playfield when no band is a quarter of the window.

A window output (DMD, backglass, topper) switched to Window in the Screens menu with no display chosen yet opens on another display than
the playfield's (the backglass's when it is a window), shrunk to fit it, and the playfield keeps the focus (`DisplaySettingsPage`,
guarded `VPX_STEAM_FRAME`): on the default display it covered the playfield and the menu before its Display choice could be reached. Not shown outside Linux: GPU busy
time, CPU use per thread, clocks and temperatures (read from `/proc` and `/sys`). The resolution graph is shown with dynamic resolution on.

## Dynamic resolution

Outside VR too (Options, Performance, Device Performance, Dynamic resolution; the settings of VR: `PlayerVR.DynamicResolution`, on in the
Low and Medium profiles, `DynamicResolutionTarget`, `DynamicResolutionMinScale`; not with stereo 3D). The scene is rendered into the top left
part of its buffers, as in VR (`RenderDevice::BeginScaledRendering`), and stretched to the window by the upscale pass of the post-processing
(`Renderer::NeedsUpscaling`, a filtered copy), the menus and the backglass staying at full size. The scale follows the GPU time of bgfx
(the whole frame, all windows), against the target share of the frame period (`src/steamframe/DesktopDynamicResolution.cpp`).

Unlike VR, the desktop draws the parts of the table that never move once, accumulated over 128 frames (the static prepass), which every frame
starts from: drawn at the scale of the frames, it is drawn again when the scale changes, a couple of seconds of heavier frames (and a hitch when
the table has ambient occlusion, computed again). So the scale moves by steps of 10 %, rarely: down after two seconds over the target (as many
steps as the GPU time is expected to need, three quarters of it following the pixels), up one step after a few seconds with room for it, the
wait doubling each time a rise is followed by a drop within 20 seconds (up to a minute). The readings are left out for the first 15 seconds of
a table (its first frames are heavier) and while the prepass is accumulated. Changes are logged (`Desktop dynamic resolution: 100 % -> 80 %`).

Made to follow the scale outside VR: the ambient occlusion (its inverse projection maps the rendered part back to the screen), the
tonemapping of the embedded windows without the color grade. The ball motion blur is left out below 100 %. Screen space reflection
(off by default) takes its view direction from the whole screen and is slightly off below 100 %.

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
src/ui/live/ingameui/InGameUIPage.h          HandleBack, StepItem and the edited items, UpdateOpenAnimation (guarded VPX_STEAM_FRAME)
src/ui/live/ingameui/InGameUI.h, .cpp        the four UI actions: Next/Previous (OnUIStep), Confirm, Cancel, the value edited with them (guarded VPX_STEAM_FRAME)
src/input/InputManager.cpp                   the menus only listen to the UI actions; labels UI: Cancel, UI: Confirm; presses only go to a mapping being captured (guarded VPX_STEAM_FRAME)
src/steamframe/vrui/PageAdapter.cpp          default StepItem and editable items, the frame of the edited row
src/steamframe/TableLibrary.cpp              the pictures are deleted with their table
src/steamframe/PerfPanel.h, .cpp             performance panel: desktop frames, the wide overlay, drawn over the backglass or the playfield
src/steamframe/DesktopDynamicResolution.h, .cpp  dynamic resolution outside VR: the scale from bgfx's GPU time
src/renderer/RenderDevice.cpp                desktop render loop gives its frames to the panel and the dynamic resolution (guarded VPX_STEAM_FRAME)
src/renderer/Renderer.cpp                    the scale outside VR, the static prepass drawn again, upscale pass, AO and embedded windows at a scale
src/ui/live/LiveUI.cpp                       the panel outside VR: backglass window, or playfield under the UI (guarded VPX_STEAM_FRAME)
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
