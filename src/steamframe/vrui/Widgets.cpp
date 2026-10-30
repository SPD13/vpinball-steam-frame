// license:GPLv3+

#include "core/stdafx.h"
#include "Widgets.h"

#include "Theme.h"
#include "fonts/IconsForkAwesome.h"

#include "imgui/imgui_internal.h"

namespace VPX::VRUI::Widgets
{

void BeginRegion(const char* id, const ImVec2& size, bool scrollable)
{
   ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | (scrollable ? ImGuiWindowFlags_None : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)));
   Theme::ApplyPixelDensity();
}

void EndRegion()
{
   ImGui::EndChild();
   Theme::ApplyPixelDensity();
}

void SteamFrameLogo(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color)
{
   const float h = size * 0.5f;
   const float line = max(1.f, size * 0.095f);
   // The viewfinder: four corners
   const float arm = size * 0.22f;
   for (int i = 0; i < 4; i++)
   {
      const float sx = (i & 1) ? 1.f : -1.f, sy = (i & 2) ? 1.f : -1.f;
      const ImVec2 corner(center.x + sx * h, center.y + sy * h);
      const ImVec2 points[3] = { ImVec2(corner.x, corner.y - sy * arm), corner, ImVec2(corner.x - sx * arm, corner.y) };
      drawList->AddPolyline(points, 3, color, ImDrawFlags_None, line);
   }
   // The visor: a rounded band with a notch for the nose
   const float w = size * 0.37f, top = center.y - size * 0.2f, bottom = center.y + size * 0.2f, r = size * 0.12f;
   const float notch = size * 0.1f, notchDepth = size * 0.1f;
   drawList->PathArcTo(ImVec2(center.x - w + r, top + r), r, IM_PI, IM_PI * 1.5f);
   drawList->PathArcTo(ImVec2(center.x + w - r, top + r), r, IM_PI * 1.5f, IM_PI * 2.f);
   drawList->PathArcTo(ImVec2(center.x + w - r, bottom - r), r, 0.f, IM_PI * 0.5f);
   drawList->PathLineTo(ImVec2(center.x + notch, bottom));
   drawList->PathLineTo(ImVec2(center.x, bottom - notchDepth));
   drawList->PathLineTo(ImVec2(center.x - notch, bottom));
   drawList->PathArcTo(ImVec2(center.x - w + r, bottom - r), r, IM_PI * 0.5f, IM_PI);
   drawList->PathStroke(color, ImDrawFlags_Closed, line);
   // The lenses
   const float lens = size * 0.09f;
   drawList->AddCircleFilled(ImVec2(center.x - size * 0.17f, center.y - size * 0.03f), lens, color);
   drawList->AddCircleFilled(ImVec2(center.x + size * 0.17f, center.y - size * 0.03f), lens, color);
}

bool RailButton(const char* id, const char* icon, const ImVec2& size, bool selected, bool focused, bool& hovered)
{
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   const bool clicked = ImGui::InvisibleButton(id, size);
   hovered = ImGui::IsItemHovered();
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float rounding = Theme::Px(Theme::kRounding);
   if (selected)
      drawList->AddRectFilled(pos, pos + size, Theme::kAccentSoft, rounding);
   else if (hovered || focused)
      drawList->AddRectFilled(pos, pos + size, Theme::kSurfaceHover, rounding);
   if (focused)
      drawList->AddRect(pos, pos + size, Theme::kAccent, rounding, ImDrawFlags_None, Theme::Px(0.15f));
   if (selected)
   {
      // Bar on the left side of the selected screen
      const float barWidth = Theme::Px(0.25f);
      drawList->AddRectFilled(ImVec2(pos.x, pos.y + size.y * 0.2f), ImVec2(pos.x + barWidth, pos.y + size.y * 0.8f), Theme::kAccent, barWidth * 0.5f);
   }
   const ImU32 iconColor = selected ? Theme::kAccent : (hovered || focused) ? Theme::kText : Theme::kTextMuted;
   if (icon[0] == '\x01') // A logo drawn by us (Screens' kSteamFrameLogo)
   {
      SteamFrameLogo(drawList, pos + size * 0.5f, Theme::Px(Theme::kRailIconSize), iconColor);
      return clicked;
   }
   ImGui::PushFont(Theme::Font(), Theme::Px(Theme::kRailIconSize));
   const ImVec2 iconSize = ImGui::CalcTextSize(icon);
   drawList->AddText(pos + (size - iconSize) * 0.5f, iconColor, icon);
   ImGui::PopFont();
   return clicked;
}

