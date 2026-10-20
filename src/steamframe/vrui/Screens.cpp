// license:GPLv3+

#include "core/stdafx.h"
#include "Screens.h"

#include "VRUI.h"

#include "core/TournamentFile.h"
#include "fonts/IconsForkAwesome.h"
#include "renderer/Renderer.h"
#include "steamframe/Launcher.h"
#include "steamframe/ScoreStore.h"
#include "steamframe/ScoresPage.h"
#include "steamframe/TablePickerPage.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/AudioSettingsPage.h"
#include "ui/live/ingameui/GraphicSettingsPage.h"
#include "ui/live/ingameui/InputSettingsPage.h"
#include "ui/live/ingameui/PluginSettingsPage.h"
#include "ui/live/ingameui/PointOfViewSettingsPage.h"
#include "ui/live/ingameui/VRSettingsPage.h"

namespace VPX::VRUI
{

using namespace VPX::InGameUI;

namespace
{

// Actions and options of the table being played (in the classic menu, the 'Table options' part of the home page)
class ThisTablePage final : public InGameUIPage
{
public:
   ThisTablePage()
      : InGameUIPage("This table"s, ""s, SaveMode::None)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>("Resume"s, "Close the menu and get back to the game"s, [this]() { m_player->m_liveUI->m_inGameUI.Close(); }));
      if (!m_player->m_ptable->m_rules.empty())
         AddItem(std::make_unique<InGameUIItem>("Table Rules"s, ""s, "table/rules"s));
      if (!m_player->m_ptable->GetOptions().empty())
         AddItem(std::make_unique<InGameUIItem>("Table Options"s, "Options defined by the table"s, "table/options"s));
      AddItem(std::make_unique<InGameUIItem>("Generic Options"s, "Difficulty, lighting and other options which apply to any table"s, "table/general"s));
      if (m_player->m_vrDevice == nullptr)
         AddItem(std::make_unique<InGameUIItem>("Point Of View"s, ""s, "settings/pov"s));
      Launcher::AddHomePageTableItems(*this);
      if (m_player->m_ptable->TournamentModePossible())
         AddItem(std::make_unique<InGameUIItem>("Generate Tournament File"s, ""s,
            [this]()
            {
               m_player->m_liveUI->HideUI();
               VPX::TournamentFile::GenerateTournamentFile();
            }));
   }
};

// Who is playing, and the leaderboards of the tables (in the classic menu, the 'Scores' tab of the table picker)
class ScoresScreenPage final : public InGameUIPage
{
public:
   ScoresScreenPage()
      : InGameUIPage("Scores"s, "Scores of the games played with the tables of the library"s, SaveMode::None)
   {
   }

   void Render(float elapsedS) override
   {
      if (m_revision != g_launcher->GetScoreStore().GetRevision())
         RequestRebuild();
      InGameUIPage::Render(elapsedS);
   }

private:
   void BuildPage() override
   {
      m_revision = g_launcher->GetScoreStore().GetRevision();
      AddPlayerItem(*this);
      BuildLeaderboardList(*this);
   }

   uint64_t m_revision = 0;
};

// Library, the settings without a screen of their own, the menu and the application
class SystemPage final : public InGameUIPage
{
public:
   SystemPage()
      : InGameUIPage("System"s, ""s, SaveMode::None)
   {
   }

private:
   void BuildPage() override
   {
      AddLibraryItems(*this);
      AddLobbyRoomItems(*this);

      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "More settings"s));
      AddItem(std::make_unique<InGameUIItem>("Display Settings"s, ""s, "settings/displays"s));
      AddItem(std::make_unique<InGameUIItem>("Alpha/DMD Profile Settings"s, ""s, "settings/display_profiles"s));
      AddItem(std::make_unique<InGameUIItem>("Nudge & Tilt Settings"s, ""s, "settings/nudge"s));
      AddItem(std::make_unique<InGameUIItem>("Plunger Settings"s, ""s, "settings/plunger"s));
      AddItem(std::make_unique<InGameUIItem>("Cabinet Settings"s, ""s, "settings/cabinet"s));
      #ifndef ENABLE_DX9
      if (m_player->m_renderer->m_stereo3D != STEREO_VR)
         AddItem(std::make_unique<InGameUIItem>("Stereo Settings"s, ""s, "settings/stereo"s));
      #endif
      AddItem(std::make_unique<InGameUIItem>("Legacy DOF & B2S Settings"s, ""s, "settings/misc"s));
      AddItem(std::make_unique<InGameUIItem>("Logging Settings"s, ""s, "settings/logging"s));
      AddItem(std::make_unique<InGameUIItem>("System Info"s, ""s, "misc/systeminfo"s));

      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Menu"s));
      AddItem(std::make_unique<InGameUIItem>("Use the classic VR menu"s, "Switch back to the classic menu (it can be switched again from the MENU tab of its table picker)"s,
         [this]()
         {
            g_settingsService.GetAppSettings().SetStandalone_NewVRUI(false, false);
            g_settingsService.GetAppSettings().Save();
            if (getenv("VPX_VRUI") != nullptr)
               m_player->m_liveUI->PushNotification("The VPX_VRUI environment variable keeps the new menu: unset it to use the classic one"s, 6000);
         }));

      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Application"s));
      AddQuitItem(*this);
   }
};

