// license:GPLv3+

#include "core/stdafx.h"
#include "MainMenuPage.h"

#include "ComposedPage.h"
#include "Shell.h"
#include "VRUI.h"

#include "ui/live/LiveUI.h"

namespace VPX::VRUI
{

using namespace VPX::InGameUI;

static Screen s_lastScreen = Screen::Tables;

Screen MainMenuPage::GetLastScreen() { return s_lastScreen; }

void MainMenuPage::SetLastScreen(Screen screen) { s_lastScreen = screen; }

MainMenuPage::MainMenuPage(std::optional<Screen> screen)
   : InGameUIPage("Menu"s, ""s, SaveMode::None)
{
   // Getting back to the menu from one of its sub-pages shows the screen it was on, opening it shows the default one
   Screen initial = screen.value_or(Shell::WasShownLastFrame() ? s_lastScreen : GetDefaultScreen());
   if (!IsScreenAvailable(initial))
      initial = Screen::Tables;
   m_screen = initial;
   s_lastScreen = initial;
}

InGameUIPage* MainMenuPage::GetContent()
{
   auto& content = m_contents[static_cast<int>(m_screen)];
   if (content == nullptr)
   {
      content = CreateScreenContent(m_screen);
      if (content)
         content->Open(false);
   }
   return content.get();
}

void MainMenuPage::SetScreen(Screen screen)
{
   if (!IsScreenAvailable(screen) || screen == m_screen)
      return;
   // Pages are opened while shown, and closed when hidden, as when navigating between them: some change the renderer while they are opened
   // (and restore it when closed), and they get the current values when opened again
   if (auto& content = m_contents[static_cast<int>(m_screen)]; content)
      content->Close(false);
   m_screen = screen;
   s_lastScreen = screen;
   if (auto& content = m_contents[static_cast<int>(screen)]; content)
      content->Open(false);
}

void MainMenuPage::Close(bool isBackwardAnimation)
{
   InGameUIPage::Close(isBackwardAnimation);
   if (auto& content = m_contents[static_cast<int>(m_screen)]; content && content->IsActive())
      content->Close(isBackwardAnimation);
}

void MainMenuPage::StepScreen(int direction)
{
   int screen = static_cast<int>(m_screen);
   for (int i = 0; i < kScreenCount; i++)
   {
      screen = (screen + direction + kScreenCount) % kScreenCount;
      if (IsScreenAvailable(static_cast<Screen>(screen)))
         break;
   }
   SetScreen(static_cast<Screen>(screen));
}

void MainMenuPage::Render(float elapsedS)
{
   InGameUIPage::Render(elapsedS);
   // Switched to the classic view while opened: show its home page instead (the route gives the page of the active view)
   if (IsActive() && !VRUI::IsActive())
      Shell::NavigateWithFreshHistory(m_player->m_liveUI->m_inGameUI, "homepage"s);
}

void MainMenuPage::SelectNextItem()
{
   if (m_railFocus)
      StepScreen(1);
   else if (InGameUIPage* content = GetContent(); content)
      content->SelectNextItem();
}

void MainMenuPage::SelectPrevItem()
{
   if (m_railFocus)
      StepScreen(-1);
   else if (InGameUIPage* content = GetContent(); content)
      content->SelectPrevItem();
}

void MainMenuPage::AdjustItem(float direction, bool isInitialPress)
{
   if (m_railFocus)
   {
      // Right enters the screen
      if (direction > 0.f && isInitialPress)
         m_railFocus = false;
      return;
   }
   InGameUIPage* const content = GetContent();
   if (content == nullptr)
   {
      m_railFocus = true;
      return;
   }
   // Tiles move in their grid, left from its first column getting back to the rail
   if (isInitialPress && content->StepTileGrid(direction < 0.f ? -1 : 1, 0))
      return;
   // Left gets back to the rail, except on the items which use it (values, input mappings, and the custom items of the pages, like tabs)
   if (direction < 0.f && isInitialPress)
   {
      const auto* composed = dynamic_cast<const ComposedPage*>(content);
      const InGameUIItem* const item = composed ? composed->GetSelectedSectionItem() : content->GetSelectedItem();
      if (item == nullptr || item->m_type == InGameUIItem::Type::Navigation || item->m_type == InGameUIItem::Type::Runnable || item->m_type == InGameUIItem::Type::Label)
      {
         m_railFocus = true;
         return;
      }
   }
   content->AdjustItem(direction, isInitialPress);
}

void MainMenuPage::ActivateItem()
{
   if (m_railFocus)
      m_railFocus = false; // OK enters the screen
   else if (InGameUIPage* const content = GetContent(); content)
      content->ActivateItem();
   else
      m_railFocus = true;
}

bool MainMenuPage::IsDefaults() const
{
   const auto& content = m_contents[static_cast<int>(m_screen)];
   return content == nullptr || content->IsDefaults();
}

bool MainMenuPage::IsModified() const
{
   const auto& content = m_contents[static_cast<int>(m_screen)];
   return content != nullptr && content->IsModified();
}

void MainMenuPage::Save()
{
   if (InGameUIPage* content = GetContent(); content)
      content->Save();
}

void MainMenuPage::SaveGlobally()
{
   if (InGameUIPage* content = GetContent(); content)
      content->SaveGlobally();
}

void MainMenuPage::SaveTableOverride()
{
   if (InGameUIPage* content = GetContent(); content)
      content->SaveTableOverride();
}

void MainMenuPage::ResetToStoredValues()
{
   if (InGameUIPage* content = GetContent(); content)
      content->ResetToStoredValues();
}

void MainMenuPage::ResetToDefaults()
{
   if (InGameUIPage* content = GetContent(); content)
      content->ResetToDefaults();
}

bool MainMenuPage::IsPlayerPauseAllowed() const
{
   const auto& content = m_contents[static_cast<int>(m_screen)];
   return content == nullptr || content->IsPlayerPauseAllowed();
}

}