float PillWidth(const char* icon, const char* label, float height)
{
   const float pad = height * 0.45f;
   float width = 2.f * pad;
   if (icon)
      width += ImGui::CalcTextSize(icon).x;
   if (icon && label && *label)
      width += height * 0.25f;
   if (label)
      width += ImGui::CalcTextSize(label).x;
   return max(width, height);
}

bool Pill(const char* id, const char* icon, const char* label, float height, bool enabled, bool focused, bool accent)
{
   const ImVec2 size(PillWidth(icon, label, height), height);
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   ImGui::BeginDisabled(!enabled);
   const bool clicked = ImGui::InvisibleButton(id, size);
   const bool hovered = enabled && ImGui::IsItemHovered();
   ImGui::EndDisabled();
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float rounding = height * 0.5f;
   const ImU32 background = !enabled ? IM_COL32(255, 255, 255, 10) : accent ? (hovered ? IM_COL32(100, 175, 255, 255) : Theme::kAccent) : (hovered || focused) ? Theme::kSurfaceHover : Theme::kSurface;
   drawList->AddRectFilled(pos, pos + size, background, rounding);
   if (focused && enabled)
      drawList->AddRect(pos, pos + size, accent ? Theme::kText : Theme::kAccent, rounding, ImDrawFlags_None, Theme::Px(0.15f));
   const ImU32 color = !enabled ? Theme::kTextDisabled : accent ? IM_COL32(8, 18, 34, 255) : Theme::kText;
   const float pad = height * 0.45f;
   float x = pos.x + pad;
   const float textY = pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
   if (icon)
   {
      drawList->AddText(ImVec2(x, textY), color, icon);
      x += ImGui::CalcTextSize(icon).x + ((label && *label) ? height * 0.25f : 0.f);
   }
   if (label)
      drawList->AddText(ImVec2(x, textY), color, label);
   return clicked && enabled;
}

// The slider of ImGui handles the interaction and draws the value, transparent otherwise: the track and its fill are drawn below it
static bool FillSlider(float fraction, const std::function<void()>& slider)
{
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   drawList->ChannelsSplit(2);
   drawList->ChannelsSetCurrent(1);
   ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
   ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
   ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
   ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 0, 0, 0));
   ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 0, 0, 0));
   slider();
   ImGui::PopStyleColor(5);
   const ImVec2 min = ImGui::GetItemRectMin();
   const ImVec2 max = ImGui::GetItemRectMax();
   const bool hovered = ImGui::IsItemHovered();
   const bool active = ImGui::IsItemActive();
   drawList->ChannelsSetCurrent(0);
   const float rounding = Theme::Px(0.45f);
   drawList->AddRectFilled(min, max, hovered || active ? Theme::kSurfaceHover : Theme::kSurface, rounding);
   const float fillWidth = (max.x - min.x) * ImSaturate(fraction);
   if (fillWidth > 0.5f)
      drawList->AddRectFilled(min, ImVec2(min.x + fillWidth, max.y), active ? IM_COL32(64, 156, 255, 150) : IM_COL32(64, 156, 255, 105), rounding,
         fillWidth >= max.x - min.x - rounding ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersLeft);
   // Edge of the fill, as a handle
   const float handleWidth = Theme::Px(0.25f);
   const float handleX = ImClamp(min.x + fillWidth - handleWidth * 0.5f, min.x, max.x - handleWidth);
   drawList->AddRectFilled(ImVec2(handleX, min.y + Theme::Px(0.3f)), ImVec2(handleX + handleWidth, max.y - Theme::Px(0.3f)), active ? Theme::kText : Theme::kAccent, handleWidth * 0.5f);
   drawList->ChannelsMerge();
   return active;
}

