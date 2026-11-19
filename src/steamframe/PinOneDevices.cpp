// license:GPLv3+

#include "core/stdafx.h"
#include "PinOneDevices.h"

#include "input/HIDEnumeration.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstring>
#include <unordered_map>

namespace VPX::PinOneDevices
{

static constexpr unsigned char IDENTITY_REPORT_ID = 7;
static constexpr size_t IDENTITY_REPORT_SIZE = 32;
static constexpr size_t IDENTITY_VERSION_OFFSET = 10;
static constexpr unsigned char IDENTITY_CAPS_DOF = 0x01;

static std::unordered_map<uint16_t, Identity> s_identities;

// A Bluetooth address as SDL and hidapi give it, without separators or case ("3c:dc:75:ed:b5:21" == "3CDC75EDB521")
static std::string NormalizedSerial(const std::string& serial)
{
   std::string out;
   for (char c : serial)
      if (std::isalnum(static_cast<unsigned char>(c)))
         out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   return out;
}

// Reads the identity report of the HID device at path: true and the firmware version if it is a PinOne's
static bool ReadIdentity(const char* path, std::string& firmware, bool& dof)
{
   SDL_hid_device* const dev = SDL_hid_open_path(path);
   if (dev == nullptr)
   {
      PLOGW << "PinOne: could not open " << path << ": " << SDL_GetError();
      return false;
   }
   unsigned char report[1 + IDENTITY_REPORT_SIZE] = { IDENTITY_REPORT_ID };
   const int read = SDL_hid_get_feature_report(dev, report, sizeof(report));
   SDL_hid_close(dev);
   // The report ID, then "PinOne" and two zeros, the format, the capabilities, the version
   const unsigned char* const data = report + 1;
   if (read < static_cast<int>(1 + IDENTITY_VERSION_OFFSET) || std::memcmp(data, "PinOne\0\0", 8) != 0 || data[8] < 1)
      return false;
   dof = (data[9] & IDENTITY_CAPS_DOF) != 0;
   const size_t end = std::min(static_cast<size_t>(read - 1), IDENTITY_REPORT_SIZE);
   firmware.clear();
   for (size_t i = IDENTITY_VERSION_OFFSET; i < end && data[i] != 0; i++)
      if (std::isprint(data[i]))
         firmware += static_cast<char>(data[i]);
   return true;
}

Identity Identify(SDL_Joystick* joystick)
{
   Identity identity;
   const uint16_t vendor = SDL_GetJoystickVendor(joystick);
   const uint16_t product = SDL_GetJoystickProduct(joystick);
   if (vendor == 0x39F5 && product == 0x9208 && SDL_GetJoystickConnectionState(joystick) == SDL_JOYSTICK_CONNECTION_WIRED)
   {
      identity.usb = true;
      identity.dofChannel = true;
      return identity;
   }
   if (vendor != 0x045E || product != 0x0B13)
      return identity;

   // The vendor collection of this pad, matched by serial only: a real Xbox pad has none, and a pad without a serial can't be told apart
   const char* const joySerial = SDL_GetJoystickSerial(joystick);
   const std::string serial = NormalizedSerial(joySerial ? joySerial : "");
   if (serial.empty())
      return identity;
   std::string vendorPath;
   if (SDL_hid_device_info* const devices = EnumerateAllHIDDevices(0x045E, 0x0B13); devices)
   {
      for (const SDL_hid_device_info* dev = devices; dev; dev = dev->next)
      {
         if (dev->usage_page != 0xFF00)
            continue;
         std::string devSerial;
         for (const wchar_t* c = dev->serial_number; c && *c; c++)
            devSerial += static_cast<char>(*c);
         if (NormalizedSerial(devSerial) != serial)
            continue;
         identity.dofChannel |= dev->usage == 0x01;
         if (vendorPath.empty() && dev->path)
            vendorPath = dev->path;
      }
      SDL_hid_free_enumeration(devices);
   }
   if (vendorPath.empty())
      return identity;

   bool dof = false;
   identity.xbox = ReadIdentity(vendorPath.c_str(), identity.firmware, dof);
   if (identity.xbox)
      PLOGI << "PinOne: the Xbox controller " << joySerial << " is a PinOne, firmware " << identity.firmware << (dof ? ", with a DOF channel" : "");
   else if (identity.dofChannel)
      PLOGI << "PinOne: the Xbox controller " << joySerial << " has the PinOne's DOF channel but no identity report (firmware before the identity report)";
   return identity;
}

std::string DisplayName(const Identity& identity, const std::string& name) { return identity.xbox ? "PinOne (XBox)"s : name; }

void OnJoystickAdded(uint16_t deviceId, const Identity& identity)
{
   if (identity.IsPinOne() || identity.dofChannel)
      s_identities[deviceId] = identity;
   else
      s_identities.erase(deviceId);
}

const Identity& Get(uint16_t deviceId)
{
   static const Identity none;
   const auto found = s_identities.find(deviceId);
   return found == s_identities.end() ? none : found->second;
}

}
