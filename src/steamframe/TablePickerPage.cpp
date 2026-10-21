// license:GPLv3+

#include "core/stdafx.h"
#include "TablePickerPage.h"
#include "ScoresPage.h"

#ifdef __STANDALONE__

#include "core/VPApp.h"
#include "steamframe/Launcher.h"
#include "steamframe/ScoreStore.h"
#include "steamframe/TableLibrary.h"
#ifdef VPX_TABLE_WEBSERVER
#include "lib/src/WebServer.h"
#endif
#include "renderer/Renderer.h"
#include "renderer/Texture.h"
#include "renderer/VRDevice.h"
#include "ui/live/LiveUI.h"
#include "steamframe/vrui/Keyboard.h"
#include "steamframe/vrui/VRUI.h"
#include "steamframe/vrui/Theme.h"
#include "steamframe/vrui/Widgets.h"

#include "fonts/IconsForkAwesome.h"
#include "imgui/imgui_stdlib.h"

using LibraryTable = VPinballLib::Table; // 'Table' alone is ambiguous with VPX parts
using VPinballLib::TableLibrary;

namespace VPX::InGameUI
{

// State of the picker, kept while the application runs (the page is recreated each time it is opened)
enum class PickerTab { All, Recent, NewlyAdded, MostPlayed, Favorites, Scores, Menu }; // Scores: the leaderboards, Menu: the options of the library and the application, instead of a list
static constexpr const char* TAB_NAMES[] = { "All", "Recent", "New", "Most played", "Favorites", "Scores", "MENU" }; // Short names: the tabs fit on one line in VR
static PickerTab s_tab = PickerTab::All;
// Tabs of the new VR menu, in order: its Scores and System screens replace the last two tabs
static constexpr PickerTab NEW_VIEW_TABS[] = { PickerTab::All, PickerTab::Recent, PickerTab::Favorites, PickerTab::MostPlayed, PickerTab::NewlyAdded };
static constexpr const char* NEW_VIEW_TAB_NAMES[] = { "All", "Recent", "Favorites", "Most played", "Newly added" };
static string s_search;
static int s_page = 0;
static char s_filterInitial = 0; // 'All' tab: 0 for all tables, '#' for names which do not start with a letter

// Custom rendered items are identified by their label, which is not displayed
static constexpr const char* TABS_ITEM = "##tabs";
static constexpr const char* SEARCH_ITEM = "##search";
static constexpr const char* PAGER_TOP_ITEM = "##pager-top";
static constexpr const char* PAGER_BOTTOM_ITEM = "##pager-bottom";
static constexpr const char* PAIRING_ITEM = "##pairing";
static constexpr const char* FILTER_LABEL = "Show: ";

#ifdef VPX_TABLE_WEBSERVER
// Whether browsers must enter the pairing code, remembered for the next sessions, and applied at once to the running server
static void SetWebServerPairing(bool required)
{
   g_settingsService.GetAppSettings().SetStandalone_WebServerPairing(required, false);
   g_settingsService.GetAppSettings().Save();
   g_launcher->GetWebServer().SetPairingRequired(required);
   PLOGI << "Wi-Fi upload pairing code " << (required ? "enabled" : "disabled");
}
#endif

static constexpr int TABLES_PER_PAGE = 12; // Rows of 2, 3, 4 or 6 tiles, depending on the width of the menu

static char GetInitial(const LibraryTable& table)
{
   const unsigned char c = table.name.empty() ? '#' : static_cast<unsigned char>(table.name[0]);
   return std::isalpha(c) ? static_cast<char>(std::toupper(c)) : '#';
}

// Date, and optionally time, in the local time zone
static string FormatTime(int64_t secondsSinceEpoch, bool withTime)
{
   const time_t time = static_cast<time_t>(secondsSinceEpoch);
   std::tm localTime {};
#ifdef _WIN32
   localtime_s(&localTime, &time);
#else
   localtime_r(&time, &localTime);
#endif
   char buffer[64];
   return strftime(buffer, sizeof(buffer), withTime ? "%d %b %Y at %H:%M" : "%d %b %Y", &localTime) > 0 ? string(buffer) : string();
}

// What is displayed when a table is hovered or selected: when it was added, when it was last played and how many times, in 2 lines (the
// information area of the pages holds 3 lines, the first one is left for the name or the path of the table). 'compact' puts the dates
// on separate lines.
static string GetTableStatsInfo(const LibraryTable& table, bool compact = false)
{
   const string added = "Added on " + FormatTime(table.createdAt, false);
   if (table.playCount == 0 || table.lastPlayedAt == 0)
      return added + (compact ? ", never played" : "\nNever played");
   const string lastPlayed = "ast played on " + FormatTime(table.lastPlayedAt, true);
   const string played = table.playCount == 1 ? "layed once"s : std::format("layed {} times", table.playCount);
   return compact ? added + ", p" + played + "\nL" + lastPlayed : added + ", l" + lastPlayed + "\nP" + played;
}

static bool IsRunningTable(const Player* player, const LibraryTable& table)
{
   // The lobby may be loaded from a table, to use its VR room: that table is not running then
   if (player->m_isLobby)
      return false;
   std::error_code ec;
   return std::filesystem::equivalent(player->m_ptable->m_filename, g_launcher->GetTableLibrary().GetFullPath(table), ec);
}


TablePickerPage::TablePickerPage(bool newView)
   : InGameUIPage("Tables"s,
        newView ? "Select a table to play it. Tables are added by copying their folder to the tables folder, or from a browser (Wi-Fi upload)."s
                : "Select a table to play it.\nTables are added by copying their folder to the tables folder."s,
        SaveMode::None)
   , m_newView(newView)
{
}

// Each decode keeps a core busy: a few at a time leave the others to the game and rendering threads
static constexpr int MAX_THUMBNAIL_LOADS = 2;

static bool IsReady(const std::future<std::shared_ptr<BaseTexture>>& future)
{
   return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

TablePickerPage::~TablePickerPage()
{
   // Loads still running are waited for by their futures (a few milliseconds)
   for (auto& [uuid, thumbnail] : m_thumbnails)
      if (thumbnail.texture)
         m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(thumbnail.texture.get());
}

ImTextureID TablePickerPage::GetThumbnail(const LibraryTable& table)
{
   if (table.image.empty())
      return nullptr;
   Thumbnail& thumbnail = m_thumbnails[table.uuid];
   // First use, or the image was replaced (the library updates the modification date): decode it on a worker thread
   if (thumbnail.modifiedAt != table.modifiedAt && !thumbnail.loading.valid())
   {
      int loads = static_cast<int>(m_abandonedLoads.size());
      for (const auto& [uuid, other] : m_thumbnails)
         loads += other.loading.valid() ? 1 : 0;
      if (loads < MAX_THUMBNAIL_LOADS)
      {
         thumbnail.loadingModifiedAt = table.modifiedAt;
         thumbnail.loading = std::async(std::launch::async, [path = g_launcher->GetTableLibrary().GetImagePath(table)]() { return BaseTexture::CreateFromFile(path, 512); });
      }
   }
   // Until then, the previous image if it was replaced
   return thumbnail.texture ? m_player->m_renderer->m_renderDevice->m_texMan.LoadTexture(thumbnail.texture.get(), false) : nullptr;
}

bool TablePickerPage::IsThumbnailLoading(const LibraryTable& table) const
{
   if (table.image.empty())
      return false;
   const auto it = m_thumbnails.find(table.uuid);
   return it == m_thumbnails.end() || (it->second.texture == nullptr && it->second.modifiedAt != table.modifiedAt);
}

// Takes the decoded images, one per frame at most as it is uploaded to the GPU when the tile is drawn
void TablePickerPage::UpdateThumbnailLoads()
{
   std::erase_if(m_abandonedLoads, [](const auto& loading) { return IsReady(loading); });
   for (auto& [uuid, thumbnail] : m_thumbnails)
   {
      if (m_thumbnailLoadedThisFrame)
         break;
      if (thumbnail.loading.valid() && IsReady(thumbnail.loading))
      {
         m_thumbnailLoadedThisFrame = true;
         if (thumbnail.texture)
            m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(thumbnail.texture.get());
         thumbnail.texture = thumbnail.loading.get(); // Null if the image could not be decoded: not tried again until it changes
         thumbnail.modifiedAt = thumbnail.loadingModifiedAt;
      }
   }
}

void TablePickerPage::Open(bool isBackwardAnimation)
{
   InGameUIPage::Open(isBackwardAnimation);
   if (!isBackwardAnimation)
      g_launcher->GetTableLibrary().RescanAsync();
}

void TablePickerPage::Render(float elapsedS)
{
   m_thumbnailLoadedThisFrame = false;
   UpdateThumbnailLoads();

   // Scans run on a worker thread (they may import large files) and are also triggered outside of this page
   const TableLibrary& library = g_launcher->GetTableLibrary();
   if (m_revision != library.GetRevision() || m_scanning != library.IsScanning() || m_scoresRevision != g_launcher->GetScoreStore().GetRevision())
      RequestRebuild();
#ifdef VPX_TABLE_WEBSERVER
   // The pairing code is renewed after failed attempts
   if (s_tab == PickerTab::Menu && HaveWebServerItemsChanged())
      RequestRebuild();
#endif
   InGameUIPage::Render(elapsedS);

   // Get back on an item which moved as the page was rebuilt (the rebuild happens at the beginning of the render following the request)
   if (!m_reselectItem.empty() && m_reselectDelay-- <= 0)
   {
      for (int i = 0; i < 1000 && GetItem(m_reselectItem) != nullptr && GetSelectedItem() != GetItem(m_reselectItem); i++)
         SelectNextItem();
      m_reselectItem.clear();
   }
}

void TablePickerPage::SetTab(int tab)
{
   const int nTabs = static_cast<int>(std::size(TAB_NAMES));
   s_tab = static_cast<PickerTab>((tab + nTabs) % nTabs);
   s_page = 0;
   RequestRebuild();
}

void TablePickerPage::StepTab(int direction)
{
   if (!m_newView)
   {
      SetTab(static_cast<int>(s_tab) + direction);
      return;
   }
   const int nTabs = static_cast<int>(std::size(NEW_VIEW_TABS));
   int index = 0;
   for (int i = 0; i < nTabs; i++)
      if (NEW_VIEW_TABS[i] == s_tab)
         index = i;
   SetTab(static_cast<int>(NEW_VIEW_TABS[(index + direction + nTabs) % nTabs]));
}

// Previous/next initial, going through 'All'
void TablePickerPage::StepFilter(int direction)
{
   string initials(1, '\0');
   for (const LibraryTable& table : g_launcher->GetTableLibrary().GetTables())
      if (initials.find(GetInitial(table)) == string::npos)
         initials += GetInitial(table);
   std::sort(initials.begin() + 1, initials.end());
   const size_t pos = initials.find(s_filterInitial);
   const size_t count = initials.size();
   s_filterInitial = initials[pos == string::npos ? 0 : (pos + (direction < 0 ? count - 1 : 1)) % count];
   s_page = 0;
   RequestRebuild();
}

void TablePickerPage::SetPage(int page, const string& pagerItem)
{
   s_page = clamp(page, 0, max(m_pageCount - 1, 0));
   m_reselectItem = pagerItem; // The number of items above the bottom pager changes with the page
   m_reselectDelay = 1;
   RequestRebuild();
}

void TablePickerPage::SetSearch(const string& search)
{
   s_search = search;
   s_page = 0;
   RequestRebuild();
}

void TablePickerPage::AdjustItem(float direction, bool isInitialPress)
{
   // Actions of this page must not repeat while the button is held
   if (!isInitialPress)
      return;

   // Items which use the direction, or are custom rendered (the base page does not know what to do with these)
   if (const InGameUIItem* item = GetSelectedItem(); item)
   {
      if (item->m_label == TABS_ITEM)
      {
         StepTab(direction < 0.f ? -1 : 1);
         return;
      }
      if (item->m_label == PAGER_TOP_ITEM || item->m_label == PAGER_BOTTOM_ITEM)
      {
         SetPage((s_page + (direction < 0.f ? m_pageCount - 1 : 1)) % max(m_pageCount, 1), item->m_label);
         return;
      }
#ifdef VPX_TABLE_WEBSERVER
      if (AdjustWebServerItem(*item))
         return;
#endif
      if (item->m_label == SEARCH_ITEM)
      {
         // Without a keyboard, the search text is entered with the navigation buttons
         m_player->m_liveUI->m_inGameUI.AddPage("tables/search"s,
            []() { return std::make_unique<TextEntryPage>("Search tables"s, s_search, [](const string& search) { s_search = search; s_page = 0; }, true); });
         m_player->m_liveUI->m_inGameUI.Navigate("tables/search"s);
         return;
      }
      if (item->m_label.starts_with(FILTER_LABEL))
      {
         StepFilter(direction < 0.f ? -1 : 1);
         return;
      }
   }

   InGameUIPage::AdjustItem(direction, isInitialPress);
}

void TablePickerPage::RenderTabs()
{
   // One button per tab, large enough to be hit easily with a mouse or a VR pointer, wrapping on a second line if the menu is narrow (VR).
   // With buttons, left/right change the tab.
   const ImGuiStyle& style = ImGui::GetStyle();
   const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
   const float textHeight = ImGui::GetTextLineHeight();
   const ImVec2 padding(0.9f * textHeight, 0.4f * textHeight);
   const float rounding = 0.3f * textHeight;
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   // The row is not highlighted as a whole (only the hovered button is): with buttons, the selected tab turns green when the row has the focus
   const InGameUIItem* const selectedItem = GetSelectedItem();
   const bool hasFocus = m_player->m_liveUI->m_inGameUI.IsFlipperNav() && selectedItem != nullptr && selectedItem->m_label == TABS_ITEM;
   ImGui::PushID(TABS_ITEM);
   for (int i = 0; i < static_cast<int>(std::size(TAB_NAMES)); i++)
   {
      const ImVec2 textSize = ImGui::CalcTextSize(TAB_NAMES[i]);
      const ImVec2 size = textSize + padding * 2.f;
      if (i > 0)
      {
         ImGui::SameLine(0.f, style.ItemSpacing.x);
         if (ImGui::GetCursorScreenPos().x + size.x > right)
            ImGui::NewLine();
      }
      // MENU is not a list: it stands apart, at the right end of the line, clear of the scroll bar
      if (static_cast<PickerTab>(i) == PickerTab::Menu)
         ImGui::SetCursorScreenPos(ImVec2(right - size.x - style.ScrollbarSize - style.ItemSpacing.x, ImGui::GetCursorScreenPos().y));
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton(TAB_NAMES[i], size))
         SetTab(i);
      const bool isActive = static_cast<int>(s_tab) == i;
      const bool isHovered = ImGui::IsItemHovered() || (hasFocus && isActive);
      const ImU32 background = isHovered ? IM_COL32(0, 255, 0, 50) : isActive ? IM_COL32(255, 255, 255, 56) : IM_COL32(255, 255, 255, 16);
      const ImU32 color = isHovered ? IM_COL32(0, 255, 0, 255) : isActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 170, 170, 255);
      drawList->AddRectFilled(pos, pos + size, background, rounding);
      if (isActive)
         drawList->AddRect(pos, pos + size, color, rounding, ImDrawFlags_None, 2.f);
      drawList->AddText(pos + padding, color, TAB_NAMES[i]);
   }
   ImGui::PopID();
}


