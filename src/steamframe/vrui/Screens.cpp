// license:GPLv3+

#include "core/stdafx.h"
#include "Screens.h"

#include "ComposedPage.h"
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
#include "ui/live/ingameui/TableMiscPage.h"
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

// Library and the settings without a screen of their own
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
      #ifndef ENABLE_DX9
      if (m_player->m_renderer->m_stereo3D != STEREO_VR)
         AddItem(std::make_unique<InGameUIItem>("Stereo Settings"s, ""s, "settings/stereo"s));
      #endif
      AddItem(std::make_unique<InGameUIItem>("Legacy DOF & B2S Settings"s, ""s, "settings/misc"s));
      AddItem(std::make_unique<InGameUIItem>("Logging Settings"s, ""s, "settings/logging"s));
      AddItem(std::make_unique<InGameUIItem>("System Info"s, ""s, "misc/systeminfo"s));
   }
};

// Leaving the application (Quit screen)
class QuitPage final : public InGameUIPage
{
public:
   QuitPage()
      : InGameUIPage("Quit"s, "Close Visual Pinball"s, SaveMode::None)
   {
   }

private:
   void BuildPage() override { AddQuitItem(*this); }
};

// Placement of the menu, and room of the lobby (VR & Room screen)
class MenuAndLobbyPage final : public InGameUIPage
{
public:
   MenuAndLobbyPage()
      : InGameUIPage("Menu and lobby"s, ""s, SaveMode::None)
   {
   }

private:
   void BuildPage() override
   {
      AddMenuDistanceItem(*this);
      AddLobbyRoomItems(*this);
   }
};

// The controls which have pages of their own (Controls screen)
class MoreControlsPage final : public InGameUIPage
{
public:
   MoreControlsPage()
      : InGameUIPage("More controls"s, ""s, SaveMode::None)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>("Nudge & Tilt Settings"s, ""s, "settings/nudge"s));
      AddItem(std::make_unique<InGameUIItem>("Plunger Settings"s, ""s, "settings/plunger"s));
      AddItem(std::make_unique<InGameUIItem>("Cabinet Settings"s, ""s, "settings/cabinet"s));
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

// Pages shown by the composed screens
constexpr const char* kVR = "settings/vr";
constexpr const char* kGraphics = "settings/graphics";
constexpr const char* kAudio = "settings/audio";
constexpr const char* kInput = "settings/input";
constexpr const char* kTableMisc = "table/general";
constexpr const char* kThisTable = "vrui/table";
constexpr const char* kMenuAndLobby = "vrui/menu-lobby";
constexpr const char* kMoreControls = "vrui/more-controls";

// Sections of the composed screens: how the options of upstream's pages (VR settings, graphics, sound...) are grouped by the new menu.
// The items of a page which no section lists (an option added upstream) are shown by its 'rest' section.
static vector<Section> GetSections(Screen screen)
{
   switch (screen)
   {
   case Screen::ThisTable:
      return {
         Section { ""s, kThisTable, {}, true },
         Section { "Table settings"s, kTableMisc, {}, true },
      };

   case Screen::Room:
      return {
         Section { "Position"s, kVR, { Prop(Settings::m_propPlayerVR_Orientation), Prop(Settings::m_propPlayerVR_TableX), Prop(Settings::m_propPlayerVR_TableY), Prop(Settings::m_propPlayerVR_TableZ) } },
         Section { "Cabinet"s, kVR,
            { Prop(Settings::m_propPlayer_LockbarWidth), Prop(Settings::m_propPlayer_LockbarHeight), Prop(Settings::m_propPlayer_ScreenPlayerX), Prop(Settings::m_propPlayer_ScreenPlayerY),
               Prop(Settings::m_propPlayerVR_ControllerCabYOffset), Prop(Settings::m_propPlayerVR_ControllerLockbarScale) } },
         Section { "Room"s, kVR,
            { Prop(Settings::m_propPlayerVR_UsePassthroughColor), Prop(Settings::m_propPlayerVR_LockFeetToGround), Prop(Settings::m_propPlayerVR_AddBackglass),
               Prop(Settings::m_propPlayerVR_ShowControllers) } },
         Section { "Menu and lobby"s, kMenuAndLobby, {}, true },
         Section { "More"s, kVR, {}, true },
      };

   case Screen::Display:
      return {
         Section { "VR rendering"s, kVR,
            { Prop(Settings::m_propPlayerVR_DisplayRefreshRate), Prop(Settings::m_propPlayerVR_Foveation), Prop(Settings::m_propPlayerVR_FoveationEyeTracked), Label("Foveation status"s),
               Label("Render resolution status"s), Prop(Settings::m_propPlayerVR_DynamicResolution), Prop(Settings::m_propPlayerVR_DynamicResolutionTarget),
               Prop(Settings::m_propPlayerVR_DynamicResolutionMinScale), Label("Dynamic resolution status"s) },
            false, false },
         Section { "Desktop mirror"s, kVR, { Prop(Settings::m_propPlayerVR_DesktopDisplay), Prop(Settings::m_propPlayerVR_ShrinkDesktopDisplay) }, false, false },
         Section { ""s, kGraphics, {}, true },
      };

   case Screen::Sound:
      return {
         Section { ""s, kAudio, {}, true },
         Section { "Spatial audio in VR"s, kVR, { Prop(Settings::m_propPlayerVR_SpatialAudioBackglass), Prop(Settings::m_propPlayerVR_SpatialAudioPlayfield) }, false, false },
      };

   case Screen::Controls:
      return {
         Section { ""s, kInput, {}, true },
         Section { "Nudge, plunger and cabinet"s, kMoreControls, {}, true },
      };

   default: return {};
   }
}