bool SliderFloat(const char* id, float* v, float vMin, float vMax, const char* format, float width)
{
   return FillSlider(vMax > vMin ? (*v - vMin) / (vMax - vMin) : 0.f,
      [&]()
      {
         ImGui::SetNextItemWidth(width);
         ImGui::SliderFloat(id, v, vMin, vMax, format, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
      });
}

bool SliderInt(const char* id, int* v, int vMin, int vMax, const char* format, float width)
{
   return FillSlider(vMax > vMin ? static_cast<float>(*v - vMin) / static_cast<float>(vMax - vMin) : 0.f,
      [&]()
      {
         ImGui::SetNextItemWidth(width);
         ImGui::SliderInt(id, v, vMin, vMax, format, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
      });
}

bool Toggle(const char* id, bool value, const ImVec2& size, bool focused)
{
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   const bool clicked = ImGui::InvisibleButton(id, size);
   const bool hovered = ImGui::IsItemHovered();
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float radius = size.y * 0.5f;
   drawList->AddRectFilled(pos, pos + size, value ? Theme::kAccent : (hovered ? Theme::kSurfaceHover : Theme::kTrack), radius);
   if (focused)
      drawList->AddRect(pos, pos + size, value ? Theme::kText : Theme::kAccent, radius, ImDrawFlags_None, Theme::Px(0.15f));
   const float knob = radius - Theme::Px(0.3f);
   drawList->AddCircleFilled(ImVec2(value ? pos.x + size.x - radius : pos.x + radius, pos.y + radius), knob, Theme::kText);
   return clicked;
}

int Stepper(const char* id, const char* value, const ImVec2& size, bool focused)
{
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float rounding = Theme::Px(0.45f);
   drawList->AddRectFilled(pos, pos + size, Theme::kSurface, rounding);
   if (focused)
      drawList->AddRect(pos, pos + size, Theme::kAccent, rounding, ImDrawFlags_None, Theme::Px(0.15f));

   int result = 0;
   const float arrowWidth = size.y * 1.2f;
   ImGui::PushID(id);
   for (int side = 0; side < 2; side++)
   {
      const ImVec2 arrowPos(side == 0 ? pos.x : pos.x + size.x - arrowWidth, pos.y);
      ImGui::SetCursorScreenPos(arrowPos);
      if (ImGui::InvisibleButton(side == 0 ? "prev" : "next", ImVec2(arrowWidth, size.y)))
         result = side == 0 ? -1 : 1;
      const bool hovered = ImGui::IsItemHovered();
      if (hovered)
         drawList->AddRectFilled(arrowPos, arrowPos + ImVec2(arrowWidth, size.y), Theme::kSurfaceHover, rounding);
      const char* icon = side == 0 ? ICON_FK_ANGLE_LEFT : ICON_FK_ANGLE_RIGHT;
      const ImVec2 iconSize = ImGui::CalcTextSize(icon);
      drawList->AddText(arrowPos + (ImVec2(arrowWidth, size.y) - iconSize) * 0.5f, hovered ? Theme::kAccent : Theme::kTextMuted, icon);
   }
   ImGui::PopID();

   // Value, centered between the arrows
   const float textMaxWidth = size.x - 2.f * arrowWidth;
   const ImVec2 textSize = ImGui::CalcTextSize(value);
   const ImVec2 textPos(pos.x + arrowWidth + max(0.f, (textMaxWidth - textSize.x) * 0.5f), pos.y + (size.y - textSize.y) * 0.5f);
   ImGui::SetCursorScreenPos(textPos);
   TextEllipsis(value, textMaxWidth, Theme::kText);

   ImGui::SetCursorScreenPos(pos);
   ImGui::Dummy(size);
   return result;
}

void RowBackground(const ImVec2& min, const ImVec2& max, bool hovered, bool selected)
{
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float rounding = Theme::Px(0.45f);
   if (selected)
   {
      drawList->AddRectFilled(min, max, Theme::kAccentSoft, rounding);
      const float barWidth = Theme::Px(0.22f);
      drawList->AddRectFilled(min, ImVec2(min.x + barWidth, max.y), Theme::kAccent, rounding, ImDrawFlags_RoundCornersLeft);
   }
   else if (hovered)
      drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 14), rounding);
}

