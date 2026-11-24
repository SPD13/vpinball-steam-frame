// license:GPLv3+

#pragma once

#include <cstdint>

// Steam Frame Edition: the frames SteamVR's compositor showed late, read from its own frame timing (OpenVR IVRCompositor::GetFrameTimings, through
// SteamVR's libopenvr_api.so opened at run time, as a background application). OpenXR does not tell an application that its frame missed the
// compositor: the predicted display time still moves by one period, and the compositor shows the previous frame again. With the headset worn, the
// compositor of the Steam Frame needs about 1.9 ms of GPU and starts its work about 2.6 ms earlier than unworn, so a frame is late from about 10 ms
// of GPU work at 72 Hz, well under the frame period (docs/Performance on the Steam Frame.md, section 8.19). Linux only; elsewhere, and without
// SteamVR, Poll returns false.

namespace VPX
{

class CompositorTiming final
{
public:
   struct Frames
   {
      int frames = 0; // Compositor frames with an application frame since the last poll
      int late = 0; // Of them, shown on another vsync than predicted, shown more than once (the next one was late), or never shown
   };

   static CompositorTiming& Get();

   // The frames since the previous call. Connects to SteamVR on the first call (once: a failure is logged and not retried). Render thread only.
   bool Poll(Frames& frames);
   bool IsConnected() const { return m_compositor != nullptr; }

private:
   CompositorTiming() = default;
   bool Connect();

   bool m_tried = false;
   void* m_library = nullptr;
   void* m_compositor = nullptr; // The function table of IVRCompositor ("FnTable:IVRCompositor_029")
   uint32_t m_lastFrameIndex = 0;
};

}
