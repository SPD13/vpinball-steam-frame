// license:GPLv3+

#include "core/stdafx.h"
#include "PinOneLayoutPopup.h"

#include "core/VPApp.h"
#include "steamframe/DofSetup.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/InGameUI.h"
#include "ui/live/ingameui/InGameUIItem.h"
#include "ui/live/ingameui/InGameUIPage.h"

namespace VPX::PinOneLayoutPopup
{

namespace
{

class PinOneLayoutPage final : public VPX::InGameUI::InGameUIPage
{
public:
   PinOneLayoutPage(uint16_t deviceId, const string& deviceName, const std::function<void()>& onClosed)
      : InGameUIPage("PinOne Detected"s, ""s, SaveMode::None)
      , m_deviceId(deviceId)
      , m_deviceName(deviceName)
      , m_onClosed(onClosed)
   {
   }

private:
   void BuildPage() override
   {
      using VPX::InGameUI::InGameUIItem;
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "PinOne '" + m_deviceName + "' was detected."));
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info,
         "Would you like the PinOne Mini mapping and its DOF configuration to be applied? This is what 'Set PinOne Mini mapping and DOF Config' does in the controller's page (Controls)."s));

      AddItem(std::make_unique<InGameUIItem>(
         VPX::Properties::BoolPropertyDef(""s, ""s, "Don't show again"s,
            "Never propose the PinOne Mini mapping in a pop-up again. It stays in the controller's page, where this pop-up can be turned back on."s, false, false),
         [this]() { return m_dontShowAgain; }, [this](const Settings&) { return m_dontShowAgain; }, [this](bool v) { m_dontShowAgain = v; },
         [](Settings&) { /* UI state, saved when answered */ }, [](bool, Settings&, bool) { /* UI state, saved when answered */ }));

      AddItem(std::make_unique<InGameUIItem>("Apply"s,
         "Replace the mappings of this controller with the PinOne layout (flippers, magna-saves, fire button, menus, plunger and nudge), turn DOF on and install the game's DOF configuration for the PinOne Mini, replacing an uploaded one."s,
         [this]() { Answer(true); }));

      AddItem(std::make_unique<InGameUIItem>("Not now"s, "Keep the mappings of this controller as they are."s, [this]() { Answer(false); }));
   }

   void Answer(bool apply)
   {
      if (m_dontShowAgain)
      {
         g_settingsService.GetAppSettings().SetPlayer_PinOneLayoutPopup(false, false);
         g_settingsService.GetAppSettings().Save();
      }
      if (apply)
         VPX::DofSetup::ApplyPinOneMiniSetup(m_player, m_deviceId);
      PLOGI << "PinOne layout pop-up: " << (apply ? "applied" : "not now") << (m_dontShowAgain ? ", not shown again" : "") << " for '" << m_deviceName << '\'';
      m_onClosed();
      m_player->m_liveUI->m_inGameUI.NavigateBack();
   }

   const uint16_t m_deviceId;
   const string m_deviceName;
   const std::function<void()> m_onClosed;
   bool m_dontShowAgain = false;
};

}

bool Propose(Player* player, uint16_t deviceId, const std::function<void()>& onClosed)
{
   VPX::InGameUI::InGameUI& ui = player->m_liveUI->m_inGameUI;
   const bool overMenu = ui.IsOpened();
   if (overMenu && !CanOpenOverMenu(player))
      return false;
   const string name = player->m_pininput.GetDeviceName(deviceId);
   ui.AddPage("popup/pinone_layout"s, [deviceId, name, onClosed]() { return std::make_unique<PinOneLayoutPage>(deviceId, name, onClosed); });
   if (overMenu)
      ui.Navigate("popup/pinone_layout"s); // Answered, NavigateBack returns to the picker
   else
      ui.Open("popup/pinone_layout"s);
   return true;
}

bool CanOpenOverMenu(Player* player)
{
   return player->m_isLobby && player->m_liveUI->m_inGameUI.IsOpened("tables/picker"s);
}

}
