// license:GPLv3+

#pragma once

#include "InGameUIItem.h"
#include "imgui/imgui.h"

class Player;

namespace VPX::InGameUI
{

class InGameUIPage
{
public:
   enum class SaveMode { None, Global, Table, Both };

   InGameUIPage(const string& title, const string& info, SaveMode saveMode);
   virtual ~InGameUIPage() = default;

   Settings& GetSettings();

   InGameUIItem& AddItem(std::unique_ptr<InGameUIItem> item);

   virtual void Open(bool isBackwardAnimation);
   virtual void Close(bool isBackwardAnimation);
   virtual void Save();
   bool IsActive() const { return m_openAnimTarget == 0.f; }
   bool IsClosed() const { return (m_openAnimPos == m_openAnimTarget) && (m_openAnimPos == -1.f || m_openAnimPos == 1.f); }
   virtual void SaveGlobally();
   virtual void SaveTableOverride();
   virtual void ResetToStoredValues();
   virtual void ResetToDefaults();
   bool IsResettingToDefaults() const { return m_resettingToDefaults; }
   bool IsResettingToInitialValues() const { return m_resettingToInitialValues; }
   bool IsResetting() const { return m_resettingToDefaults || m_resettingToInitialValues; }
   virtual void Render(float elapsedS);
   virtual void SelectNextItem();
   virtual void SelectPrevItem();
   virtual void AdjustItem(float direction, bool isInitialPress);
   virtual void ReleaseAdjustedItem();
   bool IsAdjustable() const;
   virtual bool IsDefaults() const;
   virtual bool IsModified() const;
   virtual bool IsFlipperNavNeeded() const { return false; }
#if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition: what the OK button (A) or a click does on the selected item, which may differ from Right (tiles move in their grid,
   // text fields of the new VR UI give the focus to its keyboard: src/steamframe/vrui/PageAdapter.cpp)
   virtual void ActivateItem();
   // Moves the selection in the grid of tiles it is in (src/steamframe/vrui/LegacyTiles.cpp)
   bool StepTileGrid(int dx, int dy);
   int m_tileColumns = 1; // Columns of the grid of tiles, as last drawn by the view
   // Back button (Credit, Exit) on this page: true when the page used it, otherwise the menu goes back to the previous page (the cabinet
   // launcher, src/steamframe/CabinetLauncherPage.cpp, is never closed with Back). wasButtonNav: the buttons were already navigating
   // (not a pointer), as in the main menu of the VR UI where Back gets from a screen to its rail
   virtual bool HandleBack(bool wasButtonNav) { return false; }
   // The buttons of the menus: UI: Next Item and UI: Previous Item go through the stops of the page in reading order, UI: Confirm activates
   // the stop (or starts editing a value, which Next/Previous then change), UI: Cancel goes back (or restores the edited value). With a
   // full D-pad, UI: Up and UI: Down move by rows in grids. See InGameUI::OnUIStep and src/steamframe/vrui/PageAdapter.cpp.
   enum class StepPhase
   {
      Press, // The button was pressed
      Repeat, // It is held: repeated, faster and faster
      Held, // It is held: every frame, for pages with their own repeats (the reel of the cabinet launcher)
   };
   virtual void StepItem(int direction, StepPhase phase); // Default: the previous/next item
   virtual void StepRow(int direction, StepPhase phase); // UI: Up/Down. Default: the row above/below in a grid of tiles, otherwise StepItem
   virtual InGameUIPage* GetButtonTarget() { return this; } // The page the buttons act on (the screen of the main menu of the VR UI)
   virtual const InGameUIItem* GetFocusedItem() const { return GetSelectedItem(); } // Its item they act on (one of its sections)
   virtual bool IsEditableItem(const InGameUIItem& item) const; // Edited once confirmed: the sliders and choices
   virtual string GetEditKey(const InGameUIItem& item) const; // What identifies the edited item, even when the page is rebuilt (its label)
   // The selected item, kept by the main menu of the VR UI when it opens a page, to get back to it
   virtual int GetNavPosition() const { return m_selectedItem; }
   virtual void SetNavPosition(int position) { m_selectedItem = position; }
   // Moves the open/close animation forward, for pages drawing themselves without calling InGameUIPage::Render (CabinetLauncherPage.cpp)
   void UpdateOpenAnimation(float elapsedS);
#endif

   InGameUIItem* GetItem(const string& label) const;
   const InGameUIItem* GetSelectedItem() const { return (m_selectedItem >= 0 && m_selectedItem < (int)m_items.size()) ? m_items[m_selectedItem].get() : nullptr; }

   virtual bool IsPlayerPauseAllowed() const { return true; }

   ImVec2 GetWindowPos() const { return m_windowPos; }
   ImVec2 GetWindowSize() const { return m_windowSize; }
   bool IsWindowHovered() const { return m_windowHovered; }
   float GetOpenCloseAnimPos() const { return m_openAnimPos; }

   Player* const m_player;

   void RequestRebuild()
   {
      if (m_deferRebuild)
         m_rebuildPending = true;
      else
         m_needsRebuild = true;
   }
   virtual void BuildPage() = 0;

private:
   bool HasSelectableItem() const; // Selection loops search for a selectable item, so they must not run without one

   const string m_title;
   const string m_info;
   const SaveMode m_saveMode;

   bool m_needsRebuild = true;
   bool m_deferRebuild = false;
   bool m_rebuildPending = false;
   bool m_isBuildingPage = false;
   void ClearItems();

   // Open/Close animations
   // -1.f = closed, 0.f = opened, 1.f = appearing
   float m_openAnimPos = -1.f;
   float m_openAnimStart = -1.f;
   float m_openAnimTarget = -1.f;
   float m_openAnimElapsed = 0.f;

   ImVec2 m_windowPos = ImVec2(0.f, 0.f);
   ImVec2 m_windowSize = ImVec2(0.f, 0.f);
   bool m_windowHovered = false;
   float m_adjustedValue = 0.f;
   uint32_t m_lastUpdateMs = 0;
   uint32_t m_pressStartMs = 0;
   string m_pressedItemLabel;
   float m_pressedItemScroll = 0.f;
   bool m_isDraggingScroll = false;
   float m_dragScrollVelocity = 0.f; // px/s, positive = scroll down
   ImVec2 m_dragScrollStartPos = ImVec2(0.f, 0.f);
   vector<std::unique_ptr<InGameUIItem>> m_items;
   int m_selectedItem = 0;
   unsigned int m_resetNotifId = 0;
   bool m_resettingToDefaults = false;
   bool m_resettingToInitialValues = false;

#if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition (src/steamframe/vrui/): tiles of the classic UI, and the new VR UI which draws the page instead of Render when it is active
   void RenderTile(int index, const ImVec2& pos, const ImVec2& size, const InGameUIItem*& hoveredItem);
   friend class InGameUIPageView;
   bool RenderVRUI(float elapsedS);
#endif
   void RenderInputActionPopup();
   bool m_defineActionPopup = false;
   InGameUIItem* m_defineActionItem = nullptr;

   void BrowseForPath(const InGameUIItem& item);
   // Folder/file selection dialogs are asynchronous: their result is applied to the targeted item on the next frame
   std::shared_ptr<string> m_pathSelectResult;
   string m_pathSelectGroupId;
   string m_pathSelectPropId;

   static void RenderToggle(const string& label, const ImVec2& size, bool& v);
   static void TextWithEllipsis(const string& text, float maxWidth);
};

}
