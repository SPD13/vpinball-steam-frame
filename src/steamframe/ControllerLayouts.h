// license:GPLv3+

#pragma once

// Steam Frame Edition: controller layouts are offered in the controller's page (Controllers > the controller), not in a pop-up.
//
// Upstream proposes a device's default layout in a modal dialog whenever a controller connects with a mapping that differs from that
// layout, at every session, and "yes" first erases the mappings the player made for it (InputManager::ApplyDefaultDeviceMapping). With a
// PinOne in its Bluetooth Xbox mode, that replaced its flippers (sent as the shoulder buttons) by magna-saves. The fork shows no pop-up
// (InputManager::ProcessInput, all fork builds; the VR controllers still get their layout at once), and the controller's page offers:
// - Set default mapping: upstream's default layout of the device (InputManager::ApplyDefaultDeviceMapping).
// - Set PinOne Mini mapping and DOF Config (src/steamframe/DofSetup.h, which also turns DOF on and installs the PinOne Mini DOF
//   configuration): the layout of the PinOne pinball controller (Cleveland Software Design), on the PinOne in its Bluetooth
//   Xbox mode (it presents itself as an Xbox pad, 045e:0b13: offered only when it answers the identity report of its firmware, listed
//   as "PinOne (XBox)", src/steamframe/PinOneDevices.h; not on a real Xbox pad) and on the PinOne's USB controller (39f5:9208, raw button n = PinOne input
//   n + 1). Made from the owner's mapping of a Bluetooth PinOne on the Frame (2026-11-17), with the firmware's default Xbox assignment:
//     PinOne input              Xbox control      USB button   VPX action
//     3 left flipper            LB                2            Left flipper and left staged flipper
//     1 right flipper           RB                0            Right flipper and right staged flipper
//     4 left magna-save         left trigger      3            Left magna-save
//     2 right magna-save        right trigger     1            Right magna-save
//     6 front                   A                 5            Launch ball and Start
//     8 front                   B                 7            Start
//     9 fire                    Start (Menu)      8            Start
//     5 front                   Y                 4            Insert coin
//     29                        View              28           In-game menu
//     25, 28, 27, 26 shifted    D-pad L, D, U, R  24, 27, 26, 25  PinMAME service buttons 1 to 4 (Cancel, Down, Up, Enter), menu navigation
//     7 front                   X                 6            (none)
//     plunger                   right stick Y     axis Z       Plunger position
//     accelerometer             left stick X/Y    axes X/Y     Nudge, read as a stick position (as upstream's gamepad layout)
//   The PinOne's assignments can be changed in its configuration tool; this layout follows its defaults.
// Both replace every mapping of that controller (actions, plunger, nudge), like upstream's. Mappings are global (application settings).

#include <SDL3/SDL_joystick.h>

#include <cstdint>

class InputManager;

namespace VPX::ControllerLayouts
{

// Called at the end of SDLInputHandler::OnJoystickAdded, after PinOneDevices::OnJoystickAdded: records the inputs of the PinOne
// layout of an identified PinOne in Xbox mode or of the PinOne's USB controller (by settings id, valid from one table to the next), and names the inputs of the latter.
void OnJoystickAdded(InputManager& input, uint16_t deviceId, SDL_Joystick* joystick);

bool HasPinOneLayout(InputManager& input, uint16_t deviceId);
void ApplyPinOneLayout(InputManager& input, uint16_t deviceId);

}
