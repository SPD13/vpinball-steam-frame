// license:GPLv3+

#include "TableLibrary.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using std::string;
using namespace std::string_literals;

namespace VPinballLib {

namespace {

constexpr const char* IMPORT_FOLDER = ".import"; // Hidden scratch folder inside the tables folder, so that imported folders are moved instead of copied
constexpr const char* const SIDECAR_EXTENSIONS[] = { ".ini", ".vbs", ".directb2s", ".png", ".jpg" };

string ToLower(string value)
{
   std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return value;
}

bool HasExtension(const fs::path& path, const char* ext) { return ToLower(path.extension().string()) == ext; }

// Dot files cover our scratch folder, partial downloads and the '._xxx.vpx' resource forks found in zips made on macOS
bool IsHidden(const fs::path& path)
{
   const string name = path.filename().string();
   return name.empty() || name[0] == '.' || name == "__MACOSX";
}

int64_t Now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::optional<int64_t> FileModifiedAt(const fs::path& path)
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

bool Exists(const fs::path& path)
{
   std::error_code ec;
   return fs::exists(path, ec);
}

string NameFromStem(const fs::path& path)
{
   string name = path.stem().string();
   std::replace(name.begin(), name.end(), '_', ' ');
   return name;
}

bool SameTable(const Table& a, const Table& b)
{
   return a.uuid == b.uuid && a.name == b.name && a.path == b.path && a.image == b.image && a.createdAt == b.createdAt && a.modifiedAt == b.modifiedAt
      && a.lastPlayedAt == b.lastPlayedAt && a.playCount == b.playCount && a.favorite == b.favorite && a.usesPup == b.usesPup && a.pupPacks == b.pupPacks
      && a.pupScannedAt == b.pupScannedAt && a.hasPupPack == b.hasPupPack;
}

}

TableLibrary::TableLibrary(Config config)
   : m_config(std::move(config))
{
}

TableLibrary::~TableLibrary()
{
   if (m_worker.joinable())
      m_worker.join();
}

void TableLibrary::Log(LogLevel level, const string& message) const
{
   if (m_config.log)
      m_config.log(level, message);
}

///////////////////////////////////////////////////////////////////////////////
// Paths

fs::path TableLibrary::BuildPath(const string& relativePath) const { return m_config.tablesPath / fs::path(relativePath); }

string TableLibrary::RelativePath(const fs::path& fullPath) const
{
   const fs::path relative = fullPath.lexically_normal().lexically_relative(m_config.tablesPath.lexically_normal());
   if (relative.empty() || *relative.begin() == "..")
      return fullPath.generic_string();
   return relative.generic_string();
}

// tables.json can be edited or uploaded by the user: never follow a path that leaves the tables folder
bool TableLibrary::IsInsideTables(const fs::path& fullPath) const
{
   const fs::path relative = fullPath.lexically_normal().lexically_relative(m_config.tablesPath.lexically_normal());
   return !relative.empty() && relative != "." && *relative.begin() != "..";
}

bool TableLibrary::IsExcluded(const fs::path& fullPath) const
{
   const fs::path normalized = fullPath.lexically_normal();
   return std::any_of(m_config.excludedPaths.begin(), m_config.excludedPaths.end(), [&](const fs::path& excluded) { return excluded.lexically_normal() == normalized; });
}

bool TableLibrary::IsArchive(const fs::path& path) const
{
   return std::any_of(m_config.archiveExtensions.begin(), m_config.archiveExtensions.end(), [&path](const string& ext) { return HasExtension(path, ext.c_str()); });
}

std::vector<fs::path> TableLibrary::ListTableFiles(const fs::path& folder, int settleSeconds) const
{
   std::vector<fs::path> files;
   const int64_t now = Now();
   std::error_code ec;
   for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const fs::path& path = it->path();
      if (it->is_directory(ec)) {
         if (IsHidden(path) || IsExcluded(path))
            it.disable_recursion_pending();
         continue;
      }
      if (IsHidden(path) || !HasExtension(path, ".vpx") || !it->is_regular_file(ec))
         continue;
      if (settleSeconds > 0 && now - FileModifiedAt(path).value_or(0) < settleSeconds)
         continue;
      files.push_back(path);
   }
   std::sort(files.begin(), files.end());
   return files;
}

string TableLibrary::FindImage(const string& tablePath) const
{
   for (const char* ext : { ".png", ".jpg" }) {
      const fs::path imagePath = BuildPath(tablePath).replace_extension(ext);
      if (Exists(imagePath))
         return RelativePath(imagePath);
   }
   return string();
}

string TableLibrary::SanitizeName(const string& name)
{
   static const string invalidChars = " _/\\:*?\"<>|.&'()"s;
   string sanitized;
   for (const char c : name) {
      const bool invalid = invalidChars.find(c) != string::npos;
      if (!invalid)
         sanitized += c;
      else if (!sanitized.empty() && sanitized.back() != '-')
         sanitized += '-';
   }
   while (!sanitized.empty() && sanitized.back() == '-')
      sanitized.pop_back();
   return sanitized.empty() ? "table"s : sanitized;
}

