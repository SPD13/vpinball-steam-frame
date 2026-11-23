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
// A controller the table drives through DOF gets no vibrations while the DOF plugin is on and its configuration has an entry for the
// table: its motors follow either the table's DOF or the physics vibrations, never both (owner's choice, 2026-11-18), as a PinOne turns
// the Xbox rumble into outputs of its own, which would otherwise fire with the table's DOF and with the physics vibrations. A table
// missing from the DOF configuration (DOF Config Tool database) gets the vibrations instead: the DOF plugin tells whether it has an
// entry ("DOF"/"GetTableConfig", "DOF"/"OnTableConfigChanged", followed from OnGameStarted to OnGameEnding). Such a controller is a PinOne with a DOF channel libdof
// uses (src/steamframe/PinOneDevices.h): wired (39f5:9208, its USB vendor report), or in its Bluetooth Xbox mode (045e:0b13 with the
// 0xFF00 vendor collection, which a real Xbox pad has not; matched to the joystick by serial only, the Bluetooth address). Found when
// the joystick connects (OnJoystickAdded); the DOF plugin's switch (Plugins > DOF > DOF Enable) is read at each vibration, so with DOF off the PinOne gets the
// vibrations again right away.
//
// The same option keeps the table's DOF on the controller that started the game (owner's request, 2026-11-22): a game started from the
// VR controllers or another gamepad, while a PinOne with a DOF channel is connected, pauses the DOF outputs ("DOF"/"SetOutputsPaused",
// plugins/dof/DOFPlugin.cpp: lit outputs turned off, events kept and replayed on resume); a game then started from the PinOne resumes
// them. Before Start is pressed, after a keyboard Start, or with the option off, DOF runs as before. libdof has no per-device switch, so
// the pause holds every DOF device, not only the PinOne.

#include <cstdint>
#include <string>

namespace VPX::Vibrations
{

// A new InputManager, for a new table: no controller started the game yet
void Reset();

// Called by InputAction::OnInputChanged when an action becomes pressed by a binding of the given device
void OnActionPressed(const std::string& actionSettingId, uint16_t deviceId, const std::string& deviceSettingId);

// Called at the end of SDLInputHandler::OnJoystickAdded, after PinOneDevices::OnJoystickAdded: records the controllers the table
// drives through DOF
void OnJoystickAdded(uint16_t deviceId, const std::string& name);

// Called when Player.RumbleStartController is changed in the menu: pauses or resumes the DOF outputs
void OnStartControllerOptionChanged();

// Whether a vibration is sent to the given device
bool ShouldVibrate(uint16_t deviceId);

// Called by Player after the plugins are told the game started, and before they are told it ended: follows whether the DOF
// configuration has an entry for the table
void OnGameStarted(class Player* player);
void OnGameEnding();

}
