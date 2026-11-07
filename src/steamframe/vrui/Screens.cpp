// license:GPLv3+

#include "core/stdafx.h"
#include "Screens.h"

#include "ComposedPage.h"
#include "MainMenuPage.h"
#include "Theme.h"
#include "VRUI.h"
#include "Widgets.h"

#include "core/TournamentFile.h"
#include "fonts/IconsForkAwesome.h"
#include "renderer/Renderer.h"
#include "steamframe/DesktopDynamicResolution.h"
#include "steamframe/NaN.h"
#include "steamframe/Launcher.h"
#include "steamframe/MeshSimplifier.h"
#include "steamframe/LightMapPruning.h"
#include "steamframe/Reflections.h"
#include "steamframe/SceneBufferFormat.h"
#include "steamframe/PerfPanel.h"
#include "steamframe/ScoreStore.h"
#include "steamframe/ScoresPage.h"
#include "steamframe/TablePickerPage.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/AudioSettingsPage.h"
#include "ui/live/ingameui/DisplaySettingsPage.h"
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
      if (m_player->m_vrDevice) // Outside VR, the Screens screen of the rail shows them
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
   void BuildPage() override
   {
      // During a table started from the lobby: close it and get back to the lobby, as the Exit Game action does
      if (g_launcher->m_launcherMode && !m_player->m_isLobby)
         AddItem(std::make_unique<InGameUIItem>("Return to Lobby"s, "Close this table and choose another one"s,
            [this]()
            {
               g_launcher->m_nextTableFilename.clear();
               m_player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
            }));
      AddQuitItem(*this);
   }
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

// Performance profile (top of the Device Performance screen): Low, Medium and High set the performance settings of the screen at once, any
// other combination of them is Custom. Medium is the defaults, in every build (Settings_properties.inl). The values are set as the items of
// the screen would set them: live, and saved with its Save button (for the table or for all of them), undone with Undo.
enum class PerfProfile { Low, Medium, High, Custom };

struct ProfileSetting
{
   const VPX::Properties::PropertyRegistry::PropId* id;
   int values[3]; // Low, Medium, High (a switch: 0 or 1; a choice: the index of its value)
};

static const vector<ProfileSetting>& GetProfileSettings()
{
   static const vector<ProfileSetting> settings = {
      { &Settings::m_propPlayerVR_DynamicResolution, { 1, 1, 0 } },
      { &Settings::m_propPlayerVR_Foveation, { 3, 2, 0 } }, // High, Medium, Off
      { &Settings::m_propPlayerVR_FoveateTonemapping, { 1, 1, 0 } },
      { &Settings::m_propPlayerVR_FoveateAntialiasing, { 1, 0, 0 } }, // Always On, Always Off, Always Off
      { &Settings::m_propPlayer_SimplifyMeshes, { 1, 1, 0 } },
      { &Settings::m_propPlayer_LightMapPruning, { 2, 2, 0 } }, // Medium, Medium, Off
      { &Settings::m_propPlayer_CompactSceneBuffer, { 1, 1, 0 } },
      { &Settings::m_propPlayer_ExtraMirrors, { 1, 1, 0 } }, // Off, Off, On
      { &Settings::m_propPlayer_RoomReflection, { 1, 1, 0 } }, // Off, Off, On
      { &Settings::m_propPlayer_ReflectionSize, { 1, 1, 0 } }, // Half, Half, Full
   };
   return settings;
}

static const ProfileSetting* FindProfileSetting(const InGameUIItem& item)
{
   if (item.m_type != InGameUIItem::Type::Property || item.m_property == nullptr)
      return nullptr;
   const auto id = Settings::GetRegistry().GetPropertyId(item.m_property->m_groupId, item.m_property->m_propId);
   if (!id.has_value())
      return nullptr;
   for (const ProfileSetting& setting : GetProfileSettings())
      if (setting.id->index == id.value().index)
         return &setting;
   return nullptr;
}