string TableLibrary::GetDefaultName(const Table& table) { return NameFromStem(fs::path(table.path)); }

string TableLibrary::GetUniqueFolder(const string& baseName) const
{
   const string sanitized = SanitizeName(baseName);
   string candidate = sanitized;
   for (int counter = 2; Exists(BuildPath(candidate)); counter++)
      candidate = sanitized + '-' + std::to_string(counter);
   return candidate;
}

///////////////////////////////////////////////////////////////////////////////
// Table list

string TableLibrary::GenerateUUID(const std::vector<Table>& tables) const
{
   thread_local std::mt19937_64 generator { std::random_device {}() };
   static constexpr char digits[] = "0123456789abcdef";
   for (;;) {
      uint8_t bytes[16];
      for (int i = 0; i < 16; i += 8) {
         const uint64_t value = generator();
         for (int j = 0; j < 8; j++)
            bytes[i + j] = static_cast<uint8_t>(value >> (8 * j));
      }
      bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
      bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 1
      string uuid;
      for (int i = 0; i < 16; i++) {
         if (i == 4 || i == 6 || i == 8 || i == 10)
            uuid += '-';
         uuid += digits[bytes[i] >> 4];
         uuid += digits[bytes[i] & 0x0F];
      }
      if (std::none_of(tables.begin(), tables.end(), [&](const Table& table) { return table.uuid == uuid; }))
         return uuid;
   }
}

Table TableLibrary::CreateTable(const fs::path& fullPath, const std::vector<Table>& tables) const
{
   Table table;
   table.uuid = GenerateUUID(tables);
   table.name = NameFromStem(fullPath);
   table.path = RelativePath(fullPath);
   table.image = FindImage(table.path);
   table.createdAt = table.modifiedAt = Now();
   return table;
}

std::vector<Table> TableLibrary::GetTables(bool ascending) const
{
   std::vector<Table> tables;
   {
      std::lock_guard lock(m_mutex);
      tables = m_tables;
   }
   std::sort(tables.begin(), tables.end(), [ascending](const Table& a, const Table& b) {
      const string nameA = ToLower(a.name), nameB = ToLower(b.name);
      if (nameA != nameB)
         return ascending ? nameA < nameB : nameA > nameB;
      return a.uuid < b.uuid;
   });
   return tables;
}

std::optional<Table> TableLibrary::GetTable(const string& uuid) const
{
   std::lock_guard lock(m_mutex);
   const auto it = std::find_if(m_tables.begin(), m_tables.end(), [&](const Table& table) { return table.uuid == uuid; });
   return it != m_tables.end() ? std::optional<Table>(*it) : std::nullopt;
}

// Writers are serialized by m_operationMutex, so they may read m_tables freely: m_mutex is only held for the
// swap to keep GetTables (called by the UI each frame) from waiting on file operations
void TableLibrary::Commit(std::vector<Table> tables)
{
   {
      std::lock_guard lock(m_mutex);
      m_tables = std::move(tables);
   }
   SaveJson();
   m_revision++;
}

