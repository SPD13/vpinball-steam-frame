// license:GPLv3+

// Steam Frame Edition: the routes of the web server for the backup page (assets/web/backup.html). Members of WebServer, declared in
// lib/src/WebServer.h, registered by GetLibraryRoute (steamframe/WebServerLibrary.cpp). See steamframe/Backup.h.
//
// Backup: the page asks what can be backed up (/backup-items), the selection is prepared (/backup-prepare: the checksums of the files are
// computed on a worker thread, see /backup-status), then downloaded (/backup-download), the zip being produced while it is sent.
// Restore: the page reads the zip, starts a restore (/restore-begin), asks which files are already on the device (/restore-check), uploads the
// selected entries in chunks (/restore-file), then has the library information, the scores and the settings merged (/restore-finish, on a worker
// thread, see /restore-status).

#include "core/stdafx.h"
#include "lib/src/WebServer.h"

#include "core/player.h"
#include "core/VPApp.h"
#include "steamframe/Backup.h"
#include "steamframe/Launcher.h"
#include "steamframe/RomAliases.h"
#include "steamframe/ScoreStore.h"
#include "steamframe/TableLibrary.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
   // Same as WebServer.cpp
   constexpr const char* HEADER_JSON = "Content-Type: application/json\r\n";

   constexpr const char* RESPONSE_OK = "OK";
   constexpr const char* RESPONSE_BAD_REQUEST = "Bad request";
   constexpr const char* RESPONSE_NOT_FOUND = "File not found";
   constexpr const char* RESPONSE_METHOD_NOT_ALLOWED = "Method Not Allowed";

   constexpr int STATUS_OK = 200;
   constexpr int STATUS_BAD_REQUEST = 400;
   constexpr int STATUS_NOT_FOUND = 404;
   constexpr int STATUS_METHOD_NOT_ALLOWED = 405;
   constexpr int STATUS_CONFLICT = 409;
   constexpr int STATUS_INTERNAL_SERVER_ERROR = 500;
   constexpr int STATUS_INSUFFICIENT_STORAGE = 507;

   constexpr auto IDLE_TIMEOUT = std::chrono::minutes(30); // A backup or a restore nobody asks about any more is dropped
   constexpr uintmax_t FREE_SPACE_MARGIN = uintmax_t(256) * 1024 * 1024;

   int64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

   string GenerateId()
   {
      static std::mt19937_64 generator { std::random_device {}() };
      static constexpr char HEX[] = "0123456789abcdef";
      string id;
      for (uint64_t value = generator(); id.size() < 16; value >>= 4)
         id += HEX[value & 0xF];
      return id;
   }

   std::filesystem::path UTF8ToPath(const string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

   // A table being played holds its files and saves its settings, scores and nvram when it closes (the lobby is not a table)
   bool IsTablePlayed() { return g_pplayer != nullptr && !g_pplayer->m_isLobby; }

   bool IsPost(struct mg_connection* c, struct mg_http_message* hm)
   {
      if (mg_strcmp(hm->method, mg_str("POST")) == 0)
         return true;
      mg_http_reply(c, STATUS_METHOD_NOT_ALLOWED, "", "%s", RESPONSE_METHOD_NOT_ALLOWED);
      return false;
   }

   std::optional<string> GetVar(struct mg_http_message* hm, const char* name)
   {
      if (mg_http_var(hm->query, mg_str(name)).buf == nullptr)
         return std::nullopt;
      string buffer(8192, '\0');
      const int length = mg_http_get_var(&hm->query, name, buffer.data(), buffer.size());
      if (length < 0)
         return std::nullopt;
      buffer.resize(length);
      return buffer;
   }

   std::optional<uint64_t> GetNumber(struct mg_http_message* hm, const char* name)
   {
      const std::optional<string> value = GetVar(hm, name);
      if (!value || value->empty() || value->find_first_not_of("0123456789") != string::npos || value->size() > 19)
         return std::nullopt;
      return std::stoull(*value);
   }

   std::optional<json> ParseBody(struct mg_http_message* hm)
   {
      try {
         return json::parse(hm->body.buf, hm->body.buf + hm->body.len);
      }
      catch (const std::exception&) {
         return std::nullopt;
      }
   }

   void ReplyJson(struct mg_connection* c, const json& response)
   {
      const string body = response.dump(-1, ' ', false, json::error_handler_t::replace);
      mg_http_reply(c, STATUS_OK, HEADER_JSON, "%s", body.c_str());
   }

   void ReplyError(struct mg_connection* c, int status, const string& message)
   {
      const string body = json { { "error", message } }.dump(-1, ' ', false, json::error_handler_t::replace);
      mg_http_reply(c, status, HEADER_JSON, "%s", body.c_str());
   }

   // The requests come from the page, but a malformed one (wrong JSON types...) must not stop the application: it gets an error
   template <typename Handler> void Guarded(struct mg_connection* c, const char* route, const Handler& handler)
   {
      try {
         handler();
      }
      catch (const std::exception& e) {
         PLOGE << route << ": " << e.what();
         ReplyError(c, STATUS_INTERNAL_SERVER_ERROR, e.what());
      }
   }

   string ReadFile(const fs::path& path)
   {
      std::ifstream file(path, std::ios::binary);
      std::stringstream content;
      content << file.rdbuf();
      return content.str();
   }

   void SetModifiedAt(const fs::path& path, int64_t time)
   {
      std::error_code ec;
#ifdef _MSC_VER
      fs::last_write_time(path, std::chrono::clock_cast<std::chrono::file_clock>(std::chrono::sys_seconds(std::chrono::seconds(time))), ec);
#else
      fs::last_write_time(path, std::chrono::file_clock::from_sys(std::chrono::sys_seconds(std::chrono::seconds(time))), ec);
#endif
   }

   std::optional<int64_t> GetModifiedAt(const fs::path& path)
   {
      std::error_code ec;
      const fs::file_time_type time = fs::last_write_time(path, ec);
      if (ec)
         return std::nullopt;
#ifdef _MSC_VER
      const auto sysTime = std::chrono::clock_cast<std::chrono::system_clock>(time);
#else
      const auto sysTime = std::chrono::file_clock::to_sys(time);
#endif
      return std::chrono::duration_cast<std::chrono::seconds>(sysTime.time_since_epoch()).count();
   }

   // The library is loaded by its first scan (started with the table picker): until it is over, the files can not be linked to their tables
   bool IsLibraryLoading()
   {
      VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
      if (library.IsScanning())
         return true;
      if (!library.HasScanned()) {
         library.RescanAsync();
         return true;
      }
      return false;
   }

   // The tables of the library, as the backup links files to them
   std::vector<Backup::TableInfo> GetTableInfos()
   {
      VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
      const fs::path aliasFile = library.GetAliasFilePath();
      std::vector<Backup::TableInfo> infos;
      for (const VPinballLib::Table& table : library.GetTables()) {
         Backup::TableInfo info { table.uuid, table.name, table.path, table.rom, string(), table.pupPacks };
         if (!table.rom.empty() && !aliasFile.empty())
            info.romAlias = VPinballLib::RomAliases::FindAlias(aliasFile, table.rom).value_or(""s);
         infos.push_back(std::move(info));
      }
      return infos;
   }

   ///////////////////////////////////////////////////////////////////////////////
   // Backup job: one at a time, the page polls it while the checksums are computed, then downloads it

   enum class JobState { Preparing, Ready, Failed };

   struct BackupJob
   {
      string id;
      std::shared_ptr<Backup::Archive> archive;
      std::thread worker;
      std::atomic<bool> cancel { false };
      std::atomic<JobState> state { JobState::Preparing };
      std::mutex mutex; // Protects error
      string error;
      std::unique_ptr<Backup::KeepAwake> keepAwake;
      std::chrono::steady_clock::time_point lastActivity = std::chrono::steady_clock::now();
      uint64_t lastBytesSent = 0;

      ~BackupJob()
      {
         cancel = true;
         if (worker.joinable())
            worker.join();
         Backup::UnregisterArchive(id); // Downloads in progress keep their archive
      }
   };
   std::unique_ptr<BackupJob> s_backupJob;

   ///////////////////////////////////////////////////////////////////////////////
   // Restore session: one at a time, from its beginning until the page has seen it applied

   enum class RestoreState { Receiving, Applying, Done, Failed };

   struct RestoreSession
   {
      string id;
      int64_t startedAt = Now();
      Backup::Roots roots;
      fs::path staging; // The entries merged into what the device has wait there until the restore is finished (see Backup::IsMergedEntry)
      std::unique_ptr<Backup::KeepAwake> keepAwake;
      std::thread worker;
      std::atomic<RestoreState> state { RestoreState::Receiving };
      std::atomic<bool> notified { false };
      std::mutex mutex; // Protects report and error
      std::vector<string> report;
      string error;
      std::atomic<uint64_t> files { 0 }, bytes { 0 };
      std::set<fs::path> uploadFolders; // Where the files are written until complete (see RestoreFile)
      std::chrono::steady_clock::time_point lastActivity = std::chrono::steady_clock::now();

      ~RestoreSession()
      {
         if (worker.joinable())
            worker.join();
         std::error_code ec;
         fs::remove_all(staging, ec);
         // The files of an interrupted restore that were not complete
         for (const fs::path& folder : uploadFolders) {
            std::vector<fs::path> parts;
            for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
               if (it->path().extension() == ".restore")
                  parts.push_back(it->path());
            for (const fs::path& part : parts)
               fs::remove(part, ec);
            fs::remove(folder, ec); // Only when empty
         }
      }

      void Report(const string& line)
      {
         PLOGI << "Restore: " << line;
         std::lock_guard lock(mutex);
         report.push_back(line);
      }
   };
   std::unique_ptr<RestoreSession> s_restoreSession;

   // Called on each request of the backup page: forget what is not used any more, let the device sleep once a download is over
   void Housekeeping()
   {
      const auto now = std::chrono::steady_clock::now();
      if (s_backupJob) {
         if (s_backupJob->state == JobState::Ready) { // The worker thread has laid out the archive
            const uint64_t sent = s_backupJob->archive->GetBytesSent();
            if (sent != s_backupJob->lastBytesSent) {
               s_backupJob->lastBytesSent = sent;
               s_backupJob->lastActivity = now;
            }
            if (sent >= s_backupJob->archive->GetSize())
               s_backupJob->keepAwake = nullptr;
         }
         if (s_backupJob->state == JobState::Failed)
            s_backupJob->keepAwake = nullptr;
         if (now - s_backupJob->lastActivity > IDLE_TIMEOUT && s_backupJob->state != JobState::Preparing)
            s_backupJob = nullptr;
      }
      if (s_restoreSession && now - s_restoreSession->lastActivity > IDLE_TIMEOUT && s_restoreSession->state != RestoreState::Applying) {
         PLOGW << "Restore abandoned";
         s_restoreSession = nullptr;
      }
   }

   RestoreSession* GetSession(struct mg_connection* c, struct mg_http_message* hm)
   {
      const std::optional<string> id = GetVar(hm, "session");
      if (!s_restoreSession || !id || *id != s_restoreSession->id) {
         ReplyError(c, STATUS_NOT_FOUND, "This restore is over");
         return nullptr;
      }
      s_restoreSession->lastActivity = std::chrono::steady_clock::now();
      return s_restoreSession.get();
   }

   // Where an uploaded entry goes: its place on the device, or the staging folder for the merged ones. Nothing for paths that are not acceptable.
   std::optional<fs::path> GetDestination(const RestoreSession& session, const string& zipPath, Backup::Item& item)
   {
      if (Backup::IsMergedEntry(zipPath)) {
         item = Backup::ParseItem(zipPath.substr(0, zipPath.find('/'))).value_or(Backup::Item::Settings);
         return session.staging / UTF8ToPath(zipPath);
      }
      return Backup::GetRestorePath(session.roots, zipPath, item);
   }

   // The merged entries, once all the files are in place
   void ApplyRestore(RestoreSession& session)
   {
      VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
      const fs::path& staging = session.staging;
      std::error_code ec;
      const auto has = [&staging](const char* entry) {
         std::error_code existsEc;
         return fs::is_regular_file(staging / entry, existsEc);
      };

      // ROM aliases first, as the scan of the library looks for the aliases the tables need
      if (has("aliases/pinmame/alias.txt")) {
         const string current = library.ReadAliasFile();
         const string merged = Backup::MergeAliases(current, ReadFile(staging / "aliases/pinmame/alias.txt"));
         if (merged == current)
            session.Report("ROM aliases: all the aliases of the backup were already defined");
         else {
            std::vector<VPinballLib::RomAliases::Issue> errors, warnings;
            string error;
            if (library.SaveAliasFile(merged, errors, warnings, error))
               session.Report("ROM aliases: the aliases of the backup were added to the alias file");
            else
               session.Report("ROM aliases: not restored, " + (errors.empty() ? error : "line " + std::to_string(errors.front().line) + ": " + errors.front().message));
         }
      }

      std::map<string, string> tableUuids;
      if (has("library/pref/tables.json")) {
         tableUuids = library.ImportMetadata(ReadFile(staging / "library/pref/tables.json"), has("library/pref/table-stats.json") ? ReadFile(staging / "library/pref/table-stats.json") : string(),
            session.startedAt);
         session.Report("Library: names, favorites and play counts restored for " + std::to_string(tableUuids.size()) + " table(s)");
      }
      library.Rescan(); // Registers the restored tables

      if (has("scores/pref/scores.json") || has("scores/pref/profiles.json")) {
         std::map<string, string> tablesByPath;
         for (const VPinballLib::Table& table : library.GetTables())
            tablesByPath[table.path] = table.uuid;
         const auto [profiles, scores] = g_launcher->GetScoreStore().Import(has("scores/pref/profiles.json") ? ReadFile(staging / "scores/pref/profiles.json") : string(),
            has("scores/pref/scores.json") ? ReadFile(staging / "scores/pref/scores.json") : string(), tableUuids, tablesByPath);
         session.Report("Scores: " + std::to_string(profiles) + " profile(s) and " + std::to_string(scores) + " score(s) added");
      }

      if (has("settings/pref/VPinballX.ini")) {
         string device;
         if (has("backup.json")) {
            try {
               device = json::parse(ReadFile(staging / "backup.json")).value("device", ""s);
            }
            catch (const std::exception&) {
            }
         }
         Backup::SavePendingSettings(staging / "settings/pref/VPinballX.ini", device);
         session.Report(device == Backup::GetDeviceClass() ? "Settings: they will be applied when the next table or the lobby is loaded"s
                                                           : "Settings: they will be applied when the next table or the lobby is loaded, except the ones of the display and of the rendering, as the backup was made on another kind of device"s);
      }
      fs::remove_all(staging, ec);
   }
}