void SectionHeader(const char* text, float width)
{
   ImGui::Dummy(ImVec2(0.f, Theme::Px(0.6f)));
   ImGui::PushFont(Theme::BoldFont(), Theme::Px(Theme::kHeaderTextSize));
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   ImGui::PushStyleColor(ImGuiCol_Text, Theme::kAccent);
   ImGui::TextUnformatted(text);
   ImGui::PopStyleColor();
   ImGui::PopFont();
   const float y = ImGui::GetCursorScreenPos().y - Theme::Px(0.15f);
   ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + width, y), Theme::kDivider, Theme::Px(0.08f));
   ImGui::Dummy(ImVec2(0.f, Theme::Px(0.2f)));
}

void WrappedText(const char* text, ImU32 color, float width)
{
   ImGui::PushStyleColor(ImGuiCol_Text, color);
   ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
   ImGui::TextUnformatted(text);
   ImGui::PopTextWrapPos();
   ImGui::PopStyleColor();
}

void TextEllipsis(const char* text, float maxWidth, ImU32 color)
{
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   const ImVec2 size = ImGui::CalcTextSize(text);
   ImGui::PushStyleColor(ImGuiCol_Text, color);
   if (size.x <= maxWidth)
      ImGui::TextUnformatted(text);
   else
   {
      ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + maxWidth, pos.y + size.y), pos.x + maxWidth, text, nullptr, &size);
      ImGui::Dummy(ImVec2(maxWidth, size.y));
   }
   ImGui::PopStyleColor();
}

void Bubble(const ImVec2& leftCenter, const char* text)
{
   ImDrawList* const drawList = ImGui::GetForegroundDrawList();
   const ImVec2 textSize = ImGui::CalcTextSize(text);
   const ImVec2 pad(Theme::Px(0.7f), Theme::Px(0.45f));
   const ImVec2 min(leftCenter.x, leftCenter.y - textSize.y * 0.5f - pad.y);
   const ImVec2 max(min.x + textSize.x + 2.f * pad.x, min.y + textSize.y + 2.f * pad.y);
   drawList->AddRectFilled(min, max, IM_COL32(235, 240, 248, 250), Theme::Px(0.4f));
   drawList->AddText(min + pad, IM_COL32(12, 16, 24, 255), text);
}

void Spinner(const ImVec2& center, float radius, ImU32 color)
{
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float start = static_cast<float>(ImGui::GetTime()) * 2.f * static_cast<float>(M_PI);
   drawList->PathArcTo(center, radius, start, start + 1.5f * static_cast<float>(M_PI), 32);
   drawList->PathStroke(color, ImDrawFlags_None, max(2.f, 0.2f * radius));
}

void ScrollIntoView(const ImVec2& rectMin, const ImVec2& rectMax)
{
   const float top = ImGui::GetWindowPos().y;
   const float bottom = top + ImGui::GetWindowHeight();
   if (rectMin.y < top)
      ImGui::SetScrollY(ImGui::GetScrollY() - (top - rectMin.y));
   else if (rectMax.y > bottom)
      ImGui::SetScrollY(ImGui::GetScrollY() + min(rectMax.y - bottom, rectMin.y - top)); // Keep the top visible when taller than the window
}

}
