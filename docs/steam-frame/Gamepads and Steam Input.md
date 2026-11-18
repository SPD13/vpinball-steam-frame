# Gamepads and Steam Input on the Steam Frame

On the Steam Frame, VPX reads gamepads directly, including those Steam Input takes over, because Steam does not deliver their input to
VPX while it runs in the headset. This document records why, how it was found, how VPX works around it, and where to look if gamepads
stop working again. Code: `src/steamframe/SteamInputGamepads.h/.cpp`, called from `SDLInputHandler`'s constructor
(`src/input/SDLInputHandler.h`) before SDL starts its gamepads, in the Frame build only (`VPX_STEAM_FRAME_HEADSET`).

## 1. Symptom (2026-11-17)

A PinOne pinball controller paired over Bluetooth in its Xbox mode (it presents itself as an Xbox Series controller, `045e:0b13`, named
"Xbox Wireless Controller") showed in VPX's Controllers menu, but mapping a flipper button detected nothing. Setting Steam Input to
*Disabled* for VPX in the Steam library made it work at once.

## 2. How Steam Input reaches a game

- Steam opens the physical controllers it supports (Xbox, PlayStation, Switch, Steam controllers, and anything using their ids) through
  its own HID drivers, and creates for each a virtual pad, a uinput device named `Microsoft X-Box 360 pad N`, vendor Valve `0x28de`,
  product `0x11ff`.
- It launches the game with `SDL_GAMECONTROLLER_IGNORE_DEVICES`, the list of the vendor/product ids it took over (791 entries on the
  Frame, `0x045e/0x0b13` among them), so that SDL skips the physical devices and the game sees only the virtual pads.
- Steam feeds a virtual pad only while it holds the game as the focused one. Otherwise the controller drives Steam's own interface
  (app 769). No Steamworks API is involved: a game reading gamepads through SDL gets the virtual pads like any other gamepad. The Steam
  Input API (`ISteamInput`) is an alternative for games that want action sets, and depends on the same focus.

## 3. What happens on the Frame

With VPX running in the headset and Steam Input on (its default):

| Where | What was seen | How |
|---|---|---|
| PinOne's kernel device (`event6`, `Bus=0005`) | every flipper press and release (`BTN_TL`, `BTN_TR`) | evdev reader, below |
| Steam's decoding | every press: controller 1, buttons 30 and 31 | `SteamClient.Input.RegisterForControllerInputMessages`, below |
| Steam's virtual pads (`event5`, `event7`, the only pads VPX had open) | nothing, not even the plunger axis | evdev reader |
| gamescope | `GAMESCOPE_FOCUSABLE_APPS` = VPX's app id, `GAMESCOPE_FOCUSED_APP` and `GAMESCOPE_FOCUSED_WINDOW` empty, no `GAMESCOPECTRL_BASELAYER_APPID` | `xprop -root` on `:0` |
| Steam's controller log | `Queueing activation for controller: 1 app: 769` only, never VPX's app id, while VPX ran | `~/.local/share/Steam/logs/controller.txt` |

So Steam decodes the controller correctly but routes it to its own interface: it never treats VPX as the focused game. The firmware, the
Bluetooth link and Steam's driver are fine; VPX's configuration in Steam was the standard *Gamepad* template.

