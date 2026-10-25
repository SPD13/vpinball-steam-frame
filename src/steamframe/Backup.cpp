// license:GPLv3+

#include "core/stdafx.h"
#include "steamframe/Backup.h"

#include "core/VPApp.h"
#include "steamframe/Launcher.h"
#include "steamframe/RomAliases.h"
#include "steamframe/TableLibrary.h"

#include <mINI/ini.h>
#include <mongoose/mongoose.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <fstream>
#include <set>
#include <sstream>
#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Backup {

namespace {

constexpr const char* ITEM_IDS[] = { "games", "thumbnails", "roms", "saves", "library", "settings", "scores", "aliases" };
static_assert(std::size(ITEM_IDS) == static_cast<size_t>(Item::Count));

constexpr const char* MANIFEST_NAME = "backup.json";
constexpr const char* PENDING_SETTINGS_SECTION = "BackupRestore";

// The files of the settings folder that are backed up, with their item (nothing else of that folder is written by a restore)
constexpr std::pair<const char*, Item> PREF_FILES[] = {
   { "tables.json", Item::Library },
   { "table-stats.json", Item::Library },
   { "VPinballX.ini", Item::Settings },
   { "lobby-room.json", Item::Settings },
   { "score-rules.json", Item::Settings },
   { "scores.json", Item::Scores },
   { "profiles.json", Item::Scores },
};

// Merged into what the device has (see WebServerBackup.cpp)
constexpr const char* MERGED_ENTRIES[] = {
   MANIFEST_NAME,
   "library/pref/tables.json",
   "library/pref/table-stats.json",
   "scores/pref/scores.json",
   "scores/pref/profiles.json",
   "settings/pref/VPinballX.ini",
   "aliases/pinmame/alias.txt",
};

string ToLower(string value)
{
   std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return value;
}

// Staging folders of the uploads, imports and restores, and the resource forks of macOS
bool IsStagingName(const string& name) { return name == ".upload" || name == ".import" || name == ".restore" || name == "__MACOSX"; }

bool IsJunkFile(const string& name) { return name == ".DS_Store" || name.starts_with("._"); }

bool IsValidUtf8(const string& text)
{
   size_t i = 0;
   while (i < text.size()) {
      const auto c = static_cast<unsigned char>(text[i]);
      const int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
      if (length == 0 || i + length > text.size())
         return false;
      for (int j = 1; j < length; j++)
         if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80)
            return false;
      i += length;
   }
   return true;
}

string ToUtf8(const fs::path& path)
{
   const std::u8string text = path.generic_u8string();
   return string(text.begin(), text.end());
}

fs::path FromUtf8(const string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

// Without the trailing separator, so that paths of folders compare equal
fs::path Normal(const fs::path& path)
{
   fs::path normal = path.lexically_normal();
   if (!normal.has_filename() && normal.has_parent_path())
      normal = normal.parent_path();
   return normal;
}

bool IsInside(const fs::path& path, const fs::path& folder)
{
   const fs::path relative = Normal(path).lexically_relative(Normal(folder));
   return !relative.empty() && relative != "." && *relative.begin() != "..";
}

std::optional<int64_t> ModifiedAt(const fs::path& path)
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

///////////////////////////////////////////////////////////////////////////////
// CRC-32 (the one of zip files), sliced by 8 bytes: a few GB per second, faster than the storage of the device

struct Crc32Tables
{
   uint32_t table[8][256];
   Crc32Tables()
   {
      for (uint32_t i = 0; i < 256; i++) {
         uint32_t crc = i;
         for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
         table[0][i] = crc;
      }
      for (uint32_t i = 0; i < 256; i++)
         for (int slice = 1; slice < 8; slice++)
            table[slice][i] = (table[slice - 1][i] >> 8) ^ table[0][table[slice - 1][i] & 0xFF];
   }
};

uint32_t Crc32(uint32_t crc, const uint8_t* data, size_t length)
{
   static const Crc32Tables tables;
   const auto& t = tables.table;
   crc = ~crc;
   while (length >= 8) {
      const uint32_t low = crc ^ (uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 | uint32_t(data[3]) << 24);
      const uint32_t high = uint32_t(data[4]) | uint32_t(data[5]) << 8 | uint32_t(data[6]) << 16 | uint32_t(data[7]) << 24;
      crc = t[7][low & 0xFF] ^ t[6][(low >> 8) & 0xFF] ^ t[5][(low >> 16) & 0xFF] ^ t[4][low >> 24] ^ t[3][high & 0xFF] ^ t[2][(high >> 8) & 0xFF]
         ^ t[1][(high >> 16) & 0xFF] ^ t[0][high >> 24];
      data += 8;
      length -= 8;
   }
   while (length-- > 0)
      crc = (crc >> 8) ^ t[0][(crc ^ *data++) & 0xFF];
   return ~crc;
}

///////////////////////////////////////////////////////////////////////////////
// Zip records

void Put16(string& s, uint16_t v)
{
   s += static_cast<char>(v & 0xFF);
   s += static_cast<char>(v >> 8);
}

void Put32(string& s, uint32_t v)
{
   Put16(s, static_cast<uint16_t>(v & 0xFFFF));
   Put16(s, static_cast<uint16_t>(v >> 16));
}

void Put64(string& s, uint64_t v)
{
   Put32(s, static_cast<uint32_t>(v & 0xFFFFFFFF));
   Put32(s, static_cast<uint32_t>(v >> 32));
}

constexpr uint32_t ZIP32_MAX = 0xFFFFFFFF;
constexpr uint16_t FLAG_UTF8 = 0x0800;
constexpr uint16_t VERSION_ZIP64 = 45;
constexpr uint16_t VERSION_DEFAULT = 20;

// MS-DOS date and time, in local time, as zip tools show them (the exact time is in the 'UT' extra field)
std::pair<uint16_t, uint16_t> DosDateTime(int64_t time)
{
   const time_t t = static_cast<time_t>(time);
   std::tm tm {};
#ifdef _WIN32
   localtime_s(&tm, &t);
#else
   localtime_r(&t, &tm);
#endif
   if (tm.tm_year < 80)
      return { static_cast<uint16_t>((0 << 9) | (1 << 5) | 1), 0 }; // 1980-01-01, the first date it can hold
   const auto date = static_cast<uint16_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
   const auto dosTime = static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
   return { date, dosTime };
}

void PutTimestamp(string& s, int64_t time)
{
   Put16(s, 0x5455); // Extended timestamp: modification time only
   Put16(s, 5);
   s += static_cast<char>(1);
   Put32(s, static_cast<uint32_t>(std::clamp<int64_t>(time, 0, ZIP32_MAX)));
}

}

