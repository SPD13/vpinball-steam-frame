// license:GPLv3+

#include "TableLibrary.h"
#include "PupPackOptions.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <fstream>
#include <random>
#include <sstream>

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
      && a.pupScannedAt == b.pupScannedAt && a.hasPupPack == b.hasPupPack && a.rom == b.rom;
}

}

TableLibrary::TableLibrary(Config config)
   : m_config(std::move(config))
   , m_gameCatalog(m_config.pinmameCatalogPath)
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
                  table.rom = it->value("rom", ""s);
                  if (!it->contains("rom"))
                     table.pupScannedAt = 0; // Scanned before the ROM was read from the script: read again
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
            stats[table.uuid]["rom"] = table.rom;
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
   if (!m_rejectedArchivesLoaded)
      LoadRejectedArchives();
   EnsureAliasFile();
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
      int importedPacks = 0, importedRoms = 0;
      const std::vector<Table> imported = ImportArchive(archive, tables, nullptr, &importedPacks, &importedRoms);
      if (imported.empty() && importedPacks == 0 && importedRoms == 0) {
         Log(LogLevel::Warn, "No table imported from " + archive.string() + ", the file is left untouched");
         m_rejectedArchives.insert(archiveId);
         SaveRejectedArchives();
         continue;
      }
      tables.insert(tables.end(), imported.begin(), imported.end());
      fs::remove(archive, ec);
      Log(LogLevel::Info, "Imported " + std::to_string(imported.size()) + " table(s), " + std::to_string(importedPacks) + " PinUP Player pack(s) and "
            + std::to_string(importedRoms) + " ROM(s) from " + archive.string());
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

   // Once the scripts of the new tables are read, for the games and ROMs they need
   progress(87, "Looking for game aliases...");
   ResolveGameAliases(tables);
   progress(88, "Looking for ROMs...");
   MoveRootRoms(tables);

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
            table.rom = use.rom;
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

// The pack of a folder holding .pup files inside 'folder': the folder itself, or a parent whose option batch files copy from it, for packs
// with their .pup files in their option folders only ('austin/PuP-Pack_Options/Option 3 - BG Topper Separate/screens.pup' is 'austin')
static fs::path PackOfOptionFolder(const fs::path& folder, const fs::path& pupFolder)
{
   for (fs::path parent = pupFolder.parent_path(); parent != parent.parent_path(); parent = parent.parent_path()) {
      const fs::path relative = parent.lexically_relative(folder);
      if (relative.empty() || *relative.begin() == "..")
         break;
      for (const fs::path& source : PupPackOptions::PackSources(parent)) {
         std::error_code ec;
         if (fs::equivalent(source, pupFolder, ec))
            return parent;
      }
      if (relative == ".")
         break;
   }
   return pupFolder;
}

// The PinUP Player packs in 'folder': the folders holding screens.pup, playlists.pup or triggers.pup, or whose options copy them, without
// the ones nested in another pack
static std::vector<fs::path> FindPupPacks(const fs::path& folder)
{
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
         roots.insert(PackOfOptionFolder(folder, it->path().parent_path()));
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
   return packs;
}

// The PinUP Player packs in 'folder' moved to the shared pack folder, replacing a pack of the same name. A pack at the root of 'folder' (an
// archive of the pack's content) is named 'rootName'.
int TableLibrary::MovePupPacks(const fs::path& folder, const string& rootName) const
{
   if (m_config.sharedPupVideosPath.empty())
      return 0;
   int moved = 0;
   for (const fs::path& pack : FindPupPacks(folder))
      if (MovePupPack(pack, pack.lexically_normal() == folder.lexically_normal() ? rootName : pack.filename().string()))
         moved++;
   return moved;
}