///////////////////////////////////////////////////////////////////////////////
// Backup

// GET: what can be backed up, by item and by table, with their size
void WebServer::BackupItems(struct mg_connection *c, struct mg_http_message* hm)
{
   Guarded(c, "/backup-items", [&]() {
      Housekeeping();
      if (IsLibraryLoading()) {
         ReplyJson(c, { { "scanning", true } });
         return;
      }
      const Backup::Catalog catalog = Backup::Scan(Backup::Roots::OfDevice(), GetTableInfos());
      struct Total
      {
         uint64_t files = 0, bytes = 0;
      };
      Total items[static_cast<int>(Backup::Item::Count)];
      std::vector<std::map<string, uint64_t>> tableBytes(catalog.tables.size());
      std::map<string, uint64_t> otherBytes;
      for (const Backup::FileEntry& file : catalog.files) {
         Total& total = items[static_cast<int>(file.item)];
         total.files++;
         total.bytes += file.size;
         const string item = Backup::GetItemId(file.item);
         for (int index : file.tables)
            tableBytes[index][item] += file.size;
         if (file.tables.empty() && !file.global)
            otherBytes[item] += file.size;
      }
      json itemList = json::array();
      for (int i = 0; i < static_cast<int>(Backup::Item::Count); i++)
         itemList.push_back({ { "id", Backup::GetItemId(static_cast<Backup::Item>(i)) }, { "files", items[i].files }, { "bytes", items[i].bytes } });
      json tables = json::array();
      for (size_t i = 0; i < catalog.tables.size(); i++)
         tables.push_back({ { "uuid", catalog.tables[i].uuid }, { "name", catalog.tables[i].name }, { "path", catalog.tables[i].path }, { "bytes", tableBytes[i] } });
      json skipped = json::array();
      for (size_t i = 0; i < catalog.skipped.size() && i < 100; i++)
         skipped.push_back(catalog.skipped[i]);
      ReplyJson(c, { { "items", itemList }, { "tables", tables }, { "other", otherBytes }, { "skipped", skipped }, { "skippedCount", catalog.skipped.size() },
         { "playing", IsTablePlayed() }, { "device", Backup::GetDeviceClass() } });
   });
}