#ifdef VPX_TABLE_WEBSERVER
// Adding tables from the browser of a phone or computer
class WiFiPage final : public InGameUIPage
{
public:
   WiFiPage()
      : InGameUIPage("Wi-Fi upload"s, "Add, rename or delete tables from the browser of a phone or computer on the same network"s, SaveMode::None)
   {
   }

   void Render(float elapsedS) override
   {
      if (HaveWebServerItemsChanged())
         RequestRebuild();
      InGameUIPage::Render(elapsedS);
   }

   void AdjustItem(float direction, bool isInitialPress) override
   {
      if (const InGameUIItem* item = GetSelectedItem(); isInitialPress && item && AdjustWebServerItem(*item))
         return;
      InGameUIPage::AdjustItem(direction, isInitialPress);
   }

private:
   void BuildPage() override { AddWebServerItems(*this); }
};
#endif

}

bool IsScreenAvailable(Screen screen)
{
   switch (screen)
   {
   case Screen::ThisTable: return g_pplayer && !g_pplayer->m_isLobby;
   case Screen::WiFi:
      #ifdef VPX_TABLE_WEBSERVER
      return true;
      #else
      return false;
      #endif
   case Screen::Tables:
   case Screen::Scores:
   case Screen::Room:
   case Screen::Display:
   case Screen::Sound:
   case Screen::Controls:
   case Screen::Plugins:
   case Screen::System: return true;
   case Screen::Count: break;
   }
   return false;
}

const char* GetScreenIcon(Screen screen)
{
   switch (screen)
   {
   case Screen::Tables: return ICON_FK_TH_LARGE;
   case Screen::ThisTable: return ICON_FK_PLAY;
   case Screen::Scores: return ICON_FK_TROPHY;
   case Screen::Room: return ICON_FK_CUBE;
   case Screen::Display: return ICON_FK_TACHOMETER;
   case Screen::Sound: return ICON_FK_VOLUME_UP;
   case Screen::Controls: return ICON_FK_GAMEPAD;
   case Screen::Plugins: return ICON_FK_PUZZLE_PIECE;
   case Screen::System: return ICON_FK_COG;
   case Screen::WiFi: return ICON_FK_WIFI;
   case Screen::Count: break;
   }
   return "";
}

const char* GetScreenName(Screen screen)
{
   switch (screen)
   {
   case Screen::Tables: return "Tables";
   case Screen::ThisTable: return "This table";
   case Screen::Scores: return "Scores";
   case Screen::Room: return g_pplayer && g_pplayer->m_vrDevice ? "VR & Room" : "Point of view";
   case Screen::Display: return "Display & Performance";
   case Screen::Sound: return "Sound";
   case Screen::Controls: return "Controls";
   case Screen::Plugins: return "Plugins";
   case Screen::System: return "System";
   case Screen::WiFi: return "Wi-Fi upload";
   case Screen::Count: break;
   }
   return "";
}

std::unique_ptr<InGameUIPage> CreateScreenContent(Screen screen)
{
   switch (screen)
   {
   case Screen::Tables: return std::make_unique<TablePickerPage>();
   case Screen::ThisTable: return std::make_unique<ThisTablePage>();
   case Screen::Scores: return std::make_unique<ScoresScreenPage>();
   case Screen::Room:
      if (g_pplayer->m_vrDevice)
         return std::make_unique<VRSettingsPage>();
      return std::make_unique<PointOfViewSettingsPage>();
   case Screen::Display: return std::make_unique<GraphicSettingsPage>();
   case Screen::Sound: return std::make_unique<AudioSettingsPage>();
   case Screen::Controls: return std::make_unique<InputSettingsPage>();
   case Screen::Plugins: return std::make_unique<PluginHomePage>();
   case Screen::System: return std::make_unique<SystemPage>();
   case Screen::WiFi:
      #ifdef VPX_TABLE_WEBSERVER
      return std::make_unique<WiFiPage>();
      #else
      break;
      #endif
   case Screen::Count: break;
   }
   return nullptr;
}

Screen GetDefaultScreen()
{
   return IsScreenAvailable(Screen::ThisTable) ? Screen::ThisTable : Screen::Tables;
}

}