const char* GetItemId(Item item) { return ITEM_IDS[static_cast<int>(item)]; }

std::optional<Item> ParseItem(std::string_view id)
{
   for (int i = 0; i < static_cast<int>(Item::Count); i++)
      if (id == ITEM_IDS[i])
         return static_cast<Item>(i);
   return std::nullopt;
}

Roots Roots::OfDevice()
{
   VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
   Roots roots;
   roots.tables = library.GetTablesPath();
   const fs::path aliasFile = library.GetAliasFilePath();
   roots.pinmame = aliasFile.empty() ? fs::path() : aliasFile.parent_path();
   roots.pup = library.GetSharedPupVideosPath();
   roots.pref = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences);
   return roots;
}

const fs::path* Roots::Find(std::string_view name) const
{
   const fs::path* path = name == "tables" ? &tables : name == "pinmame" ? &pinmame : name == "pup" ? &pup : name == "pref" ? &pref : nullptr;
   return path != nullptr && !path->empty() ? path : nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// Catalog of the files, and their links to the tables

namespace {

class Scanner final
{
public:
   Scanner(const Roots& roots, Catalog& catalog)
      : m_roots(roots)
      , m_catalog(catalog)
   {
      m_catalog.tablePaths.resize(m_catalog.tables.size());
      const fs::path tablesRoot = Normal(m_roots.tables);
      for (int i = 0; i < static_cast<int>(m_catalog.tables.size()); i++) {
         const TableInfo& table = m_catalog.tables[i];
         const fs::path file = Normal(m_roots.tables / FromUtf8(table.path));
         m_tablesByFolder[file.parent_path()].push_back({ ToLower(file.stem().string()), i });
         if (!table.rom.empty())
            m_tablesByRom[ToLower(table.rom)].push_back(i);
         if (!table.romAlias.empty())
            m_tablesByRom[ToLower(table.romAlias)].push_back(i);
         for (const string& pack : table.pupPacks)
            m_tablesByPack[ToLower(pack)].push_back(i);
      }
      // The folder of a table holds the files it needs, and the ones of the other tables in it: the tables folder itself is no table folder
      for (const auto& [folder, tables] : m_tablesByFolder) {
         if (folder == tablesRoot)
            continue;
         std::vector<int>& owners = m_folderTables[folder];
         for (const auto& [stem, index] : tables)
            owners.push_back(index);
      }
   }

   void ScanTables()
   {
      const fs::path root = Normal(m_roots.tables);
      Walk(root, "tables", [this, &root](const fs::path& folder) {
         // Scanned with their own root, when they are in the tables folder
         if ((!m_roots.pinmame.empty() && folder == Normal(m_roots.pinmame)) || (!m_roots.pup.empty() && folder == Normal(m_roots.pup)))
            return false;
         // Cache of the textures of a table, built again when it is played
         return ToLower(folder.filename().string()) != "cache" || !m_tablesByFolder.contains(folder.parent_path());
      }, [this, &root](const fs::path& file, const string& relative) { AddTableFile(root, file, relative); });
   }

   void ScanPinmame()
   {
      if (m_roots.pinmame.empty())
         return;
      const fs::path root = Normal(m_roots.pinmame);
      Walk(root, "pinmame", nullptr, [this, &root](const fs::path& file, const string& relative) {
         const fs::path rel = FromUtf8(relative);
         auto part = rel.begin();
         const string first = ToLower(part->string());
         if (++part == rel.end()) {
            // Files at the root of the PinMAME folder: its alias file, and others (ini...) that go with the ROMs
            if (first == "alias.txt")
               Add(file, Item::Aliases, "pinmame", relative, {}, true);
            else
               Add(file, Item::Roms, "pinmame", relative, {}, false);
            return;
         }
         const fs::path second = *part;
         const bool inFolder = ++part != rel.end();
         // ROMs, nvram, cfg and hi files are named after their game, altcolor and altsound have a folder per game
         Item item = Item::Roms;
         if (first == "nvram" || first == "cfg" || first == "hi" || first == "memcard")
            item = Item::Saves;
         const bool perGameFolder = first == "altcolor" || first == "altsound";
         const bool perGameFile = first == "roms" || item == Item::Saves;
         std::vector<int> tables;
         if (perGameFolder && inFolder) {
            tables = TablesOfRom(second.string());
            AddTablePath(tables, "pinmame/" + ToUtf8(*rel.begin() / second) + '/');
         }
         else if (perGameFile) {
            tables = TablesOfRom(inFolder ? second.string() : second.stem().string());
            AddTablePath(tables, inFolder ? "pinmame/" + ToUtf8(*rel.begin() / second) + '/' : "pinmame/" + relative);
         }
         Add(file, item, "pinmame", relative, std::move(tables), false);
      });
   }

   void ScanPup()
   {
      if (m_roots.pup.empty())
         return;
      Walk(Normal(m_roots.pup), "pup", nullptr, [this](const fs::path& file, const string& relative) {
         const fs::path rel = FromUtf8(relative);
         if (std::next(rel.begin()) == rel.end())
            return; // Archives of packs, imported by the next scan of the library, and other files out of the packs
         const string pack = rel.begin()->string();
         std::vector<int> tables;
         if (const auto it = m_tablesByPack.find(ToLower(pack)); it != m_tablesByPack.end())
            tables = it->second;
         AddTablePath(tables, "pup/" + pack + '/');
         Add(file, Item::Games, "pup", relative, std::move(tables), false);
      });
   }

   void ScanPref()
   {
      for (const auto& [name, item] : PREF_FILES) {
         const fs::path file = m_roots.pref / name;
         std::error_code ec;
         if (!fs::is_regular_file(fs::symlink_status(file, ec)))
            continue;
         // The room of the lobby taken from a table outside of the tables folder would point to nothing on another device
         if (string(name) == "lobby-room.json") {
            try {
               std::ifstream stream(file);
               if (fs::path(nlohmann::json::parse(stream).value("table", ""s)).is_absolute()) {
                  m_catalog.skipped.push_back(ToUtf8(file) + ": the room of the lobby comes from a table outside of the tables folder");
                  continue;
               }
            }
            catch (const std::exception&) {
               continue;
            }
         }
         Add(file, item, "pref", name, {}, true);
      }
   }

private:
   void Walk(const fs::path& root, const char* rootName, const std::function<bool(const fs::path&)>& enterFolder,
      const std::function<void(const fs::path&, const string&)>& addFile)
   {
      std::error_code ec;
      if (!fs::is_directory(root, ec))
         return;
      for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
         const fs::path path = it->path();
         const string name = path.filename().string();
         std::error_code statusEc;
         const fs::file_status status = it->symlink_status(statusEc);
         if (fs::is_directory(status)) {
            if (IsStagingName(name) || (enterFolder && !enterFolder(Normal(path))))
               it.disable_recursion_pending();
            continue;
         }
         if (IsJunkFile(name))
            continue;
         const string relative = ToUtf8(path.lexically_relative(root));
         if (fs::is_symlink(status)) {
            m_catalog.skipped.push_back(string(rootName) + '/' + relative + ": symbolic link");
            continue;
         }
         if (!fs::is_regular_file(status))
            continue;
         if (!IsValidUtf8(relative)) {
            m_catalog.skipped.push_back(string(rootName) + '/' + relative + ": the name is not UTF-8");
            continue;
         }
         addFile(path, relative);
      }
   }

   void AddTableFile(const fs::path& root, const fs::path& file, const string& relative)
   {
      const fs::path folder = file.parent_path();
      const string extension = ToLower(file.extension().string());
      if (folder == root && (extension == ".zip" || extension == ".vpxz" || extension == ".rar" || extension == ".7z")) {
         m_catalog.skipped.push_back("tables/" + relative + ": archive waiting to be imported by the table library");
         return;
      }
      if (extension == ".vpxbackup") // A backup uploaded to the device, not something to back up again
         return;

      // The table folder holding the file (the closest folder with tables, below the tables folder)
      fs::path tableFolder;
      for (fs::path f = folder; f != root && f.has_relative_path() && f.parent_path() != f; f = f.parent_path())
         if (m_folderTables.contains(f)) {
            tableFolder = f;
            break;
         }
      std::vector<int> folderTables;
      if (!tableFolder.empty()) {
         folderTables = m_folderTables[tableFolder];
         AddTablePath(folderTables, "tables/" + ToUtf8(tableFolder.lexically_relative(root)) + '/');
      }

      // Inside a table folder: its own PinMAME folder (used instead of the shared one when it has a 'roms' folder), and the VPReg files of its scripts
      if (!tableFolder.empty()) {
         const fs::path inFolder = file.lexically_relative(tableFolder);
         const string first = ToLower(inFolder.begin()->string());
         if (first == "pinmame" && std::next(inFolder.begin()) != inFolder.end()) {
            const auto second = std::next(inFolder.begin());
            const string sub = ToLower(second->string());
            const bool atRoot = std::next(second) == inFolder.end();
            const Item item = atRoot && sub == "alias.txt" ? Item::Aliases : (sub == "nvram" || sub == "cfg" || sub == "hi" || sub == "memcard") ? Item::Saves : Item::Roms;
            Add(file, item, "tables", relative, folderTables, false);
            return;
         }
      }
      // What the scripts save (VPReg.ini, VPReg.stg and their own files) in the 'user' folder along the tables, or shared by the tables of the tables folder
      if (const fs::path inFolder = file.lexically_relative(tableFolder.empty() ? root : tableFolder); ToLower(inFolder.begin()->string()) == "user" && std::next(inFolder.begin()) != inFolder.end()) {
         std::vector<int> tables = folderTables;
         if (tableFolder.empty()) { // Shared by the tables at the root of the tables folder
            if (const auto it = m_tablesByFolder.find(root); it != m_tablesByFolder.end())
               for (const auto& [stem, index] : it->second)
                  tables.push_back(index);
            AddTablePath(tables, "tables/" + ToUtf8(*inFolder.begin()) + '/');
         }
         Add(file, Item::Saves, "tables", relative, std::move(tables), false);
         return;
      }

      // Along a table: its file, its image, its settings (<table>.ini, or <table><profile>.ini), and the files named after it
      if (const auto it = m_tablesByFolder.find(folder); it != m_tablesByFolder.end()) {
         const string stem = ToLower(file.stem().string());
         int owner = -1;
         size_t ownerStemLength = 0;
         for (const auto& [tableStem, index] : it->second) {
            const bool match = extension == ".ini" ? stem.starts_with(tableStem) : stem == tableStem;
            if (match && tableStem.size() >= ownerStemLength) {
               owner = index;
               ownerStemLength = tableStem.size();
            }
         }
         if (owner >= 0) {
            const Item item = (extension == ".png" || extension == ".jpg") ? Item::Thumbnails : extension == ".ini" ? Item::Settings : Item::Games;
            AddTablePath({ owner }, "tables/" + relative);
            Add(file, item, "tables", relative, { owner }, false);
            return;
         }
      }
      Add(file, Item::Games, "tables", relative, std::move(folderTables), false);
   }

   std::vector<int> TablesOfRom(const string& game) const
   {
      const auto it = m_tablesByRom.find(ToLower(game));
      return it == m_tablesByRom.end() ? std::vector<int>() : it->second;
   }

   void AddTablePath(const std::vector<int>& tables, const string& path)
   {
      for (int index : tables) {
         std::vector<string>& paths = m_catalog.tablePaths[index];
         if (std::find(paths.begin(), paths.end(), path) == paths.end())
            paths.push_back(path);
      }
   }

   void Add(const fs::path& file, Item item, const char* rootName, const string& relative, std::vector<int> tables, bool global)
   {
      std::error_code ec;
      FileEntry entry;
      entry.path = file;
      entry.item = item;
      entry.zipPath = string(GetItemId(item)) + '/' + rootName + '/' + relative;
      entry.size = fs::file_size(file, ec);
      if (ec) {
         m_catalog.skipped.push_back(string(rootName) + '/' + relative + ": " + ec.message());
         return;
      }
      entry.modifiedAt = ModifiedAt(file).value_or(0);
      std::sort(tables.begin(), tables.end());
      tables.erase(std::unique(tables.begin(), tables.end()), tables.end());
      entry.tables = std::move(tables);
      entry.global = global;
      m_catalog.files.push_back(std::move(entry));
   }

   const Roots& m_roots;
   Catalog& m_catalog;
   std::map<fs::path, std::vector<std::pair<string, int>>> m_tablesByFolder; // Folder of the .vpx files: lower case stem, table index
   std::map<fs::path, std::vector<int>> m_folderTables; // Table folders (not the tables folder itself): the tables in it
   std::map<string, std::vector<int>> m_tablesByRom; // Lower case game name
   std::map<string, std::vector<int>> m_tablesByPack; // Lower case pack name
};

}

