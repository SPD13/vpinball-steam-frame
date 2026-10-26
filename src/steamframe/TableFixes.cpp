// license:GPLv3+

#include "TableFixes.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using std::string;
using namespace std::string_literals;

namespace VPinballLib {

namespace {

string ToLower(string s)
{
   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return s;
}

int64_t Now()
{
   return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool ReadFile(const fs::path& path, string& content)
{
   std::ifstream file(path, std::ios::binary);
   if (!file)
      return false;
   content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
   return !file.bad();
}

bool WriteFile(const fs::path& path, const string& content)
{
   fs::path tempPath = path;
   tempPath += ".tmp";
   {
      std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
      file.write(content.data(), static_cast<std::streamsize>(content.size()));
      if (!file)
         return false;
   }
   std::error_code ec;
   fs::rename(tempPath, path, ec);
   return !ec;
}

// FNV-1a, to recognize the .vbs files written by the script patches
string Hash(const string& content)
{
   uint64_t hash = 14695981039346656037ull;
   for (const unsigned char c : content) {
      hash ^= c;
      hash *= 1099511628211ull;
   }
   std::ostringstream text;
   text << std::hex << hash;
   return text.str();
}

int64_t FileTime(const fs::path& path)
{
   std::error_code ec;
   const auto time = fs::last_write_time(path, ec);
   return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

// '\n' not preceded by '\r' become '\r\n'
string ToCrLf(const string& text)
{
   string result;
   result.reserve(text.size() + text.size() / 16);
   for (size_t i = 0; i < text.size(); i++) {
      if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r'))
         result += '\r';
      result += text[i];
   }
   return result;
}

int CountOccurrences(const string& text, const string& find)
{
   if (find.empty())
      return 0;
   int count = 0;
   for (size_t pos = text.find(find); pos != string::npos; pos = text.find(find, pos + find.size()))
      count++;
   return count;
}

fs::path OrigPath(const fs::path& vbsPath)
{
   fs::path path = vbsPath;
   path += ".orig";
   return path;
}

std::vector<string> LowerList(const nlohmann::json& json, const char* key)
{
   std::vector<string> list;
   if (const auto it = json.find(key); it != json.end() && it->is_array())
      for (const auto& item : *it)
         if (item.is_string() && !item.get<string>().empty())
            list.push_back(ToLower(item.get<string>()));
   return list;
}

}

const char* TableFixes::GetStatusName(Status status)
{
   switch (status) {
   case Status::Available: return "available";
   case Status::Applied: return "applied";
   case Status::Outdated: return "outdated";
   case Status::Pending: return "pending";
   case Status::NotApplicable: return "notApplicable";
   }
   return "";
}

TableFixes::TableFixes(Config config)
   : m_config(std::move(config))
{
}

void TableFixes::Log(LogLevel level, const string& message) const
{
   if (m_config.log)
      m_config.log(level, message);
}

void TableFixes::EnsureLoaded()
{
   if (m_loaded)
      return;
   m_loaded = true;
   LoadRecipes();
   LoadState();
}

void TableFixes::Reload()
{
   std::lock_guard lock(m_mutex);
   m_loaded = false;
   m_scriptChecks.clear();
   EnsureLoaded();
}

std::vector<TableFixes::Recipe> TableFixes::GetRecipes()
{
   std::lock_guard lock(m_mutex);
   EnsureLoaded();
   return m_recipes;
}

void TableFixes::LoadRecipes()
{
   m_recipes.clear();
   std::set<string> disabled;
   const auto load = [&](const fs::path& path, bool local) {
      if (path.empty())
         return;
      std::ifstream file(path);
      if (!file) {
         if (!local)
            Log(LogLevel::Warn, "No recipes found at " + path.string());
         return;
      }
      nlohmann::json json;
      try {
         json = nlohmann::json::parse(file);
      } catch (const std::exception& e) {
         Log(LogLevel::Error, "Failed to parse " + path.string() + ": " + e.what());
         return;
      }
      if (local)
         for (const string& id : LowerList(json, "disable"))
            disabled.insert(id);
      const auto recipes = json.find("recipes");
      if (recipes == json.end() || !recipes->is_array())
         return;
      for (const auto& entry : *recipes) {
         try {
            Recipe recipe;
            recipe.id = ToLower(entry.at("id").get<string>());
            recipe.version = entry.value("version", 1);
            recipe.title = entry.value("title", recipe.id);
            recipe.symptom = entry.value("symptom", ""s);
            recipe.cause = entry.value("cause", ""s);
            recipe.change = entry.value("change", ""s);
            recipe.doc = entry.value("doc", ""s);
            recipe.local = local;
            if (const auto match = entry.find("match"); match != entry.end()) {
               recipe.files = LowerList(*match, "files");
               recipe.roms = LowerList(*match, "roms");
            }
            if (const auto script = entry.find("script"); script != entry.end())
               for (const auto& patch : *script)
                  recipe.script.push_back({ patch.at("find").get<string>(), patch.value("replace", ""s), patch.value("count", 1) });
            if (const auto rom = entry.find("rom"); rom != entry.end()) {
               RomProcedure procedure;
               procedure.bootMs = rom->value("bootMs", procedure.bootMs);
               procedure.holdMs = rom->value("holdMs", procedure.holdMs);
               procedure.waitMs = rom->value("waitMs", procedure.waitMs);
               procedure.reset = rom->value("reset", procedure.reset);
               procedure.runWithoutNvram = rom->value("runWithoutNvram", procedure.runWithoutNvram);
               procedure.notice = rom->value("notice", ""s);
               for (const auto& sequence : rom->at("sequences")) {
                  RomSequence romSequence { LowerList(sequence, "roms"), sequence.at("switches").get<std::vector<int>>() };
                  for (const string& name : romSequence.roms)
                     if (std::find(recipe.roms.begin(), recipe.roms.end(), name) == recipe.roms.end())
                        recipe.roms.push_back(name);
                  procedure.sequences.push_back(std::move(romSequence));
               }
               recipe.rom = std::move(procedure);
            }
            if (recipe.id.empty() || (recipe.script.empty() && !recipe.rom)) {
               Log(LogLevel::Warn, "Recipe '" + recipe.id + "' of " + path.string() + " has nothing to apply: ignored");
               continue;
            }
            if (recipe.files.empty() && recipe.roms.empty()) {
               Log(LogLevel::Warn, "Recipe '" + recipe.id + "' of " + path.string() + " matches no table: ignored");
               continue;
            }
            // A local recipe replaces the shipped one with the same id
            std::erase_if(m_recipes, [&recipe](const Recipe& other) { return other.id == recipe.id; });
            m_recipes.push_back(std::move(recipe));
         } catch (const std::exception& e) {
            Log(LogLevel::Error, "Invalid recipe in " + path.string() + ": " + e.what());
         }
      }
   };
   load(m_config.shippedRecipesPath, false);
   load(m_config.localRecipesPath, true);
   for (Recipe& recipe : m_recipes)
      recipe.disabled = disabled.contains(recipe.id);
}

void TableFixes::LoadState()
{
   m_tables.clear();
   m_roms.clear();
   m_libraryRecorded = false;
   std::ifstream file(m_config.statePath);
   if (!file)
      return;
   try {
      const nlohmann::json json = nlohmann::json::parse(file);
      m_libraryRecorded = json.value("libraryRecorded", false);
      const auto readFixes = [](const nlohmann::json& entry) {
         std::map<string, AppliedFix> fixes;
         if (const auto it = entry.find("fixes"); it != entry.end())
            for (const auto& [id, fix] : it->items())
               fixes[id] = { fix.value("version", 0), fix.value("appliedAt", int64_t(0)) };
         return fixes;
      };
      if (const auto tables = json.find("tables"); tables != json.end())
         for (const auto& [uuid, entry] : tables->items())
            m_tables[uuid] = { entry.value("checkedVpxTime", int64_t(0)), entry.value("patchedVpxTime", int64_t(0)), entry.value("vbsHash", ""s), readFixes(entry) };
      if (const auto roms = json.find("roms"); roms != json.end())
         for (const auto& [rom, entry] : roms->items())
            m_roms[rom] = { entry.value("pending", ""s), readFixes(entry) };
   } catch (const std::exception& e) {
      Log(LogLevel::Error, "Failed to parse " + m_config.statePath.string() + ": " + e.what());
   }
}

void TableFixes::SaveState() const
{
   const auto writeFixes = [](const std::map<string, AppliedFix>& fixes) {
      nlohmann::ordered_json json = nlohmann::ordered_json::object();
      for (const auto& [id, fix] : fixes)
         json[id] = { { "version", fix.version }, { "appliedAt", fix.appliedAt } };
      return json;
   };
   nlohmann::ordered_json tables = nlohmann::ordered_json::object();
   for (const auto& [uuid, state] : m_tables) {
      nlohmann::ordered_json entry = { { "checkedVpxTime", state.checkedVpxTime } };
      if (!state.vbsHash.empty()) {
         entry["patchedVpxTime"] = state.patchedVpxTime;
         entry["vbsHash"] = state.vbsHash;
      }
      if (!state.fixes.empty())
         entry["fixes"] = writeFixes(state.fixes);
      tables[uuid] = entry;
   }
   nlohmann::ordered_json roms = nlohmann::ordered_json::object();
   for (const auto& [rom, state] : m_roms) {
      nlohmann::ordered_json entry = nlohmann::ordered_json::object();
      if (!state.pending.empty())
         entry["pending"] = state.pending;
      if (!state.fixes.empty())
         entry["fixes"] = writeFixes(state.fixes);
      roms[rom] = entry;
   }
   const nlohmann::ordered_json json = { { "libraryRecorded", m_libraryRecorded }, { "tables", tables }, { "roms", roms } };
   std::error_code ec;
   fs::create_directories(m_config.statePath.parent_path(), ec);
   if (!WriteFile(m_config.statePath, json.dump(2) + '\n'))
      Log(LogLevel::Error, "Failed to save " + m_config.statePath.string());
}

fs::path TableFixes::GetFullPath(const Table& table) const
{
   return m_config.tablesPath / fs::path(reinterpret_cast<const char8_t*>(table.path.c_str()));
}

// The modification date of the .vpx file itself (the one of the library also changes with the name and the image of the table)
int64_t TableFixes::GetVpxTime(const Table& table) const
{
   return FileTime(GetFullPath(table));
}

bool TableFixes::MatchesFile(const Recipe& recipe, const Table& table) const
{
   const string fileName = ToLower(table.path.substr(table.path.find_last_of('/') + 1));
   return std::any_of(recipe.files.begin(), recipe.files.end(), [&fileName](const string& part) { return fileName.find(part) != string::npos; });
}

bool TableFixes::MatchesRom(const Recipe& recipe, const string& rom)
{
   return !rom.empty() && std::find(recipe.roms.begin(), recipe.roms.end(), ToLower(rom)) != recipe.roms.end();
}

bool TableFixes::Matches(const Recipe& recipe, const Table& table) const
{
   if (recipe.disabled)
      return false;
   // Both given: both must match (a table of a ROM), otherwise the one given
   const bool fileMatch = recipe.files.empty() || MatchesFile(recipe, table);
   const bool romMatch = recipe.roms.empty() || MatchesRom(recipe, table.rom);
   return fileMatch && romMatch;
}

int TableFixes::ApplyScriptPatch(string& script, const ScriptPatch& patch)
{
   string find = patch.find;
   string replace = patch.replace;
   int count = CountOccurrences(script, find);
   if (count == 0 && find.find('\n') != string::npos && script.find("\r\n") != string::npos) {
      find = ToCrLf(find);
      replace = ToCrLf(replace);
      count = CountOccurrences(script, find);
   }
   if (count != patch.count)
      return count;
   string result;
   result.reserve(script.size());
   size_t start = 0;
   for (size_t pos = script.find(find); pos != string::npos; pos = script.find(find, start)) {
      result.append(script, start, pos - start);
      result += replace;
      start = pos + find.size();
   }
   result.append(script, start, string::npos);
   script = std::move(result);
   return count;
}

// The script the patches apply to: the .vbs the table came with (kept as .vbs.orig once patched), otherwise the one inside the .vpx.
// 'hasOwnVbs': the table has a .vbs which is not ours, to keep as .vbs.orig before writing the patched one.
string TableFixes::ReadBaseScript(const Table& table, const TableState& state, bool& hasOwnVbs) const
{
   hasOwnVbs = false;
   const fs::path tablePath = GetFullPath(table);
   const fs::path vbsPath = fs::path(tablePath).replace_extension(".vbs");
   string script;
   std::error_code ec;
   if (fs::is_regular_file(OrigPath(vbsPath), ec) && ReadFile(OrigPath(vbsPath), script))
      return script;
   if (fs::is_regular_file(vbsPath, ec) && ReadFile(vbsPath, script) && (state.vbsHash.empty() || Hash(script) != state.vbsHash)) {
      hasOwnVbs = true;
      return script;
   }
   return m_config.readEmbeddedScript ? m_config.readEmbeddedScript(tablePath) : string();
}

TableFixes::ScriptCheck TableFixes::CheckScript(const Recipe& recipe, const Table& table)
{
   const fs::path vbsPath = GetFullPath(table).replace_extension(".vbs");
   const int64_t signature = GetVpxTime(table) ^ FileTime(vbsPath) ^ (FileTime(OrigPath(vbsPath)) << 1);
   const string key = table.uuid + '/' + recipe.id + '/' + std::to_string(recipe.version);
   if (const auto it = m_scriptChecks.find(key); it != m_scriptChecks.end() && it->second.signature == signature)
      return it->second;

   ScriptCheck check { signature, true, ""s };
   bool hasOwnVbs;
   string script = ReadBaseScript(table, m_tables[table.uuid], hasOwnVbs);
   if (script.empty()) {
      check.applicable = false;
      check.detail = "The script of the table could not be read";
   }
   for (const ScriptPatch& patch : recipe.script) {
      if (!check.applicable)
         break;
      if (const int count = ApplyScriptPatch(script, patch); count != patch.count) {
         check.applicable = false;
         check.detail = "The text to patch was found " + std::to_string(count) + " time(s) in the script instead of " + std::to_string(patch.count)
            + ": this version of the table does not need the fix, or needs another one";
      }
   }
   m_scriptChecks[key] = check;
   return check;
}

void TableFixes::RemoveOwnVbs(const Table& table, TableState& state)
{
   const fs::path vbsPath = GetFullPath(table).replace_extension(".vbs");
   std::error_code ec;
   string content;
   if (!state.vbsHash.empty() && ReadFile(vbsPath, content) && Hash(content) == state.vbsHash) {
      fs::remove(vbsPath, ec);
      if (fs::is_regular_file(OrigPath(vbsPath), ec))
         fs::rename(OrigPath(vbsPath), vbsPath, ec);
      Log(LogLevel::Info, "Removed the patched script of " + table.path);
   }
   state.vbsHash.clear();
   state.patchedVpxTime = 0;
   std::erase_if(state.fixes, [this](const auto& fix) {
      const auto recipe = std::find_if(m_recipes.begin(), m_recipes.end(), [&fix](const Recipe& r) { return r.id == fix.first; });
      return recipe == m_recipes.end() || !recipe->script.empty();
   });
}

bool TableFixes::ApplyScriptRecipes(const Table& table, std::vector<string>& messages)
{
   std::vector<const Recipe*> recipes;
   for (const Recipe& recipe : m_recipes)
      if (!recipe.script.empty() && Matches(recipe, table))
         recipes.push_back(&recipe);
   if (recipes.empty())
      return false;

   TableState& state = m_tables[table.uuid];
   bool hasOwnVbs;
   const string base = ReadBaseScript(table, state, hasOwnVbs);
   if (base.empty()) {
      messages.push_back("The script of " + table.name + " could not be read");
      return false;
   }
   string script = base;
   std::vector<const Recipe*> applied;
   for (const Recipe* recipe : recipes) {
      string patched = script;
      bool ok = true;
      for (const ScriptPatch& patch : recipe->script)
         if (const int count = ApplyScriptPatch(patched, patch); count != patch.count) {
            messages.push_back("'" + recipe->title + "' does not apply: its text was found " + std::to_string(count) + " time(s) in the script instead of " + std::to_string(patch.count));
            ok = false;
            break;
         }
      if (ok) {
         script = std::move(patched);
         applied.push_back(recipe);
      }
   }

   if (applied.empty()) {
      RemoveOwnVbs(table, state);
      return false;
   }

   const fs::path vbsPath = GetFullPath(table).replace_extension(".vbs");
   std::error_code ec;
   if (hasOwnVbs) {
      fs::rename(vbsPath, OrigPath(vbsPath), ec);
      if (ec) {
         messages.push_back("Failed to keep the script of " + table.name + " as " + OrigPath(vbsPath).filename().string() + ": " + ec.message());
         return false;
      }
   }
   if (!WriteFile(vbsPath, script)) {
      messages.push_back("Failed to write " + vbsPath.string());
      return false;
   }
   state.vbsHash = Hash(script);
   state.patchedVpxTime = GetVpxTime(table);
   std::erase_if(state.fixes, [&recipes](const auto& fix) { return std::any_of(recipes.begin(), recipes.end(), [&fix](const Recipe* r) { return r->id == fix.first; }); });
   const int64_t now = Now();
   for (const Recipe* recipe : applied) {
      state.fixes[recipe->id] = { recipe->version, now };
      messages.push_back("'" + recipe->title + "' applied: the script of the table is patched");
      Log(LogLevel::Info, "Applied '" + recipe->id + "' (version " + std::to_string(recipe->version) + ") to " + table.path + ", script written to " + vbsPath.filename().string());
   }
   return true;
}

std::vector<TableFixes::TableFix> TableFixes::GetTableFixes(const Table& table)
{
   std::lock_guard lock(m_mutex);
   EnsureLoaded();
   std::vector<TableFix> fixes;
   const auto stateIt = m_tables.find(table.uuid);
   for (const Recipe& recipe : m_recipes) {
      if (!Matches(recipe, table))
         continue;
      TableFix fix;
      fix.id = recipe.id;
      if (!recipe.script.empty()) {
         const AppliedFix* applied = nullptr;
         if (stateIt != m_tables.end()) {
            const TableState& state = stateIt->second;
            string content;
            const auto it = state.fixes.find(recipe.id);
            // Still applied: our .vbs is there, built from the current .vpx
            if (it != state.fixes.end() && state.patchedVpxTime == GetVpxTime(table) && ReadFile(GetFullPath(table).replace_extension(".vbs"), content) && Hash(content) == state.vbsHash)
               applied = &it->second;
         }
         if (applied) {
            fix.status = applied->version == recipe.version ? Status::Applied : Status::Outdated;
            fix.appliedVersion = applied->version;
            fix.appliedAt = applied->appliedAt;
            if (fix.status == Status::Outdated)
               fix.detail = "A newer version of the fix is available";
         } else if (const ScriptCheck check = CheckScript(recipe, table); check.applicable)
            fix.status = Status::Available;
         else {
            fix.status = Status::NotApplicable;
            fix.detail = check.detail;
         }
      } else {
         const auto romIt = m_roms.find(ToLower(table.rom));
         const auto applied = romIt == m_roms.end() ? nullptr : [&]() -> const AppliedFix* { const auto it = romIt->second.fixes.find(recipe.id); return it == romIt->second.fixes.end() ? nullptr : &it->second; }();
         if (romIt != m_roms.end() && romIt->second.pending == recipe.id) {
            fix.status = Status::Pending;
            fix.detail = "Runs at the next start of the table";
         } else if (applied) {
            fix.status = applied->version == recipe.version ? Status::Applied : Status::Outdated;
            if (fix.status == Status::Outdated)
               fix.detail = "A newer version of the fix is available";
         } else {
            fix.status = Status::Available;
            if (recipe.rom->runWithoutNvram)
               fix.detail = "Runs by itself at the first start of the table if its game has no saved NVRAM yet";
         }
         if (applied) {
            fix.appliedVersion = applied->version;
            fix.appliedAt = applied->appliedAt;
         }
      }
      fixes.push_back(std::move(fix));
   }
   return fixes;
}

TableFixes::Summary TableFixes::GetSummary(const Table& table)
{
   Summary summary;
   for (const TableFix& fix : GetTableFixes(table)) {
      if (fix.status == Status::Available || fix.status == Status::Outdated)
         summary.available++;
      else if (fix.status == Status::Applied || fix.status == Status::Pending)
         summary.applied++;
   }
   return summary;
}

bool TableFixes::Patch(const Table& table, std::vector<string>& messages)
{
   std::lock_guard lock(m_mutex);
   EnsureLoaded();
   TableState& state = m_tables[table.uuid];
   const int64_t vpxTime = GetVpxTime(table);
   if (!state.vbsHash.empty() && state.patchedVpxTime != vpxTime)
      RemoveOwnVbs(table, state); // Built for the previous version of the .vpx
   bool patched = ApplyScriptRecipes(table, messages);
   for (const Recipe& recipe : m_recipes) {
      if (!recipe.rom || !recipe.script.empty() || !Matches(recipe, table))
         continue;
      m_roms[ToLower(table.rom)].pending = recipe.id;
      messages.push_back("'" + recipe.title + "' runs at the next start of the table");
      Log(LogLevel::Info, "Requested '" + recipe.id + "' for the ROM '" + table.rom + "' of " + table.path);
      patched = true;
   }
   if (messages.empty())
      messages.push_back("No fix applies to this table");
   state.checkedVpxTime = vpxTime;
   SaveState();
   return patched;
}

void TableFixes::OnTablesScanned(const std::vector<Table>& tables)
{
   std::lock_guard lock(m_mutex);
   EnsureLoaded();
   bool changed = false;
   if (!m_libraryRecorded) {
      // First scan with the fixes: the tables already in the library are patched on request only
      for (const Table& table : tables)
         m_tables[table.uuid].checkedVpxTime = GetVpxTime(table);
      m_libraryRecorded = true;
      SaveState();
      return;
   }
   std::set<string> uuids;
   for (const Table& table : tables) {
      uuids.insert(table.uuid);
      TableState& state = m_tables[table.uuid];
      const int64_t vpxTime = GetVpxTime(table);
      if (state.checkedVpxTime == vpxTime)
         continue;
      // Added, or its .vpx changed: a .vbs written for the previous version would replace the new script
      if (!state.vbsHash.empty() && state.patchedVpxTime != vpxTime)
         RemoveOwnVbs(table, state);
      std::vector<string> messages;
      ApplyScriptRecipes(table, messages);
      for (const string& message : messages)
         Log(LogLevel::Info, table.path + ": " + message);
      state.checkedVpxTime = vpxTime;
      changed = true;
   }
   // Tables removed from the library
   changed |= std::erase_if(m_tables, [&uuids](const auto& entry) { return !uuids.contains(entry.first); }) > 0;
   if (changed)
      SaveState();
}

string TableFixes::GetRomFix(const string& rom, bool hasNvram, const fs::path& tablePath)
{
   std::lock_guard lock(m_mutex);
   m_romFixRunning = false; // Set again below when a procedure runs for this start of the ROM
   EnsureLoaded();
   const string romName = ToLower(rom);
   const string fileName = ToLower(tablePath.filename().string());
   for (const Recipe& recipe : m_recipes) {
      if (recipe.disabled || !recipe.rom || !recipe.script.empty() || !MatchesRom(recipe, romName))
         continue;
      if (!recipe.files.empty() && !std::any_of(recipe.files.begin(), recipe.files.end(), [&fileName](const string& part) { return fileName.find(part) != string::npos; }))
         continue;
      const auto romIt = m_roms.find(romName);
      const bool requested = romIt != m_roms.end() && romIt->second.pending == recipe.id;
      if (!requested && !(recipe.rom->runWithoutNvram && !hasNvram))
         continue;
      const RomProcedure& procedure = *recipe.rom;
      const auto sequence = std::find_if(procedure.sequences.begin(), procedure.sequences.end(),
         [&romName](const RomSequence& s) { return std::find(s.roms.begin(), s.roms.end(), romName) != s.roms.end(); });
      if (sequence == procedure.sequences.end())
         continue;
      Log(LogLevel::Info, "Running '" + recipe.id + "' for the ROM '" + rom + "' (" + (requested ? "requested" : "no NVRAM yet") + ')');
      m_romFixRunning = true;
      return nlohmann::json {
         { "id", recipe.id }, { "version", recipe.version }, { "title", recipe.title }, { "bootMs", procedure.bootMs }, { "holdMs", procedure.holdMs },
         { "waitMs", procedure.waitMs }, { "reset", procedure.reset }, { "notice", procedure.notice }, { "switches", sequence->switches } }.dump();
   }
   return string();
}

void TableFixes::ReportRomFix(const string& rom, const string& id, int version, bool completed)
{
   std::lock_guard lock(m_mutex);
   m_romFixRunning = false;
   EnsureLoaded();
   if (!completed) {
      Log(LogLevel::Warn, "'" + id + "' was interrupted for the ROM '" + rom + "': it runs again at the next start");
      m_roms[ToLower(rom)].pending = id;
      SaveState();
      return;
   }
   RomState& state = m_roms[ToLower(rom)];
   state.fixes[id] = { version, Now() };
   if (state.pending == id)
      state.pending.clear();
   Log(LogLevel::Info, "'" + id + "' (version " + std::to_string(version) + ") was run for the ROM '" + rom + '\'');
   SaveState();
}

}