void TablePickerPage::RenderSearch()
{
   // A text box for keyboards, filtering while typing. With buttons, activating the item opens the text entry page instead.
   const ImGuiStyle& style = ImGui::GetStyle();
   ImGui::PushID(SEARCH_ITEM);
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.f)); // Same height as the other items
   ImGui::TextUnformatted(ICON_FK_SEARCH);
   ImGui::SameLine();
   const float clearWidth = s_search.empty() ? 0.f : ImGui::CalcTextSize(ICON_FK_TIMES).x + 2.f * style.FramePadding.x + style.ItemSpacing.x;
   ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearWidth - 2.f * style.ItemSpacing.x);
   string search = s_search;
   if (ImGui::InputTextWithHint("##text", "Search: a few letters of the name", &search) && search != s_search)
      SetSearch(search);
   // In VR there is no keyboard: activating the field opens a virtual one under it
   if (ImGui::IsItemActivated() && m_player->m_vrDevice)
      m_virtualKeyboard = true;
   if (!s_search.empty())
   {
      ImGui::SameLine();
      if (ImGui::Button(ICON_FK_TIMES))
         SetSearch(string());
   }
   ImGui::PopStyleVar();
   if (m_virtualKeyboard)
   {
      // Keys add to the search (not to the text field, which loses the focus when a key is clicked), which filters the list at once
      string search = s_search;
      if (RenderVirtualKeyboard(search, 64, false, true))
         m_virtualKeyboard = false;
      if (search != s_search)
         SetSearch(search);
   }
   ImGui::PopID();
}

