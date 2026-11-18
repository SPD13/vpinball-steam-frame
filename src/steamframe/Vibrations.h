// license:GPLv3+

#pragma once

// Steam Frame Edition: vibrations go to the controller that started the game (Controllers > "Send vibrations to the controller used to
// start the game", Player.RumbleStartController, on by default).
//
// Upstream sends every vibration (flipper hits, bumpers, slingshots, plunger, nudge...) to all the controllers at once: the gamepads read
// through SDL (SDLInputHandler::PlayRumble) and the VR controllers (XRInputHandler::PlayRumble, OpenXR haptics). With a pinball controller
// and the VR controllers both connected, both vibrated, and a PinOne turns the vibrations of its Xbox mode into its solenoid outputs.
//
// With the option on, the device whose binding pressed the Start action (InputAction::OnInputChanged) is recorded, and the handlers send
// vibrations to that device only: one SDL controller, or the VR controllers (both hands, one device for VPX). Until Start is pressed on the
// table (attract mode, a table started otherwise), all controllers vibrate as before; Start pressed on the keyboard leaves no controller
// vibrating. The record is cleared when a table starts (InputManager is made again for each table). Stopping a vibration is always sent.

#include <cstdint>
#include <string>

namespace VPX::Vibrations
{

// A new InputManager, for a new table: no controller started the game yet
void Reset();

// Called by InputAction::OnInputChanged when an action becomes pressed by a binding of the given device
void OnActionPressed(const std::string& actionSettingId, uint16_t deviceId, const std::string& deviceSettingId);

// Whether a vibration is sent to the given device
bool ShouldVibrate(uint16_t deviceId);

}