class PerfProfilePage final : public InGameUIPage
{
public:
   PerfProfilePage()
      : InGameUIPage("Performance profile"s, ""s, SaveMode::None)
   {
   }

   void SetScreen(const ComposedPage* screen) { m_screen = screen; }

   // The row is edited with the buttons: UI: Confirm, then Next/Previous change the profile (Custom is not one to choose), Confirm or Cancel
   // leave it
   bool IsEditableItem(const InGameUIItem& item) const override { return item.m_label == kRow || InGameUIPage::IsEditableItem(item); }

   void AdjustItem(float direction, bool isInitialPress) override
   {
      if (const InGameUIItem* const item = GetSelectedItem(); item && item->m_label == kRow)
      {
         if (isInitialPress)
         {
            const PerfProfile current = GetProfile();
            const int index = current == PerfProfile::Custom ? (direction > 0.f ? -1 : 3) : static_cast<int>(current);
            Apply(static_cast<PerfProfile>((index + (direction > 0.f ? 1 : 2)) % 3));
         }
         return;
      }
      InGameUIPage::AdjustItem(direction, isInitialPress);
   }

private:
   static constexpr const char* kRow = "##profile";

   // The profile the shown settings match (the ones of VR only in VR). Outside VR, Low and Medium set the same settings: the one chosen last
   // is shown, Medium otherwise (the default).
   PerfProfile GetProfile() const
   {
      bool matches[3] = { true, true, true };
      if (m_screen)
         m_screen->ForEachShownItem(
            [&matches](InGameUIPage&, InGameUIItem& item)
            {
               if (const ProfileSetting* setting = FindProfileSetting(item); setting)
               {
                  const int value = item.m_property->m_type == VPX::Properties::PropertyDef::Type::Bool ? (item.GetBoolValue() ? 1 : 0) : item.GetIntValue();
                  for (int p = 0; p < 3; p++)
                     matches[p] &= value == setting->values[p];
               }
            });
      if (m_chosen != PerfProfile::Custom && matches[static_cast<int>(m_chosen)])
         return m_chosen;
      return matches[1] ? PerfProfile::Medium : matches[0] ? PerfProfile::Low : matches[2] ? PerfProfile::High : PerfProfile::Custom;
   }

   void Apply(PerfProfile profile) const
   {
      if (m_screen == nullptr || profile == PerfProfile::Custom)
         return;
      m_chosen = profile;
      const int p = static_cast<int>(profile);
      m_screen->ForEachShownItem(
         [p](InGameUIPage&, InGameUIItem& item)
         {
            if (const ProfileSetting* setting = FindProfileSetting(item); setting)
            {
               if (item.m_property->m_type == VPX::Properties::PropertyDef::Type::Bool)
                  item.SetValue(setting->values[p] != 0);
               else
                  item.SetValue(setting->values[p]);
            }
         });
   }

   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>(kRow, "Confirm, then Next/Previous: choose the profile, Confirm or Cancel when done. Changing one of the settings below makes it Custom"s,
         [this](int, const InGameUIItem*)
         {
            const float height = ImGui::GetFrameHeight();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f), Theme::kText, "Performance profile");
            const PerfProfile current = GetProfile();
            static constexpr const char* names[] = { "Low", "Medium", "High", "Custom" };
            const float gap = Theme::Px(0.4f);
            float x = pos.x + width * 0.42f;
            for (int p = 0; p < 4; p++)
            {
               ImGui::SetCursorScreenPos(ImVec2(x, pos.y));
               // Custom is only shown, highlighted while the settings match no profile
               if (Widgets::Pill(names[p], nullptr, names[p], height, true, false, p == static_cast<int>(current)) && p != static_cast<int>(PerfProfile::Custom))
                  Apply(static_cast<PerfProfile>(p));
               x += Widgets::PillWidth(nullptr, names[p], height) + gap;
            }
            ImGui::SetCursorScreenPos(pos);
            ImGui::Dummy(ImVec2(width, height));
         }));
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info,
         "Low is for low performance devices: it sets the settings that maximize the performance, with visual tradeoffs. Medium is the balanced "
         "option, where the visual tradeoff is minimal. High has no performance optimization and offers the best visual quality."s));
   }

   const ComposedPage* m_screen = nullptr;
   mutable PerfProfile m_chosen = PerfProfile::Custom; // The profile chosen last, shown when the settings match it and another one
};

