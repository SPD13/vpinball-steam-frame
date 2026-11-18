// license:GPLv3+

#include "core/stdafx.h"
#include "SteamInputGamepads.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace VPX::SteamInputGamepads
{

void ReadControllersClaimedBySteam()
{
   const char* const ignored = SDL_getenv("SDL_GAMECONTROLLER_IGNORE_DEVICES");
   if (ignored == nullptr || *ignored == '\0')
      return;
   const size_t count = 1 + std::count(ignored, ignored + strlen(ignored), ',');
   PLOGI << "Steam Input: reading directly the " << count << " controller types Steam asked to ignore (SDL_GAMECONTROLLER_IGNORE_DEVICES), "
            "as Steam does not feed its virtual pads to VPX in the headset (docs/steam-frame/Gamepads and Steam Input.md)";
   SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, "", SDL_HINT_OVERRIDE);
}

static bool IsSteamVirtualGamepadGUID(SDL_GUID guid)
{
   Uint16 vendor = 0, product = 0;
   SDL_GetJoystickGUIDInfo(guid, &vendor, &product, nullptr, nullptr);
   return vendor == 0x28de && product == 0x11ff;
}

bool IsSteamVirtualGamepad(SDL_JoystickID id)
{
   if (!IsSteamVirtualGamepadGUID(SDL_GetJoystickGUIDForID(id)))
      return false;
   const char* const name = SDL_GetJoystickNameForID(id);
   PLOGI << "Steam Input: virtual pad '" << (name ? name : "") << "' left out, VPX reads the controllers directly";
   return true;
}

bool IsSteamVirtualGamepadSettingsId(const std::string& settingsId)
{
   if (settingsId.size() < 7 + 32 || !settingsId.starts_with("SDLJoy_"))
      return false;
   return IsSteamVirtualGamepadGUID(SDL_StringToGUID(settingsId.substr(7, 32).c_str()));
}

}
