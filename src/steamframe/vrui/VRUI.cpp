// license:GPLv3+

#include "core/stdafx.h"
#include "VRUI.h"

#include "MainMenuPage.h"
#include "SelfTest.h"

#include "steamframe/TablePickerPage.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/HomePage.h"

#include "imgui/imgui.h"

namespace VPX::VRUI
{

namespace
{
   enum class Choice { FromSetting, New, Classic };

   Choice GetEnvironmentChoice()
   {
      static const Choice choice = []()
      {
         const char* const env = getenv("VPX_VRUI");
         if (env == nullptr)
            return Choice::FromSetting;
         const string value = lowerCase(string(env));
         if (value == "new"s)
            return Choice::New;
         if (value == "classic"s)
            return Choice::Classic;
         PLOGW << "VPX_VRUI: unknown value '" << env << "' (expected 'new' or 'classic'), the Standalone.NewVRUI setting is used";
         return Choice::FromSetting;
      }();
      return choice;
   }

   bool ComputeActive()
   {
      switch (GetEnvironmentChoice())
      {
      case Choice::New: return true;
      case Choice::Classic: return false;
      case Choice::FromSetting: break;
      }
      return g_pplayer && g_pplayer->m_vrDevice && g_settingsService.GetAppSettings().GetStandalone_NewVRUI();
   }

   bool g_active = false;
   int g_latchedFrame = -1;
}

void LatchSwitch()
{
   if (ImGui::GetCurrentContext() == nullptr)
   {
      g_active = ComputeActive();
      return;
   }
   const int frame = ImGui::GetFrameCount();
   if (frame == g_latchedFrame)
      return;
   g_latchedFrame = frame;
   if (!ImGui::IsAnyItemActive())
      g_active = ComputeActive();
}

bool IsActive()
{
   LatchSwitch();
   return g_active;
}

void OnPlayerCreated(Player* player, bool isLobby)
{
   using namespace VPX::InGameUI;
   VPX::InGameUI::InGameUI& ui = player->m_liveUI->m_inGameUI;
   ui.AddPage("homepage"s, []() -> std::unique_ptr<InGameUIPage>
      {
         if (IsActive())
            return std::make_unique<MainMenuPage>();
         return std::make_unique<HomePage>();
      });
   ui.AddPage("tables/picker"s, []() -> std::unique_ptr<InGameUIPage>
      {
         if (IsActive())
            return std::make_unique<MainMenuPage>(Screen::Tables);
         return std::make_unique<TablePickerPage>();
      });
   SelfTest::OnPlayerCreated(player, isLobby);
}

}
