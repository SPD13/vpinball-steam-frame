// license:GPLv3+

#include "core/stdafx.h"
#include "Keyboard.h"

#include "Theme.h"
#include "Widgets.h"

#include "ui/live/LiveUI.h"

#include "fonts/IconsForkAwesome.h"

namespace VPX::VRUI::Keyboard
{

namespace
{
   string g_owner;
   bool g_ownerShown = false;
   string g_title;
   string g_text;
   size_t g_maxLength = 64;
   bool g_shift = false;
   std::function<void(const string&)> g_onChange;
   std::function<void()> g_onDone;

   constexpr const char* kRows[] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-.&" };
   constexpr int kColumns = 10;
   constexpr int kBottomRow = 4;
   // Last row, in key widths: shift, space, delete, clear, done
   constexpr float kBottomKeyStart[] = { 0.f, 1.5f, 5.f, 6.5f, 8.f, 10.f };
   constexpr int kBottomKeys = 5;

   // Key selected with the buttons: a character (row 0 to 3, column 0 to 9) or a key of the last row. The column is kept on the last row, to
   // get back to it going up.
   bool g_focused = false;
   int g_focusRow = 1;
   int g_focusColumn = 0;
   int g_focusBottomKey = 0;
   bool g_pressFocused = false; // OK was pressed: the selected key is pressed when the keyboard is drawn

   int GetBottomKeyAt(int column)
   {
      int key = 0;
      while (key < kBottomKeys - 1 && static_cast<float>(column) + 0.5f >= kBottomKeyStart[key + 1])
         key++;
      return key;
   }

