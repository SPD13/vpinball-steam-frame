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
lobby), Scores, VR & Room, Display & Performance, Sound, Controls, Plugins, System, Wi-Fi upload, Quit (its
button is on the screen, so that moving down the rail never quits). A screen shows a page (upstream's, like
`GraphicSettingsPage`, or one of the fork) inside the menu; the pages it opens (sub-pages) are shown in the same frame, with a Back button.
The routes `homepage` and `tables/picker` give this menu when the new view is active, and the classic pages otherwise (`VRUI::OnPlayerCreated`).

- Pointer (VR controller, mouse): click the rail, the items, the Back/Close and Defaults/Undo/Save buttons.
- Buttons (d-pad): on the rail, up/down change the screen and right enters it; in a screen, up/down select an item and left/right adjust it,
  left on an item which does not use it (link, action, text) gets back to the rail.
- Sizes are angles (`Theme.h`): in VR they keep the same apparent size at any menu distance (`Standalone.VRMenuDistance`); on the desktop
  the menu fits the window. In VR, glyphs are rasterized at the density of the eye views (`Theme::ApplyPixelDensity`).
- Text is typed with the keyboard of the menu in VR (`Keyboard.cpp`): below the menu, it edits the clicked field (search of the Tables
  screen, text settings, names) and updates it at each key.

## Screens made of several pages

Some screens group options of several pages (`ComposedPage`, layout in `GetSections` of `Screens.cpp`): VR & Room (position, cabinet and
room options of the VR settings, menu distance, lobby room), Display & Performance (VR rendering options of the VR settings, then the
graphics settings), Sound (sound settings, spatial audio of the VR settings), Controls (input settings, nudge/plunger/cabinet pages), This
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
sequence, a search typed with the keyboard, a value changed then undone on a composed screen, the page of a table and a sub-page, then
quits (`SelfTest.cpp`):

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
