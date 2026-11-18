// license:GPLv3+

#include "core/stdafx.h"
#include "Vibrations.h"

#include "core/VPApp.h"

#include <optional>

namespace VPX::Vibrations
{

static std::optional<uint16_t> s_startDevice;

void Reset()
{
   s_startDevice.reset();
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
   if (!s_startDevice.has_value() || !g_settingsService.GetAppSettings().GetPlayer_RumbleStartController())
      return true;
   return *s_startDevice == deviceId;
}

}