// Performance screen: Device Performance (what the Steam Frame Edition adds) or VPX Performance (the graphics of Visual Pinball), each shown
// in place of this screen (Cancel gets back here)
class PerformanceChoicePage final : public InGameUIPage
{
public:
   PerformanceChoicePage()
      : InGameUIPage("Performance"s, ""s, SaveMode::None)
   {
   }

private:
   void BuildPage() override
   {
      const auto open = [](Screen screen)
      {
         if (auto* const menu = dynamic_cast<MainMenuPage*>(g_pplayer->m_liveUI->m_inGameUI.GetActivePage()); menu)
         {
            menu->SetScreen(screen);
            menu->SetRailFocus(false);
         }
      };
      AddItem(std::make_unique<InGameUIItem>("Device Performance"s,
         "What the Steam Frame Edition adds to keep heavy tables smooth: dynamic resolution, foveated rendering in VR, simplified meshes, compact scene buffer, reflections"s,
         [open]() { open(Screen::SteamFrame); }));
      AddItem(std::make_unique<InGameUIItem>("VPX Performance"s, "The graphics options of Visual Pinball (and, in VR, the performance panel and the VR rendering)"s,
         [open]() { open(Screen::Display); }));
   }
};

// The performance panel (VPX Performance screen): left of the cabinet in VR, over the backglass window or the playfield outside VR. A tool to tune
// the tables, on or off for all of them, saved at once
class PerformancePanelPage final : public InGameUIPage
{
public:
   PerformancePanelPage()
      : InGameUIPage("Performance panel"s, ""s, SaveMode::None)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propStandalone_PerfPanel, //
         []() { return VPX::PerfPanel::Get().IsEnabled(); }, //
         [](bool v)
         {
            VPX::PerfPanel::Get().SetEnabled(v);
            Settings& settings = g_settingsService.GetAppSettings();
            settings.SetStandalone_PerfPanel(v, false);
            settings.Save();
         }));
   }
};

// The simplified meshes (Device Performance screen), switched and tuned live, saved for the table or for all of them like the settings pages
class SimplifiedMeshesPage final : public InGameUIPage
{
public:
   SimplifiedMeshesPage()
      : InGameUIPage("Simplified meshes"s, ""s, SaveMode::Both)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_SimplifyMeshes, //
         []() { return MeshSimplifier::Get().IsEnabled(); }, //
         [](bool v) { MeshSimplifier::Get().SetEnabled(v); }));
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_SimplifyMeshesError, 1.f, "%.2f px"s, //
         []() { return MeshSimplifier::Get().GetAllowedPixels(); }, //
         [](float, float v) { MeshSimplifier::Get().SetAllowedPixels(v); }));
      AddItem(std::make_unique<InGameUIItem>("Simplified meshes status"s, "Parts simplified when the table loaded, and the triangles left out over the last second"s,
         [](int, const InGameUIItem*)
         {
            const MeshSimplifier& simplifier = MeshSimplifier::Get();
            const int parts = simplifier.GetSimplifiedPartCount();
            const float saved = simplifier.GetSavedShare();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(parts == 0 ? "No part simplified (the setting was off when the table loaded, or the table is light)"
                  : !simplifier.IsEnabled()  ? std::format("{} parts simplified, drawn in full (off)", parts).c_str()
                  : VPX::IsNaN(saved)        ? std::format("{} parts simplified", parts).c_str()
                                             : std::format("{} parts simplified, {:.0f} % of the triangles left out", parts, saved * 100.f).c_str());
         }));
   }
};

