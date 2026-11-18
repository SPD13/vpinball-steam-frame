// license:GPLv3+

#pragma once

// Steam Frame Edition: gamepads that Steam Input takes over are read directly, not through Steam's virtual pads.
//
// When Steam starts a game, it lists in SDL_GAMECONTROLLER_IGNORE_DEVICES every controller that Steam Input takes over (Xbox, PlayStation,
// Switch... pads, and controllers that present themselves as one, like the PinOne in its Bluetooth Xbox mode), so that SDL skips them and the
// game reads Steam's virtual pads ("Microsoft X-Box 360 pad N", Valve VID 0x28de) instead. Steam feeds a virtual pad only while it holds the
// game as the focused one. On the Steam Frame, while VPX renders in the headset, it never does: gamescope (run as a SteamVR overlay,
// --backend openvr) lists VPX in GAMESCOPE_FOCUSABLE_APPS, but GAMESCOPE_FOCUSED_APP stays empty, Steam sets no GAMESCOPECTRL_BASELAYER_APPID,
// and its controller log activates the controllers for its own UI (app 769) only. The virtual pads stay silent: the controller shows in the
// Controllers menu but no button reaches VPX. Disabling Steam Input for VPX in Steam fixed it, so does this, without a Steam setting.
//
// VPX empties the list before SDL starts its gamepads, and SDL opens the physical controllers. Steam's virtual pads are still there, and
// SDL gives each the name and ids of the controller it stands for: the PinOne's shows as "Xbox Series X Controller #2", 045e:0b13, next to
// the real one (#1), and the pad of the Frame's own controllers as "Steam Frame Controllers #1", 28de:11e0. Only their SDL GUID carries the
// ids of Steam's virtual gamepad, Valve's 28de:11ff (VPX UID SDLJoy_0300....de280000ff11...). They are not opened (IsSteamVirtualGamepad,
// called by SDLInputHandler::OnJoystickAdded), so the Controllers menu lists the real controllers only. If a Steam update starts feeding
// the virtual pads in VR, nothing changes for VPX: it reads the controllers themselves. Steam's pads saved by earlier builds (in Input.Devices,
// and in the mappings, when a layout was accepted for one) are dropped when the settings are read (IsSteamVirtualGamepadSettingsId, called by
// InputManager::LoadDevicesFromSettings, InputAction::SetMapping and PhysicsSensor::SetMapping, for the plunger and nudge axes), so that they do not show as disconnected controllers.
//
// Investigation and tools: docs/steam-frame/Gamepads and Steam Input.md.

#include <SDL3/SDL_joystick.h>

#include <string>

namespace VPX::SteamInputGamepads
{

// Call before SDL_InitSubSystem(SDL_INIT_GAMEPAD).
void ReadControllersClaimedBySteam();

// True for a virtual pad of Steam Input (Valve's 28de:11ff in its SDL GUID, whatever name and ids SDL reports), which VPX leaves out.
bool IsSteamVirtualGamepad(SDL_JoystickID id);

// The same for a device's settings id ("SDLJoy_" + SDL GUID + "_" + index, SDLInputHandler::OnJoystickAdded).
bool IsSteamVirtualGamepadSettingsId(const std::string& settingsId);

}
