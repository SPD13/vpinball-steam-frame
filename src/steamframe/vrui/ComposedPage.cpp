// license:GPLv3+

#include "core/stdafx.h"
#include "ComposedPage.h"

#include "PageView.h"
#include "Shell.h"
#include "Theme.h"
#include "Widgets.h"

#include "ui/live/LiveUI.h"

#include "unordered_dense.h"

namespace VPX::VRUI
{

using namespace VPX::InGameUI;

namespace
{
   ankerl::unordered_dense::map<string, vector<ItemKey>> g_claims;

   int GetPropIndex(const InGameUIItem& item)
   {
      if (item.m_type != InGameUIItem::Type::Property || item.m_property == nullptr)
         return -1;
      const auto id = Settings::GetRegistry().GetPropertyId(item.m_property->m_groupId, item.m_property->m_propId);
      return id.has_value() ? static_cast<int>(id.value().index) : -1;
   }

   bool Matches(const InGameUIItem& item, const vector<ItemKey>& keys)
   {
      const int propIndex = GetPropIndex(item);
      for (const ItemKey& key : keys)
         if (key.propIndex >= 0 ? key.propIndex == propIndex : (!key.label.empty() && key.label == item.m_label))
            return true;
      return false;
   }

   bool IsHeader(const InGameUIItem& item) { return item.m_type == InGameUIItem::Type::Label && item.m_labelType == InGameUIItem::LabelType::Header; }

   bool IsCoreType(InGameUIItem::Type type)
   {
      using enum InGameUIItem::Type;
      return type == Back || type == ResetToDefaults || type == ResetToStoredValues || type == SaveChanges;
   }

   InGameUIPage::SaveMode ComputeSaveMode(const vector<SourcePage>& sources)
   {
      bool global = false, table = false;
      for (const SourcePage& source : sources)
      {
         switch (InGameUIPageView::GetSaveMode(*source.page))
         {
         case InGameUIPage::SaveMode::None: break;
         case InGameUIPage::SaveMode::Global: global = true; break;
         case InGameUIPage::SaveMode::Table: table = true; break;
         case InGameUIPage::SaveMode::Both: global = table = true; break;
         }
      }
      return global && table ? InGameUIPage::SaveMode::Both : global ? InGameUIPage::SaveMode::Global : table ? InGameUIPage::SaveMode::Table : InGameUIPage::SaveMode::None;
   }

