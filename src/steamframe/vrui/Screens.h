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
   Performance, // The choice of the two next screens, which are not on the rail (they are shown in its place)
   Display, // VPX Performance: graphics, and the performance panel and VR rendering in VR
   SteamFrame, // Device Performance: the features of the Steam Frame Edition (dynamic resolution, foveated rendering, simplified meshes...)
   Sound,
   Controls,
   Plugins,
   System, // Library, other settings
   WiFi, // Adding tables from a browser
   Quit, // Leaving the application (a button on the screen, so that moving down the rail never quits)
   Count
};

inline constexpr int kScreenCount = static_cast<int>(Screen::Count);

// Icon of a screen: a glyph of the icon font, or kSteamFrameLogo, drawn by Widgets::SteamFrameLogo
inline constexpr const char* kSteamFrameLogo = "\x01SteamFrame";

bool IsScreenAvailable(Screen screen);
bool IsScreenOnRail(Screen screen); // Available and with an icon on the rail (the others are opened from a screen of the rail)
Screen GetRailScreen(Screen screen); // The screen of the rail it is opened from (itself when on the rail)
const char* GetScreenIcon(Screen screen);
const char* GetScreenName(Screen screen);

// The page shown by a screen (an upstream page, or one of the fork)
std::unique_ptr<InGameUI::InGameUIPage> CreateScreenContent(Screen screen);

// Screen shown when the menu opens: the table being played, or the table library in the lobby
Screen GetDefaultScreen();

}
