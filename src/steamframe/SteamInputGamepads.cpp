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

}
