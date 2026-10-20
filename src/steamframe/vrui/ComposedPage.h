// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUIPage.h"

// A screen of the new VR menu made of sections, each showing a part of a page (upstream's or ours). The pages are kept whole and alive: they
// build their items, handle their changes, and keep their rules (rebuilds, overrides of Open/Close/ResetToDefaults); the sections only choose
// which of their items are shown. So options can be grouped differently from upstream's pages without copying them, and an option added
// upstream shows up by itself: a 'rest' section shows the items of a page that no section of any screen shows (see docs/VR UI.md).
namespace VPX::VRUI
{

// An item of a page: the property it edits, or its label
struct ItemKey
{
   int propIndex = -1;
   string label;
};
ItemKey Prop(const VPX::Properties::PropertyRegistry::PropId& id);
ItemKey Label(const string& label);

struct Section
{
   string title; // Header shown above the items (none if empty)
   string source; // Page the items come from
   vector<ItemKey> keys; // Items shown, in the order of the page
   bool rest = false; // Instead of keys: the items of the page that no section with keys shows, on any screen (with the headers of the page)
   bool pageRules = true; // Defaults applied with the rules of the page (its ResetToDefaults), else to the shown items only
};

struct SourcePage
{
   string id;
   std::unique_ptr<InGameUI::InGameUIPage> page;
};

// The items shown by the sections with keys of all the screens, so that the 'rest' sections show the others: registered once by the layout
void ClaimItems(const string& source, const vector<ItemKey>& keys);

class ComposedPage final : public InGameUI::InGameUIPage
{
public:
   ComposedPage(const string& title, const string& info, vector<SourcePage> sources, vector<Section> sections);

   void Open(bool isBackwardAnimation) override;
   void Close(bool isBackwardAnimation) override;
   void Render(float elapsedS) override;
   void SelectNextItem() override;
   void SelectPrevItem() override;
   void AdjustItem(float direction, bool isInitialPress) override;
   bool IsDefaults() const override;
   bool IsModified() const override;
   void SaveGlobally() override;
   void SaveTableOverride() override;
   void ResetToStoredValues() override;
   void ResetToDefaults() override;
   bool IsPlayerPauseAllowed() const override;

   // For the menu and the footer (the items are the ones of the pages, not of this one)
   const InGameUI::InGameUIItem* GetSelectedSectionItem() const;
   bool IsActionSelected(InGameUI::InGameUIItem::Type type) const;
   bool HasAdjustableItems() const;
   // The pages the sections come from (the input mapping popup of an item is opened by its own page)
   void ForEachSourcePage(const std::function<void(InGameUI::InGameUIPage&)>& func) const
   {
      for (const SourcePage& source : m_sources)
         func(*source.page);
   }

private:
   void BuildPage() override { }

   InGameUI::InGameUIPage* GetSource(const string& id) const;
   const string& GetTitle() const;
   // Whether an item of a section is shown (headers of 'rest' sections: when an item of the section follows them)
   bool IsShown(const Section& section, const InGameUI::InGameUIPage& page, int index) const;
   // Calls a function for each shown item (not the headers) of each section
   void ForEachShownItem(const std::function<void(InGameUI::InGameUIPage&, InGameUI::InGameUIItem&)>& func) const;

   vector<SourcePage> m_sources;
   vector<Section> m_sections;

   // Button navigation, through the shown items of all the sections, then the Defaults/Undo/Save actions of the footer
   struct NavEntry
   {
      int section; // -1 for an action
      int index; // Index of the item in its page, or the type of the action
   };
   vector<NavEntry> m_nav;
   NavEntry m_selected { -2, 0 };
};

}