bool RenderVirtualKeyboard(string& text, size_t maxLength, bool capitalizeWords, bool withDone)
{
   static constexpr const char* rows[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-&." };
   const bool upperCase = capitalizeWords && (text.empty() || text.back() == ' ');
   bool done = false;
   const ImGuiStyle& style = ImGui::GetStyle();
   const float spacing = style.ItemSpacing.x;
   const float keyWidth = (ImGui::GetContentRegionAvail().x - 9.f * spacing) / 10.f;
   const float keyHeight = 1.5f * ImGui::GetFrameHeight();
   ImGui::PushID("VirtualKeyboard");
   ImGui::Dummy(ImVec2(0.f, 0.5f * keyHeight));
   for (const char* row : rows)
   {
      for (const char* key = row; *key; key++)
      {
         if (key != row)
            ImGui::SameLine(0.f, spacing);
         const char c = upperCase ? *key : static_cast<char>(std::tolower(static_cast<unsigned char>(*key)));
         const char label[2] = { c, '\0' };
         ImGui::PushID(*key);
         if (ImGui::Button(label, ImVec2(keyWidth, keyHeight)) && text.size() < maxLength)
            text += c;
         ImGui::PopID();
      }
   }
   // Last row: space over 4 keys (6 without done), then delete, clear and done over 2 keys each
   const float wideKey = 2.f * keyWidth + spacing;
   if (ImGui::Button("Space", ImVec2(withDone ? 2.f * wideKey + spacing : 3.f * wideKey + 2.f * spacing, keyHeight)) && text.size() < maxLength)
      text += ' ';
   ImGui::SameLine(0.f, spacing);
   if (ImGui::Button(ICON_FK_ARROW_LEFT " Delete", ImVec2(wideKey, keyHeight)) && !text.empty())
   {
      // A whole UTF-8 character
      while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
         text.pop_back();
      if (!text.empty())
         text.pop_back();
   }
   ImGui::SameLine(0.f, spacing);
   if (ImGui::Button("Clear", ImVec2(wideKey, keyHeight)))
      text.clear();
   if (withDone)
   {
      ImGui::SameLine(0.f, spacing);
      done = ImGui::Button(ICON_FK_CHECK " Done", ImVec2(wideKey, keyHeight));
   }
   ImGui::PopID();
   return done;
}

void TablePickerPage::RenderPager(const char* item)
{
   ImGui::PushID(item);
   ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.f));
   if (ImGui::Button(ICON_FK_ANGLE_LEFT))
      SetPage((s_page + m_pageCount - 1) % m_pageCount, item);
   ImGui::SameLine();
   ImGui::Text("Page %d of %d", s_page + 1, m_pageCount);
   ImGui::SameLine();
   if (ImGui::Button(ICON_FK_ANGLE_RIGHT))
      SetPage((s_page + 1) % m_pageCount, item);
   ImGui::PopStyleVar();
   ImGui::PopID();
}

