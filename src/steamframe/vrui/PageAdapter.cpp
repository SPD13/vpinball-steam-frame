// license:GPLv3+

// Steam Frame Edition: the bridge between upstream's in-game UI pages and the new VR UI. InGameUIPage::Render calls RenderVRUI once it has done
// its model work (page rebuild, result of a path selection dialog, open/close animation): when it returns true, the new VR UI has drawn the page
// and handled its interactions, and upstream's drawing code is skipped. This file is the only one using the private members of InGameUIPage
// (it is its friend), so that an upstream change to them only breaks the build here.
//
// What InGameUIPage::Render does is mirrored here (see the parity checklist of docs/VR UI.md): the header actions as items, the selection
// fix-up, the rebuild deferred while a slider is dragged, the values written back each frame (which also clamps stored values out of range).
// Interactions go through the methods of the page (AdjustItem, Save, ...), so that their rules apply to both views.

#include "core/stdafx.h"
#include "PageView.h"

#include "ComposedPage.h"
#include "Keyboard.h"
#include "Shell.h"
#include "Theme.h"
#include "VRUI.h"
#include "Widgets.h"

#include "core/VPApp.h"
#include "fonts/IconsForkAwesome.h"
#include "ui/live/LiveUI.h"

#include "imgui/imgui_stdlib.h"

