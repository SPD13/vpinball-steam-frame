// license:GPLv3+

#pragma once

// Steam Frame Edition: DOF for the PinOne from its controller page (Controls > the controller).
//
// The page of a PinOne (its USB controller, or "PinOne (XBox)": the PinOne in its Bluetooth Xbox mode, told from a real Xbox pad by
// its firmware's identity report, src/steamframe/PinOneDevices.h) has:
// - on a PinOne with a DOF channel (src/steamframe/PinOneDevices.h), a DOF
//   Enable switch, the same as Plugins > DOF > DOF Enable (the plugin loaded or unloaded at once, its setting saved), with a note that the
//   controller's vibrations are off while DOF drives it (src/steamframe/Vibrations.h: except on the tables DOF has no entry for);
// - Set PinOne Mini mapping and DOF Config: the PinOne layout (src/steamframe/ControllerLayouts.h), DOF turned on, and the game's DOF
//   configuration for the PinOne Mini installed in <preferences>/directoutputconfig, where libdof reads it (replacing the files an
//   upload put there). That configuration ships in assets/dof/pinone-mini: the owner's DOF Config Tool output for a PinOne Mini
//   (2026-11-18, device PinOne = LedWiz 11, port 1 "Left Solenoid", port 2 "Right Solenoid", its two motors).

#include <cstdint>

class Player;

namespace VPX::DofSetup
{

// Whether the DOF plugin runs
bool IsDofEnabled(Player* player);

// Whether the device is a PinOne with a DOF channel (src/steamframe/PinOneDevices.h), the pages that show the DOF Enable switch
bool HasDofChannel(uint16_t deviceId);

// Loads or unloads the DOF plugin and saves Plugin.DOF.Enable, as Plugins > DOF > DOF Enable does
void SetDofEnabled(Player* player, bool enabled);

// The PinOne layout on the device, DOF on, and the PinOne Mini DOF configuration installed (DOF restarted on it if it ran)
void ApplyPinOneMiniSetup(Player* player, uint16_t deviceId);

}