// One pack moved to the shared pack folder under 'name', with its VR layout
bool TableLibrary::MovePupPack(const fs::path& pack, const string& name) const
{
   if (m_config.sharedPupVideosPath.empty() || name.empty() || name == "." || name == "..")
      return false;
   std::error_code ec;
   fs::create_directories(m_config.sharedPupVideosPath, ec);
   // The plugin finds packs ignoring case: an existing pack with the same name in another case is replaced too
   for (auto it = fs::directory_iterator(m_config.sharedPupVideosPath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (it->is_directory(ec) && ToLower(it->path().filename().string()) == ToLower(name)) {
         Log(LogLevel::Info, "Replacing the PinUP Player pack " + it->path().string());
         std::error_code removeEc;
         fs::remove_all(it->path(), removeEc);
         break;
      }
   }
   ec.clear();
   const fs::path dest = m_config.sharedPupVideosPath / name;
   fs::rename(pack, dest, ec);
   if (ec) { // The shared folder may be on another file system (a PinUP Player folder defined by the user)
      ec.clear();
      fs::copy(pack, dest, fs::copy_options::recursive, ec);
      if (ec) {
         Log(LogLevel::Error, "Failed to move the PinUP Player pack " + pack.string() + " to " + dest.string() + ": " + ec.message());
         return false;
      }
      fs::remove_all(pack, ec);
   }
   Log(LogLevel::Info, "PinUP Player pack '" + name + "' added to " + m_config.sharedPupVideosPath.string());
   ApplyVrPupOptions(dest);
   return true;
}

// The packs of an imported table folder that are not in a 'pupvideos' folder, the only place along a table where the plugin looks for them
// (a bundle with 'Jaws/Jaws.vpx' and 'Jaws/jaws/screens.pup'): moved to the shared pack folder
int TableLibrary::MoveStrayTablePacks(const fs::path& tableFolder) const
{
   int moved = 0;
   for (const fs::path& pack : FindPupPacks(tableFolder)) {
      if (ToLower(pack.parent_path().filename().string()) == "pupvideos")
         continue;
      if (pack.lexically_normal() == tableFolder.lexically_normal()) {
         Log(LogLevel::Warn, "The files of a PinUP Player pack are next to the table in " + tableFolder.string() + ": they are left there");
         continue;
      }
      if (MovePupPack(pack, pack.filename().string()))
         moved++;
   }
   return moved;
}

// The ROMs of a bundle that PinMAME would not find: it only looks in its ROM folder and in the 'pinmame/roms' folder along a table. They are
// the archives of a 'roms' folder (a Windows install's 'VPinMAME/roms', or a 'roms' folder beside the table) and, in a table folder, the
// archives next to a table. They go to the 'pinmame/roms' folder of the table when it has one (PinMAME then only looks there), otherwise to
// the ROM folder of PinMAME.
int TableLibrary::MoveBundleRoms(const fs::path& folder, bool isTableFolder)
{
   if (m_config.romsPath.empty())
      return 0;
   std::error_code ec;
   fs::path dest = m_config.romsPath;
   std::set<fs::path> tableDirs;
   if (isTableFolder) {
      if (const fs::path tableRoms = FindTableRomsFolder(folder); !tableRoms.empty())
         dest = tableRoms;
      for (const fs::path& table : ListTableFiles(folder, 0))
         tableDirs.insert(table.parent_path().lexically_normal());
   }
   std::vector<fs::path> roms;
   for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_directory(ec) && (IsHidden(it->path()) || ToLower(it->path().filename().string()) == "pupvideos")) {
         it.disable_recursion_pending(); // Media of a pack are no ROMs
         continue;
      }
      const fs::path& path = it->path();
      if (!it->is_regular_file(ec) || IsHidden(path) || !(HasExtension(path, ".zip") || HasExtension(path, ".7z")))
         continue;
      const fs::path parent = path.parent_path().lexically_normal();
      if (parent == dest.lexically_normal())
         continue;
      if (ToLower(parent.filename().string()) == "roms" || tableDirs.contains(parent))
         roms.push_back(path);
   }
   int moved = 0;
   for (const fs::path& rom : roms)
      if (MoveRom(rom, dest))
         moved++;
   return moved;
}

// A ROM archive moved to 'destFolder', replacing a ROM of the same name, and no longer missing
bool TableLibrary::MoveRom(const fs::path& rom, const fs::path& destFolder)
{
   std::error_code ec;
   fs::create_directories(destFolder, ec);
   const fs::path dest = destFolder / rom.filename();
   if (fs::exists(dest, ec) && !fs::equivalent(dest, rom, ec))
      fs::remove(dest, ec);
   ec.clear();
   fs::rename(rom, dest, ec);
   if (ec) { // The ROM folder may be on another file system (a PinMAME folder defined by the user)
      ec.clear();
      fs::copy_file(rom, dest, fs::copy_options::overwrite_existing, ec);
      if (ec) {
         Log(LogLevel::Error, "Failed to move the ROM " + rom.string() + " to " + dest.string() + ": " + ec.message());
         return false;
      }
      fs::remove(rom, ec);
   }
   Log(LogLevel::Info, "ROM " + rom.filename().string() + " added to " + destFolder.string());
   OnFileAdded(rom.filename().string());
   return true;
}