// The light maps without their dimmest triangles (Device Performance screen), switched live, saved for the table or for all of them like the settings pages
class LightMapsPage final : public InGameUIPage
{
public:
   LightMapsPage()
      : InGameUIPage("Light maps"s, ""s, SaveMode::Both)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_LightMapPruning, //
         []() { return LightMapPruning::Get().GetLevel(); }, //
         [](int, int v) { LightMapPruning::Get().SetLevel(v); }));
      AddItem(std::make_unique<InGameUIItem>("Light map pruning status"s, "Light maps of this table, and the share of their triangles left out"s,
         [](int, const InGameUIItem*)
         {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(LightMapPruning::Get().GetStatus().c_str());
         }));
   }
};

// Dynamic resolution outside VR (Device Performance screen; in VR, the items of the VR settings): the settings of VR, applied live, saved for the
// table or for all of them like the settings pages
class DesktopDynamicResolutionPage final : public InGameUIPage
{
public:
   DesktopDynamicResolutionPage()
      : InGameUIPage("Dynamic resolution"s, ""s, SaveMode::Both)
   {
   }

private:
   void BuildPage() override
   {
      DesktopDynamicResolution& dynamicResolution = DesktopDynamicResolution::Get();
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayerVR_DynamicResolution, //
         [&dynamicResolution]() { return dynamicResolution.IsEnabled(); }, //
         [&dynamicResolution](bool v) { dynamicResolution.SetEnabled(v); }));
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayerVR_DynamicResolutionTarget, 100.f, "%4.0f %%"s, //
         [&dynamicResolution]() { return dynamicResolution.GetTarget(); }, //
         [&dynamicResolution](float, float v) { dynamicResolution.SetTarget(v); }));
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayerVR_DynamicResolutionMinScale, 100.f, "%4.0f %%"s, //
         [&dynamicResolution]() { return dynamicResolution.GetMinScale(); }, //
         [&dynamicResolution](float, float v) { dynamicResolution.SetMinScale(v); }));
      AddItem(std::make_unique<InGameUIItem>("Desktop dynamic resolution status"s, "Current rendering size, and the GPU time per frame against the frame period"s,
         [&dynamicResolution](int, const InGameUIItem*)
         {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(("Dynamic resolution: " + dynamicResolution.GetStatus()).c_str());
         }));
   }
};

// The format of the scene buffers (Device Performance screen), switched live, saved for the table or for all of them like the settings pages
class SceneBufferPage final : public InGameUIPage
{
public:
   SceneBufferPage()
      : InGameUIPage("Scene buffer"s, ""s, SaveMode::Both)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_CompactSceneBuffer, //
         []() { return SceneBufferFormat::IsCompactRequested(); }, //
         [](bool v) { SceneBufferFormat::SetCompactRequested(v); }));
      AddItem(std::make_unique<InGameUIItem>("Scene buffer status"s, "Format the scene is rendered in"s,
         [](int, const InGameUIItem*)
         {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(("Scene buffer: " + SceneBufferFormat::GetStatus()).c_str());
         }));
   }
};