void TablePickerPage::BuildPage()
{
   TableLibrary& library = g_launcher->GetTableLibrary();
   m_revision = library.GetRevision();
   m_scanning = library.IsScanning();
   // The new VR menu shows the scores and the options on screens of their own
   if (m_newView && (s_tab == PickerTab::Scores || s_tab == PickerTab::Menu))
      s_tab = PickerTab::All;
   m_scoresRevision = g_launcher->GetScoreStore().GetRevision();
   const bool gridView = g_settingsService.GetAppSettings().GetStandalone_TablePickerGridView();
   const bool sortAscending = g_settingsService.GetAppSettings().GetStandalone_TablePickerSortAscending();
   vector<LibraryTable> tables = library.GetTables(s_tab != PickerTab::All || sortAscending);
   const size_t tableCount = tables.size();

   // Names are displayed, and identify the menu items: tables sharing a name are told apart by their folder
   ankerl::unordered_dense::map<string, int> nameCounts;
   for (const LibraryTable& table : tables)
      nameCounts[table.name]++;
   const std::filesystem::path runningTable = m_player->m_isLobby ? std::filesystem::path() : m_player->m_ptable->m_filename.lexically_normal(); // See IsRunningTable

   // The tab selects and orders the tables
   constexpr size_t filterMinTables = 9; // With a few tables, going through the list is quicker than selecting a letter
   const bool hasLetterFilter = s_tab == PickerTab::All && tableCount >= filterMinTables;
   if (!hasLetterFilter || std::ranges::none_of(tables, [](const LibraryTable& table) { return GetInitial(table) == s_filterInitial; }))
      s_filterInitial = 0;
   const char* emptyMessage = "";
   switch (s_tab)
   {
   case PickerTab::All:
      std::erase_if(tables, [](const LibraryTable& table) { return s_filterInitial != 0 && GetInitial(table) != s_filterInitial; });
      emptyMessage = m_newView ? "No table yet: copy table folders to the tables folder, or add them from a browser (see Wi-Fi upload)"
                               : "No table yet: copy table folders to the tables folder, or add them from a browser (see MENU)";
      break;
   case PickerTab::Recent:
      std::erase_if(tables, [](const LibraryTable& table) { return table.lastPlayedAt == 0; });
      std::ranges::stable_sort(tables, [](const LibraryTable& a, const LibraryTable& b) { return a.lastPlayedAt > b.lastPlayedAt; });
      emptyMessage = "No table was played yet";
      break;
   case PickerTab::NewlyAdded:
      std::ranges::stable_sort(tables, [](const LibraryTable& a, const LibraryTable& b) { return a.createdAt > b.createdAt; });
      emptyMessage = "No table yet";
      break;
   case PickerTab::MostPlayed:
      std::erase_if(tables, [](const LibraryTable& table) { return table.playCount == 0; });
      std::ranges::stable_sort(tables, [](const LibraryTable& a, const LibraryTable& b) { return a.playCount != b.playCount ? a.playCount > b.playCount : a.lastPlayedAt > b.lastPlayedAt; });
      emptyMessage = "No table was played yet";
      break;
   case PickerTab::Favorites:
      std::erase_if(tables, [](const LibraryTable& table) { return !table.favorite; });
      emptyMessage = "No favorite yet: use the star of a table, or 'Add to favorites' in its page";
      break;
   case PickerTab::Scores:
   case PickerTab::Menu:
      break;
   }

   // Fuzzy search on the name. In the 'All' tab the best matches come first, the other tabs keep their order.
   if (!s_search.empty())
   {
      vector<std::pair<int, LibraryTable>> matches;
      for (LibraryTable& table : tables)
         if (const std::optional<int> score = TableLibrary::FuzzyScore(s_search, table.name); score)
            matches.emplace_back(*score, std::move(table));
      if (s_tab == PickerTab::All)
         std::ranges::stable_sort(matches, [](const auto& a, const auto& b) { return a.first > b.first; });
      tables.clear();
      for (auto& [score, table] : matches)
         tables.push_back(std::move(table));
      emptyMessage = "No table matches the search";
   }

   // Pages: a short list is quicker to go through with buttons, and only the images of the page are kept in memory
   const size_t matchCount = tables.size();
   m_pageCount = max(1, static_cast<int>((matchCount + TABLES_PER_PAGE - 1) / TABLES_PER_PAGE));
   s_page = clamp(s_page, 0, m_pageCount - 1);
   if (matchCount > TABLES_PER_PAGE)
   {
      const size_t first = static_cast<size_t>(s_page) * TABLES_PER_PAGE;
      tables = vector<LibraryTable>(tables.begin() + static_cast<std::ptrdiff_t>(first), tables.begin() + static_cast<std::ptrdiff_t>(min(first + TABLES_PER_PAGE, matchCount)));
   }

   AddItem(std::make_unique<InGameUIItem>(TABS_ITEM, "Left/Right: previous/next list"s, [this](int, const InGameUIItem*) { m_newView ? RenderTabsNewView() : RenderTabs(); }))
      .m_customHighlight = true;
   if (s_tab == PickerTab::Menu)
   {
      KeepThumbnails({});
      BuildMenuTab();
      return;
   }
   // Who is playing, for the leaderboards (on the Scores screen of the new VR menu)
   if (!m_newView)
      AddPlayerItem(*this);
   if (s_tab == PickerTab::Scores)
   {
      KeepThumbnails({});
      BuildLeaderboardList(*this);
      return;
   }
   AddItem(std::make_unique<InGameUIItem>(SEARCH_ITEM, "Type a few letters of the name of a table. Without a keyboard, Left/Right open a text entry page."s, [this](int, const InGameUIItem*) { m_newView ? RenderSearchNewView() : RenderSearch(); }));

   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header,
      m_scanning                   ? "Scanning tables folder..."s
         : matchCount != tableCount ? std::format("{} of {} tables", matchCount, tableCount)
                                    : std::format("{} table{}", tableCount, tableCount == 1 ? "" : "s")));

   if (hasLetterFilter)
   {
      const string filter = s_filterInitial == 0 ? "All tables"s : s_filterInitial == '#' ? "0-9 and others"s : string(1, s_filterInitial);
      if (m_newView)
      {
         // A row with previous/next arrows: left/right change the letter (an action row would get back to the rail of the menu)
         AddItem(std::make_unique<InGameUIItem>(FILTER_LABEL + filter, "Left/Right: only show the tables starting with the previous/next letter"s,
            [this, filter](int, const InGameUIItem*)
            {
               namespace Theme = VPX::VRUI::Theme;
               const float height = ImGui::GetFrameHeight();
               const ImVec2 pos = ImGui::GetCursorScreenPos();
               const float width = ImGui::GetContentRegionAvail().x;
               const InGameUIItem* const selected = GetSelectedItem();
               const bool hasFocus = m_player->m_liveUI->m_inGameUI.IsFlipperNav() && selected != nullptr && selected->m_label.starts_with(FILTER_LABEL);
               ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f), Theme::kText, "Starting with");
               ImGui::SetCursorScreenPos(ImVec2(pos.x + width * 0.42f, pos.y));
               if (const int direction = VPX::VRUI::Widgets::Stepper("##filter", filter.c_str(), ImVec2(width * 0.58f - Theme::Px(2.2f), height), hasFocus); direction != 0)
                  StepFilter(direction);
               ImGui::SetCursorScreenPos(pos);
               ImGui::Dummy(ImVec2(width, height));
            }));
      }
      else
         AddItem(std::make_unique<InGameUIItem>(FILTER_LABEL + filter, "Left/Right: only show the tables starting with the previous/next letter"s,
            []() { /* Handled by AdjustItem as it depends on the direction */ }));
   }

   if (m_pageCount > 1)
      AddItem(std::make_unique<InGameUIItem>(PAGER_TOP_ITEM, "Left/Right: previous/next page"s, [this](int, const InGameUIItem*) { m_newView ? RenderPagerNewView(PAGER_TOP_ITEM) : RenderPager(PAGER_TOP_ITEM); }));

   if (tables.empty() && !m_scanning)
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, emptyMessage));

   ankerl::unordered_dense::set<string> displayedTables;
   for (const LibraryTable& table : tables)
   {
      string fullName = table.name;
      if (nameCounts[table.name] > 1)
         fullName += " [" + table.path + ']';
      string label = fullName;
      if (runningTable == library.GetFullPath(table).lexically_normal())
         label += " (playing)";
      if (!gridView && table.favorite)
         label = ICON_FK_STAR " "s + label;
      const string path = "tables/" + table.uuid;
      const string uuid = table.uuid;
      displayedTables.insert(uuid);
      m_player->m_liveUI->m_inGameUI.AddPage(path, [uuid]() { return std::make_unique<TableActionsPage>(uuid); });
      InGameUIItem& item = AddItem(std::make_unique<InGameUIItem>(label, fullName + '\n' + GetTableStatsInfo(table), path));
      if (gridView)
      {
         item.m_tileImage = [this, table]() { return GetThumbnail(table); };
         item.m_tileLoading = [this, table]() { return IsThumbnailLoading(table); };
         item.m_tileToggleIconOn = ICON_FK_STAR;
         item.m_tileToggleIconOff = ICON_FK_STAR_O;
         item.m_tileToggleState = [favorite = table.favorite]() { return favorite; };
         item.m_tileToggleAction = [uuid, favorite = table.favorite]() { g_launcher->GetTableLibrary().SetFavorite(uuid, !favorite); }; // The page is rebuilt as the library changes
      }
   }

   if (m_pageCount > 1)
      AddItem(std::make_unique<InGameUIItem>(PAGER_BOTTOM_ITEM, "Left/Right: previous/next page"s, [this](int, const InGameUIItem*) { m_newView ? RenderPagerNewView(PAGER_BOTTOM_ITEM) : RenderPager(PAGER_BOTTOM_ITEM); }));

   KeepThumbnails(displayedTables);
}

