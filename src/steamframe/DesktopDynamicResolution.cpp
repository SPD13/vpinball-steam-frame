// license:GPLv3+

#include "core/stdafx.h"
#include "DesktopDynamicResolution.h"

#include <chrono>

namespace VPX
{

static constexpr float kStep = 0.1f; // The scales are the tenths of the full size (and the minimum)
static constexpr double kWindowS = 0.5; // GPU time averaged over half a second...
static constexpr int kOverWindows = 4; // ...over the target for two seconds to step down (each step draws the static prepass again)
static constexpr double kSettleS = 1.; // Readings left out after a change, and after the static prepass is accumulated
// Readings left out for the first seconds of a table: its first frames are heavier than the ones that follow (on Addams Family at twice the
// window's size on a Mac, 15 ms for 20 s, then 7 to 8 ms), and stepping down for them would draw the prepass again twice for nothing
static constexpr double kWarmupS = 15.;
static constexpr float kMinRiseWaitS = 3.f, kMaxRiseWaitS = 60.f;
static constexpr double kRiseDropS = 20.; // A drop that soon after a rise doubles the wait before the next one

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// GPU time expected at another scale: three quarters of it follow the pixels (the rest does not: vertices, shadows, the windows other than the
// playfield, fixed size passes)
static float Predict(float gpuMs, float from, float to) { return gpuMs * (0.25f + 0.75f * (to * to) / (from * from)); }

DesktopDynamicResolution& DesktopDynamicResolution::Get()
{
   static DesktopDynamicResolution instance;
   return instance;
}

void DesktopDynamicResolution::OnTableStart(const Settings& settings)
{
   m_enabled = settings.GetPlayerVR_DynamicResolution();
   SetTarget(settings.GetPlayerVR_DynamicResolutionTarget());
   SetMinScale(settings.GetPlayerVR_DynamicResolutionMinScale());
   m_scale = 1.f;
   m_frameScale = 1.f;
   m_tableFrames = false;
   m_reset = true;
   PLOGI << "Desktop dynamic resolution: " << (m_enabled ? std::format("on, target {:.0f} %, minimum {:.0f} %", m_target * 100.f, m_minScale * 100.f) : "off"s);
}

void DesktopDynamicResolution::SetEnabled(bool enabled)
{
   m_enabled = enabled;
   if (!enabled)
      m_scale = 1.f;
   m_reset = true;
}

void DesktopDynamicResolution::SetTarget(float target) { m_target = clamp(target, 0.5f, 1.f); }

void DesktopDynamicResolution::SetMinScale(float minScale)
{
   m_minScale = clamp(minScale, 0.5f, 1.f);
   if (m_scale < m_minScale)
      m_scale = m_minScale.load();
}

float DesktopDynamicResolution::BeginFrame(bool eligible, bool accumulating, bool& changed)
{
   m_eligible = eligible;
   m_accumulating = accumulating;
   m_tableFrames = true;
   const float scale = eligible && m_enabled ? m_scale.load() : 1.f;
   changed = scale != m_frameScale;
   m_frameScale = scale;
   return scale;
}

void DesktopDynamicResolution::ResetReadings(double now)
{
   m_windowStart = now;
   m_windowSumMs = 0.;
   m_windowCount = 0;
}

void DesktopDynamicResolution::ChangeScale(float scale, double now)
{
   const float previous = m_scale;
   if (scale < previous && now - m_lastRise < kRiseDropS)
      m_riseWaitS = min(m_riseWaitS * 2.f, kMaxRiseWaitS);
   if (scale > previous)
      m_lastRise = now;
   m_scale = scale;
   m_changes++;
   m_overWindows = 0;
   m_underWindows = 0;
   m_settleUntil = now + kSettleS;
   ResetReadings(now);
   PLOGI << std::format("Desktop dynamic resolution: {:.0f} % -> {:.0f} % (GPU {:.1f} ms, target {:.1f} ms of {:.1f} ms)", previous * 100.f, scale * 100.f, m_meanGpuMs.load(),
      m_budgetMs * m_target, m_budgetMs.load());
}

void DesktopDynamicResolution::OnFrame(float gpuMs, float refreshHz)
{
   const double now = Now();
   if (m_reset.exchange(false))
   {
      m_overWindows = 0;
      m_underWindows = 0;
      m_lastRise = -1e9;
      m_riseWaitS = kMinRiseWaitS;
      m_meanGpuMs = 0.f;
      m_warmupPending = true;
      ResetReadings(now);
   }
   // The warm-up starts with the first frame of the table (the render loop presents frames while it loads)
   if (!m_tableFrames)
      return;
   if (m_warmupPending)
   {
      m_warmupPending = false;
      m_settleUntil = now + kWarmupS;
   }
   if (refreshHz <= 0.f || !m_enabled || !m_eligible)
      return;
   const float budgetMs = 1000.f / refreshHz;
   m_budgetMs = budgetMs;
   // The static prepass draws the table again over 128 frames after a change of scale (and when the table starts): its frames say nothing of the
   // frames that follow
   if (m_accumulating)
      m_settleUntil = max(m_settleUntil, now + kSettleS);
   if (now < m_settleUntil)
   {
      ResetReadings(now);
      return;
   }
   if (gpuMs <= 0.f)
      return;
   m_windowSumMs += gpuMs;
   m_windowCount++;
   if (now - m_windowStart < kWindowS)
      return;
   const float meanMs = static_cast<float>(m_windowSumMs / m_windowCount);
   m_meanGpuMs = meanMs;
   ResetReadings(now);

   const float targetMs = budgetMs * m_target;
   const float minScale = m_minScale;
   const float scale = m_scale;
   if (meanMs > targetMs)
   {
      m_underWindows = 0;
      if (++m_overWindows < kOverWindows || scale <= minScale + 0.001f)
         return;
      // The largest step expected to keep under the target, with a little room
      float next = scale;
      do
         next = max(minScale, (ceilf(next / kStep - 0.001f) - 1.f) * kStep);
      while (next > minScale + 0.001f && Predict(meanMs, scale, next) > targetMs * 0.95f);
      ChangeScale(next, now);
   }
   else
   {
      m_overWindows = 0;
      if (scale >= 1.f)
         return;
      const float next = min(1.f, (floorf(scale / kStep + 0.001f) + 1.f) * kStep);
      if (Predict(meanMs, scale, next) >= targetMs * 0.9f)
      {
         m_underWindows = 0;
         return;
      }
      if (static_cast<float>(++m_underWindows * kWindowS) >= m_riseWaitS)
         ChangeScale(next, now);
   }
}

string DesktopDynamicResolution::GetStatus() const
{
   if (!m_enabled)
      return "off"s;
   if (!m_eligible)
      return "not used (VR or stereo 3D)"s;
   string status = std::format("{:.0f} % of the render size", m_scale * 100.f);
   if (const float budgetMs = m_budgetMs; budgetMs > 0.f && m_meanGpuMs > 0.f)
      status += std::format(", GPU {:.1f} ms of {:.1f} ms (target {:.0f} %)", m_meanGpuMs.load(), budgetMs, m_target * 100.f);
   if (const int changes = m_changes; changes > 0)
      status += std::format(", {} change{}", changes, changes > 1 ? "s" : "");
   return status;
}

}
