// license:GPLv3+

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "RomAliases.h"

namespace VPinballLib {

// A table known to the library. 'path' and 'image' are relative to the tables folder and always use '/'.
struct Table {
   std::string uuid;
   std::string name;
   std::string path;
   std::string image;
   int64_t createdAt = 0;
   int64_t modifiedAt = 0;

   // Player statistics, which are not part of tables.json (see TableLibrary::Config::statsPath)
   int64_t lastPlayedAt = 0;
   int playCount = 0;
   bool favorite = false;

   // PinUP Player pack, not part of tables.json either: what the script says (see Config::readPupUse), cached along the statistics with the
   // modification date of the table it was read from, and whether one of the packs it names is in a 'pupvideos' folder (computed, not saved)
   bool usesPup = false;
   std::vector<std::string> pupPacks;
   int64_t pupScannedAt = 0;
   bool hasPupPack = false;
   std::string rom; // The PinMAME ROM the script names (cGameName), read and cached along the packs
};

// Table database shared by the launchers: keeps tables.json in sync with the content of the tables folder.
//
// This is the C++ counterpart of TableManager.kt (Android) and TableManager.swift (iOS) and reads/writes
// the same tables.json: {"tableCount":N,"tables":[{uuid,name,path,image,createdAt,modifiedAt}]}
//
// It only depends on the standard library (zip support and logging are injected) and is thread safe, as
// the web server triggers rescans from its own thread.
class TableLibrary final
{
public:
   enum class LogLevel { Info, Warn, Error };

   using ProgressCallback = std::function<void(int progress, const std::string& message)>;
   using ZipProgressCallback = std::function<void(int current, int total, const char* filename)>;
   using ZipFunction = std::function<bool(const std::filesystem::path& sourcePath, const std::filesystem::path& destPath, ZipProgressCallback callback)>;
   using LogCallback = std::function<void(LogLevel level, const std::string& message)>;
   struct PupUse {
      bool usesPup = false; // The script creates PinUP Player
      std::vector<std::string> names; // Candidate pack names, by order of preference
      std::string rom; // PinMAME ROM, empty for the other tables
      std::vector<std::string> games; // Other PinMAME games the script quotes, clues for the alias of a 'rom' PinMAME does not know
   };

   struct Config {
      std::filesystem::path tablesPath; // Folder owned by the library, one sub folder per table
      std::filesystem::path jsonPath; // Location of tables.json
      std::filesystem::path statsPath; // Play counts, last played dates and favorites, by table uuid. Defaults to 'table-stats.json' along tables.json. This
                                       // is a separate file as the mobile launchers, which share the tables.json format, reject the fields they do not know
      std::vector<std::filesystem::path> excludedPaths; // Folders inside tablesPath that must not be scanned
      ZipFunction zip; // Usually ZipUtils::Zip, needed by Export
      ZipFunction unzip; // Usually ZipUtils::Extract, needed to import the bundles of 'archiveExtensions'
      std::vector<std::string> archiveExtensions { ".zip", ".vpxz" }; // Lower case, with the dot: bundles imported with 'unzip'
      LogCallback log;
      // Optional: which PinUP Player packs a table uses, read from its script (see PupPacks::GetTableUse), and the folder of the packs shared by
      // the tables (they are also looked for in a 'pupvideos' folder along each table)
      std::function<PupUse(const std::filesystem::path& tablePath)> readPupUse;
      std::filesystem::path sharedPupVideosPath;
      // Imported packs get their VR layout when they have one, as a PinUP Player user would run its option (see PupPackOptions::ApplyRecommended)
      bool applyVrPupOptions = true;
      // Optional: the ROM folder of PinMAME. ROMs found in imported bundles outside of a table's 'pinmame/roms' folder, and ROM archives
      // dropped in the tables folder under the name of a ROM a table needs, are moved there (see ImportArchive and MoveRootRoms)
      std::filesystem::path romsPath;
      // Optional: the driver catalog of PinMAME (gamedefs/catalog/pinmame.json of its plugin), to recognize the tables asking for a game
      // PinMAME does not know. The library adds the alias they need to the alias file of PinMAME, 'alias.txt' along the ROM folder, and moves
      // the ROM archive of the game found in the table folder to the ROM folder (see ResolveGameAliases). The alias file is created from
      // 'defaultAliasesPath' (VPinMAME's built-in aliases, VPMAlias.txt) when there is none.
      std::filesystem::path pinmameCatalogPath;
      std::filesystem::path defaultAliasesPath;
   };

   explicit TableLibrary(Config config);
   ~TableLibrary();

