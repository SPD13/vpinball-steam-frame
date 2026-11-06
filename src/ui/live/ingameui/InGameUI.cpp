// license:GPLv3+

#include "core/stdafx.h"

#include "InGameUI.h"
#include "ui/live/LiveUI.h"

#include "AudioSettingsPage.h"
#include "CabinetSettingsPage.h"
#include "DisplayProfileSettingsPage.h"
#include "DisplaySettingsPage.h"
#include "GraphicSettingsPage.h"
#include "HomePage.h"
#include "InputProfilePage.h"
#include "InputSettingsPage.h"
#include "LoadingPage.h"
#include "LoggingSettingsPage.h"
#include "MiscSettingsPage.h"
#include "NudgeSettingsPage.h"
#include "PluginSettingsPage.h"
#include "PlungerSettingsPage.h"
#include "PointOfViewSettingsPage.h"
#include "StereoSettingsPage.h"
#include "TableMiscPage.h"
#include "TableOptionsPage.h"
#include "TableRulesPage.h"
#include "VRSettingsPage.h"
#include "SystemInfoPage.h"
#include "MessagePage.h"
#ifdef VPX_STEAM_FRAME
#include "steamframe/ScoresPage.h"
#include "steamframe/TablePickerPage.h"
#include "steamframe/vrui/Keyboard.h"
#include "steamframe/vrui/UpdateCheck.h"
#endif
#include "parts/ball.h"


