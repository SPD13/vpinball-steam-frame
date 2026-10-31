// license:GPLv3+

#include "core/stdafx.h"
#include "PerfPanel.h"

#include "core/player.h"
#include "renderer/IndexBuffer.h"
#include "renderer/MeshBuffer.h"
#include "renderer/RenderDevice.h"
#include "renderer/RenderPass.h"
#include "renderer/Shader.h"
#include "renderer/VertexBuffer.h"
#include "steamframe/MeshSimplifier.h"
#include "steamframe/vrui/Theme.h"
#include "imgui/imgui.h"
#include "implot/implot.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#ifdef __linux__
#include <unistd.h>
#endif

namespace VPX
{

namespace
{

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// Colors of the series (as the performance window of ARMSX2 VR) and of the ratings
constexpr ImU32 kColorFps = IM_COL32(79, 140, 255, 255);
constexpr ImU32 kColorResolution = IM_COL32(217, 140, 255, 255);
constexpr ImU32 kColorGpu = IM_COL32(70, 195, 123, 255);
constexpr ImU32 kColorCompositor = IM_COL32(144, 151, 166, 255);
constexpr ImU32 kColorLogic = IM_COL32(245, 165, 36, 255);
constexpr ImU32 kColorRender = IM_COL32(79, 140, 255, 255);
constexpr ImU32 kColorPinMAME = IM_COL32(217, 140, 255, 255);
constexpr ImU32 kColorAudio = IM_COL32(79, 209, 197, 255);
constexpr ImU32 kColorOther = IM_COL32(144, 151, 166, 255);
constexpr ImU32 kColorGreen = IM_COL32(46, 184, 92, 255);
constexpr ImU32 kColorYellow = IM_COL32(234, 179, 8, 255);
constexpr ImU32 kColorRed = IM_COL32(220, 53, 53, 255);
constexpr ImU32 kColorGrey = IM_COL32(80, 88, 104, 255);

#ifdef __linux__
string ReadFile(const string& path)
{
   std::ifstream file(path);
   if (!file)
      return string();
   std::stringstream ss;
   ss << file.rdbuf();
   return ss.str();
}

// User and system time of a /proc stat line, in clock ticks (the name in parentheses may contain spaces)
uint64_t StatTicks(const string& stat)
{
   const size_t end = stat.rfind(')');
   if (end == string::npos)
      return 0;
   std::istringstream fields(stat.substr(end + 2));
   string field;
   uint64_t utime = 0, stime = 0;
   for (int i = 0; i <= 12 && (fields >> field); i++)
   {
      if (i == 11)
         utime = std::strtoull(field.c_str(), nullptr, 10);
      else if (i == 12)
         stime = std::strtoull(field.c_str(), nullptr, 10);
   }
   return utime + stime;
}

float ReadNumber(const string& path)
{
   const string text = ReadFile(path);
   return text.empty() ? NAN : std::strtof(text.c_str(), nullptr);
}

// GPU busy time of the DRM clients of a process (/proc/<pid>/fdinfo, 'drm-engine-gpu'), as a % of the time between two readings. Clients come
// and go: only the ones present in both readings count. The file descriptors of the clients are looked for again every few seconds.
class DrmBusyReader
{
public:
   float Read(int pid, double elapsed)
   {
      if (pid <= 0)
         return NAN;
      if (m_pid != pid || --m_rescanCountdown <= 0)
      {
         if (m_pid != pid)
            m_previous.clear();
         m_pid = pid;
         m_rescanCountdown = 20;
         m_fds.clear();
         std::error_code ec;
         for (const auto& entry : std::filesystem::directory_iterator(std::format("/proc/{}/fdinfo", pid), ec))
         {
            const string info = ReadFile(entry.path().string());
            if (info.find("drm-engine-gpu") != string::npos)
               m_fds.push_back(entry.path().filename().string());
         }
      }
      std::unordered_map<string, uint64_t> current;
      for (const string& fd : m_fds)
      {
         const string info = ReadFile(std::format("/proc/{}/fdinfo/{}", pid, fd));
         string clientId;
         uint64_t ns = 0;
         bool hasEngine = false;
         std::istringstream lines(info);
         string line;
         while (std::getline(lines, line))
         {
            const size_t colon = line.find(':');
            if (colon == string::npos)
               continue;
            const string key = line.substr(0, colon);
            const char* value = line.c_str() + colon + 1;
            if (key == "drm-client-id")
               clientId = string(value + strspn(value, " \t"));
            else if (key == "drm-engine-gpu")
            {
               ns = std::strtoull(value, nullptr, 10);
               hasEngine = true;
            }
         }
         if (hasEngine && !clientId.empty())
            current[clientId] = ns;
      }
      double busyNs = 0.;
      bool any = false;
      for (const auto& [client, ns] : current)
         if (auto previous = m_previous.find(client); previous != m_previous.end() && ns >= previous->second)
         {
            busyNs += static_cast<double>(ns - previous->second);
            any = true;
         }
      const bool first = m_previous.empty();
      m_previous = std::move(current);
      if (first || !any || elapsed <= 0.)
         return first ? NAN : (m_fds.empty() ? NAN : 0.f);
      return static_cast<float>(busyNs / 1e9 / elapsed * 100.);
   }

private:
   int m_pid = 0;
   int m_rescanCountdown = 0;
   vector<string> m_fds;
   std::unordered_map<string, uint64_t> m_previous;
};

int FindProcess(const string& name)
{
   std::error_code ec;
   for (const auto& entry : std::filesystem::directory_iterator("/proc", ec))
   {
      const string pid = entry.path().filename().string();
      if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0])))
         continue;
      string comm = ReadFile(entry.path().string() + "/comm");
      while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r'))
         comm.pop_back();
      if (comm == name)
         return std::atoi(pid.c_str());
   }
   return 0;
}
#endif

}