// POST with a JSON body {items: [ids], allTables, tables: [uuids], otherFiles}: starts computing the checksums of the selected files
void WebServer::BackupPrepare(struct mg_connection *c, struct mg_http_message* hm)
{
   Guarded(c, "/backup-prepare", [&]() {
      if (!IsPost(c, hm))
         return;
      Housekeeping();
      const std::optional<json> body = ParseBody(hm);
      if (!body || !body->is_object()) {
         mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
         return;
      }
      if (IsTablePlayed()) {
         ReplyError(c, STATUS_CONFLICT, "A table is being played: close it before making a backup");
         return;
      }
      if (IsLibraryLoading()) {
         ReplyError(c, STATUS_CONFLICT, "The table library is being scanned: try again in a moment");
         return;
      }
      Backup::Selection selection;
      try {
         for (const string& id : body->value("items", std::vector<string>()))
            if (const std::optional<Backup::Item> item = Backup::ParseItem(id))
               selection.items[static_cast<int>(*item)] = true;
         selection.allTables = body->value("allTables", true);
         selection.tables = body->value("tables", std::vector<string>());
         selection.otherFiles = body->value("otherFiles", true);
      }
      catch (const std::exception&) {
         mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
         return;
      }
      const Backup::Catalog catalog = Backup::Scan(Backup::Roots::OfDevice(), GetTableInfos());
      const std::vector<const Backup::FileEntry*> files = Backup::Select(catalog, selection);
      if (files.empty()) {
         ReplyError(c, STATUS_BAD_REQUEST, "Nothing to back up in this selection");
         return;
      }
      const int64_t createdAt = Now();
      s_backupJob = nullptr; // Cancels the previous one
      auto job = std::make_unique<BackupJob>();
      job->id = GenerateId();
      job->archive = std::make_shared<Backup::Archive>(files, Backup::BuildManifest(catalog, files, GetVersionLabel(), createdAt), createdAt);
      job->keepAwake = std::make_unique<Backup::KeepAwake>();
      BackupJob* const jobPtr = job.get();
      job->worker = std::thread([jobPtr]() {
         string error;
         if (jobPtr->archive->ComputeChecksums(jobPtr->cancel, error)) {
            Backup::RegisterArchive(jobPtr->id, jobPtr->archive);
            jobPtr->state = JobState::Ready;
            PLOGI.printf("Backup ready: %zu files, %llu MB", jobPtr->archive->GetFileCount(), static_cast<unsigned long long>(jobPtr->archive->GetSize() >> 20));
         }
         else {
            {
               std::lock_guard lock(jobPtr->mutex);
               jobPtr->error = jobPtr->cancel ? "Cancelled"s : error;
            }
            jobPtr->state = JobState::Failed;
            if (!jobPtr->cancel) {
               PLOGE << "Backup failed: " << error;
            }
         }
      });
      PLOGI.printf("Backup started: %zu files, %llu MB", files.size(), static_cast<unsigned long long>(job->archive->GetTotalBytes() >> 20));
      s_backupJob = std::move(job);
      ReplyJson(c, { { "id", s_backupJob->id }, { "files", files.size() }, { "bytes", s_backupJob->archive->GetTotalBytes() } });
   });
}

