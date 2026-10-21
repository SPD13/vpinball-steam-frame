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

   Player *m_player;
   ImVec2 m_prevMousePos;
   bool m_useFlipperNav = false;
   int m_flipperNavRepeatCount = 0;
   uint32_t m_flipperNavStart = 0;
   ankerl::unordered_dense::map<string, std::function<std::unique_ptr<InGameUIPage>()>> m_pages;
   vector<string> m_navigationHistory;
   vector<std::unique_ptr<InGameUIPage>> m_activePages;
   uint32_t m_lastRenderMs = 0;
};

}