PerfPanel& PerfPanel::Get()
{
   static PerfPanel panel;
   return panel;
}

PerfPanel::PerfPanel() { ResetHistory(); }

PerfPanel::~PerfPanel()
{
   {
      const std::lock_guard lock(m_threadMutex);
      m_threadStop = true;
   }
   m_threadWake.notify_all();
   if (m_thread.joinable())
      m_thread.join();
}

void PerfPanel::SetEnabled(bool enabled)
{
   if (enabled == m_enabled)
      return;
   if (enabled)
   {
      const std::lock_guard lock(m_historyMutex);
      ResetHistory();
   }
   m_enabled = enabled;
   if (!enabled)
      m_dirty = false;
}

void PerfPanel::OnFrame(const FrameInfo& frame)
{
   // The frame counts for the rating while the game is played (the logic thread said so a moment ago) and the headset is worn (when the
   // runtime reports the GPU time, it is under 1 ms when nothing is rendered)
   const bool valid = m_playing && Now() - m_playingTime < 0.25 && (frame.gpuMs < 0.f || frame.gpuMs >= 1.f);
   const std::lock_guard lock(m_frameMutex);
   m_lastFrame = frame;
   FrameCounters& c = m_frameCounters;
   c.frames++;
   c.periods += frame.periods;
   c.scaleSum += frame.scale;
   if (frame.gpuMs >= 1.f)
   {
      c.gpuReadings++;
      c.gpuMsSum += frame.gpuMs;
   }
   if (valid)
   {
      c.validFrames++;
      c.validPeriods += frame.periods;
      // At native resolution: the rendered width is the native width (within half a percent, the scale steps being fractional)
      if (static_cast<float>(frame.eyeWidth) * frame.scale >= static_cast<float>(frame.nativeWidth) * 0.995f)
         c.nativeFrames++;
      if (frame.dynamicResolution && frame.scale <= frame.minScale + 0.005f)
         c.floorFrames++;
   }
}

void PerfPanel::OnTableStart()
{
   m_tableStarted = true;
   if (!m_thread.joinable())
      m_thread = std::thread(&PerfPanel::SamplerThread, this);
}

void PerfPanel::OnLogicFrame(const Player* player)
{
   m_playing = player->IsPlaying(false) && !player->m_isLoading && player->GetCloseState() == Player::CS_PLAYING;
   m_playingTime = Now();
   if (!m_enabled)
      return;
   m_scriptMs = static_cast<float>(player->m_logicProfiler.GetSlidingAvg(FrameProfiler::PROFILE_SCRIPT) * 1e-3);
   m_physicsMs = static_cast<float>(player->m_logicProfiler.GetSlidingAvg(FrameProfiler::PROFILE_PHYSICS) * 1e-3);
   if (player->m_renderProfiler)
      m_waitLogicMs = static_cast<float>(player->m_renderProfiler->GetSlidingAvg(FrameProfiler::PROFILE_RENDER_WAIT) * 1e-3);
}

void PerfPanel::ResetHistory()
{
   m_sampleSerial++;
   for (Sample& sample : m_samples)
      sample.fill(NAN);
   m_sampleCount = 0;
   m_nextSample = 0;
   m_window = {};
   m_windowSamples = 0;
   m_lastRating = {};
}

PerfPanel::RatingResult PerfPanel::Rate(const WindowStats& stats, float budgetMs, float targetMs)
{
   RatingResult result;
   result.stats = stats;
   if (stats.validSeconds < kMinValidSeconds || stats.periods == 0)
   {
      result.rating = Rating::NotEnoughPlay;
      return result;
   }
   if (stats.FpsRatio() < kOnTimeRatio)
   {
      result.rating = Rating::Red;
      // Frames missed with the GPU time over the target, or the resolution at its floor, are the GPU's; with the GPU time under the target, the frames
      // were late because the CPU did not prepare them in time (logic thread, PinMAME, render thread submitting)
      const float gpuMs = stats.GpuMs();
      if (stats.FloorFraction() >= 0.5f || (gpuMs > 0.f && gpuMs > targetMs))
         result.reason = "GPU bound";
      else if (gpuMs > 0.f)
         result.reason = "CPU bound";
      return result;
   }
   result.rating = stats.NativeFraction() >= kNativeFraction ? Rating::Green : Rating::Yellow;
   return result;
}

PerfPanel::RatingResult PerfPanel::GetRunRating()
{
   const std::lock_guard lock(m_historyMutex);
   // The sampling thread has not seen the start of this table: what it holds is the previous one's
   return m_tableStarted ? RatingResult() : Rate(m_run, m_target.budgetMs, m_target.targetMs);
}

const char* PerfPanel::GetRatingName(Rating rating)
{
   switch (rating)
   {
   case Rating::Green: return "green";
   case Rating::Yellow: return "yellow";
   case Rating::Red: return "red";
   default: return "";
   }
}