// Only keep the images of the displayed page
void TablePickerPage::KeepThumbnails(const ankerl::unordered_dense::set<string>& displayedTables)
{
   for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();)
   {
      if (displayedTables.contains(it->first))
         ++it;
      else
      {
         if (it->second.texture)
            m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(it->second.texture.get());
         if (it->second.loading.valid())
            m_abandonedLoads.push_back(std::move(it->second.loading));
         it = m_thumbnails.erase(it);
      }
   }
}

// The MENU tab: the options of the library and of the application, which are not needed while choosing a table
void TablePickerPage::BuildMenuTab()
{
   AddLibraryItems(*this);
   AddLobbyRoomItems(*this);

   // In launcher mode, this page is the front door of the application: give access to the rest of the menu from it
   if (g_launcher->m_launcherMode)
      AddItem(std::make_unique<InGameUIItem>("Settings"s, "Controls, VR, graphics, sound and the other settings"s, "homepage"s));

   if (g_launcher->m_launcherMode)
      AddQuitItem(*this);

#ifdef VPX_TABLE_WEBSERVER
   AddWebServerItems(*this);
#endif
}

void AddLibraryItems(InGameUIPage& page)
{
   TableLibrary& library = g_launcher->GetTableLibrary();
   const bool gridView = g_settingsService.GetAppSettings().GetStandalone_TablePickerGridView();
   const bool sortAscending = g_settingsService.GetAppSettings().GetStandalone_TablePickerSortAscending();

   page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Library"s));

   page.AddItem(std::make_unique<InGameUIItem>(gridView ? "View: Grid"s : "View: List"s, "Switch between a grid of table images and a list of names"s,
      [&page, gridView]()
      {
         g_settingsService.GetAppSettings().SetStandalone_TablePickerGridView(!gridView, false);
         page.RequestRebuild();
      }));

   page.AddItem(std::make_unique<InGameUIItem>(Launcher::GetTableImageFocusLabel(), "What the images of the tables captured in VR show: their backglass, their playfield seen from above, or their whole cabinet"s,
      [&page]()
      {
         Launcher::NextTableImageFocus();
         page.RequestRebuild();
      }));

   AddMenuDistanceItem(page);

   page.AddItem(std::make_unique<InGameUIItem>(sortAscending ? "Sort: A to Z"s : "Sort: Z to A"s, "Change the sort order of the 'All' list"s,
      [&page, sortAscending]()
      {
         g_settingsService.GetAppSettings().SetStandalone_TablePickerSortAscending(!sortAscending, false);
         page.RequestRebuild();
      }));

   page.AddItem(std::make_unique<InGameUIItem>("Rescan tables folder"s, "Look for tables added to, or removed from, the tables folder"s, []() { g_launcher->GetTableLibrary().RescanAsync(); }));

   page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "Tables folder: " + library.GetTablesPath().string()));
   if (g_launcher->IsSharedPinMAMEFolderUsed())
      page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "ROMs (zipped): 'pinmame/roms' in the tables folder, or in a table's folder"s));
}

// Distance of the menu window in VR, changed by steps of 10cm, the window being placed again at once
void AddMenuDistanceItem(InGameUIPage& page)
{
   if (page.m_player->m_vrDevice == nullptr)
      return;
   page.AddItem(std::make_unique<InGameUIItem>("Menu distance"s, "How far from the player the menu window is placed in VR"s,
      [&page](int index, const InGameUIItem*)
      {
         const float distance = g_settingsService.GetAppSettings().GetStandalone_VRMenuDistance();
         const auto setDistance = [&page](float value)
         {
            g_settingsService.GetAppSettings().SetStandalone_VRMenuDistance(clamp(roundf(value * 10.f) / 10.f, 0.3f, 3.f), false);
            #ifdef ENABLE_XR
            page.m_player->m_vrDevice->RecenterUIPanel();
            #endif
         };
         ImGui::AlignTextToFramePadding();
         ImGui::Text("Menu distance: %.1f m", distance);
         const float buttonWidth = ImGui::GetFrameHeight() * 1.6f;
         ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x * 3.f);
         ImGui::BeginDisabled(distance <= 0.3f + 1e-3f);
         if (ImGui::Button(std::format("{}##MenuCloser{}", ICON_FK_MINUS, index).c_str(), ImVec2(buttonWidth, 0.f)))
            setDistance(distance - 0.1f);
         ImGui::EndDisabled();
         ImGui::SameLine();
         ImGui::BeginDisabled(distance >= 3.f - 1e-3f);
         if (ImGui::Button(std::format("{}##MenuFarther{}", ICON_FK_PLUS, index).c_str(), ImVec2(buttonWidth, 0.f)))
            setDistance(distance + 0.1f);
         ImGui::EndDisabled();
      }));
}

// The lobby may use the room of a table, chosen from the menu of that table while it is played
void AddLobbyRoomItems(InGameUIPage& page)
{
   if (!g_launcher->m_launcherMode)
      return;
   if (const std::optional<Launcher::LobbyRoom> room = g_launcher->GetLobbyRoom(); room)
   {
      page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "Lobby room: the room of " + room->tableName));
      page.AddItem(std::make_unique<InGameUIItem>("Use the default lobby room"s, "Show the table picker in the default room again"s,
         [&page]()
         {
            g_launcher->ResetLobbyRoom();
            if (page.m_player->m_isLobby)
            {
               // Load the lobby again, with the default room
               g_launcher->m_reloadLobby = true;
               page.m_player->SetCloseState(Player::CS_CLOSE_APP);
            }
            else
               page.RequestRebuild();
         }));
   }
   else
      page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "Lobby room: default (the room of a table can be used, from the menu of that table)"s));
}

void AddQuitItem(InGameUIPage& page)
{
   page.AddItem(std::make_unique<InGameUIItem>("Quit Visual Pinball"s, ""s,
      [&page]()
      {
         g_launcher->m_launcherMode = false;
         page.m_player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
      }));
}