// GET with 'id'
void WebServer::BackupStatus(struct mg_connection *c, struct mg_http_message* hm)
{
   Housekeeping();
   const std::optional<string> id = GetVar(hm, "id");
   if (!s_backupJob || !id || *id != s_backupJob->id) {
      ReplyError(c, STATUS_NOT_FOUND, "This backup is no longer available");
      return;
   }
   BackupJob& job = *s_backupJob;
   job.lastActivity = std::chrono::steady_clock::now();
   const JobState state = job.state;
   string error;
   {
      std::lock_guard lock(job.mutex);
      error = job.error;
   }
   if (error.empty())
      error = job.archive->GetError(); // Read errors while it is sent
   ReplyJson(c, { { "state", state == JobState::Preparing ? "preparing" : state == JobState::Ready ? "ready" : "failed" }, { "checked", job.archive->GetCheckedBytes() },
      { "total", job.archive->GetTotalBytes() }, { "size", state == JobState::Ready ? job.archive->GetSize() : 0 }, { "sent", job.archive->GetBytesSent() }, { "error", error } });
}

// GET with 'id': the zip, produced while it is sent (resumable, mongoose handles the ranges)
void WebServer::BackupDownload(struct mg_connection *c, struct mg_http_message* hm)
{
   Housekeeping();
   const std::optional<string> id = GetVar(hm, "id");
   if (!s_backupJob || !id || *id != s_backupJob->id || s_backupJob->state != JobState::Ready) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", "%s", RESPONSE_NOT_FOUND);
      return;
   }
   s_backupJob->lastActivity = std::chrono::steady_clock::now();
   if (!s_backupJob->keepAwake)
      s_backupJob->keepAwake = std::make_unique<Backup::KeepAwake>();
   // Not named .zip, as browsers that open 'safe' files would extract it and delete it (Safari): it still is a zip
   const time_t createdAt = static_cast<time_t>(s_backupJob->archive->GetCreatedAt());
   std::tm tm {};