bool TableLibrary::Update(const string& uuid, const std::function<bool(Table&)>& change, bool isContentChange)
{
   std::vector<Table> tables = m_tables;
   const auto it = std::find_if(tables.begin(), tables.end(), [&](const Table& table) { return table.uuid == uuid; });
   if (it == tables.end() || !change(*it))
      return false;
   if (isContentChange) // The modification date tells when the table, its name or its image changed (images are reloaded when it changes)
      it->modifiedAt = Now();
   Commit(std::move(tables));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
// tables.json

void TableLibrary::LoadJson()
{
   m_loaded = true;
   std::ifstream file(m_config.jsonPath);
   if (!file)
      return;
   try {
      const nlohmann::json json = nlohmann::json::parse(file);
      std::vector<Table> tables;
      for (const auto& entry : json.at("tables")) {
         Table table;
         table.uuid = entry.value("uuid", string());
         table.name = entry.value("name", string());
         table.path = entry.value("path", string());
         table.image = entry.value("image", string());
         table.createdAt = entry.value("createdAt", int64_t(0));
         table.modifiedAt = entry.value("modifiedAt", int64_t(0));
         tables.push_back(std::move(table));
      }
      // Player statistics are optional, and must not prevent from loading the tables
      if (std::ifstream statsFile(GetStatsPath()); statsFile) {
         try {
            const nlohmann::json stats = nlohmann::json::parse(statsFile);
            for (Table& table : tables) {
               if (const auto it = stats.find(table.uuid); it != stats.end()) {
                  table.lastPlayedAt = it->value("lastPlayedAt", int64_t(0));
                  table.playCount = it->value("playCount", 0);
                  table.favorite = it->value("favorite", false);
                  table.usesPup = it->value("usesPup", false);
                  table.pupPacks = it->value("pupPacks", std::vector<string>());
                  table.pupScannedAt = it->value("pupScannedAt", int64_t(0));
               }
            }
         } catch (const std::exception& e) {
            Log(LogLevel::Error, "Failed to parse " + GetStatsPath().string() + ": " + e.what());
         }
      }

      std::lock_guard lock(m_mutex);
      m_tables = std::move(tables);
   } catch (const std::exception& e) {
      Log(LogLevel::Error, "Failed to parse tables.json: "s + e.what());
   }
}

fs::path TableLibrary::GetStatsPath() const { return m_config.statsPath.empty() ? m_config.jsonPath.parent_path() / "table-stats.json" : m_config.statsPath; }

void TableLibrary::SaveJson()
{
   nlohmann::ordered_json tables = nlohmann::ordered_json::array();
   nlohmann::ordered_json stats = nlohmann::ordered_json::object();
   for (const Table& table : GetTables()) {
      tables.push_back({ { "uuid", table.uuid }, { "name", table.name }, { "path", table.path }, { "image", table.image }, { "createdAt", table.createdAt }, { "modifiedAt", table.modifiedAt } });
      if (table.lastPlayedAt != 0 || table.playCount != 0 || table.favorite || table.pupScannedAt != 0) {
         stats[table.uuid] = { { "playCount", table.playCount }, { "lastPlayedAt", table.lastPlayedAt }, { "favorite", table.favorite } };
         if (table.pupScannedAt != 0) {
            stats[table.uuid]["usesPup"] = table.usesPup;
            stats[table.uuid]["pupPacks"] = table.pupPacks;
            stats[table.uuid]["pupScannedAt"] = table.pupScannedAt;
         }
      }
   }
   const nlohmann::ordered_json json = { { "tableCount", tables.size() }, { "tables", tables } };

   // Write then rename, so that a crash or a full disk never leaves a truncated database
   const auto save = [this](const fs::path& path, const nlohmann::ordered_json& content) {
      std::error_code ec;
      fs::create_directories(path.parent_path(), ec);
      fs::path tempPath = path;
      tempPath += ".tmp";
      {
         std::ofstream file(tempPath, std::ios::trunc);
         file << content.dump(2) << '\n';
         if (!file) {
            Log(LogLevel::Error, "Failed to write " + tempPath.string());
            return;
         }
      }
      fs::rename(tempPath, path, ec);
      if (ec)
         Log(LogLevel::Error, "Failed to save " + path.string() + ": " + ec.message());
   };
   save(m_config.jsonPath, json);
   save(GetStatsPath(), stats);
}

///////////////////////////////////////////////////////////////////////////////
// Rescan

void TableLibrary::Rescan(const ProgressCallback& onProgress, int settleSeconds)
{
   std::lock_guard operationLock(m_operationMutex);
   const auto progress = [&](int value, const char* message) {
      if (onProgress)
         onProgress(value, message);
   };

   std::error_code ec;
   fs::create_directories(m_config.tablesPath, ec);
   fs::remove_all(m_config.tablesPath / IMPORT_FOLDER, ec); // Left over of an interrupted import

   progress(10, "Loading tables...");
   if (!m_loaded)
      LoadJson();
   const bool jsonExists = Exists(m_config.jsonPath);
   std::vector<Table> tables = m_tables;

   progress(20, "Validating tables...");
   for (Table& table : tables) {
      // Databases written by older versions used absolute paths
      if (fs::path(table.path).is_absolute())
         table.path = RelativePath(table.path);
      if (fs::path(table.image).is_absolute())
         table.image = RelativePath(table.image);
   }
   std::set<string> seenUuids, seenPaths;
   std::erase_if(tables, [&](const Table& table) {
      if (table.uuid.empty() || table.path.empty() || !IsInsideTables(BuildPath(table.path)) || !Exists(BuildPath(table.path)))
         return true;
      return !seenUuids.insert(table.uuid).second || !seenPaths.insert(table.path).second;
   });

   progress(40, "Scanning for images...");
   for (Table& table : tables) {
      if (!table.image.empty() && (!IsInsideTables(BuildPath(table.image)) || !Exists(BuildPath(table.image))))
         table.image.clear();
      if (table.image.empty())
         table.image = FindImage(table.path);
      table.modifiedAt = std::max(table.modifiedAt, FileModifiedAt(BuildPath(table.path)).value_or(0));
      if (!table.image.empty())
         table.modifiedAt = std::max(table.modifiedAt, FileModifiedAt(BuildPath(table.image)).value_or(0));
   }

   // Only bundles at the root of the tables folder are imported: deeper archives belong to the tables (PinMAME ROMs must stay zipped)
   progress(50, "Importing bundles...");
   std::vector<fs::path> archives;
   for (auto it = fs::directory_iterator(m_config.tablesPath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec) && !IsHidden(it->path()) && IsArchive(it->path()))
         archives.push_back(it->path());
   }
   // PinUP Player packs uploaded to the shared pack folder
   if (!m_config.sharedPupVideosPath.empty()) {
      std::error_code packEc;
      for (auto it = fs::directory_iterator(m_config.sharedPupVideosPath, packEc); !packEc && it != fs::directory_iterator(); it.increment(packEc)) {
         if (it->is_regular_file(packEc) && !IsHidden(it->path()) && IsArchive(it->path()))
            archives.push_back(it->path());
      }
   }
   std::sort(archives.begin(), archives.end());
   for (const fs::path& archive : archives) {
      const int64_t modifiedAt = FileModifiedAt(archive).value_or(0);
      if (settleSeconds > 0 && Now() - modifiedAt < settleSeconds)
         continue;
      const string archiveId = archive.string() + '|' + std::to_string(modifiedAt) + '|' + std::to_string(fs::file_size(archive, ec));
      if (m_rejectedArchives.contains(archiveId))
         continue;
      int importedPacks = 0;
      const std::vector<Table> imported = ImportArchive(archive, tables, nullptr, &importedPacks);
      if (imported.empty() && importedPacks == 0) {
         Log(LogLevel::Warn, "No table imported from " + archive.string() + ", the file is left untouched");
         m_rejectedArchives.insert(archiveId);
         continue;
      }
      tables.insert(tables.end(), imported.begin(), imported.end());
      fs::remove(archive, ec);
      Log(LogLevel::Info, "Imported " + std::to_string(imported.size()) + " table(s) and " + std::to_string(importedPacks) + " PinUP Player pack(s) from " + archive.string());
   }

   progress(70, "Scanning for tables...");
   seenPaths.clear();
   for (const Table& table : tables)
      seenPaths.insert(table.path);
   for (const fs::path& file : ListTableFiles(m_config.tablesPath, settleSeconds)) {
      if (!seenPaths.contains(RelativePath(file))) {
         tables.push_back(CreateTable(file, tables));
         seenPaths.insert(tables.back().path);
      }
   }

   progress(85, "Looking for PinUP Player packs...");
   UpdatePupPacks(tables);

   progress(90, "Finalizing...");
   if (!jsonExists || tables.size() != m_tables.size() || !std::equal(tables.begin(), tables.end(), m_tables.begin(), SameTable))
      Commit(std::move(tables));
   progress(100, "Complete");
}