Catalog Scan(const Roots& roots, const std::vector<TableInfo>& tables)
{
   Catalog catalog;
   catalog.tables = tables;
   Scanner scanner(roots, catalog);
   scanner.ScanTables();
   scanner.ScanPinmame();
   scanner.ScanPup();
   scanner.ScanPref();
   return catalog;
}

std::vector<const FileEntry*> Select(const Catalog& catalog, const Selection& selection)
{
   std::set<int> tables;
   if (!selection.allTables)
      for (int i = 0; i < static_cast<int>(catalog.tables.size()); i++)
         if (std::find(selection.tables.begin(), selection.tables.end(), catalog.tables[i].uuid) != selection.tables.end())
            tables.insert(i);
   std::vector<const FileEntry*> files;
   for (const FileEntry& file : catalog.files) {
      if (!selection.items[static_cast<int>(file.item)])
         continue;
      const bool selected = selection.allTables || file.global || (file.tables.empty() ? selection.otherFiles : std::any_of(file.tables.begin(), file.tables.end(), [&tables](int i) { return tables.contains(i); }));
      if (selected)
         files.push_back(&file);
   }
   return files;
}

string BuildManifest(const Catalog& catalog, const std::vector<const FileEntry*>& files, const string& appVersion, int64_t createdAt)
{
   // The tables offered by the restore page: the ones whose files are in the backup (not the ones only sharing a folder of saves with them)
   std::map<Item, std::pair<uint64_t, uint64_t>> counts; // Files, bytes
   std::set<int> tables, linkedTables;
   for (const FileEntry* file : files) {
      counts[file->item].first++;
      counts[file->item].second += file->size;
      if (file->item == Item::Games || file->item == Item::Thumbnails || file->item == Item::Roms)
         tables.insert(file->tables.begin(), file->tables.end());
      linkedTables.insert(file->tables.begin(), file->tables.end());
   }
   if (tables.empty()) // A backup of saves or settings only
      tables = std::move(linkedTables);
   nlohmann::ordered_json items = nlohmann::ordered_json::object();
   for (const auto& [item, count] : counts)
      items[GetItemId(item)] = { { "files", count.first }, { "bytes", count.second } };
   nlohmann::ordered_json tableList = nlohmann::ordered_json::array();
   for (int index : tables) {
      const TableInfo& table = catalog.tables[index];
      tableList.push_back({ { "uuid", table.uuid }, { "name", table.name }, { "path", table.path }, { "rom", table.rom }, { "paths", catalog.tablePaths[index] } });
   }
   const nlohmann::ordered_json manifest = {
      { "format", "vpinball-steam-frame-backup" },
      { "version", 1 },
      { "app", appVersion },
      { "device", GetDeviceClass() },
      { "createdAt", createdAt },
      { "items", items },
      { "tables", tableList },
   };
   return manifest.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

///////////////////////////////////////////////////////////////////////////////
// Archive

Archive::Archive(const std::vector<const FileEntry*>& files, const string& manifest, int64_t createdAt)
   : m_createdAt(createdAt)
{
   const auto addContent = [this](File file, string content) {
      file.size = content.size();
      file.crc = Crc32(0, reinterpret_cast<const uint8_t*>(content.data()), content.size());
      file.content = std::move(content);
      m_files.push_back(std::move(file));
   };
   addContent({ fs::path(), MANIFEST_NAME, 0, createdAt }, manifest);
   for (const FileEntry* file : files) {
      if (file->global) {
         std::ifstream stream(file->path, std::ios::binary);
         std::stringstream content;
         content << stream.rdbuf();
         addContent({ file->path, file->zipPath, 0, file->modifiedAt }, content.str());
         continue;
      }
      m_files.push_back({ file->path, file->zipPath, file->size, file->modifiedAt });
      m_totalBytes += file->size;
   }
}

string Archive::GetError() const
{
   std::lock_guard lock(m_errorMutex);
   return m_error;
}

void Archive::SetError(const string& error)
{
   std::lock_guard lock(m_errorMutex);
   if (m_error.empty())
      m_error = error;
}

bool Archive::ComputeChecksums(const std::atomic<bool>& cancel, string& error)
{
   std::vector<uint8_t> buffer(size_t(1) << 20);
   for (File& file : m_files) {
      if (file.content)
         continue;
      std::error_code ec;
      if (fs::file_size(file.path, ec) != file.size || ec || ModifiedAt(file.path).value_or(0) != file.modifiedAt) {
         error = ToUtf8(file.path) + " has changed since the backup was prepared";
         return false;
      }
      std::ifstream stream(file.path, std::ios::binary);
      uint32_t crc = 0;
      uint64_t remaining = file.size;
      while (remaining > 0 && stream) {
         if (cancel)
            return false;
         stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(std::min<uint64_t>(buffer.size(), remaining)));
         const auto read = static_cast<size_t>(stream.gcount());
         if (read == 0)
            break;
         crc = Crc32(crc, buffer.data(), read);
         remaining -= read;
         m_checkedBytes += read;
      }
      if (remaining > 0) {
         error = "Failed to read " + ToUtf8(file.path);
         return false;
      }
      file.crc = crc;
   }
   Layout();
   return true;
}

