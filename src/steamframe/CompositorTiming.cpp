// license:GPLv3+

#include "core/stdafx.h"
#include "CompositorTiming.h"

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace VPX
{

#if defined(__linux__)
namespace
{
   // The parts of OpenVR's headers (openvr.h, BSD-3-Clause, Valve) used here. Compositor_FrameTiming as of IVRCompositor_029, packed to 4 bytes on Linux
   #pragma pack(push, 4)
   struct TrackedDevicePose
   {
      float deviceToAbsoluteTracking[3][4];
      float velocity[3];
      float angularVelocity[3];
      int32_t trackingResult;
      bool poseIsValid;
      bool deviceIsConnected;
   };
   struct FrameTiming
   {
      uint32_t size;
      uint32_t frameIndex;
      uint32_t numFramePresents; // Times this frame was presented: 0 when a newer frame replaced it, 2 or more when the next one was late
      uint32_t numMisPresented; // Times it was presented on another vsync than predicted
      uint32_t numDroppedFrames;
      uint32_t reprojectionFlags;
      double systemTimeInSeconds;
      float preSubmitGpuMs, postSubmitGpuMs, totalRenderGpuMs, compositorRenderGpuMs, compositorRenderCpuMs, compositorIdleCpuMs;
      float clientFrameIntervalMs, presentCallCpuMs, waitForPresentCpuMs, submitFrameMs;
      float waitGetPosesCalledMs, newPosesReadyMs, newFrameReadyMs, compositorUpdateStartMs, compositorUpdateEndMs, compositorRenderStartMs;
      TrackedDevicePose hmdPose;
      uint32_t numVSyncsReadyForUse;
      uint32_t numVSyncsToFirstView;
      float transferLatencyMs;
   };
   #pragma pack(pop)
   static_assert(sizeof(FrameTiming) == 188, "Compositor_FrameTiming of IVRCompositor_029");

   constexpr int kApplicationBackground = 3; // EVRApplicationType VRApplication_Background: no scene, does not start SteamVR
   constexpr size_t kGetFrameTimingsSlot = 11; // Index of GetFrameTimings in VR_IVRCompositor_FnTable (IVRCompositor_029)
   typedef intptr_t (*InitInternal2Fn)(int* error, int applicationType, const char* startupInfo);
   typedef void* (*GetGenericInterfaceFn)(const char* version, int* error);
   typedef uint32_t (*GetFrameTimingsFn)(FrameTiming* timing, uint32_t frames);
}
#endif

CompositorTiming& CompositorTiming::Get()
{
   static CompositorTiming instance;
   return instance;
}

bool CompositorTiming::Connect()
{
#if defined(__linux__)
   m_tried = true;
   // SteamVR's own copy first (the Steam Frame), then the library path
   for (const char* name : { "/opt/steamvr/bin/linuxarm64/libopenvr_api.so", "libopenvr_api.so" })
      if ((m_library = dlopen(name, RTLD_NOW | RTLD_LOCAL)) != nullptr)
         break;
   if (m_library == nullptr)
   {
      PLOGI << "Compositor frame timing: no OpenVR library (" << dlerror() << ')';
      return false;
   }
   const auto init = reinterpret_cast<InitInternal2Fn>(dlsym(m_library, "VR_InitInternal2"));
   const auto getInterface = reinterpret_cast<GetGenericInterfaceFn>(dlsym(m_library, "VR_GetGenericInterface"));
   int error = 0;
   if (init == nullptr || getInterface == nullptr || (init(&error, kApplicationBackground, nullptr), error != 0))
   {
      PLOGI << "Compositor frame timing: OpenVR not available (error " << error << ')';
      return false;
   }
   void* const table = getInterface("FnTable:IVRCompositor_029", &error);
   if (table == nullptr || error != 0)
   {
      PLOGI << "Compositor frame timing: no IVRCompositor_029 (error " << error << ')';
      return false;
   }
   m_compositor = table;
   PLOGI << "Compositor frame timing: connected to SteamVR";
   return true;
#else
   m_tried = true;
   return false;
#endif
}

bool CompositorTiming::Poll(Frames& frames)
{
   frames = Frames {};
   if (m_compositor == nullptr && (m_tried || !Connect()))
      return false;
#if defined(__linux__)
   const auto getFrameTimings = reinterpret_cast<GetFrameTimingsFn const*>(m_compositor)[kGetFrameTimingsSlot];
   // The last frames, oldest first: at 72 Hz, a poll per application frame sees 1 or 2 new ones; 16 covers a hitch of 200 ms
   FrameTiming timings[16];
   timings[0].size = sizeof(FrameTiming);
   const uint32_t count = getFrameTimings(timings, 16);
   // The last 2 frames are left for the next poll: their presents are not final yet (the latest is not shown yet, the one before is shown again
   // if the latest is late). The compositor's frame index keeps growing across applications
   for (uint32_t i = 0; i + 2 < count; i++)
   {
      const FrameTiming& timing = timings[i];
      if (timing.frameIndex <= m_lastFrameIndex)
         continue;
      m_lastFrameIndex = timing.frameIndex;
      if (timing.preSubmitGpuMs <= 0.f) // No application frame (between tables, or another scene application)
         continue;
      frames.frames++;
      if (timing.numMisPresented > 0 || timing.numFramePresents != 1)
         frames.late++;
   }
   return true;
#else
   return false;
#endif
}

}
