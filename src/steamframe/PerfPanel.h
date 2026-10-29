// license:GPLv3+

#pragma once

// Steam Frame Edition: the performance panel, a tool to tune the tables in the headset (Settings Standalone/PerfPanel, switched in the Display &
// Performance screen of the VR menu). A panel stands left of the cabinet, facing the player, with:
// - the target: the headset's native resolution at its refresh rate (2160x2160 per eye at 72 Hz on the Steam Frame);
// - the table's rating, given every 30 seconds for the 30 seconds before: green when the frames were at native resolution 95 % of the time and
//   on time, yellow when they were on time at a lower resolution (dynamic resolution), red when frames missed their display;
// - the frame rate, resolution, GPU time, GPU and CPU use (per thread), clocks and temperatures of the last 3 minutes.
//
// The data comes from three threads, none of which may use the objects of another (the VR device and the player come and go with the tables):
// - the render thread gives each frame (OnFrame, from VRDevice::UpdateDynamicResolution);
// - the logic thread gives its profiler and whether the game is played (OnLogicFrame), and draws the panel (BuildWindow and Render, from LiveUI);
// - a thread of the panel takes a sample every 0.5 s: it adds up the frames given since the last one, reads the CPU and GPU use of the
//   process from /proc and the clocks and temperatures from /sys (Linux), and gives the rating at the end of each 30 s window.
// The panel is drawn in its own OpenXR quad layer, only when a sample was taken (see VRDevice in PerfPanelLayer.cpp).

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class RenderDevice;
class RenderTarget;
class MeshBuffer;
class Player;
struct ImDrawList;

namespace VPX
{

class PerfPanel final
{
public:
   static PerfPanel& Get();
   ~PerfPanel();

   // On or off (starts or stops the sampling thread). Read from the settings when the application starts, changed by the menu
   void SetEnabled(bool enabled);
   bool IsEnabled() const { return m_enabled; }

   // Size of the panel image, in pixels, and of the panel standing in the room, in meters (about 18 pixels per degree, seen from the lockbar)
   static constexpr int kWidth = 900;
   static constexpr int kHeight = 1200;
   static constexpr float kWidthMeters = 0.72f;
   static constexpr float kHeightMeters = 0.96f;

   // Render thread, once per frame (VRDevice::UpdateDynamicResolution)
   struct FrameInfo
   {
      int periods = 1; // Display periods since the previous frame (more than 1 when frames missed their display)
      float gpuMs = -1.f; // GPU time of the application for a frame reported by the runtime (< 0 when not available)
      float scale = 1.f; // Render scale of the frame (dynamic resolution), of the swapchain size
      int eyeWidth = 0, eyeHeight = 0; // Swapchain size (the table's ResFactor)
      int nativeWidth = 0, nativeHeight = 0; // Native resolution of the headset
      float minScale = 1.f; // Floor of the dynamic resolution
      float budgetMs = 0.f; // Frame period of the display
      float targetMs = 0.f; // GPU time target of the dynamic resolution
      bool dynamicResolution = false;
   };
   void OnFrame(const FrameInfo& frame);
   // A table starts with the VR device (ApplyTableSettings): the history and the rating start again
   void OnTableStart();

   // Logic thread, once per frame (LiveUI::RenderUI): whether the game is played (not paused by the menu, not loading), and the logic profiler
   void OnLogicFrame(const Player* player);
   // Logic thread: builds the ImGui window of the panel (between ImGui::NewFrame and ImGui::Render), returns its draw list, to be left out of the
   // UI and given to Render
   ImDrawList* BuildWindow();
   // Logic thread, after ImGui::Render: renders the draw list of the panel into the panel's image, false if nothing was drawn. The mesh buffer is
   // the caller's: it must not outlive the player (its GPU buffers are released with it), while the panel lives as long as the application
   bool Render(RenderDevice* rd, RenderTarget* target, const ImDrawList* drawList, std::shared_ptr<MeshBuffer>& meshBuffer);

   // A sample was taken since the panel was last drawn: the render thread acquires an image of the panel's swapchain for the next frame
   bool IsDirty() const { return m_dirty; }
   void ClearDirty() { m_dirty = false; }

   // Thread names of the process, at most 15 characters on Linux (pthread_setname_np fails otherwise)
   static constexpr const char* kSamplerThreadName = "PerfSampler";

private:
   PerfPanel();