void Archive::Layout()
{
   m_segments.clear();
   uint64_t offset = 0;
   const auto addSegment = [this, &offset](uint64_t size, int file, string data) {
      m_segments.push_back({ offset, size, file, std::move(data) });
      offset += size;
   };
   for (size_t i = 0; i < m_files.size(); i++) {
      File& file = m_files[i];
      file.headerOffset = offset;
      const bool zip64Sizes = file.size >= ZIP32_MAX;
      const auto [date, time] = DosDateTime(file.modifiedAt);
      string header;
      Put32(header, 0x04034b50);
      Put16(header, zip64Sizes ? VERSION_ZIP64 : VERSION_DEFAULT);
      Put16(header, FLAG_UTF8);
      Put16(header, 0); // Stored
      Put16(header, time);
      Put16(header, date);
      Put32(header, file.crc);
      Put32(header, zip64Sizes ? ZIP32_MAX : static_cast<uint32_t>(file.size));
      Put32(header, zip64Sizes ? ZIP32_MAX : static_cast<uint32_t>(file.size));
      Put16(header, static_cast<uint16_t>(file.name.size()));
      Put16(header, static_cast<uint16_t>((zip64Sizes ? 20 : 0) + 9));
      header += file.name;
      if (zip64Sizes) {
         Put16(header, 0x0001);
         Put16(header, 16);
         Put64(header, file.size);
         Put64(header, file.size);
      }
      PutTimestamp(header, file.modifiedAt);
      addSegment(header.size(), -1, std::move(header));
      if (file.content)
         addSegment(file.size, -1, *file.content);
      else
         addSegment(file.size, static_cast<int>(i), string());
   }

   const uint64_t directoryOffset = offset;
   string directory;
   for (const File& file : m_files) {
      const bool zip64Sizes = file.size >= ZIP32_MAX;
      const bool zip64Offset = file.headerOffset >= ZIP32_MAX;
      const uint16_t zip64Length = (zip64Sizes ? 16 : 0) + (zip64Offset ? 8 : 0);
      const auto [date, time] = DosDateTime(file.modifiedAt);
      Put32(directory, 0x02014b50);
      Put16(directory, (3 << 8) | VERSION_ZIP64); // Made on Unix, for the permissions of the extracted files
      Put16(directory, zip64Length > 0 ? VERSION_ZIP64 : VERSION_DEFAULT);
      Put16(directory, FLAG_UTF8);
      Put16(directory, 0);
      Put16(directory, time);
      Put16(directory, date);
      Put32(directory, file.crc);
      Put32(directory, zip64Sizes ? ZIP32_MAX : static_cast<uint32_t>(file.size));
      Put32(directory, zip64Sizes ? ZIP32_MAX : static_cast<uint32_t>(file.size));
      Put16(directory, static_cast<uint16_t>(file.name.size()));
      Put16(directory, static_cast<uint16_t>((zip64Length > 0 ? 4 + zip64Length : 0) + 9));
      Put16(directory, 0); // Comment
      Put16(directory, 0); // Disk
      Put16(directory, 0); // Internal attributes
      Put32(directory, 0100644u << 16); // rw-r--r--
      Put32(directory, zip64Offset ? ZIP32_MAX : static_cast<uint32_t>(file.headerOffset));
      directory += file.name;
      if (zip64Length > 0) {
         Put16(directory, 0x0001);
         Put16(directory, zip64Length);
         if (zip64Sizes) {
            Put64(directory, file.size);
            Put64(directory, file.size);
         }
         if (zip64Offset)
            Put64(directory, file.headerOffset);
      }
      PutTimestamp(directory, file.modifiedAt);
   }
   const uint64_t directorySize = directory.size();
   const uint64_t entries = m_files.size();
   if (entries >= 0xFFFF || directorySize >= ZIP32_MAX || directoryOffset >= ZIP32_MAX) {
      const uint64_t zip64EndOffset = directoryOffset + directorySize;
      Put32(directory, 0x06064b50); // Zip64 end of central directory
      Put64(directory, 44);
      Put16(directory, (3 << 8) | VERSION_ZIP64);
      Put16(directory, VERSION_ZIP64);
      Put32(directory, 0);
      Put32(directory, 0);
      Put64(directory, entries);
      Put64(directory, entries);
      Put64(directory, directorySize);
      Put64(directory, directoryOffset);
      Put32(directory, 0x07064b50); // Its locator
      Put32(directory, 0);
      Put64(directory, zip64EndOffset);
      Put32(directory, 1);
   }
   Put32(directory, 0x06054b50);
   Put16(directory, 0);
   Put16(directory, 0);
   Put16(directory, static_cast<uint16_t>(std::min<uint64_t>(entries, 0xFFFF)));
   Put16(directory, static_cast<uint16_t>(std::min<uint64_t>(entries, 0xFFFF)));
   Put32(directory, static_cast<uint32_t>(std::min<uint64_t>(directorySize, ZIP32_MAX)));
   Put32(directory, static_cast<uint32_t>(std::min<uint64_t>(directoryOffset, ZIP32_MAX)));
   Put16(directory, 0);
   addSegment(directory.size(), -1, std::move(directory));
   m_size = offset;
}