Why Steam does not focus VPX is not established. gamescope runs as a SteamVR overlay (`--backend openvr`), and VPX draws to the headset
through OpenXR. Its desktop window ("Visual Pinball Desktop Display", 1920x1080 on gamescope's X server `:1`) is mapped and is that
server's active window, but is never shown in the headset (the desktop mirror is forced off on the Frame). Two candidates: Steam only
brings a game to the front through gamescope when its window is shown (game theater), or the Steam VR dashboard keeps the focus for a
title started as a devkit shortcut (compare the "Resume Game" dashboard behavior in VPX's launch from the library). An experiment to tell:
show VPX's window in the game theater from the Steam dashboard, then check `GAMESCOPE_FOCUSED_APP` and the virtual pads again.

## 4. Workaround in VPX

Before `SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)` (the only place VPX starts SDL's joysticks), the Frame build sets
`SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES` to an empty list with `SDL_HINT_OVERRIDE`, which takes precedence over the environment
variable. SDL then opens the physical controllers, as it does when Steam Input is disabled for VPX. The log says so once:
`Steam Input: reading directly the N controller types Steam asked to ignore ...`.

- Steam's virtual pads are left out. SDL gives each the name and the ids of the controller it stands for. Seen on the Frame
  (2026-11-17): the PinOne as `Xbox Series X Controller #1` (`045e:0b13`, VPX UID `SDLJoy_050018dc5e040000130b...`), Steam's pad for it
  as `Xbox Series X Controller #2`, also `045e:0b13`, and the pad of the Frame's own controllers as `Steam Frame Controllers #1`
  (`28de:11e0`). Only their SDL GUID carries the ids of Steam's virtual gamepad, Valve's `28de:11ff` (`SDLJoy_0300....de280000ff11...`),
  so the fork's earlier check (`m_isVRVirtualGamepad`: Valve vendor, or a name with "Steam" or "X-Box 360 pad") missed the first one.
  `SteamInputGamepads::IsSteamVirtualGamepad` tests the GUID, and `SDLInputHandler::OnJoystickAdded` does not open those pads: the
  Controllers menu lists the real controllers only (`Steam Input: virtual pad '...' left out` in the log). Real Valve hardware has other
  product ids (Steam Controller, the Frame's controllers) and is kept.
- Steam's pads saved by earlier builds are dropped when the settings are read. The menu also lists the devices saved in `Input.Devices`
  (disconnected ones included), and a mapping naming an unknown device brings it back as "Unknown Device" (`InputManager::GetDeviceId`).
  Older builds saw only Steam's pads, so accepting a layout for a gamepad saved one, mappings included (seen on the owner's Frame:
  `SDLJoy_0300b836de280000ff11000001000000_1`, "Xbox Series X Controller #2"). `InputManager::LoadDevicesFromSettings` skips them in
  `Input.Devices`, `InputAction::SetMapping` drops every binding that names one (the whole binding, so that a combination never loses
  a member), and `PhysicsSensor::SetMapping` leaves a plunger or nudge axis saved for one unmapped; `Input.Devices` is written back without them when VPX closes. VPX saves a mapping only when it changes, so the
  mappings naming them stay in the file, ignored at every read, until the action is mapped again (on the owner's Frame, after the
  cleanup: `Devices` clean, 21 such entries left in `Mapping.*`). Their `Device.<id>.*` keys stay in the file too, unused.
- Steam keeps the physical controller open as well; both read the same reports (hidraw and evdev readers each get every report).
- Steam settings are left alone: nothing to set per user, and other games keep Steam Input.

Other ways considered:

- *Disable Steam Input for VPX* in the Steam library, or from the installer through the Steam client, as the library name is set
  (`SteamClient.Apps.SetThirdPartyControllerConfiguration(appid, 0)`; the value is
  `appDetailsStore.GetAppDetails(appid).eEnableThirdPartyControllerConfiguration`, saved by Steam as
  `localconfig.vdf` › `apps` › `<appid>` › `UseSteamControllerConfig`, 0 = disabled). Works, but changes a Steam setting.
- *PinOne only*: without its Bluetooth Xbox mode, the PinOne uses its own ids (`39F5:9208`), which Steam does not take over.

## 5. If gamepads stop working on the Frame

1. VPX log: the `Steam Input: reading directly ...` line, then the joysticks `OnJoystickAdded` logs (name, vendor, product). The physical
   controller should be listed with its own ids, and each of Steam's pads logged as left out.
2. The virtual pads are left out, so a Steam update that starts feeding them in VR changes nothing for VPX. If Steam fixes the focus
   (section 3 no longer holds) and the workaround is to go, remove both parts: the emptied ignore list and the virtual pads left out.
3. A controller missing: is it on Steam's list (`tr '\0' '\n' < /proc/<vpx pid>/environ | grep IGNORE_DEVICES`)? Does the kernel see it
   (`/proc/bus/input/devices`), and does it send events (evdev reader)? Which input devices does VPX hold (`ls -l /proc/<pid>/fd | grep
   input`)?
4. An SDL update that renames the hint, or reads the ignore list elsewhere: check `SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES` in
   `SDL_hints.h` and that SDL still reads the list through its hints (an override hint must win over the environment variable).

## 6. Tools used (on the Frame, user `steamos`, which is in the `input` group)

- **evdev reader**: a short Python script reading `struct input_event` (`qqHHi`, 24 bytes) from `/dev/input/eventN` with `select`,
  printing `EV_KEY` (type 1), `EV_MSC` scan codes (type 4) and large `EV_ABS` changes (type 3). No sudo needed.
- **Steam client probe**: Steam opens its CEF debugging port on `127.0.0.1:8080` in Developer Mode; `Doc/steam-frame-build/set-library-name.py`
  has the DevTools client (`DevToolsSocket`, target `SharedJSContext`). `SteamClient.Input.RegisterForControllerInputMessages(cb)` reports
  `[controller index, button, pressed]` as Steam decodes them; keep the log in a `window.` variable and read it with a second call (the
  client's socket times out after 120 s), and unregister the callback.
- **gamescope**: `DISPLAY=:0 xprop -root | grep GAMESCOPE` (focus properties); VPX's window is on `:1` (`xwininfo -root -tree`).
- **Steam controller log**: `~/.local/share/Steam/logs/controller.txt` (binary-safe grep: `grep -a`): driver used per controller,
  config sets, and `Queueing activation for controller: N app: X`.
- **btmon** (Bluetooth, needs sudo): `sudo btmon -w file.btsnoop`, read with `btmon -r file` without sudo.
