# PinOne Integration in the Steam Frame Edition

The PinOne (Cleveland Software Design, [github.com/philipellisis/PinOne](https://github.com/philipellisis/PinOne)) is a pinball
controller: flipper and magna-save buttons, front buttons, a plunger, an accelerometer for the nudge, and outputs (solenoids,
motors, lights) that DOF (DirectOutput) can drive. This document records how the Steam Frame Edition supports it, by USB and over
Bluetooth, from the pairing to the table's feedback: the changes made in the PinOne's firmware, in libdof and in VPX, why each was
needed, what was checked on the Frame, and what is still open. User instructions are in the README
(`.github/README.md`, "PinOne pinball controller"); the branch document (`docs/Steam Frame Branch.md`, sections 33 to 36) has the
short version.

Work done from 2026-11-16 to 2026-11-19 with the owner's PinOne (a PinOne Mini, USB name *PinOne Ultra*, firmware 3.3.0, ESP32-S3).

## 1. Summary

| Part | What it does | Where | State (2026-11-19) |
|---|---|---|---|
| Bluetooth pairing | Fixes the pairing with Linux hosts (NimBLE bug) | Firmware `fix/ble-client-features` | Works on the Frame; [philipellisis/PinOne#9](https://github.com/philipellisis/PinOne/pull/9) open |
| Steam Input | VPX reads the pads Steam takes over | `src/steamframe/SteamInputGamepads.*` | Fork master, tested |
| Mapping | *Set PinOne Mini mapping and DOF Config* in the controller's page, no layout pop-up | `src/steamframe/ControllerLayouts.*` | Fork master, tested |
| Vibrations | Only the controller that pressed Start vibrates | `src/steamframe/Vibrations.*` | Fork master, tested |
| DOF over Bluetooth | A DOF channel in the Xbox mode, libdof finds it | Firmware `feature/ble-xbox-dof`, `steamframe/patches/libdof` | Fork master, tested on the Frame |
| DOF configuration | Upload of the DOF Config Tool's zip, backups, a shipped PinOne Mini configuration | `TableLibrary::ImportDofConfig`, `Backup.cpp`, `src/steamframe/DofSetup.*`, `assets/dof/pinone-mini` | Fork master, tested |
| DOF or vibrations | A PinOne follows the table's DOF, or the vibrations on tables DOF doesn't know | `Vibrations.*`, `plugins/dof/DOFPlugin.cpp` | Fork master, tested |
| Identity | Tells a PinOne in Xbox mode from a real Xbox pad, lists it as *PinOne (XBox)* | Firmware `feature/ble-xbox-identity`, `src/steamframe/PinOneDevices.*` | Fork master; checked on the Frame (2026-11-19); firmware in [#11](https://github.com/philipellisis/PinOne/pull/11) |
| Plunger over Bluetooth | Sends the USB axis value (rest at 0, quick release), and a first report when the host subscribes | Firmware `fix/ble-plunger-rest` | Flashed, rest checked on the Frame, [philipellisis/PinOne#10](https://github.com/philipellisis/PinOne/pull/10) open |
| Nudge strength | The PinOne layout sets its nudge at 200 %; options to go further | `src/steamframe/ControllerLayouts.cpp`, section 10.5 | Fork master, not tested in a game |
| Nudge speed | Not sent in Xbox mode; a vendor report like DOF is the path if needed | Section 10.3, 10.4 | Owner's decision, not implemented |

## 2. The PinOne's connections

| Connection | Ids | Name | Reports |
|---|---|---|---|
| USB | `39f5:9208` (Cleveland Software Design's USB-IF vendor id), wired | *PinOne Ultra* | Gamepad (29+ buttons, axes X, Y, Z, Rx, Ry), vendor collection `0xFF00`/`0x01` with report 6 (64 bytes) for DOF and the configuration tool, a CDC serial port (firmware upload, legacy serial DOF) |
| Bluetooth, own profile | `39f5:9208`, wireless | the configured device name (*PinOne*) | Keyboard, gamepad, DOF feature report 3 |
| Bluetooth, **Xbox mode** | `045e:0b13` (Xbox Series X, model 1914), version `0x0509` | *Xbox Wireless Controller* | The stock Xbox report map: input 1, rumble output 3; plus, with our firmware, the vendor collection of sections 6 and 8 |

The Xbox mode (a configuration tool switch, applied after a restart) exists so that every host, Windows, Steam and SDL included,
uses the PinOne as a standard gamepad without a driver. It is the mode used on the Frame over Bluetooth. The firmware also maps the
host's rumble to outputs: the strong motor to output 1, the weak one to output 2 (the strong motor is `data[3]` of report 3; the
firmware had them swapped, fixed in `feature/ble-xbox-dof`).

Firmware code: `BleController.cpp/.h` (NimBLE-Arduino 2.3.7, both Bluetooth profiles), `XboxDescriptor.h` (the Xbox report map),
`Communication.cpp/.h` (the DOF and admin frame parser, USB and Bluetooth), `OutputTask.cpp` (the output task, core 0).

## 3. Bluetooth pairing (firmware, PR #9)

**Symptom (2026-11-16).** The PinOne bonded and connected over Bluetooth, but the Frame never created an input device for it (no
`Bus=0005` entry in `/proc/bus/input/devices`, no hidraw), and Steam never saw it.

**Cause.** A btmon trace showed the pairing (Just Works) and the GATT discovery succeed. Then BlueZ always writes the GATT *Client
Supported Features* characteristic (`0x2B29`), and the PinOne answered *Insufficient Authentication* (ATT `0x05`). The kernel then
retried the pairing with MITM, the PinOne refused, and the Frame stopped there: the report map was never read. The PinOne's NimBLE
(NimBLE-Arduino 2.3.7, still in 2.5.2) stores the value a bonded host writes by deleting the stored entry first, and fails the write
when there is none, which is the case right after a new bond: the store's `BLE_HS_ENOENT` (5) goes back to the host as ATT error 5.
NimBLE's master branch (3.0.0-dev) has no such store anymore.

**Fix** (`BleController.cpp`, `onAuthenticationComplete`): write an empty entry for the bonded peer, so that the delete finds one.
The entry must use the peer's identity address type: the Frame has `Privacy = device` and connects from a resolvable private
address, so NimBLE's entry is keyed as `PUBLIC_ID`/`RANDOM_ID` when `getAddress() != getIdAddress()` (only without
`BLE_HOST_BASED_PRIVACY`, off on the ESP32-S3). A first version keyed by the plain address type never matched, and the failed pairing
then made NimBLE delete the bond (`REPEAT_PAIRING`) while the Frame kept its key: every reconnect failed with *PIN or Key Missing*,
and Steam showed the PinOne flipping between connected and disconnected.

**Result.** Paired from the Steam UI on the Frame (2026-11-17): the write succeeds, the report map is read, the kernel creates the
Xbox gamepad and its hidraw node, and it reconnects with its stored key after a power cycle. The workaround is harmless once NimBLE
is fixed. Branch `fix/ble-client-features` (commit `4b602e9`), published as
[philipellisis/PinOne#9](https://github.com/philipellisis/PinOne/pull/9) (open).

Known side issue: after the configuration tool's *Clear bonds*, the PinOne stopped advertising until restarted (cause not found).

## 4. Steam Input

In Xbox mode, Steam takes the PinOne over (its ids are an Xbox pad's), hides the physical device from games
(`SDL_GAMECONTROLLER_IGNORE_DEVICES`) and gives them a virtual pad instead. On the Frame, Steam doesn't feed that virtual pad while VPX
runs in the headset (gamescope never reports VPX as the focused app), so the PinOne was listed but silent. VPX therefore reads the
physical pads itself (the ignore list emptied before SDL starts) and leaves Steam's virtual pads out (GUID `28de:11ff`). Full
account: [Gamepads and Steam Input](Gamepads%20and%20Steam%20Input.md). Fork master `e403dea86`, `3ec1ff457`.

## 5. Mapping

Upstream proposes a device's default layout in a pop-up whenever a controller connects with another mapping, and "yes" erases the
mappings made by hand. For a PinOne in Xbox mode its generic gamepad layout (flippers on the triggers, magna-saves on the shoulder
buttons) is wrong: the PinOne sends its flippers as LB/RB. The fork shows no pop-up anymore (`InputManager::ProcessInput`, every fork
build; the VR controllers still get their layout at once), and the controller's page (`InputDevicePage`, Controls > the controller)
offers the layouts instead:

- **Set default mapping**: upstream's default layout of the device.
- **Set PinOne Mini mapping and DOF Config** (section 9): the PinOne layout, made from the owner's mapping of the Bluetooth PinOne on the
  Frame, on the firmware's default Xbox assignment (`bleButtonMap`).

| PinOne input | Xbox control | USB button | VPX action | Menu action |
|---|---|---|---|---|
| 3, left flipper | LB | 3 | Left flipper, staged left flipper | UI: Previous Item (`UIDown`) |
| 1, right flipper | RB | 1 | Right flipper, staged right flipper | UI: Next Item (`UIUp`) |
| 4, left magna-save | Left trigger | 4 | Left magna-save | UI: Cancel (`UILeft`) |
| 2, right magna-save | Right trigger | 2 | Right magna-save | UI: Cancel (`UILeft`) |
| 6 | A | 6 | Launch ball, Start | UI: Down (`UIRowDown`) |
| 8 | B | 8 | Start | UI: Next Item (`UIUp`) |
| 9, fire button | Start | 9 | Start | UI: Confirm (`UIRight`) |
| 5 | Y | 5 | Insert coin | UI: Up (`UIRowUp`) |
| 7 | X | 7 | (free) | UI: Previous Item (`UIDown`) |
| 29 | View | 29 | Menu | |
| 25 to 28 | D-pad left, right, up, down | 25 to 28 | By USB, PinMAME service buttons 1 to 4 (not in Xbox mode, below) | As upstream's gamepad layout: `UILeft` (Cancel), `UIRight` (Confirm), `UIUp` (Next Item), `UIDown` (Previous Item) |
| Plunger | Right stick Y | Z axis | Plunger (position) | |
| Accelerometer | Left stick | X, Y axes | Nudge (`GamepadNudge`, read as a position, as upstream's gamepad layout), strength 200 % (section 10.5) | |

`ControllerLayouts::OnJoystickAdded` (called at the end of `SDLInputHandler::OnJoystickAdded`) reads the actual bindings of the pad
through SDL's gamepad mapping (`SDL_GetGamepadBindings`), so the layout uses the inputs SDL maps to LB, RB, the triggers and so on, and
keeps them by settings id. On USB, raw button *n* is PinOne input *n* + 1, and the inputs get names (*Left Flipper*, *Plunger*...). The
USB axis order (SDL numbers X, Y, Z, Rx, Ry) has not been checked with the plunger yet. Mappings are global (application settings),
not per table. Fork master `ecd04ed5f`.

**Menu actions on the PinOne's buttons** (owner's choice, 2026-11-19). Besides the D-pad, the layout maps the fork's menu actions
(`InputManager`, `VPX_STEAM_FRAME`) to the PinOne's own buttons, last column: Y and A to *UI: Up* and *UI: Down* (`UIRowUp`,
`UIRowDown`: the row above or below in a grid, otherwise the previous or next item), X and the left flipper to *UI: Previous Item*, B
and the right flipper to *UI: Next Item* (the fork has no left and right menu actions: previous and next take their place), the fire
button to *UI: Confirm*, the magna-saves to *UI: Cancel* (the triggers in Xbox mode past -0.3, like the magna-saves). The X button is
read from SDL's bindings (`SDL_GAMEPAD_BUTTON_WEST`) in Xbox mode, raw button 6 by USB (`PinOneInputs::x`). The game's actions on these
buttons stay: the menu actions only act while a menu is open (`InGameUI`, `m_useFlipperNav`), as on the keyboard, where the flipper and
magna-save keys are also the menu keys. In the fork, the menu button (View) only opens the menu; *UI: Cancel* goes back.

The D-pad keeps upstream's menu actions, whose meaning the fork changed: up is *UI: Next Item* (`UIUp`) and down *UI: Previous Item*
(`UIDown`), left and right *Cancel* and *Confirm*. Y and A give the PinOne an up and down that move as their names say.

**No service buttons on the D-pad in Xbox mode** (owner's decision, 2026-11-19). Upstream's generic gamepad layout
(`SDLInputHandler.h`) maps the D-pad left, down, up and right to the PinMAME service buttons 1 to 4 (Cancel, Down, Up, Enter) and to
the menu navigation, and the PinOne layout did the same. On Addams Family, hard nudges changed the table's volume: the PinOne's tilt
buttons were on and set to its D-pad inputs, so a hard nudge pressed a D-pad direction, and a WPC game takes Up and Down during play as
its volume buttons (and Enter opens its test menu). The PinOne layout in Xbox mode (`PinOneInputs::dpadServiceButtons` false) now maps the
D-pad to the menu navigation only; applying it clears the controller's previous mappings, so the service buttons go away from it. They
stay on the keyboard (7, 8, 9, 0) and can be mapped by hand. The USB layout keeps them (the owner asked for the Xbox controller); its
tilt buttons press the same inputs, so the advice there is to turn them off (VPX simulates the tilt from the nudges, `PlumbHandler`).

The owner's PinOne settings, read over USB with the configuration tool's request (admin `SEND_CONFIG`, answered with a `C,...` line,
then `OFF`): tilt buttons on (`tiltButton` 23), up, down, left and right on raw buttons 25, 24, 26 and 27 (D-pad right, left, up and
down in the Bluetooth map), thresholds 1200 (X) and 2200 (Y); accelerometer *Max* 5000 (X and Y), dead zone 975, sensitivity 0,
velocity off, sensor found at `0x6A`; plunger min 40, mid 326, max 1905 mV, resting dead zone 161 mV, quick release 150 ms. A tilt left
pressed D-pad up (service button 3, *Up*), a tilt right D-pad down (*Down*), the firmware's tilt "up" D-pad right (*Enter*).

## 6. DOF over Bluetooth in Xbox mode

### 6.1 Why

DOF drives cabinet feedback from the table: the ROM's or script's coils, flashers and lamps go through the B2S plugin's state events
(`B2SPluginEventStream`) to the DOF plugin (`plugins/dof/DOFPlugin.cpp`), which hands them to libdof (`DataReceive(type, number,
value)`); libdof maps them to the outputs of the configured controllers with the DOF Config Tool's files. libdof already supported the
PinOne by USB (its vendor report 6). In Xbox mode the PinOne had no DOF channel: its only output was the Xbox rumble report, which VPX
fills with its physics vibrations, not with the table's events. The owner wanted the table's DOF while the PinOne stays an Xbox
controller (decisions, 2026-11-18: a vendor report in the Xbox map; libdof changed as a fork patch and proposed upstream).

### 6.2 Firmware (branch `feature/ble-xbox-dof`, commit `2ff060d`, on top of PR #9; proposed in [#11](https://github.com/philipellisis/PinOne/pull/11))

- `XboxDescriptor.h`: a second top-level collection after the stock Xbox one: usage page `0xFF00`, usage `0x01`, output report 6,
  9 bytes. Same usage page, usage and report id as the USB vendor channel, so libdof finds it the same way. Hosts that don't know it
  ignore it; a real Xbox pad has no `0xFF00` collection.
- `BleController.cpp`: the report is a GATT characteristic written without response. Its callback copies the value (the
  NimBLE value is a copy, kept while it is read) and queues each 9-byte frame (`Communication::queueBleFrame`, a FreeRTOS queue, so
  frames written back to back are not lost in a shared buffer).
- `Communication::communicateBle`, on the output task: applies the queued frames with the same code as USB: bank frames
  `[0, 200+bank, v0..v6]` through `updateOutputsFromPacket` (light show suppressed, values DOF already set skipped) and single output
  frames `[0, 252, output, value]`. Admin (250) and connect (251) frames are refused: there is nothing to reply on over Bluetooth.
- The combined (own) Bluetooth profile's DOF report 3 goes through the same queue and parser now (it had a single overwritten buffer,
  without light show suppression or dedupe), keeping its formats for the configuration tool's *Test Bluetooth Outputs*.
- The rumble motor order fixed (strong motor = `data[3]`).

A host paired before this firmware must **remove the PinOne and pair it again**: hosts cache the report map.

### 6.3 libdof (fork patch `steamframe/patches/libdof/01-pinone-ble-xbox-mode.patch`)

On the pinned libdof (`3d0ad20a`), applied by `platforms/linux-aarch64/external.sh` and `linux-x64/external.sh` after the download
(cache suffix `-pinonexbox1`):

- `PinOneHidCommunication::FindDevices` also enumerates `045e:0b13` and keeps only entries with usage page `0xFF00`, usage `0x01`:
  a real Xbox pad never matches.
- `PinOneHidDevice::GetReportSize`: 10 bytes for the Xbox ids (report id + one 9-byte frame), 64 for USB.
- `PinOne::ConnectToController` takes the ids of the opened path from `hid_get_device_info` (it assumed the USB ids).

The `PinOne` controller class, its 63 outputs and its LedWiz 11 numbering are unchanged, so DOF Config Tool configurations made for
a USB PinOne work over Bluetooth as they are. libdof follows the C# DirectOutput one to one, so an upstream pull request needs the
same change on the C# side (the author's DirectOutput fork, whose HID pull request #27 is still open): not drafted yet.

### 6.4 VPX

- **DOF on by default** in the fork: `Plugin.DOF.Enable` (`PropBoolDyn(PluginDOF, Enable, "DOF Enable", ...)` in
  `Settings_properties.inl`, under `VPX_STEAM_FRAME`), upstream's default is off.
- **Configuration files.** libdof reads the DOF Config Tool's files (`directoutputconfig11.ini` for the PinOne = LedWiz 11,
  `DirectOutputShapes.xml/png`, `tablemappings.xml`) from `<preferences>/directoutputconfig`; without `GlobalConfig_B2SServer.xml` it
  still looks there. `TableLibrary::ImportDofConfig`, called by the web server for every upload: a `directoutputconfig*.ini` or
  `ledcontrol*.ini`, or a zip holding only `.ini`, `.xml`, `.png` and readme files with one of those inis, uploaded anywhere in the
  tables folder, is moved there (the zip extracted, its folders flattened, then deleted), replacing files of the same name (doctest in
  `tests/test-table-library.cpp`). Backups hold the folder with the settings (`Backup.cpp`, `DOF_CONFIG_FOLDER`).
- **Shipped configuration** (`src/assets/dof/pinone-mini`): the owner's DOF Config Tool output for a PinOne Mini (2026-11-18: device
  PinOne = LedWiz 11, port 1 *Left Solenoid*, port 2 *Right Solenoid*, its two motors). One configuration covers every table of the
  DOF database (about 1,750): libdof finds a table by its ROM name, then by an entry its name starts with (`mm` for `mm_109b`).

### 6.5 Checked on the Frame (2026-11-18)

The owner's configuration imported from its zip; libdof found the Bluetooth PinOne and loaded the table's entry (Medieval Madness);
SDL's hidapi reported the vendor collection with the serial; raw report 6 writes fired the outputs (single output frames from a
shell, `printf ... > /dev/hidrawN`); the owner felt the table's DOF on the PinOne. A first "no vibration" was the motors' power
supply unplugged: the PinOne's motors need their own supply, which USB does not give.

Still to check: the latency, the Xbox pad on Windows with the extra collection (Steam and `joy.cpl` on the owner's mini PC), and
whether the USB PinOne's vendor hidraw node needs a udev rule on the Frame.

## 7. DOF or vibrations

### 7.1 Vibrations to the controller that started the game

Upstream sends every physics vibration (flipper hits, bumpers, slingshots, plunger, nudge) to every controller at once: the SDL pads
and the VR controllers. With a PinOne in Xbox mode, its outputs fired with the VR controllers' haptics. The fork records the device
whose binding pressed Start (`InputAction::OnInputChanged` → `Vibrations::OnActionPressed`) and sends vibrations to that device only
(`SDLInputHandler::PlayRumble`, `XRInputHandler::PlayRumble` ask `Vibrations::ShouldVibrate`); until Start is pressed all controllers
vibrate; Start on the keyboard leaves none vibrating; stopping a vibration is always sent. Option *Send vibrations to the controller
used to start the game* (`Player.RumbleStartController`, on by default, Controls). Fork master `8dd5ac5e6`.

### 7.2 One or the other

The owner's choice (2026-11-18): the PinOne's motors follow the table's DOF or the physics vibrations, never both. An option to get
both (`Player.RumbleDofController`) was written, then removed. `Vibrations::ShouldVibrate` returns false for a controller with a DOF
channel (section 8) while DOF is on and the DOF configuration has an entry for the table:

```cpp
if (s_dofDevices.contains(deviceId) && DofEnabled() && s_dofTableHasConfig)
   return false;
```

- `DofEnabled()` reads `Plugin.DOF.Enable` at each vibration, so switching DOF off gives the vibrations back at once.
- **Tables DOF doesn't know get the vibrations.** The DOF plugin tells whether the table has an entry, with libdof's own lookup
  (`RomListHasConfig`: the ROM name, upper case, then an entry it starts with; the ROM list read with `GetLedControlRomNames`).
  Two generic plugin messages, an upstream candidate: `DOF`/`GetTableConfig` (data: a `bool*` the plugin sets) and
  `DOF`/`OnTableConfigChanged` (broadcast when it changes: `SetupDOF`, the game end, the controllers removed). VPX subscribes from
  the game start to its end (`Vibrations::OnGameStarted`/`OnGameEnding`, hooks in `player.cpp` after `m_pluginAPI.OnGameStart()` and
  before `OnGameEnd`), and asks once at the start because the plugin may set the table up before it (when the script creates its
  controller).

Checked on the Frame (2026-11-18): Medieval Madness (in the database) gives DOF only, Apollo (not in it) gives the vibrations.

## 8. Telling a PinOne from an Xbox pad

In Xbox mode the PinOne copies a real Xbox Series X pad on purpose: same name, vendor and product ids, version `0x0509`, report map.
Fields that differ but don't help:

| Field | PinOne | Real Xbox pad | Usable |
|---|---|---|---|
| Name, ids | Same | Same | No |
| Version | `0x0509`, fixed | `0x05xx`, changes with its firmware | No |
| Device Information strings (manufacturer *Microsoft*, model, serial = chip MAC, firmware revision) | Readable by GATT | Microsoft's | Not given to SDL by Linux; reading them through BlueZ's D-Bus beside Steam is fragile |
| Bluetooth address (the kernel's `HID_UNIQ`, SDL's and hidapi's serial) | Espressif prefix | Microsoft prefix | Fragile: Espressif has many prefixes |
| Vendor collection `0xFF00` | Yes (DOF firmware) | No | **Yes** |

Changing the Xbox version number to mark the PinOne was rejected: drivers and SDL may key their handling of a real pad on it.

### 8.1 The DOF channel (fork master)

`PinOneDevices::Identify` (before 2026-11-19, `Vibrations::HasPinOneDofCollection`) enumerates the HID devices with the Xbox ids
(`EnumerateAllHIDDevices`, SDL's hidapi with the controllers-only filter lifted for the call) and looks for a `0xFF00` entry whose serial
matches the joystick's (`SDL_GetJoystickSerial`; both are the Bluetooth address, compared without separators or case). Only the serial
counts: a rule that took the only such device for a pad without a serial was removed on 2026-11-19, as it could have taken a real Xbox
pad for a PinOne. On the Frame the kernel gives the address (`HID_UNIQ=3c:dc:75:ed:b5:21` for the owner's PinOne), so the match
works. The USB PinOne (`39f5:9208`, wired) always has its channel.

### 8.2 The identity report

**Firmware** (branch `feature/ble-xbox-identity`, commit `6d2ce85` on top of `feature/ble-xbox-dof`; proposed with the DOF report in
[#11](https://github.com/philipellisis/PinOne/pull/11)): a read-only
feature report 7 in the same vendor collection, 32 bytes:

| Bytes | Content |
|---|---|
| 0-7 | `PinOne` and two zeros |
| 8 | Format of the report: 1 |
| 9 | Capabilities: bit 0 = DOF output report 6 |
| 10-31 | Firmware version (`FIRMWARE_VERSION`, *3.3.0*), ASCII, zero padded |

Descriptor: `Report ID (7), Usage (0x02), Report Count (32), Feature (Constant, Variable, Absolute)`, with the collection's 8-bit
size and 0-255 range. The value is set at start (`setIdentityValue`); a host's write (`SET_REPORT`) is undone by the characteristic's
callback. Hosts that don't know the report never read it: Windows and SDL keep seeing an Xbox controller. It needs a re-pair, like any
report map change. `FIRMWARE_VERSION` was not bumped.

**VPX** (`src/steamframe/PinOneDevices.h/.cpp`, fork master):

- `SDLInputHandler::OnJoystickAdded` calls `PinOneDevices::Identify(joystick)` **before** `RegisterDevice`, because the name the
  device is listed under depends on it. For an Xbox pad with the vendor collection (matched by serial, section 8.1), it opens the
  collection's path (`SDL_hid_open_path`) and reads report 7 (`SDL_hid_get_feature_report`: on Linux `HIDIOCGFEATURE` on the hidraw
  node, which BlueZ turns into a GATT read of the report characteristic). A real Xbox pad has no vendor collection and is never opened.
- A pad that answers with the `PinOne` magic is listed as **PinOne (XBox)** (`PinOneDevices::DisplayName`; the settings id, built
  from the SDL GUID, doesn't change, so existing mappings stay). Log line: `PinOne: the Xbox controller <serial> is a PinOne,
  firmware <version>, with a DOF channel`.
- `PinOneDevices::OnJoystickAdded` keeps the identity by device id and erases it for any other controller: a real Xbox pad and a
  PinOne can share a GUID (same ids, name and version), hence a settings id and a device id.
- The identity (`Identity`: `usb`, `xbox`, `dofChannel`, `firmware`) is what the rest uses: `Vibrations::OnJoystickAdded` (DOF channel),
  `ControllerLayouts` (the PinOne layout only on a PinOne: `HasPinOneLayout` checks `IsPinOne()`), `DofSetup::HasDofChannel`, the
  controller page.
- A PinOne on the DOF firmware without the identity report keeps its DOF channel (and the vibration rule), but is listed as an Xbox
  controller and gets no PinOne items. That firmware was never released.

The read happens on the main thread when the pad connects. BlueZ's HID-over-GATT answers `GET_REPORT` with a GATT read (one
Bluetooth round trip) or an error at once when the report doesn't exist; if it didn't answer, the kernel's `uhid` would wait up to 5
seconds. The log shows the read's outcome.

**Checked on the Frame (2026-11-19)**, firmware `c2b667c` flashed and the PinOne paired again: the report map (314 bytes) declares
report 7; read from a shell (`HIDIOCGFEATURE` on the hidraw node) it answers in 13 ms with `PinOne`, format 1, capabilities `0x01`,
firmware `3.3.0`; and VPX logged `PinOne: the Xbox controller 3c:dc:75:ed:b5:21 is a PinOne, firmware 3.3.0, with a DOF channel`,
listed it as *PinOne (XBox)* and played Addams Family with it. Not checked: a real Xbox pad beside it, Windows.

## 9. Controller page

`InputDevicePage::BuildPage` (Controls > the controller), in the fork build:

- *Set default mapping*, when the device has one.
- On a PinOne only (`ControllerLayouts::HasPinOneLayout`: the USB PinOne, or *PinOne (XBox)*):
  - an information line *PinOne in Bluetooth Xbox mode, firmware X* (identity report);
  - **Set PinOne Mini mapping and DOF Config** (`DofSetup::ApplyPinOneMiniSetup`): the PinOne layout, the shipped configuration
    copied from `assets/dof/pinone-mini` to `<preferences>/directoutputconfig` (replacing an uploaded one), DOF restarted if it ran (it
    read the previous configuration for the table) and turned on, and a notification;
  - on a PinOne with a DOF channel, **DOF Enable**: the same property as Plugins > DOF > DOF Enable (label from the property),
    loading or unloading the plugin as `PluginSheetPage` does (the script stopped if the plugin contributed to it) and saving the
    setting; with a note that the controller's vibrations are off while DOF is on, except on the tables missing from the DOF
    configuration.
- *Erase this device*.

## 10. Plunger and nudge over Bluetooth

### 10.1 The axes each connection carries

| Signal | USB | Bluetooth, Xbox mode | Bluetooth, own profile |
|---|---|---|---|
| Nudge position X, Y | X, Y (16 bits) | Left stick X, Y (16 bits) | Left stick X, Y |
| Plunger | Z (16 bits) | Right stick Y (16 bits) | Right stick Y |
| Nudge speed X, Y (optional, accelerometer) | Rx, Ry (16 bits) | **Not sent** | **Not sent** |
| Free | Rz (8 bits) | Right stick X | Right stick X |

The stick each signal goes on is set in the configuration tool (Inputs page for USB, Game Controller page for Bluetooth:
`usb*Axis`, `ble*Axis`). Both Bluetooth profiles have the Xbox stick layout: 4 two-way 16-bit axes and 2 one-way 10-bit triggers.
The plunger and the two nudge positions take 3 sticks, so the nudge speed has no room.

The USB report is correct: the firmware registers `_combinedReportDescriptor` (`UsbHid.cpp`), where Z is 16 bits like X, Y, Rx and
Ry and Rz is 8 bits, the layout the report structure has; checked in the descriptor macOS received (2026-11-19).
`_hidReportDescriptorGamepad` at the top of the file, with Z and Rz on 8 bits, is a V1 leftover that nothing uses.

### 10.2 Plunger: the same value as USB (firmware, 2026-11-19)

**Symptom.** Over Bluetooth, the table's plunger made small bumps back and forth at rest. In VPX's plunger sensor page the raw line was
noisy and the filtered line looked smooth. The table follows the filtered value (`PlungerHandler::GetPosition`: a Kalman filter and a
4 ms average), but the filter is tuned for fast releases and lets slow jitter through; the graph's -1.2 to 1.2 scale and its clamp at
the fully forward position hid the rest.

**Measured on the Frame** (the kernel's input device for the PinOne, `ABS_RZ`, plunger untouched): the rest level moved between
-0.33 and -0.065 of the stick from one capture to the next, and the noise between 0.007 and 0.09 (standard deviation, energy at
30-50 Hz). The noisiest capture was the only one with the Bluetooth link at 7.5 ms (the others at 15 ms). The PinOne never sent its
rest value.

**Cause.** The Bluetooth report read `Plunger::getAdjustedValue()`, the reading mapped to the axis, while USB gets the processed
value: 0 at rest (the reading within `plungerMid` ± `plungerRestingDeadZone` for `restingStateMax`, 160 ms, with
`disablePlungerWhenNotInUse`), 0 and the launch button after a quick release, and the quick release delay. At rest the reading is also a
single ADC sample: the firmware averages only while the plunger moves, since USB sends 0 then.

**Fix** (branch `fix/ble-plunger-rest`, from upstream `main`, [philipellisis/PinOne#10](https://github.com/philipellisis/PinOne/pull/10)):

- `e671e50`: `Plunger` keeps the value it last put on the USB axis (`getReportedValue()`) and the Bluetooth report sends it. The values
  are now the USB ones; only the timing differs: USB is polled every 1 ms, Bluetooth sends a change at most every 8 ms and as the link
  allows (every 7.5 to 15 ms), plus a heartbeat every 25 ms.
- `b4bda93`: reports only go out on a change, from previous values that start at 0. With the plunger at exactly 0 at rest, a host that
  subscribed while nothing moved never got a first report: it kept an empty one (sticks read as fully deflected) and the heartbeat
  re-sent it. `BleController::update()` now resets the remembered axes when the subscription starts. The noisy plunger used to hide
  this.

Cherry-picked on `feature/ble-xbox-identity` (`da9c6e9`, `c2b667c`). Flashed on the owner's PinOne on 2026-11-19 on top of the DOF
firmware (`3d51cd4`, a local build, no re-pair needed), then with the identity report (`c2b667c`). **Checked on the Frame**: at
rest, 30 s without a plunger event, the axis at 32768 (the stick's center), and 400 raw reports in 10 s, all at 32768. Still to check: the return to 0 after a release (it needs the
plunger to stop inside the rest window, so the calibrated `plungerMid` matters), a table, the quick release over Bluetooth.

Still open: the noise while the plunger moves (the firmware averages 11 ADC reads taken back to back, which removes no 30-50 Hz noise;
a median and a 10-20 ms low-pass would), its source (not the ADC2 and radio conflict: GPIO 7 is `ADC1_CH6` on the ESP32-S3, the
`ADC2_CH7` comment in `Pins.h` and the README is wrong), and why the rest level moves.

### 10.3 Nudge speed in Xbox mode: not sent (owner's decision, 2026-11-19)

The limit is not Bluetooth's: a Bluetooth HID report map can declare any axes, and a report with two more 16-bit values (20 bytes) still
fits in the smallest Bluetooth packet. It is the Xbox mode's: Windows, Steam, a Quest and SDL's Xbox driver recognise the pad by its
ids and read the report at the stock Xbox positions, without the report map. The owner keeps the Xbox compatibility strict, so in Xbox
mode the PinOne sends no nudge speed, and VPX derives the speed from the nudge position (samples every 7.5 to 15 ms instead of the
PinOne's own rate). Options set aside:

| Option | Why not |
|---|---|
| Rx, Ry appended to the Xbox report | No longer the stock report; SDL's Xbox driver, which reads the pad on the Frame, would most likely ignore them |
| Speed on the free right stick X | Only one axis fits beside the plunger and the two positions |
| Speed on the triggers | One-way, 10 bits, at rest at 0: a two-way value needs an offset VPX's sensors don't have |
| The own Bluetooth profile with the USB layout | Not the Xbox mode: the Frame would lose the Xbox compatibility, and libdof only finds the Xbox mode's DOF channel |

### 10.4 Possible path: a vendor report, like DOF

If nudging over Bluetooth needs the speed, it goes through the vendor collection, as DOF (section 6) and the identity (section 8.2) do.
The stock Xbox report stays as it is, byte for byte.

- **Firmware.** An input report 8 in the `0xFF00` collection (`XboxDescriptor.h`): nudge speed X and Y, 16-bit signed, the values and
  scale the USB Rx and Ry get (`Accelerometer.cpp`, `usbVelocityXAxis`/`usbVelocityYAxis`). Sent from `BleController::update()` on a
  change, with the same 8 ms limit as the gamepad report, and once when the host subscribes (section 10.2). Each HID input report is
  its own GATT characteristic with its own subscription. Capability bit 1 in the identity report (byte 9) tells a host the report
  exists. The PinOne needs pairing again (new report map).
- **Hosts.** Programs that use the PinOne as an Xbox pad only read report 1; a vendor input report doesn't create an input device in
  Linux's HID layer, and hidraw passes it to whoever reads. **To check before relying on it:** SDL's Xbox driver reads the raw reports
  on the Frame and must skip an unknown report id rather than misread it; Windows and Steam with the extra input report; the extra
  traffic (one more notification per change, beside the gamepad report and DOF).
- **VPX.** `PinOneDevices` already opens the collection's path (`SDL_hid_open_path`) for the identity report. A reader keeps it open
  for a PinOne whose capability bit is set, reads report 8 (`SDL_hid_read`, on its own thread or polled without blocking from the input
  loop), and hands the values to the input manager as two extra axes of the *PinOne (XBox)* device. The cabinet nudge already takes
  speed sensors (`CabinetNudgeSensor::GetXVelSensor`/`GetYVelSensor`, in m/s after scale), so the axes map like any other, and the
  PinOne layout (section 5) maps them by default. As with DOF, VPX's core changes nothing for other controllers.

Upstream: the firmware part would go to philipellisis/PinOne with the DOF channel; the VPX reader stays in the fork, like
`PinOneDevices`.

### 10.5 Nudge strength (2026-11-19)

**Symptom.** On Addams Family, shaking the PinOne had no visible effect on the nudge, while the VR controllers' nudge worked. The
owner checked in the nudge sensor page that the PinOne's input reached VPX: the nudges were there, too weak to notice.

**Path of a nudge.** The firmware sends the accelerometer's acceleration on the left stick (`accelerometerRead()`: the reading minus
the centering offset, the dead zone, the orientation, the soft limit, then `xValue × 32767 / accelerometerMax`). VPX reads the stick
with `GamepadNudge`: a full stick is a strong nudge (`StrongNudgeAcceleration`, 0.7 g) times the sensor's strength
(`Mapping.Nudge<n>.Strength`, 0 to 2, default 1), and `NudgeIntentHandler` turns each peak into an impulse on a cabinet model that
sways at about 4.5 Hz.

**Measured on the Frame** (firm shakes, the kernel's input device): each nudge jumped from 0 to about 0.20 of the stick, peaked at
0.20 to 0.26, and fell back to 0 after 20 to 100 ms. No value between 0 and 0.195 ever came: the firmware's dead zone sets readings
below it to 0 without rescaling the rest, so the first value let through is already the dead zone's size (dead zone / *Max*: about
975 / 5000 = 0.195 with the owner's settings, read over USB in section 5; 30 / 280 = 0.11 with the defaults). At 100 % the PinOne's nudges were impulses of 0.14 to 0.18 g: all
alike, and too small to notice in VR.

**Decision (owner, 2026-11-19): 200 % for the moment.** `ApplyPinOneLayout` (*Set PinOne Mini mapping and DOF Config*) creates the
PinOne's `GamepadNudge` with `SetStrengthScale(2.f)`, the setting's maximum, saved with the sensor (`NudgeHandler::AddSensor`). A
mapping set before keeps its strength until the button is pressed again. 200 % doubles the impulses (0.28 to 0.36 g for the same
shakes); it was not tested in a game yet.

**Options for later**, from the simplest:

| Where | Change | Effect | Limits |
|---|---|---|---|
| VPX, sensor page | *Nudge Strength* | Scales every PinOne nudge | Capped at 200 % (the property's range): the layout already uses it all |
| PinOne configuration tool | Lower *Max* (X and Y) | Halving it doubles what the PinOne sends, up to a full stick | Lower the *Dead zone* with it, or the jump at the dead zone grows too (it is dead zone / *Max*) |
| PinOne configuration tool | Lower the *Dead zone* | Light knocks get through | Too low lets the cabinet's vibrations through (the auto-centering removes slow drift only) |
| Firmware | Rescale above the dead zone (`(abs(v) - dz) × max / (max - dz)`, as VPX's `SensorMapping` does) instead of cutting below it | Light knocks come out small instead of not at all; no jump at the threshold | Changes the PinOne's response on every host: an option, or a pull request to discuss upstream |
| VPX, the layout | A strength chosen from the PinOne's *Max* and dead zone (they could go in the identity report) | Nudges of the same size whatever the PinOne's settings | Needs a firmware change; the 200 % cap stays unless the layout scales the axes instead (`SensorMapping` scale) |
| Firmware and VPX | Nudge speed instead of acceleration on the left stick (section 10.3, 10.4) | VPX's cabinet sensor takes the speed integrated on the PinOne, which keeps a short knock's size at Bluetooth's 7.5 to 15 ms samples | Larger change: an option in the firmware, a capability bit, another mapping |

**Related findings, not changed:**

- **The plunger gate.** With `disableAccelOnPlungerMove` (default on), the firmware holds X and Y at 0 while it considers the plunger
  moving, that is until the plunger has stayed within `plungerMid` ± `plungerRestingDeadZone` for 160 ms. A plunger that stops
  outside that window after a launch turns the nudge off until it settles: one more reason to calibrate *Mid* (section 10.2).
- **Tilt buttons.** On the two hardest left shakes, the firmware's tilt detection (`processTiltButton`, raw reading against
  `accelerometerTilt`) pressed its tilt button for about 50 ms, which the owner's Bluetooth map sends as D-pad up: an operator menu
  button (PinMAME service button 3) in the PinOne layout, the volume up of a WPC game. Since then the Xbox-mode layout leaves the
  service buttons off the D-pad (section 5); the tilt buttons should still be off or on a free button.
- **Front nudges.** `NudgeIntentHandler` keeps only negative front values (`nudge.y = min(y, 0)`: a player pushes the cabinet). The
  PinOne's Y peaks of the test were positive; if they were forward shakes, front nudges are dropped and the Y axis needs reversing
  (the sensor's *Reversed axis*, or the PinOne's orientation). Not checked yet.
- **Several nudge sensors.** `NudgeHandler` uses the first active sensor only. The VR controllers' left thumbstick is sensor 0
  (`XRInputHandler`, OpenXR axes 20 and 21) and stays active for 10 s after a thumbstick nudge, hiding the PinOne meanwhile. The VR
  bumpers use the nudge actions (keyboard nudge), which are not sensors.
- The layout maps the axes as `Position` while `GamepadNudge` creates them as `Acceleration`; `GamepadNudge` ignores the type, so only
  the saved setting differs.

## 11. Building and flashing the firmware

On the Mac, `vp_vr/PinOne-build/build.sh` builds whatever branch `vp_vr/PinOne` has checked out (arduino-cli, core esp32 3.3.6, FQBN
`esp32:esp32:esp32s3usbotg`, libraries in `PinOne-build/libraries`: NimBLE-Arduino 2.3.7, Adafruit PWM Servo 3.0.3, BusIO 1.17.4).
The Mac has no Rosetta, so Arduino's x86 ctags can't run, and Homebrew's universal-ctags ignores `#line` directives: arduino-cli
then inserts the `setup()`/`loop()` prototypes inside `setup()`, which recurses until the PinOne boot loops (*Double exception*). The
script moves the sketch body into a `.cpp` file, for which Arduino generates no prototypes. Output: `PinOne-build/out/PinOne-V2.ino.bin`
(about 754 KB).

Flashing: esptool 5.1.0 from Arduino15, `--before default-reset` (the PinOne drops into its ROM loader, `303a:1001`), bootloader at
`0x0`, partitions `0x8000`, `boot_app0` `0xe000`, application `0x10000`; the configuration (NVS) is kept. The ROM loader comes back
on another serial port (`/dev/cu.usbmodem11201` instead of `11202`): flash it there with `--before no-reset`, and end with a
`--after watchdog-reset` command (`read-mac`), since `hard-reset` left it in the loader (2026-11-19). The original flash is backed
up (8 MB) in `PinOne-build/backup/`. After a firmware that changes the report map, remove the PinOne from the Frame's Bluetooth devices
and pair it again from the Steam UI.

Branches in `vp_vr/PinOne` (clone of upstream `main` `2256f78`; the owner's fork is `SPD13/PinOne`):

| Branch | Commit | Content | Pushed |
|---|---|---|---|
| `fix/ble-client-features` | `4b602e9` | Pairing fix (section 3) | Yes, PR #9 |
| `feature/ble-xbox-dof` | `2ff060d` | DOF report 6, queue, motor order (section 6.2) | Yes, to `SPD13/PinOne` |
| `feature/ble-xbox-identity` | `c2b667c` | Identity report 7 (section 8.2, `6d2ce85`), plunger fixes cherry-picked | No |
| `feature/ble-xbox-dof-identity` | `bedd33d` | DOF report 6 and identity report 7 rebuilt on `main` without #9 (`8be3348`, `bedd33d`) | Yes, PR #11 |
| `fix/ble-plunger-rest` | `b4bda93` | Plunger value as USB, first report on subscribe (section 10.2), from `main` | Yes, PR #10 |

## 12. Upstream

| Change | Upstream | State |
|---|---|---|
| Pairing fix | philipellisis/PinOne | [#9](https://github.com/philipellisis/PinOne/pull/9) open |
| Bluetooth DOF channel, identity report | philipellisis/PinOne | [#11](https://github.com/philipellisis/PinOne/pull/11) open (2026-11-19, branch `feature/ble-xbox-dof-identity` from `main`, independent of #9) |
| Bluetooth plunger as USB, first report on subscribe | philipellisis/PinOne | [#10](https://github.com/philipellisis/PinOne/pull/10) open (2026-11-19, independent of #9) |
| libdof: PinOne in Xbox mode | vpinball/libdof (and the C# DirectOutput side) | Fork patch only, PR not drafted |
| DOF plugin's `GetTableConfig`/`OnTableConfigChanged` messages | vpinball/vpinball | Candidate, generic |
| Everything else (`src/steamframe/*`, the controller page) | | Fork only |

VPX itself needs no change to drive a Bluetooth PinOne on other platforms: libdof does it (with its patch), the firmware gives the
channel.

## 13. Troubleshooting

| Symptom | Check first |
|---|---|
| The PinOne pairs but no controller appears | The pairing fix in the firmware; `/proc/bus/input/devices` (`Bus=0005`), `journalctl` for bluetoothd and SMP errors (section 3) |
| Listed but its buttons do nothing | Steam's virtual pad is read instead of the PinOne ([Gamepads and Steam Input](Gamepads%20and%20Steam%20Input.md), section 5) |
| Listed as *Xbox Series X Controller*, no PinOne items | Firmware without the identity report, or the PinOne not paired again after the update; the log's `PinOne:` line |
| DOF fires nothing | The motors' power supply; port assignments in the DOF Config Tool (a configuration without them lists the tables but fires nothing); `Plugin.DOF.Enable`; libdof's lines in `vpinball.log` (PinOne found, the table's entry) |
| No vibration on a table | DOF is on and the table is in the DOF database: the PinOne follows the table's DOF (log `Vibrations: the DOF configuration has an entry for this table`) |
| The table's plunger jitters over Bluetooth | Firmware without `fix/ble-plunger-rest` (raw reading sent); with it, the plunger's rest outside `plungerMid` ± the resting dead zone: calibrate in the configuration tool (section 10.2) |
| Over Bluetooth, sticks read fully deflected until something moves | Firmware with the plunger fix but without `b4bda93` (no first report, section 10.2) |
| Shaking the PinOne barely nudges | The PinOne layout applied again since 200 % (sensor page: *Nudge Strength*); the PinOne's *Max* and dead zone; the plunger at rest (the firmware holds the nudge at 0 while the plunger moves); the VR left thumbstick unused for 10 s (section 10.5) |
| A hard nudge changes the volume or opens the test menu (WPC) | The PinOne's tilt buttons on its D-pad inputs; a layout applied before the D-pad change (apply it again) or the USB layout, which keeps the service buttons (section 5) |
| A real Xbox pad loses its vibrations or gets PinOne items | Should not happen: it has no vendor collection. Look for a `PinOne:` or `Vibrations: ... is a PinOne` log line naming it |

## 14. Code

| File | Role |
|---|---|
| `src/steamframe/PinOneDevices.h/.cpp` | Which controllers are a PinOne: USB ids, vendor collection by serial, identity report |
| `src/steamframe/ControllerLayouts.h/.cpp` | The PinOne layout, USB input names |
| `src/steamframe/Vibrations.h/.cpp` | Start controller, DOF or vibrations |
| `src/steamframe/DofSetup.h/.cpp` | DOF switch, PinOne Mini setup |
| `src/steamframe/SteamInputGamepads.h/.cpp` | Pads Steam takes over |
| `src/input/SDLInputHandler.h` | Hooks in `OnJoystickAdded` (`VPX_STEAM_FRAME`) |
| `src/ui/live/ingameui/InputDevicePage.cpp` | The controller page |
| `plugins/dof/DOFPlugin.cpp` | Table entry lookup and its messages |
| `src/core/player.cpp` | `Vibrations::OnGameStarted`/`OnGameEnding` |
| `src/core/Settings_properties.inl` | `Plugin.DOF.Enable` on by default, `Player.RumbleStartController` |
| `src/steamframe/TableLibrary.cpp`, `Backup.cpp` | DOF configuration upload, backups |
| `src/assets/dof/pinone-mini/` | Shipped DOF configuration |
| `steamframe/patches/libdof/`, `platforms/linux-*/external.sh` | libdof patch |
| PinOne: `XboxDescriptor.h`, `BleController.cpp/.h`, `Communication.cpp/.h`, `Plunger.cpp/.h`, `UsbHid.cpp`, `README.md` | Firmware |
