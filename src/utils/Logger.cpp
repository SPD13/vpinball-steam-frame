// license:GPLv3+

#include <core/stdafx.h>
#include "Logger.h"

#include <sstream>
#include <iomanip>
#if defined(VPX_STEAM_FRAME)
#include <condition_variable>
#include <deque>
#endif

#include <plog/Init.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Appenders/RollingFileAppender.h>
#ifdef __STANDALONE__
#ifndef __ANDROID__
#include <plog/Appenders/ColorConsoleAppender.h>
#else
#include <plog/Appenders/AndroidAppender.h>
#endif
#ifdef __LIBVPINBALL__
#include "lib/src/WebServer.h"
#endif
#endif

#include "core/VPApp.h"
#include "core/player.h"
#include "ui/live/LiveUI.h"


class DebugAppender final : public plog::IAppender
{
public:
   DebugAppender()
   {
      m_uiThreadId = std::this_thread::get_id();
   }

   void write(const plog::Record &record) PLOG_OVERRIDE
   {
      // This sink writes to the debugger window script console
      if ((std::this_thread::get_id() != m_uiThreadId) || (g_pplayer == nullptr) || (g_pplayer->m_liveUI == nullptr))
         return;
      g_pplayer->m_liveUI->m_debuggerUI.AddLog(record.getMessage());
   }

private:
   std::thread::id m_uiThreadId;
};

#ifdef __LIBVPINBALL__
class WebServerAppender final : public plog::IAppender
{
public:
   void write(const plog::Record &record) PLOG_OVERRIDE
   {
      time_t rawTime = record.getTime().time;
      struct tm timeInfo;
      #ifdef _WIN32
      localtime_s(&timeInfo, &rawTime);
      #else
      localtime_r(&rawTime, &timeInfo);
      #endif

      std::string level;
      switch (record.getSeverity()) {
         case plog::fatal:   level = "FATAL"sv; break;
         case plog::error:   level = "ERROR"sv; break;
         case plog::warning: level = "WARN"sv; break;
         case plog::info:    level = "INFO"sv; break;
         case plog::debug:   level = "DEBUG"sv; break;
         case plog::verbose: level = "VERBOSE"sv; break;
         default:            level = "UNKNOWN"sv; break;
      }

      const std::string message(record.getMessage());

      char timeBuffer[32];
      snprintf(timeBuffer, std::size(timeBuffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
               timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
               timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec,
               static_cast<int>(record.getTime().millitm));

      std::stringstream ss;
      ss << timeBuffer << ' ' << std::left << std::setw(5) << level << ' ';
      ss << '[' << record.getTid() << "] ";
      ss << '[' << record.getFunc() << '@' << record.getLine() << "] ";
      ss << message;

      WebServer::LogAppender(ss.str());
   }
};
#endif

template <bool useUtcTime> class ThreadAwareTxtFormatter
{
public:
   static plog::util::nstring header() { return plog::util::nstring(); }

   static plog::util::nstring format(const plog::Record& record)
   {
      tm t;
      useUtcTime ? plog::util::gmtime_s(&t, &record.getTime().time) : plog::util::localtime_s(&t, &record.getTime().time);

      plog::util::nostringstream ss;
      ss << t.tm_year + 1900 << '-' << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_mon + 1 << PLOG_NSTR('-') << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_mday
         << PLOG_NSTR(' ');
      ss << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_hour << PLOG_NSTR(':') << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_min << PLOG_NSTR(':')
         << std::setfill(PLOG_NSTR('0')) << std::setw(2) << t.tm_sec << PLOG_NSTR('.') << std::setfill(PLOG_NSTR('0')) << std::setw(3) << static_cast<int>(record.getTime().millitm)
         << PLOG_NSTR(' ');
      ss << std::setfill(PLOG_NSTR(' ')) << std::setw(5) << std::left << severityToString(record.getSeverity()) << PLOG_NSTR(' ');
      #ifdef _WIN32
         bool logged = false;
         HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, record.getTid());
         if (hThread != nullptr)
         {
            PWSTR data;
            HRESULT hr = GetThreadDescription(hThread, &data);
            if (SUCCEEDED(hr))
            {
               if (data[0] != 0)
               {
                  ss << '[' << MakeString(data) << "] ";
                  logged = true;
               }
               LocalFree(data);
            }
            CloseHandle(hThread);
         }
         if (!logged)
            ss << PLOG_NSTR('[') << record.getTid() << PLOG_NSTR("] ");
      #else
         ss << PLOG_NSTR('[') << record.getTid() << PLOG_NSTR("] ");
      #endif
      ss << PLOG_NSTR('[') << record.getFunc() << PLOG_NSTR('@') << record.getLine() << PLOG_NSTR("] ");
      ss << record.getMessage() << PLOG_NSTR('\n');

      return ss.str();
   }
};

#if defined(VPX_STEAM_FRAME) && defined(__STANDALONE__) && !defined(__ANDROID__)
// Steam Frame Edition: the log file and the console are written by a thread of their own. They were written by the thread that logs, and
// a line written by the logic thread while it prepared a frame could wait for the disk for several milliseconds (seen on the Frame with
// the line of the controller models, 2026-11-02; a table's script prints and PuP's errors come from that thread too), which delays the
// frame. The line is formatted at once, with the time and the thread that logged it, and written shortly after. If the process crashes,
// the lines of the last moment may not be written.
class PreformattedFormatter
{
public:
   static plog::util::nstring header() { return plog::util::nstring(); }
   static plog::util::nstring format(const plog::Record& record) { return record.getMessage(); }
};

