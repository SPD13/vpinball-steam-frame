// license:GPLv3+

#pragma once

// Steam Frame Edition: the VR room of the tables whose script leaves it off, turned on when the table plays in a headset.
//
// Most tables made for VR turn their room on by themselves: their script reads RenderingMode (2 in VR) or offers the room as a table option.
// Older ones have a user option at the top of the script, to edit by hand, and some ship with it off ("Tron Legacy (Stern 2011) VPW Mod"
// `Const VRRoom = 0 '0 - VR Room Off, 1 - Tron Room...`, "Whirlwind 4K" `Const VRRoom = 0 '0 = Off, 1 = VR Room On`, "Getaway mod"
// `VRRoom = 0 '0 = Desktop/FS 1 = VR Room`): in the headset they show a table floating in the void, without their cabinet or room.
//
// When the table plays in a headset, the script run gets that option set to 1 (the first room the option offers). The table file and its
// .vbs are left as they are: in desktop mode the option stays as written. Only a clear case is changed:
// - the script does not read RenderingMode (outside comments), so it does not choose the room itself;
// - the first assignment of VRRoom or VR_Room outside the subs and functions (`VRRoom = 0`, `Const VRRoom = 0` or `Dim VRRoom : VRRoom = 0`)
//   sets it to 0, and its comment says that 0 is off (or desktop) and names a choice 1.
// Other forms (a room number where 0 is a room, a value loaded from saved data, VRRoomChoice) are left alone.

#include <optional>
#include <string>

namespace VPX::VRRoomOption
{

struct Change
{
   std::string script; // The script with the option set to 1
   int line = 0; // 1-based line of the option
   std::string option; // The line as written in the table, without its line ending
};

// The script with its VR room turned on, or nothing when it does not have an option left off as described above. Only depends on the
// standard library (tests/test-vr-room-option.cpp).
std::optional<Change> TurnOn(const std::string& script);

}