// The items listed by the sections are not shown by the 'rest' sections of their page, on any screen
static void ClaimLayoutItems()
{
   static bool claimed = false;
   if (std::exchange(claimed, true))
      return;
   for (int i = 0; i < kScreenCount; i++)
      for (const Section& section : GetSections(static_cast<Screen>(i)))
         if (!section.rest)
            ClaimItems(section.source, section.keys);
}

static std::unique_ptr<InGameUIPage> CreateComposed(Screen screen, const string& title, const string& info, vector<SourcePage> sources)
{
   ClaimLayoutItems();
   auto page = std::make_unique<ComposedPage>(title, info, std::move(sources), GetSections(screen));
   return page;
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
   case Screen::System:
   case Screen::Quit: return true;
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
   case Screen::Quit: return ICON_FK_SIGN_OUT;
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
   case Screen::Quit: return "Quit";
   case Screen::Count: break;
   }
   return "";
}

std::unique_ptr<InGameUIPage> CreateScreenContent(Screen screen)
{
   // The VR settings are only available in VR (their items use the VR device)
   const bool vr = g_pplayer->m_vrDevice != nullptr;
   const auto source = [](const char* id, std::unique_ptr<InGameUIPage> page) { return SourcePage { id, std::move(page) }; };
   vector<SourcePage> sources;
   switch (screen)
   {
   case Screen::Tables: return std::make_unique<TablePickerPage>(true);
   case Screen::ThisTable:
      sources.push_back(source(kThisTable, std::make_unique<ThisTablePage>()));
      sources.push_back(source(kTableMisc, std::make_unique<TableMiscPage>()));
      return CreateComposed(screen, "This table"s, ""s, std::move(sources));
   case Screen::Scores: return std::make_unique<ScoresScreenPage>();
   case Screen::Room:
      if (!vr)
         return std::make_unique<PointOfViewSettingsPage>();
      sources.push_back(source(kVR, std::make_unique<VRSettingsPage>()));
      sources.push_back(source(kMenuAndLobby, std::make_unique<MenuAndLobbyPage>()));
      return CreateComposed(screen, "VR & Room"s, "Where the table and the cabinet stand in the room"s, std::move(sources));
   case Screen::Display:
      if (vr)
         sources.push_back(source(kVR, std::make_unique<VRSettingsPage>()));
      sources.push_back(source(kGraphics, std::make_unique<GraphicSettingsPage>()));
      return CreateComposed(screen, "Display & Performance"s, ""s, std::move(sources));
   case Screen::Sound:
      sources.push_back(source(kAudio, std::make_unique<AudioSettingsPage>()));
      if (vr)
         sources.push_back(source(kVR, std::make_unique<VRSettingsPage>()));
      return CreateComposed(screen, "Sound"s, ""s, std::move(sources));
   case Screen::Controls:
      sources.push_back(source(kInput, std::make_unique<InputSettingsPage>()));
      sources.push_back(source(kMoreControls, std::make_unique<MoreControlsPage>()));
      return CreateComposed(screen, "Controls"s, ""s, std::move(sources));
   case Screen::Plugins: return std::make_unique<PluginHomePage>();
   case Screen::System: return std::make_unique<SystemPage>();
   case Screen::WiFi:
      #ifdef VPX_TABLE_WEBSERVER
      return std::make_unique<WiFiPage>();
      #else
      break;
      #endif
   case Screen::Quit: return std::make_unique<QuitPage>();
   case Screen::Count: break;
   }
   return nullptr;
}

Screen GetDefaultScreen()
{
   return IsScreenAvailable(Screen::ThisTable) ? Screen::ThisTable : Screen::Tables;
}

}