class AsyncAppender final : public plog::IAppender
{
public:
   AsyncAppender(plog::IAppender* file, plog::IAppender* console)
      : m_file(file)
      , m_console(console)
      , m_thread([this]() { Run(); })
   {
   }

   ~AsyncAppender() override
   {
      {
         std::lock_guard lock(m_mutex);
         m_stop = true;
      }
      m_wake.notify_one();
      if (m_thread.joinable())
         m_thread.join();
   }

   void write(const plog::Record& record) PLOG_OVERRIDE
   {
      Line line { record.getSeverity(), ThreadAwareTxtFormatter<false>::format(record) };
      {
         std::lock_guard lock(m_mutex);
         if (m_queue.size() < 100000)
            m_queue.push_back(std::move(line));
         else
            m_dropped++;
      }
      m_wake.notify_one();
   }

private:
   struct Line
   {
      plog::Severity severity;
      plog::util::nstring text;
   };

   void Run()
   {
      SetThreadName("LogWriter"s);
      std::deque<Line> lines;
      std::unique_lock lock(m_mutex);
      while (true)
      {
         m_wake.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
         lines.swap(m_queue);
         const size_t dropped = std::exchange(m_dropped, 0);
         const bool stop = m_stop;
         lock.unlock();
         if (dropped > 0)
            lines.push_back(Line { plog::warning, std::to_string(dropped) + " log lines dropped: the log was written faster than the disk took them\n" });
         for (const Line& line : lines)
         {
            plog::Record record(line.severity, "", 0, "", nullptr, 0);
            record << line.text.c_str();
            m_file->write(record);
            m_console->write(record);
         }
         lines.clear();
         lock.lock();
         if (stop && m_queue.empty())
            return;
      }
   }

   plog::IAppender* const m_file;
   plog::IAppender* const m_console;
   std::mutex m_mutex;
   std::condition_variable m_wake;
   std::deque<Line> m_queue;
   size_t m_dropped = 0;
   bool m_stop = false;
   std::thread m_thread; // Last: started once the members it uses exist
};

static plog::RollingFileAppender<PreformattedFormatter>* s_asyncFileAppender = nullptr;
#endif

Logger* Logger::m_pInstance = nullptr;

static plog::RollingFileAppender<ThreadAwareTxtFormatter<false>>* s_fileAppender = nullptr;

Logger* Logger::GetInstance()
{
   if (!m_pInstance)
      m_pInstance = new Logger();

   return m_pInstance;
}

void Logger::SetupLogger(const bool enable)
{
   plog::Severity maxLogSeverity = plog::none;
   if (enable)
   {
      static bool initialized = false;
      if (!initialized)
      {
         initialized = true;
         const std::filesystem::path logPath = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "vpinball.log");
#if defined(VPX_STEAM_FRAME) && defined(__STANDALONE__) && !defined(__ANDROID__)
         // Steam Frame Edition: the file and the console written by a thread of their own (see AsyncAppender), the debugger window as before
         static plog::RollingFileAppender<PreformattedFormatter> asyncFileAppender(PathToUTF8(logPath).c_str(), 1024 * 1024 * 5, 1); // Opened as UTF-8
         static plog::ColorConsoleAppender<PreformattedFormatter> asyncConsoleAppender;
         static AsyncAppender asyncAppender(&asyncFileAppender, &asyncConsoleAppender); // Destroyed first, writing what is left
         s_asyncFileAppender = &asyncFileAppender;
         static DebugAppender debugAppender;
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&debugAppender);
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&asyncAppender);
         plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->addAppender(&asyncAppender);
#else
         static plog::RollingFileAppender<ThreadAwareTxtFormatter<false>> fileAppender(PathToUTF8(logPath).c_str(), 1024 * 1024 * 5, 1); // Opened as UTF-8
         s_fileAppender = &fileAppender;
         static DebugAppender debugAppender;
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&debugAppender);
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&fileAppender);
         plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->addAppender(&fileAppender);

#ifdef __STANDALONE__
#ifndef __ANDROID__
         static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&consoleAppender);
         plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->addAppender(&consoleAppender);
#else
         static plog::AndroidAppender<plog::TxtFormatter> androidAppender("vpinball");
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&androidAppender);
         plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->addAppender(&androidAppender);
#endif
#ifdef __LIBVPINBALL__
         static WebServerAppender webServerAppender;
         plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(&webServerAppender);
         plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->addAppender(&webServerAppender);
#endif  
#endif
#endif
      }
      #ifdef _DEBUG
      maxLogSeverity = plog::debug;
      #else
      maxLogSeverity = plog::info;
      #endif
   }
   plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->setMaxSeverity(maxLogSeverity);
   plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->setMaxSeverity(maxLogSeverity);
}

void Logger::Init()
{
   plog::init<PLOG_DEFAULT_INSTANCE_ID>();
   plog::init<PLOG_NO_DBG_OUT_INSTANCE_ID>(); // Logger that does not show in the debug window to avoid duplicated messages
}

void Logger::Truncate()
{
#if defined(VPX_STEAM_FRAME) && defined(__STANDALONE__) && !defined(__ANDROID__)
   if (s_asyncFileAppender)
      s_asyncFileAppender->truncate();
#endif
   if (s_fileAppender)
      s_fileAppender->truncate();
}