// Reads an archive for one connection (a resumed download opens another one)
class Archive::Reader final
{
public:
   explicit Reader(std::shared_ptr<Archive> archive)
      : m_archive(std::move(archive))
   {
   }

   size_t Read(uint8_t* buffer, size_t length)
   {
      Archive& archive = *m_archive;
      size_t total = 0;
      while (total < length && m_position < archive.m_size) {
         const auto segment = std::prev(std::upper_bound(archive.m_segments.begin(), archive.m_segments.end(), m_position, [](uint64_t position, const Segment& s) { return position < s.offset; }));
         const uint64_t inSegment = m_position - segment->offset;
         const size_t count = static_cast<size_t>(std::min<uint64_t>(length - total, segment->size - inSegment));
         if (segment->file < 0)
            memcpy(buffer + total, segment->data.data() + inSegment, count);
         else if (!ReadFile(segment->file, inSegment, buffer + total, count))
            return Fail();
         total += count;
         m_position += count;
      }
      uint64_t sent = archive.m_bytesSent.load();
      while (m_position > sent && !archive.m_bytesSent.compare_exchange_weak(sent, m_position)) { }
      return total;
   }

   void Seek(uint64_t position) { m_position = std::min(position, m_archive->m_size); }

   struct mg_connection* m_connection = nullptr;

private:
   bool ReadFile(int index, uint64_t offset, uint8_t* buffer, size_t length)
   {
      const File& file = m_archive->m_files[index];
      if (m_file != index) {
         m_stream = std::ifstream();
         std::error_code ec;
         if (fs::file_size(file.path, ec) != file.size || ec || ModifiedAt(file.path).value_or(0) != file.modifiedAt) {
            m_archive->SetError(ToUtf8(file.path) + " has changed since the backup was prepared");
            return false;
         }
         m_stream.open(file.path, std::ios::binary);
         m_file = index;
         m_streamPosition = 0;
      }
      if (m_streamPosition != offset) {
         m_stream.clear();
         m_stream.seekg(static_cast<std::streamoff>(offset));
      }
      m_stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(length));
      if (static_cast<size_t>(m_stream.gcount()) != length) {
         m_archive->SetError("Failed to read " + ToUtf8(file.path));
         m_file = -1;
         return false;
      }
      m_streamPosition = offset + length;
      return true;
   }

   size_t Fail()
   {
      // The connection is closed, as the browser would wait for the rest otherwise
      if (m_connection != nullptr)
         m_connection->is_closing = 1;
      return 0;
   }

   std::shared_ptr<Archive> m_archive;
   uint64_t m_position = 0;
   int m_file = -1;
   std::ifstream m_stream;
   uint64_t m_streamPosition = 0;
};