// New VR menu: the tabs as pills, then the view (grid or list) and sort order as icon buttons on the right
void TablePickerPage::RenderTabsNewView()
{
   namespace Theme = VPX::VRUI::Theme;
   namespace Widgets = VPX::VRUI::Widgets;
   const float height = Theme::Px(2.9f);
   const float gap = Theme::Px(0.5f);
   const float left = ImGui::GetCursorScreenPos().x;
   const float right = left + ImGui::GetContentRegionAvail().x;
   const float y = ImGui::GetCursorScreenPos().y;
   const InGameUIItem* const selectedItem = GetSelectedItem();
   const bool hasFocus = m_player->m_liveUI->m_inGameUI.IsFlipperNav() && selectedItem != nullptr && selectedItem->m_label == TABS_ITEM;
   ImGui::PushID(TABS_ITEM);
   float x = left;
   for (int i = 0; i < static_cast<int>(std::size(NEW_VIEW_TABS)); i++)
   {
      const bool isActive = s_tab == NEW_VIEW_TABS[i];
      ImGui::SetCursorScreenPos(ImVec2(x, y));
      if (Widgets::Pill(NEW_VIEW_TAB_NAMES[i], nullptr, NEW_VIEW_TAB_NAMES[i], height, true, hasFocus && isActive, isActive))
         SetTab(static_cast<int>(NEW_VIEW_TABS[i]));
      x += Widgets::PillWidth(nullptr, NEW_VIEW_TAB_NAMES[i], height) + gap;
   }
   const bool gridView = g_settingsService.GetAppSettings().GetStandalone_TablePickerGridView();
   const bool sortAscending = g_settingsService.GetAppSettings().GetStandalone_TablePickerSortAscending();
   const char* const viewIcon = gridView ? ICON_FK_LIST : ICON_FK_TH_LARGE;
   const char* const sortIcon = sortAscending ? ICON_FK_SORT_ALPHA_DESC : ICON_FK_SORT_ALPHA_ASC;
   const float sortWidth = Widgets::PillWidth(sortIcon, nullptr, height);
   const float viewWidth = Widgets::PillWidth(viewIcon, nullptr, height);
   ImGui::SetCursorScreenPos(ImVec2(right - sortWidth, y));
   if (Widgets::Pill("##sort", sortIcon, nullptr, height, true, false))
   {
      g_settingsService.GetAppSettings().SetStandalone_TablePickerSortAscending(!sortAscending, false);
      RequestRebuild();
   }
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip(sortAscending ? "Sort from Z to A" : "Sort from A to Z");
   ImGui::SetCursorScreenPos(ImVec2(right - sortWidth - gap - viewWidth, y));
   if (Widgets::Pill("##view", viewIcon, nullptr, height, true, false))
   {
      g_settingsService.GetAppSettings().SetStandalone_TablePickerGridView(!gridView, false);
      RequestRebuild();
   }
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip(gridView ? "Show a list of names" : "Show a grid of table images");
   ImGui::PopID();
   ImGui::SetCursorScreenPos(ImVec2(left, y));
   ImGui::Dummy(ImVec2(right - left, height));
}

// New VR menu: the search field. In VR (no keyboard), it opens the keyboard of the menu, which filters the list at each key.
void TablePickerPage::RenderSearchNewView()
{
   namespace Theme = VPX::VRUI::Theme;
   namespace Widgets = VPX::VRUI::Widgets;
   namespace Keyboard = VPX::VRUI::Keyboard;
   const float height = Theme::Px(2.9f);
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   const float width = ImGui::GetContentRegionAvail().x;
   const float clearWidth = s_search.empty() ? 0.f : height * 1.3f;
   const float rounding = Theme::Px(0.45f);
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float iconWidth = ImGui::CalcTextSize(ICON_FK_SEARCH).x;
   const float pad = Theme::Px(0.7f);
   const float textY = pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
   ImGui::PushID(SEARCH_ITEM);
   if (Keyboard::IsUsed())
   {
      static const string owner = "tables/search"s;
      Keyboard::KeepOpen(owner);
      const bool editing = Keyboard::IsEditing(owner);
      const ImVec2 fieldSize(width - clearWidth, height);
      if (ImGui::InvisibleButton("##field", fieldSize))
         OpenSearchKeyboard();
      const bool hovered = ImGui::IsItemHovered();
      drawList->AddRectFilled(pos, pos + fieldSize, hovered ? Theme::kSurfaceHover : Theme::kSurface, rounding);
      if (editing)
         drawList->AddRect(pos, pos + fieldSize, Theme::kAccent, rounding, ImDrawFlags_None, Theme::Px(0.15f));
      drawList->AddText(ImVec2(pos.x + pad, textY), Theme::kTextMuted, ICON_FK_SEARCH);
      ImGui::SetCursorScreenPos(ImVec2(pos.x + 2.f * pad + iconWidth, textY));
      Widgets::TextEllipsis(s_search.empty() ? "Search: a few letters of the name" : s_search.c_str(), fieldSize.x - 3.f * pad - iconWidth,
         s_search.empty() ? Theme::kTextDisabled : Theme::kText);
   }
   else
   {
      // A keyboard: a text box, filtering while typing
      ImGui::SetCursorScreenPos(pos);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f * pad + iconWidth, (height - ImGui::GetTextLineHeight()) * 0.5f));
      ImGui::SetNextItemWidth(width - clearWidth);
      string search = s_search;
      if (ImGui::InputTextWithHint("##text", "Search: a few letters of the name", &search) && search != s_search)
         SetSearch(search);
      ImGui::PopStyleVar();
      drawList->AddText(ImVec2(pos.x + pad, textY), Theme::kTextMuted, ICON_FK_SEARCH);
   }
   if (!s_search.empty())
   {
      ImGui::SetCursorScreenPos(ImVec2(pos.x + width - clearWidth + Theme::Px(0.4f), pos.y));
      if (Widgets::Pill("##clear", ICON_FK_TIMES, nullptr, height, true, false))
      {
         SetSearch(string());
         if (Keyboard::IsEditing("tables/search"s))
            Keyboard::Close();
      }
   }
   ImGui::PopID();
   ImGui::SetCursorScreenPos(pos);
   ImGui::Dummy(ImVec2(width, height));
}

void TablePickerPage::OpenSearchKeyboard()
{
   VPX::VRUI::Keyboard::Open("tables/search"s, "Search"s, s_search, 64, [this](const string& search) { SetSearch(search); });
}

void TablePickerPage::RenderPagerNewView(const char* item)
{
   namespace Theme = VPX::VRUI::Theme;
   namespace Widgets = VPX::VRUI::Widgets;
   const float height = Theme::Px(2.6f);
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   const float width = ImGui::GetContentRegionAvail().x;
   const string label = std::format("Page {} of {}", s_page + 1, m_pageCount);
   const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
   const float buttonWidth = Widgets::PillWidth(ICON_FK_ANGLE_LEFT, nullptr, height);
   const float total = 2.f * buttonWidth + labelWidth + 2.f * Theme::Px(1.f);
   const float x = pos.x + (width - total) * 0.5f;
   ImGui::PushID(item);
   ImGui::SetCursorScreenPos(ImVec2(x, pos.y));
   if (Widgets::Pill("##prev", ICON_FK_ANGLE_LEFT, nullptr, height, true, false))
      SetPage((s_page + m_pageCount - 1) % m_pageCount, item);
   ImGui::GetWindowDrawList()->AddText(ImVec2(x + buttonWidth + Theme::Px(1.f), pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f), Theme::kTextMuted, label.c_str());
   ImGui::SetCursorScreenPos(ImVec2(x + buttonWidth + labelWidth + 2.f * Theme::Px(1.f), pos.y));
   if (Widgets::Pill("##next", ICON_FK_ANGLE_RIGHT, nullptr, height, true, false))
      SetPage((s_page + 1) % m_pageCount, item);
   ImGui::PopID();
   ImGui::SetCursorScreenPos(pos);
   ImGui::Dummy(ImVec2(width, height));
}