   void Changed()
   {
      if (g_onChange)
         g_onChange(g_text);
   }
}

bool IsUsed()
{
   static const bool forced = getenv("VPX_VRUI_KEYBOARD") != nullptr && string(getenv("VPX_VRUI_KEYBOARD")) == "1"s;
   return forced || (g_pplayer && g_pplayer->m_vrDevice);
}

void Open(const string& owner, const string& title, const string& text, size_t maxLength, const std::function<void(const string&)>& onChange, const std::function<void()>& onDone)
{
   g_owner = owner;
   g_ownerShown = true;
   g_title = title;
   g_text = text;
   g_maxLength = maxLength;
   g_shift = false;
   g_onChange = onChange;
   g_onDone = onDone;
   g_focused = false;
   g_focusRow = 1;
   g_focusColumn = 0;
   g_pressFocused = false;
}

void Close()
{
   g_owner.clear();
   g_focused = false;
   g_pressFocused = false;
   g_onChange = nullptr;
   g_onDone = nullptr;
}

bool IsOpen() { return !g_owner.empty(); }

bool IsEditing(const string& owner) { return !owner.empty() && g_owner == owner; }

void Focus()
{
   if (IsOpen())
      g_focused = true;
}

bool HasFocus() { return g_focused && IsOpen(); }

bool HandleButton(Button button)
{
   if (!HasFocus())
      return false;
   switch (button)
   {
   case Button::Up: g_focusRow = max(g_focusRow - 1, 0); break;
   case Button::Down:
      if (g_focusRow == kBottomRow - 1)
         g_focusBottomKey = GetBottomKeyAt(g_focusColumn);
      g_focusRow = min(g_focusRow + 1, kBottomRow);
      break;
   case Button::Left:
   case Button::Right:
   {
      const int step = button == Button::Left ? -1 : 1;
      if (g_focusRow == kBottomRow)
      {
         // Going up then reaches the column above the middle of the key
         g_focusBottomKey = (g_focusBottomKey + step + kBottomKeys) % kBottomKeys;
         g_focusColumn = static_cast<int>(0.5f * (kBottomKeyStart[g_focusBottomKey] + kBottomKeyStart[g_focusBottomKey + 1]));
      }
      else
         g_focusColumn = (g_focusColumn + step + kColumns) % kColumns;
      break;
   }
   case Button::Confirm: g_pressFocused = true; break;
   case Button::Back: g_focused = false; break;
   }
   return true;
}

void KeepOpen(const string& owner)
{
   if (IsEditing(owner))
      g_ownerShown = true;
}

void EndFrame()
{
   // The edited field is not shown anymore (other page or screen, menu closed): stop editing
   if (!g_owner.empty() && !g_ownerShown)
      Close();
   g_ownerShown = false;
}

void TypeForTest(const string& text, bool clear)
{
   if (clear)
      g_text.clear();
   g_text += text;
   Changed();
}

float GetHeight(float width)
{
   const float keyHeight = Theme::Px(3.f);
   const float gap = Theme::Px(0.35f);
   return ImGui::GetTextLineHeightWithSpacing() + gap + 5.f * (keyHeight + gap) + Theme::Px(0.6f);
}

void Render(const ImVec2& pos, float width)
{
   if (!IsOpen())
      return;
   // The pointer is used again (trigger): the keys are clicked
   const bool buttonNav = g_pplayer && g_pplayer->m_liveUI->m_inGameUI.IsFlipperNav();
   if (!buttonNav)
      g_focused = false;
   const float keyHeight = Theme::Px(3.f);
   const float gap = Theme::Px(0.35f);
   const float keyWidth = (width - static_cast<float>(kColumns - 1) * gap) / static_cast<float>(kColumns);
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   drawList->AddRectFilled(pos - ImVec2(gap, gap), pos + ImVec2(width + gap, GetHeight(width)), IM_COL32(10, 12, 17, 250), Theme::Px(0.5f));

   // What is typed, with a caret, and with the buttons, how to type
   const char* const hint = !buttonNav ? "" : g_focused ? "A: press the key   B: back to the menu" : "A: type with the buttons";
   const float hintWidth = ImGui::CalcTextSize(hint).x;
   drawList->AddText(ImVec2(pos.x + width - hintWidth, pos.y), Theme::kTextMuted, hint);
   ImGui::SetCursorScreenPos(pos);
   const string shown = g_title.empty() ? g_text + '|' : g_title + ": " + g_text + '|';
   Widgets::TextEllipsis(shown.c_str(), width - hintWidth - (hintWidth > 0.f ? 2.f * gap : 0.f), Theme::kText);
   float y = pos.y + ImGui::GetTextLineHeightWithSpacing() + gap;

   ImGui::PushID("VRUIKeyboard");
   // Keys of a fixed width, their label centered. The key selected with the buttons is outlined, and pressed by OK.
   const auto fixedKey = [&](const char* id, const char* label, float x, float w, bool focused, bool accent = false)
   {
      const ImVec2 keyPos(x, y);
      ImGui::SetCursorScreenPos(keyPos);
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, keyHeight)) || (focused && g_pressFocused);
      const bool hovered = ImGui::IsItemHovered() || focused;
      const ImU32 background = accent ? (hovered ? IM_COL32(100, 175, 255, 255) : Theme::kAccent) : hovered ? Theme::kSurfaceHover : Theme::kSurface;
      drawList->AddRectFilled(keyPos, keyPos + ImVec2(w, keyHeight), background, Theme::Px(0.4f));
      if (focused)
         drawList->AddRect(keyPos, keyPos + ImVec2(w, keyHeight), accent ? IM_COL32_WHITE : Theme::kAccent, Theme::Px(0.4f), ImDrawFlags_None, Theme::Px(0.2f));
      const ImVec2 labelSize = ImGui::CalcTextSize(label);
      drawList->AddText(keyPos + (ImVec2(w, keyHeight) - labelSize) * 0.5f, accent ? IM_COL32(8, 18, 34, 255) : Theme::kText, label);
      return clicked;
   };
   const auto isFocused = [](int row, int column) { return g_focused && g_focusRow == row && (row == kBottomRow ? g_focusBottomKey : g_focusColumn) == column; };

   for (int rowIndex = 0; rowIndex < kBottomRow; rowIndex++)
   {
      const char* const row = kRows[rowIndex];
      float x = pos.x;
      for (const char* c = row; *c; c++)
      {
         const char typed = g_shift ? static_cast<char>(std::toupper(static_cast<unsigned char>(*c))) : *c;
         const char label[2] = { typed, '\0' };
         ImGui::PushID(*c);
         if (fixedKey("##key", label, x, keyWidth, isFocused(rowIndex, static_cast<int>(c - row))) && g_text.size() < g_maxLength)
         {
            g_text += typed;
            g_shift = false;
            Changed();
         }
         ImGui::PopID();
         x += keyWidth + gap;
      }
      y += keyHeight + gap;
   }

   // Last row: shift (1.5 keys), space (3.5), delete (1.5), clear (1.5), done (2), see kBottomKeyStart
   const float unit = keyWidth + gap;
   const auto bottomKey = [&](int key, const char* id, const char* label, bool accent = false)
   {
      const float x = pos.x + kBottomKeyStart[key] * unit;
      const float w = key == kBottomKeys - 1 ? pos.x + width - x : (kBottomKeyStart[key + 1] - kBottomKeyStart[key]) * unit - gap;
      return fixedKey(id, label, x, w, isFocused(kBottomRow, key), accent);
   };
   if (bottomKey(0, "##shift", g_shift ? ICON_FK_ARROW_UP " ABC" : ICON_FK_ARROW_UP " abc"))
      g_shift = !g_shift;
   if (bottomKey(1, "##space", "Space") && g_text.size() < g_maxLength)
   {
      g_text += ' ';
      Changed();
   }
   if (bottomKey(2, "##delete", ICON_FK_ARROW_LEFT) && !g_text.empty())
   {
      // A whole UTF-8 character
      while (!g_text.empty() && (static_cast<unsigned char>(g_text.back()) & 0xC0) == 0x80)
         g_text.pop_back();
      if (!g_text.empty())
         g_text.pop_back();
      Changed();
   }
   if (bottomKey(3, "##clear", "Clear"))
   {
      g_text.clear();
      Changed();
   }
   const bool done = bottomKey(4, "##done", ICON_FK_CHECK " Done", true);
   g_pressFocused = false;
   if (done)
   {
      const std::function<void()> onDone = g_onDone;
      Close();
      if (onDone)
         onDone();
   }
   ImGui::PopID();
}

}
