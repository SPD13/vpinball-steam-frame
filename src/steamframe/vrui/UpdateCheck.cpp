// license:GPLv3+

#include "core/stdafx.h"
#include "UpdateCheck.h"

#include "core/VPApp.h"

#include <nlohmann/json.hpp>

#include <regex>
#include <thread>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace VPX::VRUI::UpdateCheck
{

namespace
{
   constexpr const char* kReleaseApi = "https://api.github.com/repos/SPD13/vpinball-steam-frame/releases/latest";

   // Written by the worker thread. Never destroyed: the thread may still run when the application exits.
   struct State
   {
      std::atomic<bool> started = false;
      std::atomic<bool> updateAvailable = false;
      std::mutex mutex;
      string latestVersion;
   };
   State* const g_state = new State();

   // The few functions of libcurl used, loaded from the library of the system
   struct Curl
   {
      using Handle = void;
      Handle* (*easyInit)() = nullptr;
      int (*easySetopt)(Handle*, int, ...) = nullptr;
      int (*easyPerform)(Handle*) = nullptr;
      int (*easyGetinfo)(Handle*, int, ...) = nullptr;
      void (*easyCleanup)(Handle*) = nullptr;
      const char* (*easyStrerror)(int) = nullptr;

      // Values of curl.h, stable across versions of libcurl
      static constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptWriteFunction = 20011, kOptTimeout = 13,
                           kOptFollowLocation = 52, kOptNoSignal = 99, kInfoResponseCode = 0x200002;

      bool Load()
      {
#ifdef _WIN32
         return false;
#else
   #ifdef __APPLE__
         static constexpr const char* names[] = { "/usr/lib/libcurl.4.dylib" };
   #else
         static constexpr const char* names[] = { "libcurl.so.4", "libcurl-gnutls.so.4" };
   #endif
         void* library = nullptr;
         for (const char* name : names)
            if ((library = dlopen(name, RTLD_NOW | RTLD_LOCAL)) != nullptr)
               break;
         if (library == nullptr)
            return false;
         easyInit = reinterpret_cast<decltype(easyInit)>(dlsym(library, "curl_easy_init"));
         easySetopt = reinterpret_cast<decltype(easySetopt)>(dlsym(library, "curl_easy_setopt"));
         easyPerform = reinterpret_cast<decltype(easyPerform)>(dlsym(library, "curl_easy_perform"));
         easyGetinfo = reinterpret_cast<decltype(easyGetinfo)>(dlsym(library, "curl_easy_getinfo"));
         easyCleanup = reinterpret_cast<decltype(easyCleanup)>(dlsym(library, "curl_easy_cleanup"));
         easyStrerror = reinterpret_cast<decltype(easyStrerror)>(dlsym(library, "curl_easy_strerror"));
         return easyInit && easySetopt && easyPerform && easyGetinfo && easyCleanup && easyStrerror; // The library stays loaded
#endif
      }
   };

   size_t AppendBody(const char* data, size_t size, size_t count, void* body)
   {
      static_cast<string*>(body)->append(data, size * count);
      return size * count;
   }

   // The body of a GET request, or an error message
   std::optional<string> Get(const char* url, string& error)
   {
      static Curl curl;
      static const bool loaded = curl.Load();
      if (!loaded)
      {
         error = "libcurl not found";
         return std::nullopt;
      }
      Curl::Handle* const handle = curl.easyInit();
      if (handle == nullptr)
      {
         error = "libcurl failed to start";
         return std::nullopt;
      }
      string body;
      curl.easySetopt(handle, Curl::kOptUrl, url);
      curl.easySetopt(handle, Curl::kOptUserAgent, "vpx-steam-frame"); // GitHub's API refuses requests without one
      curl.easySetopt(handle, Curl::kOptWriteFunction, &AppendBody);
      curl.easySetopt(handle, Curl::kOptWriteData, &body);
      curl.easySetopt(handle, Curl::kOptTimeout, 20L);
      curl.easySetopt(handle, Curl::kOptFollowLocation, 1L);
      curl.easySetopt(handle, Curl::kOptNoSignal, 1L); // Not on the main thread
      const int result = curl.easyPerform(handle);
      long status = 0;
      curl.easyGetinfo(handle, Curl::kInfoResponseCode, &status);
      curl.easyCleanup(handle);
      if (result != 0)
      {
         error = curl.easyStrerror(result);
         return std::nullopt;
      }
      if (status != 200)
      {
         error = "HTTP status " + std::to_string(status);
         return std::nullopt;
      }
      return body;
   }

   // Versions are "<yyyymmdd>-<commit>" (development builds may add a suffix): a release of a later day is newer, and so is another
   // release of the same day (made again from a newer commit)
   bool IsNewer(const string& latest, const string& installed)
   {
      const auto date = [](const string& version) { return version.size() >= 8 && std::all_of(version.begin(), version.begin() + 8, ::isdigit) ? version.substr(0, 8) : string(); };
      const string latestDate = date(latest), installedDate = date(installed);
      if (latestDate.empty() || installedDate.empty())
         return latest != installed;
      return latestDate > installedDate || (latestDate == installedDate && latest != installed);
   }

   void Check(const string& installed)
   {
      string error;
      const std::optional<string> body = Get(kReleaseApi, error);
      if (!body)
      {
         PLOGW << "Update check: the latest release could not be read from GitHub (" << error << ')';
         return;
      }
      // The game archive of the release names its version (see make-release.sh)
      string latest;
      try
      {
         static const std::regex archiveName(R"(^vpx-steam-frame-(.+)\.tar\.gz$)");
         const nlohmann::json release = nlohmann::json::parse(*body);
         for (const auto& asset : release.at("assets"))
            if (std::smatch match; asset.contains("name") && std::regex_match(asset.at("name").get_ref<const string&>(), match, archiveName))
               latest = match[1];
      }
      catch (const std::exception& e)
      {
         PLOGW << "Update check: unexpected answer from GitHub (" << e.what() << ')';
         return;
      }
      if (latest.empty())
      {
         PLOGW << "Update check: the latest release has no game archive";
         return;
      }
      const bool newer = IsNewer(latest, installed);
      PLOGI << "Update check: installed " << installed << ", latest release " << latest << (newer ? ": an update is available" : ": up to date");
      {
         std::lock_guard lock(g_state->mutex);
         g_state->latestVersion = latest;
      }
      g_state->updateAvailable = newer;
   }
}

void Start()
{
   if (g_state->started.exchange(true))
      return;
   string installed;
   if (const char* forced = getenv("VPX_UPDATE_CHECK_VERSION"); forced && *forced)
      installed = forced;
   else
   {
      std::ifstream file(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Root, "VERSION"));
      std::getline(file, installed);
      while (!installed.empty() && std::isspace(static_cast<unsigned char>(installed.back())))
         installed.pop_back();
   }
   if (installed.empty())
   {
      PLOGI << "Update check: no VERSION file next to the executable (development build), not checked";
      return;
   }
   std::thread(Check, installed).detach();
}

bool IsUpdateAvailable() { return g_state->updateAvailable; }

string GetLatestVersion()
{
   std::lock_guard lock(g_state->mutex);
   return g_state->latestVersion;
}

}