#ifdef VPX_TABLE_WEBSERVER
// Address and pairing code displayed by the items, to rebuild them when they change (the pairing code is renewed after failed attempts)
static string s_webServerUrl;
static string s_pairingCode;

bool HaveWebServerItemsChanged()
{
   return s_webServerUrl != g_launcher->GetWebServer().GetUrl() || s_pairingCode != g_launcher->GetWebServer().GetPairingCode();
}

#ifdef VPX_TABLE_WEBSERVER
// The pairing code, with a switch on its right to stop asking for it. With buttons, left/right flip the switch.
static void RenderPairing(InGameUIPage& page, const string& pairingCode)
{
   const bool required = g_launcher->GetWebServer().IsPairingRequired();
   const InGameUIItem* const selectedItem = page.GetSelectedItem();
   const bool hasFocus = page.m_player->m_liveUI->m_inGameUI.IsFlipperNav() && selectedItem != nullptr && selectedItem->m_label == PAIRING_ITEM;
   ImGui::AlignTextToFramePadding();
   ImGui::Text("Pairing code: %s", required ? pairingCode.c_str() : "disabled");
   ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x * 4.f);

   // The switch and its state form a single button, large enough to be hit easily with a VR pointer
   const char* const stateLabel = required ? "Required" : "Off";
   const float height = ImGui::GetFrameHeight();
   const ImVec2 trackSize(height * 1.8f, height);
   const float labelSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
   const ImVec2 pos = ImGui::GetCursorScreenPos();
   ImGui::PushID(PAIRING_ITEM);
   if (ImGui::InvisibleButton("switch", ImVec2(trackSize.x + labelSpacing + ImGui::CalcTextSize(stateLabel).x, height)))
      SetWebServerPairing(!required);
   ImGui::PopID();
   const bool isHovered = ImGui::IsItemHovered() || hasFocus;
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const float radius = height * 0.5f;
   drawList->AddRectFilled(pos, pos + trackSize, required ? IM_COL32(0, 160, 0, 255) : IM_COL32(110, 110, 110, 255), radius);
   if (isHovered)
      drawList->AddRect(pos, pos + trackSize, IM_COL32(0, 255, 0, 255), radius, ImDrawFlags_None, 2.f);
   drawList->AddCircleFilled(pos + ImVec2(required ? trackSize.x - radius : radius, radius), radius - 3.f, IM_COL32_WHITE);
   drawList->AddText(pos + ImVec2(trackSize.x + labelSpacing, (height - ImGui::GetTextLineHeight()) * 0.5f), isHovered ? IM_COL32(0, 255, 0, 255) : IM_COL32_WHITE, stateLabel);
}
#endif

// The web server gives access to the tables folder to the local network, so it only runs on request and asks for a pairing code
void AddWebServerItems(InGameUIPage& page)
{
   page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Add tables from a browser"s));
   WebServer& webServer = g_launcher->GetWebServer();
   s_webServerUrl = webServer.GetUrl();
   s_pairingCode = webServer.GetPairingCode();
   // 3 modes: off, on until the application is closed (to be turned on again at the next launch), or always on (started with the application)
   const bool alwaysOn = g_settingsService.GetAppSettings().GetStandalone_WebServerAlwaysOn();
   page.AddItem(std::make_unique<InGameUIItem>(!webServer.IsRunning() ? "Wi-Fi upload: Off"s : alwaysOn ? "Wi-Fi upload: On (always on)"s : "Wi-Fi upload: On (ask every time)"s,
      "Manage tables from the browser of a phone or computer connected to the same network. Once on, it stays on while tables are played. 'Ask every time' turns it on until the application is closed, 'always on' also when the application starts."s,
      [&page, alwaysOn]()
      {
         WebServer& webServer = g_launcher->GetWebServer();
         // Off -> On (ask every time) -> On (always on) -> Off
         const bool startWithApp = webServer.IsRunning() && !alwaysOn;
         if (webServer.IsRunning() && alwaysOn)
            webServer.Stop();
         else if (!webServer.IsRunning())
            webServer.Start();
         g_settingsService.GetAppSettings().SetStandalone_WebServerAlwaysOn(startWithApp, false);
         g_settingsService.GetAppSettings().Save();
         page.RequestRebuild();
      }));
   if (webServer.IsRunning())
   {
      page.AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, s_webServerUrl.empty() ? "No network address found"s : "Open " + s_webServerUrl));
      page.AddItem(std::make_unique<InGameUIItem>(PAIRING_ITEM,
         "Left/Right: ask, or not, for the pairing code before giving a browser access to the tables folder. Without it, any device on the local network can add, change or delete tables."s,
         [&page](int, const InGameUIItem*) { RenderPairing(page, s_pairingCode); })).m_customHighlight = true;
   }
}

// Left/Right on the pairing item flip it (see AddWebServerItems)
bool AdjustWebServerItem(const InGameUIItem& item)
{
   if (item.m_label != PAIRING_ITEM)
      return false;
   SetWebServerPairing(!g_launcher->GetWebServer().IsPairingRequired());
   return true;
}
#endif

static constexpr const char* CHARACTER_LABEL = "Character: ";
static constexpr std::string_view TEXT_ENTRY_CHARACTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz0123456789-'&!().,";

TextEntryPage::TextEntryPage(const string& title, const string& text, const std::function<void(const string&)>& onSave, bool allowEmpty, size_t maxLength)
   : InGameUIPage(title, VPX::VRUI::IsActive() ? "Type with the keyboard, then press Done"s : "Type on the keyboard, or with buttons: Left/Right select a character, which is then added to the text"s, SaveMode::None)
   , m_text(text)
   , m_onSave(onSave)
   , m_allowEmpty(allowEmpty)
   , m_maxLength(maxLength)
{
}

void TextEntryPage::AdjustItem(float direction, bool isInitialPress)
{
   // The character selector repeats while the button is held, as it is a long list, the other actions do not
   if (const InGameUIItem* item = GetSelectedItem(); item && item->m_label.starts_with(CHARACTER_LABEL))
   {
      static uint32_t lastChangeMs = 0;
      if (!isInitialPress && msec() - lastChangeMs < 150)
         return;
      lastChangeMs = msec();
      const size_t count = TEXT_ENTRY_CHARACTERS.size();
      m_charIndex = (m_charIndex + (direction < 0.f ? count - 1 : 1)) % count;
      RequestRebuild();
      return;
   }
   if (isInitialPress)
      InGameUIPage::AdjustItem(direction, isInitialPress);
}

void TextEntryPage::OpenKeyboard()
{
   VPX::VRUI::Keyboard::Open(std::format("textentry:{}", static_cast<const void*>(this)), ""s, m_text, m_maxLength,
      [this](const string& text)
      {
         m_text = text;
         RequestRebuild();
      },
      // Done saves the text, when it can be saved
      [this]()
      {
         if (CanSave())
            Save();
      });
}

bool TextEntryPage::CanSave() const { return m_allowEmpty || m_text.find_first_not_of(' ') != string::npos; }

void TextEntryPage::Save()
{
   m_onSave(m_text);
   m_player->m_liveUI->m_inGameUI.NavigateBack();
}