#ifdef _WIN32
   localtime_s(&tm, &createdAt);
#else
   localtime_r(&createdAt, &tm);
#endif
   char name[64];
   strftime(name, sizeof(name), "VPX-Backup-%Y-%m-%d-%H%M.vpxbackup", &tm);
   const string headers = "Content-Disposition: attachment; filename=\""s + name + "\"\r\nCache-Control: no-store\r\n";
   struct mg_http_serve_opts opts = {};
   opts.fs = Backup::GetArchiveFileSystem();
   opts.extra_headers = headers.c_str();
   const string path = s_backupJob->id + ".zip";
   PLOGI.printf("Backup download started: %s", name);
   mg_http_serve_file(c, hm, path.c_str(), &opts);
   Backup::AttachConnection(c);
}

// POST with 'id'
void WebServer::BackupCancel(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetVar(hm, "id");
   if (s_backupJob && id && *id == s_backupJob->id) {
      s_backupJob = nullptr;
      PLOGI.printf("Backup cancelled");
   }
   mg_http_reply(c, STATUS_OK, "", "%s", RESPONSE_OK);
}

///////////////////////////////////////////////////////////////////////////////
// Restore

// POST with a JSON body {files, bytes}: the entries the page is about to upload. Replies the id of the restore.
void WebServer::RestoreBegin(struct mg_connection *c, struct mg_http_message* hm)
{
   Guarded(c, "/restore-begin", [&]() {
      if (!IsPost(c, hm))
         return;
      Housekeeping();
      const std::optional<json> body = ParseBody(hm);
      if (!body || !body->is_object()) {
         mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
         return;
      }
      if (IsTablePlayed()) {
         ReplyError(c, STATUS_CONFLICT, "A table is being played: close it before restoring a backup");
         return;
      }
      if (s_restoreSession && s_restoreSession->state == RestoreState::Applying) {
         ReplyError(c, STATUS_CONFLICT, "Another restore is being finished");
         return;
      }
      auto session = std::make_unique<RestoreSession>();
      session->id = GenerateId();
      session->roots = Backup::Roots::OfDevice();
      session->staging = session->roots.pref / ".restore";
      const uint64_t bytes = body->value("bytes", uint64_t(0));
      std::error_code ec;
      fs::create_directories(session->roots.tables, ec);
      const fs::space_info space = fs::space(session->roots.tables, ec);
      if (!ec && space.available < bytes + FREE_SPACE_MARGIN) {
         ReplyError(c, STATUS_INSUFFICIENT_STORAGE, "Not enough free space on the device: " + std::to_string((bytes + FREE_SPACE_MARGIN) >> 20) + " MB needed, " + std::to_string(space.available >> 20) + " MB free");
         return;
      }
      s_restoreSession = nullptr; // An unfinished one
      fs::remove_all(session->staging, ec);
      fs::create_directories(session->staging, ec);
      session->keepAwake = std::make_unique<Backup::KeepAwake>();
      PLOGI.printf("Restore started: %llu files, %llu MB", static_cast<unsigned long long>(body->value("files", uint64_t(0))), static_cast<unsigned long long>(bytes >> 20));
      s_restoreSession = std::move(session);
      ReplyJson(c, { { "session", s_restoreSession->id } });
   });
}

