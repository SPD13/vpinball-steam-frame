// license:GPLv3+

#pragma once

// Steam Frame Edition: which controllers are a PinOne (Cleveland Software Design's pinball controller).
//
// - By USB, the PinOne's controller has its own ids (39f5:9208, wired).
// - In its Bluetooth Xbox mode, the PinOne presents itself as an Xbox Series X controller (045e:0b13, same name and version), so that
//   hosts use it as one. Its firmware adds a vendor top-level collection (usage page 0xFF00) to the Xbox report map, which a real
//   Xbox pad doesn't have, matched to the joystick by serial (the Bluetooth address, the kernel's HID_UNIQ):
//   - usage 0x01, output report 6: its DOF channel, the one libdof drives (src/steamframe/Vibrations.h);
//   - identity feature report 7 (firmware branch feature/ble-xbox-identity): 32 bytes, "PinOne" and two zeros, format 1, capabilities
//     (bit 0 = DOF report 6), the firmware version in ASCII from byte 10. Read once when the joystick is added.
//   A pad that answers the identity report is a PinOne: it is listed as "PinOne (XBox)" in Controls, and its page offers the PinOne
//   Mini mapping and DOF configuration (src/steamframe/DofSetup.h). A PinOne with the DOF channel but an older firmware (no identity
//   report) keeps its DOF channel, still listed as an Xbox controller.

#include <cstdint>
#include <string>

struct SDL_Joystick;

namespace VPX::PinOneDevices
{

struct Identity
{
   bool usb = false; // The PinOne's USB controller
   bool xbox = false; // A pad that answered the identity report
   bool dofChannel = false; // A channel libdof drives (USB, or the Bluetooth DOF collection)
   std::string firmware; // From the identity report

   bool IsPinOne() const { return usb || xbox; }
};

// Called by SDLInputHandler::OnJoystickAdded before the device is registered (the name it is listed under depends on it). Opens the
// HID device of an Xbox pad with the PinOne's vendor collection to read its identity report, a Bluetooth round trip.
Identity Identify(SDL_Joystick* joystick);

// The name an identified PinOne in Xbox mode is listed under, or the name given
std::string DisplayName(const Identity& identity, const std::string& name);

// Records the identity of the device just registered (a device id comes back with the same settings id, maybe as another controller)
void OnJoystickAdded(uint16_t deviceId, const Identity& identity);

const Identity& Get(uint16_t deviceId);

}
