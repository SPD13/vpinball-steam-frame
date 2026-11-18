// license:GPLv3+

#include "core/stdafx.h"
#include "InputDevicePage.h"

#include "core/VPApp.h"
#include "ui/live/LiveUI.h"
#if defined(VPX_STEAM_FRAME)
#include "steamframe/ControllerLayouts.h"
#endif


namespace VPX::InGameUI
{

InputDevicePage::InputDevicePage(uint16_t deviceId)
   : InGameUIPage("Input Device"s, ""s, SaveMode::Global)
   , m_deviceId(deviceId)
   , m_deviceSettingId(m_player->m_pininput.GetDeviceSettingId(m_deviceId))
{
}

void InputDevicePage::BuildPage()
{
   const auto noAutoLayoutId = Settings::GetRegistry().GetPropertyId("Input"s, std::format("Device.{}.NoAutoLayout", m_deviceSettingId));
#if defined(VPX_STEAM_FRAME)
   // No pop-up proposes a layout in the fork: they are offered here (src/steamframe/ControllerLayouts.h)
   if (m_player->m_pininput.HasDeviceDefaultMapping(m_deviceId))
      AddItem(std::make_unique<InGameUIItem>("Set default mapping"s, "Replace the mappings of this controller with its default layout."s,
         [this]()
         {
            m_player->m_pininput.ApplyDefaultDeviceMapping(m_deviceId);
            m_player->m_liveUI->m_inGameUI.NavigateBack();
            RequestRebuild();
         }));
   if (VPX::ControllerLayouts::HasPinOneLayout(m_player->m_pininput, m_deviceId))
      AddItem(std::make_unique<InGameUIItem>("Set PinOne mapping"s,
         "Replace the mappings of this controller with the PinOne layout: flippers, magna-saves, fire button (launch ball), plunger and nudge."s,
         [this]()
         {
            VPX::ControllerLayouts::ApplyPinOneLayout(m_player->m_pininput, m_deviceId);
            m_player->m_liveUI->m_inGameUI.NavigateBack();
            RequestRebuild();
         }));
#else
   if (m_player->m_pininput.HasDeviceDefaultMapping(m_deviceId))
   {
      if (noAutoLayoutId.has_value())
         AddItem(std::make_unique<InGameUIItem>( //
            noAutoLayoutId.value(), //
            [noAutoLayoutId]() { return g_settingsService.GetAppSettings().GetBool(noAutoLayoutId.value()); }, //
            [noAutoLayoutId](bool v) { g_settingsService.GetAppSettings().Set(noAutoLayoutId.value(), v, false); }));

      AddItem(std::make_unique<InGameUIItem>("Apply default layout"s, "Reset the input layout of this device to its default."s,
         [this]()
         {
            m_player->m_pininput.ApplyDefaultDeviceMapping(m_deviceId);
            m_player->m_liveUI->m_inGameUI.NavigateBack();
            RequestRebuild();
         }));
   }
#endif

   AddItem(std::make_unique<InGameUIItem>("Erase this device"s, "Clean all mappings using this device."s,
      [this, noAutoLayoutId]()
      {
         if (noAutoLayoutId.has_value())
            g_settingsService.GetAppSettings().Set(noAutoLayoutId.value(), false, false);
         m_player->m_pininput.ClearDeviceMappings(m_deviceId);
         m_player->m_liveUI->m_inGameUI.NavigateBack();
         RequestRebuild();
      }));
}

}
