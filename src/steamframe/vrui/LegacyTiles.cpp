// license:GPLv3+

// Steam Frame Edition: tiles of the classic in-game UI (navigation and runnable items with an image, laid out as a grid by
// InGameUIPage::Render), defined here to keep the upstream file close to upstream's.

#include "core/stdafx.h"
#include "ui/live/ingameui/InGameUIPage.h"

#include "ui/live/LiveUI.h"

#include "imgui/imgui_internal.h"

namespace VPX::InGameUI
{

void InGameUIPage::RenderTile(int index, const ImVec2& pos, const ImVec2& size, const InGameUIItem*& hoveredItem)
{
   const InGameUIItem* const item = m_items[index].get();
   const ImVec2 padding = ImGui::GetStyle().ItemSpacing;
   const bool isFlipperNav = m_player->m_liveUI->m_inGameUI.IsFlipperNav();

   // Reserve the layout space (this also moves the cursor below the tile, ready for a new row or for the items following the grid)
   ImGui::SetCursorScreenPos(pos);
   ImGui::Dummy(size);
   const ImVec2 nextRowPos = ImGui::GetCursorScreenPos();

   const bool isMouseOver = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(pos, pos + size);
   const bool hovered = isFlipperNav ? (index == m_selectedItem) : isMouseOver;
   if (hovered || isMouseOver)
      hoveredItem = item;
   if (hovered && isFlipperNav)
   {
      const float localY = pos.y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
      if (localY < ImGui::GetScrollY())
         ImGui::SetScrollY(localY);
      else if (localY + size.y > ImGui::GetScrollY() + ImGui::GetWindowHeight())
         ImGui::SetScrollY(localY + size.y - ImGui::GetWindowHeight());
   }

   if (ImGui::IsRectVisible(pos, pos + size))
   {
      ImDrawList* const drawList = ImGui::GetWindowDrawList();
      if (hovered)
         drawList->AddRectFilled(pos, pos + size - ImVec2(padding.x * 0.5f, padding.y * 0.5f), IM_COL32(0, 255, 0, 50));

      // Image, fitted in the tile while keeping its aspect ratio, or a placeholder
      const ImVec2 imagePos = pos + padding;
      const ImVec2 imageSize(size.x - 2.f * padding.x, size.y - ImGui::GetTextLineHeight() - 3.f * padding.y);
      const ImTextureID image = item->m_tileImage();
      if (image && image->GetWidth() > 0 && image->GetHeight() > 0)
      {
         const float scale = min(imageSize.x / static_cast<float>(image->GetWidth()), imageSize.y / static_cast<float>(image->GetHeight()));
         const ImVec2 fitSize(scale * static_cast<float>(image->GetWidth()), scale * static_cast<float>(image->GetHeight()));
         ImGui::SetCursorScreenPos(imagePos + (imageSize - fitSize) * 0.5f);
         ImGui::Image(image, fitSize);
      }
      else
      {
         drawList->AddRectFilled(imagePos, imagePos + imageSize, IM_COL32(255, 255, 255, 24));
         drawList->AddRect(imagePos, imagePos + imageSize, IM_COL32(255, 255, 255, 64));
         if (item->m_tileLoading && item->m_tileLoading())
         {
            // Spinner: an arc of 3/4 of a turn, turning about once per second
            const ImVec2 center = imagePos + imageSize * 0.5f;
            const float radius = 0.12f * min(imageSize.x, imageSize.y);
            const float start = static_cast<float>(ImGui::GetTime()) * 2.f * static_cast<float>(M_PI);
            drawList->PathArcTo(center, radius, start, start + 1.5f * static_cast<float>(M_PI), 32);
            drawList->PathStroke(IM_COL32(255, 255, 255, 160), ImDrawFlags_None, max(2.f, 0.2f * radius));
         }
      }

      if (hovered)
         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
      ImGui::SetCursorScreenPos(ImVec2(imagePos.x, imagePos.y + imageSize.y + padding.y));
      TextWithEllipsis(item->m_label, imageSize.x);
      if (hovered)
         ImGui::PopStyleColor();
   }

   // Toggle icon, in the top right corner of the image
   bool isToggleHovered = false;
   if (item->m_tileToggleState && item->m_tileToggleAction)
   {
      const bool isOn = item->m_tileToggleState();
      const string& icon = isOn ? item->m_tileToggleIconOn : item->m_tileToggleIconOff;
      const ImVec2 iconSize = ImGui::CalcTextSize(icon.c_str());
      const ImVec2 toggleMax(pos.x + size.x - padding.x, pos.y + padding.y + iconSize.y + padding.y);
      const ImVec2 toggleMin(toggleMax.x - iconSize.x - 2.f * padding.x, pos.y + padding.y);
      isToggleHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(toggleMin, toggleMax);
      if (ImGui::IsRectVisible(toggleMin, toggleMax))
      {
         ImDrawList* const drawList = ImGui::GetWindowDrawList();
         drawList->AddRectFilled(toggleMin, toggleMax, IM_COL32(0, 0, 0, isToggleHovered ? 200 : 120), 0.25f * iconSize.y);
         drawList->AddText(ImVec2(toggleMin.x + padding.x, toggleMin.y + 0.5f * padding.y), isOn ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 255, 255, isToggleHovered ? 255 : 180), icon.c_str());
      }
   }

   if (isMouseOver && !m_isDraggingScroll && ImGui::IsMouseReleased(ImGuiMouseButton_::ImGuiMouseButton_Left))
   {
      m_selectedItem = index;
      if (isToggleHovered)
         item->m_tileToggleAction();
      else
         AdjustItem(1.f, true);
   }

   ImGui::SetCursorScreenPos(nextRowPos);
}

}