namespace VPX::InGameUI
{

InGameUI::InGameUI(LiveUI &liveUI)
   : m_player(g_pplayer)
{
   AddPage("homepage"s, []() { return std::make_unique<HomePage>(); });
   AddPage("loading"s, []() { return std::make_unique<LoadingPage>(); });
   AddPage("settings/audio"s, []() { return std::make_unique<AudioSettingsPage>(); });
   AddPage("settings/cabinet"s, []() { return std::make_unique<CabinetSettingsPage>(); });
   AddPage("settings/display_profiles"s, []() { return std::make_unique<DisplayProfileSettingsPage>(); });
   AddPage("settings/displays"s, []() { return std::make_unique<DisplayHomePage>(); });
   AddPage("settings/graphics"s, []() { return std::make_unique<GraphicSettingsPage>(); });
   AddPage("settings/input"s, []() { return std::make_unique<InputSettingsPage>(); });
   AddPage("settings/logging"s, []() { return std::make_unique<LoggingSettingsPage>(); });
   AddPage("settings/misc"s, []() { return std::make_unique<MiscSettingsPage>(); });
   AddPage("settings/nudge"s, []() { return std::make_unique<NudgeSettingsPage>(); });
   AddPage("settings/plunger"s, []() { return std::make_unique<PlungerSettingsPage>(); });
   AddPage("settings/pov"s, []() { return std::make_unique<PointOfViewSettingsPage>(); });
   AddPage("settings/stereo"s, []() { return std::make_unique<StereoSettingsPage>(); });
   AddPage("settings/vr"s, []() { return std::make_unique<VRSettingsPage>(); });
   AddPage("table/general"s, []() { return std::make_unique<TableMiscPage>(); });
   AddPage("table/options"s, []() { return std::make_unique<TableOptionsPage>(); });
   AddPage("table/rules"s, []() { return std::make_unique<TableRulesPage>(); });
   AddPage("plugins/homepage"s, []() { return std::make_unique<PluginHomePage>(); });
   AddPage("misc/systeminfo"s, []() { return std::make_unique<SystemInfoPage>(); });
   AddPage("misc/message"s, []() { return std::make_unique<MessagePage>(); });
#ifdef VPX_STEAM_FRAME
   AddPage("tables/picker"s, []() { return std::make_unique<TablePickerPage>(); });
   AddPage("profiles"s, []() { return std::make_unique<ProfilesPage>(); });
   AddPage("scores/result"s, []() { return std::make_unique<ScoreResultPage>(); });
#endif
}

void InGameUI::AddPage(const string &path, const std::function<std::unique_ptr<InGameUIPage>()>& pageFactory)
{
   m_pages[path] = pageFactory;
}

void InGameUI::Navigate(const string &path, bool isBack)
{
   if (!m_activePages.empty() && m_activePages.back()->IsActive())
   {
      m_activePages.back()->Close(isBack);
   }
   auto it = m_pages.find(path);
   if (it == m_pages.end())
   {
      PLOGE << "InGameUI: unknown page '" << path << '\'';
      Close();
      return;
   }
#if defined(VPX_STEAM_FRAME)
   m_navigatingBack = isBack; // Steam Frame Edition: the main menu of the VR UI gets back to the screen and the item it opened a page from
   m_activePages.push_back(it->second());
   m_navigatingBack = false;
#else
   m_activePages.push_back(it->second());
#endif
   if (m_activePages.back())
   {
      m_navigationHistory.push_back(path);
      m_activePages.back()->Open(isBack);
   }
   else
   {
      PLOGE << "InGameUI: Failed to create page '" << path << '\'';
      m_activePages.pop_back();
      Close();
   }
}

void InGameUI::RequestRebuildAll()
{
   for (const auto &page : m_activePages)
      page->RequestRebuild();
}

void InGameUI::NavigateBack()
{
   assert(IsOpened());
   assert(!m_navigationHistory.empty());

   // The loading page is the navigation root while loading: it cannot be closed until the table is loaded
   if (m_player->m_isLoading && m_navigationHistory.size() == 1)
      return;

   m_navigationHistory.pop_back();
   if (m_navigationHistory.empty())
   {
      Close();
   }
   else
   {
      const string path = m_navigationHistory.back();
      m_navigationHistory.pop_back();
      Navigate(path, true);
   }
}

void InGameUI::Open(const string& page)
{
   assert(!IsOpened());
   Navigate(page);
#if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition: in VR, the controller points by default, until a button of the UI is pressed (see UseButtonNav)
   m_useFlipperNav = !m_player->m_vrDevice && m_player->m_ptable->GetViewMode() == ViewSetupID::BG_FULLSCREEN;
#else
   m_useFlipperNav = m_player->m_vrDevice || m_player->m_ptable->GetViewMode() == ViewSetupID::BG_FULLSCREEN;
#endif
}

#if defined(VPX_STEAM_FRAME)
bool InGameUI::UseButtonNav()
{
   const bool wasFlipperNav = m_useFlipperNav;
   m_useFlipperNav = true;
   SDL_HideCursor();
   return wasFlipperNav;
}
#endif

void InGameUI::Close()
{
   m_navigationHistory.clear();
   if (GetActivePage())
      GetActivePage()->Close(false);
   if (!m_player->IsPlaying(false))
      m_player->SetPlayState(true);
   m_player->m_ptable->FireOptionEvent(PinTable::OptionEventType::EndOfEdit);
}

void InGameUI::Update()
{
   const uint32_t now = msec();
   float elapsed = static_cast<float>(now - m_lastRenderMs) / 1000.f;
   if (elapsed > 1.f)
      elapsed = 0.f;
   m_lastRenderMs = now;
   if (m_activePages.empty())
      return;

   // Remove closed pages (after closing animation), except during popup (that will get back to previous page)
   if (m_navigationHistory.empty() || !m_navigationHistory.back().starts_with("popup/"))
   {
      for (auto it = m_activePages.begin(); it != m_activePages.end();)
      {
         if ((*it)->IsClosed())
            it = m_activePages.erase(it);
         else
            ++it;
      }
   }

   if (const InGameUIPage *const activePage = GetActivePage(); activePage && activePage->IsActive())
   {
      // Only pause player if balls are moving to keep attract mode if possible
      if (m_player->IsPlaying(false))
      {
         if (activePage->IsPlayerPauseAllowed())
         {
            bool ballMoving = false;
            for (const auto &ball : m_player->m_vball)
            {
               if (ball->GetVelocity().LengthSquared() > 0.25f)
               {
                  ballMoving = true;
                  break;
               }
            }
            if (ballMoving)
            {
               m_player->SetPlayState(false);
            }
         }
      }
      else if (!activePage->IsPlayerPauseAllowed())
      {
         m_player->SetPlayState(true);
      }

      HandlePageInput();
      HandleLegacyFlyOver();
   }

   // Copy list as it may be modified when the page is updated (for example when a navigation event is triggered)
   vector<InGameUIPage*> pages;
   pages.reserve(m_activePages.size());
   for (const auto &page : m_activePages)
      pages.push_back(page.get());
   for (const auto &page : pages)
      page->Render(elapsed);
}

void InGameUI::OnUIUpAction()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   OnUIStep(1); // Steam Frame Edition: UI: Next Item
   return;
#endif

   const bool wasFlipperNav = m_useFlipperNav;
   m_useFlipperNav = true;
   m_flipperNavStart = msec();
   m_flipperNavRepeatCount = 0;
   if (m_player->ShouldHideDesktopCursor())
      SDL_HideCursor();
   GetActivePage()->SelectPrevItem();
   if (!wasFlipperNav)
      GetActivePage()->SelectNextItem();
}

