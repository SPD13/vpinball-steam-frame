// license:GPLv3+

#include "core/stdafx.h"
#include "Keyboard.h"

#include "Theme.h"
#include "Widgets.h"

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
}

void Close()
{
   g_owner.clear();
   g_onChange = nullptr;
   g_onDone = nullptr;
}

bool IsOpen() { return !g_owner.empty(); }

bool IsEditing(const string& owner) { return !owner.empty() && g_owner == owner; }

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
   const float keyHeight = Theme::Px(3.f);
   const float gap = Theme::Px(0.35f);
   const float keyWidth = (width - static_cast<float>(kColumns - 1) * gap) / static_cast<float>(kColumns);
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   drawList->AddRectFilled(pos - ImVec2(gap, gap), pos + ImVec2(width + gap, GetHeight(width)), IM_COL32(10, 12, 17, 250), Theme::Px(0.5f));

   // What is typed, with a caret
   ImGui::SetCursorScreenPos(pos);
   const string shown = g_title.empty() ? g_text + '|' : g_title + ": " + g_text + '|';
   Widgets::TextEllipsis(shown.c_str(), width, Theme::kText);
   float y = pos.y + ImGui::GetTextLineHeightWithSpacing() + gap;

   ImGui::PushID("VRUIKeyboard");
   // Keys of a fixed width, their label centered
   const auto fixedKey = [&](const char* id, const char* label, float x, float w, bool accent = false)
   {
      const ImVec2 keyPos(x, y);
      ImGui::SetCursorScreenPos(keyPos);
      const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, keyHeight));
      const bool hovered = ImGui::IsItemHovered();
      const ImU32 background = accent ? (hovered ? IM_COL32(100, 175, 255, 255) : Theme::kAccent) : hovered ? Theme::kSurfaceHover : Theme::kSurface;
      drawList->AddRectFilled(keyPos, keyPos + ImVec2(w, keyHeight), background, Theme::Px(0.4f));
      const ImVec2 labelSize = ImGui::CalcTextSize(label);
      drawList->AddText(keyPos + (ImVec2(w, keyHeight) - labelSize) * 0.5f, accent ? IM_COL32(8, 18, 34, 255) : Theme::kText, label);
      return clicked;
   };

   for (const char* row : kRows)
   {
      float x = pos.x;
      for (const char* c = row; *c; c++)
      {
         const char typed = g_shift ? static_cast<char>(std::toupper(static_cast<unsigned char>(*c))) : *c;
         const char label[2] = { typed, '\0' };
         ImGui::PushID(*c);
         if (fixedKey("##key", label, x, keyWidth) && g_text.size() < g_maxLength)
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

   // Last row: shift (1.5 keys), space (3.5), delete (1.5), clear (1.5), done (2)
   const float unit = keyWidth + gap;
   float x = pos.x;
   if (fixedKey("##shift", g_shift ? ICON_FK_ARROW_UP " ABC" : ICON_FK_ARROW_UP " abc", x, 1.5f * unit - gap))
      g_shift = !g_shift;
   x += 1.5f * unit;
   if (fixedKey("##space", "Space", x, 3.5f * unit - gap) && g_text.size() < g_maxLength)
   {
      g_text += ' ';
      Changed();
   }
   x += 3.5f * unit;
   if (fixedKey("##delete", ICON_FK_ARROW_LEFT, x, 1.5f * unit - gap) && !g_text.empty())
   {
      // A whole UTF-8 character
      while (!g_text.empty() && (static_cast<unsigned char>(g_text.back()) & 0xC0) == 0x80)
         g_text.pop_back();
      if (!g_text.empty())
         g_text.pop_back();
      Changed();
   }
   x += 1.5f * unit;
   if (fixedKey("##clear", "Clear", x, 1.5f * unit - gap))
   {
      g_text.clear();
      Changed();
   }
   x += 1.5f * unit;
   if (fixedKey("##done", ICON_FK_CHECK " Done", x, pos.x + width - x, true))
   {
      const std::function<void()> onDone = g_onDone;
      Close();
      if (onDone)
         onDone();
   }
   ImGui::PopID();
}

}
