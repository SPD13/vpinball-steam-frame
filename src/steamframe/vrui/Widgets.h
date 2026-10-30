// license:GPLv3+

#pragma once

#include "imgui/imgui.h"

// Widgets of the new VR UI: large targets for a VR controller pointer, and a 'focused' state for button navigation (d-pad). This is the only
// file of the new VR UI allowed to use imgui_internal.h, so that ImGui updates only need checking here.
namespace VPX::VRUI::Widgets
{

// Child window region, keeping the glyph density of the VR UI (see Theme::ApplyPixelDensity)
void BeginRegion(const char* id, const ImVec2& size, bool scrollable);
void EndRegion();

// The logo of the Steam Frame screen, drawn in a square of 'size' centered on 'center': a headset visor with two lenses, in the corners of a
// viewfinder frame (the icon font has no headset)
void SteamFrameLogo(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color);

// Square icon button of the rail (the icon is a glyph, or Screens' kSteamFrameLogo). Returns true when clicked.
bool RailButton(const char* id, const char* icon, const ImVec2& size, bool selected, bool focused, bool& hovered);

// Rounded button with an optional icon before its label. Returns true when clicked.
bool Pill(const char* id, const char* icon, const char* label, float height, bool enabled, bool focused, bool accent = false);
float PillWidth(const char* icon, const char* label, float height);

// Sliders drawn as a track filled up to the value, the value written over it. Same behavior as ImGui's (dragging, Ctrl+click to type).
// Returns true while the value is dragged.
bool SliderFloat(const char* id, float* v, float vMin, float vMax, const char* format, float width);
bool SliderInt(const char* id, int* v, int vMin, int vMax, const char* format, float width);

// On/off switch. Returns true when clicked.
bool Toggle(const char* id, bool value, const ImVec2& size, bool focused);

// Value with previous/next arrows on its sides. Returns -1 or +1 when an arrow is clicked, 0 otherwise.
int Stepper(const char* id, const char* value, const ImVec2& size, bool focused);

// Background of an item row: highlighted when hovered by the pointer, or selected with button navigation
void RowBackground(const ImVec2& min, const ImVec2& max, bool hovered, bool selected);

// Section header of a list of items
void SectionHeader(const char* text, float width);

// Wrapped text in a color
void WrappedText(const char* text, ImU32 color, float width);

// Text cut with an ellipsis to fit a width
void TextEllipsis(const char* text, float maxWidth, ImU32 color);

// Label bubble drawn over everything, its left side at the given point (vertically centered on it)
void Bubble(const ImVec2& leftCenter, const char* text);

// Spinner turning about once per second
void Spinner(const ImVec2& center, float radius, ImU32 color);

// Scrolls the current window so that a rectangle (screen coordinates) is visible
void ScrollIntoView(const ImVec2& min, const ImVec2& max);

}