// POST with 'session' and a JSON body {entries: [{path, size, modifiedAt}]} (in batches): replies the indexes of the entries already on the
// device (same size and date, skipped by the page, which makes an interrupted restore resumable), and of the ones that can not be restored
void WebServer::RestoreCheck(struct mg_connection *c, struct mg_http_message* hm)
{
   Guarded(c, "/restore-check", [&]() {
      if (!IsPost(c, hm))
         return;
      RestoreSession* const session = GetSession(c, hm);
      if (session == nullptr)
         return;
      const std::optional<json> body = ParseBody(hm);
      if (!body || !body->is_object() || !body->contains("entries") || !(*body)["entries"].is_array()) {
         mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
         return;
      }
      json existing = json::array(), invalid = json::array();
      int index = 0;
      for (const json& entry : (*body)["entries"]) {
         const string path = entry.value("path", ""s);
         Backup::Item item;
         const std::optional<fs::path> destination = GetDestination(*session, path, item);
         if (!destination)
            invalid.push_back(index);
         else if (!Backup::IsMergedEntry(path)) {
            std::error_code ec;
            const fs::file_status status = fs::symlink_status(*destination, ec);
            if (fs::is_regular_file(status) && fs::file_size(*destination, ec) == entry.value("size", uint64_t(0)) && !ec
               && std::abs(GetModifiedAt(*destination).value_or(0) - entry.value("modifiedAt", int64_t(0))) <= 1)
               existing.push_back(index);
         }
         index++;
      }
      ReplyJson(c, { { "existing", existing }, { "invalid", invalid } });
   });
}