   string DescribeItem(const InGameUIItem& item)
   {
      if (item.m_type == InGameUIItem::Type::Property && item.m_property)
         return item.m_property->m_groupId + '.' + item.m_property->m_propId + " (" + item.m_property->m_label + ')';
      return '\'' + item.m_label + '\'';
   }
}

ItemKey Prop(const VPX::Properties::PropertyRegistry::PropId& id) { return ItemKey { static_cast<int>(id.index), string() }; }

ItemKey Label(const string& label) { return ItemKey { -1, label }; }

void ClaimItems(const string& source, const vector<ItemKey>& keys)
{
   vector<ItemKey>& claims = g_claims[source];
   claims.insert(claims.end(), keys.begin(), keys.end());
}

ComposedPage::ComposedPage(const string& title, const string& info, vector<SourcePage> sources, vector<Section> sections)
   : InGameUIPage(title, info, ComputeSaveMode(sources))
   , m_sources(std::move(sources))
   , m_sections(std::move(sections))
{
   // Sections of a page which is not available (for example VR settings outside VR) are dropped
   std::erase_if(m_sections, [this](const Section& section) { return GetSource(section.source) == nullptr; });
}

InGameUIPage* ComposedPage::GetSource(const string& id) const
{
   for (const SourcePage& source : m_sources)
      if (source.id == id)
         return source.page.get();
   return nullptr;
}

bool ComposedPage::IsShown(const Section& section, const InGameUIPage& page, int index) const
{
   const auto& items = InGameUIPageView::GetItems(page);
   const InGameUIItem& item = *items[index];
   if (IsCoreType(item.m_type))
      return false;
   if (!section.rest)
      return !IsHeader(item) && Matches(item, section.keys);
   if (IsHeader(item))
   {
      // Shown when an item of its part of the page is
      for (int i = index + 1; i < static_cast<int>(items.size()) && !IsHeader(*items[i]); i++)
         if (IsShown(section, page, i))
            return true;
      return false;
   }
   const auto claims = g_claims.find(section.source);
   return claims == g_claims.end() || !Matches(item, claims->second);
}

void ComposedPage::ForEachShownItem(const std::function<void(InGameUIPage&, InGameUIItem&)>& func) const
{
   for (const Section& section : m_sections)
   {
      InGameUIPage* const page = GetSource(section.source);
      // Index based, as a change may rebuild the page (the rebuild happens when the page is drawn, so the items stay valid until then)
      for (int i = 0; i < static_cast<int>(InGameUIPageView::GetItems(*page).size()); i++)
         if (IsShown(section, *page, i) && !IsHeader(*InGameUIPageView::GetItems(*page)[i]))
            func(*page, *InGameUIPageView::GetItems(*page)[i]);
   }
}

void ComposedPage::Open(bool isBackwardAnimation)
{
   InGameUIPage::Open(isBackwardAnimation);
   for (const SourcePage& source : m_sources)
      source.page->Open(isBackwardAnimation);
   m_selected = { -2, 0 };
}

void ComposedPage::Close(bool isBackwardAnimation)
{
   InGameUIPage::Close(isBackwardAnimation);
   for (const SourcePage& source : m_sources)
      if (source.page->IsActive())
         source.page->Close(isBackwardAnimation);
}

void ComposedPage::Render(float elapsedS)
{
   InGameUIPage::Render(elapsedS);

   static const bool dumpUnplaced = getenv("VPX_VRUI_DUMP_UNPLACED") != nullptr;
   static ankerl::unordered_dense::set<string> dumped;
   vector<NavEntry> nav;
   for (int s = 0; s < static_cast<int>(m_sections.size()); s++)
   {
      const Section& section = m_sections[s];
      InGameUIPage* const page = GetSource(section.source);
      const std::function<bool(int)> filter = [this, &section, page](int index) { return IsShown(section, *page, index); };

      // The header of the section, if it shows anything (a page builds its items when drawn: the first time, it is drawn to know)
      bool hasItems = false;
      for (int i = 0; i < static_cast<int>(InGameUIPageView::GetItems(*page).size()) && !hasItems; i++)
         hasItems = filter(i);
      if (!hasItems && !InGameUIPageView::NeedsRebuild(*page))
         continue;
      if (hasItems && !section.title.empty())
         Widgets::SectionHeader(section.title.c_str(), ImGui::GetContentRegionAvail().x);

      // Only the section of the selected item shows a selection
      const int selected = m_selected.section == s ? m_selected.index : -1;
      InGameUIPageView::SetSelectedIndex(*page, selected);
      Shell::RenderSection(*page, filter, elapsedS);
      // A click selects the clicked item (and adjusts it)
      if (const int clicked = InGameUIPageView::GetSelectedIndex(*page); clicked != selected && clicked >= 0)
         m_selected = { s, clicked };

      const auto& items = InGameUIPageView::GetItems(*page);
      for (int i = 0; i < static_cast<int>(items.size()); i++)
      {
         if (!filter(i))
            continue;
         if (items[i]->IsSelectable())
            nav.push_back({ s, i });
         // Logged for the pages whose items are grouped by sections: an option added upstream which is not grouped yet
         if (dumpUnplaced && section.rest && g_claims.contains(section.source) && !IsHeader(*items[i]) && dumped.insert(section.source + ':' + DescribeItem(*items[i])).second)
            PLOGI << "VRUI unplaced item of '" << section.source << "', shown in the '" << (section.title.empty() ? GetTitle() : section.title) << "' section of '"
                  << GetTitle() << "': " << DescribeItem(*items[i]);
      }
   }

   // The actions of the footer, after the items, as upstream's view does
   if (HasAdjustableItems())
   {
      using enum InGameUIItem::Type;
      if (!IsDefaults())
         nav.push_back({ -1, static_cast<int>(ResetToDefaults) });
      if (IsModified())
         nav.push_back({ -1, static_cast<int>(ResetToStoredValues) });
      if (InGameUIPageView::GetSaveMode(*this) != SaveMode::None && IsModified())
         nav.push_back({ -1, static_cast<int>(SaveChanges) });
   }
   m_nav = std::move(nav);

   // Keep the selection on an existing item
   if (std::ranges::none_of(m_nav, [this](const NavEntry& entry) { return entry.section == m_selected.section && entry.index == m_selected.index; }))
      m_selected = m_nav.empty() ? NavEntry { -2, 0 } : m_nav.front();
}

const string& ComposedPage::GetTitle() const { return InGameUIPageView::GetTitle(*this); }

void ComposedPage::SelectNextItem()
{
   if (m_nav.empty())
      return;
   const auto it = std::ranges::find_if(m_nav, [this](const NavEntry& entry) { return entry.section == m_selected.section && entry.index == m_selected.index; });
   m_selected = (it == m_nav.end() || it + 1 == m_nav.end()) ? m_nav.front() : *(it + 1);
}

void ComposedPage::SelectPrevItem()
{
   if (m_nav.empty())
      return;
   const auto it = std::ranges::find_if(m_nav, [this](const NavEntry& entry) { return entry.section == m_selected.section && entry.index == m_selected.index; });
   m_selected = (it == m_nav.end() || it == m_nav.begin()) ? m_nav.back() : *(it - 1);
}

void ComposedPage::AdjustItem(float direction, bool isInitialPress)
{
   if (m_selected.section == -1)
   {
      if (!isInitialPress)
         return;
      switch (static_cast<InGameUIItem::Type>(m_selected.index))
      {
      case InGameUIItem::Type::ResetToDefaults: ResetToDefaults(); break;
      case InGameUIItem::Type::ResetToStoredValues: ResetToStoredValues(); break;
      case InGameUIItem::Type::SaveChanges: Save(); break;
      default: break;
      }
      return;
   }
   if (m_selected.section < 0 || m_selected.section >= static_cast<int>(m_sections.size()))
      return;
   InGameUIPage* const page = GetSource(m_sections[m_selected.section].source);
   InGameUIPageView::SetSelectedIndex(*page, m_selected.index);
   page->AdjustItem(direction, isInitialPress);
}

const InGameUIItem* ComposedPage::GetSelectedSectionItem() const
{
   if (m_selected.section < 0 || m_selected.section >= static_cast<int>(m_sections.size()))
      return nullptr;
   const auto& items = InGameUIPageView::GetItems(*GetSource(m_sections[m_selected.section].source));
   return m_selected.index >= 0 && m_selected.index < static_cast<int>(items.size()) ? items[m_selected.index].get() : nullptr;
}

bool ComposedPage::IsActionSelected(InGameUIItem::Type type) const { return m_selected.section == -1 && m_selected.index == static_cast<int>(type); }

bool ComposedPage::HasAdjustableItems() const
{
   bool adjustable = false;
   ForEachShownItem([&adjustable](InGameUIPage&, InGameUIItem& item) { adjustable |= item.IsAdjustable(); });
   return adjustable;
}

bool ComposedPage::IsDefaults() const
{
   bool defaults = true;
   ForEachShownItem([&defaults](InGameUIPage&, InGameUIItem& item) { defaults &= item.m_excludeFromDefault || item.IsDefaultValue(); });
   return defaults;
}

bool ComposedPage::IsModified() const
{
   bool modified = false;
   ForEachShownItem([&modified](InGameUIPage&, InGameUIItem& item) { modified |= item.IsModified(); });
   return modified;
}

// As InGameUIPage::SaveGlobally and SaveTableOverride, for the shown items of the pages which save settings
void ComposedPage::SaveGlobally()
{
   Settings& tableSettings = g_settingsService.GetActiveSettings();
   Settings& appSettings = g_settingsService.GetAppSettings();
   ForEachShownItem([&tableSettings](InGameUIPage& page, InGameUIItem& item) { if (InGameUIPageView::GetSaveMode(page) != SaveMode::None) item.ResetSave(tableSettings); });
   tableSettings.Save();
   ForEachShownItem([&appSettings](InGameUIPage& page, InGameUIItem& item) { if (InGameUIPageView::GetSaveMode(page) != SaveMode::None) item.Save(appSettings, false); });
   appSettings.Save();
}

void ComposedPage::SaveTableOverride()
{
   Settings& tableSettings = g_settingsService.GetActiveSettings();
   Settings& appSettings = g_settingsService.GetAppSettings();
   ForEachShownItem([&tableSettings](InGameUIPage& page, InGameUIItem& item) { if (InGameUIPageView::GetSaveMode(page) != SaveMode::None) item.ResetSave(tableSettings); });
   ForEachShownItem(
      [&tableSettings, &appSettings](InGameUIPage& page, InGameUIItem& item)
      {
         // Items of the pages which only save globally are saved globally
         const SaveMode mode = InGameUIPageView::GetSaveMode(page);
         if (mode == SaveMode::None)
            return;
         const bool global = item.m_globalOnly || mode == SaveMode::Global;
         item.Save(global ? appSettings : tableSettings, !global);
      });
   tableSettings.Save();
   appSettings.Save();
}

void ComposedPage::ResetToStoredValues()
{
   if (!IsModified())
      return;
   ForEachShownItem([](InGameUIPage&, InGameUIItem& item) { item.ResetToStoredValue(); });
   m_player->m_liveUI->PushNotification("Changes were undone"s, 5000);
}

void ComposedPage::ResetToDefaults()
{
   if (IsDefaults())
      return;
   // Each page resets its shown items, with its own rules (for example the VR settings, which define the defaults of the table position from
   // the headset): the items it does not show here are excluded from the reset meanwhile
   for (const SourcePage& source : m_sources)
   {
      // Pages shown by sections which do not use their rules: their shown items are reset one by one
      if (std::ranges::any_of(m_sections, [&source](const Section& section) { return section.source == source.id && !section.pageRules; }))
      {
         bool reset = false;
         const auto& items = InGameUIPageView::GetItems(*source.page);
         for (int i = 0; i < static_cast<int>(items.size()); i++)
         {
            const bool shown = std::ranges::any_of(m_sections, [&](const Section& section) { return section.source == source.id && IsShown(section, *source.page, i); });
            if (shown && !IsHeader(*items[i]) && !items[i]->m_excludeFromDefault && !items[i]->IsDefaultValue())
            {
               items[i]->ResetToDefault();
               reset = true;
            }
         }
         if (reset)
            m_player->m_liveUI->PushNotification("Settings reset to defaults"s, 5000);
         continue;
      }
      vector<InGameUIItem*> excluded;
      bool shows = false;
      const auto& items = InGameUIPageView::GetItems(*source.page);
      for (int i = 0; i < static_cast<int>(items.size()); i++)
      {
         bool shown = false;
         for (const Section& section : m_sections)
            shown |= section.source == source.id && IsShown(section, *source.page, i);
         shows |= shown && !IsHeader(*items[i]);
         if (!shown && !items[i]->m_excludeFromDefault)
         {
            items[i]->m_excludeFromDefault = true;
            excluded.push_back(items[i].get());
         }
      }
      if (shows)
         source.page->ResetToDefaults();
      for (InGameUIItem* item : excluded)
         item->m_excludeFromDefault = false;
   }
}

bool ComposedPage::IsPlayerPauseAllowed() const
{
   return std::ranges::all_of(m_sources, [](const SourcePage& source) { return source.page->IsPlayerPauseAllowed(); });
}

}
