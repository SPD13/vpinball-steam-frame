// license:GPLv3+

#pragma once

#include "unordered_dense.h"
#include "InGameUIPage.h"

class LiveUI;

namespace VPX::InGameUI
{

class InGameUI final
{
public:
   explicit InGameUI(LiveUI &liveUI);
   ~InGameUI() = default;

   void Open(const string& page);
   bool IsOpened() const { return GetActivePage() != nullptr && GetActivePage()->IsActive(); }
   bool IsOpened(const string &page) const { return IsOpened() && m_navigationHistory.back() == page; }
   void Update();
   void Close();

   bool ProposeInputLayout(const string& deviceName, const std::function<void(bool, bool)>& handler);

   void AddPage(const string &path, const std::function<std::unique_ptr<InGameUIPage>()>& pageFactory);
   void Navigate(const string &path, bool isBack = false);
   void NavigateBack();
   void RequestRebuildAll();

   bool IsFlipperNav() const { return m_useFlipperNav; }
   void UsePointerNav() { m_useFlipperNav = false; } // For pointers which can not be detected from mouse moves, like VR controllers
#if defined(VPX_STEAM_FRAME)
   bool UseButtonNav(); // Steam Frame Edition: any button of the UI switches to button navigation. Returns whether it was already used.
   void OnUIConfirm(); // Steam Frame Edition: UI: Confirm (and A of a VR controller) activates the selected item, or starts/ends editing it
   bool IsEditing(const InGameUIItem* item); // Steam Frame Edition: the item is being edited with the buttons (see OnUIConfirm)
   void OnUIRow(int direction); // Steam Frame Edition: UI: Up (-1), UI: Down (1)
   bool IsNavigatingBack() const { return m_navigatingBack; } // Steam Frame Edition: the page being created is the one gone back to
#endif
   void OnUIUpAction();
   void OnUIDownAction();
   void OnUILeftAction();
   void OnUIRightAction();
   void OnUIResetToDefaults();
   void OnUICancelChanges();
   void OnUISaveChanges();
   void OnUINavigateBack();

   InGameUIPage* GetActivePage() const { return m_activePages.empty() ? nullptr : m_activePages.back().get(); }

private:
#if defined(VPX_STEAM_FRAME)
   friend class VRUINavigation; // Steam Frame Edition: the new VR menu goes to a screen with a fresh navigation history (src/steamframe/vrui/Shell.cpp)
#endif
   void HandlePageInput();
   void HandleLegacyFlyOver();
#if defined(VPX_STEAM_FRAME)
   // Steam Frame Edition: UI: Next Item and UI: Previous Item (src/ui/live/ingameui/InGameUIPage.h, StepItem)
   void OnUIStep(int direction);
   enum class ButtonUse { Confirm, Back, Next, Previous, Up, Down, Count };
   bool IsSameButtonPress(ButtonUse use);
   int m_lastButtonFrame[static_cast<int>(ButtonUse::Count)] = { -1, -1, -1, -1, -1, -1 };
   // The value edited with the buttons: its page (the one its AdjustItem changes it), the key identifying it, its value before editing
   InGameUIPage* GetEditedPage();
   void BeginEdit(InGameUIPage& page, const InGameUIItem& item);
   void EndEdit(bool keep);
   bool m_navigatingBack = false;
   InGameUIPage* m_editedPage = nullptr;
   string m_editedKey;
   bool m_editedRestorable = false;
   float m_editedFloat = 0.f;
   int m_editedInt = 0;
#endif

   Player *m_player;
   ImVec2 m_prevMousePos;
   bool m_useFlipperNav = false;
   bool m_itemAdjustWasPressed = false;
   int m_flipperNavRepeatCount = 0;
   uint32_t m_flipperNavStart = 0;
   ankerl::unordered_dense::map<string, std::function<std::unique_ptr<InGameUIPage>()>> m_pages;
   vector<string> m_navigationHistory;
   vector<std::unique_ptr<InGameUIPage>> m_activePages;
   uint32_t m_lastRenderMs = 0;
};

}
