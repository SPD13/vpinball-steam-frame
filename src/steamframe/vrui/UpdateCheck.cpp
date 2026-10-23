// license:GPLv3+

#include "core/stdafx.h"
#include "UpdateCheck.h"

#include "core/VPApp.h"

#include <mongoose/mongoose.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_process.h>

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
   // Free space needed next to the game folder: the archive, and the new game folder beside the running one (as the installer app asks)
   constexpr uintmax_t kNeededFreeSpace = 1536ull * 1024 * 1024;

   // Written by the worker threads. Never destroyed: a thread may still run when the application exits.
   struct State
   {
      std::atomic<bool> started = false;
      std::atomic<bool> updateAvailable = false;
      std::atomic<InstallPhase> installPhase = InstallPhase::None;
      std::atomic<float> downloadProgress = 0.f;
      std::mutex mutex;
      string latestVersion;
      // The game archive of the latest release
      string assetUrl;
      uint64_t assetSize = 0;
      string assetSha256; // Lowercase hex, empty when GitHub gives none
      string installMessage;
   };
   State* const g_state = new State();

   // The few functions of libcurl used, loaded from the library of the system
   struct Curl
   {
      using Handle = void;
      using WriteFunction = size_t (*)(const char*, size_t, size_t, void*);
      Handle* (*easyInit)() = nullptr;
      int (*easySetopt)(Handle*, int, ...) = nullptr;
      int (*easyPerform)(Handle*) = nullptr;
      int (*easyGetinfo)(Handle*, int, ...) = nullptr;
      void (*easyCleanup)(Handle*) = nullptr;
      const char* (*easyStrerror)(int) = nullptr;

      // Values of curl.h, stable across versions of libcurl
      static constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptWriteFunction = 20011, kOptTimeout = 13,
                           kOptLowSpeedLimit = 19, kOptLowSpeedTime = 20, kOptFollowLocation = 52, kOptConnectTimeout = 78, kOptNoSignal = 99,
                           kInfoResponseCode = 0x200002;

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

   // A GET request whose body goes to the write function. A small answer has a timeout; a download only fails when it stalls.
   bool Request(const char* url, Curl::WriteFunction write, void* data, bool download, string& error)
   {
      static Curl curl;
      static const bool loaded = curl.Load();
      if (!loaded)
      {
         error = "libcurl not found";
         return false;
      }
      Curl::Handle* const handle = curl.easyInit();
      if (handle == nullptr)
      {
         error = "libcurl failed to start";
         return false;
      }
      curl.easySetopt(handle, Curl::kOptUrl, url);
      curl.easySetopt(handle, Curl::kOptUserAgent, "vpx-steam-frame"); // GitHub's API refuses requests without one
      curl.easySetopt(handle, Curl::kOptWriteFunction, write);
      curl.easySetopt(handle, Curl::kOptWriteData, data);
      if (download)
      {
         curl.easySetopt(handle, Curl::kOptConnectTimeout, 20L);
         curl.easySetopt(handle, Curl::kOptLowSpeedLimit, 1024L); // Bytes per second, for 30 seconds
         curl.easySetopt(handle, Curl::kOptLowSpeedTime, 30L);
      }
      else
         curl.easySetopt(handle, Curl::kOptTimeout, 20L);
      curl.easySetopt(handle, Curl::kOptFollowLocation, 1L); // Release assets are served from another host
      curl.easySetopt(handle, Curl::kOptNoSignal, 1L); // Not on the main thread
      const int result = curl.easyPerform(handle);
      long status = 0;
      curl.easyGetinfo(handle, Curl::kInfoResponseCode, &status);
      curl.easyCleanup(handle);
      if (result != 0)
      {
         error = curl.easyStrerror(result);
         return false;
      }
      if (status != 200)
      {
         error = "HTTP status " + std::to_string(status);
         return false;
      }
      return true;
   }

   size_t AppendBody(const char* data, size_t size, size_t count, void* body)
   {
      static_cast<string*>(body)->append(data, size * count);
      return size * count;
   }

   // The body of a GET request, or an error message
   std::optional<string> Get(const char* url, string& error)
   {
      string body;
      if (!Request(url, &AppendBody, &body, false, error))
         return std::nullopt;
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

   // The game folder an update replaces: the folder of the executable in the Frame build, or the test folder. None elsewhere.
   std::optional<std::filesystem::path> GetInstallFolder()
   {
      if (const char* forced = getenv("VPX_UPDATE_INSTALL_DIR"); forced && *forced)
         return std::filesystem::path(forced).lexically_normal();
#ifdef VPX_STEAM_FRAME_HEADSET
      std::error_code ec;
      std::filesystem::path folder = std::filesystem::weakly_canonical(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Root), ec);
      if (ec)
         return std::nullopt;
      if (!folder.has_filename()) // Trailing separator
         folder = folder.parent_path();
      return folder;
#else
      return std::nullopt;
#endif
   }

   // The folders used beside the game folder while it is replaced: the download and its unpacked files, the new game folder, and the
   // replaced one (kept until the next start, as the running game uses its files)
   std::filesystem::path Sibling(const std::filesystem::path& folder, const char* suffix) { return folder.parent_path() / (folder.filename().string() + suffix); }

   void RemoveLeftovers(const std::filesystem::path& folder, bool withOld)
   {
      for (const char* suffix : { ".update", ".new", ".old" })
      {
         if (!withOld && strcmp(suffix, ".old") == 0)
            continue;
         const std::filesystem::path path = Sibling(folder, suffix);
         std::error_code ec;
         if (!std::filesystem::exists(path, ec))
            continue;
         std::filesystem::remove_all(path, ec);
         if (ec)
            PLOGW << "Update: " << path << " could not be removed (" << ec.message() << ')';
         else
            PLOGI << "Update: removed " << path;
      }
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
      string latest, url, sha256;
      uint64_t size = 0;
      try
      {
         static const std::regex archiveName(R"(^vpx-steam-frame-(.+)\.tar\.gz$)");
         const nlohmann::json release = nlohmann::json::parse(*body);
         for (const auto& asset : release.at("assets"))
            if (std::smatch match; asset.contains("name") && std::regex_match(asset.at("name").get_ref<const string&>(), match, archiveName))
            {
               latest = match[1];
               url = asset.value("browser_download_url", ""s);
               size = asset.value("size", uint64_t(0));
               // "sha256:<hex>"
               if (const auto digest = asset.find("digest"); digest != asset.end() && digest->is_string() && digest->get_ref<const string&>().starts_with("sha256:"))
                  sha256 = lowerCase(digest->get_ref<const string&>().substr(7));
            }
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
         g_state->assetUrl = url;
         g_state->assetSize = size;
         g_state->assetSha256 = sha256;
      }
      g_state->updateAvailable = newer;
   }

   // The archive being downloaded, hashed as it arrives
   struct Download
   {
      FILE* file = nullptr;
      uint64_t written = 0;
      uint64_t expected = 0;
      mg_sha256_ctx sha256;
   };

   size_t WriteDownload(const char* data, size_t size, size_t count, void* userData)
   {
      Download& download = *static_cast<Download*>(userData);
      const size_t length = size * count;
      if (fwrite(data, 1, length, download.file) != length)
         return 0; // Makes libcurl fail the transfer
      mg_sha256_update(&download.sha256, reinterpret_cast<const unsigned char*>(data), length);
      download.written += length;
      if (download.expected > 0)
         g_state->downloadProgress = min(1.f, static_cast<float>(static_cast<double>(download.written) / static_cast<double>(download.expected)));
      return length;
   }

   // Runs a command and waits for it, its output going to the log of the system
   bool Run(const vector<string>& command, string& error)
   {
      vector<const char*> argv;
      for (const string& arg : command)
         argv.push_back(arg.c_str());
      argv.push_back(nullptr);
      SDL_Process* const process = SDL_CreateProcess(argv.data(), false);
      if (process == nullptr)
      {
         error = command[0] + " could not be started ("s + SDL_GetError() + ')';
         return false;
      }
      int exitCode = -1;
      SDL_WaitProcess(process, true, &exitCode);
      SDL_DestroyProcess(process);
      if (exitCode != 0)
      {
         error = command[0] + " failed (exit code " + std::to_string(exitCode) + ')';
         return false;
      }
      return true;
   }

   // Downloads, checks and unpacks the release, then replaces the game folder: an error message on failure
   std::optional<string> InstallUpdate(const std::filesystem::path& folder)
   {
      string version, url, sha256;
      uint64_t size;
      {
         std::lock_guard lock(g_state->mutex);
         version = g_state->latestVersion;
         url = g_state->assetUrl;
         size = g_state->assetSize;
         sha256 = g_state->assetSha256;
      }
      PLOGI << "Update: installing " << version << " into " << folder << " from " << url;
      RemoveLeftovers(folder, false);
      const std::filesystem::path work = Sibling(folder, ".update"), fresh = Sibling(folder, ".new"), old = Sibling(folder, ".old");
      std::error_code ec;
      std::filesystem::create_directories(work, ec);
      if (ec)
         return "the download folder could not be created"s;
      if (const std::filesystem::space_info space = std::filesystem::space(work, ec); !ec && space.available < kNeededFreeSpace)
         return "not enough free space (1.5 GB needed)"s;

      // Download
      const std::filesystem::path archive = work / "release.tar.gz";
      Download download;
      download.expected = size;
      mg_sha256_init(&download.sha256);
      download.file = fopen(archive.string().c_str(), "wb");
      if (download.file == nullptr)
         return "the download could not be written"s;
      string error;
      const bool downloaded = Request(url.c_str(), &WriteDownload, &download, true, error);
      const bool closed = fclose(download.file) == 0;
      if (!downloaded)
         return "download failed (" + error + ')';
      if (!closed)
         return "the download could not be written"s;
      if (size > 0 && download.written != size)
         return "incomplete download"s;
      if (!sha256.empty())
      {
         unsigned char digest[32];
         mg_sha256_final(digest, &download.sha256);
         string hex;
         for (const unsigned char byte : digest)
            hex += std::format("{:02x}", byte);
         if (hex != sha256)
            return "the download is damaged (SHA-256 does not match)"s;
      }
      else
         PLOGW << "Update: GitHub gives no SHA-256 for the archive, only its size is checked";

      // Unpack. tar keeps the symbolic links (library aliases) and the permissions of the files.
      g_state->installPhase = InstallPhase::Installing;
      if (!Run({ "tar"s, "-xzf"s, archive.string(), "-C"s, work.string() }, error))
         return "the archive could not be unpacked: " + error;
      std::filesystem::remove(archive, ec);
      const std::filesystem::path game = work / ("vpx-steam-frame-" + version) / "game";
      if (!std::filesystem::exists(game / "VPinballX_BGFX", ec) || !std::filesystem::exists(game / "run.sh", ec))
         return "the archive holds no game"s;

      // Replace the game folder. The running game keeps its files open: they are deleted with the old folder at the next start.
      std::filesystem::rename(game, fresh, ec);
      if (ec)
         return "the new game folder could not be made ("s + ec.message() + ')';
      for (const char* executable : { "run.sh", "VPinballX_BGFX" })
         std::filesystem::permissions(fresh / executable, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add, ec);
      const bool replacing = std::filesystem::exists(folder, ec);
      if (replacing)
      {
         std::filesystem::remove_all(old, ec);
         std::filesystem::rename(folder, old, ec);
         if (ec)
            return "the game folder could not be replaced ("s + ec.message() + ')';
      }
      std::filesystem::rename(fresh, folder, ec);
      if (ec)
      {
         const string reason = ec.message();
         if (replacing)
            std::filesystem::rename(old, folder, ec);
         return "the game folder could not be replaced (" + reason + ')';
      }
      std::filesystem::remove_all(work, ec);
      PLOGI << "Update: " << version << " installed, it runs from the next start";
      return std::nullopt;
   }

   void Install()
   {
      std::optional<string> failure = "this build cannot install updates"s;
      if (const std::optional<std::filesystem::path> folder = GetInstallFolder(); folder)
      {
         try
         {
            failure = InstallUpdate(*folder);
         }
         catch (const std::exception& e)
         {
            failure = e.what();
         }
         if (failure)
            RemoveLeftovers(*folder, false);
      }
      if (failure)
      {
         PLOGE << "Update: install failed, " << *failure;
         std::lock_guard lock(g_state->mutex);
         g_state->installMessage = *failure;
      }
      g_state->installPhase = failure ? InstallPhase::Failed : InstallPhase::Done;
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
      PLOGI << "Update check: no VERSION file next to the executable (development build), not checked";
   // The folders left by an update (the replaced game folder) or by an interrupted one are deleted first, on the worker thread as it takes
   // a while
   std::thread([installed]()
      {
         if (const std::optional<std::filesystem::path> folder = GetInstallFolder(); folder)
            RemoveLeftovers(*folder, true);
         if (!installed.empty())
            Check(installed);
      }).detach();
}

bool IsUpdateAvailable() { return g_state->updateAvailable; }

string GetLatestVersion()
{
   std::lock_guard lock(g_state->mutex);
   return g_state->latestVersion;
}

bool CanInstall()
{
   if (!g_state->updateAvailable || !GetInstallFolder())
      return false;
   std::lock_guard lock(g_state->mutex);
   return !g_state->assetUrl.empty();
}

void StartInstall()
{
   if (!CanInstall())
      return;
   InstallPhase phase = g_state->installPhase;
   do
   {
      if (phase != InstallPhase::None && phase != InstallPhase::Failed)
         return;
   } while (!g_state->installPhase.compare_exchange_weak(phase, InstallPhase::Downloading));
   g_state->downloadProgress = 0.f;
   std::thread(Install).detach();
}

InstallStatus GetInstallStatus()
{
   InstallStatus status { g_state->installPhase, g_state->downloadProgress, {} };
   if (status.phase == InstallPhase::Failed)
   {
      std::lock_guard lock(g_state->mutex);
      status.message = g_state->installMessage;
   }
   return status;
}

}