// The packs named by the script of the tables, read again when a table changed, then whether they are installed. Packs are folders, named
// as the pack ignoring case, in the 'pupvideos' folder along the table or in the shared one (the same lookup as the PUP plugin)
void TableLibrary::UpdatePupPacks(std::vector<Table>& tables) const
{
   if (!m_config.readPupUse)
      return;
   const auto lower = [](string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
   };
   const auto listFolders = [&lower](const fs::path& folder) {
      std::set<string> names;
      std::error_code ec;
      for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
         if (it->is_directory(ec))
            names.insert(lower(it->path().filename().string()));
      return names;
   };
   const std::set<string> sharedPacks = m_config.sharedPupVideosPath.empty() ? std::set<string>() : listFolders(m_config.sharedPupVideosPath);
   for (Table& table : tables) {
      const fs::path tablePath = BuildPath(table.path);
      if (table.pupScannedAt != table.modifiedAt || table.pupScannedAt == 0) {
         try {
            const PupUse use = m_config.readPupUse(tablePath);
            table.usesPup = use.usesPup;
            table.pupPacks = use.names;
         } catch (const std::exception& e) {
            Log(LogLevel::Warn, "Failed to read the script of " + tablePath.string() + ": " + e.what());
         }
         table.pupScannedAt = table.modifiedAt != 0 ? table.modifiedAt : 1;
      }
      table.hasPupPack = false;
      if (table.pupPacks.empty())
         continue;
      std::set<string> localPacks;
      std::error_code ec;
      for (auto it = fs::directory_iterator(tablePath.parent_path(), ec); !ec && it != fs::directory_iterator(); it.increment(ec))
         if (it->is_directory(ec) && lower(it->path().filename().string()) == "pupvideos")
            localPacks = listFolders(it->path());
      for (const string& name : table.pupPacks)
         if (localPacks.contains(lower(name)) || sharedPacks.contains(lower(name)))
            table.hasPupPack = true;
   }
}