   // Synchronize with the tables folder: drop entries whose file is gone, register new .vpx files, pick up
   // artwork, and import the bundles (.zip, .vpxz, and the other archiveExtensions) dropped at the root of the folder (deleted once imported).
   // Files modified during the last 'settleSeconds' are considered as still being copied and left for the
   // next rescan: use 0 when the caller knows that transfers are done (startup, web server event, button).
   void Rescan(const ProgressCallback& onProgress = nullptr, int settleSeconds = 0);

   // Same, on a worker thread, for callers that must not block like a UI: poll IsScanning/GetRevision for the outcome.
   // A request made while a scan is running triggers another scan afterward, so that no change is missed.
   void RescanAsync(int settleSeconds = 0);
   bool IsScanning() const { return m_scanning.load(); }

   // Tables sorted by name (case insensitive)
   std::vector<Table> GetTables(bool ascending = true) const;
   std::optional<Table> GetTable(const std::string& uuid) const;
   // Table of a file of the tables folder (nothing for files outside of the library)
   std::optional<Table> FindTable(const std::filesystem::path& fullPath);

   // Incremented each time the table list changes, for UIs that poll
   uint64_t GetRevision() const { return m_revision.load(); }

   // Import a .vpx, .vpxz or .zip located anywhere (the source file is left untouched)
   std::vector<Table> Import(const std::filesystem::path& path, const ProgressCallback& onProgress = nullptr);

   bool Delete(const std::string& uuid);
   // Name displayed for the table (its files are not renamed). Empty or blank: back to the name derived from the file name (see GetDefaultName).
   bool Rename(const std::string& uuid, const std::string& newName);
   // Empty: remove the image, absolute: copy the image next to the table, relative: reference an existing file
   bool SetImage(const std::string& uuid, const std::string& imagePath);
   // Look again for <table>.png|.jpg, typically after a screenshot was captured
   bool ReloadImage(const std::string& uuid);
   bool ResetIni(const std::string& uuid);
   bool SetFavorite(const std::string& uuid, bool favorite);
   // To be called when a table starts to be played, whether it was selected from the library or not (nothing happens for tables outside of the library)
   bool RecordPlay(const std::filesystem::path& fullPath);
   // Zip the table folder as <destFolder>/<name>.vpxz
   std::optional<std::filesystem::path> Export(const std::string& uuid, const std::filesystem::path& destFolder, const ProgressCallback& onProgress = nullptr);

   // ROMs that tables need and are missing from the device, kept in missing-roms.json along tables.json to tell the user what to upload
   // from the web server. A ROM is a zip file ('rom'), searched in 'folder' (relative to the tables folder when inside), in which 'files' were
   // not found, for a table. It is removed from the list when a file with its name is added, and a missing file when a file with its name is added.
   struct MissingRom {
      std::string rom;
      std::string folder;
      std::vector<std::string> files;
      std::string table; // Name of the table that needed it
      int64_t reportedAt = 0; // Seconds since epoch
   };
   void AddMissingRom(const std::string& rom, const std::filesystem::path& folder, const std::vector<std::string>& files, const std::string& table);
   std::vector<MissingRom> GetMissingRoms() const;
   void ClearMissingRoms();
   void OnFileAdded(const std::string& fileName);

   // PinMAME ROM aliases (see RomAliases.h). The alias file shared by the tables, empty without a ROM folder (a table with its own
   // 'pinmame/roms' folder has its alias file in that 'pinmame' folder, which PinMAME then uses instead).
   std::filesystem::path GetAliasFilePath() const;
   std::string ReadAliasFile() const;
   // Save the content, normalized, as the alias file. Nothing is saved when there are errors (lines that libpinmame would misread).
   bool SaveAliasFile(const std::string& content, std::vector<RomAliases::Issue>& errors, std::vector<RomAliases::Issue>& warnings, std::string& error);
   // The tables asking for a game that PinMAME does not know, and that has no alias
   struct UnknownGame {
      std::string table;
      std::string rom;
   };
   std::vector<UnknownGame> GetUnknownGames();

   const std::filesystem::path& GetTablesPath() const { return m_config.tablesPath; }
   std::filesystem::path GetFullPath(const Table& table) const { return BuildPath(table.path); }
   std::filesystem::path GetImagePath(const Table& table) const { return table.image.empty() ? std::filesystem::path() : BuildPath(table.image); }
   std::filesystem::path GetIniPath(const Table& table) const { return BuildPath(table.path).replace_extension(".ini"); }
   std::filesystem::path GetScriptPath(const Table& table) const { return BuildPath(table.path).replace_extension(".vbs"); }