namespace VPX::InGameUI
{

using VPX::Properties::PropertyDef;
namespace Keyboard = VPX::VRUI::Keyboard;
namespace Theme = VPX::VRUI::Theme;
namespace Widgets = VPX::VRUI::Widgets;

static bool IsCoreType(InGameUIItem::Type type)
{
   using enum InGameUIItem::Type;
   switch (type)
   {
   case Back:
   case ResetToDefaults:
   case ResetToStoredValues:
   case SaveChanges: return true;
   case Label:
   case Runnable:
   case Navigation:
   case ActionInputMapping:
   case Property:
   case CustomRender: return false;
   }
   return false;
}

const string& InGameUIPageView::GetTitle(const InGameUIPage& page) { return page.m_title; }

const string& InGameUIPageView::GetInfo(const InGameUIPage& page) { return page.m_info; }

InGameUIPage::SaveMode InGameUIPageView::GetSaveMode(const InGameUIPage& page) { return page.m_saveMode; }

const vector<std::unique_ptr<InGameUIItem>>& InGameUIPageView::GetItems(const InGameUIPage& page) { return page.m_items; }

int InGameUIPageView::GetSelectedIndex(const InGameUIPage& page) { return page.m_selectedItem; }

void InGameUIPageView::SetSelectedIndex(InGameUIPage& page, int index) { page.m_selectedItem = index; }

bool InGameUIPageView::NeedsRebuild(const InGameUIPage& page) { return page.m_needsRebuild; }

void InGameUIPageView::SyncCoreItems(InGameUIPage& page, bool withBack)
{
   using enum InGameUIItem::Type;
   std::erase_if(page.m_items, [](auto& item) { return IsCoreType(item->m_type); });
   if (page.IsAdjustable())
   {
      if (!page.IsDefaults())
         page.m_items.push_back(std::make_unique<InGameUIItem>(ResetToDefaults));
      if (page.IsModified())
         page.m_items.push_back(std::make_unique<InGameUIItem>(ResetToStoredValues));
      if (page.m_saveMode != InGameUIPage::SaveMode::None && page.IsModified())
         page.m_items.push_back(std::make_unique<InGameUIItem>(SaveChanges));
   }
   if (withBack && !page.m_player->m_isLoading)
      page.m_items.push_back(std::make_unique<InGameUIItem>(Back));

   // As the number of selectable items may have changed, ensure m_selectedItem is still valid and pointing to a selectable item
   if (!page.HasSelectableItem())
      page.m_selectedItem = -1;
   else
   {
      page.m_selectedItem = clamp(page.m_selectedItem, 0, static_cast<int>(page.m_items.size()) - 1);
      while (!page.m_items[page.m_selectedItem]->IsSelectable())
         page.m_selectedItem = (page.m_selectedItem + 1) % static_cast<int>(page.m_items.size());
   }
}

bool InGameUIPageView::HasCoreItem(const InGameUIPage& page, InGameUIItem::Type type)
{
   return std::ranges::any_of(page.m_items, [type](const auto& item) { return item->m_type == type; });
}

bool InGameUIPageView::IsCoreItemSelected(const InGameUIPage& page, InGameUIItem::Type type)
{
   if (const auto* composed = dynamic_cast<const VRUI::ComposedPage*>(&page); composed)
      return composed->IsActionSelected(type) && page.m_player->m_liveUI->m_inGameUI.IsFlipperNav();
   const InGameUIItem* const item = page.GetSelectedItem();
   return item && item->m_type == type && page.m_player->m_liveUI->m_inGameUI.IsFlipperNav();
}

void InGameUIPageView::SetWindowOutputs(InGameUIPage& page, const ImVec2& pos, const ImVec2& size, bool hovered)
{
   page.m_windowPos = pos;
   page.m_windowSize = size;
   page.m_windowHovered = hovered;
}

void InGameUIPageView::RenderPopups(InGameUIPage& page) { page.RenderInputActionPopup(); }

void InGameUIPageView::Back(InGameUIPage& page) { page.m_player->m_liveUI->m_inGameUI.NavigateBack(); }

// The item of a page for a property, looked up again each time as the page may have been rebuilt
static InGameUIItem* FindPropertyItem(const vector<std::unique_ptr<InGameUIItem>>& items, const string& groupId, const string& propId)
{
   for (const auto& item : items)
      if (item->m_type == InGameUIItem::Type::Property && item->m_property->m_groupId == groupId && item->m_property->m_propId == propId)
         return item.get();
   return nullptr;
}

// Text property typed with the keyboard of the menu (no keyboard in VR), which changes the value at each key
static bool IsKeyboardField(const InGameUIItem& item)
{
   if (item.m_type != InGameUIItem::Type::Property || item.m_property->m_type != PropertyDef::Type::String || !Keyboard::IsUsed())
      return false;
   const auto* prop = dynamic_cast<const VPX::Properties::StringPropertyDef*>(item.m_property.get());
   return prop && prop->m_pathType == VPX::Properties::StringPropertyDef::PathType::None;
}

static string GetKeyboardOwner(const InGameUIPage& page, const InGameUIItem& item)
{
   return std::format("{}:{}.{}", static_cast<const void*>(&page), item.m_property->m_groupId, item.m_property->m_propId);
}

static void OpenKeyboard(InGameUIPage& page, const InGameUIItem& item)
{
   InGameUIPage* const pagePtr = &page;
   Keyboard::Open(GetKeyboardOwner(page, item), item.m_property->m_label, item.GetStringValue(), 256,
      [pagePtr, groupId = item.m_property->m_groupId, propId = item.m_property->m_propId](const string& text)
      {
         if (InGameUIItem* edited = FindPropertyItem(InGameUIPageView::GetItems(*pagePtr), groupId, propId); edited)
            edited->SetValue(text);
      });
}

// Marker of a property: modified (to be saved), or overridden by the table
static void RenderPropertyMarker(const InGameUIItem& item, const ImVec2& pos, float rowHeight)
{
   const char* icon = nullptr;
   ImU32 color = Theme::kTextMuted;
   if (item.IsModified())
   {
      icon = ICON_FK_PENCIL;
      color = Theme::kModified;
   }
   else if (item.IsOverriden(g_settingsService.GetAppSettings(), g_settingsService.GetActiveSettings()))
      icon = ICON_FK_DOT_CIRCLE_O;
   if (icon)
      ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, pos.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f), color, icon);
}