void TextEntryPage::BuildPage()
{
   const char c = TEXT_ENTRY_CHARACTERS[m_charIndex];
   const string displayedChar = c == ' ' ? "space"s : string(1, c);
   AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, m_text + '_'));
   AddItem(std::make_unique<InGameUIItem>("##keyboard"s, "Pointer: click the keys"s,
      [this](int, const InGameUIItem*)
      {
         // New VR menu: a field which opens its keyboard
         if (VPX::VRUI::IsActive())
         {
            namespace Theme = VPX::VRUI::Theme;
            namespace Keyboard = VPX::VRUI::Keyboard;
            const string owner = std::format("textentry:{}", static_cast<const void*>(this));
            Keyboard::KeepOpen(owner);
            const float height = ImGui::GetFrameHeight();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, height);
            if (ImGui::InvisibleButton("##field", size) || !m_keyboardOpened)
            {
               m_keyboardOpened = true;
               OpenKeyboard();
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(pos, pos + size, ImGui::IsItemHovered() ? Theme::kSurfaceHover : Theme::kSurface, Theme::Px(0.45f));
            if (Keyboard::IsEditing(owner))
               drawList->AddRect(pos, pos + size, Theme::kAccent, Theme::Px(0.45f), ImDrawFlags_None, Theme::Px(0.15f));
            drawList->AddText(ImVec2(pos.x + Theme::Px(0.7f), pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f), Theme::kTextMuted,
               ICON_FK_KEYBOARD_O "  Type with the keyboard");
            return;
         }
         string text = m_text;
         RenderVirtualKeyboard(text, m_maxLength, true, false);
         if (text != m_text)
         {
            m_text = text;
            RequestRebuild();
         }
      })).m_customHighlight = true;
   const auto addSaveItem = [this]()
   {
      if (CanSave())
         AddItem(std::make_unique<InGameUIItem>("Save"s, ""s, [this]() { Save(); }));
   };

   // New VR menu: its keyboard only (Done saves), without the selection of the characters with the buttons
   if (VPX::VRUI::IsActive())
   {
      addSaveItem();
      return;
   }

   AddItem(std::make_unique<InGameUIItem>(CHARACTER_LABEL + "< "s + displayedChar + " >", "Left/Right: previous/next character"s, []() { /* Handled by AdjustItem as it depends on the direction */ }));
   AddItem(std::make_unique<InGameUIItem>("Add '" + displayedChar + '\'', ""s,
      [this, c]()
      {
         if (m_text.size() < m_maxLength)
            m_text += c;
         // Words usually are capitalized: propose lowercase after a letter, uppercase after a space
         if (c == ' ')
            m_charIndex = TEXT_ENTRY_CHARACTERS.find('A');
         else if (std::isupper(static_cast<unsigned char>(c)))
            m_charIndex = TEXT_ENTRY_CHARACTERS.find(static_cast<char>(std::tolower(c)));
         RequestRebuild();
      }));
   if (!m_text.empty())
      AddItem(std::make_unique<InGameUIItem>("Delete last character"s, ""s,
         [this]()
         {
            // Remove a whole UTF-8 character (existing names may hold some)
            while (!m_text.empty() && (static_cast<unsigned char>(m_text.back()) & 0xC0) == 0x80)
               m_text.pop_back();
            if (!m_text.empty())
               m_text.pop_back();
            RequestRebuild();
         }));
   addSaveItem();
}


static string GetTableTitle(const string& uuid)
{
   const std::optional<LibraryTable> table = g_launcher->GetTableLibrary().GetTable(uuid);
   return table ? table->name : "Table"s;
}

static string GetTableInfo(const string& uuid)
{
   const std::optional<LibraryTable> table = g_launcher->GetTableLibrary().GetTable(uuid);
   if (!table)
      return ""s;
   return table->path + '\n' + GetTableStatsInfo(*table, true);
}

TableActionsPage::TableActionsPage(const string& uuid)
   : InGameUIPage(GetTableTitle(uuid), GetTableInfo(uuid), SaveMode::None)
   , m_uuid(uuid)
{
}

void TableActionsPage::AdjustItem(float direction, bool isInitialPress)
{
   // Actions of this page must not repeat while the button is held
   if (isInitialPress)
      InGameUIPage::AdjustItem(direction, isInitialPress);
}

void TableActionsPage::BuildPage()
{
   TableLibrary& library = g_launcher->GetTableLibrary();
   const std::optional<LibraryTable> table = library.GetTable(m_uuid);
   if (!table)
   {
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Info, "This table is no longer available"s));
      return;
   }
   const bool isRunning = IsRunningTable(m_player, *table);

   if (m_confirmDelete)
   {
      AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, "Delete this table and its files?"s));
      AddItem(std::make_unique<InGameUIItem>("Cancel"s, ""s,
         [this]()
         {
            m_confirmDelete = false;
            RequestRebuild();
         }));
      AddItem(std::make_unique<InGameUIItem>("Delete " + table->name, "This can not be undone"s,
         [this, name = table->name]()
         {
            const bool deleted = g_launcher->GetTableLibrary().Delete(m_uuid);
            m_player->m_liveUI->PushNotification(deleted ? name + " was deleted" : "Failed to delete " + name, 3000);
            m_player->m_liveUI->m_inGameUI.NavigateBack();
         }));
      return;
   }

   AddItem(std::make_unique<InGameUIItem>(isRunning ? "Restart"s : "Play"s, ""s,
      [this, tablePath = library.GetFullPath(*table)]()
      {
         g_launcher->m_nextTableFilename = tablePath;
         m_player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
      }));

   AddItem(std::make_unique<InGameUIItem>(table->favorite ? ICON_FK_STAR " Remove from favorites"s : ICON_FK_STAR_O " Add to favorites"s, "Favorites have their own list in the table picker"s,
      [this, favorite = table->favorite]()
      {
         g_launcher->GetTableLibrary().SetFavorite(m_uuid, !favorite);
         RequestRebuild();
      }));

   AddItem(std::make_unique<InGameUIItem>("Rename..."s, "Change the name displayed for this table (its files are not renamed)"s,
      [this, name = table->name]()
      {
         const string uuid = m_uuid;
         m_player->m_liveUI->m_inGameUI.AddPage("tables/rename"s,
            [uuid, name]() { return std::make_unique<TextEntryPage>("Rename table"s, name, [uuid](const string& newName) { g_launcher->GetTableLibrary().Rename(uuid, newName); }); });
         m_player->m_liveUI->m_inGameUI.Navigate("tables/rename"s);
      }));

   // The running table holds its files and saves its settings when closing
   if (!isRunning)
   {
      if (FileExists(library.GetIniPath(*table)))
         AddItem(std::make_unique<InGameUIItem>("Reset table settings"s, "Remove the settings saved for this table (its .ini file)"s,
            [this]()
            {
               const bool reset = g_launcher->GetTableLibrary().ResetIni(m_uuid);
               m_player->m_liveUI->PushNotification(reset ? "Table settings were reset"s : "Failed to reset table settings"s, 3000);
               RequestRebuild();
            }));

      AddItem(std::make_unique<InGameUIItem>("Delete..."s, "Remove this table from the device"s,
         [this]()
         {
            m_confirmDelete = true;
            RequestRebuild();
         }));
   }

   // The scores to beat, before playing
   AddTopScores(*this, m_uuid, 10);
}

}

#endif
