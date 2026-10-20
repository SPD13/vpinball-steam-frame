// license:GPLv3+

#pragma once

#include "ui/live/ingameui/InGameUIPage.h"

namespace VPX::InGameUI
{

// Draws the content of a page (its items) with the new VR UI, and gives the shell what it shows around it (title, actions). Friend of
// InGameUIPage: the only code using its private members (see PageAdapter.cpp).
class InGameUIPageView final
{
public:
   static const string& GetTitle(const InGameUIPage& page);
   static const string& GetInfo(const InGameUIPage& page);

   // Header actions of upstream's view (Defaults, Undo, Save, Back) are items of the page, at the end of its list, so that button navigation
   // reaches them: they are added again each frame, in the same order and on the same conditions as InGameUIPage::Render
   static void SyncCoreItems(InGameUIPage& page, bool withBack);
   static bool HasCoreItem(const InGameUIPage& page, InGameUIItem::Type type);
   static bool IsCoreItemSelected(const InGameUIPage& page, InGameUIItem::Type type);

   // Items, in the current window (the content region of the shell). Returns the item under the pointer, if any. The item selected with
   // button navigation is highlighted unless 'showSelection' is false (the rail of the menu has the focus).
   static const InGameUIItem* RenderContent(InGameUIPage& page, bool showSelection);

   // Defaults, Undo and Save buttons of the footer, right aligned to 'right' ('left' gets where they start). Returns the tooltip of the
   // hovered one, if any.
   static const char* RenderActions(InGameUIPage& page, float right, float height, float& left);

   // What the pages which draw more windows next to theirs use (see InGameUIPage::GetWindowPos)
   static void SetWindowOutputs(InGameUIPage& page, const ImVec2& pos, const ImVec2& size, bool hovered);

   // Popups of the page (input mapping)
   static void RenderPopups(InGameUIPage& page);

   static void Back(InGameUIPage& page);
};

}