PerfPanel::Rating PerfPanel::GetRatingFromName(const string& name)
{
   for (const Rating rating : { Rating::Green, Rating::Yellow, Rating::Red })
      if (name == GetRatingName(rating))
         return rating;
   return Rating::None;
}

uint32_t PerfPanel::GetRatingColor(Rating rating)
{
   switch (rating)
   {
   case Rating::Green: return kColorGreen;
   case Rating::Yellow: return kColorYellow;
   case Rating::Red: return kColorRed;
   default: return kColorGrey;
   }
}

uint32_t PerfPanel::GetRatingTextColor(Rating rating) { return rating == Rating::Yellow ? IM_COL32(66, 32, 6, 255) : IM_COL32(255, 255, 255, 255); }

void PerfPanel::SamplerThread()
{
   SetThreadName(kSamplerThreadName);
   double last = Now();
   #ifdef __linux__
   const int pid = getpid();
   const double tck = static_cast<double>(sysconf(_SC_CLK_TCK));
   std::unordered_map<int, uint64_t> previousTicks;
   uint64_t previousProcessTicks = 0;
   int compositorPid = 0;
   int compositorLookup = 0;
   DrmBusyReader vpxGpu, compositorGpu;
   // Clocks and temperatures, looked for once
   vector<string> cpuFreqPaths, cpuThermalPaths, gpuThermalPaths;
   string gpuFreqPath;
   {
      std::error_code ec;
      vector<string> policies;
      for (const auto& entry : std::filesystem::directory_iterator("/sys/devices/system/cpu/cpufreq", ec))
         if (entry.path().filename().string().rfind("policy", 0) == 0)
            policies.push_back(entry.path().string() + "/scaling_cur_freq");
      std::sort(policies.begin(), policies.end());
      for (size_t i = 0; i < policies.size() && i < 4; i++)
         cpuFreqPaths.push_back(policies[i]);
      for (const auto& entry : std::filesystem::directory_iterator("/sys/class/devfreq", ec))
         if (entry.path().filename().string().find("gpu") != string::npos && gpuFreqPath.empty())
            gpuFreqPath = entry.path().string() + "/cur_freq";
      for (const auto& entry : std::filesystem::directory_iterator("/sys/class/thermal", ec))
      {
         const string type = ReadFile(entry.path().string() + "/type");
         if (type.rfind("gpuss", 0) == 0)
            gpuThermalPaths.push_back(entry.path().string() + "/temp");
         else if (type.rfind("cpu", 0) == 0)
            cpuThermalPaths.push_back(entry.path().string() + "/temp");
      }
   }
   #endif

   while (true)
   {
      {
         std::unique_lock lock(m_threadMutex);
         m_threadWake.wait_for(lock, std::chrono::duration<float>(kSampleSeconds), [this] { return m_threadStop; });
         if (m_threadStop)
            return;
      }
      const double now = Now();
      const double elapsed = now - last;
      last = now;

      FrameCounters frames;
      FrameInfo frame;
      {
         const std::lock_guard lock(m_frameMutex);
         frames = m_frameCounters;
         m_frameCounters = {};
         frame = m_lastFrame;
      }

      Sample sample;
      sample.fill(NAN);
      if (frames.frames > 0)
      {
         sample[FPS] = static_cast<float>(static_cast<double>(frames.frames) / elapsed);
         if (frame.nativeWidth > 0)
            sample[RESOLUTION] = static_cast<float>(frames.scaleSum / static_cast<double>(frames.frames)) * static_cast<float>(frame.eyeWidth) / static_cast<float>(frame.nativeWidth) * 100.f;
         if (frames.gpuReadings > 0)
            sample[GPU_MS] = static_cast<float>(frames.gpuMsSum / static_cast<double>(frames.gpuReadings));
      }
      if (m_playing && now - m_playingTime < 0.25)
      {
         sample[SCRIPT_MS] = m_scriptMs;
         sample[PHYSICS_MS] = m_physicsMs;
      }

      SystemInfo system;
      #ifdef __linux__
      // Readings of the panel: next time it is shown, they start again rather than giving the average of the time it was not
      if (!m_enabled)
      {
         previousTicks.clear();
         previousProcessTicks = 0;
         vpxGpu = {};
         compositorGpu = {};
      }
      else
      {
         // CPU per thread of the process, grouped
         float cpu[5] = { 0.f, 0.f, 0.f, 0.f, 0.f }; // Logic, render, PinMAME, audio, other
         std::unordered_map<int, uint64_t> ticks;
         std::error_code ec;
         for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", ec))
         {
            const int tid = std::atoi(entry.path().filename().string().c_str());
            const string stat = ReadFile(entry.path().string() + "/stat");
            if (tid <= 0 || stat.empty())
               continue;
            const uint64_t t = StatTicks(stat);
            ticks[tid] = t;
            const auto previous = previousTicks.find(tid);
            if (previous == previousTicks.end() || t < previous->second)
               continue;
            const float percent = static_cast<float>(static_cast<double>(t - previous->second) / tck / elapsed * 100.);
            string comm = ReadFile(entry.path().string() + "/comm");
            while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r'))
               comm.pop_back();
            if (tid == pid)
               cpu[0] += percent; // The main thread runs the game loop
            else if (comm == "RenderThread")
               cpu[1] += percent;
            else if (comm == "PinMAME")
               cpu[2] += percent;
            else if (comm.rfind("SDLAudio", 0) == 0)
               cpu[3] += percent;
            else if (comm != kSamplerThreadName)
               cpu[4] += percent;
         }
         const bool firstReading = previousTicks.empty();
         previousTicks = std::move(ticks);
         if (!firstReading)
         {
            sample[CPU_LOGIC] = cpu[0];
            sample[CPU_RENDER] = cpu[1];
            sample[CPU_PINMAME] = cpu[2];
            sample[CPU_AUDIO] = cpu[3];
            sample[CPU_OTHER] = cpu[4];
         }
         const uint64_t processTicks = StatTicks(ReadFile("/proc/self/stat"));
         if (previousProcessTicks && processTicks >= previousProcessTicks)
            system.vpxCpu = static_cast<float>(static_cast<double>(processTicks - previousProcessTicks) / tck / elapsed * 100.);
         previousProcessTicks = processTicks;

         // Compositor, looked for again every 10 s while not found
         if (compositorPid == 0 && --compositorLookup <= 0)
         {
            compositorPid = FindProcess("vrcompositor");
            compositorLookup = 20;
         }
         if (compositorPid != 0)
         {
            const string stat = ReadFile(std::format("/proc/{}/stat", compositorPid));
            if (stat.empty())
               compositorPid = 0;
            else
            {
               sample[GPU_COMPOSITOR] = compositorGpu.Read(compositorPid, elapsed);
            }
         }
         sample[GPU_BUSY] = vpxGpu.Read(pid, elapsed);

         for (size_t i = 0; i < cpuFreqPaths.size(); i++)
            system.cpuMHz[i] = ReadNumber(cpuFreqPaths[i]) / 1000.f; // kHz
         if (!gpuFreqPath.empty())
            system.gpuMHz = ReadNumber(gpuFreqPath) / 1e6f; // Hz
         for (const string& path : cpuThermalPaths)
            if (const float t = ReadNumber(path) / 1000.f; !std::isnan(t) && (std::isnan(system.cpuTemp) || t > system.cpuTemp))
               system.cpuTemp = t;
         for (const string& path : gpuThermalPaths)
            if (const float t = ReadNumber(path) / 1000.f; !std::isnan(t) && (std::isnan(system.gpuTemp) || t > system.gpuTemp))
               system.gpuTemp = t;
      }
      #endif

      {
         const std::lock_guard lock(m_historyMutex);
         if (m_tableStarted.exchange(false))
         {
            ResetHistory();
            m_run = {};
            m_startupSeconds = 0.;
         }
         m_sampleSerial++;
         m_samples[m_nextSample] = sample;
         m_nextSample = (m_nextSample + 1) % kNumSamples;
         m_sampleCount = min(m_sampleCount + 1, kNumSamples);
         m_system = system;
         if (frame.nativeWidth > 0)
            m_target = frame;
         // Rating window and whole run: the valid frames only (headset worn, game played), after the first seconds of the table
         const bool rated = m_startupSeconds >= kStartupSeconds;
         if (frame.budgetMs > 0.f)
            m_startupSeconds += static_cast<double>(frames.validPeriods) * frame.budgetMs / 1000.;
         if (rated)
            for (WindowStats* stats : { &m_window, &m_run })
            {
               stats->frames += frames.validFrames;
               stats->periods += frames.validPeriods;
               stats->nativeFrames += frames.nativeFrames;
               stats->floorFrames += frames.floorFrames;
               if (frame.budgetMs > 0.f)
                  stats->validSeconds += static_cast<double>(frames.validPeriods) * frame.budgetMs / 1000.;
               if (frames.validFrames > 0 && frames.gpuReadings > 0)
               {
                  stats->gpuMsSum += frames.gpuMsSum;
                  stats->gpuReadings += frames.gpuReadings;
               }
            }
         if (++m_windowSamples >= kWindowSamples)
         {
            m_lastRating = Rate(m_window, frame.budgetMs, frame.targetMs);
            m_window = {};
            m_windowSamples = 0;
         }
      }
      m_dirty = m_enabled.load();
   }
}

