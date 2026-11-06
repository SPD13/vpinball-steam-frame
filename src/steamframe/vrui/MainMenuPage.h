// license:GPLv3+

#pragma once

#include "Screens.h"

namespace VPX::VRUI
{

// The main menu of the new VR UI: one page with a rail of icon buttons on the left, choosing a screen, whose content is another page
// (see Screens.h) drawn inside the menu. With button navigation, the focus is either on the rail (Next/Previous change the screen, Confirm
// enters it) or on the content (Cancel gets back to the rail). The menu opens with the focus on the rail, except when getting back to it
// from one of its sub-pages.
// It replaces upstream's home page and the classic table picker when the new VR UI is active (see RegisterRoutes in VRUI.cpp).
class MainMenuPage final : public InGameUI::InGameUIPage
{
public:
   explicit MainMenuPage(std::optional<Screen> screen = std::nullopt);

   Screen GetScreen() const { return m_screen; }
   void SetScreen(Screen screen);
   InGameUIPage* GetContent();
   void BackToRailScreen(); // From a screen opened by another one (VPX Performance, Device Performance), back to that one
   bool IsRailFocused() const { return m_railFocus; }
   void SetRailFocus(bool railFocus) { m_railFocus = railFocus; }

   // Screen of the last menu, shown again when getting back to the menu from one of its sub-pages
   static Screen GetLastScreen();
   static void SetLastScreen(Screen screen);

   void Close(bool isBackwardAnimation) override;
   void Render(float elapsedS) override;
   void SelectNextItem() override;
   void SelectPrevItem() override;
   void AdjustItem(float direction, bool isInitialPress) override;
   void ReleaseAdjustedItem() override;
   void ActivateItem() override;
   bool HandleBack(bool wasButtonNav) override;
   void StepItem(int direction, StepPhase phase) override;
   void StepRow(int direction, StepPhase phase) override;
   InGameUIPage* GetButtonTarget() override;
   const InGameUI::InGameUIItem* GetFocusedItem() const override { return nullptr; } // On the rail (otherwise the buttons act on the screen)
   bool IsDefaults() const override;
   bool IsModified() const override;
   void Save() override;
   void SaveGlobally() override;
   void SaveTableOverride() override;
   void ResetToStoredValues() override;
   void ResetToDefaults() override;
   bool IsPlayerPauseAllowed() const override;

private:
   void BuildPage() override { }
   void StepScreen(int direction);

   Screen m_screen;
   bool m_railFocus = false;
   // The pages of the screens, created when first shown and kept while the menu is opened (to keep their state, and not to rescan the library each time)
   std::array<std::unique_ptr<InGameUIPage>, kScreenCount> m_contents;
   bool m_restorePosition = false; // Back from a page it opened: the screen gets back to the item it was opened from
};

}