///////////////////////////////////////////////////////////////////////////////
// File system of the archives, for mongoose

namespace {

std::mutex s_archivesMutex;
std::map<string, std::shared_ptr<Archive>> s_archives;

std::shared_ptr<Archive> FindArchive(const char* path)
{
   const string name = path;
   if (!name.ends_with(".zip"))
      return nullptr; // mg_http_serve_file looks for a '.gz' version first
   std::lock_guard lock(s_archivesMutex);
   const auto it = s_archives.find(name.substr(0, name.size() - 4));
   return it == s_archives.end() ? nullptr : it->second;
}

int ArchiveStat(const char* path, size_t* size, time_t* mtime)
{
   const std::shared_ptr<Archive> archive = FindArchive(path);
   if (archive == nullptr)
      return 0;
   if (size != nullptr)
      *size = static_cast<size_t>(archive->GetSize());
   if (mtime != nullptr)
      *mtime = static_cast<time_t>(archive->GetCreatedAt());
   return MG_FS_READ;
}

void ArchiveList(const char*, void (*)(const char*, void*), void*) { }

void* ArchiveOpen(const char* path, int flags)
{
   const std::shared_ptr<Archive> archive = (flags & MG_FS_WRITE) ? nullptr : FindArchive(path);
   return archive == nullptr ? nullptr : new Archive::Reader(archive);
}

void ArchiveClose(void* fd) { delete static_cast<Archive::Reader*>(fd); }

size_t ArchiveRead(void* fd, void* buffer, size_t length) { return static_cast<Archive::Reader*>(fd)->Read(static_cast<uint8_t*>(buffer), length); }

size_t ArchiveWrite(void*, const void*, size_t) { return 0; }

size_t ArchiveSeek(void* fd, size_t offset)
{
   static_cast<Archive::Reader*>(fd)->Seek(offset);
   return offset;
}

bool ArchiveMove(const char*, const char*) { return false; }
bool ArchiveRemove(const char*) { return false; }
bool ArchiveMakeDirectory(const char*) { return false; }

struct mg_fs s_archiveFileSystem = { ArchiveStat, ArchiveList, ArchiveOpen, ArchiveClose, ArchiveRead, ArchiveWrite, ArchiveSeek, ArchiveMove, ArchiveRemove, ArchiveMakeDirectory };

}

