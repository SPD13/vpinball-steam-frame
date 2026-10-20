// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUIPage.h"

// Screens of the main menu of the new VR UI, chosen with the icon buttons of its rail (top to bottom)
namespace VPX::VRUI
{

enum class Screen
{
   Tables, // Table library: tabs, search, tiles
   ThisTable, // Actions and options of the table being played (not in the lobby)
   Scores, // Who is playing, leaderboards
   Room, // VR placement and room (point of view on the desktop)
   Display, // Graphics and performance
   Sound,
   Controls,
   Plugins,
   System, // Library, other settings
   WiFi, // Adding tables from a browser
   Quit, // Leaving the application (a button on the screen, so that moving down the rail never quits)
   Count
};

inline constexpr int kScreenCount = static_cast<int>(Screen::Count);

bool IsScreenAvailable(Screen screen);
const char* GetScreenIcon(Screen screen);
const char* GetScreenName(Screen screen);

// The page shown by a screen (an upstream page, or one of the fork)
std::unique_ptr<InGameUI::InGameUIPage> CreateScreenContent(Screen screen);

// Screen shown when the menu opens: the table being played, or the table library in the lobby
Screen GetDefaultScreen();

}
