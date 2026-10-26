// license:GPLv3+

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "TableLibrary.h"

namespace VPinballLib {

// Fixes of the tables and ROMs that do not work as they are, described by recipes (docs/Table and ROM Fixes.md).
//
// The recipes ship with the application (assets/fixes/fixes.json); a fixes.json in the preferences folder adds recipes and turns shipped
// ones off. A recipe matches tables by their file name, or by the PinMAME ROM their script runs, and makes one of two kinds of change:
// - script patches: text replacements in the table script, written as the table's .vbs, which VPX loads instead of the embedded script.
//   The .vpx is never changed, and a .vbs the table came with is kept as <table>.vbs.orig, the base of the patches. A patch only applies
//   when its text is found the expected number of times, so that a new version of the table, already fixed, is left alone.
// - a ROM procedure: a sequence of switch presses, run by the PinMAME plugin once the emulation started, like a player in the service
//   menu of the game (see GetRomFix). It runs by itself when the ROM has no NVRAM yet, or at the next start once requested by Patch.
//
// What was applied is kept in table-fixes.json along tables.json. Tables added to the library are patched as they are found; the tables
// already there when a recipe is shipped are patched on request (Patch, from the web server or the menu of the table).
//
// It only depends on the standard library (the script reader and logging are injected) and is thread safe.
class TableFixes final
{
public:
   enum class LogLevel { Info, Warn, Error };
   using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

   struct ScriptPatch {
      std::string find;
      std::string replace;
      int count = 1; // Number of times 'find' must be found for the patch to apply (all of them are replaced)
   };
   struct RomSequence {
      std::vector<std::string> roms; // Lower case
      std::vector<int> switches; // libpinmame switch numbers, pressed one after the other
   };
   struct RomProcedure {
      std::vector<RomSequence> sequences;
      int bootMs = 25000; // Wait after the start of the emulation, before the first press
      int holdMs = 100; // Press duration
      int waitMs = 1200; // Wait after each release
      bool reset = true; // Reset the machine afterwards, so that the game starts again with what the procedure changed
      bool runWithoutNvram = true; // Run by itself when the ROM has no NVRAM yet
      std::string notice; // Shown to the player while it runs
   };
   struct Recipe {
      std::string id;
      int version = 1;
      std::string title;
      std::string symptom; // What the player sees without the fix
      std::string cause; // Why it happens
      std::string change; // What the fix changes
      std::string doc; // Section of docs/Table and ROM Fixes.md
      bool local = false; // From the fixes.json of the preferences folder
      bool disabled = false; // Turned off by the fixes.json of the preferences folder
      std::vector<std::string> files; // Lower case parts of table file names (any of them matches)
      std::vector<std::string> roms; // Lower case ROM names (any of them matches), from 'match' and the ROM sequences
      std::vector<ScriptPatch> script;
      std::optional<RomProcedure> rom;
   };

   enum class Status {
      Available, // Matches the table, not applied
      Applied, // Applied, with the current version of the recipe
      Outdated, // Applied with an older version of the recipe
      Pending, // ROM procedure requested, runs at the next start of the table
      NotApplicable // Matches the table by name or ROM, but its script does not have the text to patch (already fixed by its author?)
   };
   static const char* GetStatusName(Status status);

   struct TableFix {
      std::string id;
      Status status = Status::Available;
      std::string detail;
      int appliedVersion = 0;
      int64_t appliedAt = 0;
   };

   struct Config {
      std::filesystem::path shippedRecipesPath; // assets/fixes/fixes.json
      std::filesystem::path localRecipesPath; // fixes.json of the preferences folder (optional)
      std::filesystem::path statePath; // table-fixes.json
      std::filesystem::path tablesPath; // Tables folder of the library, the paths of the tables are relative to it
      std::function<std::string(const std::filesystem::path& vpxPath)> readEmbeddedScript; // Script inside a .vpx file
      LogCallback log;
   };

   explicit TableFixes(Config config);

