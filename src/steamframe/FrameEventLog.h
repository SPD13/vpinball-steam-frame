// license:GPLv3+

#pragma once

// Steam Frame Edition: logs the VR frames out of the ordinary, each with its time, so that a glitch the player saw can be matched with the
// log afterwards (2026-11-14: single fully black frames during play, a couple of times per session, cause unknown). One line per event:
//    Frame event: frames not rendered from now (runtime shouldRender <0|1>, session state <s>)        (VPX ends them without any layer)
//    Frame event: rendering again after <n> frames not rendered (...), <ms> ms
//    Frame event: displayed <p> periods after the previous frame                                     (a frame missed its display)
//    Frame event: GPU <ms> ms for a period of <ms> ms                                                  (the runtime's counter, over 1.5 periods)
//    Frame event: drawn at <scale> of the swapchain image                                             (dynamic resolution far down)
// Missed displays come in bursts when a table starts: at most kMaxLinesPerWindow lines per 5 s, then one line counting the others.

#include <chrono>
#include <cstdint>

namespace VPX
{

class FrameEventLog final
{
public:
   // From VRDevice::UpdateDynamicResolution, once per frame: display periods since the previous frame, the runtime's GPU time (-1 when
   // not read) and the display period
   void OnTiming(int periods, float gpuMs, float budgetMs);
   // From VRDevice::RenderFrame, just before xrEndFrame
   void OnSubmit(bool rendered, bool shouldRender, int sessionState, float executedScale);

private:
   bool Allow();

   static constexpr int kMaxLinesPerWindow = 10;
   std::chrono::steady_clock::time_point m_windowStart {};
   int m_windowLines = 0, m_windowSuppressed = 0;

   int m_notRendered = 0; // Frames of the current run ended without layers
   bool m_runShouldRender = true;
   int m_runSessionState = 0;
   std::chrono::steady_clock::time_point m_runStart {};

   bool m_lowScale = false;
};

}
