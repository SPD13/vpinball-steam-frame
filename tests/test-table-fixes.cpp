// license:GPLv3+

// TableFixes only depends on the standard library, so these tests do not need the VPX application. The tables are text files standing for
// .vpx files: their content is their embedded script. The last test checks the recipes shipped in src/assets/fixes/fixes.json against their
// record, docs/Table and ROM Fixes.md (run from the root of the repository, or set VPX_SOURCE_DIR).
// Build alone: clang++ -std=c++20 -DDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN -Ithird-party/include tests/test-table-fixes.cpp src/steamframe/TableFixes.cpp

#ifndef _WIN32

#include "doctest.h"

#include "../src/steamframe/TableFixes.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace VPinballLib;
using std::string;

namespace {

string Read(const fs::path& path)
{
   std::ifstream file(path, std::ios::binary);
   return string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void Write(const fs::path& path, const string& content)
{
   fs::create_directories(path.parent_path());
   std::ofstream file(path, std::ios::binary | std::ios::trunc);
   file << content;
}

// A later modification date than the one the file has (the file system may keep seconds only)
void Touch(const fs::path& path)
{
   fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(5));
}

const string BTTF_SCRIPT = "Sub bttf_Init\r\n    vpmInit Me\r\n\tBackdrop_Init\r\n\r\n\tvpmInit me\r\n\r\n' ball through system\r\nEnd Sub\r\n";
const string BTTF_FIXED_SCRIPT = "Sub bttf_Init\r\n    vpmInit Me\r\n\tBackdrop_Init\r\nEnd Sub\r\n";

const char* RECIPES = R"({
  "recipes": [
    { "id": "bttf", "version": 1, "title": "BTTF", "match": { "files": ["back to the future"] },
      "script": [ { "find": "\tBackdrop_Init\n\n\tvpmInit me\n", "replace": "\tBackdrop_Init\n\n\t' vpmInit me\n" } ] },
    { "id": "gts3", "version": 1, "title": "GTS3",
      "rom": { "bootMs": 1000, "notice": "Patching", "sequences": [ { "roms": ["sfight2"], "switches": [-8, 4] }, { "roms": ["smb"], "switches": [143] } ] } }
  ]
})";

struct Fixture {
   fs::path root;
   fs::path tables;
   std::vector<string> logs;

   Fixture(const string& recipes = RECIPES)
   {
      string pattern = (fs::temp_directory_path() / "vpx-table-fixes-XXXXXX").string();
      root = mkdtemp(pattern.data());
      tables = root / "tables";
      fs::create_directories(tables);
      Write(root / "assets" / "fixes.json", recipes);
   }
   ~Fixture()
   {
      std::error_code ec;
      fs::remove_all(root, ec);
   }

   TableFixes::Config GetConfig()
   {
      TableFixes::Config config;
      config.shippedRecipesPath = root / "assets" / "fixes.json";
      config.localRecipesPath = root / "prefs" / "fixes.json";
      config.statePath = root / "prefs" / "table-fixes.json";
      config.tablesPath = tables;
      config.readEmbeddedScript = [](const fs::path& path) { return Read(path); };
      config.log = [this](TableFixes::LogLevel, const string& message) { logs.push_back(message); };
      return config;
   }

   Table AddTable(const string& uuid, const string& path, const string& script, const string& rom = "")
   {
      Write(tables / path, script);
      Table table;
      table.uuid = uuid;
      table.name = fs::path(path).stem().string();
      table.path = path;
      table.rom = rom;
      return table;
   }
};

}

TEST_CASE("TableFixes: script patches match the line ends of the script")
{
   string script = BTTF_SCRIPT;
   const TableFixes::ScriptPatch patch { "\tBackdrop_Init\n\n\tvpmInit me\n", "\tBackdrop_Init\n" };
   CHECK(TableFixes::ApplyScriptPatch(script, patch) == 1);
   CHECK(script == "Sub bttf_Init\r\n    vpmInit Me\r\n\tBackdrop_Init\r\n\r\n' ball through system\r\nEnd Sub\r\n");

   // Found twice when once is expected: nothing is changed
   string twice = BTTF_SCRIPT + BTTF_SCRIPT;
   CHECK(TableFixes::ApplyScriptPatch(twice, patch) == 2);
   CHECK(twice == BTTF_SCRIPT + BTTF_SCRIPT);
}

