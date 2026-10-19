// license:GPLv3+

#pragma once

#include <mutex>

class NotificationOverlay final
{
public:
   NotificationOverlay() = default;
   ~NotificationOverlay() = default;

   unsigned int PushNotification(const string &message, const int lengthMs, const unsigned int reuseId = 0);

   // Notifications are stacked from a quarter of the display height, or, with 'aboveY', upwards from just above that position (for example the top
   // of the in-game menu window, which the player looks at)
   void Update(bool showNotification, ImFont * font, float aboveY = -1.f);

private:
   struct Notification
   {
      unsigned int id;
      string message;
      uint32_t disappearTick;
   };
   std::mutex m_mutex; // Notifications may be pushed from any thread (e.g. worker threads reporting load errors)
   vector<Notification> m_notifications;
   unsigned int m_nextNotificationIs = 1;

   ImVec2 LayoutNotification(int index, vector<string>& lines) const; // Wrapped lines and size
   float RenderNotification(int index, float posY) const;
};