struct mg_fs* GetArchiveFileSystem() { return &s_archiveFileSystem; }

void RegisterArchive(const string& id, std::shared_ptr<Archive> archive)
{
   std::lock_guard lock(s_archivesMutex);
   s_archives[id] = std::move(archive);
}

void UnregisterArchive(const string& id)
{
   std::lock_guard lock(s_archivesMutex);
   s_archives.erase(id);
}

void AttachConnection(struct mg_connection* c)
{
   const auto* fd = static_cast<const struct mg_fd*>(c->pfn_data);
   if (fd != nullptr && fd->fs == &s_archiveFileSystem)
      static_cast<Archive::Reader*>(fd->fd)->m_connection = c;
}

///////////////////////////////////////////////////////////////////////////////
// Restore

std::optional<fs::path> GetRestorePath(const Roots& roots, const string& zipPath, Item& item)
{
   // '<item>/<root>/<path>', with the components a file name may have: nothing absolute, no '..', no staging folders, no control characters
   std::vector<string> parts;
   size_t start = 0;
   for (;;) {
      const size_t end = zipPath.find('/', start);
      parts.push_back(zipPath.substr(start, end == string::npos ? string::npos : end - start));
      if (end == string::npos)
         break;
      start = end + 1;
   }
   if (parts.size() < 3 || !IsValidUtf8(zipPath))
      return std::nullopt;
   for (const string& part : parts) {
      if (part.empty() || part == "." || part == ".." || IsStagingName(part))
         return std::nullopt;
      if (std::any_of(part.begin(), part.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F || c == '\\' || c == ':'; }))
         return std::nullopt;
   }
   const std::optional<Item> parsedItem = ParseItem(parts[0]);
   const fs::path* root = roots.Find(parts[1]);
   if (!parsedItem || root == nullptr)
      return std::nullopt;
   // Only the files of the settings folder that backups hold
   if (parts[1] == "pref" && (parts.size() != 3 || std::none_of(std::begin(PREF_FILES), std::end(PREF_FILES), [&parts](const auto& file) { return parts[2] == file.first; })))
      return std::nullopt;
   string relative;
   for (size_t i = 2; i < parts.size(); i++)
      relative += (i > 2 ? "/" : "") + parts[i];
   const fs::path path = *root / FromUtf8(relative);
   if (!IsInside(path, *root))
      return std::nullopt;
   item = *parsedItem;
   return path;
}

bool IsMergedEntry(const string& zipPath)
{
   return std::any_of(std::begin(MERGED_ENTRIES), std::end(MERGED_ENTRIES), [&zipPath](const char* entry) { return zipPath == entry; });
}

string GetDeviceClass()
{
#if defined(VPX_STEAM_FRAME_HEADSET)
   return "steam-frame"s;
#elif defined(__APPLE__)
   return "macos"s;
#elif defined(_WIN32)
   return "windows"s;
#else
   return "linux"s;
#endif
}

fs::path GetPendingSettingsPath() { return g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "VPinballX.restore.ini"); }

void SavePendingSettings(const fs::path& restoredIni, const string& sourceDevice)
{
   mINI::INIStructure ini;
   mINI::INIFile(restoredIni).read(ini);
   ini[PENDING_SETTINGS_SECTION]["SourceDevice"] = sourceDevice;
   if (!mINI::INIFile(GetPendingSettingsPath()).generate(ini, true)) {
      PLOGE << "Failed to save the restored settings to " << GetPendingSettingsPath();
   }
}

namespace {

// Settings that belong to the device rather than to the user: its folders, displays, audio and input devices, the position of the table in
// its room... A backup made on another kind of device (see GetDeviceClass) does not restore the settings of the rendering either.
bool IsPortableSetting(const string& sectionName, const string& keyName, bool sameDevice, const std::set<string>& pathSettings)
{
   // VPX builds mINI case sensitive (see LayeredINIPropertyStore.h), the names are compared ignoring case
   const string section = ToLower(sectionName), key = ToLower(keyName);
   static const std::set<string> excludedSections = { "version", "recentdir", "window", "backglass", "scoreview", "topper", "input", ToLower(PENDING_SETTINGS_SECTION) };
   static const std::set<string> excludedKeys = {
      "standalone.tablespath", "standalone.renderingmodeoverride",
      "player.sounddevice", "player.sounddevicebg", "player.bgset", "player.cachemode", "player.gfxbackend",
      "playervr.orientation", "playervr.tablex", "playervr.tabley", "playervr.tablez", "playervr.controllercabyoffset", "playervr.controllerlockbarscale", "playervr.desktopdisplay",
      "global.enablelog", "global.logscriptoutput", "global.resetlogonplay",
      "plugin.pinmame.pinmamepath", "plugin.pup.pupfolder",
   };
   static const std::set<string> renderingKeys = {
      "player.aafactor", "player.msaasamples", "player.fxaa", "player.sharpen", "player.ssrefl", "player.pfreflection", "player.disableao", "player.dynamicao",
      "player.forceanisotropicfiltering", "player.compresstextures", "player.maxtexdimension", "player.syncmode", "player.maxframerate", "player.maxprerenderedframes",
      "player.alpharampaccuracy", "playervr.resfactor", "playervr.displayrefreshrate",
   };
   const string id = section + '.' + key;
   if (excludedSections.contains(section) || excludedKeys.contains(id) || pathSettings.contains(id))
      return false;
   if (section == "standalone" && key.starts_with("webserver"))
      return false;
   if (section == "player" && (key.starts_with("playfield") || key.starts_with("screen") || key.starts_with("lockbar")))
      return false;
   // Settings of plugins that are not loaded are not in the registry: the ones that look like folders are left out as well
   if (key.ends_with("path") || key.ends_with("folder") || key.ends_with("dir") || key.ends_with("directory"))
      return false;
   if (!sameDevice && (renderingKeys.contains(id) || (section == "player" && key.starts_with("hdr"))
      || (section == "playervr" && (key.starts_with("dynamicresolution") || key.starts_with("foveation")))))
      return false;
   return true;
}

}