void InGameUI::OnUIDownAction()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   OnUIStep(-1); // Steam Frame Edition: UI: Previous Item
   return;
#endif

   const bool wasFlipperNav = m_useFlipperNav;
   m_useFlipperNav = true;
   m_flipperNavStart = msec();
   m_flipperNavRepeatCount = 0;
   if (m_player->ShouldHideDesktopCursor())
      SDL_HideCursor();
   GetActivePage()->SelectNextItem();
   if (!wasFlipperNav)
      GetActivePage()->SelectPrevItem();
}

void InGameUI::OnUILeftAction()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   OnUINavigateBack(); // Steam Frame Edition: UI: Cancel
   return;
#endif
   if (m_useFlipperNav)
      GetActivePage()->AdjustItem(-1, true);
}

void InGameUI::OnUIRightAction()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   OnUIConfirm(); // Steam Frame Edition: UI: Confirm
   return;
#endif
   if (m_useFlipperNav)
      GetActivePage()->AdjustItem(1, true);
}

void InGameUI::OnUIResetToDefaults()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   UseButtonNav();
#endif
   GetActivePage()->ResetToDefaults();
}

void InGameUI::OnUICancelChanges()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   UseButtonNav();
#endif
   GetActivePage()->ResetToStoredValues();
}

void InGameUI::OnUISaveChanges()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   UseButtonNav();
#endif
   GetActivePage()->Save();
}

#if defined(VPX_STEAM_FRAME)
void InGameUI::OnUIConfirm()
{
   if (ImGui::IsAnyItemActive() || IsSameButtonPress(ButtonUse::Confirm))
      return;

   if (VRUI::Keyboard::HandleButton(VRUI::Keyboard::Button::Confirm))
      return;
   if (!UseButtonNav()) // The first press only shows the selected item, as Next and Previous do
      return;
   // Editing a value: Confirm keeps it
   if (GetEditedPage())
   {
      EndEdit(true);
      return;
   }
   // A slider or a choice: Confirm starts editing it, Next/Previous change it
   if (InGameUIPage* const target = GetActivePage()->GetButtonTarget(); target)
      if (const InGameUIItem* const item = target->GetFocusedItem(); item && target->IsEditableItem(*item))
      {
         BeginEdit(*target, *item);
         return;
      }
   GetActivePage()->ActivateItem();
}

// UI: Next Item, UI: Previous Item: the next/previous stop of the page (StepItem), the value of the edited item, the key of the keyboard
void InGameUI::OnUIStep(int direction)
{
   if (IsSameButtonPress(direction > 0 ? ButtonUse::Next : ButtonUse::Previous))
      return;
   if (VRUI::Keyboard::HandleButton(direction > 0 ? VRUI::Keyboard::Button::Right : VRUI::Keyboard::Button::Left)) // In reading order
      return;
   m_flipperNavStart = msec();
   m_flipperNavRepeatCount = 0;
   if (!UseButtonNav()) // The first press only shows the selected item
      return;
   if (InGameUIPage* const edited = GetEditedPage())
      edited->AdjustItem(static_cast<float>(direction), true);
   else
      GetActivePage()->StepItem(direction, InGameUIPage::StepPhase::Press);
}

// A key mapped to two actions with the same use in the menus (Escape: Exit Game and UI: Cancel, Enter: Launch Ball and UI: Confirm in VR)
// sends both on the same frame: only the first one is used
bool InGameUI::IsSameButtonPress(ButtonUse use)
{
   const int frame = ImGui::GetFrameCount();
   int& last = m_lastButtonFrame[static_cast<int>(use)];
   const bool same = last == frame;
   last = frame;
   return same;
}

// UI: Up, UI: Down (a full D-pad): the row above/below in a grid of tiles and on the keyboard, otherwise as Previous/Next (StepRow). Up
// increases an edited value, Down decreases it.
void InGameUI::OnUIRow(int direction)
{
   if (ImGui::IsAnyItemActive() || IsSameButtonPress(direction < 0 ? ButtonUse::Up : ButtonUse::Down))
      return;
   if (VRUI::Keyboard::HandleButton(direction < 0 ? VRUI::Keyboard::Button::Up : VRUI::Keyboard::Button::Down))
      return;
   m_flipperNavStart = msec();
   m_flipperNavRepeatCount = 0;
   if (!UseButtonNav()) // The first press only shows the selected item
      return;
   if (InGameUIPage* const edited = GetEditedPage())
      edited->AdjustItem(static_cast<float>(-direction), true);
   else
      GetActivePage()->StepRow(direction, InGameUIPage::StepPhase::Press);
}