ImDrawList* PerfPanel::BuildWindow()
{
   if (!m_enabled)
      return nullptr;

   // Copy of what is drawn, taken from the sampling thread when it took a new sample (the window is built every frame, see LiveUI::RenderUI),
   // with the series in time order, the last sample at 0 s
   static std::array<std::array<float, kNumSamples>, NUM_SERIES> series;
   static std::array<float, kNumSamples> times;
   static int count = 0, windowSamples = 0;
   static WindowStats window;
   static bool startup = false;
   static RatingResult rating;
   static FrameInfo target;
   static SystemInfo system;
   static uint64_t copiedSerial = ~0ull;
   if (copiedSerial != m_sampleSerial)
   {
      const std::lock_guard lock(m_historyMutex);
      copiedSerial = m_sampleSerial;
      startup = m_startupSeconds < kStartupSeconds;
      count = m_sampleCount;
      for (int i = 0; i < count; i++)
      {
         const Sample& sample = m_samples[(m_nextSample - count + i + kNumSamples) % kNumSamples];
         for (int s = 0; s < NUM_SERIES; s++)
            series[s][i] = sample[s];
         times[i] = -static_cast<float>(count - 1 - i) * kSampleSeconds;
      }
      window = m_window;
      windowSamples = m_windowSamples;
      rating = m_lastRating;
      target = m_target;
      system = m_system;
   }
   const auto last = [&](int s) -> float
   {
      // Mean of the last 2 seconds, NaN when none
      float sum = 0.f;
      int n = 0;
      for (int i = max(0, count - 4); i < count; i++)
         if (!std::isnan(series[s][i]))
         {
            sum += series[s][i];
            n++;
         }
      return n ? sum / static_cast<float>(n) : NAN;
   };
   const auto fmt = [](float v, const char* format) { return std::isnan(v) ? string("-") : std::vformat(format, std::make_format_args(v)); };

   ImFont* const font = VRUI::Theme::Font() ? VRUI::Theme::Font() : ImGui::GetFont();
   ImFont* const boldFont = VRUI::Theme::BoldFont() ? VRUI::Theme::BoldFont() : font;
   constexpr float kText = 28.f, kSmall = 25.f, kValue = 36.f, kTitle = 30.f, kBadge = 58.f;
   constexpr float kPad = 22.f;

   ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
   ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight)));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, kPad));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
   ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 4.f));
   ImGui::Begin("##PerfPanel", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus
         | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
   ImDrawList* const drawList = ImGui::GetWindowDrawList();
   const ImVec2 origin = ImGui::GetWindowPos();
   drawList->AddRectFilled(origin, origin + ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight)), IM_COL32(16, 19, 26, 255), 20.f);
   const float contentWidth = static_cast<float>(kWidth) - 2.f * kPad;
   const auto rightAligned = [&](const string& text, ImU32 color)
   {
      ImGui::SameLine(ImGui::GetCursorStartPos().x + contentWidth - ImGui::CalcTextSize(text.c_str()).x);
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(text.c_str());
      ImGui::PopStyleColor();
   };

   // Rating of the last window, the target and the current window
   {
      const char* label;
      ImU32 color;
      switch (rating.rating)
      {
      case Rating::Green: label = "GREEN"; color = kColorGreen; break;
      case Rating::Yellow: label = "YELLOW"; color = kColorYellow; break;
      case Rating::Red: label = "RED"; color = kColorRed; break;
      case Rating::NotEnoughPlay: label = "NO RATING"; color = kColorGrey; break;
      default: label = "RATING..."; color = kColorGrey; break;
      }
      const ImVec2 badgePos = ImGui::GetCursorScreenPos();
      const ImVec2 badgeSize(290.f, 150.f);
      drawList->AddRectFilled(badgePos, badgePos + badgeSize, color, 14.f);
      ImGui::PushFont(boldFont, rating.rating == Rating::NotEnoughPlay || rating.rating == Rating::None ? kTitle : kBadge);
      const ImVec2 labelSize = ImGui::CalcTextSize(label);
      drawList->AddText(badgePos + ImVec2((badgeSize.x - labelSize.x) * 0.5f, 22.f), IM_COL32(255, 255, 255, 255), label);
      ImGui::PopFont();
      ImGui::PushFont(font, kSmall);
      string detail;
      if (rating.rating == Rating::Red)
         detail = rating.reason.empty() ? "Frames missed" : rating.reason;
      else if (rating.rating == Rating::Yellow)
         detail = "On time, below native";
      else if (rating.rating == Rating::Green)
         detail = "Native, on time";
      else if (rating.rating == Rating::NotEnoughPlay)
         detail = "Less than 15 s played";
      else
         detail = "First rating after 30 s";
      const ImVec2 detailSize = ImGui::CalcTextSize(detail.c_str());
      drawList->AddText(badgePos + ImVec2((badgeSize.x - detailSize.x) * 0.5f, badgeSize.y - detailSize.y - 12.f), IM_COL32(255, 255, 255, 230), detail.c_str());
      ImGui::PopFont();

      // Right of the badge: target, numbers of the rated window, current window
      ImGui::SetCursorScreenPos(badgePos + ImVec2(badgeSize.x + 18.f, 0.f));
      ImGui::BeginGroup();
      ImGui::PushFont(boldFont, kText);
      const float refreshHz = target.budgetMs > 0.f ? 1000.f / target.budgetMs : 0.f;
      ImGui::TextUnformatted(target.nativeWidth > 0 ? std::format("Target {}x{} @ {:.0f} Hz", target.nativeWidth, target.nativeHeight, refreshHz).c_str() : "Target: waiting for the headset");
      ImGui::PopFont();
      ImGui::PushFont(font, kSmall);
      ImGui::PushStyleColor(ImGuiCol_Text, VRUI::Theme::kTextMuted);
      if (rating.rating == Rating::Green || rating.rating == Rating::Yellow || rating.rating == Rating::Red)
      {
         const WindowStats& s = rating.stats;
         ImGui::TextUnformatted(std::format("Native {:.0f} % of the time, floor {:.0f} %", s.NativeFraction() * 100.f, s.FloorFraction() * 100.f).c_str());
         ImGui::TextUnformatted(std::format("On time {:.1f} %, {} frames missed", s.FpsRatio() * 100.f, s.periods - s.frames).c_str());
         ImGui::TextUnformatted(s.GpuMs() > 0.f ? std::format("GPU {:.1f} ms (target {:.1f} ms)", s.GpuMs(), target.targetMs).c_str() : "GPU time not reported");
      }
      else
      {
         ImGui::TextUnformatted("Rates the last 30 s: green = native");
         ImGui::TextUnformatted("95 % of the time, yellow = lower");
         ImGui::TextUnformatted("resolution, red = frames missed");
      }
      const int secondsLeft = static_cast<int>(ceilf(static_cast<float>(kWindowSamples - windowSamples) * kSampleSeconds));
      ImGui::TextUnformatted(window.periods > 0
            ? std::format("In {} s: {:.0f} % native, {:.1f} % on time", secondsLeft, window.NativeFraction() * 100.f, window.FpsRatio() * 100.f).c_str()
            : std::format("In {} s ({})", secondsLeft, startup ? "table starting" : "not played").c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::EndGroup();
      ImGui::SetCursorScreenPos(badgePos + ImVec2(0.f, badgeSize.y + 8.f));
      ImGui::PushFont(font, kSmall);
      // Settings that keep the table from reaching the target
      ImGui::PushStyleColor(ImGuiCol_Text, kColorYellow);
      if (target.nativeWidth > 0 && target.eyeWidth < target.nativeWidth)
         ImGui::TextUnformatted(std::format("Render resolution {}x{} is below native: green is out of reach", target.eyeWidth, target.eyeHeight).c_str());
      else if (target.nativeWidth > 0 && !target.dynamicResolution)
         ImGui::TextUnformatted("Dynamic resolution is off");
      else
         ImGui::NewLine();
      ImGui::PopStyleColor();
      // The performance features of the fork (Steam Frame screen of the menu): foveated rendering (left), simplified meshes (right)
      static constexpr const char* kFoveationLevels[] = { "off", "Low", "Medium", "High" };
      const string foveation = target.foveationLevel < 0 ? "Foveation: not supported"s
         : target.foveationLevel == 0                    ? "Foveation off"s
                                                         : std::format("Foveation {}, {}", kFoveationLevels[min(target.foveationLevel, 3)],
                                                              target.foveationEyeTracked ? "eye-tracked" : target.foveationFixedLow ? "fixed (Low profile)" : "fixed");
      ImGui::PushStyleColor(ImGuiCol_Text, target.foveationLevel > 0 ? VRUI::Theme::kText : VRUI::Theme::kTextMuted);
      ImGui::TextUnformatted(foveation.c_str());
      ImGui::PopStyleColor();
      const MeshSimplifier& simplifier = MeshSimplifier::Get();
      const float saved = simplifier.GetSavedShare();
      const string meshes = simplifier.GetSimplifiedPartCount() == 0 ? "Meshes: none simplified"s
         : !simplifier.IsEnabled()                                  ? "Simplified meshes off"s
         : std::isnan(saved)                                        ? "Meshes: measuring"s
                                                                    : std::format("Meshes -{:.0f} % triangles", saved * 100.f);
      rightAligned(meshes, simplifier.IsEnabled() && simplifier.GetSimplifiedPartCount() > 0 ? VRUI::Theme::kText : VRUI::Theme::kTextMuted);
      ImGui::PopFont();
   }

   // Graphs of the last 3 minutes
   ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.f, 4.f));
   ImPlot::PushStyleVar(ImPlotStyleVar_LegendPadding, ImVec2(2.f, 2.f));
   ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
   ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImGui::ColorConvertU32ToFloat4(IM_COL32(28, 33, 44, 255)));
   ImPlot::PushStyleColor(ImPlotCol_LegendBg, ImVec4(0.f, 0.f, 0.f, 0.f));
   ImPlot::PushStyleColor(ImPlotCol_LegendBorder, ImVec4(0.f, 0.f, 0.f, 0.f));
   const auto section = [&](const char* title, const string& value, ImU32 valueColor)
   {
      ImGui::PushFont(boldFont, kTitle);
      ImGui::TextUnformatted(title);
      ImGui::PopFont();
      ImGui::PushFont(boldFont, kValue);
      rightAligned(value, valueColor);
      ImGui::PopFont();
   };
   const auto line = [&](const char* label, int s, ImU32 color, float weight = 2.f)
   {
      ImPlotSpec spec;
      spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
      spec.LineWeight = weight;
      ImPlot::PlotLine(label, times.data(), series[s].data(), count, spec);
   };
   const auto reference = [&](const char* label, float value, ImU32 color)
   {
      ImPlotSpec spec;
      spec.LineColor = ImGui::ColorConvertU32ToFloat4(color);
      spec.LineWeight = 1.f;
      spec.Flags = static_cast<int>(ImPlotInfLinesFlags_Horizontal) | static_cast<int>(ImPlotItemFlags_NoLegend);
      ImPlot::PlotInfLines(label, &value, 1, spec);
   };
   const auto beginPlot = [&](const char* id, float height, float yMax, bool legend) -> bool
   {
      ImGui::PushFont(font, kSmall);
      if (!ImPlot::BeginPlot(id, ImVec2(-1.f, height), ImPlotFlags_NoInputs | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle | (legend ? 0 : ImPlotFlags_NoLegend)))
      {
         ImGui::PopFont();
         return false;
      }
      ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoDecorations);
      ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoLabel);
      ImPlot::SetupAxisLimits(ImAxis_X1, -static_cast<double>(kNumSamples - 1) * kSampleSeconds, 0., ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0., yMax, ImPlotCond_Always);
      if (legend)
         ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
      return true;
   };
   const auto endPlot = [&]()
   {
      ImPlot::EndPlot();
      ImGui::PopFont();
   };
   constexpr float kPlotHeight = 136.f;

   const float refreshHz = target.budgetMs > 0.f ? 1000.f / target.budgetMs : 72.f;
   const float fps = last(FPS);
   section("Frame rate", fmt(fps, "{:.0f} fps"), (!std::isnan(fps) && fps < refreshHz * kOnTimeRatio) ? kColorRed : kColorFps);
   if (beginPlot("##fps", kPlotHeight, ceilf(refreshHz * 1.15f / 10.f) * 10.f, false))
   {
      reference("##refresh", refreshHz, VRUI::Theme::kTextMuted);
      line("Headset", FPS, kColorFps);
      endPlot();
   }

   const float resolution = last(RESOLUTION);
   const string resolutionText = std::isnan(resolution) || target.nativeWidth <= 0
      ? string("-")
      : std::format("{}x{}  {:.0f} %", static_cast<int>(lroundf(static_cast<float>(target.nativeWidth) * resolution / 100.f)),
         static_cast<int>(lroundf(static_cast<float>(target.nativeHeight) * resolution / 100.f)), resolution);
   section("Resolution (of native)", resolutionText, kColorResolution);
   if (beginPlot("##resolution", kPlotHeight, 130.f, false))
   {
      reference("##native", 100.f, VRUI::Theme::kTextMuted);
      if (target.dynamicResolution && target.nativeWidth > 0)
         reference("##floor", target.minScale * static_cast<float>(target.eyeWidth) / static_cast<float>(target.nativeWidth) * 100.f, kColorRed);
      line("Rendered", RESOLUTION, kColorResolution);
      endPlot();
   }

   const float gpuMs = last(GPU_MS);
   section("GPU", fmt(gpuMs, "{:.1f} ms") + (std::isnan(last(GPU_BUSY)) ? string() : std::format("  {:.0f} %", last(GPU_BUSY))),
      (!std::isnan(gpuMs) && gpuMs > target.targetMs && target.targetMs > 0.f) ? kColorYellow : kColorGpu);
   const float budgetMs = target.budgetMs > 0.f ? target.budgetMs : 13.9f;
   if (beginPlot("##gpu", kPlotHeight, ceilf(budgetMs * 1.3f), true))
   {
      // GPU time per frame (ms) against the budget and the target of the dynamic resolution
      reference("##budget", budgetMs, kColorRed);
      if (target.targetMs > 0.f)
         reference("##target", target.targetMs, VRUI::Theme::kTextMuted);
      line("ms per frame", GPU_MS, kColorGpu);
      endPlot();
   }
   if (beginPlot("##gpubusy", kPlotHeight * 0.7f, 100.f, true))
   {
      line("VPX %", GPU_BUSY, kColorGpu);
      line("Compositor %", GPU_COMPOSITOR, kColorCompositor, 1.5f);
      endPlot();
   }

   section("CPU (% of a core)", std::isnan(system.vpxCpu) ? string("-") : std::format("VPX {:.0f} %", system.vpxCpu), kColorLogic);
   if (beginPlot("##cpu", kPlotHeight, 100.f, true))
   {
      line("Logic", CPU_LOGIC, kColorLogic);
      line("Render", CPU_RENDER, kColorRender);
      line("PinMAME", CPU_PINMAME, kColorPinMAME);
      line("Audio", CPU_AUDIO, kColorAudio, 1.5f);
      line("Other", CPU_OTHER, kColorOther, 1.5f);
      endPlot();
   }
   ImPlot::PopStyleColor(4);
   ImPlot::PopStyleVar(2);

   // Footer: the logic thread's script and physics, clocks and temperatures
   ImGui::PushFont(font, kSmall);
   ImGui::PushStyleColor(ImGuiCol_Text, VRUI::Theme::kTextMuted);
   ImGui::TextUnformatted(std::format("Script {} ms, physics {} ms, render waits {} ms for logic", fmt(last(SCRIPT_MS), "{:.1f}"), fmt(last(PHYSICS_MS), "{:.1f}"),
      fmt(m_waitLogicMs.load(), "{:.1f}")).c_str());
   string clocks = "CPU MHz";
   for (const float mhz : system.cpuMHz)
      if (!std::isnan(mhz))
         clocks += std::format(" {:.0f}", mhz);
   ImGui::TextUnformatted(std::format("{}, GPU {} MHz, {} °C CPU, {} °C GPU", clocks, fmt(system.gpuMHz, "{:.0f}"), fmt(system.cpuTemp, "{:.0f}"), fmt(system.gpuTemp, "{:.0f}")).c_str());
   ImGui::PopStyleColor();
   ImGui::PopFont();

   ImGui::End();
   ImGui::PopStyleVar(3);
   return drawList;
}

