// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUI.h"

// The frame of the new VR menu, drawn around the active page of the in-game UI: the rail of screens on the left, a header with the title,
// Back and Close buttons, the items of the page, and a footer with the description of the hovered item and the Defaults/Undo/Save buttons.
namespace VPX::VRUI::Shell
{

// Draws a page with the new VR UI (called by InGameUIPage::RenderVRUI). Returns false to let upstream's view draw it.
bool RenderPage(InGameUI::InGameUIPage& page, float elapsedS);

bool IsMainMenu(const InGameUI::InGameUIPage& page);

// Whether the menu was drawn on the previous frame: it is opened, and moving between its pages
bool WasShownLastFrame();

// Navigates to a page which becomes the root of the navigation (getting back from it closes the menu)
void NavigateWithFreshHistory(InGameUI::InGameUI& ui, const string& path);

}
