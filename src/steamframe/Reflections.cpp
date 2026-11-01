// license:GPLv3+

#include "core/stdafx.h"
#include "Reflections.h"

#include "math/matrix.h"
#include "parts/pintable.h"
#include "renderer/RenderProbe.h"

#include <atomic>
#include <bit>

namespace VPX::Reflections
{

// How far a part may reach beyond the playfield and still be taken as a part of the cabinet, in VP units: the side rails, the backbox behind it, the lockbar
// and the buttons in front of it. Height is not looked at, which keeps a topper or a tall backbox in the reflection.
static constexpr float kSideMargin = 300.f;
static constexpr float kBackMargin = 600.f;
static constexpr float kFrontMargin = 700.f;

static constexpr float kWarmupMs = 60000.f; // Tables run below their full resolution for up to half a minute after they start (see VRDevice::UpdateAutoFoveatedAntialiasing)
static constexpr int kSecondsBelow = 10;

static std::atomic<int> s_extraMirrors = On;
static std::atomic<int> s_roomReflection = On;
static std::atomic<bool> s_extraMirrorsAutoOff = false;
static std::atomic<bool> s_roomReflectionAutoOff = false;

// What the table has, seen while it is drawn: the automatic mode has nothing to decide without it, and the menu says so
static vector<const RenderProbe*> s_extraMirrors_seen;
static std::atomic<int> s_extraMirrorCount = 0;
static std::atomic<bool> s_roomPartSeen = false;

// Automatic mode, on the render thread
static std::atomic<float> s_playedMs = 0.f;
static std::atomic<uint64_t> s_secondsBelow = 0; // One bit per second of play, the latest in bit 0
static unsigned int s_frames = 0, s_framesBelow = 0; // In the current second

static bool IsExtraMirror(const RenderProbe& probe)
{
   if (probe.GetType() != RenderProbe::PLANE_REFLECTION || probe.GetName() == RenderProbe::PLAYFIELD_REFLECTION_RENDERPROBE_NAME)
      return false;
   const vec3 n = probe.GetReflectionPlaneNormal();
   return fabsf(n.z) < 0.9f * sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
}

static bool IsExtraMirrorsPending() { return s_extraMirrors == Automatic && !s_extraMirrorsAutoOff && s_extraMirrorCount > 0; }
static bool IsRoomReflectionPending() { return s_roomReflection == Automatic && !s_roomReflectionAutoOff && s_roomPartSeen; }

void Load(const Settings& settings)
{
   s_extraMirrors = settings.GetPlayer_ExtraMirrors();
   s_roomReflection = settings.GetPlayer_RoomReflection();
   s_extraMirrorsAutoOff = false;
   s_roomReflectionAutoOff = false;
   s_extraMirrors_seen.clear();
   s_extraMirrorCount = 0;
   s_roomPartSeen = false;
   s_playedMs = 0.f;
   s_secondsBelow = 0;
   s_frames = s_framesBelow = 0;
}

int GetExtraMirrorsMode() { return s_extraMirrors; }

void SetExtraMirrorsMode(int mode) { s_extraMirrors = mode; }

int GetRoomReflectionMode() { return s_roomReflection; }

void SetRoomReflectionMode(int mode) { s_roomReflection = mode; }

bool IsProbeShown(const RenderProbe& probe)
{
   if (!IsExtraMirror(probe))
      return true;
   if (std::find(s_extraMirrors_seen.begin(), s_extraMirrors_seen.end(), &probe) == s_extraMirrors_seen.end())
   {
      s_extraMirrors_seen.push_back(&probe);
      s_extraMirrorCount = static_cast<int>(s_extraMirrors_seen.size());
   }
   const int mode = s_extraMirrors;
   return mode == On || (mode == Automatic && !s_extraMirrorsAutoOff);
}

bool IsPartLeftOut(const Vertex3Ds& boxMin, const Vertex3Ds& boxMax, const Matrix3D& model, const PinTable& table)
{
   const int mode = s_roomReflection;
   if (mode == On)
      return false;
   bool beyond = false;
   for (int i = 0; i < 8 && !beyond; i++)
   {
      Vertex3Ds c((i & 1) ? boxMax.x : boxMin.x, (i & 2) ? boxMax.y : boxMin.y, (i & 4) ? boxMax.z : boxMin.z);
      model.MultiplyVector(c);
      beyond = c.x < table.m_left - kSideMargin || c.x > table.m_right + kSideMargin || c.y < table.m_top - kBackMargin || c.y > table.m_bottom + kFrontMargin;
   }
   if (!beyond)
      return false;
   s_roomPartSeen = true;
   return mode == Off || s_roomReflectionAutoOff;
}

bool OnPlayedFrame(bool belowFullResolution, float frameMs)
{
   if (frameMs <= 0.f)
      return false;
   if (!IsExtraMirrorsPending() && !IsRoomReflectionPending())
      return false;
   if (s_playedMs < kWarmupMs)
   {
      s_playedMs = s_playedMs + frameMs;
      return true;
   }
   s_frames++;
   if (belowFullResolution)
      s_framesBelow++;
   if (static_cast<float>(s_frames) * frameMs < 1000.f)
      return true;
   constexpr uint64_t lastMinute = (1ull << 60) - 1;
   s_secondsBelow = ((s_secondsBelow << 1) | (s_framesBelow * 2 > s_frames ? 1u : 0u)) & lastMinute;
   s_frames = s_framesBelow = 0;
   if (std::popcount(s_secondsBelow.load()) >= kSecondsBelow)
   {
      // One step at a time, the extra mirrors first as they cost the most, and the table gets the time to climb back before the next one
      s_secondsBelow = 0;
      if (IsExtraMirrorsPending())
      {
         s_extraMirrorsAutoOff = true;
         PLOGI << "Extra mirrors turned off (automatic): the table was below its full resolution for " << kSecondsBelow << " of the last 60 seconds";
      }
      else
      {
         s_roomReflectionAutoOff = true;
         PLOGI << "Room reflection turned off (automatic): the table was below its full resolution for " << kSecondsBelow << " of the last 60 seconds";
      }
   }
   return true;
}

static string GetAutomaticStatus(bool autoOff, const string& shown, const string& leftOut)
{
   if (autoOff)
      return leftOut + " (automatic: the table went below its full resolution)";
   if (s_playedMs < kWarmupMs)
      return shown + " (automatic: decided after the first minute of the table)";
   return std::format("{} (automatic: {} of the last 60 s below full resolution, left out from {})", shown, std::popcount(s_secondsBelow.load()), kSecondsBelow);
}

string GetExtraMirrorsStatus()
{
   const int mode = s_extraMirrors;
   if (mode == On && s_extraMirrorCount == 0)
      return "Extra mirrors: none drawn on this table"s;
   if (s_extraMirrorCount == 0)
      return "Extra mirrors: this table shows none"s;
   const string count = std::format("{} extra mirror{}", s_extraMirrorCount.load(), s_extraMirrorCount > 1 ? "s" : "");
   if (mode == On)
      return count + " drawn";
   if (mode == Off)
      return count + " left out";
   return GetAutomaticStatus(s_extraMirrorsAutoOff, count + " drawn", count + " left out");
}

string GetRoomReflectionStatus()
{
   const int mode = s_roomReflection;
   if (mode == On)
      return "Room: in the reflections"s;
   if (!s_roomPartSeen)
      return "Room: no part of it reaches the reflections on this table"s;
   if (mode == Off)
      return "Room: left out of the reflections"s;
   return GetAutomaticStatus(s_roomReflectionAutoOff, "Room: in the reflections"s, "Room: left out of the reflections"s);
}

}