// Same rendering as LiveUI::RenderUI, into the panel's image with a plain orthographic projection
bool PerfPanel::Render(RenderDevice* rd, RenderTarget* target, const ImDrawList* drawList, std::shared_ptr<MeshBuffer>& meshBuffer)
{
   return RenderDrawList(rd, target, drawList, meshBuffer, "PerfPanel"s);
}

bool PerfPanel::RenderDrawList(RenderDevice* rd, RenderTarget* target, const ImDrawList* drawList, std::shared_ptr<MeshBuffer>& meshBuffer, const string& name)
{
   if (target == nullptr || drawList == nullptr || drawList->IdxBuffer.empty() || rd->GetCurrentPass() == nullptr)
      return false;
   RenderTarget* const previous = rd->GetCurrentPass()->m_rt;
   rd->SetRenderTarget(name, target, false, true);
   rd->Clear(clearType::TARGET, 0x00000000);

   const unsigned int numVertices = drawList->VtxBuffer.size();
   const unsigned int numIndices = drawList->IdxBuffer.size();
   if (meshBuffer == nullptr || meshBuffer->m_ib->m_count < numIndices || meshBuffer->m_vb->m_count < numVertices)
   {
      auto ib = std::make_shared<IndexBuffer>(rd, max(meshBuffer ? meshBuffer->m_ib->m_count : 0, numIndices), true, IndexBuffer::Format::FMT_INDEX32);
      auto vb = std::make_shared<VertexBuffer>(rd, max(meshBuffer ? meshBuffer->m_vb->m_count : 0, numVertices), nullptr, true);
      meshBuffer = std::make_shared<MeshBuffer>(name, vb, ib, false);
   }
   Vertex3D_NoTex2* vb;
   meshBuffer->m_vb->Lock(vb);
   for (unsigned int i = 0; i < numVertices; i++)
   {
      const uint32_t rgba = drawList->VtxBuffer[i].col;
      vb[i].x = drawList->VtxBuffer[i].pos.x;
      vb[i].y = drawList->VtxBuffer[i].pos.y;
      vb[i].z = (float)((rgba >> 24) & 0xFFu) * (float)(1.0 / 255.0); // alpha
      vb[i].nx = (float)(rgba & 0x000000FFu) * (float)(1.0 / 255.0); // red
      vb[i].ny = (float)(rgba & 0x0000FF00u) * (float)(1.0 / 65280.0); // green
      vb[i].nz = (float)(rgba & 0x00FF0000u) * (float)(1.0 / 16711680.0); // blue
      vb[i].tu = drawList->VtxBuffer[i].uv.x;
      vb[i].tv = drawList->VtxBuffer[i].uv.y;
   }
   meshBuffer->m_vb->Unlock();
   uint32_t* ib;
   meshBuffer->m_ib->Lock(ib);
   memcpy(ib, drawList->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx));
   meshBuffer->m_ib->Unlock();

   const Matrix3D matView = Matrix3D::MatrixOrthoOffCenterRH(0.f, static_cast<float>(target->GetWidth()), static_cast<float>(target->GetHeight()), 0.f, 0.f, 1.f);
   rd->m_uiShader->SetMatrix(ShaderUniform::matWorldView, &matView, 1);
   rd->ResetRenderState();
   rd->SetRenderState(RenderState::COLORWRITEENABLE, RenderState::RGBMASK_RGBA);
   rd->SetRenderState(RenderState::ALPHABLENDENABLE, RenderState::RS_TRUE);
   rd->SetRenderState(RenderState::BLENDOP, RenderState::BLENDOP_ADD);
   rd->SetRenderState(RenderState::SRCBLEND, RenderState::SRC_ALPHA);
   rd->SetRenderState(RenderState::DESTBLEND, RenderState::INVSRC_ALPHA);
   rd->SetRenderState(RenderState::CULLMODE, RenderState::CULL_NONE);
   rd->SetRenderState(RenderState::ZWRITEENABLE, RenderState::RS_FALSE);
   rd->SetRenderState(RenderState::ZENABLE, RenderState::RS_FALSE);
   rd->m_uiShader->SetTechnique(ShaderTechnique::LiveUI_mono);
   int depthSort = -10000;
   for (const ImDrawCmd* cmd = drawList->CmdBuffer.begin(), *cmdEnd = drawList->CmdBuffer.end(); cmd != cmdEnd; cmd++)
   {
      if (cmd->ElemCount == 0 || cmd->UserCallback != nullptr)
         continue;
      rd->m_uiShader->SetVector(ShaderUniform::clip_plane, cmd->ClipRect.x, cmd->ClipRect.y, cmd->ClipRect.z, cmd->ClipRect.w);
      rd->m_uiShader->SetTexture(ShaderUniform::tex_base_color, cmd->GetTexID(), SamplerFilter::SF_BILINEAR);
      // UI shader parameters: x = no stereo offset, y = texture is sRGB decoded, w = no HDR conversion (see LiveUI::RenderUI)
      const vec4 uiParams(0.f, (cmd->GetTexID() != nullptr && cmd->GetTexID()->IsSRGB()) ? 1.f : 0.f, 0.f, 0.f);
      rd->m_uiShader->SetVector(ShaderUniform::staticColor_Alpha, &uiParams);
      rd->DrawMesh(rd->m_uiShader, true, Vertex3Ds(0.f, 0.f, 0.f), static_cast<float>(depthSort), meshBuffer, RenderDevice::TRIANGLELIST, cmd->IdxOffset, cmd->ElemCount);
      depthSort--;
   }
   // Back to the target of the UI: the passes of a frame are ordered from its last one (RenderFrame::Execute), which must stay the display's
   rd->SetRenderTarget("LiveUI"s, previous, true, true);
   return true;
}

}