// POST with 'session', 'path' (of the entry in the zip), 'offset', 'length' (of the file) and 'modifiedAt', the body being the bytes of the file
// at 'offset' (in order, less than 3 MB per request). The file is written aside, then moved in place when complete.
void WebServer::RestoreFile(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   RestoreSession* const session = GetSession(c, hm);
   if (session == nullptr)
      return;
   if (session->state != RestoreState::Receiving) {
      ReplyError(c, STATUS_CONFLICT, "This restore is being finished");
      return;
   }
   if (IsTablePlayed()) {
      ReplyError(c, STATUS_CONFLICT, "A table is being played: close it, then start the restore again (the files already restored are skipped)");
      return;
   }
   const std::optional<string> path = GetVar(hm, "path");
   const std::optional<uint64_t> offset = GetNumber(hm, "offset"), length = GetNumber(hm, "length"), modifiedAt = GetNumber(hm, "modifiedAt");
   Backup::Item item;
   const std::optional<fs::path> destination = path ? GetDestination(*session, *path, item) : std::nullopt;
   if (!destination || !offset || !length || !modifiedAt || *offset + hm->body.len > *length) {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return;
   }

   std::error_code ec;
   const fs::path staging = destination->parent_path() / ".upload";
   fs::path partPath = staging / destination->filename();
   partPath += ".restore";
   if (*offset == 0) {
      fs::create_directories(staging, ec);
      session->uploadFolders.insert(staging);
      std::ofstream(partPath, std::ios::binary | std::ios::trunc);
   }
   else if (fs::file_size(partPath, ec) != *offset || ec) {
      ReplyError(c, STATUS_CONFLICT, "Chunk out of order");
      return;
   }
   {
      std::ofstream file(partPath, std::ios::binary | std::ios::app);
      file.write(hm->body.buf, static_cast<std::streamsize>(hm->body.len));
      file.close();
      if (!file) {
         PLOGE << "Restore: failed to write " << partPath;
         ReplyError(c, STATUS_INTERNAL_SERVER_ERROR, "Failed to write the file on the device (is it full?)");
         return;
      }
   }
   const uint64_t written = *offset + hm->body.len;
   if (written == *length) {
      fs::create_directories(destination->parent_path(), ec);
      fs::rename(partPath, *destination, ec);
      if (ec) {
         PLOGE << "Restore: failed to move " << partPath << " to " << *destination << ": " << ec.message();
         ReplyError(c, STATUS_INTERNAL_SERVER_ERROR, "Failed to restore " + *path);
         return;
      }
      fs::remove(staging, ec); // Only when no other file is being written there
      SetModifiedAt(*destination, static_cast<int64_t>(*modifiedAt));
      session->files++;
      session->bytes += *length;
      if (item == Backup::Item::Roms)
         g_launcher->GetTableLibrary().OnFileAdded(PathToUTF8(destination->filename())); // It may be a missing ROM
   }
   ReplyJson(c, { { "written", written } });
}