const char* InGameUIPageView::RenderActions(InGameUIPage& page, float right, float height, float& left)
{
   left = right;
   const auto* composed = dynamic_cast<const VRUI::ComposedPage*>(&page);
   if (composed ? !composed->HasAdjustableItems() : !page.IsAdjustable())
      return nullptr;
   using enum InGameUIItem::Type;
   struct Action { InGameUIItem::Type type; const char* icon; const char* label; const char* tooltip; bool enabled; };
   const bool canSave = page.m_saveMode != InGameUIPage::SaveMode::None;
   const Action actions[] = {
      { ResetToDefaults, ICON_FK_HEART, "Defaults", "Reset this page to its default values\n[Input shortcut: Launch Button]", !page.IsDefaults() },
      { ResetToStoredValues, ICON_FK_UNDO, "Undo", "Undo the changes", page.IsModified() },
      { SaveChanges, ICON_FK_FLOPPY_O, "Save", "Save the changes", canSave && page.IsModified() },
   };
   const float spacing = Theme::Px(0.6f);
   float width = 0.f;
   for (const Action& action : actions)
      if (action.type != SaveChanges || canSave)
         width += Widgets::PillWidth(action.icon, action.label, height) + spacing;
   const float y = ImGui::GetCursorScreenPos().y;
   float x = right - width + spacing;
   left = x;
   const char* tooltip = nullptr;
   for (const Action& action : actions)
   {
      if (action.type == SaveChanges && !canSave)
         continue;
      ImGui::SetCursorScreenPos(ImVec2(x, y));
      if (Widgets::Pill(action.label, action.icon, action.label, height, action.enabled, IsCoreItemSelected(page, action.type), action.type == SaveChanges))
      {
         switch (action.type)
         {
         case ResetToDefaults: page.ResetToDefaults(); break;
         case ResetToStoredValues: page.ResetToStoredValues(); break;
         case SaveChanges: page.Save(); break;
         default: break;
         }
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || IsCoreItemSelected(page, action.type))
         tooltip = action.tooltip;
      x += Widgets::PillWidth(action.icon, action.label, height) + spacing;
   }
   return tooltip;
}

