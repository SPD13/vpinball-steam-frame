// license:GPLv3+

#pragma once

#include "imgui/imgui.h"

// Look of the new VR UI. Sizes are given as angles seen from the player (degrees), converted to UI pixels each frame: in VR from the
// panel the UI is drawn on and the menu distance, so that the menu keeps the same apparent size wherever it is placed, and on the
// desktop (preview of the VR menu, for tests) by fitting the menu in the window.
namespace VPX::VRUI::Theme
{

// Width of the VR UI panel standing in the room, which covers the whole UI display area (see Panel.cpp)
inline constexpr float kVRPanelWidth = 2.8f; // meters

// Layout, in degrees
inline constexpr float kMenuWidth = 84.f;
inline constexpr float kMenuHeight = 56.f;
inline constexpr float kRailWidth = 5.4f;
inline constexpr float kRailButton = 3.6f; // Square, smaller when the buttons would not fit the height of the menu (see kRailMargin)
inline constexpr float kRailMargin = 0.6f; // Above and below the buttons of the rail, at least
inline constexpr float kRailGap = 0.3f;
inline constexpr float kHeaderHeight = 4.4f;
inline constexpr float kFooterHeight = 5.2f;
inline constexpr float kPadding = 1.2f;
inline constexpr float kRowHeight = 3.4f; // Smallest pointing target of the content
inline constexpr float kTileWidth = 12.f; // At least: the tiles share the width of the content, 6 per row in a menu of 84 degrees
inline constexpr float kRowGap = 0.25f;
inline constexpr float kRounding = 0.6f;
inline constexpr float kPillHeight = 3.2f;
inline constexpr float kBadgeHeight = 2.6f; // Wi-Fi badge, below the menu
inline constexpr float kBadgeGap = 0.8f;

// Text, in degrees (font size)
inline constexpr float kTextSize = 1.6f;
inline constexpr float kTitleSize = 2.2f;
inline constexpr float kHeaderTextSize = 1.75f;
inline constexpr float kSmallTextSize = 1.35f;
inline constexpr float kRailIconSize = 1.9f;

// Colors
inline constexpr ImU32 kPanelBg = IM_COL32(16, 19, 26, 242);
inline constexpr ImU32 kRailBg = IM_COL32(10, 12, 17, 255);
inline constexpr ImU32 kSurface = IM_COL32(28, 33, 44, 255);
inline constexpr ImU32 kSurfaceHover = IM_COL32(40, 47, 62, 255);
inline constexpr ImU32 kAccent = IM_COL32(64, 156, 255, 255);
inline constexpr ImU32 kAccentSoft = IM_COL32(64, 156, 255, 64);
inline constexpr ImU32 kText = IM_COL32(234, 238, 245, 255);
inline constexpr ImU32 kTextMuted = IM_COL32(150, 160, 178, 255);
inline constexpr ImU32 kTextDisabled = IM_COL32(95, 104, 120, 255);
inline constexpr ImU32 kError = IM_COL32(255, 96, 96, 255);
inline constexpr ImU32 kModified = IM_COL32(255, 176, 32, 255);
inline constexpr ImU32 kWarning = IM_COL32(250, 204, 21, 255); // Update badge
inline constexpr ImU32 kOnWarning = IM_COL32(28, 24, 4, 255);
inline constexpr ImU32 kDivider = IM_COL32(255, 255, 255, 22);
inline constexpr ImU32 kTrack = IM_COL32(60, 68, 86, 255);

// Computes the size conversion and fonts for this frame: called before the UI is drawn
void BeginFrame();

// UI pixels for an angle (degrees)
float Px(float degrees);

// Whether the UI is drawn in VR (else it is the desktop preview)
bool IsVR();

// Pushes the density at which the glyphs are rasterized, as the eye views magnify the UI pixels in VR. ImGui resets it in Begin/End,
// so it is applied again after each window or child window is begun or ended (see Widgets::BeginRegion).
void ApplyPixelDensity();

ImFont* Font(); // Regular, with icons
ImFont* BoldFont(); // Without icons

// ImGui style used by the new UI (frames, sliders, scrollbars), pushed while it draws
void PushStyle();
void PopStyle();

}
