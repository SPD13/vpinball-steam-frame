// license:GPLv3+

#include "core/stdafx.h"
#include "FrameEventLog.h"

namespace VPX
{

bool FrameEventLog::Allow()
{
   const auto now = std::chrono::steady_clock::now();
   if (now - m_windowStart >= std::chrono::seconds(5))
   {
      if (m_windowSuppressed > 0)
         PLOGI << "Frame event: " << m_windowSuppressed << " more in the last 5 s, not listed";
      m_windowStart = now;
      m_windowLines = 0;
      m_windowSuppressed = 0;
   }
   if (m_windowLines >= kMaxLinesPerWindow)
   {
      m_windowSuppressed++;
      return false;
   }
   m_windowLines++;
   return true;
}

void FrameEventLog::OnTiming(int periods, float gpuMs, float budgetMs)
{
   if (periods >= 2 && Allow())
      PLOGI << "Frame event: displayed " << periods << " periods after the previous frame";
   if (budgetMs > 0.f && gpuMs > budgetMs * 1.5f && Allow())
      PLOGI << "Frame event: GPU " << gpuMs << " ms for a period of " << budgetMs << " ms";
}

void FrameEventLog::OnSubmit(bool rendered, bool shouldRender, int sessionState, float executedScale)
{
   if (!rendered)
   {
      if (m_notRendered == 0)
      {
         m_runStart = std::chrono::steady_clock::now();
         m_runShouldRender = shouldRender;
         m_runSessionState = sessionState;
         // Logged at once, whatever the window: a stretch may last until the session ends (the headset taken off)
         PLOGI << "Frame event: frames not rendered from now (runtime shouldRender " << (shouldRender ? 1 : 0) << ", session state " << sessionState << ")";
      }
      m_notRendered++;
      return;
   }
   if (m_notRendered > 0)
   {
      // Logged when the run ends, whatever the window: these are the frames the compositor got nothing for
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_runStart).count();
      PLOGI << "Frame event: rendering again after " << m_notRendered << " frames not rendered (runtime shouldRender " << (m_runShouldRender ? 1 : 0) << ", session state "
            << m_runSessionState << "), " << ms << " ms";
      m_notRendered = 0;
   }
   const bool low = executedScale < 0.5f;
   if (low && !m_lowScale && Allow())
      PLOGI << "Frame event: drawn at " << executedScale << " of the swapchain image";
   m_lowScale = low;
}

}
