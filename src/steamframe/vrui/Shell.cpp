// license:GPLv3+

#include "core/stdafx.h"
#include "Shell.h"

#include "Keyboard.h"
#include "MainMenuPage.h"
#include "PageView.h"
#include "SelfTest.h"
#include "Theme.h"
#include "Widgets.h"

#include "fonts/IconsForkAwesome.h"
#include "renderer/Renderer.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/LoadingPage.h"

namespace VPX::InGameUI
{

// Friend of InGameUI (see InGameUI.h): the menu goes to a screen with a fresh navigation history
class VRUINavigation final
{
public:
   static void ResetHistory(InGameUI& ui, const string& path) { ui.m_navigationHistory.assign(1, path); }
};

}

namespace VPX::VRUI::Shell
{

using namespace VPX::InGameUI;

namespace
{
   bool g_embedding = false; // The shell is drawing the content of a screen of the main menu
   bool g_showSelection = true;
   const std::function<bool(int)>* g_sectionFilter = nullptr; // Section of a composed screen being drawn
   ImVec2 g_menuPos, g_menuSize;
   const InGameUIItem* g_embeddedHovered = nullptr;
   int g_lastShownFrame = -10;

   // Draws the title, cut to fit
   void Title(const char* text, const ImVec2& pos, float maxWidth, float height)
   {
      ImGui::PushFont(Theme::BoldFont(), Theme::Px(Theme::kTitleSize));
      ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f));
      Widgets::TextEllipsis(text, maxWidth, Theme::kText);
      ImGui::PopFont();
   }
}

bool IsMainMenu(const InGameUIPage& page) { return dynamic_cast<const MainMenuPage*>(&page) != nullptr; }

void RenderSection(InGameUIPage& page, const std::function<bool(int)>& filter, float elapsedS)
{
   assert(g_embedding && g_sectionFilter == nullptr);
   g_sectionFilter = &filter;
   page.Render(elapsedS);
   g_sectionFilter = nullptr;
}

bool WasShownLastFrame() { return ImGui::GetCurrentContext() != nullptr && ImGui::GetFrameCount() - g_lastShownFrame <= 1; }

void NavigateWithFreshHistory(VPX::InGameUI::InGameUI& ui, const string& path)
{
   ui.Navigate(path);
   VRUINavigation::ResetHistory(ui, path);
}