TEST_CASE("TableFixes: the tables already in the library are patched on request only")
{
   Fixture fixture;
   const Table table = fixture.AddTable("1", "BTTF/VR ROOM Back to the Future V103.vpx", BTTF_SCRIPT);
   const fs::path vbs = fixture.tables / "BTTF/VR ROOM Back to the Future V103.vbs";
   TableFixes fixes(fixture.GetConfig());

   fixes.OnTablesScanned({ table });
   CHECK_FALSE(fs::exists(vbs));
   auto status = fixes.GetTableFixes(table);
   REQUIRE(status.size() == 1);
   CHECK(status[0].status == TableFixes::Status::Available);
   CHECK(fixes.GetSummary(table).available == 1);

   std::vector<string> messages;
   CHECK(fixes.Patch(table, messages));
   REQUIRE(fs::exists(vbs));
   CHECK(Read(vbs).find("\t' vpmInit me\r\n") != string::npos);
   CHECK(Read(fixture.tables / table.path) == BTTF_SCRIPT); // The .vpx is not changed
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Applied);
   CHECK(fixes.GetSummary(table).applied == 1);

   // Patched again: same result
   CHECK(fixes.Patch(table, messages));
   CHECK(Read(vbs).find("\t' vpmInit me\r\n") != string::npos);

   // The state is kept
   TableFixes reloaded(fixture.GetConfig());
   CHECK(reloaded.GetTableFixes(table)[0].status == TableFixes::Status::Applied);
}

TEST_CASE("TableFixes: tables added to the library are patched, a .vbs they came with is kept")
{
   Fixture fixture;
   TableFixes fixes(fixture.GetConfig());
   fixes.OnTablesScanned({}); // First scan, empty library

   const Table added = fixture.AddTable("1", "Back to the Future.vpx", "embedded script, not used");
   Write(fixture.tables / "Back to the Future.vbs", BTTF_SCRIPT); // The .vbs the table came with
   const Table other = fixture.AddTable("2", "Other table.vpx", BTTF_SCRIPT);
   fixes.OnTablesScanned({ added, other });

   CHECK(Read(fixture.tables / "Back to the Future.vbs.orig") == BTTF_SCRIPT);
   CHECK(Read(fixture.tables / "Back to the Future.vbs").find("\t' vpmInit me\r\n") != string::npos);
   CHECK(fixes.GetTableFixes(added)[0].status == TableFixes::Status::Applied);
   CHECK_FALSE(fs::exists(fixture.tables / "Other table.vbs"));
   CHECK(fixes.GetTableFixes(other).empty());

   // A new version of the table, already fixed by its author: its .vbs replaced, the fix no longer applies and the patched script is removed
   Write(fixture.tables / "Back to the Future.vpx", BTTF_FIXED_SCRIPT);
   Touch(fixture.tables / "Back to the Future.vpx");
   fs::remove(fixture.tables / "Back to the Future.vbs.orig");
   fs::remove(fixture.tables / "Back to the Future.vbs");
   fixes.OnTablesScanned({ added, other });
   CHECK_FALSE(fs::exists(fixture.tables / "Back to the Future.vbs"));
   const auto status = fixes.GetTableFixes(added);
   REQUIRE(status.size() == 1);
   CHECK(status[0].status == TableFixes::Status::NotApplicable);
   CHECK(fixes.GetSummary(added).available == 0);
}

TEST_CASE("TableFixes: a new .vpx of a patched table restores the .vbs it came with before patching again")
{
   Fixture fixture;
   TableFixes fixes(fixture.GetConfig());
   fixes.OnTablesScanned({});
   const Table table = fixture.AddTable("1", "Back to the Future.vpx", BTTF_SCRIPT);
   fixes.OnTablesScanned({ table });
   const fs::path vbs = fixture.tables / "Back to the Future.vbs";
   REQUIRE(fs::exists(vbs));

   // The .vpx changes, still with the bug: patched again from the new script
   Write(fixture.tables / table.path, BTTF_SCRIPT + "' new version\r\n");
   Touch(fixture.tables / table.path);
   fixes.OnTablesScanned({ table });
   CHECK(Read(vbs).find("' new version") != string::npos);
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Applied);
}

TEST_CASE("TableFixes: a newer version of a recipe is offered to the patched tables")
{
   Fixture fixture;
   const Table table = fixture.AddTable("1", "Back to the Future.vpx", BTTF_SCRIPT);
   {
      TableFixes fixes(fixture.GetConfig());
      fixes.OnTablesScanned({ table });
      std::vector<string> messages;
      fixes.Patch(table, messages);
   }
   string recipes = RECIPES;
   recipes.replace(recipes.find("\"version\": 1"), 12, "\"version\": 2");
   Write(fixture.root / "assets" / "fixes.json", recipes);
   TableFixes fixes(fixture.GetConfig());
   const auto status = fixes.GetTableFixes(table);
   CHECK(status[0].status == TableFixes::Status::Outdated);
   CHECK(status[0].appliedVersion == 1);
   CHECK(fixes.GetSummary(table).available == 1);
   std::vector<string> messages;
   fixes.Patch(table, messages);
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Applied);
}