// The edited value is still the focused item of the page the buttons act on (otherwise its editing ended: other page, pointer used)
InGameUIPage* InGameUI::GetEditedPage()
{
   if (m_editedPage == nullptr)
      return nullptr;
   InGameUIPage* const target = GetActivePage() ? GetActivePage()->GetButtonTarget() : nullptr;
   const InGameUIItem* const item = target == m_editedPage && m_useFlipperNav ? target->GetFocusedItem() : nullptr;
   if (item == nullptr || target->GetEditKey(*item) != m_editedKey)
      m_editedPage = nullptr;
   return m_editedPage;
}

bool InGameUI::IsEditing(const InGameUIItem* item)
{
   InGameUIPage* const page = GetEditedPage();
   return page && item && page->GetFocusedItem() == item;
}

void InGameUI::BeginEdit(InGameUIPage& page, const InGameUIItem& item)
{
   m_editedPage = &page;
   m_editedKey = page.GetEditKey(item);
   m_editedRestorable = item.m_type == InGameUIItem::Type::Property;
   if (m_editedRestorable && item.m_property->m_type == VPX::Properties::PropertyDef::Type::Float)
      m_editedFloat = item.GetFloatValue();
   else if (m_editedRestorable)
      m_editedInt = item.GetIntValue();
}

void InGameUI::EndEdit(bool keep)
{
   InGameUIPage* const page = GetEditedPage();
   m_editedPage = nullptr;
   if (page == nullptr)
      return;
   page->ReleaseAdjustedItem(); // Snaps a float to its steps
   if (const InGameUIItem* const item = page->GetFocusedItem(); !keep && m_editedRestorable && item && item->m_type == InGameUIItem::Type::Property)
   {
      if (item->m_property->m_type == VPX::Properties::PropertyDef::Type::Float)
         item->SetValue(m_editedFloat);
      else
         item->SetValue(m_editedInt);
   }
}
#endif

void InGameUI::OnUINavigateBack()
{
   if (ImGui::IsAnyItemActive())
      return;

#if defined(VPX_STEAM_FRAME)
   if (IsSameButtonPress(ButtonUse::Back))
      return;
   if (VRUI::Keyboard::HandleButton(VRUI::Keyboard::Button::Back))
      return;
   // Editing a value: Cancel restores it
   if (GetEditedPage())
   {
      UseButtonNav();
      EndEdit(false);
      return;
   }
   const bool wasButtonNav = UseButtonNav();
   if (GetActivePage() && GetActivePage()->HandleBack(wasButtonNav))
      return;
#endif
   NavigateBack();
}

