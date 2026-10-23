// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUIPage.h"

#ifdef __STANDALONE__

#include <future>

namespace VPinballLib { struct Table; }
class BaseTexture;

namespace VPX::InGameUI
{

// Keyboard drawn with buttons, for VR where there is no keyboard: keys change 'text' at once (up to 'maxLength' bytes). With 'capitalizeWords',
// letters are uppercase at the start of words and lowercase elsewhere, otherwise always lowercase. Returns true when 'Done' is pressed
// ('withDone' adds this key).
bool RenderVirtualKeyboard(string& text, size_t maxLength, bool capitalizeWords, bool withDone);

// Items of the MENU tab of the table picker, also shown by the screens of the new VR menu (src/steamframe/vrui/)
void AddLibraryItems(InGameUIPage& page); // View, image focus, menu distance, sort, rescan, tables folder
void AddMenuDistanceItem(InGameUIPage& page); // VR only
void AddLobbyRoomItems(InGameUIPage& page); // Launcher mode only
std::unique_ptr<InGameUIPage> CreateLobbyRoomsPage(); // The room chooser, page 'lobby/rooms'
void AddQuitItem(InGameUIPage& page);
#ifdef VPX_TABLE_WEBSERVER
void AddWebServerItems(InGameUIPage& page); // Wi-Fi upload
bool HaveWebServerItemsChanged(); // Address or pairing code changed since the items were added: they must be rebuilt
bool AdjustWebServerItem(const InGameUIItem& item); // Left/Right on an item of AddWebServerItems: true if handled
#endif

// Lists the tables of the application's table library, to switch table without leaving the player
class TablePickerPage final : public InGameUIPage
{
public:
   // 'newView': the Tables screen of the new VR menu (src/steamframe/vrui/), which shows the scores and the options of the MENU tab on
   // screens of their own, and draws the tabs and the search field with its widgets
   explicit TablePickerPage(bool newView = false);
   ~TablePickerPage() override;

   void Open(bool isBackwardAnimation) override;
   void Render(float elapsedS) override;
   void SelectNextItem() override;
   void SelectPrevItem() override;
   void AdjustItem(float direction, bool isInitialPress) override;
   void ActivateItem() override;

   // New VR menu: opens its keyboard on the search field (as clicking it does)
   void OpenSearchKeyboard();

private:
   void BuildPage() override;

   // Table images, loaded when they get visible. Decoding takes a few milliseconds, too long for a frame: it runs on worker threads, a few at
   // a time, and the decoded images are uploaded to the GPU one per frame at most. The tiles show a spinner meanwhile. The images not
   // displayed for the longest time are released beyond a budget (MAX_KEPT_THUMBNAILS), so that long lists do not fill the GPU memory.
   struct Thumbnail
   {
      std::shared_ptr<BaseTexture> texture;
      int64_t modifiedAt = 0;
      std::future<std::shared_ptr<BaseTexture>> loading; // Valid while decoding
      int64_t loadingModifiedAt = 0;
      int lastUsedFrame = 0;
   };
   ImTextureID GetThumbnail(const VPinballLib::Table& table);
   bool IsThumbnailLoading(const VPinballLib::Table& table) const;
   void UpdateThumbnailLoads();
   void ReleaseThumbnail(Thumbnail& thumbnail);
   ankerl::unordered_dense::map<string, Thumbnail> m_thumbnails;
   vector<std::future<std::shared_ptr<BaseTexture>>> m_abandonedLoads; // Of thumbnails no longer displayed, kept until done not to wait for them
   bool m_thumbnailLoadedThisFrame = false;

   void SetTab(int tab);
   void StepTab(int direction);
   void ActivateTabIcon(int icon);
   void StepFilter(int direction);
   void SetSearch(const string& search);
   void RenderTabs();
   void RenderTabsNewView();
   void RenderSearchNewView();
   void BuildMenuTab();
   void KeepThumbnails(const ankerl::unordered_dense::set<string>& displayedTables);
   void RenderSearch();
   bool m_virtualKeyboard = false; // Shown under the search field in VR, where there is no keyboard, from its activation until 'Done'
   const bool m_newView;
   bool m_searchSelected = false; // New view: the search field is the selected item (its keyboard opens when the buttons select it)
   int m_tabIcon = -1; // New view: the icon button of the tab row focused with the buttons (TabIcon), -1 when on the tabs

   uint64_t m_revision = 0;
   uint64_t m_scoresRevision = 0; // Of the profiles and scores, shown by the player item and the Scores tab
   bool m_scanning = false;
};

// Text entry with the 4 navigation buttons, like the initials of an arcade high score: left/right select a character, which is then added.
// A virtual keyboard is also shown, for pointers (VR controllers, mouse).
class TextEntryPage final : public InGameUIPage
{
public:
   TextEntryPage(const string& title, const string& text, const std::function<void(const string&)>& onSave, bool allowEmpty = false, size_t maxLength = 64);

   void AdjustItem(float direction, bool isInitialPress) override;
   void ActivateItem() override;

private:
   void BuildPage() override;
   string GetKeyboardOwner() const;
   void OpenKeyboard(); // New VR menu
   bool CanSave() const;
   void Save();

   string m_text;
   size_t m_charIndex = 0;
   bool m_keyboardOpened = false; // New VR menu: the keyboard opens with the page, once
   const std::function<void(const string&)> m_onSave;
   const bool m_allowEmpty;
   const size_t m_maxLength;
};

// Actions on a table of the library: play, rename, reset settings, delete
class TableActionsPage final : public InGameUIPage
{
public:
   explicit TableActionsPage(const string& uuid);

   void AdjustItem(float direction, bool isInitialPress) override;

private:
   void BuildPage() override;

   const string m_uuid;
   bool m_confirmDelete = false;
};

}

#endif