void ApplyPendingSettings()
{
   const fs::path pendingPath = GetPendingSettingsPath();
   std::error_code ec;
   if (!fs::exists(pendingPath, ec))
      return;
   mINI::INIStructure restored;
   if (!mINI::INIFile(pendingPath).read(restored)) {
      PLOGE << "Failed to read the restored settings " << pendingPath;
      fs::remove(pendingPath, ec);
      return;
   }
   const bool sameDevice = restored[PENDING_SETTINGS_SECTION]["SourceDevice"] == GetDeviceClass();

   // The settings holding a path (in lower case, see IsPortableSetting)
   std::set<string> pathSettings;
   const VPX::Properties::PropertyRegistry& registry = Settings::GetRegistry();
   for (const VPX::Properties::PropertyRegistry::PropId id : registry.GetPropertyIds())
      if (const VPX::Properties::StringPropertyDef* prop = registry.GetStringProperty(id); prop != nullptr && prop->m_pathType != VPX::Properties::StringPropertyDef::PathType::None)
         pathSettings.insert(ToLower(prop->m_groupId + '.' + prop->m_propId));

   // The file is updated, then loaded again: the settings that are kept in memory (changed since the last save) are saved first
   Settings& settings = g_settingsService.GetAppSettings();
   if (settings.IsModified())
      settings.Save();
   mINI::INIStructure current;
   mINI::INIFile(settings.GetIniPath()).read(current);
   int applied = 0, skipped = 0;
   for (const auto& [section, keys] : restored)
      for (const auto& [key, value] : keys) {
         if (!IsPortableSetting(section, key, sameDevice, pathSettings)) {
            if (section != PENDING_SETTINGS_SECTION)
               skipped++;
            continue;
         }
         current[section][key] = value;
         applied++;
      }
   if (!mINI::INIFile(settings.GetIniPath()).write(current, true)) {
      PLOGE << "Failed to write the restored settings to " << settings.GetIniPath();
      return;
   }
   settings.Load(false);
   fs::remove(pendingPath, ec);
   PLOGI << "Settings restored from a backup: " << applied << " applied, " << skipped << " left out as they belong to the device"
         << (sameDevice ? "" : " (made on another kind of device, its rendering settings are left out)");
}

string MergeAliases(const string& current, const string& restored)
{
   const auto aliasOf = [](const string& line) -> std::optional<string> {
      const size_t start = line.find_first_not_of(" \t");
      if (start == string::npos || line[start] == '#' || line[start] == ';' || line.compare(start, 2, "//") == 0)
         return std::nullopt;
      const size_t comma = line.find(',', start);
      if (comma == string::npos)
         return std::nullopt;
      string alias = line.substr(start, comma - start);
      alias.erase(alias.find_last_not_of(" \t") + 1);
      return ToLower(alias);
   };
   std::set<string> defined;
   std::istringstream currentLines(current);
   for (string line; std::getline(currentLines, line);)
      if (const std::optional<string> alias = aliasOf(line))
         defined.insert(*alias);
   string added;
   std::istringstream restoredLines(restored);
   for (string line; std::getline(restoredLines, line);) {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      if (const std::optional<string> alias = aliasOf(line); alias && defined.insert(*alias).second)
         added += line + '\n';
   }
   if (added.empty())
      return current;
   string merged = current;
   if (!merged.empty() && merged.back() != '\n')
      merged += '\n';
   return merged + "# Restored from a backup\n" + added;
}

///////////////////////////////////////////////////////////////////////////////
// Keep awake

KeepAwake::KeepAwake()
{
#ifndef _WIN32
   // A child that holds the inhibition until it is stopped (or for 6 hours, if the application dies): systemd's sleep inhibitor on Linux,
   // caffeinate on macOS. Forked by hand, so that it does not inherit the sockets of the web server.
#ifdef __APPLE__
   const char* const argv[] = { "/usr/bin/caffeinate", "-i", "-t", "21600", nullptr };
#else
   const char* const argv[] = { "systemd-inhibit", "--what=sleep:idle", "--who=Visual Pinball", "--why=Backup or restore in progress", "--mode=block", "sleep", "21600", nullptr };
#endif
   const pid_t pid = fork();
   if (pid == 0) {
      // The thread that forks may block signals, which the child would keep through exec
      sigset_t signals;
      sigemptyset(&signals);
      sigprocmask(SIG_SETMASK, &signals, nullptr);
      for (int fd = 3; fd < 1024; fd++)
         close(fd);
      execvp(argv[0], const_cast<char* const*>(argv));
      _exit(127);
   }
   if (pid > 0)
      m_pid = pid;
   else {
      PLOGW << "Failed to keep the device awake during the transfer";
   }
#endif
}

KeepAwake::~KeepAwake()
{
#ifndef _WIN32
   // Killed, which can not be ignored, so that this never waits (the inhibition of systemd ends when its file descriptor is closed by the kernel)
   if (m_pid > 0) {
      kill(m_pid, SIGKILL);
      waitpid(m_pid, nullptr, 0);
   }
#endif
}

}
