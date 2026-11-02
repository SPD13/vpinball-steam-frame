// license:GPLv3+

#include "core/stdafx.h"
#include "ThreadPlacement.h"

#if defined(__linux__)

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace VPX::ThreadPlacement
{

static std::thread s_thread;
static std::mutex s_mutex;
static std::condition_variable s_wake;
static bool s_stop = false;
static cpu_set_t s_original;

static cpu_set_t MakeSet(std::initializer_list<int> cores)
{
   cpu_set_t set;
   CPU_ZERO(&set);
   for (const int core : cores)
      CPU_SET(core, &set);
   return set;
}

static int ReadCapacity(int core)
{
   std::ifstream file("/sys/devices/system/cpu/cpu" + std::to_string(core) + "/cpu_capacity");
   int capacity = -1;
   file >> capacity;
   return file ? capacity : -1;
}

// The layout of the Steam Frame: two slow cores, then five, then the fastest
static bool IsExpectedLayout()
{
   if (std::thread::hardware_concurrency() != 8 || ReadCapacity(8) != -1)
      return false;
   int capacity[8];
   for (int core = 0; core < 8; core++)
      if ((capacity[core] = ReadCapacity(core)) <= 0)
         return false;
   if (capacity[0] * 2 >= capacity[7] || capacity[1] * 2 >= capacity[7])
      return false;
   for (int core = 2; core < 7; core++)
      if (capacity[core] * 2 < capacity[7] || capacity[core] > capacity[7])
         return false;
   return true;
}

static string ReadThreadName(const std::filesystem::path& task)
{
   std::ifstream file(task / "comm");
   string name;
   std::getline(file, name);
   return name;
}

template <typename Choose>
static void PlaceThreads(const Choose& cores)
{
   std::error_code error;
   for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task", error))
   {
      const pid_t tid = static_cast<pid_t>(atoi(entry.path().filename().c_str()));
      if (tid <= 0)
         continue;
      const cpu_set_t& wanted = cores(tid, entry.path());
      cpu_set_t current;
      // A thread may end at any time, and the next pass looks at the others again
      if (sched_getaffinity(tid, sizeof(current), &current) == 0 && !CPU_EQUAL(&current, &wanted))
         sched_setaffinity(tid, sizeof(wanted), &wanted);
   }
}

void Start(const Settings& settings)
{
   Stop();
   if (!settings.GetPlayer_ThreadPlacement())
      return;
   if (!IsExpectedLayout())
   {
      PLOGI << "Thread placement: not the core layout of the Steam Frame, left to the system";
      return;
   }
   const pid_t logicThread = static_cast<pid_t>(syscall(SYS_gettid));
   if (sched_getaffinity(logicThread, sizeof(s_original), &s_original) != 0)
      return;
   PLOGI << "Thread placement: render thread on core 7, logic thread on cores 5 and 6, the others on cores 2 to 4";
   s_stop = false;
   s_thread = std::thread([logicThread]()
      {
         SetThreadName("ThreadPlacer"s);
         const cpu_set_t logic = MakeSet({ 5, 6 });
         const cpu_set_t render = MakeSet({ 7 });
         const cpu_set_t others = MakeSet({ 2, 3, 4 });
         std::unique_lock lock(s_mutex);
         while (!s_stop)
         {
            PlaceThreads([&](pid_t tid, const std::filesystem::path& task) -> const cpu_set_t& { return tid == logicThread ? logic : ReadThreadName(task) == "RenderThread" ? render : others; });
            s_wake.wait_for(lock, std::chrono::milliseconds(250), [] { return s_stop; });
         }
      });
}

void Stop()
{
   if (!s_thread.joinable())
      return;
   {
      std::lock_guard lock(s_mutex);
      s_stop = true;
   }
   s_wake.notify_all();
   s_thread.join();
   PlaceThreads([](pid_t, const std::filesystem::path&) -> const cpu_set_t& { return s_original; });
}

}

#else

namespace VPX::ThreadPlacement
{

void Start(const Settings&) { }
void Stop() { }

}

#endif
