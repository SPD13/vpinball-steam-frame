// license:GPLv3+

#include "core/stdafx.h"
#include "DofSetup.h"

#include "core/VPApp.h"
#include "core/player.h"
#include "steamframe/ControllerLayouts.h"
#include "steamframe/Vibrations.h"
#include "ui/live/LiveUI.h"

#include <filesystem>

namespace VPX::DofSetup
{

static constexpr const char* DOF_PLUGIN_ID = "DOF";

static std::optional<VPX::Properties::PropertyRegistry::PropId> EnablePropId()
{
   return Settings::GetRegistry().GetPropertyId("Plugin."s + DOF_PLUGIN_ID, "Enable"s);
}

bool IsDofEnabled(Player* player)
{
   const auto plugin = player->m_pluginManager.GetPlugin(DOF_PLUGIN_ID);
   return plugin && plugin->IsLoaded();
}

bool HasDofChannel(uint16_t deviceId) { return VPX::Vibrations::IsDofController(deviceId); }

void SetDofEnabled(Player* player, bool enabled)
{
   MsgPI::MsgPluginManager& manager = player->m_pluginManager;
   const auto plugin = manager.GetPlugin(DOF_PLUGIN_ID);
   if (plugin == nullptr)
   {
      PLOGE << "DOF setup: the DOF plugin is not installed";
      return;
   }
   // As PluginSheetPage::AddPluginEnableItem
   if (enabled && !plugin->IsLoaded())
      manager.LoadPlugin(*plugin);
   else if (!enabled && plugin->IsLoaded())
   {
      if (player->m_scriptInterpreter && player->m_pluginAPI.IsScriptContributor(plugin->m_endpointId))
      {
         player->m_scriptInterpreter->Stop(player->m_ptable);
         ULONG refCount = player->m_scriptInterpreter->Release();
         player->m_scriptInterpreter = nullptr;
         assert(refCount == 0);
         player->m_liveUI->PushNotification("The plugin you have disabled contributed to the script engine.\nTherefore, the script was stopped to prevent issues.", 5000);
      }
      manager.UnloadPlugin(*plugin);
   }
   if (const auto propId = EnablePropId(); propId.has_value())
      g_settingsService.GetAppSettings().Set(propId.value(), enabled, false);
}

// Copies the shipped PinOne Mini configuration where libdof reads it
static bool InstallPinOneMiniConfig()
{
   const std::filesystem::path source = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, std::filesystem::path("dof") / "pinone-mini");
   const std::filesystem::path target = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "directoutputconfig");
   std::error_code ec;
   std::filesystem::create_directories(target, ec);
   int count = 0;
   for (const auto& entry : std::filesystem::directory_iterator(source, ec))
   {
      if (!entry.is_regular_file())
         continue;
      if (!std::filesystem::copy_file(entry.path(), target / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec))
      {
         PLOGE << "DOF setup: could not copy " << entry.path() << " to " << target << ": " << ec.message();
         return false;
      }
      count++;
   }
   if (ec || count == 0)
   {
      PLOGE << "DOF setup: no PinOne Mini configuration in " << source << (ec ? ": " + ec.message() : ""s);
      return false;
   }
   PLOGI << "DOF setup: the PinOne Mini configuration (" << count << " files) installed in " << target;
   return true;
}

void ApplyPinOneMiniSetup(Player* player, uint16_t deviceId)
{
   VPX::ControllerLayouts::ApplyPinOneLayout(player->m_pininput, deviceId);
   const bool installed = InstallPinOneMiniConfig();
   // A running DOF read the previous configuration for this table: restart it on the new one
   if (installed && IsDofEnabled(player))
      SetDofEnabled(player, false);
   SetDofEnabled(player, true);
   player->m_liveUI->PushNotification(installed ? "PinOne Mini mapping and DOF configuration set, DOF on"s
                                                : "PinOne mapping set, DOF on, but the DOF configuration could not be installed (see the log)"s,
      5000);
}

}