   static std::string SanitizeName(const std::string& name);
   // Name given to a table when it is added: its file name, without extension, underscores replaced by spaces
   static std::string GetDefaultName(const Table& table);

   // Fuzzy search: the characters of each word of the query must all be found in the text, in the same order, ignoring case. Words may be in any order.
   // Returns nothing if they are not, otherwise a score which is higher for matches that are consecutive, at the start of words, or early in the text.
   static std::optional<int> FuzzyScore(const std::string& query, const std::string& text);

private:
   void Log(LogLevel level, const std::string& message) const;
   std::filesystem::path BuildPath(const std::string& relativePath) const;
   std::string RelativePath(const std::filesystem::path& fullPath) const;
   bool IsInsideTables(const std::filesystem::path& fullPath) const;
   bool IsExcluded(const std::filesystem::path& fullPath) const;
   bool IsArchive(const std::filesystem::path& path) const;
   std::vector<std::filesystem::path> ListTableFiles(const std::filesystem::path& folder, int settleSeconds) const;
   std::string FindImage(const std::string& tablePath) const;
   std::string GenerateUUID(const std::vector<Table>& tables) const;
   std::string GetUniqueFolder(const std::string& baseName) const;
   Table CreateTable(const std::filesystem::path& fullPath, const std::vector<Table>& tables) const;

   void LoadJson();
   void SaveJson();
   void Commit(std::vector<Table> tables);
   void UpdatePupPacks(std::vector<Table>& tables) const;
   bool Update(const std::string& uuid, const std::function<bool(Table&)>& change, bool isContentChange = true);
   std::filesystem::path GetStatsPath() const;
   std::filesystem::path GetMissingRomsPath() const;
   std::vector<MissingRom> LoadMissingRoms() const;
   void SaveMissingRoms(const std::vector<MissingRom>& missingRoms) const;
   mutable std::mutex m_missingRomsMutex;
   static std::optional<int> FuzzyWordScore(const std::string& word, const std::string& text);
   std::vector<Table> ImportVPX(const std::filesystem::path& path, const std::vector<Table>& tables);
   std::vector<Table> ImportArchive(const std::filesystem::path& path, const std::vector<Table>& tables, const ProgressCallback& onProgress, int* importedPacks = nullptr,
      int* importedRoms = nullptr);
   int MovePupPacks(const std::filesystem::path& folder, const std::string& rootName) const;
   bool MovePupPack(const std::filesystem::path& pack, const std::string& name) const;
   int MoveStrayTablePacks(const std::filesystem::path& tableFolder) const;
   int MoveBundleRoms(const std::filesystem::path& folder, bool isTableFolder);
   bool MoveRom(const std::filesystem::path& rom, const std::filesystem::path& destFolder);
   void MoveRootRoms(const std::vector<Table>& tables);
   std::filesystem::path GetRejectedArchivesPath() const;
   void LoadRejectedArchives();
   void SaveRejectedArchives() const;
   void ApplyVrPupOptions(const std::filesystem::path& folder) const;
   std::filesystem::path GetTableAliasFilePath(const std::filesystem::path& tableFolder) const;
   std::filesystem::path FindTableRomsFolder(const std::filesystem::path& tableFolder) const;
   void EnsureAliasFile() const;
   void CollectAliasHints(const std::filesystem::path& bundleFolder);
   void ResolveGameAliases(const std::vector<Table>& tables);
   bool MoveTableRom(const std::filesystem::path& tableFolder, const std::string& game);

   const Config m_config;
   std::mutex m_operationMutex; // Serializes the operations modifying the library (they may be long: unzip, copy...)
   mutable std::mutex m_mutex; // Protects m_tables, never held during file operations
   std::vector<Table> m_tables;
   bool m_loaded = false;
   std::set<std::string> m_rejectedArchives; // Bundles without table, to avoid extracting them at each rescan (kept in rejected-archives.json)
   bool m_rejectedArchivesLoaded = false;
   RomAliases::GameCatalog m_gameCatalog;
   std::map<std::string, RomAliases::Evidence> m_aliasHints; // Alias pairs of the imported bundles, by lower case name (see CollectAliasHints)
   std::set<std::string> m_aliasChecks; // The tables already looked at by ResolveGameAliases (uuid, date, ROM and alias file revision)
   std::atomic<uint64_t> m_aliasFileRevision { 0 }; // Incremented when the alias file is saved from the web server
   mutable std::mutex m_aliasFileMutex;
   std::atomic<uint64_t> m_revision { 0 };

   std::mutex m_workerMutex; // Protects m_worker and m_rescanPending
   std::thread m_worker;
   std::atomic<bool> m_scanning { false };
   bool m_rescanPending = false;
};

}