TEST_CASE("TableFixes: ROM procedures run without NVRAM, or when requested, until they complete")
{
   Fixture fixture;
   const Table table = fixture.AddTable("1", "Street Fighter II.vpx", "script", "sfight2");
   TableFixes fixes(fixture.GetConfig());
   fixes.OnTablesScanned({ table });
   const fs::path tablePath = fixture.tables / table.path;

   const string fix = fixes.GetRomFix("SFIGHT2", false, tablePath);
   REQUIRE_FALSE(fix.empty());
   const nlohmann::json json = nlohmann::json::parse(fix);
   CHECK(json["id"] == "gts3");
   CHECK(json["bootMs"] == 1000);
   CHECK(json["switches"] == nlohmann::json::array({ -8, 4 }));
   CHECK(nlohmann::json::parse(fixes.GetRomFix("smb", false, tablePath))["switches"] == nlohmann::json::array({ 143 }));
   CHECK(fixes.GetRomFix("sfight2", true, tablePath).empty()); // It has an NVRAM
   CHECK(fixes.GetRomFix("afm_113b", false, tablePath).empty());

   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Available);
   std::vector<string> messages;
   CHECK(fixes.Patch(table, messages));
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Pending);
   CHECK_FALSE(fixes.GetRomFix("sfight2", true, tablePath).empty()); // Requested

   fixes.ReportRomFix("sfight2", "gts3", 1, false); // Interrupted: still requested
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Pending);
   fixes.ReportRomFix("sfight2", "gts3", 1, true);
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Applied);
   CHECK(fixes.GetRomFix("sfight2", true, tablePath).empty());
}

TEST_CASE("TableFixes: a ROM procedure run before the first scan does not make the tables of the library new")
{
   Fixture fixture;
   const Table table = fixture.AddTable("1", "Back to the Future.vpx", BTTF_SCRIPT);
   {
      TableFixes fixes(fixture.GetConfig());
      fixes.ReportRomFix("sfight2", "gts3", 1, true); // Saves the state, before any scan (a table played with -play)
   }
   TableFixes fixes(fixture.GetConfig());
   fixes.OnTablesScanned({ table });
   CHECK_FALSE(fs::exists(fixture.tables / "Back to the Future.vbs"));
   CHECK(fixes.GetTableFixes(table)[0].status == TableFixes::Status::Available);
}

TEST_CASE("TableFixes: local recipes add to and turn off the shipped ones")
{
   Fixture fixture;
   Write(fixture.root / "prefs" / "fixes.json", R"({ "disable": ["gts3"], "recipes": [ { "id": "mine", "title": "Mine", "match": { "files": ["other"] },
      "script": [ { "find": "a", "replace": "b" } ] } ] })");
   const Table rom = fixture.AddTable("1", "Street Fighter II.vpx", "script", "sfight2");
   const Table other = fixture.AddTable("2", "Other.vpx", "a");
   TableFixes fixes(fixture.GetConfig());
   CHECK(fixes.GetTableFixes(rom).empty());
   CHECK(fixes.GetRomFix("sfight2", false, fixture.tables / rom.path).empty());
   const auto status = fixes.GetTableFixes(other);
   REQUIRE(status.size() == 1);
   CHECK(status[0].id == "mine");
   bool hasLocal = false;
   for (const TableFixes::Recipe& recipe : fixes.GetRecipes())
      hasLocal |= recipe.id == "mine" && recipe.local;
   CHECK(hasLocal);
}

TEST_CASE("TableFixes: the shipped recipes are valid and recorded in their document")
{
   const char* sourceDir = getenv("VPX_SOURCE_DIR");
   const fs::path root = sourceDir ? fs::path(sourceDir) : fs::current_path();
   const fs::path recipesPath = root / "src" / "assets" / "fixes" / "fixes.json";
   const fs::path docPath = root / "docs" / "Table and ROM Fixes.md";
   REQUIRE_MESSAGE(fs::exists(recipesPath), "Run from the root of the repository, or set VPX_SOURCE_DIR");
   std::vector<string> logs;
   TableFixes::Config config;
   config.shippedRecipesPath = recipesPath;
   config.log = [&logs](TableFixes::LogLevel, const string& message) { logs.push_back(message); };
   TableFixes fixes(config);
   const std::vector<TableFixes::Recipe> recipes = fixes.GetRecipes();
   CHECK(logs.empty());
   CHECK(recipes.size() == nlohmann::json::parse(std::ifstream(recipesPath))["recipes"].size());
   const string doc = Read(docPath);
   for (const TableFixes::Recipe& recipe : recipes) {
      CAPTURE(recipe.id);
      CHECK(!recipe.title.empty());
      CHECK(!recipe.symptom.empty());
      CHECK(!recipe.cause.empty());
      CHECK(!recipe.change.empty());
      CHECK(recipe.doc == recipe.id);
      CHECK_MESSAGE(doc.find("<a id=\"" + recipe.id + "\"></a>") != string::npos, "No section for the recipe in docs/Table and ROM Fixes.md");
   }
}

#endif
