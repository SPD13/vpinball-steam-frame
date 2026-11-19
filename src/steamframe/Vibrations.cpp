// license:GPLv3+

#include "core/stdafx.h"
#include "Vibrations.h"

#include "core/VPApp.h"
#include "core/player.h"
#include "steamframe/PinOneDevices.h"

#include <algorithm>
#include <atomic>
#include <optional>
#include <unordered_set>

namespace VPX::Vibrations
{

static std::optional<uint16_t> s_startDevice;
static std::unordered_set<uint16_t> s_dofDevices;

// Whether the DOF configuration has an entry for the table being played, as the DOF plugin tells (read on the input thread)
static std::atomic<bool> s_dofTableHasConfig = false;
static const MsgPluginAPI* s_msgApi = nullptr;
static uint32_t s_endpointId = 0;
static unsigned int s_getDofTableConfigId = 0;
static unsigned int s_onDofTableConfigChangedId = 0;

void Reset()
{
   s_startDevice.reset();
   s_dofDevices.clear();
}

// Whether the DOF plugin is on (Plugins > DOF > DOF Enable), read at each vibration so turning it off gives the PinOne its vibrations back at once
static bool DofEnabled()
{
   static const auto dofEnable = Settings::GetRegistry().GetPropertyId("Plugin.DOF"s, "Enable"s);
   return dofEnable.has_value() && g_settingsService.GetAppSettings().GetBool(dofEnable.value());
}

void OnJoystickAdded(uint16_t deviceId, const std::string& name)
{
   const VPX::PinOneDevices::Identity& pinOne = VPX::PinOneDevices::Get(deviceId);
   if (!pinOne.dofChannel)
   {
      s_dofDevices.erase(deviceId); // A device id comes back with its settings id, maybe as another controller
      return;
   }
   s_dofDevices.insert(deviceId);
   PLOGI << "Vibrations: '" << name << "' is a PinOne with a DOF channel" << (pinOne.usb ? " (USB)" : " (Bluetooth Xbox mode)")
         << (DofEnabled() ? ", it gets the vibrations only on the tables DOF has no entry for" : ", DOF is off: it gets the vibrations");
}

void OnActionPressed(const std::string& actionSettingId, uint16_t deviceId, const std::string& deviceSettingId)
{
   if (actionSettingId != "Start" || s_startDevice == deviceId)
      return;
   s_startDevice = deviceId;
   PLOGI << "Vibrations: the game was started from '" << deviceSettingId << '\''
         << (g_settingsService.GetAppSettings().GetPlayer_RumbleStartController() ? ", only this controller vibrates" : " (all controllers vibrate, option off)");
}

bool ShouldVibrate(uint16_t deviceId)
{
   if (s_dofDevices.contains(deviceId) && DofEnabled() && s_dofTableHasConfig)
      return false;
   if (!s_startDevice.has_value() || !g_settingsService.GetAppSettings().GetPlayer_RumbleStartController())
      return true;
   return *s_startDevice == deviceId;
}

static void RefreshDofTableConfig(const bool log)
{
   bool hasConfig = false; // Stays false without the DOF plugin
   s_msgApi->BroadcastMsg(s_endpointId, s_getDofTableConfigId, &hasConfig);
   if (s_dofTableHasConfig.exchange(hasConfig) != hasConfig || log)
      PLOGI << (hasConfig ? "Vibrations: the DOF configuration has an entry for this table, a PinOne driven by DOF follows the table's DOF"
                          : "Vibrations: no DOF entry for this table, a PinOne driven by DOF gets the vibrations");
}

static void OnDofTableConfigChanged(const unsigned int, void*, void*) { RefreshDofTableConfig(false); }

void OnGameStarted(Player* player)
{
   // A player that closed without OnGameEnding leaves its subscription to its own, gone, message API
   s_msgApi = &player->m_pluginManager.GetMsgAPI();
   s_endpointId = player->m_pluginAPI.GetVPXEndPointId();
   s_getDofTableConfigId = s_msgApi->GetMsgID("DOF", "GetTableConfig");
   s_onDofTableConfigChangedId = s_msgApi->GetMsgID("DOF", "OnTableConfigChanged");
   s_msgApi->SubscribeMsg(s_endpointId, s_onDofTableConfigChangedId, OnDofTableConfigChanged, nullptr);
   // The DOF plugin may have set the table up before the game start (when the script made its controller)
   RefreshDofTableConfig(true);
}

void OnGameEnding()
{
   s_dofTableHasConfig = false;
   if (s_msgApi == nullptr)
      return;
   s_msgApi->UnsubscribeMsg(s_onDofTableConfigChangedId, OnDofTableConfigChanged, nullptr);
   s_msgApi->ReleaseMsgID(s_getDofTableConfigId);
   s_msgApi->ReleaseMsgID(s_onDofTableConfigChangedId);
   s_msgApi = nullptr;
}

}
