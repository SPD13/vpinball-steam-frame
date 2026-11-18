// license:GPLv3+

#include "core/stdafx.h"
#include "Vibrations.h"

#include "core/VPApp.h"
#include "input/HIDEnumeration.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_set>

namespace VPX::Vibrations
{

static std::optional<uint16_t> s_startDevice;
static std::unordered_set<uint16_t> s_dofDevices;

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

void OnJoystickAdded(uint16_t deviceId, SDL_Joystick* joystick)
{
   const auto dofEnable = Settings::GetRegistry().GetPropertyId("Plugin.DOF"s, "Enable"s);
   if (!dofEnable.has_value() || !g_settingsService.GetAppSettings().GetBool(dofEnable.value()))
      return;
   const uint16_t vendor = SDL_GetJoystickVendor(joystick);
   const uint16_t product = SDL_GetJoystickProduct(joystick);
   const bool wiredPinOne = vendor == 0x39F5 && product == 0x9208 && SDL_GetJoystickConnectionState(joystick) == SDL_JOYSTICK_CONNECTION_WIRED;
   const bool xboxPinOne = vendor == 0x045E && product == 0x0B13 && HasPinOneDofCollection(joystick);
   if (!wiredPinOne && !xboxPinOne)
      return;
   s_dofDevices.insert(deviceId);
   const char* const name = SDL_GetJoystickName(joystick);
   PLOGI << "Vibrations: '" << (name ? name : "") << "' is a PinOne driven by DOF" << (wiredPinOne ? " (USB)" : " (Bluetooth Xbox mode)")
         << (g_settingsService.GetAppSettings().GetPlayer_RumbleDofController() ? ", it also gets the vibrations" : ", it gets no vibrations");
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
   if (s_dofDevices.contains(deviceId) && !g_settingsService.GetAppSettings().GetPlayer_RumbleDofController())
      return false;
   if (!s_startDevice.has_value() || !g_settingsService.GetAppSettings().GetPlayer_RumbleStartController())
      return true;
   return *s_startDevice == deviceId;
}

}
