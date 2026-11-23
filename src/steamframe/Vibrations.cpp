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

// Whether the DOF plugin was told to pause its outputs ("DOF"/"SetOutputsPaused"): the game was started from another controller than the
// PinOne driven by DOF
static bool s_dofPaused = false;
static unsigned int s_setDofOutputsPausedId = 0;

static void UpdateDofPause();

void Reset()
{
   s_startDevice.reset();
   s_dofDevices.clear();
   s_dofPaused = false; // Nothing to tell: the DOF plugin unpauses at each game end, and the last player may have gone without OnGameEnding
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
      UpdateDofPause();
      return;
   }
   s_dofDevices.insert(deviceId);
   UpdateDofPause();
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
   UpdateDofPause();
}

bool ShouldVibrate(uint16_t deviceId)
{
   if (s_dofDevices.contains(deviceId) && DofEnabled() && s_dofTableHasConfig)
      return false;
   if (!s_startDevice.has_value() || !g_settingsService.GetAppSettings().GetPlayer_RumbleStartController())
      return true;
   return *s_startDevice == deviceId;
}

// The DOF outputs pause while the game is played from the VR controllers or another gamepad than the PinOne driven by DOF: the vibrations
// go to that controller, the PinOne's DOF (its motors, in place of its vibrations) would follow a game played without it. A game started
// from the keyboard keeps them (a cabinet's key encoder, a PinOne sending keys). libdof drives all its devices together: the pause holds the
// other DOF devices too, only once a PinOne with a DOF channel is connected.
static bool DofShouldPause()
{
   if (!s_startDevice.has_value() || s_dofDevices.empty() || s_dofDevices.contains(*s_startDevice) || g_pplayer == nullptr
      || !g_settingsService.GetAppSettings().GetPlayer_RumbleStartController())
      return false;
   const InputManager::DeviceType type = g_pplayer->m_pininput.GetDeviceType(*s_startDevice);
   return type == InputManager::DeviceType::VRController || type == InputManager::DeviceType::Joystick;
}

static void UpdateDofPause()
{
   if (s_msgApi == nullptr) // Before the game started, applied by OnGameStarted
      return;
   bool paused = DofShouldPause();
   if (paused == s_dofPaused)
      return;
   s_dofPaused = paused;
   PLOGI << (paused ? "Vibrations: the game is played without the PinOne driven by DOF, its DOF outputs pause"
                    : "Vibrations: the DOF outputs resume");
   s_msgApi->BroadcastMsg(s_endpointId, s_setDofOutputsPausedId, &paused);
}

void OnStartControllerOptionChanged() { UpdateDofPause(); }

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
   s_setDofOutputsPausedId = s_msgApi->GetMsgID("DOF", "SetOutputsPaused");
   s_dofPaused = false; // The DOF plugin starts each game unpaused
   s_msgApi->SubscribeMsg(s_endpointId, s_onDofTableConfigChangedId, OnDofTableConfigChanged, nullptr);
   // The DOF plugin may have set the table up before the game start (when the script made its controller)
   RefreshDofTableConfig(true);
   UpdateDofPause(); // Start may have been pressed while the table loaded
}

void OnGameEnding()
{
   s_dofTableHasConfig = false;
   if (s_msgApi == nullptr)
      return;
   s_msgApi->UnsubscribeMsg(s_onDofTableConfigChangedId, OnDofTableConfigChanged, nullptr);
   s_msgApi->ReleaseMsgID(s_getDofTableConfigId);
   s_msgApi->ReleaseMsgID(s_onDofTableConfigChangedId);
   s_msgApi->ReleaseMsgID(s_setDofOutputsPausedId);
   s_dofPaused = false; // The DOF plugin unpauses at the game end
   s_msgApi = nullptr;
}

}
