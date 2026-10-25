// license:GPLv3+

#pragma once

// Steam Frame Edition: backup of what the user keeps on the device (tables, ROMs, table images, library information, settings, scores, ROM
// aliases), downloaded as a zip from the Backup page of the web server (assets/web/backup.html), and restored from such a zip.
//
// The zip is never written on the device: it is produced while it is downloaded (see Archive and GetArchiveFileSystem), its entries stored
// without compression (tables and media are already compressed), so that its size is known beforehand and a broken download can be resumed.
// Each entry is '<item>/<root>/<path>': the root (tables, pinmame, pup, pref) is the folder of the device it comes from, so that a backup made on
// macOS restores to the folders of the Steam Frame. A restore is read by the browser, which uploads the entries the user selected (see
// WebServerBackup.cpp); the library information, the scores and the settings are merged into what the device has, the other files are copied.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct mg_fs;
struct mg_connection;

namespace Backup {

enum class Item : int
{
   Games, // The tables and the files along them (backglass, music, PinUP Player packs...)
   Thumbnails, // The images of the tables (<table>.png|.jpg)
   Roms, // PinMAME ROMs and the media of the games (altcolor, altsound...)
   Saves, // PinMAME nvram, cfg and hi, and the VPReg files of the scripts (the high scores of the games, and the settings of the machines)
   Library, // Favorites, display names and play counts (tables.json, table-stats.json)
   Settings, // VPinballX.ini, the settings of the tables (<table>.ini), the room of the lobby
   Scores, // Leaderboards and player profiles (scores.json, profiles.json)
   Aliases, // PinMAME alias files (alias.txt)
   Count
};
const char* GetItemId(Item item);
std::optional<Item> ParseItem(std::string_view id);

// The folders of the device that the entries come from and go to
struct Roots
{
   std::filesystem::path tables; // Table library
   std::filesystem::path pinmame; // PinMAME folder (roms, nvram...), may be outside of the tables folder
   std::filesystem::path pup; // Shared 'pupvideos' folder of the PinUP Player packs
   std::filesystem::path pref; // Settings folder (VPinballX.ini, tables.json, scores.json...)
   static Roots OfDevice();
   const std::filesystem::path* Find(std::string_view name) const;
};

// A table of the library, as the backup links files to it
struct TableInfo
{
   std::string uuid;
   std::string name;
   std::string path; // Relative to the tables folder
   std::string rom; // PinMAME game, empty if none
   std::string romAlias; // Game its ROM archive is named after when 'rom' is an alias, empty otherwise
   std::vector<std::string> pupPacks;
};

struct FileEntry
{
   std::filesystem::path path;
   std::string zipPath; // '<item>/<root>/<path relative to the root>'
   Item item = Item::Games;
   uint64_t size = 0;
   int64_t modifiedAt = 0; // Seconds since epoch
   std::vector<int> tables; // Indexes in Catalog::tables, empty for files that are not linked to a table
   bool global = false; // Not linked to tables (settings, scores...): part of the backup whenever its item is
};

// The files that can be backed up, and the links between files and tables (by exact path, or by folder)
struct Catalog
{
   std::vector<TableInfo> tables;
   std::vector<std::vector<std::string>> tablePaths; // For each table, root-relative paths ('tables/Addams/' for a folder, 'pinmame/roms/afm_113b.zip')
   std::vector<FileEntry> files;
   std::vector<std::string> skipped; // Files left out, with the reason (not UTF-8, symbolic links...)
};
Catalog Scan(const Roots& roots, const std::vector<TableInfo>& tables);

struct Selection
{
   bool items[static_cast<int>(Item::Count)] = {};
   bool allTables = true;
   std::vector<std::string> tables; // Uuids, when not allTables
   bool otherFiles = true; // Files linked to no table, when not allTables
};
std::vector<const FileEntry*> Select(const Catalog& catalog, const Selection& selection);
// backup.json, the first entry of the zip: what the backup holds, and the links between files and tables for the restore page
std::string BuildManifest(const Catalog& catalog, const std::vector<const FileEntry*>& files, const std::string& appVersion, int64_t createdAt);

// A zip of files, produced while it is read. Checksums are computed first (ComputeChecksums), as the headers of the entries hold them. The global files
// (settings, library, scores, alias file) are read when it is created, as the application may save them at any time.
class Archive final
{
public:
   Archive(const std::vector<const FileEntry*>& files, const std::string& manifest, int64_t createdAt);