   std::atomic<bool> m_enabled = false;
   std::atomic<bool> m_dirty = false;

   // Sampling thread
   std::thread m_thread;
   std::mutex m_threadMutex;
   std::condition_variable m_threadWake;
   bool m_threadStop = false;
   void SamplerThread();

   // Frames given by the render thread since the last sample
   struct FrameCounters
   {
      uint64_t frames = 0, periods = 0;
      uint64_t validFrames = 0, validPeriods = 0; // Frames which count for the rating: the headset is worn and the game is played
      uint64_t nativeFrames = 0, floorFrames = 0; // Valid frames at native resolution, at the floor of the dynamic resolution
      uint64_t gpuReadings = 0;
      double gpuMsSum = 0.;
      double scaleSum = 0.;
   };
   std::mutex m_frameMutex;
   FrameCounters m_frameCounters;
   FrameInfo m_lastFrame;
   std::atomic<bool> m_tableStarted = false;

   // Given by the logic thread
   std::atomic<bool> m_playing = false;
   std::atomic<double> m_playingTime = 0.; // When m_playing was last given: older than a moment, the game is not played (table closed, loading)
   std::atomic<float> m_scriptMs = 0.f, m_physicsMs = 0.f, m_waitLogicMs = 0.f;

public:
   // One sample every 0.5 s, NaN where there is no value (the game was not played, or the value is not available on this system)
   enum Series
   {
      FPS, // Frames displayed per second
      RESOLUTION, // Rendered width, % of the native width
      GPU_MS, // Runtime GPU time per frame
      GPU_BUSY, // VPX GPU busy, %
      GPU_COMPOSITOR, // Compositor GPU busy, %
      CPU_LOGIC, CPU_RENDER, CPU_PINMAME, CPU_AUDIO, CPU_OTHER, // % of one core
      SCRIPT_MS, PHYSICS_MS,
      NUM_SERIES
   };
   static constexpr float kSampleSeconds = 0.5f;
   static constexpr int kNumSamples = 360; // 3 minutes
   static constexpr float kWindowSeconds = 30.f; // Rating window
   static constexpr int kWindowSamples = static_cast<int>(kWindowSeconds / kSampleSeconds);

   enum class Rating { None, NotEnoughPlay, Green, Yellow, Red };
   struct WindowStats
   {
      double validSeconds = 0.;
      uint64_t frames = 0, periods = 0, nativeFrames = 0, floorFrames = 0;
      double gpuMsSum = 0.;
      uint64_t gpuReadings = 0;
      float FpsRatio() const { return periods ? static_cast<float>(frames) / static_cast<float>(periods) : 0.f; }
      float NativeFraction() const { return frames ? static_cast<float>(nativeFrames) / static_cast<float>(frames) : 0.f; }
      float FloorFraction() const { return frames ? static_cast<float>(floorFrames) / static_cast<float>(frames) : 0.f; }
      float GpuMs() const { return gpuReadings ? static_cast<float>(gpuMsSum / static_cast<double>(gpuReadings)) : -1.f; }
   };
   struct RatingResult
   {
      Rating rating = Rating::None;
      string reason; // Red: GPU or CPU bound
      WindowStats stats;
   };
   // "At target": at most 2 % of the display periods without a new frame (1.4 missed frames per second at 72 Hz)
   static constexpr float kOnTimeRatio = 0.98f;
   static constexpr float kNativeFraction = 0.95f;
   static constexpr double kMinValidSeconds = 15.;
   static RatingResult Rate(const WindowStats& stats, float budgetMs, float targetMs);

private:
   using Sample = std::array<float, NUM_SERIES>;
   // Written by the sampling thread, read by the logic thread to draw
   std::mutex m_historyMutex;
   std::array<Sample, kNumSamples> m_samples {};
   std::atomic<uint64_t> m_sampleSerial = 0; // Changed with the history, which the drawing copies again then
   int m_sampleCount = 0;
   int m_nextSample = 0;
   WindowStats m_window; // Current rating window
   int m_windowSamples = 0;
   RatingResult m_lastRating;
   FrameInfo m_target; // Last frame given: target, swapchain size, budget
   struct SystemInfo
   {
      float cpuMHz[4] = { NAN, NAN, NAN, NAN };
      float gpuMHz = NAN;
      float cpuTemp = NAN, gpuTemp = NAN;
      float vpxCpu = NAN; // Whole process, % of one core
   } m_system;
   void ResetHistory();

};

}
