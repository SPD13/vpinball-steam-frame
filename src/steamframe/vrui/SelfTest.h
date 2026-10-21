// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUIPage.h"

// Screenshots of the new VR menu, to check it without a headset: with VPX_VRUI_SHOTS=<folder>, the menu is opened (it opens by itself in the
// lobby), each screen of the rail is shown and captured, then a sub-page, and the application quits. Each capture logs the time taken to draw
// the menu since the previous one (a large library is checked with the last table of the Tables screen selected). Run with VPX_VRUI=new, in the lobby
// (-Launcher) and in a table (-play).
namespace VPX::VRUI::SelfTest
{

bool IsEnabled();

// Called when a player starts: opens the menu in a table
void OnPlayerCreated(Player* player, bool isLobby);

// Called each frame the menu is drawn, with the time it took (logged with each capture)
void Update(InGameUI::InGameUIPage& page, float renderMs);

}