// The PinUP Player packs in 'folder' (the folders holding screens.pup, playlists.pup or triggers.pup) moved to the shared pack folder, replacing
// a pack of the same name. A pack at the root of 'folder' (an archive of the pack's content) is named 'rootName'.
int TableLibrary::MovePupPacks(const fs::path& folder, const string& rootName) const
{
   if (m_config.sharedPupVideosPath.empty())
      return 0;
   const auto lower = [](string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
   };
   std::error_code ec;
   std::set<fs::path> roots;
   for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec))
         continue;
      const string name = lower(it->path().filename().string());
      if (name == "screens.pup" || name == "playlists.pup" || name == "triggers.pup")
         roots.insert(it->path().parent_path());
   }
   // A pack nested in another one moves with it (the set is sorted, so parents come first)
   std::vector<fs::path> packs;
   for (const fs::path& root : roots) {
      const bool covered = std::any_of(packs.begin(), packs.end(), [&](const fs::path& parent) {
         const fs::path relative = root.lexically_relative(parent);
         return !relative.empty() && *relative.begin() != "..";
      });
      if (!covered)
         packs.push_back(root);
   }

   int moved = 0;
   fs::create_directories(m_config.sharedPupVideosPath, ec);
   for (const fs::path& pack : packs) {
      const string name = pack.lexically_normal() == folder.lexically_normal() ? rootName : pack.filename().string();
      if (name.empty() || name == "." || name == "..")
         continue;
      // The plugin finds packs ignoring case: an existing pack with the same name in another case is replaced too
      for (auto it = fs::directory_iterator(m_config.sharedPupVideosPath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
         if (it->is_directory(ec) && lower(it->path().filename().string()) == lower(name)) {
            Log(LogLevel::Info, "Replacing the PinUP Player pack " + it->path().string());
            std::error_code removeEc;
            fs::remove_all(it->path(), removeEc);
            break;
         }
      }
      const fs::path dest = m_config.sharedPupVideosPath / name;
      fs::rename(pack, dest, ec);
      if (ec) { // The shared folder may be on another file system (a PinUP Player folder defined by the user)
         ec.clear();
         fs::copy(pack, dest, fs::copy_options::recursive, ec);
         if (ec) {
            Log(LogLevel::Error, "Failed to move the PinUP Player pack " + pack.string() + " to " + dest.string() + ": " + ec.message());
            continue;
         }
         fs::remove_all(pack, ec);
      }
      Log(LogLevel::Info, "PinUP Player pack '" + name + "' added to " + m_config.sharedPupVideosPath.string());
      moved++;
   }
   return moved;
}

void TableLibrary::RescanAsync(int settleSeconds)
{
   std::lock_guard lock(m_workerMutex);
   if (m_scanning.exchange(true)) {
      m_rescanPending = true;
      return;
   }
   if (m_worker.joinable())
      m_worker.join(); // Previous worker has left its loop, this does not wait
   m_worker = std::thread([this, settleSeconds]() {
      for (;;) {
         Rescan(nullptr, settleSeconds);
         std::lock_guard lock(m_workerMutex);
         if (!m_rescanPending) {
            m_scanning = false;
            return;
         }
         m_rescanPending = false;
      }
   });
}

///////////////////////////////////////////////////////////////////////////////
// Import

std::vector<Table> TableLibrary::Import(const fs::path& path, const ProgressCallback& onProgress)
{
   std::lock_guard operationLock(m_operationMutex);
   std::error_code ec;
   if (!fs::is_regular_file(path, ec)) {
      Log(LogLevel::Error, "File does not exist: " + path.string());
      return {};
   }
   fs::create_directories(m_config.tablesPath, ec);

   std::vector<Table> imported;
   if (HasExtension(path, ".vpx"))
      imported = ImportVPX(path, m_tables);
   else if (IsArchive(path))
      imported = ImportArchive(path, m_tables, onProgress);
   else
      Log(LogLevel::Error, "Unsupported file extension: " + path.extension().string());
   if (imported.empty())
      return {};

   std::vector<Table> tables = m_tables;
   tables.insert(tables.end(), imported.begin(), imported.end());
   Commit(std::move(tables));
   if (onProgress)
      onProgress(100, "Import complete");
   return imported;
}

std::vector<Table> TableLibrary::ImportVPX(const fs::path& path, const std::vector<Table>& tables)
{
   const fs::path destFolder = BuildPath(GetUniqueFolder(NameFromStem(path)));
   const fs::path destFile = destFolder / path.filename();
   fs::path partFile = destFile;
   partFile += ".part"; // Never expose a half copied table to a concurrent scan

   std::error_code ec;
   fs::create_directories(destFolder, ec);
   if (!ec)
      fs::copy_file(path, partFile, ec);
   if (!ec)
      fs::rename(partFile, destFile, ec);
   if (ec) {
      Log(LogLevel::Error, "Failed to copy " + path.string() + ": " + ec.message());
      fs::remove_all(destFolder, ec);
      return {};
   }
   return { CreateTable(destFile, tables) };
}

