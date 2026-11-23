// license:GPLv3+

#pragma once

// Steam Frame Edition: pop-up proposing the PinOne Mini mapping and DOF configuration when a PinOne connects in the lobby
//
// InputManager::ProcessInput proposes a controller's default layout in a pop-up when the controller connects without it (upstream's
// InputProfilePage, "Device ... was detected"), in the lobby only in the fork. A PinOne (src/steamframe/PinOneDevices.h) gets this pop-up
// instead, while its mapping isn't the PinOne layout (ControllerLayouts::IsPinOneLayoutApplied): Apply does what "Set PinOne Mini mapping
// and DOF Config" does in the controller's page (DofSetup::ApplyPinOneMiniSetup). "Don't show again" turns off Player.PinOneLayoutPopup,
// which the PinOne's page in Controls turns back on.

#include <cstdint>
#include <functional>

class Player;

namespace VPX::PinOneLayoutPopup
{

// Opens the pop-up, unless the in-game UI is already open (then returns false). onClosed runs when it is answered.
// The lobby opens on the table picker, which stays open until the player closes it: there the pop-up opens over the picker, and its answer
// goes back to it (CanOpenOverMenu), so that a PinOne paired while the lobby shows the picker gets it at once.
bool Propose(Player* player, uint16_t deviceId, const std::function<void()>& onClosed);

// Whether the pop-up may open over the in-game UI: in the lobby, over the table picker
bool CanOpenOverMenu(Player* player);

}
