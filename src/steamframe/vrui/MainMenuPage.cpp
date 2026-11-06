// license:GPLv3+

#include "core/stdafx.h"
#include "MainMenuPage.h"

#include "ComposedPage.h"
#include "PageView.h"
#include "Shell.h"
#include "VRUI.h"

#include "ui/live/LiveUI.h"

namespace VPX::VRUI
{

using namespace VPX::InGameUI;

static Screen s_lastScreen = Screen::Tables;
static int s_lastPosition = -1; // The selected item of the screen shown last, when it opened a page

Screen MainMenuPage::GetLastScreen() { return s_lastScreen; }

void MainMenuPage::SetLastScreen(Screen screen) { s_lastScreen = screen; }

MainMenuPage::MainMenuPage(std::optional<Screen> screen)
   : InGameUIPage("Menu"s, ""s, SaveMode::None)
{
   // Getting back to the menu from one of its sub-pages shows the screen it was on, on the item which opened the page (whatever the route of
   // the menu: the cabinet launcher opens it on the Tables screen); opening it shows the given or default one
   const bool isBack = m_player->m_liveUI->m_inGameUI.IsNavigatingBack();
   Screen initial = isBack ? s_lastScreen : screen.value_or(Shell::WasShownLastFrame() ? s_lastScreen : GetDefaultScreen());
   m_restorePosition = isBack && s_lastPosition >= 0;
   if (!IsScreenAvailable(initial))
      initial = Screen::Tables;
   m_screen = initial;
   s_lastScreen = initial;
   // Two levels with the buttons: the rail first, OK entering the screen; back from a sub-page, on the content it was opened from
   m_railFocus = !Shell::WasShownLastFrame();
}

InGameUIPage* MainMenuPage::GetContent()
{
   auto& content = m_contents[static_cast<int>(m_screen)];
   if (content == nullptr)
   {
      content = CreateScreenContent(m_screen);
      if (content)
         content->Open(false);
      if (content && std::exchange(m_restorePosition, false))
         content->SetNavPosition(s_lastPosition);
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
   // Kept to get back to the item when a page it opened is left (see the constructor)
   const auto& current = m_contents[static_cast<int>(m_screen)];
   s_lastPosition = current ? current->GetNavPosition() : -1;
   InGameUIPage::Close(isBackwardAnimation);
   if (auto& content = m_contents[static_cast<int>(m_screen)]; content && content->IsActive())
      content->Close(isBackwardAnimation);
}

void MainMenuPage::StepScreen(int direction)
{
   // The screens of the rail (from one opened by another, the one of the rail it was opened from)
   int screen = static_cast<int>(GetRailScreen(m_screen));
   for (int i = 0; i < kScreenCount; i++)
   {
      screen = (screen + direction + kScreenCount) % kScreenCount;
      if (IsScreenOnRail(static_cast<Screen>(screen)))
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

// Values edited with the buttons (see InGameUI::OnUIConfirm), and clicks on the items
void MainMenuPage::AdjustItem(float direction, bool isInitialPress)
{
   if (m_railFocus)
      return;
   if (InGameUIPage* const content = GetContent(); content)
      content->AdjustItem(direction, isInitialPress);
   else
      m_railFocus = true;
}

void MainMenuPage::ReleaseAdjustedItem()
{
   if (InGameUIPage* const content = GetContent(); content && !m_railFocus)
      content->ReleaseAdjustedItem();
}

// Next/Previous: the screens on the rail, the stops of the screen once entered with Confirm (Cancel gets back to the rail, see HandleBack)
void MainMenuPage::StepItem(int direction, StepPhase phase)
{
   if (m_railFocus)
   {
      if (phase != StepPhase::Held)
         StepScreen(direction);
      return;
   }
   if (InGameUIPage* const content = GetContent(); content)
      content->StepItem(direction, phase);
   else
      m_railFocus = true;
}

// Up/Down: the screens on the rail (as Previous/Next), the rows of the screen
void MainMenuPage::StepRow(int direction, StepPhase phase)
{
   InGameUIPage* const content = m_railFocus ? nullptr : GetContent();
   if (content)
      content->StepRow(direction, phase);
   else
      StepItem(direction, phase);
}

InGameUIPage* MainMenuPage::GetButtonTarget()
{
   InGameUIPage* const content = m_railFocus ? nullptr : GetContent();
   return content ? content->GetButtonTarget() : this;
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

// Button navigation (a cabinet, a pinball controller in VR, the sticks of the VR controllers): Cancel gets back to the rail from the content,
// and leaves the menu from the rail. While pointing (VR controllers, mouse), Back leaves the menu at once, as before.
bool MainMenuPage::HandleBack(bool wasButtonNav)
{
   if (!wasButtonNav || m_railFocus)
      return false;
   // The screen first, for its own levels (the tables of the Tables screen get back to its tabs)
   if (InGameUIPage* const content = GetContent(); content && content->HandleBack(wasButtonNav))
      return true;
   // A screen opened from another (VPX Performance, Device Performance): back to that one, on the item which opened it
   if (const Screen parent = GetRailScreen(m_screen); parent != m_screen)
   {
      BackToRailScreen();
      return true;
   }
   m_railFocus = true;
   return true;
}

void MainMenuPage::BackToRailScreen()
{
   const Screen from = m_screen;
   SetScreen(GetRailScreen(from));
   if (InGameUIPage* const content = GetContent(); content)
      InGameUIPageView::SetSelectedIndex(*content, from == Screen::Display ? 1 : 0); // Device Performance first, VPX Performance second
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
