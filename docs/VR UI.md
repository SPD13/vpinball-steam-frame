# VR UI of the Steam Frame Edition

The in-game UI has two views in VR: the **classic** one (upstream's in-game UI, with the fork's VR panel, pointer, tiles and table picker)
and the **new** one (one menu with big icon buttons on the left, its own look). Both draw the same pages and items, and read and write
the same settings: switching between them loses nothing.

## Switch

- `Standalone.NewVRUI` (VPinballX.ini): the new view, in VR and on the desktop. On by default. There is no switch in the menus, so that a
  player cannot end up in a view without finding the way back.
- `VPX_VRUI=new` or `VPX_VRUI=classic` (environment variable): overrides the setting, for tests.
- The choice is read once per frame, and not while an item is being edited (slider dragged, text typed).

## How it works

Upstream's in-game UI is a model and a view in one class: each page (`InGameUIPage`) builds a list of items (`InGameUIItem`) in
`BuildPage()`, and `InGameUIPage::Render` draws them. The fork keeps the model and replaces the view:

- `InGameUIPage::Render` calls `RenderVRUI` once its model work is done (page rebuild, result of a path selection dialog, open/close
  animation). When the new view is active, it draws the page and handles its interactions, and upstream's drawing code is skipped.
- `src/steamframe/vrui/PageAdapter.cpp` is the only file using the private members of `InGameUIPage` (it is its friend): an upstream
  change to them breaks the build there, not silently.
- Interactions go through the page's own methods (`SelectNextItem`, `AdjustItem`, `Save`, `ResetToDefaults`...): upstream's behaviour
  changes (value steps, enum wrapping, rebuild rules) apply to both views.
- `Tripwires.cpp` breaks the build when upstream adds a kind of item or property, and the vrui sources build with `-Werror=switch`.

## Rules

- **Features go in the model** (the items of a page, its methods), not in the view: they then appear in both views. Only the look and
  the layout may differ.
- **Fork code which draws itself** (CustomRender items, `Render()` overrides of `TablePickerPage` and `ScoresPage`) keeps a path for the
  classic view when it uses the new view's widgets (`VRUI::IsActive()`).
- **Upstream files** only get guarded hooks (`#if defined(VPX_STEAM_FRAME)`), see `steamframe/README.md`.

## Parity checklist (weekly sync)

`steamframe/tools/upstream-report.sh` tags the upstream commits which touch what the new view mirrors (`vrui-parity`). For each one,
check whether the new view needs the same change:

- [ ] `InGameUIPage::Render`: header actions (Back, Defaults, Undo, Save: order and conditions), selection fix-up, rebuild deferral
      while a slider is dragged, markdown scrolling, window outputs (position, size, hover) used by the pages drawing extra windows.
- [ ] `InGameUIPage::AdjustItem`: handled through the page, nothing to mirror unless a new item type appears.
- [ ] `InGameUIItem`: new fields or types (the tripwires catch new types).
- [ ] `PropertyDef`: new property types or path types (tripwires).
- [ ] Plugin pages (`PluginSettingsPage`, `VPXPluginPagedUI`): new kinds of items.
- [ ] New options of the VR settings (`VRSettingsPage`): shown in 'More' of VR & Room until placed in a section (`VPX_VRUI_DUMP_UNPLACED=1`).
- [ ] Both views in a table and in the lobby (Frame test checklist).

## The new menu

One window with a rail of icon buttons on the left (`MainMenuPage`), each choosing a screen (`Screens.cpp`): Tables, This table (not in the
lobby), Scores, VR & Room, Display & Performance, Steam Frame (its logo is drawn by `Widgets::SteamFrameLogo`: a headset visor in the corners
of a viewfinder, the icon font having no headset), Sound, Controls, Plugins, System, Wi-Fi upload, Quit (its
button is on the screen, so that moving down the rail never quits; during a table it also offers Return to Lobby). A screen shows a page (upstream's, like
`GraphicSettingsPage`, or one of the fork) inside the menu; the pages it opens (sub-pages) are shown in the same frame, with a Back button.
The routes `homepage` and `tables/picker` give this menu when the new view is active, and the classic pages otherwise (`VRUI::OnPlayerCreated`).