const InGameUIItem* InGameUIPageView::RenderContent(InGameUIPage& page, bool showSelection, const std::function<bool(int)>* filter)
{
   using enum InGameUIItem::Type;
   const bool flipperNav = page.m_player->m_liveUI->m_inGameUI.IsFlipperNav() && showSelection;
   const InGameUIItem* hoveredItem = nullptr;
   ImDrawList* const drawList = ImGui::GetWindowDrawList();

   const float x0 = ImGui::GetCursorScreenPos().x;
   const float width = ImGui::GetContentRegionAvail().x;
   const float rowHeight = Theme::Px(Theme::kRowHeight);
   const float pad = Theme::Px(0.8f);
   const float markerWidth = Theme::Px(2.2f);
   const float labelWidth = floorf(width * 0.42f);
   const float widgetX = x0 + labelWidth;
   const float widgetWidth = width - labelWidth - markerWidth - pad;
   const float frameHeight = ImGui::GetFrameHeight();

   // Consecutive tiles are laid out as a grid, which still is navigated as a list
   const int tileColumns = clamp(static_cast<int>(width / Theme::Px(Theme::kTileWidth)), 2, 6);
   if (filter == nullptr)
      page.m_tileColumns = tileColumns; // For the button navigation in the grid (InGameUIPage::StepTileGrid)
   const float tileWidth = floorf(width / static_cast<float>(tileColumns));
   const float tileGap = Theme::Px(0.5f);
   const float tileImageHeight = (tileWidth - tileGap - 2.f * pad) * 0.625f;
   const ImVec2 tileSize(tileWidth - tileGap, tileImageHeight + 2.f * pad + ImGui::GetTextLineHeight() + pad);
   int tileColumn = 0;
   float tileRowY = 0.f;

   ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, Theme::Px(Theme::kRowGap)));
   for (int i = 0; i < static_cast<int>(page.m_items.size()); i++)
   {
      // A value change handled while drawing a previous item may have requested a page rebuild: stop here as the following items may be stale
      if (page.m_needsRebuild)
         break;

      InGameUIItem* const item = page.m_items[i].get();
      if (IsCoreType(item->m_type) || (filter && !(*filter)(i)))
         continue;
      const bool selected = flipperNav && i == page.m_selectedItem;

      // Tiles: image with the label below, and an optional toggle (favorite star) in the top right corner of the image
      if (item->m_tileImage && (item->m_type == Navigation || item->m_type == Runnable))
      {
         if (tileColumn == 0)
            tileRowY = ImGui::GetCursorScreenPos().y;
         const ImVec2 pos(x0 + static_cast<float>(tileColumn) * tileWidth, tileRowY);
         // Off screen (lists of thousands of tables): only its place in the layout, its image being loaded when it is less than a screen
         // below the view, ready to be scrolled to. The selected tile is always drawn, as it scrolls the view to itself.
         if (!selected && !ImGui::IsRectVisible(pos, pos + tileSize))
         {
            if (ImGui::IsRectVisible(pos - ImVec2(0.f, ImGui::GetWindowHeight()), pos + tileSize))
               item->m_tileImage();
            ImGui::SetCursorScreenPos(ImVec2(x0, pos.y + tileSize.y + tileGap));
            ImGui::Dummy(ImVec2(0.f, 0.f));
            tileColumn = (tileColumn + 1) % tileColumns;
            continue;
         }
         ImGui::PushID(i);
         ImGui::SetCursorScreenPos(pos);
         const bool clicked = ImGui::InvisibleButton("tile", tileSize);
         const bool hovered = ImGui::IsItemHovered();
         if (hovered || selected)
            hoveredItem = item;
         if (selected)
            Widgets::ScrollIntoView(pos, pos + tileSize);
         if (ImGui::IsRectVisible(pos, pos + tileSize))
         {
            const float rounding = Theme::Px(0.5f);
            drawList->AddRectFilled(pos, pos + tileSize, hovered || selected ? Theme::kSurfaceHover : Theme::kSurface, rounding);
            if (selected)
               drawList->AddRect(pos, pos + tileSize, Theme::kAccent, rounding, ImDrawFlags_None, Theme::Px(0.18f));
            const ImVec2 imagePos = pos + ImVec2(pad, pad);
            const ImVec2 imageSize(tileSize.x - 2.f * pad, tileImageHeight);
            const ImTextureID image = item->m_tileImage();
            if (image && image->GetWidth() > 0 && image->GetHeight() > 0)
            {
               const float scale = min(imageSize.x / static_cast<float>(image->GetWidth()), imageSize.y / static_cast<float>(image->GetHeight()));
               const ImVec2 fitSize(scale * static_cast<float>(image->GetWidth()), scale * static_cast<float>(image->GetHeight()));
               const ImVec2 fitPos = imagePos + (imageSize - fitSize) * 0.5f;
               drawList->AddImageRounded(image, fitPos, fitPos + fitSize, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE, Theme::Px(0.3f));
            }
            else
            {
               drawList->AddRectFilled(imagePos, imagePos + imageSize, IM_COL32(255, 255, 255, 12), Theme::Px(0.3f));
               if (item->m_tileLoading && item->m_tileLoading())
                  Widgets::Spinner(imagePos + imageSize * 0.5f, 0.12f * min(imageSize.x, imageSize.y), IM_COL32(255, 255, 255, 160));
               else
               {
                  const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FK_PICTURE_O);
                  drawList->AddText(imagePos + (imageSize - iconSize) * 0.5f, Theme::kTextDisabled, ICON_FK_PICTURE_O);
               }
            }
            ImGui::SetCursorScreenPos(ImVec2(imagePos.x, imagePos.y + imageSize.y + pad * 0.6f));
            Widgets::TextEllipsis(item->m_label.c_str(), imageSize.x, hovered || selected ? Theme::kText : Theme::kTextMuted);
            // Tag, in the bottom left corner of the image (the toggle is in the top right one)
            if (!item->m_tileBadge.empty())
            {
               const ImVec2 textSize = ImGui::CalcTextSize(item->m_tileBadge.c_str());
               const ImVec2 badgePad(Theme::Px(0.35f), Theme::Px(0.1f));
               const ImVec2 badgeMin(imagePos.x + Theme::Px(0.3f), imagePos.y + imageSize.y - textSize.y - 2.f * badgePad.y - Theme::Px(0.3f));
               drawList->AddRectFilled(badgeMin, badgeMin + textSize + badgePad * 2.f, Theme::kAccent, Theme::Px(0.3f));
               drawList->AddText(badgeMin + badgePad, IM_COL32_WHITE, item->m_tileBadge.c_str());
            }
            // Warning, in the top left corner of the image
            if (!item->m_tileWarning.empty())
            {
               const ImVec2 textSize = ImGui::CalcTextSize(item->m_tileWarning.c_str());
               const ImVec2 badgePad(Theme::Px(0.35f), Theme::Px(0.1f));
               const ImVec2 badgeMin(imagePos.x + Theme::Px(0.3f), imagePos.y + Theme::Px(0.3f));
               drawList->AddRectFilled(badgeMin, badgeMin + textSize + badgePad * 2.f, IM_COL32(234, 179, 8, 240), Theme::Px(0.3f));
               drawList->AddText(badgeMin + badgePad, IM_COL32(66, 32, 6, 255), item->m_tileWarning.c_str());
            }
         }
         bool toggleHovered = false;
         if (item->m_tileToggleState && item->m_tileToggleAction)
         {
            const bool isOn = item->m_tileToggleState();
            const string& icon = isOn ? item->m_tileToggleIconOn : item->m_tileToggleIconOff;
            const float radius = Theme::Px(1.5f);
            const ImVec2 center(pos.x + tileSize.x - pad - radius * 0.6f, pos.y + pad + radius * 0.6f);
            const ImVec2 toCenter = ImGui::GetMousePos() - center;
            toggleHovered = hovered && toCenter.x * toCenter.x + toCenter.y * toCenter.y < radius * radius;
            drawList->AddCircleFilled(center, radius * 0.85f, IM_COL32(0, 0, 0, toggleHovered ? 210 : 140));
            const ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
            drawList->AddText(center - iconSize * 0.5f, isOn ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 255, 255, toggleHovered ? 255 : 190), icon.c_str());
         }
         if (clicked)
         {
            page.m_selectedItem = i;
            if (toggleHovered)
               item->m_tileToggleAction();
            else
               page.ActivateItem();
         }
         // Cursor below the tile: next row of tiles, or the items following the grid
         ImGui::SetCursorScreenPos(ImVec2(x0, pos.y + tileSize.y + tileGap));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         tileColumn = (tileColumn + 1) % tileColumns;
         ImGui::PopID();
         continue;
      }
      if (tileColumn != 0)
      {
         // Below the last, incomplete, row of tiles
         ImGui::SetCursorScreenPos(ImVec2(x0, tileRowY + tileSize.y + tileGap));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         tileColumn = 0;
      }

      const ImVec2 rowMin = ImGui::GetCursorScreenPos();
      // Rows of a list off screen (the tables of the list view): only their place in the layout, as tiles
      if ((item->m_type == Navigation || item->m_type == Runnable) && !selected && !ImGui::IsRectVisible(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight)))
      {
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowHeight));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         continue;
      }
      ImGui::PushID(i);
      const bool isMouseOver = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight));
      const float textY = rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;

      switch (item->m_type)
      {
      case Label:
      {
         switch (item->m_labelType)
         {
         case InGameUIItem::LabelType::Header: Widgets::SectionHeader(item->m_label.c_str(), width); break;
         case InGameUIItem::LabelType::Info: Widgets::WrappedText(item->m_label.c_str(), Theme::kTextMuted, width); break;
         case InGameUIItem::LabelType::Error: Widgets::WrappedText(item->m_label.c_str(), Theme::kError, width); break;
         case InGameUIItem::LabelType::Markdown:
         {
            // Scrollable with button navigation, as upstream's view
            drawList->ChannelsSplit(2);
            drawList->ChannelsSetCurrent(1);
            const float startY = ImGui::GetCursorPosY();
            page.m_player->m_liveUI->SetMarkdownStartId(ImGui::GetItemID());
            ImGui::Markdown(item->m_label.c_str(), item->m_label.length(), page.m_player->m_liveUI->GetMarkdownConfig());
            const float infoHeight = ImGui::GetCursorPosY() - startY;
            drawList->ChannelsSetCurrent(0);
            Widgets::RowBackground(rowMin, ImVec2(rowMin.x + width, ImGui::GetCursorScreenPos().y), false, selected);
            drawList->ChannelsMerge();
            if (selected && infoHeight > ImGui::GetWindowHeight())
            {
               const float scrollSpread = infoHeight - ImGui::GetWindowHeight();
               page.m_pressedItemScroll = clamp(page.m_pressedItemScroll, 0.f, scrollSpread);
               ImGui::SetScrollY(ImGui::GetCursorPosY() - infoHeight + page.m_pressedItemScroll);
            }
            else if (selected)
               Widgets::ScrollIntoView(rowMin, ImVec2(rowMin.x + width, ImGui::GetCursorScreenPos().y));
            break;
         }
         }
         if (isMouseOver && item->IsSelectable())
            hoveredItem = item;
         break;
      }

      case Navigation:
      case Runnable:
      {
         const bool clicked = ImGui::InvisibleButton("row", ImVec2(width, rowHeight));
         const bool hovered = ImGui::IsItemHovered();
         Widgets::RowBackground(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight), hovered, selected);
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x + pad, textY));
         Widgets::TextEllipsis(item->m_label.c_str(), width - 2.f * pad - markerWidth, item->m_type == Runnable ? IM_COL32(150, 200, 255, 255) : Theme::kText);
         if (item->m_type == Navigation)
            drawList->AddText(ImVec2(rowMin.x + width - pad - ImGui::CalcTextSize(ICON_FK_ANGLE_RIGHT).x, textY), hovered || selected ? Theme::kAccent : Theme::kTextMuted, ICON_FK_ANGLE_RIGHT);
         if (hovered || selected)
            hoveredItem = item;
         if (selected)
            Widgets::ScrollIntoView(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight));
         if (clicked)
         {
            page.m_selectedItem = i;
            page.ActivateItem();
         }
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowHeight));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         break;
      }

      case CustomRender:
      {
         // Drawn by the page: its background is placed behind it once its height is known
         drawList->ChannelsSplit(2);
         drawList->ChannelsSetCurrent(1);
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x + pad, rowMin.y + Theme::Px(0.3f)));
         ImGui::BeginGroup();
         ImGui::PushItemWidth(width - 2.f * pad);
         if (item->m_customRender)
            item->m_customRender(i, item);
         ImGui::PopItemWidth();
         ImGui::EndGroup();
         const ImVec2 rowMax(rowMin.x + width, max(rowMin.y + rowHeight, ImGui::GetItemRectMax().y + Theme::Px(0.3f)));
         const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(rowMin, rowMax);
         drawList->ChannelsSetCurrent(0);
         if (!item->m_customHighlight)
            Widgets::RowBackground(rowMin, rowMax, hovered, selected);
         drawList->ChannelsMerge();
         if (hovered || selected)
            hoveredItem = item;
         if (selected)
            Widgets::ScrollIntoView(rowMin, rowMax);
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         break;
      }

      case ActionInputMapping:
      {
         Widgets::RowBackground(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight), isMouseOver, selected);
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x + pad, textY));
         Widgets::TextEllipsis(item->m_label.c_str(), labelWidth - 2.f * pad, Theme::kText);
         const float widgetY = rowMin.y + (rowHeight - frameHeight) * 0.5f;
         const float clearWidth = frameHeight * 1.2f;
         const float stateWidth = ImGui::CalcTextSize(ICON_FK_CIRCLE).x + pad;
         float x = widgetX;
         if (item->m_inputAction->IsMapped())
         {
            ImGui::SetCursorScreenPos(ImVec2(x, widgetY));
            if (ImGui::Button(ICON_FK_TIMES "##clear", ImVec2(clearWidth, frameHeight)))
            {
               item->m_inputAction->ClearMapping();
               // Base navigation actions must always be mapped (otherwise navigation ends up broken), so immediately start a mapping definition
               if (item->m_inputAction->IsNavigationAction())
               {
                  page.m_defineActionPopup = true;
                  page.m_defineActionItem = item;
               }
            }
         }
         x += clearWidth + ImGui::GetStyle().ItemSpacing.x;
         const string mappingLabel = item->m_inputAction->GetMappingLabel();
         const float mapWidth = widgetX + widgetWidth - x - stateWidth;
         ImGui::SetCursorScreenPos(ImVec2(x, widgetY));
         if (ImGui::Button((mappingLabel + "##map").c_str(), ImVec2(mapWidth, frameHeight)))
         {
            page.m_defineActionPopup = true;
            page.m_defineActionItem = item;
         }
         drawList->AddText(ImVec2(widgetX + widgetWidth - stateWidth + pad * 0.5f, textY), item->m_inputAction->IsPressed() ? Theme::kAccent : Theme::kTextMuted,
            item->m_inputAction->IsPressed() ? ICON_FK_CIRCLE : ICON_FK_CIRCLE_O);
         if (item->IsModified())
            drawList->AddText(ImVec2(x0 + width - markerWidth + pad * 0.5f, textY), Theme::kModified, ICON_FK_PENCIL);
         if (isMouseOver || selected)
            hoveredItem = item;
         if (selected)
            Widgets::ScrollIntoView(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight));
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowHeight));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         break;
      }

      case Property:
      {
         Widgets::RowBackground(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight), isMouseOver, selected);
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x + pad, textY));
         Widgets::TextEllipsis(item->m_property->m_label.c_str(), labelWidth - 2.f * pad, Theme::kText);
         const ImVec2 widgetPos(widgetX, rowMin.y + (rowHeight - frameHeight) * 0.5f);
         ImGui::SetCursorScreenPos(widgetPos);
         switch (item->m_property->m_type)
         {
         case PropertyDef::Type::Float:
         {
            auto prop = dynamic_cast<VPX::Properties::FloatPropertyDef*>(item->m_property.get());
            float v = item->GetFloatValue() * item->m_floatValueDisplayScale;
            page.m_deferRebuild = Widgets::SliderFloat("##value", &v, prop->m_min * item->m_floatValueDisplayScale, prop->m_max * item->m_floatValueDisplayScale, item->m_format.c_str(), widgetWidth);
            item->SetValue(v / item->m_floatValueDisplayScale);
            page.m_deferRebuild = false;
            break;
         }

         case PropertyDef::Type::Int:
         {
            auto prop = dynamic_cast<VPX::Properties::IntPropertyDef*>(item->m_property.get());
            int v = item->GetIntValue();
            const auto id = Settings::GetRegistry().GetPropertyId(item->m_property->m_groupId, item->m_property->m_propId);
            const bool isWindowSize = id.has_value() && (Settings::m_propPlayer_PlayfieldWidth.index == id.value().index || Settings::m_propPlayer_PlayfieldHeight.index == id.value().index);
            if (isWindowSize && !flipperNav)
            {
               // As upstream's view: the main window size is typed, as a slider would move with the window it resizes
               const float buttonWidth = frameHeight * 1.4f;
               ImGui::SetNextItemWidth(widgetWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
               ImGui::InputInt("##value", &v, 1, 100, ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank);
               ImGui::SameLine();
               if (ImGui::Button(Settings::m_propPlayer_PlayfieldWidth.index == id.value().index ? ICON_FK_ARROWS_H : ICON_FK_ARROWS_V, ImVec2(buttonWidth, frameHeight)))
                  v = prop->m_max;
            }
            else
            {
               Widgets::SliderInt("##value", &v, prop->m_min, prop->m_max, item->m_format.c_str(), widgetWidth);
            }
            page.m_deferRebuild = ImGui::IsItemActive();
            item->SetValue(v);
            page.m_deferRebuild = false;
            break;
         }

         case PropertyDef::Type::Enum:
         {
            // Previous/next arrows instead of a combo box: no popup list to point at in VR
            auto prop = dynamic_cast<VPX::Properties::EnumPropertyDef*>(item->m_property.get());
            const int nValues = static_cast<int>(prop->m_values.size());
            const int index = clamp(item->GetIntValue() - prop->m_min, 0, max(nValues - 1, 0));
            const string& value = nValues > 0 ? prop->m_values[index] : string();
            if (const int direction = Widgets::Stepper("##value", value.c_str(), ImVec2(widgetWidth, frameHeight), false); direction != 0)
            {
               page.m_selectedItem = i;
               page.AdjustItem(static_cast<float>(direction), true);
            }
            item->SetValue(item->GetIntValue());
            break;
         }

         case PropertyDef::Type::Bool:
         {
            const ImVec2 toggleSize(frameHeight * 1.9f, frameHeight);
            ImGui::SetCursorScreenPos(ImVec2(widgetX + widgetWidth - toggleSize.x, widgetPos.y));
            if (Widgets::Toggle("##value", item->GetBoolValue(), toggleSize, false))
            {
               page.m_selectedItem = i;
               page.AdjustItem(1.f, true);
            }
            break;
         }

         case PropertyDef::Type::String:
         {
            auto prop = dynamic_cast<VPX::Properties::StringPropertyDef*>(item->m_property.get());
            string v = item->GetStringValue();
            if (IsKeyboardField(*item))
            {
               // No keyboard in VR: the field opens the keyboard of the menu, which changes the value at each key
               const string owner = GetKeyboardOwner(page, *item);
               const ImVec2 fieldSize(widgetWidth, frameHeight);
               Keyboard::KeepOpen(owner, widgetPos, widgetPos + fieldSize);
               const bool editing = Keyboard::IsEditing(owner);
               const bool clicked = ImGui::InvisibleButton("##value", fieldSize);
               const bool hovered = ImGui::IsItemHovered();
               const float rounding = Theme::Px(0.45f);
               drawList->AddRectFilled(widgetPos, widgetPos + fieldSize, hovered ? Theme::kSurfaceHover : Theme::kSurface, rounding);
               if (editing)
                  drawList->AddRect(widgetPos, widgetPos + fieldSize, Theme::kAccent, rounding, ImDrawFlags_None, Theme::Px(0.15f));
               const float iconWidth = ImGui::CalcTextSize(ICON_FK_KEYBOARD_O).x;
               ImGui::SetCursorScreenPos(ImVec2(widgetPos.x + pad, textY));
               Widgets::TextEllipsis(v.empty() ? "Type..." : v.c_str(), widgetWidth - 3.f * pad - iconWidth, v.empty() ? Theme::kTextDisabled : Theme::kText);
               drawList->AddText(ImVec2(widgetPos.x + widgetWidth - pad - iconWidth, textY), editing ? Theme::kAccent : Theme::kTextMuted, ICON_FK_KEYBOARD_O);
               if (clicked)
               {
                  page.m_selectedItem = i;
                  OpenKeyboard(page, *item);
               }
            }
            else if (prop->m_pathType != VPX::Properties::StringPropertyDef::PathType::None)
            {
               const bool isFolder = prop->m_pathType == VPX::Properties::StringPropertyDef::PathType::Folder;
               const float buttonWidth = frameHeight * 1.4f;
               ImGui::SetNextItemWidth(widgetWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
               ImGui::InputTextWithHint("##value", isFolder ? "Select a folder..." : "Select a file...", &v);
               ImGui::SameLine();
               if (ImGui::Button(isFolder ? ICON_FK_FOLDER_OPEN : ICON_FK_FILE_O, ImVec2(buttonWidth, frameHeight)))
                  page.BrowseForPath(*item);
            }
            else
            {
               ImGui::SetNextItemWidth(widgetWidth);
               ImGui::InputText("##value", &v);
            }
            if (!Keyboard::IsUsed() || prop->m_pathType != VPX::Properties::StringPropertyDef::PathType::None)
               item->SetValue(v);
            break;
         }
         }
         RenderPropertyMarker(*item, ImVec2(x0 + width - markerWidth + pad * 0.5f, rowMin.y), rowHeight);
         if (isMouseOver || selected)
            hoveredItem = item;
         if (selected)
            Widgets::ScrollIntoView(rowMin, ImVec2(rowMin.x + width, rowMin.y + rowHeight));
         ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowHeight));
         ImGui::Dummy(ImVec2(0.f, 0.f));
         break;
      }

      case Back:
      case ResetToDefaults:
      case ResetToStoredValues:
      case SaveChanges: break;
      }
      ImGui::PopID();
   }
   if (tileColumn != 0)
   {
      ImGui::SetCursorScreenPos(ImVec2(x0, tileRowY + tileSize.y + tileGap));
      ImGui::Dummy(ImVec2(0.f, 0.f));
   }
   ImGui::PopStyleVar();

   // Rebuild requests made while a slider is dragged are held until it is released (see InGameUIPage::Render)
   if (page.m_rebuildPending && !ImGui::IsAnyItemActive())
   {
      page.m_rebuildPending = false;
      page.m_needsRebuild = true;
   }
   ImGui::Dummy(ImVec2(0.f, Theme::Px(0.5f)));

   // The hovered item must still exist (a click may have rebuilt the page)
   if (hoveredItem && std::ranges::none_of(page.m_items, [hoveredItem](const auto& item) { return item.get() == hoveredItem; }))
      hoveredItem = nullptr;
   return hoveredItem;
}

void InGameUIPage::ActivateItem()
{
   // A text field of the new view: OK gives the focus to the keyboard of the menu, to type with the buttons
   if (const InGameUIItem* item = GetSelectedItem(); VRUI::IsActive() && item && IsKeyboardField(*item))
   {
      if (!Keyboard::IsEditing(GetKeyboardOwner(*this, *item)))
         OpenKeyboard(*this, *item);
      Keyboard::Focus();
      return;
   }
   AdjustItem(1.f, true);
}

bool InGameUIPage::RenderVRUI(float elapsedS)
{
   if (!VRUI::IsActive())
      return VRUI::Shell::IsMainMenu(*this); // The main menu of the new view has nothing for upstream's view to draw (it is closing)
   return VRUI::Shell::RenderPage(*this, elapsedS);
}

}