   // Fails when a file can not be read or has changed. Cancelled when 'cancel' is set.
   bool ComputeChecksums(const std::atomic<bool>& cancel, std::string& error);
   uint64_t GetTotalBytes() const { return m_totalBytes; }
   uint64_t GetCheckedBytes() const { return m_checkedBytes.load(); }
   size_t GetFileCount() const { return m_files.size(); }

   uint64_t GetSize() const { return m_size; }
   int64_t GetCreatedAt() const { return m_createdAt; }
   uint64_t GetBytesSent() const { return m_bytesSent.load(); }
   std::string GetError() const;

   class Reader;

private:
   struct File
   {
      std::filesystem::path path;
      std::string name;
      uint64_t size;
      int64_t modifiedAt;
      uint32_t crc = 0;
      uint64_t headerOffset = 0;
      std::optional<std::string> content; // Read when the archive is created: the manifest, and the files the application rewrites (settings, scores...)
   };
   struct Segment
   {
      uint64_t offset;
      uint64_t size;
      int file; // Index in m_files of the data of a file, -1 for the bytes of 'data'
      std::string data;
   };
   void Layout();
   void SetError(const std::string& error);

   std::vector<File> m_files;
   std::vector<Segment> m_segments;
   uint64_t m_totalBytes = 0;
   uint64_t m_size = 0;
   const int64_t m_createdAt;
   std::atomic<uint64_t> m_checkedBytes { 0 };
   std::atomic<uint64_t> m_bytesSent { 0 };
   mutable std::mutex m_errorMutex;
   std::string m_error;
};

// The file system that mongoose's mg_http_serve_file reads the archives from (it handles the HTTP headers, ranges and the pace of the
// connection): '<id>.zip' is the archive registered with that id
struct mg_fs* GetArchiveFileSystem();
void RegisterArchive(const std::string& id, std::shared_ptr<Archive> archive);
void UnregisterArchive(const std::string& id);
// To be called once mg_http_serve_file has started to send an archive, so that a read error closes the connection instead of leaving
// the browser waiting for the rest
void AttachConnection(struct mg_connection* c);

// The destination on the device of a restored entry, nothing if the path is not acceptable (absolute, '..', staging folders...)
std::optional<std::filesystem::path> GetRestorePath(const Roots& roots, const std::string& zipPath, Item& item);
// Entries merged into what the device has instead of being copied (tables.json, scores.json, VPinballX.ini, the shared alias file)
bool IsMergedEntry(const std::string& zipPath);

// The settings restored from a backup are applied when the next table (or the lobby) is loaded, from the main thread (the settings of
// the application are not thread safe, and saved from memory): 'VPinballX.restore.ini' in the settings folder holds them meanwhile
std::filesystem::path GetPendingSettingsPath();
void SavePendingSettings(const std::filesystem::path& restoredIni, const std::string& sourceDevice);
void ApplyPendingSettings();
// The kind of device the backup is made on: settings of the display and of the rendering are only restored on the same kind
std::string GetDeviceClass();

// Merge the 'alias,game' lines of 'restored' that 'current' does not define
std::string MergeAliases(const std::string& current, const std::string& restored);

// Keeps the device from sleeping while it is held (the Steam Frame leaves the network when it sleeps, which would break a transfer)
class KeepAwake final
{
public:
   KeepAwake();
   ~KeepAwake();
   KeepAwake(const KeepAwake&) = delete;
   KeepAwake& operator=(const KeepAwake&) = delete;

private:
   int m_pid = -1;
};

}