bool RenderPage(InGameUIPage& page, float elapsedS)
{
   // Content of a screen of the main menu: only its items, in the content region of the menu
   if (g_embedding)
   {
      if (g_sectionFilter)
      {
         // Its actions are the ones of the composed screen
         if (const InGameUIItem* hovered = InGameUIPageView::RenderContent(page, g_showSelection, g_sectionFilter); hovered)
            g_embeddedHovered = hovered;
         InGameUIPageView::SetWindowOutputs(page, g_menuPos, g_menuSize, ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows));
         return true;
      }
      InGameUIPageView::SyncCoreItems(page, false);
      g_embeddedHovered = InGameUIPageView::RenderContent(page, g_showSelection);
      // The pages which draw more windows (plots, previews) place them around the menu, as around their window in upstream's view
      InGameUIPageView::SetWindowOutputs(page, g_menuPos, g_menuSize, ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows));
      return true;
   }

   // The loading page is the root of the navigation while a table loads: it keeps upstream's view (no menu to navigate)
   if (dynamic_cast<const LoadingPage*>(&page) != nullptr)
      return false;

   // Pages being closed (sliding out in upstream's view) are not drawn
   Player* const player = page.m_player;
   VPX::InGameUI::InGameUI& ui = player->m_liveUI->m_inGameUI;
   if (ui.GetActivePage() != &page || !page.IsActive())
      return true;

   // A keyboard left opened by a previous menu
   if (!WasShownLastFrame())
      Keyboard::Close();

   MainMenuPage* const menu = dynamic_cast<MainMenuPage*>(&page);
   InGameUIPage* const content = menu ? menu->GetContent() : &page;
   const bool flipperNav = ui.IsFlipperNav();

   Theme::BeginFrame();
   ImGui::PushFont(Theme::Font(), Theme::Px(Theme::kTextSize));
   Theme::PushStyle();

   const ImGuiIO& io = ImGui::GetIO();
   const ImVec2 size(Theme::Px(Theme::kMenuWidth), Theme::Px(Theme::kMenuHeight));
   const ImVec2 pos(floorf((io.DisplaySize.x - size.x) * 0.5f), floorf((io.DisplaySize.y - size.y) * 0.5f));
   const float pad = Theme::Px(Theme::kPadding);
   const float rounding = Theme::Px(Theme::kRounding) * 1.6f;
   const float railWidth = Theme::Px(Theme::kRailWidth);
   g_menuPos = pos;
   g_menuSize = size;
   ImGui::SetNextWindowPos(pos);
   ImGui::SetNextWindowSize(size);
   ImGui::Begin("##VRUIMenu", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
   Theme::ApplyPixelDensity();
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   // Opaque when the passthrough color key is used, as it keys the colors of the UI too
   const ImU32 panelBg = player->m_renderer->m_vrApplyColorKey ? (Theme::kPanelBg | IM_COL32_A_MASK) : Theme::kPanelBg;
   drawList->AddRectFilled(pos, pos + size, panelBg, rounding);
   drawList->AddRectFilled(pos, ImVec2(pos.x + railWidth, pos.y + size.y), Theme::kRailBg, rounding, ImDrawFlags_RoundCornersLeft);

   // Rail: one icon button per screen, centered vertically, the name of the screen in a bubble when hovered or focused
   {
      const Screen current = menu ? menu->GetScreen() : MainMenuPage::GetLastScreen();
      const float button = Theme::Px(Theme::kRailButton);
      const float gap = Theme::Px(Theme::kRailGap);
      int count = 0;
      for (int i = 0; i < kScreenCount; i++)
         count += IsScreenAvailable(static_cast<Screen>(i)) ? 1 : 0;
      float y = pos.y + (size.y - static_cast<float>(count) * (button + gap) + gap) * 0.5f;
      const float x = pos.x + (railWidth - button) * 0.5f;
      for (int i = 0; i < kScreenCount; i++)
      {
         const Screen screen = static_cast<Screen>(i);
         if (!IsScreenAvailable(screen))
            continue;
         ImGui::SetCursorScreenPos(ImVec2(x, y));
         bool hovered = false;
         const bool focused = menu && menu->IsRailFocused() && flipperNav && screen == current;
         ImGui::PushID(i);
         if (Widgets::RailButton("##rail", GetScreenIcon(screen), ImVec2(button, button), screen == current, focused, hovered))
         {
            if (menu)
               menu->SetScreen(screen);
            else
            {
               // From a sub-page, the menu opens on the screen as the root of the navigation
               MainMenuPage::SetLastScreen(screen);
               NavigateWithFreshHistory(ui, "homepage"s);
            }
         }
         ImGui::PopID();
         if (hovered || focused)
         {
            ImGui::PushFont(Theme::Font(), Theme::Px(Theme::kSmallTextSize));
            Widgets::Bubble(ImVec2(x + button + gap * 2.f, y + button * 0.5f), GetScreenName(screen));
            ImGui::PopFont();
         }
         y += button + gap;
      }
   }

   // Header: Back (sub-pages), title, Close
   const float contentX = pos.x + railWidth + pad;
   const float contentWidth = size.x - railWidth - 2.f * pad;
   const float headerHeight = Theme::Px(Theme::kHeaderHeight);
   const float pillHeight = Theme::Px(Theme::kPillHeight);
   {
      const float y = pos.y + Theme::Px(0.5f);
      const float pillY = y + (headerHeight - pillHeight) * 0.5f;
      float titleX = contentX;
      if (!menu && !player->m_isLoading)
      {
         ImGui::SetCursorScreenPos(ImVec2(contentX, pillY));
         if (Widgets::Pill("##back", ICON_FK_ARROW_LEFT, "Back", pillHeight, true, InGameUIPageView::IsCoreItemSelected(page, InGameUIItem::Type::Back)))
            InGameUIPageView::Back(page);
         titleX += Widgets::PillWidth(ICON_FK_ARROW_LEFT, "Back", pillHeight) + pad;
      }
      const bool inTable = !player->m_isLobby && !player->m_isLoading;
      const char* closeIcon = inTable ? ICON_FK_PLAY : ICON_FK_TIMES;
      const char* closeLabel = inTable ? "Resume" : "Close";
      const float closeWidth = Widgets::PillWidth(closeIcon, closeLabel, pillHeight);
      const string title = menu ? string(GetScreenName(menu->GetScreen())) : InGameUIPageView::GetTitle(page);
      Title(title.c_str(), ImVec2(titleX, y), contentX + contentWidth - closeWidth - pad - titleX, headerHeight);
      if (!player->m_isLoading)
      {
         ImGui::SetCursorScreenPos(ImVec2(contentX + contentWidth - closeWidth, pillY));
         if (Widgets::Pill("##close", closeIcon, closeLabel, pillHeight, true, false))
            ui.Close();
      }
   }

   // Content: the items of the page, or of the screen of the menu
   const float footerHeight = Theme::Px(Theme::kFooterHeight);
   // The keyboard takes the bottom of the menu on the desktop; in VR, where the panel is much larger than the menu, it stands below it
   const bool keyboardInside = Keyboard::IsOpen() && !Theme::IsVR();
   const float keyboardHeight = keyboardInside ? Keyboard::GetHeight(contentWidth) + Theme::Px(0.6f) : 0.f;
   const ImVec2 contentPos(contentX, pos.y + headerHeight + Theme::Px(0.5f));
   const ImVec2 contentSize(contentWidth, size.y - headerHeight - footerHeight - Theme::Px(0.5f) - keyboardHeight);
   const InGameUIItem* hoveredItem = nullptr;
   ImGui::SetCursorScreenPos(contentPos);
   Widgets::BeginRegion("##content", contentSize, true);
   if (content)
   {
      if (menu)
      {
         g_embedding = true;
         g_showSelection = !menu->IsRailFocused();
         g_embeddedHovered = nullptr;
         content->Render(elapsedS);
         g_embedding = false;
         hoveredItem = g_embeddedHovered;
      }
      else
      {
         InGameUIPageView::SyncCoreItems(page, true);
         hoveredItem = InGameUIPageView::RenderContent(page, true);
      }
   }
   const bool contentHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
   Widgets::EndRegion();

   // Keyboard, between the content and the footer (desktop)
   if (keyboardInside)
      Keyboard::Render(ImVec2(contentX, contentPos.y + contentSize.y + Theme::Px(0.6f)), contentWidth);

   // Footer: description of the hovered item (or of the page), and the Defaults/Undo/Save buttons of the page
   {
      const float y = pos.y + size.y - footerHeight;
      drawList->AddLine(ImVec2(contentX, y), ImVec2(contentX + contentWidth, y), Theme::kDivider, Theme::Px(0.08f));
      const char* actionTooltip = nullptr;
      float actionsLeft = contentX + contentWidth;
      if (content)
      {
         ImGui::SetCursorScreenPos(ImVec2(contentX, y + (footerHeight - pillHeight) * 0.5f));
         actionTooltip = InGameUIPageView::RenderActions(*content, contentX + contentWidth, pillHeight, actionsLeft);
      }
      const char* text = actionTooltip ? actionTooltip
         : (hoveredItem && !hoveredItem->m_tooltip.empty()) ? hoveredItem->m_tooltip.c_str()
         : content ? InGameUIPageView::GetInfo(*content).c_str() : "";
      ImGui::PushFont(Theme::Font(), Theme::Px(Theme::kSmallTextSize));
      const float textWidth = actionsLeft - contentX - pad;
      ImGui::PushClipRect(ImVec2(contentX, y), ImVec2(contentX + textWidth, y + footerHeight), true);
      ImGui::SetCursorScreenPos(ImVec2(contentX, y + Theme::Px(0.5f)));
      Widgets::WrappedText(text, Theme::kTextMuted, textWidth);
      ImGui::PopClipRect();
      ImGui::PopFont();
   }

   InGameUIPageView::SetWindowOutputs(page, pos, size, contentHovered || ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows));
   ImGui::End();

   // Keyboard, below the menu (VR)
   if (Keyboard::IsOpen() && !keyboardInside)
   {
      const float gap = Theme::Px(1.f);
      const float keyboardPad = Theme::Px(0.8f);
      const ImVec2 keyboardPos(contentX - keyboardPad, pos.y + size.y + gap);
      const ImVec2 keyboardSize(contentWidth + 2.f * keyboardPad, Keyboard::GetHeight(contentWidth) + 2.f * keyboardPad);
      ImGui::SetNextWindowPos(keyboardPos);
      ImGui::SetNextWindowSize(keyboardSize);
      ImGui::Begin("##VRUIKeyboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
      Theme::ApplyPixelDensity();
      ImGui::GetWindowDrawList()->AddRectFilled(keyboardPos, keyboardPos + keyboardSize, panelBg, rounding);
      Keyboard::Render(keyboardPos + ImVec2(keyboardPad, keyboardPad), contentWidth);
      ImGui::End();
   }
   Keyboard::EndFrame();

   if (content)
      InGameUIPageView::RenderPopups(*content);
   Theme::PopStyle();
   ImGui::PopFont();

   g_lastShownFrame = ImGui::GetFrameCount();
   SelfTest::Update(page);
   return true;
}

}