std::vector<Table> TableLibrary::ImportArchive(const fs::path& path, const std::vector<Table>& tables, const ProgressCallback& onProgress, int* importedPacks)
{
   if (!m_config.unzip) {
      Log(LogLevel::Error, "No unzip support, unable to import " + path.string());
      return {};
   }

   std::error_code ec;
   const fs::path importFolder = m_config.tablesPath / IMPORT_FOLDER;
   const fs::path tempFolder = importFolder / GenerateUUID({});
   fs::create_directories(tempFolder, ec);
   if (ec) {
      Log(LogLevel::Error, "Failed to create " + tempFolder.string() + ": " + ec.message());
      return {};
   }

   std::vector<Table> imported;
   const bool extracted = m_config.unzip(path, tempFolder, [&](int current, int total, const char*) {
      if (onProgress && total > 0)
         onProgress(static_cast<int>(95LL * current / total), "Importing archive");
   });
   if (!extracted) {
      Log(LogLevel::Error, "Failed to extract " + path.string());
   } else {
      // Each folder holding tables becomes a table folder, moved as a whole to keep backglass, ini, music, ROMs... A folder
      // nested in another one holding tables moves with its parent (folders are sorted, so parents come first).
      std::set<fs::path> tableFolders;
      for (const fs::path& file : ListTableFiles(tempFolder, 0))
         tableFolders.insert(file.parent_path());
      std::vector<fs::path> sourceFolders;
      for (const fs::path& folder : tableFolders) {
         const bool covered = std::any_of(sourceFolders.begin(), sourceFolders.end(), [&](const fs::path& parent) {
            const fs::path relative = folder.lexically_relative(parent);
            return !relative.empty() && *relative.begin() != "..";
         });
         if (!covered)
            sourceFolders.push_back(folder);
      }

      std::vector<Table> known = tables;
      for (const fs::path& sourceFolder : sourceFolders) {
         // Named after the first table located directly in the folder
         std::vector<fs::path> files = ListTableFiles(sourceFolder, 0);
         std::erase_if(files, [&](const fs::path& file) { return file.parent_path() != sourceFolder; });
         const fs::path destFolder = BuildPath(GetUniqueFolder(NameFromStem(files.front())));
         fs::rename(sourceFolder, destFolder, ec);
         if (ec) {
            Log(LogLevel::Error, "Failed to move " + sourceFolder.string() + ": " + ec.message());
            continue;
         }
         for (const fs::path& file : ListTableFiles(destFolder, 0)) {
            imported.push_back(CreateTable(file, known));
            known.push_back(imported.back());
         }
      }

      // The PinUP Player packs left once the table folders are moved (packs in a table folder stay with their table)
      const int packs = MovePupPacks(tempFolder, path.stem().string());
      if (importedPacks)
         *importedPacks = packs;
   }

   fs::remove_all(tempFolder, ec);
   fs::remove(importFolder, ec); // Only succeeds when empty
   return imported;
}

///////////////////////////////////////////////////////////////////////////////
// Table operations

bool TableLibrary::Delete(const string& uuid)
{
   std::lock_guard operationLock(m_operationMutex);
   std::vector<Table> tables = m_tables;
   const auto it = std::find_if(tables.begin(), tables.end(), [&](const Table& table) { return table.uuid == uuid; });
   if (it == tables.end())
      return false;

   const fs::path tablePath = BuildPath(it->path);
   if (!IsInsideTables(tablePath))
      return false;

   // A table alone in its folder goes with the folder, otherwise only the table and its companion files are removed
   std::error_code ec;
   const fs::path tableFolder = tablePath.parent_path();
   if (IsInsideTables(tableFolder) && ListTableFiles(tableFolder, 0).size() <= 1) {
      fs::remove_all(tableFolder, ec);
   } else {
      fs::remove(tablePath, ec);
      for (const char* ext : SIDECAR_EXTENSIONS) {
         std::error_code ignored;
         fs::remove(fs::path(tablePath).replace_extension(ext), ignored);
      }
   }
   if (ec) {
      Log(LogLevel::Error, "Failed to delete " + tablePath.string() + ": " + ec.message());
      return false;
   }

   tables.erase(it);
   Commit(std::move(tables));
   return true;
}

bool TableLibrary::Rename(const string& uuid, const string& newName)
{
   std::lock_guard operationLock(m_operationMutex);
   const bool isBlank = newName.find_first_not_of(' ') == string::npos;
   return Update(uuid, [&](Table& table) {
      const string name = isBlank ? GetDefaultName(table) : newName;
      if (table.name == name)
         return false;
      table.name = name;
      return true;
   });
}

bool TableLibrary::SetImage(const string& uuid, const string& imagePath)
{
   std::lock_guard operationLock(m_operationMutex);
   return Update(uuid, [&](Table& table) {
      std::error_code ec;
      const fs::path source(imagePath);
      if (!imagePath.empty() && !source.is_absolute()) {
         if (!IsInsideTables(BuildPath(imagePath)) || !Exists(BuildPath(imagePath)))
            return false;
         table.image = source.generic_string();
         return true;
      }

      const fs::path currentImage = table.image.empty() ? fs::path() : BuildPath(table.image);
      if (imagePath.empty()) {
         if (!currentImage.empty() && IsInsideTables(currentImage))
            fs::remove(currentImage, ec);
         table.image.clear();
         return true;
      }

      const fs::path destPath = BuildPath(table.path).replace_extension(HasExtension(source, ".png") ? ".png" : ".jpg");
      fs::path partPath = destPath;
      partPath += ".part";
      fs::copy_file(source, partPath, fs::copy_options::overwrite_existing, ec);
      if (ec) {
         Log(LogLevel::Error, "Failed to copy " + imagePath + ": " + ec.message());
         return false;
      }
      if (!currentImage.empty() && IsInsideTables(currentImage))
         fs::remove(currentImage, ec);
      fs::rename(partPath, destPath, ec);
      if (ec)
         return false;
      table.image = RelativePath(destPath);
      return true;
   });
}

