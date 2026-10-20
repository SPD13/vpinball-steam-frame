// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUIPage.h"

// Screenshots of the new VR menu, to check it without a headset: with VPX_VRUI_SHOTS=<folder>, the menu is opened (it opens by itself in the
// lobby), each screen of the rail is shown and captured, then a sub-page, and the application quits. Run with VPX_VRUI=new, in the lobby
// (-Launcher) and in a table (-play).
namespace VPX::VRUI::SelfTest
{

bool IsEnabled();

// Called when a player starts: opens the menu in a table
void OnPlayerCreated(Player* player, bool isLobby);

// Called each frame the menu is drawn
void Update(InGameUI::InGameUIPage& page);

}