// What the reflections leave out (Device Performance screen), switched live, saved for the table or for all of them like the settings pages
class ReflectionsPage final : public InGameUIPage
{
public:
   ReflectionsPage()
      : InGameUIPage("Reflections"s, ""s, SaveMode::Both)
   {
   }

private:
   void BuildPage() override
   {
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_ExtraMirrors, //
         []() { return Reflections::GetExtraMirrorsMode(); }, //
         [](int, int v) { Reflections::SetExtraMirrorsMode(v); }));
      AddItem(std::make_unique<InGameUIItem>("Extra mirrors status"s, "The mirrors this table shows, other than the playfield's"s,
         [](int, const InGameUIItem*)
         {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Reflections::GetExtraMirrorsStatus().c_str());
         }));
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_RoomReflection, //
         []() { return Reflections::GetRoomReflectionMode(); }, //
         [](int, int v) { Reflections::SetRoomReflectionMode(v); }));
      AddItem(std::make_unique<InGameUIItem>("Room reflection status"s, "The parts of the VR room in the reflections of this table"s,
         [](int, const InGameUIItem*)
         {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Reflections::GetRoomReflectionStatus().c_str());
         }));
      AddItem(std::make_unique<InGameUIItem>( //
         Settings::m_propPlayer_ReflectionSize, //
         []() { return Reflections::GetReflectionSize(); }, //
         [](int, int v) { Reflections::SetReflectionSize(v); }));
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
constexpr const char* kPerfPanel = "vrui/perf-panel";
constexpr const char* kSimplifiedMeshes = "vrui/simplified-meshes";
constexpr const char* kSceneBuffer = "vrui/scene-buffer";
constexpr const char* kLightMaps = "vrui/light-maps";
constexpr const char* kDesktopDynamicResolution = "vrui/desktop-dynamic-resolution";
constexpr const char* kReflections = "vrui/reflections";
constexpr const char* kPerfProfile = "vrui/perf-profile";

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
         Section { "Menu and lobby"s, kMenuAndLobby, {}, true },
         // Upstream 64a480a96 (2026-11-01): the headset's place is the same for all tables, the table offsets are this table's
         Section { "Position"s, kVR,
            { Prop(Settings::m_propPlayerVR_HeadSetOrientation), Prop(Settings::m_propPlayerVR_HeadSetX), Prop(Settings::m_propPlayerVR_HeadSetY),
               Prop(Settings::m_propPlayerVR_TableOffsetY), Prop(Settings::m_propPlayerVR_TableOffsetZ) } },
         Section { "Cabinet"s, kVR,
            { Prop(Settings::m_propPlayerVR_ScaleToLockbar), Prop(Settings::m_propPlayer_LockbarWidth), Prop(Settings::m_propPlayerVR_MatchLockbarHeight),
               Prop(Settings::m_propPlayer_LockbarHeight), Prop(Settings::m_propPlayer_ScreenPlayerX), Prop(Settings::m_propPlayer_ScreenPlayerY),
               Prop(Settings::m_propPlayerVR_ControllerCabYOffset), Prop(Settings::m_propPlayerVR_ControllerLockbarScale) } },
         Section { "Room"s, kVR,
            { Prop(Settings::m_propPlayerVR_UsePassthroughColor), Prop(Settings::m_propPlayerVR_LockFeetToGround), Prop(Settings::m_propPlayerVR_AddBackglass),
               Prop(Settings::m_propPlayerVR_ShowControllers) } },
         Section { "More"s, kVR, {}, true },
      };

   case Screen::Display:
      return {
         Section { ""s, kPerfPanel, {}, true },
         Section { "VR rendering"s, kVR, { Prop(Settings::m_propPlayerVR_DisplayRefreshRate), Label("Render resolution status"s) }, false, false },
         Section { "Desktop mirror"s, kVR, { Prop(Settings::m_propPlayerVR_DesktopDisplay), Prop(Settings::m_propPlayerVR_ShrinkDesktopDisplay) }, false, false },
         Section { ""s, kGraphics, {}, true },
      };

   case Screen::SteamFrame:
      return {
         Section { ""s, kPerfProfile, {}, true },
         Section { "Dynamic resolution"s, kVR,
            { Prop(Settings::m_propPlayerVR_DynamicResolution), Prop(Settings::m_propPlayerVR_DynamicResolutionTarget), Prop(Settings::m_propPlayerVR_DynamicResolutionSensitivity),
               Label("Dynamic resolution sensitivity help"s),
               Prop(Settings::m_propPlayerVR_DynamicResolutionMinScale),
               Label("Dynamic resolution status"s) },
            false, false },
         Section { "Dynamic resolution"s, kDesktopDynamicResolution, {}, true }, // Outside VR
         Section { "Foveated rendering"s, kVR, { Prop(Settings::m_propPlayerVR_Foveation), Prop(Settings::m_propPlayerVR_FoveationEyeTracked), Prop(Settings::m_propPlayerVR_FoveateTonemapping),
               Prop(Settings::m_propPlayerVR_FoveateAntialiasing), Label("Foveation status"s),
               Label("Foveated anti-aliasing status"s) }, false, false },
         Section { "Simplified meshes"s, kSimplifiedMeshes, {}, true },
         Section { "Light maps"s, kLightMaps, {}, true },
         Section { "Scene buffer"s, kSceneBuffer, {}, true },
         Section { "Reflections"s, kReflections, {}, true },
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
   case Screen::Screens: return g_pplayer && g_pplayer->m_vrDevice == nullptr;
   case Screen::WiFi:
      #ifdef VPX_TABLE_WEBSERVER
      return true;
      #else
      return false;
      #endif
   case Screen::Tables:
   case Screen::Scores:
   case Screen::Room:
   case Screen::Performance:
   case Screen::Display:
   case Screen::SteamFrame:
   case Screen::Sound:
   case Screen::Controls:
   case Screen::Plugins:
   case Screen::System:
   case Screen::Quit: return true;
   case Screen::Count: break;
   }
   return false;
}