- Pointer (VR controller, mouse): click the rail, the items, the Back/Close and Defaults/Undo/Save buttons.
- Buttons (d-pad, or thumbsticks as in upstream's mapping: left stick up/down, right stick left/right): on the rail, up/down change the
  screen and right enters it; in a screen, up/down select an item and left/right adjust it, left on an item which does not use it (link,
  action, text) gets back to the rail. OK (Start button, A on a VR controller) activates the selected item: enters the screen from the rail,
  opens a link or a table, runs an action, flips a switch. Back (Credit button, B; also the Quit and menu buttons) gets back. Save and Undo
  have no button shortcut any more: they are items of the page (at the end of its list) and buttons of its footer.
- In the grid of tables, the buttons move from tile to tile (up/down a row, left/right a column; left from the first column gets back to the
  rail, up from the first row to the letter filter and search) and OK opens the table. The views give the number of columns they draw
  (`InGameUIPage::m_tileColumns`, navigation in `InGameUIPage::StepTileGrid`), so this works in the classic view too.
- On the tab row of the Tables screen, left/right go through the tabs, then the icon buttons at its right end (rescan the tables folder,
  grid or list view, sort order), then back to the first tab; OK uses the focused icon button (`TablePickerPage::StepTab`).
- On the search field, right gets to the button clearing the search (when there is text), left back to the field, and OK on it clears
  the search and closes the keyboard (`TablePickerPage::m_clearFocus`).
- In VR, the menu opens in pointer navigation (hovered items are highlighted). Any button of the UI (d-pad, sticks, Save, Undo, Defaults,
  Back) switches to button navigation, the selected item being highlighted: the first press of a direction only shows it. A trigger press
  switches back without clicking (the next one clicks); aiming or scrolling does not. The ray and the dot of the pointer are hidden in
  button navigation. The thumbstick of the pointing controller scrolls in pointer navigation only,
  and not until it gets back to its center after a navigation, so that the sticks do not fight the buttons.
- Sizes are angles (`Theme.h`): in VR they are the apparent sizes at 1.5 m (`kVRReferenceDistance`), the layout not depending on
  the menu distance (`Standalone.VRMenuDistance`), which only moves the window; on the desktop the menu fits the window. In VR, glyphs are rasterized at the density of the eye views (`Theme::ApplyPixelDensity`).
- Text is typed with the keyboard of the menu in VR (`Keyboard.cpp`): just below the clicked field, over the rest of the menu until Done closes it, it edits the clicked field (search of the Tables
  screen, text settings, names) and updates it at each key. With the buttons, OK (A) on the field gives the focus to the keyboard
  (`Keyboard::Focus`): the directions then select a key, OK presses it and Back (B) gives the focus back to the menu, the keyboard staying
  opened. `InGameUI` sends each button to the keyboard first (`Keyboard::HandleButton`); a trigger press (pointer) drops the focus.
- Below the menu (and the keyboard when it reaches lower), a badge gives the address and pairing code of the Wi-Fi upload, or that it is off; clicking it opens
  the Wi-Fi upload screen.
- Above the menu, a yellow badge "Update Available - Use the installer" appears when GitHub has a newer release than the one running
  (`UpdateCheck.cpp`). The check runs once per launch, on a worker thread, when the menu is first shown: it reads the `VERSION` file next to
  the executable (written by `make-release.sh`), asks `api.github.com` for the latest release, whose game archive names its version, and
  compares the dates of the two versions (another release of the same day is newer too). The request uses the libcurl of the system,
  loaded with `dlopen` (the Steam Linux Runtime has one): no check without it, nor offline, nor for development builds, which have no
  `VERSION` file. `VPX_UPDATE_CHECK_VERSION=<version>` replaces the installed version, to test. The result is in the log ("Update check").
- On the Frame (`VPX_STEAM_FRAME_HEADSET`), the badge reads "New version available, Click here to install". A click downloads the release's
  `.tar.gz` next to the game folder (`<game>.update`, 1.5 GB free needed), checks it against the SHA-256 GitHub gives, unpacks it with `tar`
  (in the Steam Linux Runtime) and replaces the game folder with its `game/` (the running one becomes `<game>.old`, deleted at the next
  start, as Linux keeps the files of the running game open). Meanwhile the install is modal (`UpdateCheck::IsInstalling`): the menu is
  replaced by a box "Update in progress..." with the progress, and every button action is ignored (`InputManager::OnInputActionStateChanged`,
  and the held buttons in `InGameUI::HandlePageInput`), so that no table can be started nor the menu closed. Once done, the box says the game
  will exit, and its OK button (pointer only) quits. After a failure, the menu comes back and the badge gives the reason, with a retry. The Steam shortcut, its name and the library artwork are not
  changed (the installer app sets them), nor the user data, which is outside the game folder. `VPX_UPDATE_INSTALL_DIR=<folder>` allows the
  install on any system and replaces that folder instead, to test (with `VPX_UPDATE_CHECK_VERSION`). The log lines start with "Update:".

## Screens made of several pages

Some screens group options of several pages (`ComposedPage`, layout in `GetSections` of `Screens.cpp`): VR & Room (menu distance and lobby
room first, with "Choose a lobby room", then the position, cabinet and room options of the VR settings), Display & Performance (the performance panel switch, saved at once, then the refresh rate and render
resolution of the VR settings, then the graphics settings), Steam Frame (the performance features of the fork: dynamic resolution and
foveated rendering, with its tone mapping and anti-aliasing switches, from the VR settings, then the simplified meshes: switch, allowed error and status, from a page of the fork saved for
the table or for all), Sound (sound settings, spatial audio of the VR settings), Controls (input settings, nudge/plunger/cabinet pages), This
table (its actions, then its lighting and difficulty options).

- The pages stay whole and alive: they build their items and keep their rules (rebuilds, Open/Close, ResetToDefaults). A section only
  chooses which items it shows, by property (`Prop(Settings::m_prop...)`) or by label.
- **Nothing is lost when upstream adds an option**: a 'rest' section shows the items of a page which no section lists, on any screen
  (the VR settings: the 'More' section of VR & Room). `VPX_VRUI_DUMP_UNPLACED=1` logs them, to place them in a section.
- Defaults/Undo/Save apply to the shown items. Defaults use the rules of the page (`Section::pageRules`), except where only some of its
  items are shown elsewhere (VR rendering, spatial audio: the VR settings recenter the table when reset).

## Testing without a headset

On macOS (or any desktop build), `VPX_VRUI=new` shows the new menu in the window (the mouse is the pointer, the keys of the in-game UI the
d-pad), `VPX_VRUI_KEYBOARD=1` types with the keyboard of the menu as in VR. `VPX_VRUI_SHOTS=<folder>` captures every screen, a d-pad
sequence, a search typed with the keyboard, a value changed then undone on a composed screen, the end of the list of tables (selected with
the buttons), the page of a table, a sub-page, and a name typed with the buttons on the keyboard, then quits (`SelfTest.cpp`). Each capture logs the time taken to draw the menu since the
previous one: a large library is checked with a tables folder of empty `.vpx` files and their images (cloned with `cp -c` on macOS).

```
VPX_VRUI=new VPX_VRUI_KEYBOARD=1 VPX_VRUI_SHOTS=/tmp/shots VPinballX_BGFX -PrefPath <folder> -Launcher
VPX_VRUI=new VPX_VRUI_SHOTS=/tmp/shots VPinballX_BGFX -PrefPath <folder> -play <table.vpx>
```

What only the headset shows: the panel placement, the controller ray, legibility at distance, the cost on the GPU, and the sections of
the VR settings (the VR settings page needs a VR device).

## Files

| File | Contents |
|---|---|
| `steamframe/vrui.cmake` | Sources of the VR UI, included by `steamframe/steamframe.cmake` |
| `src/steamframe/vrui/VRUI.*` | The switch |
| `src/steamframe/vrui/PageAdapter.cpp` | Bridge between the pages and the new view |
| `src/steamframe/vrui/Tripwires.cpp` | Build checks against upstream changes to the item model |
| `src/steamframe/vrui/PageView.h` | What the shell asks a page (title, content, actions), implemented in `PageAdapter.cpp` |
| `src/steamframe/vrui/Shell.*` | The frame of the menu: rail, header, content region, footer |
| `src/steamframe/vrui/MainMenuPage.*` | The main menu: screens, focus of the buttons between the rail and the content |
| `src/steamframe/vrui/Screens.*` | The screens of the rail, their layout (sections), and the pages of the fork they show |
| `src/steamframe/vrui/ComposedPage.*` | Screens made of sections of several pages |
| `src/steamframe/vrui/Keyboard.*` | Keyboard of the menu |
| `src/steamframe/vrui/Theme.*` | Sizes (angles), colors, fonts |
| `src/steamframe/vrui/Widgets.*` | Rail buttons, pills, sliders, toggles, steppers (the only file using `imgui_internal.h`) |
| `src/steamframe/vrui/SelfTest.*` | Screenshots of the menu (`VPX_VRUI_SHOTS`) |
| `src/steamframe/vrui/LegacyTiles.cpp` | Tiles of the classic view (`InGameUIPage::RenderTile`) |
| `src/steamframe/vrui/Panel.cpp` | VR panel standing in the room, and controller pointing (`VRDevice` members) |
| `src/steamframe/vrui/Pointer.cpp` | The controller as a mouse for ImGui, its dot and ray (`LiveUI` members) |