void InGameUI::HandlePageInput()
{
   // Disable keyboard shortcut if no control editing is in progress
   if (ImGui::IsAnyItemActive())
      return;

   // If user has moved the mouse, disable flipper navigation
   if (m_useFlipperNav && !m_player->m_vrDevice)
   {
      ImVec2 delta = m_prevMousePos - ImGui::GetMousePos();
      m_useFlipperNav &= fabs(delta.x) <= 3.f && fabs(delta.y) <= 3.f;
   }
   m_prevMousePos = ImGui::GetMousePos();

   // Allow pages to force flipper navigation (needed by anaglyph calibration)
   for (const auto &page : m_activePages)
      m_useFlipperNav |= page->IsFlipperNavNeeded();

#if defined(VPX_STEAM_FRAME)
   if (VRUI::Keyboard::HasFocus()) // The buttons select the keys of the keyboard of the new VR UI, one per press
      return;
   if (VRUI::UpdateCheck::IsInstalling()) // The menu shows only the progress of the update, its items stay as they are
      return;
#endif

   // Handle continuous press navigation
   const uint32_t now = msec();
   const uint32_t flipperNavRepeat = m_flipperNavRepeatCount == 0 ? 500 : m_flipperNavRepeatCount == 1 ? 250 : 125;
#if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition: Next/Previous and Up/Down held repeat their step, or change the edited value continuously (Confirm and Cancel do
   // not repeat)
   const InputManager& input = m_player->m_pininput;
   const bool nextHeld = m_useFlipperNav && input.IsPressed(input.GetUIUpActionId());
   const bool prevHeld = m_useFlipperNav && input.IsPressed(input.GetUIDownActionId());
   const bool upHeld = m_useFlipperNav && input.IsPressed(input.GetUIRowUpActionId());
   const bool downHeld = m_useFlipperNav && input.IsPressed(input.GetUIRowDownActionId());
   const int stepDirection = nextHeld == prevHeld ? 0 : nextHeld ? 1 : -1;
   const int rowDirection = upHeld == downHeld ? 0 : downHeld ? 1 : -1;
   InGameUIPage* const edited = GetEditedPage();
   const int valueDirection = stepDirection != 0 ? stepDirection : -rowDirection;
   if (valueDirection != 0 && edited)
      edited->AdjustItem(static_cast<float>(valueDirection), false);
   else if (stepDirection != 0 || rowDirection != 0)
   {
      const bool repeat = now - m_flipperNavStart > flipperNavRepeat;
      if (repeat)
      {
         m_flipperNavStart = now;
         m_flipperNavRepeatCount++;
      }
      InGameUIPage* const page = GetActivePage();
      if (stepDirection != 0)
      {
         if (repeat)
            page->StepItem(stepDirection, InGameUIPage::StepPhase::Repeat);
         page->StepItem(stepDirection, InGameUIPage::StepPhase::Held);
      }
      else
      {
         if (repeat)
            page->StepRow(rowDirection, InGameUIPage::StepPhase::Repeat);
         page->StepRow(rowDirection, InGameUIPage::StepPhase::Held);
      }
   }
   else if (m_itemAdjustWasPressed && edited)
      edited->ReleaseAdjustedItem();
   m_itemAdjustWasPressed = valueDirection != 0;
   return;
#endif
   if (m_useFlipperNav && m_player->m_pininput.IsPressed(m_player->m_pininput.GetUIUpActionId()) && (now - m_flipperNavStart) > flipperNavRepeat)
   {
      m_flipperNavStart = now;
      m_flipperNavRepeatCount++;
      GetActivePage()->SelectPrevItem();
   }
   else if (m_useFlipperNav && m_player->m_pininput.IsPressed(m_player->m_pininput.GetUIDownActionId()) && (now - m_flipperNavStart) > flipperNavRepeat)
   {
      m_flipperNavStart = now;
      m_flipperNavRepeatCount++;
      GetActivePage()->SelectNextItem();
   }

   // Handle continuous press item adjustment, applying remaining adjustment on release
   const bool leftPressed = m_useFlipperNav && m_player->m_pininput.IsPressed(m_player->m_pininput.GetUILeftActionId());
   const bool rightPressed = m_useFlipperNav && m_player->m_pininput.IsPressed(m_player->m_pininput.GetUIRightActionId());
   if (leftPressed)
      GetActivePage()->AdjustItem(-1, false);
   else if (rightPressed)
      GetActivePage()->AdjustItem(1, false);
   else if (m_itemAdjustWasPressed)
      GetActivePage()->ReleaseAdjustedItem();
   m_itemAdjustWasPressed = leftPressed || rightPressed;
}

// Legacy keyboard fly camera when in ingame option
// FIXME Remove
void InGameUI::HandleLegacyFlyOver()
{
   if (!g_settingsService.GetActiveSettings().GetPlayer_EnableCameraModeFlyAround())
      return;

   if (!ImGui::IsKeyDown(ImGuiKey_LeftAlt) && !ImGui::IsKeyDown(ImGuiKey_RightAlt))
   {
      if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
         m_player->m_renderer->m_cam.x += 10.0f;
      if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
         m_player->m_renderer->m_cam.x -= 10.0f;
      if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
         m_player->m_renderer->m_cam.y += 10.0f;
      if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
         m_player->m_renderer->m_cam.y -= 10.0f;
   }
   else
   {
      if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
         m_player->m_renderer->m_cam.z += 10.0f;
      if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
         m_player->m_renderer->m_cam.z -= 10.0f;
      if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
         m_player->m_renderer->m_inc += 0.01f;
      if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
         m_player->m_renderer->m_inc -= 0.01f;
   }

   if (m_player->m_pininput.IsPressed(m_player->m_pininput.GetLeftNudgeActionId()))
      m_player->m_ptable->GetViewSetup().mViewportRotation -= 1.0f;

   if (m_player->m_pininput.IsPressed(m_player->m_pininput.GetRightNudgeActionId()))
      m_player->m_ptable->GetViewSetup().mViewportRotation += 1.0f;
}

bool InGameUI::ProposeInputLayout(const string& deviceName, const std::function<void(bool, bool)>& handler)
{
   if (IsOpened())
      return false;
   m_pages["popup/new_input_profile"s] = [deviceName, handler]() { return std::make_unique<InputProfilePage>(deviceName, handler); };
   Open("popup/new_input_profile"s);
   return true;
}

}