bool TableLibrary::ReloadImage(const string& uuid)
{
   std::lock_guard operationLock(m_operationMutex);
   return Update(uuid, [&](Table& table) {
      const string image = FindImage(table.path);
      if (image.empty())
         return false;
      if (image == table.image && FileModifiedAt(BuildPath(image)).value_or(0) <= table.modifiedAt)
         return false;
      table.image = image;
      return true;
   });
}

bool TableLibrary::SetFavorite(const string& uuid, bool favorite)
{
   std::lock_guard operationLock(m_operationMutex);
   return Update(uuid, [favorite](Table& table) {
      if (table.favorite == favorite)
         return false;
      table.favorite = favorite;
      return true;
   }, false);
}

bool TableLibrary::RecordPlay(const fs::path& fullPath)
{
   std::lock_guard operationLock(m_operationMutex);
   if (!m_loaded)
      LoadJson(); // A table may be played before the first rescan
   if (!IsInsideTables(fullPath))
      return false;
   const string relativePath = RelativePath(fullPath);
   const auto it = std::find_if(m_tables.begin(), m_tables.end(), [&](const Table& table) { return table.path == relativePath; });
   if (it == m_tables.end())
      return false;
   return Update(it->uuid, [](Table& table) {
      table.playCount++;
      table.lastPlayedAt = Now();
      return true;
   }, false);
}

std::optional<Table> TableLibrary::FindTable(const fs::path& fullPath)
{
   std::lock_guard operationLock(m_operationMutex);
   if (!m_loaded)
      LoadJson();
   if (!IsInsideTables(fullPath))
      return std::nullopt;
   const string relativePath = RelativePath(fullPath);
   std::lock_guard lock(m_mutex);
   const auto it = std::find_if(m_tables.begin(), m_tables.end(), [&](const Table& table) { return table.path == relativePath; });
   return it == m_tables.end() ? std::nullopt : std::optional<Table>(*it);
}

std::optional<int> TableLibrary::FuzzyScore(const string& query, const string& text)
{
   // Each word of the query is searched on its own, so that they can be given in any order ('mars attack' finds 'Attack from Mars')
   int score = 0;
   size_t start = 0;
   while (start < query.size()) {
      const size_t end = std::min(query.find(' ', start), query.size());
      if (end > start) {
         const std::optional<int> wordScore = FuzzyWordScore(query.substr(start, end - start), text);
         if (!wordScore)
            return std::nullopt;
         score += *wordScore;
      }
      start = end + 1;
   }
   return score;
}

std::optional<int> TableLibrary::FuzzyWordScore(const string& word, const string& text)
{
   string wanted;
   for (const char c : word)
      wanted += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   if (wanted.size() > text.size())
      return std::nullopt;

   // Best alignment, not the leftmost one: searching 'mars' in 'Attack from Mars' must match the last word, not the 'm' of 'from'.
   // best[j] is the best score with the characters of the query processed so far, the last one being matched at position j of the text.
   constexpr int noMatch = INT_MIN / 2;
   const size_t n = text.size();
   std::vector<int> best(n, noMatch), previous;
   for (size_t i = 0; i < wanted.size(); i++) {
      previous.swap(best);
      best.assign(n, noMatch);
      int bestBefore = noMatch; // Best of previous[0 .. j-2], for a match which does not directly follow the previous one
      for (size_t j = i; j < n; j++) {
         if (j >= 2)
            bestBefore = std::max(bestBefore, previous[j - 2]);
         if (static_cast<char>(std::tolower(static_cast<unsigned char>(text[j]))) != wanted[i])
            continue;
         const bool isWordStart = j == 0 || !std::isalnum(static_cast<unsigned char>(text[j - 1]));
         const int charScore = 10 + (isWordStart ? 10 : 0);
         if (i == 0)
            best[j] = charScore + (j == 0 ? 5 : 0) - static_cast<int>(std::min<size_t>(j, 9)); // Matches starting early in the text first
         else {
            const int consecutive = j >= 1 && previous[j - 1] != noMatch ? previous[j - 1] + 15 : noMatch;
            const int from = std::max(consecutive, bestBefore);
            if (from != noMatch)
               best[j] = from + charScore;
         }
      }
   }
   const int score = *std::max_element(best.begin(), best.end());
   return score == noMatch ? std::nullopt : std::optional<int>(score);
}

bool TableLibrary::ResetIni(const string& uuid)
{
   const std::optional<Table> table = GetTable(uuid);
   if (!table || !IsInsideTables(GetIniPath(*table)))
      return false;
   std::error_code ec;
   return fs::remove(GetIniPath(*table), ec);
}

