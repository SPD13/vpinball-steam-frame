// license:GPLv3+

#pragma once

// Steam Frame Edition: dynamic resolution outside VR (desktop and cabinet). The scene is rendered into the top left part of its buffers at a
// scale below 100 % when the GPU cannot keep up with the playfield window, as in VR (RenderDevice::BeginScaledRendering), then stretched to the
// window by the upscale pass of the post-processing (Renderer::ApplyUpscaling). Same settings as in VR: PlayerVR/DynamicResolution (on in the
// Low and Medium performance profiles), DynamicResolutionTarget and DynamicResolutionMinScale.
//
// Unlike VR, the desktop renders the parts of the table which never move once, accumulated over 128 frames (the static prepass), and each
// frame starts from that picture: it is drawn at the scale of the frame, so a change of scale draws it again, with a moment of heavier
// frames (and the ambient occlusion computed again, a hitch of a frame). So the scale moves by steps of 10 %, few and far between: down when the
// GPU time averaged over two seconds is over the target (as many steps as the time is expected to need, the GPU time following the pixels for
// three quarters of it), up one step when the next one is expected to stay well under the target for a few seconds, the wait doubling each time a
// rise is followed by a drop (up to a minute). The GPU time is bgfx's (RenderDevice::BGFXDesktopRenderLoop): the GPU work of the whole frame,
// all windows included. The readings are left out for the first 15 seconds of a table, while the static prepass is accumulated, and for a
// second after.

#include <atomic>

class Settings;

namespace VPX
{

class DesktopDynamicResolution final
{
public:
   static DesktopDynamicResolution& Get();

   // A table starts (Renderer's constructor, logic thread): the settings of the table, at full size
   void OnTableStart(const Settings& settings);

   // Logic thread, start of each frame (Renderer::Render3DScene): the scale of the frame being prepared. eligible: the frame can be scaled
   // (outside VR, no stereo 3D). accumulating: the static prepass (or a probe) is being accumulated. changed: the scale differs from the one of
   // the previous frame (the static prepass has to be drawn again)
   float BeginFrame(bool eligible, bool accumulating, bool& changed);

   // Render thread, once per frame of the desktop render loop: the GPU time of the last frame measured (< 0 when none), the refresh rate of the
   // playfield window or the frame rate cap if lower
   void OnFrame(float gpuMs, float refreshHz);

   bool IsEnabled() const { return m_enabled; }
   void SetEnabled(bool enabled);
   float GetTarget() const { return m_target; }
   void SetTarget(float target);
   float GetMinScale() const { return m_minScale; }
   void SetMinScale(float minScale);

   bool IsActive() const { return m_enabled && m_eligible; } // Steering the scale of the frames (for the performance panel)
   float GetScale() const { return m_scale; }
   string GetStatus() const; // One line for the menu

private:
   DesktopDynamicResolution() = default;

   void ChangeScale(float scale, double now);
   void ResetReadings(double now);

   std::atomic<bool> m_enabled = false;
   std::atomic<float> m_target = 0.85f; // GPU time aimed for, as a fraction of the frame period
   std::atomic<float> m_minScale = 0.7f;
   std::atomic<bool> m_eligible = false;
   std::atomic<bool> m_accumulating = false;
   std::atomic<float> m_scale = 1.f; // Steered by the render thread, read by the logic thread
   float m_frameScale = 1.f; // Scale of the frame the logic thread prepared last
   std::atomic<bool> m_reset = false; // A table started: the render thread starts its readings again
   std::atomic<bool> m_tableFrames = false; // The logic thread prepares the frames of the table (its warm-up starts)

   // Render thread
   std::atomic<float> m_budgetMs = 0.f;
   std::atomic<float> m_meanGpuMs = 0.f; // Over the last second
   double m_settleUntil = 0.; // Readings left out until then
   bool m_warmupPending = false;
   double m_windowStart = 0.;
   double m_windowSumMs = 0.;
   int m_windowCount = 0;
   int m_overWindows = 0, m_underWindows = 0; // Consecutive half seconds over the target, under the target with room for the next step up
   double m_lastRise = -1e9;
   float m_riseWaitS = 3.f;
   std::atomic<int> m_changes = 0;
};

}
