// license:GPLv3+

#include "core/stdafx.h"
#include "Vibrations.h"

#include "core/VPApp.h"
#include "core/player.h"
#include "input/HIDEnumeration.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
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

// A Bluetooth address as SDL and hidapi give it, without separators or case ("3c:dc:75:ed:b5:21" == "3CDC75EDB521")
static std::string NormalizedSerial(const std::string& serial)
{
   std::string out;
   for (char c : serial)
      if (std::isalnum(static_cast<unsigned char>(c)))
         out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   return out;
}

// The PinOne in its Bluetooth Xbox mode: the HID device of this joystick has the 0xFF00 vendor collection (DOF). Matched by serial;
// without serials, when there is a single such device.
static bool HasPinOneDofCollection(SDL_Joystick* joystick)
{
   const char* const joySerial = SDL_GetJoystickSerial(joystick);
   const std::string serial = NormalizedSerial(joySerial ? joySerial : "");
   int count = 0;
   bool matched = false;
   if (SDL_hid_device_info* const devices = EnumerateAllHIDDevices(0x045E, 0x0B13); devices)
   {
      for (const SDL_hid_device_info* dev = devices; dev; dev = dev->next)
      {
         if (dev->usage_page != 0xFF00 || dev->usage != 0x01)
            continue;
         count++;
         std::string devSerial;
         for (const wchar_t* c = dev->serial_number; c && *c; c++)
            devSerial += static_cast<char>(*c);
         matched |= !serial.empty() && NormalizedSerial(devSerial) == serial;
      }
      SDL_hid_free_enumeration(devices);
   }
   return matched || (serial.empty() && count == 1);
}

// Whether the DOF plugin is on (Plugins > DOF > DOF Enable), read at each vibration so turning it off gives the PinOne its vibrations back at once
static bool DofEnabled()
{
   static const auto dofEnable = Settings::GetRegistry().GetPropertyId("Plugin.DOF"s, "Enable"s);
   return dofEnable.has_value() && g_settingsService.GetAppSettings().GetBool(dofEnable.value());
}

void OnJoystickAdded(uint16_t deviceId, SDL_Joystick* joystick)
{
   const uint16_t vendor = SDL_GetJoystickVendor(joystick);
   const uint16_t product = SDL_GetJoystickProduct(joystick);
   const bool wiredPinOne = vendor == 0x39F5 && product == 0x9208 && SDL_GetJoystickConnectionState(joystick) == SDL_JOYSTICK_CONNECTION_WIRED;
   const bool xboxPinOne = vendor == 0x045E && product == 0x0B13 && HasPinOneDofCollection(joystick);
   if (!wiredPinOne && !xboxPinOne)
      return;
   s_dofDevices.insert(deviceId);
   const char* const name = SDL_GetJoystickName(joystick);
   PLOGI << "Vibrations: '" << (name ? name : "") << "' is a PinOne with a DOF channel" << (wiredPinOne ? " (USB)" : " (Bluetooth Xbox mode)")
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

bool IsDofController(uint16_t deviceId) { return s_dofDevices.contains(deviceId); }

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