   // Read the recipes again (done once on first use)
   void Reload();
   std::vector<Recipe> GetRecipes();

   // The recipes matching a table, with their status, and whether one of them can be applied or applied again
   std::vector<TableFix> GetTableFixes(const Table& table);
   struct Summary {
      int available = 0; // Can be applied (Available or Outdated)
      int applied = 0; // Applied or Pending
   };
   Summary GetSummary(const Table& table);

   // Apply the recipes matching the table, or apply them again: rebuild its .vbs from the base script with the script patches, and request
   // the ROM procedures for its next start. Returns false when nothing could be applied, with what happened in 'messages'.
   bool Patch(const Table& table, std::vector<std::string>& messages);

   // The tables after a scan of the library: the ones added, or whose .vpx changed, are patched with the recipes matching them. On the first
   // scan ever (no table-fixes.json yet), the tables already there are only recorded, they are patched on request.
   void OnTablesScanned(const std::vector<Table>& tables);

   // The ROM procedure to run for 'rom', started by 'tablePath', as a JSON object for the PinMAME plugin (empty: none): requested by Patch,
   // or because the ROM has no NVRAM yet. {"id", "version", "bootMs", "holdMs", "waitMs", "reset", "notice", "switches": [...]}
   std::string GetRomFix(const std::string& rom, bool hasNvram, const std::filesystem::path& tablePath);
   void ReportRomFix(const std::string& rom, const std::string& id, int version, bool completed);

   const std::filesystem::path& GetLocalRecipesPath() const { return m_config.localRecipesPath; }

   // Text replacement of a script patch. Line ends of 'find' and 'replace' are '\n', and match the '\r\n' of the script when it uses them.
   // Returns the number of times 'find' was found (the script is only changed when it is 'patch.count').
   static int ApplyScriptPatch(std::string& script, const ScriptPatch& patch);

private:
   struct AppliedFix {
      int version = 0;
      int64_t appliedAt = 0;
   };
   struct TableState {
      int64_t checkedVpxTime = 0; // Modification date of the .vpx file when it was last looked at by OnTablesScanned
      int64_t patchedVpxTime = 0; // Modification date of the .vpx file the .vbs was built for
      std::string vbsHash; // Hash of the .vbs written by the script patches, to recognize it
      std::map<std::string, AppliedFix> fixes;
   };
   struct RomState {
      std::string pending; // Recipe requested by Patch
      std::map<std::string, AppliedFix> fixes;
   };
   struct ScriptCheck { // What the script patches of a recipe would do on a table, cached by table and modification dates of its files
      int64_t signature = 0;
      bool applicable = false;
      std::string detail;
   };

   void Log(LogLevel level, const std::string& message) const;
   void EnsureLoaded();
   void LoadRecipes();
   void LoadState();
   void SaveState() const;
   std::filesystem::path GetFullPath(const Table& table) const;
   int64_t GetVpxTime(const Table& table) const;
   bool MatchesFile(const Recipe& recipe, const Table& table) const;
   static bool MatchesRom(const Recipe& recipe, const std::string& rom);
   bool Matches(const Recipe& recipe, const Table& table) const;
   std::string ReadBaseScript(const Table& table, const TableState& state, bool& hasOwnVbs) const;
   ScriptCheck CheckScript(const Recipe& recipe, const Table& table);
   bool ApplyScriptRecipes(const Table& table, std::vector<std::string>& messages);
   void RemoveOwnVbs(const Table& table, TableState& state);

   const Config m_config;
   std::recursive_mutex m_mutex;
   bool m_loaded = false;
   bool m_libraryRecorded = false; // The tables of the library were recorded by a first scan (see OnTablesScanned)
   std::vector<Recipe> m_recipes;
   std::map<std::string, TableState> m_tables; // By uuid
   std::map<std::string, RomState> m_roms; // By lower case ROM name
   std::map<std::string, ScriptCheck> m_scriptChecks; // By uuid + '/' + recipe id + '/' + version
};

}
