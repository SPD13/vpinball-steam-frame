// license:GPLv3+

#include "core/stdafx.h"

#include "imgui/imgui_internal.h" // Needed for FindRenderedTextEnd (should be adapted when this function will be refactored in ImGui)

#include "NotificationOverlay.h"

unsigned int NotificationOverlay::PushNotification(const string &message, const int lengthMs, const unsigned int reuseId)
{
   const std::lock_guard<std::mutex> lock(m_mutex);
   auto notif = std::ranges::find_if(m_notifications, [reuseId](const Notification &n) { return n.id == reuseId; });
   if (notif != m_notifications.end())
   {
      notif->message = message;
      notif->disappearTick = msec() + lengthMs;
      return reuseId;
   }
   else
   {
      m_notifications.emplace_back(m_nextNotificationIs, message, msec() + lengthMs);
      m_nextNotificationIs++;
      return m_nextNotificationIs - 1;
   }
}

void NotificationOverlay::Update(bool showNotification, ImFont * font, float aboveY)
{
   const std::lock_guard<std::mutex> lock(m_mutex);
   const uint32_t tick = msec();
   const auto& io = ImGui::GetIO();
   float notifY = aboveY > 0.f ? aboveY : io.DisplaySize.y * 0.25f;
#if defined(VPX_STEAM_FRAME)
   ImGui::PushFont(font, m_style.fontSize > 0.f ? m_style.fontSize : font->LegacySize);
   if (aboveY <= 0.f && m_style.center.y > 0.f)
   {
      // Stacked downwards, the whole stack centered on the point
      float height = 0.f;
      for (int i = 0; i < static_cast<int>(m_notifications.size()); i++)
         if (tick <= m_notifications[i].disappearTick)
         {
            vector<string> lines;
            height += (height > 0.f ? 10.f : 0.f) + LayoutNotification(i, lines).y;
         }
      notifY = m_style.center.y - height * 0.5f;
   }
#else
   ImGui::PushFont(font, font->LegacySize);
#endif
   for (int i = static_cast<int>(m_notifications.size()) - 1; i >= 0; i--)
   {
      if (tick > m_notifications[i].disappearTick)
         m_notifications.erase(m_notifications.begin() + i);
      else if (showNotification && aboveY > 0.f)
      {
         // Newest closest to the anchor, like the newest is first when stacked downwards
         vector<string> lines;
         notifY -= 10.f + LayoutNotification(i, lines).y;
         RenderNotification(i, notifY);
      }
      else if (showNotification)
         notifY += 10.f + RenderNotification(i, notifY);
   }
   ImGui::PopFont();
}

ImVec2 NotificationOverlay::LayoutNotification(int index, vector<string>& lines) const
{
   ImFont *const font = ImGui::GetFont();
   const ImFontBaked *const fontBaked = ImGui::GetFontBaked();

#if defined(VPX_STEAM_FRAME)
   const float padding = m_style.fontSize > 0.f ? m_style.padding * 2.f : 50.f;
   const float maxWidth = m_style.maxWidth > 0.f ? m_style.maxWidth : ImGui::GetIO().DisplaySize.x - padding;
#else
   constexpr float padding = 50.f;
   const float maxWidth = ImGui::GetIO().DisplaySize.x - padding;
#endif

   ImVec2 text_size(0, 0);

   string line;
   std::istringstream iss(m_notifications[index].message);
   while (std::getline(iss, line))
   {
      if (line.empty())
      {
         lines.emplace_back(""s);
         continue;
      }
      const char *textEnd = line.c_str();
      while (*textEnd)
      {
         const char *nextLineTextEnd = ImGui::FindRenderedTextEnd(textEnd, nullptr);
         ImVec2 lineSize = font->CalcTextSizeA(fontBaked->Size, FLT_MAX, 0.0f, textEnd, nextLineTextEnd);
         if (lineSize.x > maxWidth)
         {
#if defined(VPX_STEAM_FRAME)
            // At the size the text is drawn at (CalcWordWrapPositionA uses the size the font was loaded with)
            const char *wrapPoint = font->CalcWordWrapPosition(fontBaked->Size, textEnd, nextLineTextEnd, maxWidth);
#else
            const char *wrapPoint = font->CalcWordWrapPositionA(font->Scale, textEnd, nextLineTextEnd, maxWidth);
#endif
            if (wrapPoint == textEnd)
               wrapPoint++;
            nextLineTextEnd = wrapPoint;
            lineSize = font->CalcTextSizeA(fontBaked->Size, FLT_MAX, 0.0f, textEnd, wrapPoint);
         }

         lines.emplace_back(textEnd, nextLineTextEnd);

         if (lineSize.x > text_size.x)
            text_size.x = lineSize.x;

         textEnd = nextLineTextEnd;

         while (*textEnd == ' ')
            textEnd++;
      }
   }
   text_size.x += padding / 2.f;
   text_size.y = ((float)lines.size() * ImGui::GetTextLineHeightWithSpacing()) + padding / 2.f;
   return text_size;
}

float NotificationOverlay::RenderNotification(int index, float posY) const
{
   ImFont *const font = ImGui::GetFont();
   const ImFontBaked *const fontBaked = ImGui::GetFontBaked();
   vector<string> lines;
   const ImVec2 text_size = LayoutNotification(index, lines);

   constexpr ImGuiWindowFlags window_flags
      = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
#if defined(VPX_STEAM_FRAME)
   const bool styled = m_style.fontSize > 0.f;
   if (styled)
   {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m_style.padding * 0.5f, m_style.padding * 0.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_style.rounding);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
   }
   ImGui::SetNextWindowBgAlpha(styled ? 0.88f : 0.666f);
   const float centerX = m_style.center.x > 0.f ? m_style.center.x : ImGui::GetIO().DisplaySize.x * 0.5f;
   ImGui::SetNextWindowPos(ImVec2(centerX - text_size.x * 0.5f, posY));
#else
   ImGui::SetNextWindowBgAlpha(0.666f);
   ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - text_size.x) / 2, posY));
#endif
   ImGui::SetNextWindowSize(text_size);
   ImGui::Begin(("Notification" + std::to_string(index)).c_str(), nullptr, window_flags);
   for (const string &lline : lines)
   {
      const ImVec2 lineSize = font->CalcTextSizeA(fontBaked->Size, FLT_MAX, 0.0f, lline.c_str());
      ImGui::SetCursorPosX((text_size.x - lineSize.x) / 2);
      ImGui::TextUnformatted(lline.c_str());
   }
   ImGui::End();
#if defined(VPX_STEAM_FRAME)
   if (styled)
      ImGui::PopStyleVar(3);
#endif
   return text_size.y;
}