// The archives dropped in the tables folder that hold neither a table nor a pack (see Rescan), named as a ROM which a table names or which
// was reported missing: a ROM uploaded with the tables instead of the ROMs, moved to the ROM folder of PinMAME
void TableLibrary::MoveRootRoms(const std::vector<Table>& tables)
{
   if (m_config.romsPath.empty() || m_rejectedArchives.empty())
      return;
   std::set<string> romNames;
   for (const Table& table : tables)
      if (!table.rom.empty())
         romNames.insert(ToLower(table.rom));
   for (const MissingRom& missing : GetMissingRoms())
      romNames.insert(ToLower(fs::path(missing.rom).stem().string()));
   std::error_code ec;
   std::vector<fs::path> roms;
   for (auto it = fs::directory_iterator(m_config.tablesPath, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      const fs::path& path = it->path();
      if (it->is_regular_file(ec) && !IsHidden(path) && (HasExtension(path, ".zip") || HasExtension(path, ".7z")) && romNames.contains(ToLower(path.stem().string())))
         roms.push_back(path);
   }
   bool changed = false;
   for (const fs::path& rom : roms) {
      const string archiveId = rom.string() + '|' + std::to_string(FileModifiedAt(rom).value_or(0)) + '|' + std::to_string(fs::file_size(rom, ec));
      if (!m_rejectedArchives.contains(archiveId)) // Not looked into yet (being uploaded, or newer than the last scan)
         continue;
      if (MoveRom(rom, m_config.romsPath)) {
         m_rejectedArchives.erase(archiveId);
         changed = true;
      }
   }
   if (changed)
      SaveRejectedArchives();
}

///////////////////////////////////////////////////////////////////////////////
// PinMAME ROM aliases

fs::path TableLibrary::GetAliasFilePath() const { return m_config.romsPath.empty() ? fs::path() : m_config.romsPath.parent_path() / "alias.txt"; }

// The 'pinmame/roms' folder of a table, which PinMAME uses instead of its own folder (see PinMAMEPlugin.cpp), or an empty path
fs::path TableLibrary::FindTableRomsFolder(const fs::path& tableFolder) const
{
   std::error_code ec;
   for (auto it = fs::directory_iterator(tableFolder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
      if (it->is_directory(ec) && ToLower(it->path().filename().string()) == "pinmame")
         for (auto sub = fs::directory_iterator(it->path(), ec); !ec && sub != fs::directory_iterator(); sub.increment(ec))
            if (sub->is_directory(ec) && ToLower(sub->path().filename().string()) == "roms")
               return sub->path();
   return fs::path();
}

fs::path TableLibrary::GetTableAliasFilePath(const fs::path& tableFolder) const
{
   const fs::path tableRoms = FindTableRomsFolder(tableFolder);
   return tableRoms.empty() ? GetAliasFilePath() : tableRoms.parent_path() / "alias.txt";
}

// libpinmame only knows the aliases of its alias file, unlike VPinMAME which has its own list built in: a new PinMAME folder gets that list
void TableLibrary::EnsureAliasFile() const
{
   const fs::path aliasFile = GetAliasFilePath();
   std::error_code ec;
   if (aliasFile.empty() || m_config.defaultAliasesPath.empty() || fs::exists(aliasFile, ec) || !fs::exists(m_config.defaultAliasesPath, ec))
      return;
   std::lock_guard lock(m_aliasFileMutex);
   fs::create_directories(aliasFile.parent_path(), ec);
   fs::copy_file(m_config.defaultAliasesPath, aliasFile, ec);
   if (ec)
      Log(LogLevel::Error, "Failed to create the PinMAME alias file " + aliasFile.string() + ": " + ec.message());
   else
      Log(LogLevel::Info, "PinMAME alias file created with the aliases of VPinMAME: " + aliasFile.string());
}

// The alias pairs given by the text files of an imported bundle for the unknown games its tables ask for: the instructions may be at the root
// of the bundle, which is deleted once the table folders are moved out of it
void TableLibrary::CollectAliasHints(const fs::path& bundleFolder)
{
   if (!m_config.readPupUse || m_config.romsPath.empty() || !m_gameCatalog.IsAvailable())
      return;
   for (const fs::path& file : ListTableFiles(bundleFolder, 0)) {
      string rom;
      try {
         rom = m_config.readPupUse(file).rom;
      } catch (const std::exception&) {
         continue;
      }
      if (rom.empty() || m_gameCatalog.IsGame(rom) || m_aliasHints.contains(ToLower(rom)))
         continue;
      if (std::optional<RomAliases::Evidence> pair = RomAliases::FindPairInTexts(bundleFolder, rom, m_gameCatalog)) {
         pair->source += " of the uploaded bundle";
         m_aliasHints[ToLower(rom)] = *pair;
      }
   }
}

// The tables asking PinMAME for a game it does not know get the alias of the game found along them (see RomAliases::FindGame), written to
// the alias file. Then the ROM archive of their game, when it is missing from the ROM folder but is somewhere in the table folder, is moved there.
void TableLibrary::ResolveGameAliases(const std::vector<Table>& tables)
{
   if (m_config.romsPath.empty() || !m_gameCatalog.IsAvailable())
      return;
   for (const Table& table : tables) {
      if (table.rom.empty())
         continue;
      const string check = table.uuid + '|' + std::to_string(table.modifiedAt) + '|' + table.rom + '|' + std::to_string(m_aliasFileRevision.load());
      if (!m_aliasChecks.insert(check).second)
         continue;
      const fs::path tablePath = BuildPath(table.path);
      const fs::path tableFolder = tablePath.parent_path();
      string game = ToLower(table.rom);
      if (!m_gameCatalog.IsGame(game)) {
         const fs::path aliasFile = GetTableAliasFilePath(tableFolder);
         std::lock_guard lock(m_aliasFileMutex);
         if (const std::optional<string> alias = RomAliases::FindAlias(aliasFile, table.rom)) {
            game = ToLower(*alias);
         } else {
            std::vector<string> scriptGames;
            if (m_config.readPupUse) {
               try {
                  scriptGames = m_config.readPupUse(tablePath).games;
               } catch (const std::exception&) {
               }
            }
            const std::optional<RomAliases::Evidence> found = RomAliases::FindGame(table.rom, scriptGames, tableFolder, m_aliasHints, m_gameCatalog);
            if (!found) {
               Log(LogLevel::Warn, "Table '" + table.name + "' asks PinMAME for the game '" + table.rom + "', which PinMAME does not know, and no alias was found for it: add one to "
                     + aliasFile.string() + " (web server, Files page, Advanced menu)");
               continue;
            }
            string error;
            if (!RomAliases::AppendAlias(aliasFile, table.rom, found->game, "From " + found->source + ", for " + table.name, error)) {
               Log(LogLevel::Error, error);
               continue;
            }
            Log(LogLevel::Info, "Table '" + table.name + "' asks PinMAME for the unknown game '" + table.rom + "': alias '" + table.rom + ',' + found->game + "' added to "
                  + aliasFile.string() + " (found in " + found->source + ')');
            game = found->game;
         }
      }
      if (m_gameCatalog.IsGame(game))
         MoveTableRom(tableFolder, game);
   }
}

// The ROM archive of 'game' found in the table folder, moved to the ROM folder when the game has no ROM there: a bundle may keep its ROM in a
// folder of its own ('MutedRom/simpprty.zip'), where PinMAME does not look. A ROM already installed is never replaced.
bool TableLibrary::MoveTableRom(const fs::path& tableFolder, const string& game)
{
   const fs::path tableRoms = FindTableRomsFolder(tableFolder);
   const fs::path dest = tableRoms.empty() ? m_config.romsPath : tableRoms;
   const auto isRomOf = [&game](const fs::path& path) {
      return (HasExtension(path, ".zip") || HasExtension(path, ".7z")) && ToLower(path.stem().string()) == game;
   };
   std::error_code ec;
   for (auto it = fs::directory_iterator(dest, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
      if (isRomOf(it->path()))
         return false;
   ec.clear();
   for (auto it = fs::recursive_directory_iterator(tableFolder, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_directory(ec)) {
         if (IsHidden(it->path()) || ToLower(it->path().filename().string()) == "pupvideos")
            it.disable_recursion_pending();
         continue;
      }
      if (it->is_regular_file(ec) && !IsHidden(it->path()) && isRomOf(it->path())) {
         const fs::path rom = it->path();
         Log(LogLevel::Info, "ROM " + rom.filename().string() + " found in " + RelativePath(rom) + ", where PinMAME does not look for it");
         return MoveRom(rom, dest);
      }
   }
   return false;
}

string TableLibrary::ReadAliasFile() const
{
   std::lock_guard lock(m_aliasFileMutex);
   std::ifstream file(GetAliasFilePath(), std::ios::binary);
   std::stringstream content;
   content << file.rdbuf();
   return content.str();
}

bool TableLibrary::SaveAliasFile(const string& content, std::vector<RomAliases::Issue>& errors, std::vector<RomAliases::Issue>& warnings, string& error)
{
   const fs::path aliasFile = GetAliasFilePath();
   if (aliasFile.empty()) {
      error = "The PinMAME folder is not defined";
      return false;
   }
   const string normalized = RomAliases::Normalize(content, m_gameCatalog, errors, warnings, ReadAliasFile());
   if (!errors.empty())
      return false;
   std::lock_guard lock(m_aliasFileMutex);
   std::error_code ec;
   fs::create_directories(aliasFile.parent_path(), ec);
   fs::path partFile = aliasFile;
   partFile += ".part";
   {
      std::ofstream file(partFile, std::ios::binary | std::ios::trunc);
      file << normalized;
      file.close();
      if (!file)
         ec = std::make_error_code(std::errc::io_error);
   }
   if (!ec)
      fs::rename(partFile, aliasFile, ec);
   if (ec) {
      fs::remove(partFile, ec);
      error = "Failed to save " + aliasFile.string();
      Log(LogLevel::Error, error);
      return false;
   }
   m_aliasFileRevision++; // The tables are looked at again by the next scan
   Log(LogLevel::Info, "PinMAME alias file saved: " + aliasFile.string());
   return true;
}

std::vector<TableLibrary::UnknownGame> TableLibrary::GetUnknownGames()
{
   std::vector<UnknownGame> unknown;
   if (m_config.romsPath.empty() || !m_gameCatalog.IsAvailable())
      return unknown;
   for (const Table& table : GetTables()) {
      if (table.rom.empty() || m_gameCatalog.IsGame(table.rom))
         continue;
      const fs::path aliasFile = GetTableAliasFilePath(BuildPath(table.path).parent_path());
      std::lock_guard lock(m_aliasFileMutex);
      if (!RomAliases::FindAlias(aliasFile, table.rom))
         unknown.push_back({ table.uuid, table.name, table.rom });
   }
   return unknown;
}

// rejected-archives.json, along tables.json: the archives of the tables folder that hold neither a table nor a pack, so that they are not
// extracted again at each start
fs::path TableLibrary::GetRejectedArchivesPath() const { return m_config.jsonPath.parent_path() / "rejected-archives.json"; }

void TableLibrary::LoadRejectedArchives()
{
   m_rejectedArchivesLoaded = true;
   std::ifstream file(GetRejectedArchivesPath());
   if (!file)
      return;
   try {
      std::error_code ec;
      for (const string& archiveId : nlohmann::json::parse(file).value("archives", std::vector<string>()))
         if (fs::exists(archiveId.substr(0, archiveId.find('|')), ec)) // Forget the archives removed since
            m_rejectedArchives.insert(archiveId);
   } catch (const std::exception& e) {
      Log(LogLevel::Warn, "Failed to parse " + GetRejectedArchivesPath().string() + ": " + e.what());
   }
}

void TableLibrary::SaveRejectedArchives() const
{
   std::error_code ec;
   if (m_rejectedArchives.empty()) {
      fs::remove(GetRejectedArchivesPath(), ec);
      return;
   }
   fs::create_directories(GetRejectedArchivesPath().parent_path(), ec);
   std::ofstream file(GetRejectedArchivesPath(), std::ios::trunc);
   file << nlohmann::json { { "archives", m_rejectedArchives } }.dump(2);
   if (!file.good())
      Log(LogLevel::Error, "Failed to save " + GetRejectedArchivesPath().string());
}

// The VR layout of the packs in 'folder', for the packs that have one (their option scripts cannot run here, see PupPackOptions)
void TableLibrary::ApplyVrPupOptions(const fs::path& folder) const
{
   if (!m_config.applyVrPupOptions)
      return;
   for (const fs::path& pack : FindPupPacks(folder)) {
      string error;
      const string option = PupPackOptions::ApplyRecommended(pack, error);
      if (!option.empty())
         Log(LogLevel::Info, "PinUP Player pack " + pack.string() + ": option '" + option + "' applied");
      else if (!error.empty())
         Log(LogLevel::Warn, "PinUP Player pack " + pack.string() + ": " + error);
   }
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

std::vector<Table> TableLibrary::ImportArchive(const fs::path& path, const std::vector<Table>& tables, const ProgressCallback& onProgress, int* importedPacks, int* importedRoms)
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
      CollectAliasHints(tempFolder); // Before the table folders are moved: instructions are often at the root of the bundle
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
      int packs = 0, roms = 0;
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
         packs += MoveStrayTablePacks(destFolder);
         ApplyVrPupOptions(destFolder); // Packs of the table in its folder ('pupvideos/<pack>')
         roms += MoveBundleRoms(destFolder, true);
      }

      // The PinUP Player packs left once the table folders are moved (packs in a table's 'pupvideos' folder stay with their table), then the ROMs
      packs += MovePupPacks(tempFolder, path.stem().string());
      roms += MoveBundleRoms(tempFolder, false);
      if (importedPacks)
         *importedPacks = packs;
      if (importedRoms)
         *importedRoms = roms;
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