// POST with 'session': the files are all there, the library information, the scores and the settings are merged (see /restore-status)
void WebServer::RestoreFinish(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   RestoreSession* const session = GetSession(c, hm);
   if (session == nullptr)
      return;
   if (session->state != RestoreState::Receiving) {
      ReplyError(c, STATUS_CONFLICT, "This restore is being finished");
      return;
   }
   session->state = RestoreState::Applying;
   session->Report(std::to_string(session->files.load()) + " file(s) restored (" + std::to_string(session->bytes.load() >> 20) + " MB)");
   session->worker = std::thread([session]() {
      try {
         ApplyRestore(*session);
         session->state = RestoreState::Done;
      }
      catch (const std::exception& e) {
         std::lock_guard lock(session->mutex);
         session->error = e.what();
         session->state = RestoreState::Failed;
      }
      session->keepAwake = nullptr;
   });
   mg_http_reply(c, STATUS_OK, "", "%s", RESPONSE_OK);
}

// GET with 'session'
void WebServer::RestoreStatus(struct mg_connection *c, struct mg_http_message* hm)
{
   Housekeeping();
   RestoreSession* const session = GetSession(c, hm);
   if (session == nullptr)
      return;
   const RestoreState state = session->state;
   json report = json::array();
   string error;
   {
      std::lock_guard lock(session->mutex);
      report = session->report;
      error = session->error;
   }
   // The pages and the table picker follow the restored files, once (from this thread, as the other changes made from the browser)
   if ((state == RestoreState::Done || state == RestoreState::Failed) && !session->notified.exchange(true))
      SetLastUpdate();
   ReplyJson(c, { { "state", state == RestoreState::Receiving ? "receiving" : state == RestoreState::Applying ? "applying" : state == RestoreState::Done ? "done" : "failed" },
      { "files", session->files.load() }, { "bytes", session->bytes.load() }, { "report", report }, { "error", error } });
}

// POST with 'session': stops a restore (the files already restored stay)
void WebServer::RestoreCancel(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetVar(hm, "session");
   if (s_restoreSession && id && *id == s_restoreSession->id && s_restoreSession->state != RestoreState::Applying) {
      const bool restored = s_restoreSession->files > 0;
      s_restoreSession = nullptr;
      PLOGI.printf("Restore cancelled");
      if (restored)
         SetLastUpdate();
   }
   mg_http_reply(c, STATUS_OK, "", "%s", RESPONSE_OK);
}
