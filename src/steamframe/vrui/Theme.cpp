// license:GPLv3+

#include "core/stdafx.h"
#include "Theme.h"

#include "imgui/imgui_internal.h"

namespace VPX::VRUI::Theme
{

namespace
{
   float g_pxPerDegree = 10.f;
   float g_pixelDensity = 1.f;
   bool g_vr = false;
   ImFont* g_font = nullptr;
   ImFont* g_boldFont = nullptr;
   constexpr int kStyleVars = 13;
   constexpr int kStyleColors = 16;
}

void BeginFrame()
{
   const ImGuiIO& io = ImGui::GetIO();
   g_vr = g_pplayer && g_pplayer->m_vrDevice;
   if (g_vr)
   {
      // The panel spreads the UI display width over kVRPanelWidth meters, at the menu distance: one degree at its center is that many pixels
      const float distance = clamp(g_settingsService.GetAppSettings().GetStandalone_VRMenuDistance(), 0.3f, 3.f);
      g_pxPerDegree = io.DisplaySize.x / kVRPanelWidth * distance * static_cast<float>(M_PI / 180.);
      // The eye views show about 20 pixels per degree at their center, more than the panel pixels: rasterize the glyphs at that density
      g_pixelDensity = clamp(22.f / g_pxPerDegree, 1.f, 3.f);
   }
   else
   {
      // Desktop preview: the menu fits the window, with a margin, and room for the Wi-Fi badge below it (the menu staying centered)
      g_pxPerDegree = 0.94f * min(io.DisplaySize.x / kMenuWidth, io.DisplaySize.y / (kMenuHeight + 2.f * (kBadgeGap + kBadgeHeight)));
      g_pixelDensity = io.DisplayFramebufferScale.x > 0.f ? io.DisplayFramebufferScale.x : 1.f;
   }

   // The fonts of LiveUI: the first one is DroidSans with the ForkAwesome icons merged in, the third is DroidSans bold (see LiveUI::LiveUI)
   const ImVector<ImFont*>& fonts = io.Fonts->Fonts;
   g_font = fonts.Size > 0 ? fonts[0] : ImGui::GetFont();
   g_boldFont = fonts.Size > 2 ? fonts[2] : g_font;
}

float Px(float degrees) { return roundf(degrees * g_pxPerDegree); }

bool IsVR() { return g_vr; }

void ApplyPixelDensity()
{
   if (g_vr)
      ImGui::SetPixelDensity(g_pixelDensity);
}

ImFont* Font() { return g_font; }

ImFont* BoldFont() { return g_boldFont; }

void PushStyle()
{
   const float pad = Px(0.6f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
   ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, Px(0.55f)));
   ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Px(0.45f));
   ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad, Px(0.4f)));
   ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(pad, Px(0.4f)));
   ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Px(1.f));
   ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, Px(0.5f));
   ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, Px(2.2f));
   ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, Px(0.45f));
   ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Px(kRounding));
   ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.f);

   const auto color = [](ImGuiCol idx, ImU32 c) { ImGui::PushStyleColor(idx, c); };
   color(ImGuiCol_Text, kText);
   color(ImGuiCol_TextDisabled, kTextDisabled);
   color(ImGuiCol_FrameBg, kSurface);
   color(ImGuiCol_FrameBgHovered, kSurfaceHover);
   color(ImGuiCol_FrameBgActive, kSurfaceHover);
   color(ImGuiCol_SliderGrab, kAccent);
   color(ImGuiCol_SliderGrabActive, IM_COL32(120, 185, 255, 255));
   color(ImGuiCol_Button, kSurface);
   color(ImGuiCol_ButtonHovered, kSurfaceHover);
   color(ImGuiCol_ButtonActive, kAccentSoft);
   color(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
   color(ImGuiCol_ScrollbarGrab, IM_COL32(255, 255, 255, 40));
   color(ImGuiCol_ScrollbarGrabHovered, IM_COL32(255, 255, 255, 70));
   color(ImGuiCol_ScrollbarGrabActive, IM_COL32(255, 255, 255, 90));
   color(ImGuiCol_PopupBg, IM_COL32(22, 26, 35, 250));
   color(ImGuiCol_TextSelectedBg, kAccentSoft);
}

void PopStyle()
{
   ImGui::PopStyleColor(kStyleColors);
   ImGui::PopStyleVar(kStyleVars);
}

}
