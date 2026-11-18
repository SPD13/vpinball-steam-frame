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
//
// A controller the table drives through DOF gets no vibrations, unless Player.RumbleDofController is on (Controllers, "Send vibrations to
// a controller driven by DOF", off by default): a PinOne turns the Xbox rumble into outputs of its own, which would then fire with the
// table's DOF and with the physics vibrations. Such a controller is a PinOne with a DOF channel libdof uses, while the DOF plugin is on:
// wired (39f5:9208, its USB vendor report), or in its Bluetooth Xbox mode (045e:0b13 with a 0xFF00 vendor collection, which a real Xbox
// pad has not; matched to the joystick by serial, the Bluetooth address). Found when the joystick connects (OnJoystickAdded).

#include <SDL3/SDL_joystick.h>

#include <cstdint>
#include <string>

namespace VPX::Vibrations
{

// A new InputManager, for a new table: no controller started the game yet
void Reset();

// Called by InputAction::OnInputChanged when an action becomes pressed by a binding of the given device
void OnActionPressed(const std::string& actionSettingId, uint16_t deviceId, const std::string& deviceSettingId);

// Called at the end of SDLInputHandler::OnJoystickAdded: records the controllers the table drives through DOF
void OnJoystickAdded(uint16_t deviceId, SDL_Joystick* joystick);

// Whether a vibration is sent to the given device
bool ShouldVibrate(uint16_t deviceId);

}