bool IsScreenOnRail(Screen screen) { return GetRailScreen(screen) == screen && IsScreenAvailable(screen); }

Screen GetRailScreen(Screen screen)
{
   switch (screen)
   {
   case Screen::Display:
   case Screen::SteamFrame: return Screen::Performance;
   default: return screen;
   }
}

const char* GetScreenIcon(Screen screen)
{
   switch (screen)
   {
   case Screen::Tables: return ICON_FK_TH_LARGE;
   case Screen::ThisTable: return ICON_FK_PLAY;
   case Screen::Scores: return ICON_FK_TROPHY;
   case Screen::Room: return ICON_FK_CUBE;
   case Screen::Screens: return ICON_FK_DESKTOP;
   case Screen::Performance: return ICON_FK_TACHOMETER;
   case Screen::Display: return ICON_FK_TACHOMETER;
   case Screen::SteamFrame: return kSteamFrameLogo;
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
   case Screen::Screens: return "Screens";
   case Screen::Performance: return "Performance";
   case Screen::Display: return "VPX Performance";
   case Screen::SteamFrame: return "Device Performance";
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
   case Screen::Screens: return std::make_unique<DisplayHomePage>(); // Upstream's display settings: one page per window
   case Screen::Performance: return std::make_unique<PerformanceChoicePage>();
   case Screen::Display:
      sources.push_back(source(kPerfPanel, std::make_unique<PerformancePanelPage>())); // In VR left of the cabinet, else over the backglass or the playfield
      if (vr)
         sources.push_back(source(kVR, std::make_unique<VRSettingsPage>()));
      sources.push_back(source(kGraphics, std::make_unique<GraphicSettingsPage>()));
      return CreateComposed(screen, "VPX Performance"s, ""s, std::move(sources));
   case Screen::SteamFrame:
   {
      auto profile = std::make_unique<PerfProfilePage>();
      PerfProfilePage* const profilePage = profile.get();
      sources.push_back(source(kPerfProfile, std::move(profile)));
      if (vr)
         sources.push_back(source(kVR, std::make_unique<VRSettingsPage>()));
      else
         sources.push_back(source(kDesktopDynamicResolution, std::make_unique<DesktopDynamicResolutionPage>()));
      sources.push_back(source(kSimplifiedMeshes, std::make_unique<SimplifiedMeshesPage>()));
      sources.push_back(source(kLightMaps, std::make_unique<LightMapsPage>()));
      sources.push_back(source(kSceneBuffer, std::make_unique<SceneBufferPage>()));
      sources.push_back(source(kReflections, std::make_unique<ReflectionsPage>()));
      std::unique_ptr<InGameUIPage> page = CreateComposed(screen, "Device Performance"s, "What the Steam Frame Edition adds to keep heavy tables smooth"s, std::move(sources));
      profilePage->SetScreen(static_cast<const ComposedPage*>(page.get())); // The settings the profile sets are the items of the screen
      return page;
   }
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