std::optional<fs::path> TableLibrary::Export(const string& uuid, const fs::path& destFolder, const ProgressCallback& onProgress)
{
   std::lock_guard operationLock(m_operationMutex);
   const std::optional<Table> table = GetTable(uuid);
   if (!table || !m_config.zip)
      return std::nullopt;

   const fs::path tableFolder = BuildPath(table->path).parent_path();
   if (!IsInsideTables(tableFolder)) {
      Log(LogLevel::Error, "Only tables stored in their own folder can be exported: " + table->path);
      return std::nullopt;
   }

   std::error_code ec;
   fs::create_directories(destFolder, ec);
   const fs::path destPath = destFolder / (SanitizeName(table->name) + ".vpxz");
   fs::remove(destPath, ec);
   const bool zipped = m_config.zip(tableFolder, destPath, [&](int current, int total, const char*) {
      if (onProgress)
         onProgress(static_cast<int>(99LL * current / std::max(total, 1)), "Compressing");
   });
   if (!zipped) {
      Log(LogLevel::Error, "Failed to export " + table->path);
      fs::remove(destPath, ec);
      return std::nullopt;
   }
   if (onProgress)
      onProgress(100, "Complete");
   return destPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// missing-roms.json: {"missingRoms":[{rom,folder,files:[...],table,reportedAt}]}

fs::path TableLibrary::GetMissingRomsPath() const { return m_config.jsonPath.parent_path() / "missing-roms.json"; }

std::vector<TableLibrary::MissingRom> TableLibrary::LoadMissingRoms() const
{
   std::vector<MissingRom> missingRoms;
   std::ifstream file(GetMissingRomsPath());
   if (!file.is_open())
      return missingRoms;
   try {
      const nlohmann::json json = nlohmann::json::parse(file);
      for (const auto& entry : json.at("missingRoms")) {
         MissingRom missing;
         missing.rom = entry.value("rom", ""s);
         missing.folder = entry.value("folder", ""s);
         missing.files = entry.value("files", std::vector<string>());
         missing.table = entry.value("table", ""s);
         missing.reportedAt = entry.value("reportedAt", int64_t(0));
         if (!missing.rom.empty())
            missingRoms.push_back(missing);
      }
   }
   catch (const std::exception& e) {
      Log(LogLevel::Error, "Failed to parse missing-roms.json: "s + e.what());
   }
   return missingRoms;
}

void TableLibrary::SaveMissingRoms(const std::vector<MissingRom>& missingRoms) const
{
   std::error_code ec;
   if (missingRoms.empty()) {
      fs::remove(GetMissingRomsPath(), ec);
      return;
   }
   nlohmann::ordered_json list = nlohmann::ordered_json::array();
   for (const MissingRom& missing : missingRoms)
      list.push_back({ { "rom", missing.rom }, { "folder", missing.folder }, { "files", missing.files }, { "table", missing.table }, { "reportedAt", missing.reportedAt } });
   std::ofstream file(GetMissingRomsPath(), std::ios::trunc);
   file << nlohmann::ordered_json { { "missingRoms", list } }.dump(2);
   if (!file.good())
      Log(LogLevel::Error, "Failed to save missing-roms.json");
}

void TableLibrary::AddMissingRom(const string& rom, const fs::path& folder, const std::vector<string>& files, const string& table)
{
   std::lock_guard lock(m_missingRomsMutex);
   std::vector<MissingRom> missingRoms = LoadMissingRoms();
   // A new report of the same ROM for the same table replaces the previous one, as the missing files may have changed (tables sharing a ROM each have an entry)
   std::erase_if(missingRoms, [&rom, &table](const MissingRom& missing) { return ToLower(missing.rom) == ToLower(rom) && missing.table == table; });
   MissingRom missing;
   missing.rom = rom;
   missing.folder = IsInsideTables(folder) ? RelativePath(folder) : folder.string();
   missing.files = files;
   missing.table = table;
   missing.reportedAt = Now();
   missingRoms.push_back(missing);
   SaveMissingRoms(missingRoms);
   Log(LogLevel::Info, "Missing ROM recorded: " + rom);
}

std::vector<TableLibrary::MissingRom> TableLibrary::GetMissingRoms() const
{
   std::lock_guard lock(m_missingRomsMutex);
   return LoadMissingRoms();
}

void TableLibrary::ClearMissingRoms()
{
   std::lock_guard lock(m_missingRomsMutex);
   SaveMissingRoms({});
}

void TableLibrary::OnFileAdded(const string& fileName)
{
   std::lock_guard lock(m_missingRomsMutex);
   std::vector<MissingRom> missingRoms = LoadMissingRoms();
   const string name = ToLower(fs::path(fileName).filename().string());
   // Remove the ROMs which were uploaded, the uploaded files from the ROMs which miss them, and the ROMs which no longer miss any file
   bool changed = false;
   std::erase_if(missingRoms, [&name, &changed](MissingRom& missing) {
      if (ToLower(missing.rom) == name)
         return changed = true;
      if (std::erase_if(missing.files, [&name](const string& file) { return ToLower(file) == name; }) == 0)
         return false;
      changed = true;
      return missing.files.empty();
   });
   if (changed)
      SaveMissingRoms(missingRoms);
}

}
