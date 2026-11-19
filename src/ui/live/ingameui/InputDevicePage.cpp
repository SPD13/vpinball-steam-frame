// license:GPLv3+

#include "core/stdafx.h"
#include "InputDevicePage.h"

#include "core/VPApp.h"
#include "ui/live/LiveUI.h"
#if defined(VPX_STEAM_FRAME)
#include "steamframe/ControllerLayouts.h"
#include "steamframe/DofSetup.h"
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
   {
      // DOF for the PinOne (src/steamframe/DofSetup.h)
      AddItem(std::make_unique<InGameUIItem>("Set PinOne Mini mapping and DOF Config"s,
         "Replace the mappings of this controller with the PinOne layout (flippers, magna-saves, fire button, plunger and nudge), turn DOF on and install the game's DOF configuration for the PinOne Mini, replacing an uploaded one."s,
         [this]()
         {
            VPX::DofSetup::ApplyPinOneMiniSetup(m_player, m_deviceId);
            m_player->m_liveUI->m_inGameUI.NavigateBack();
            RequestRebuild();
         }));
      if (const auto dofEnableId = Settings::GetRegistry().GetPropertyId("Plugin.DOF"s, "Enable"s); dofEnableId.has_value())
      {
         AddItem(std::make_unique<InGameUIItem>( //
                    dofEnableId.value(), //
                    [this]() { return VPX::DofSetup::IsDofEnabled(m_player); }, //
                    [this](bool v)
                    {
                       VPX::DofSetup::SetDofEnabled(m_player, v);
                       RequestRebuild();
                    }))
            .m_excludeFromDefault
            = true;
         AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info,
            "While DOF is on, this controller's vibrations are off: the table drives its outputs. Tables missing from the DOF configuration still get the vibrations."s));
      }
   }
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
