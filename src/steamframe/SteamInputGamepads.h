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
// VPX empties the list before SDL starts its gamepads, and SDL opens the physical controllers. Steam's virtual pads are still listed, and
// SDL gives each the name and ids of the controller it stands for: the PinOne's shows as "Xbox Series X Controller #2", 045e:0b13, next to
// the real one (#1); only its SDL GUID carries Valve's 28de:11ff (VPX UID SDLJoy_0300....de280000ff11...). The fork's virtual pad check
// (SDLInputHandler::OnJoystickAdded, m_isVRVirtualGamepad: Valve vendor or a Steam name) does not catch it: it is listed and does nothing.
// If a Steam update starts feeding it in VR, both act, and only the mapped one counts.
//
// Investigation and tools: docs/steam-frame/Gamepads and Steam Input.md.

namespace VPX::SteamInputGamepads
{

// Call before SDL_InitSubSystem(SDL_INIT_GAMEPAD).
void ReadControllersClaimedBySteam();

}
