// license:GPLv3+

// TableLibrary only depends on the standard library, so these tests do not need the VPX application. They rely on
// the zip/unzip command line tools to build and extract bundles, and are therefore limited to POSIX platforms.

#ifndef _WIN32

#include "doctest.h"

#include "../src/steamframe/TableLibrary.h"
#include "../src/steamframe/PupPackOptions.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using namespace VPinballLib;
using std::string;

namespace {

struct Fixture {
   fs::path root;
   fs::path tables;
   fs::path json;
   int unzipCount = 0;
   std::vector<string> logs;

   Fixture()
   {
      string pattern = (fs::temp_directory_path() / "vpx-table-library-XXXXXX").string();
      root = mkdtemp(pattern.data());
      tables = root / "tables";
      json = root / "prefs" / "tables.json";
      fs::create_directories(tables);
   }

   ~Fixture() { fs::remove_all(root); }

   TableLibrary::Config Config()
   {
      TableLibrary::Config config;
      config.tablesPath = tables;
      config.jsonPath = json;
      config.zip = [](const fs::path& source, const fs::path& dest, TableLibrary::ZipProgressCallback) {
         return std::system(("cd '" + source.string() + "' && zip -qr '" + dest.string() + "' .").c_str()) == 0;
      };
      config.unzip = [this](const fs::path& source, const fs::path& dest, TableLibrary::ZipProgressCallback) {
         unzipCount++;
         return std::system(("unzip -qq '" + source.string() + "' -d '" + dest.string() + "'").c_str()) == 0;
      };
      config.log = [this](TableLibrary::LogLevel, const string& message) { logs.push_back(message); };
      return config;
   }

   static void Touch(const fs::path& path, const string& content = "x")
   {
      fs::create_directories(path.parent_path());
      std::ofstream(path) << content;
   }

   // Build a bundle from the content of 'folder', then remove the folder
   static void MakeZip(const fs::path& folder, const fs::path& zipPath)
   {
      REQUIRE(std::system(("cd '" + folder.string() + "' && zip -qr '" + zipPath.string() + "' .").c_str()) == 0);
      fs::remove_all(folder);
   }

   static string Read(const fs::path& path)
   {
      std::stringstream buffer;
      buffer << std::ifstream(path).rdbuf();
      return buffer.str();
   }
};

string lowerName(string name)
{
   std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return name;
}

std::vector<string> Paths(const TableLibrary& library)
{
   std::vector<string> paths;
   for (const Table& table : library.GetTables())
      paths.push_back(table.path);
   std::sort(paths.begin(), paths.end());
   return paths;
}

}

TEST_CASE("TableLibrary: folder names are sanitized like the Android and iOS launchers")
{
   CHECK(TableLibrary::SanitizeName("Attack from Mars (Bally 1995)") == "Attack-from-Mars-Bally-1995");
   CHECK(TableLibrary::SanitizeName("__a..b__") == "a-b");
   CHECK(TableLibrary::SanitizeName("Médiéval") == "Médiéval");
   CHECK(TableLibrary::SanitizeName("()") == "table");
}

TEST_CASE("TableLibrary: rescan registers tables and writes a compatible tables.json")
{
   Fixture f;
   Fixture::Touch(f.tables / "AFM" / "Attack_from_Mars.vpx");
   Fixture::Touch(f.tables / "AFM" / "Attack_from_Mars.png");
   Fixture::Touch(f.tables / "AFM" / "pinmame" / "roms" / "afm_113b.zip");
   Fixture::Touch(f.tables / "loose.VPX");
   Fixture::Touch(f.tables / "AFM" / "._Attack_from_Mars.vpx"); // macOS resource fork
   Fixture::Touch(f.tables / ".hidden" / "ghost.vpx");
   Fixture::Touch(f.tables / "excluded" / "nope.vpx");
   Fixture::Touch(f.tables / "download.vpx.part");

   TableLibrary::Config config = f.Config();
   config.excludedPaths.push_back(f.tables / "excluded");
   TableLibrary library(config);
   std::vector<int> progress;
   library.Rescan([&](int value, const string&) { progress.push_back(value); });

   CHECK(Paths(library) == std::vector<string> { "AFM/Attack_from_Mars.vpx", "loose.VPX" });
   CHECK(progress.back() == 100);
   CHECK(std::is_sorted(progress.begin(), progress.end()));
   CHECK(library.GetRevision() == 1);
   CHECK(f.unzipCount == 0); // ROM zips must stay zipped
   CHECK(fs::exists(f.tables / "AFM" / "pinmame" / "roms" / "afm_113b.zip"));

   const Table table = library.GetTables().front();
   CHECK(table.name == "Attack from Mars");
   CHECK(table.image == "AFM/Attack_from_Mars.png");
   CHECK(table.uuid.size() == 36);
   CHECK(table.uuid[14] == '4');
   CHECK(table.createdAt > 0);
   CHECK(library.GetFullPath(table) == f.tables / "AFM" / "Attack_from_Mars.vpx");
   CHECK(library.GetIniPath(table) == f.tables / "AFM" / "Attack_from_Mars.ini");

   // Same keys, in the same order, as the Kotlin and Swift implementations
   const auto json = nlohmann::ordered_json::parse(Fixture::Read(f.json));
   CHECK(json.at("tableCount") == 2);
   std::vector<string> keys;
   for (const auto& item : json.at("tables").at(0).items())
      keys.push_back(item.key());
   CHECK(keys == std::vector<string> { "uuid", "name", "path", "image", "createdAt", "modifiedAt" });
   CHECK(json.begin().key() == "tableCount");
   CHECK_FALSE(fs::exists(f.json.string() + ".tmp"));

   // Nothing changed: no new revision
   library.Rescan();
   CHECK(library.GetRevision() == 1);
}

TEST_CASE("TableLibrary: the database survives a restart and follows the folder")
{
   Fixture f;
   Fixture::Touch(f.tables / "A" / "a.vpx");
   Fixture::Touch(f.tables / "B" / "b.vpx");
   string uuidA, uuidB;
   {
      TableLibrary library(f.Config());
      library.Rescan();
      uuidA = library.GetTables()[0].uuid;
      uuidB = library.GetTables()[1].uuid;
      CHECK(library.Rename(uuidA, "Zebra"));
      CHECK_FALSE(library.Rename(uuidA, "  "));
      CHECK_FALSE(library.Rename("unknown", "x"));
   }

   fs::remove_all(f.tables / "B");
   Fixture::Touch(f.tables / "C" / "c.vpx");
   Fixture::Touch(f.tables / "A" / "a.jpg");

   TableLibrary library(f.Config());
   CHECK(library.GetTables().empty()); // Nothing is loaded before the first rescan
   library.Rescan();
   REQUIRE(library.GetTables().size() == 2);
   CHECK(library.GetTables()[0].name == "c");
   CHECK(library.GetTables()[1].name == "Zebra");
   CHECK(library.GetTables()[1].uuid == uuidA);
   CHECK(library.GetTables()[1].image == "A/a.jpg");
   CHECK(library.GetTables(false)[0].name == "Zebra");
   CHECK_FALSE(library.GetTable(uuidB).has_value());
}

TEST_CASE("TableLibrary: tables.json written by other launchers or edited by hand is sanitized")
{
   Fixture f;
   Fixture::Touch(f.tables / "A" / "a.vpx");
   Fixture::Touch(f.root / "outside.vpx");
   const string absolutePath = (f.tables / "A" / "a.vpx").string();
   Fixture::Touch(f.json,
      R"({"tableCount":5,"tables":[
         {"uuid":"u1","name":"Kept","path":")" + absolutePath + R"(","image":"A/missing.png","createdAt":5,"modifiedAt":6,"futureField":true},
         {"uuid":"u1","name":"Duplicate uuid","path":"A/a.vpx","image":"","createdAt":1,"modifiedAt":1},
         {"uuid":"u2","name":"Duplicate path","path":"A/a.vpx","image":"","createdAt":1,"modifiedAt":1},
         {"uuid":"u3","name":"Escape","path":"../outside.vpx","image":"","createdAt":1,"modifiedAt":1},
         {"uuid":"","name":"No uuid","path":"A/a.vpx"}]})");

   TableLibrary library(f.Config());
   library.Rescan();
   REQUIRE(library.GetTables().size() == 1);
   const Table table = library.GetTables()[0];
   CHECK(table.uuid == "u1");
   CHECK(table.name == "Kept");
   CHECK(table.path == "A/a.vpx");
   CHECK(table.image.empty());
   CHECK(table.createdAt == 5);
   CHECK(fs::exists(f.root / "outside.vpx"));

   SUBCASE("a corrupted database is rebuilt from the folder")
   {
      Fixture::Touch(f.json, "{ not json");
      TableLibrary rebuilt(f.Config());
      rebuilt.Rescan();
      CHECK(Paths(rebuilt) == std::vector<string> { "A/a.vpx" });
      CHECK(nlohmann::json::parse(Fixture::Read(f.json)).at("tableCount") == 1);
   }
}

TEST_CASE("TableLibrary: files still being copied are left for the next rescan")
{
   Fixture f;
   Fixture::Touch(f.tables / "A" / "a.vpx");
   Fixture::Touch(f.tables / "old" / "old.vpx");
   fs::last_write_time(f.tables / "old" / "old.vpx", fs::file_time_type::clock::now() - std::chrono::hours(1));

   TableLibrary library(f.Config());
   library.Rescan(nullptr, 30);
   CHECK(Paths(library) == std::vector<string> { "old/old.vpx" });
   library.Rescan();
   CHECK(Paths(library) == std::vector<string> { "A/a.vpx", "old/old.vpx" });
}

TEST_CASE("TableLibrary: background rescans never miss a request")
{
   Fixture f;
   {
      TableLibrary library(f.Config());
      for (int i = 0; i < 20; i++) {
         Fixture::Touch(f.tables / ("T" + std::to_string(i)) / "t.vpx");
         library.RescanAsync();
      }
      while (library.IsScanning())
         usleep(1000);
      CHECK(library.GetTables().size() == 20);

      Fixture::Touch(f.tables / "Last" / "last.vpx");
      library.RescanAsync(); // The destructor waits for the worker
   }
   CHECK(nlohmann::json::parse(Fixture::Read(f.json)).at("tableCount") == 21);
}

TEST_CASE("TableLibrary: importing a table copies it in its own folder")
{
   Fixture f;
   Fixture::Touch(f.root / "downloads" / "Medieval_Madness (1997).vpx", "table");
   Fixture::Touch(f.root / "downloads" / "readme.txt");

   TableLibrary library(f.Config());
   library.Rescan();
   const std::vector<Table> first = library.Import(f.root / "downloads" / "Medieval_Madness (1997).vpx");
   const std::vector<Table> second = library.Import(f.root / "downloads" / "Medieval_Madness (1997).vpx");
   REQUIRE(first.size() == 1);
   REQUIRE(second.size() == 1);
   CHECK(first[0].name == "Medieval Madness (1997)");
   CHECK(first[0].path == "Medieval-Madness-1997/Medieval_Madness (1997).vpx");
   CHECK(second[0].path == "Medieval-Madness-1997-2/Medieval_Madness (1997).vpx");
   CHECK(first[0].uuid != second[0].uuid);
   CHECK(Fixture::Read(library.GetFullPath(first[0])) == "table");
   CHECK(fs::exists(f.root / "downloads" / "Medieval_Madness (1997).vpx"));
   CHECK(library.GetTables().size() == 2);

   CHECK(library.Import(f.root / "downloads" / "readme.txt").empty());
   CHECK(library.Import(f.root / "downloads" / "missing.vpx").empty());
   CHECK(library.GetTables().size() == 2);

   // The rescan must not register the imported files a second time
   library.Rescan();
   CHECK(library.GetTables().size() == 2);
}

TEST_CASE("TableLibrary: bundles dropped in the tables folder are imported by the rescan")
{
   Fixture f;
   const fs::path staging = f.root / "staging";
   Fixture::Touch(staging / "Pack" / "Tron.vpx", "tron");
   Fixture::Touch(staging / "Pack" / "Tron.directb2s");
   Fixture::Touch(staging / "Pack" / "Tron.jpg");
   Fixture::Touch(staging / "Pack" / "pinmame" / "roms" / "trn_174h.zip", "rom");
   Fixture::Touch(staging / "Pack" / "Extra" / "Tron_Mod.vpx");
   Fixture::Touch(staging / "Other" / "Fish_Tales.vpx");
   Fixture::Touch(staging / "__MACOSX" / "Pack" / "._Tron.vpx");
   Fixture::MakeZip(staging, f.tables / "bundle.vpxz");

   Fixture::Touch(staging / "readme.txt");
   Fixture::MakeZip(staging, f.tables / "roms-only.zip");
   Fixture::Touch(f.tables / "broken.zip", "this is not a zip");

   TableLibrary library(f.Config());
   library.Rescan();

   CHECK(Paths(library) == std::vector<string> { "Fish-Tales/Fish_Tales.vpx", "Tron/Extra/Tron_Mod.vpx", "Tron/Tron.vpx" });
   CHECK(Fixture::Read(f.tables / "Tron" / "pinmame" / "roms" / "trn_174h.zip") == "rom");
   CHECK(fs::exists(f.tables / "Tron" / "Tron.directb2s"));
   for (const Table& table : library.GetTables())
      CHECK(table.image == (table.path == "Tron/Tron.vpx" ? "Tron/Tron.jpg" : ""));

   CHECK_FALSE(fs::exists(f.tables / "bundle.vpxz")); // Imported bundles are consumed
   CHECK(fs::exists(f.tables / "roms-only.zip")); // Anything else is left untouched...
   CHECK(fs::exists(f.tables / "broken.zip"));
   CHECK_FALSE(fs::exists(f.tables / ".import"));
   CHECK(f.unzipCount == 3);

   library.Rescan();
   CHECK(f.unzipCount == 3); // ...and not extracted again
   CHECK(library.GetTables().size() == 3);

   SUBCASE("a bundle located elsewhere is imported without being removed")
   {
      Fixture::Touch(staging / "Tron.vpx");
      Fixture::MakeZip(staging, f.root / "external.zip");
      const std::vector<Table> imported = library.Import(f.root / "external.zip");
      REQUIRE(imported.size() == 1);
      CHECK(imported[0].path == "Tron-2/Tron.vpx");
      CHECK(fs::exists(f.root / "external.zip"));
      CHECK(library.GetTables().size() == 4);
   }
}

TEST_CASE("TableLibrary: deleting a table never removes more than the table")
{
   Fixture f;
   Fixture::Touch(f.tables / "Solo" / "solo.vpx");
   Fixture::Touch(f.tables / "Solo" / "music" / "track.ogg");
   Fixture::Touch(f.tables / "Shared" / "one.vpx");
   Fixture::Touch(f.tables / "Shared" / "one.ini");
   Fixture::Touch(f.tables / "Shared" / "one.directb2s");
   Fixture::Touch(f.tables / "Shared" / "two.vpx");
   Fixture::Touch(f.tables / "Shared" / "two.ini");
   Fixture::Touch(f.tables / "loose.vpx");
   Fixture::Touch(f.tables / "loose.png");
   Fixture::Touch(f.tables / "keep.txt");

   TableLibrary library(f.Config());
   library.Rescan();
   const auto uuidOf = [&](const string& path) {
      for (const Table& table : library.GetTables())
         if (table.path == path)
            return table.uuid;
      return string();
   };

   CHECK(library.Delete(uuidOf("Solo/solo.vpx")));
   CHECK_FALSE(fs::exists(f.tables / "Solo"));

   CHECK(library.Delete(uuidOf("Shared/one.vpx")));
   CHECK_FALSE(fs::exists(f.tables / "Shared" / "one.vpx"));
   CHECK_FALSE(fs::exists(f.tables / "Shared" / "one.ini"));
   CHECK_FALSE(fs::exists(f.tables / "Shared" / "one.directb2s"));
   CHECK(fs::exists(f.tables / "Shared" / "two.vpx"));
   CHECK(fs::exists(f.tables / "Shared" / "two.ini"));

   CHECK(library.Delete(uuidOf("loose.vpx")));
   CHECK_FALSE(fs::exists(f.tables / "loose.vpx"));
   CHECK_FALSE(fs::exists(f.tables / "loose.png"));
   CHECK(fs::exists(f.tables / "keep.txt"));
   CHECK(fs::exists(f.tables / "Shared" / "two.vpx"));

   CHECK_FALSE(library.Delete("unknown"));
   CHECK(Paths(library) == std::vector<string> { "Shared/two.vpx" });
   CHECK(nlohmann::json::parse(Fixture::Read(f.json)).at("tableCount") == 1);
}

TEST_CASE("TableLibrary: play statistics and favorites are kept apart from tables.json")
{
   Fixture f;
   Fixture::Touch(f.tables / "A" / "a.vpx");
   Fixture::Touch(f.tables / "B" / "b.vpx");
   Fixture::Touch(f.root / "outside.vpx");
   string uuidA, uuidB;
   {
      TableLibrary library(f.Config());
      library.Rescan();
      uuidA = library.GetTables()[0].uuid;
      uuidB = library.GetTables()[1].uuid;
      const int64_t modifiedAt = library.GetTable(uuidA)->modifiedAt;
      const uint64_t revision = library.GetRevision();

      CHECK(library.RecordPlay(f.tables / "A" / "a.vpx"));
      CHECK(library.RecordPlay(f.tables / "A" / "a.vpx"));
      CHECK_FALSE(library.RecordPlay(f.root / "outside.vpx"));
      CHECK_FALSE(library.RecordPlay(f.tables / "A" / "unknown.vpx"));
      CHECK(library.SetFavorite(uuidB, true));
      CHECK_FALSE(library.SetFavorite(uuidB, true)); // Already a favorite
      CHECK_FALSE(library.SetFavorite("unknown", true));

      CHECK(library.GetTable(uuidA)->playCount == 2);
      CHECK(library.GetTable(uuidA)->lastPlayedAt > 0);
      CHECK(library.GetTable(uuidA)->modifiedAt == modifiedAt); // Playing is not a change of the table
      CHECK(library.GetTable(uuidB)->favorite);
      CHECK(library.GetRevision() == revision + 3);
   }

   // tables.json keeps the exact format of the mobile launchers, which reject unknown fields
   const auto json = nlohmann::json::parse(Fixture::Read(f.json));
   CHECK(json.at("tables").at(0).size() == 6);
   CHECK_FALSE(json.at("tables").at(0).contains("playCount"));
   const auto stats = nlohmann::json::parse(Fixture::Read(f.json.parent_path() / "table-stats.json"));
   CHECK(stats.at(uuidA).at("playCount") == 2);
   CHECK(stats.at(uuidB).at("favorite") == true);

   // Survives a restart, even when a table is played before the first rescan
   TableLibrary library(f.Config());
   CHECK(library.RecordPlay(f.tables / "A" / "a.vpx"));
   library.Rescan();
   CHECK(library.GetTable(uuidA)->playCount == 3);
   CHECK(library.GetTable(uuidB)->favorite);
   CHECK(library.SetFavorite(uuidB, false));
   CHECK_FALSE(nlohmann::json::parse(Fixture::Read(f.json.parent_path() / "table-stats.json")).contains(uuidB));

   SUBCASE("a corrupted statistics file does not prevent from loading the tables")
   {
      Fixture::Touch(f.json.parent_path() / "table-stats.json", "{ broken");
      TableLibrary other(f.Config());
      other.Rescan();
      CHECK(other.GetTables().size() == 2);
      CHECK(other.GetTable(uuidA)->playCount == 0);
   }
}

TEST_CASE("TableLibrary: fuzzy search")
{
   const auto score = [](const char* query, const char* text) { return TableLibrary::FuzzyScore(query, text); };
   CHECK(score("", "Attack from Mars").has_value());
   CHECK(score("afm", "Attack from Mars").has_value());
   CHECK(score("AFM", "attack from mars").has_value());
   CHECK(score("mars att", "Attack from Mars").has_value()); // Words of the query can be in any order...
   CHECK(score("  test   mo ", "Monster Test").has_value());
   CHECK_FALSE(score("sram", "Attack from Mars").has_value()); // ...but the letters of a word can not
   CHECK(score("atk mrs", "Attack from Mars").has_value());
   CHECK_FALSE(score("atk xyz", "Attack from Mars").has_value()); // Every word must be found
   CHECK_FALSE(score("afmx", "Attack from Mars").has_value());
   CHECK_FALSE(score("z", "Attack from Mars").has_value());

   // Better matches get a higher score: consecutive characters, start of words, early in the text
   CHECK(*score("mars", "Attack from Mars") > *score("mars", "Medieval Madness Remastered"));
   CHECK(*score("afm", "Attack from Mars") > *score("afm", "Staff of Magic"));
   CHECK(*score("tron", "Tron Legacy") > *score("tron", "Metron"));
}

TEST_CASE("TableLibrary: image, ini and export")
{
   Fixture f;
   Fixture::Touch(f.tables / "Alpha" / "a.vpx");
   Fixture::Touch(f.tables / "Alpha" / "a.ini");
   Fixture::Touch(f.tables / "loose.vpx");
   Fixture::Touch(f.root / "shot.png", "png");
   Fixture::Touch(f.root / "photo.jpeg", "jpeg");

   TableLibrary library(f.Config());
   library.Rescan();
   const string uuid = library.GetTables()[0].uuid;
   const string looseUuid = library.GetTables()[1].uuid;

   CHECK(library.SetImage(uuid, (f.root / "shot.png").string()));
   CHECK(library.GetTable(uuid)->image == "Alpha/a.png");
   CHECK(library.SetImage(uuid, (f.root / "photo.jpeg").string()));
   CHECK(library.GetTable(uuid)->image == "Alpha/a.jpg");
   CHECK(Fixture::Read(library.GetImagePath(*library.GetTable(uuid))) == "jpeg");
   CHECK_FALSE(fs::exists(f.tables / "Alpha" / "a.png"));
   CHECK_FALSE(library.SetImage(uuid, (f.root / "missing.png").string()));
   CHECK_FALSE(library.SetImage(uuid, "../shot.png"));
   CHECK(library.GetTable(uuid)->image == "Alpha/a.jpg");
   CHECK(library.SetImage(uuid, ""));
   CHECK(library.GetTable(uuid)->image.empty());
   CHECK_FALSE(fs::exists(f.tables / "Alpha" / "a.jpg"));

   // Screenshot captured by the player when leaving the table
   CHECK_FALSE(library.ReloadImage(uuid));
   Fixture::Touch(f.tables / "Alpha" / "a.png");
   CHECK(library.ReloadImage(uuid));
   CHECK(library.GetTable(uuid)->image == "Alpha/a.png");

   CHECK(library.ResetIni(uuid));
   CHECK_FALSE(fs::exists(f.tables / "Alpha" / "a.ini"));
   CHECK_FALSE(library.ResetIni(uuid));

   CHECK(library.Rename(uuid, "My Table!"));
   const std::optional<fs::path> exported = library.Export(uuid, f.root / "export");
   REQUIRE(exported.has_value());
   CHECK(*exported == f.root / "export" / "My-Table!.vpxz");
   CHECK(fs::file_size(*exported) > 0);
   CHECK_FALSE(library.Export(looseUuid, f.root / "export").has_value()); // Would zip the whole tables folder

   // An export can be imported back
   const std::vector<Table> imported = library.Import(*exported);
   REQUIRE(imported.size() == 1);
   CHECK(imported[0].path == "a/a.vpx");
   CHECK(imported[0].image == "a/a.png");
}

TEST_CASE("TableLibrary: PinUP Player packs are imported to the shared pack folder")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   TableLibrary library(config);
   fs::create_directories(shared);

   // Pack folder at the root of the archive, PUPVideos folder of a Windows install, and the content of a pack without its folder
   Fixture::Touch(f.root / "a" / "afm_113b" / "screens.pup");
   Fixture::Touch(f.root / "a" / "afm_113b" / "FullDMD" / "dmd.mp4");
   Fixture::MakeZip(f.root / "a", shared / "afm.zip");
   Fixture::Touch(f.root / "b" / "PUPVideos" / "rom2" / "playlists.pup");
   Fixture::MakeZip(f.root / "b", shared / "PinUPSystem.zip");
   Fixture::Touch(f.root / "c" / "triggers.pup");
   Fixture::Touch(f.root / "c" / "Backglass" / "bg.mp4");
   Fixture::MakeZip(f.root / "c", shared / "jaws.zip");
   // A pack already there, in another case, is replaced
   Fixture::Touch(shared / "AFM_113B" / "old.txt");

   library.Rescan();
   CHECK(fs::exists(shared / "afm_113b" / "screens.pup"));
   CHECK(fs::exists(shared / "afm_113b" / "FullDMD" / "dmd.mp4"));
   CHECK_FALSE(fs::exists(shared / "AFM_113B" / "old.txt"));
   CHECK(fs::exists(shared / "rom2" / "playlists.pup"));
   CHECK(fs::exists(shared / "jaws" / "triggers.pup"));
   CHECK(fs::exists(shared / "jaws" / "Backglass" / "bg.mp4"));
   CHECK_FALSE(fs::exists(shared / "afm.zip"));
   CHECK_FALSE(fs::exists(shared / "PinUPSystem.zip"));
   CHECK_FALSE(fs::exists(shared / "jaws.zip"));
   CHECK(library.GetTables().empty()); // The packs are not scanned for tables
}

TEST_CASE("TableLibrary: a bundle keeps the pack of its table, and a bundle with only a pack is imported as a pack")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   TableLibrary library(config);

   // The table and its pack side by side in the bundle: the pack goes to the shared folder instead of being deleted with the bundle
   Fixture::Touch(f.root / "a" / "Jaws" / "Jaws.vpx");
   Fixture::Touch(f.root / "a" / "PUPVideos" / "jaws" / "screens.pup");
   Fixture::MakeZip(f.root / "a", f.tables / "jaws-with-pack.zip");
   // A pack inside the table folder stays with the table
   Fixture::Touch(f.root / "b" / "Bleach" / "Bleach.vpx");
   Fixture::Touch(f.root / "b" / "Bleach" / "pupvideos" / "bleach" / "screens.pup");
   Fixture::MakeZip(f.root / "b", f.tables / "bleach.zip");
   // Only a pack, dropped with the tables
   Fixture::Touch(f.root / "c" / "motu" / "triggers.pup");
   Fixture::MakeZip(f.root / "c", f.tables / "motu-pup-pack.zip");

   library.Rescan();
   CHECK(Paths(library) == std::vector<string> { "Bleach/Bleach.vpx", "Jaws/Jaws.vpx" });
   CHECK(fs::exists(shared / "jaws" / "screens.pup"));
   CHECK(fs::exists(f.tables / "Bleach" / "pupvideos" / "bleach" / "screens.pup"));
   CHECK(fs::exists(shared / "motu" / "triggers.pup"));
   CHECK_FALSE(fs::exists(f.tables / "motu-pup-pack.zip"));
   CHECK_FALSE(fs::exists(f.tables / "jaws-with-pack.zip"));
}

TEST_CASE("TableLibrary: tables know whether their PinUP Player pack is installed")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   int reads = 0;
   config.readPupUse = [&reads](const fs::path& path) {
      reads++;
      TableLibrary::PupUse use;
      use.usesPup = path.stem() != "AFM";
      use.names = { path.stem() == "AFM" ? "afm_113b" : lowerName(path.stem().string()) };
      return use;
   };
   Fixture::Touch(f.tables / "AFM" / "AFM.vpx");
   Fixture::Touch(f.tables / "Jaws" / "Jaws.vpx");
   Fixture::Touch(f.tables / "Bleach" / "Bleach.vpx");
   Fixture::Touch(shared / "AFM_113b" / "screens.pup"); // Shared, in another case
   Fixture::Touch(f.tables / "Bleach" / "PupVideos" / "bleach" / "screens.pup"); // Along the table

   {
      TableLibrary library(config);
      library.Rescan();
      std::map<string, Table> tables;
      for (const Table& table : library.GetTables())
         tables[table.path] = table;
      CHECK(tables["AFM/AFM.vpx"].hasPupPack);
      CHECK_FALSE(tables["AFM/AFM.vpx"].usesPup);
      CHECK_FALSE(tables["Jaws/Jaws.vpx"].hasPupPack);
      CHECK(tables["Jaws/Jaws.vpx"].usesPup);
      CHECK(tables["Jaws/Jaws.vpx"].pupPacks == std::vector<string> { "jaws" });
      CHECK(tables["Bleach/Bleach.vpx"].hasPupPack);
      CHECK(reads == 3);
   }

   // The names are cached in table-stats.json: a restart does not read the scripts again, and a pack added since is found
   Fixture::Touch(shared / "jaws" / "screens.pup");
   TableLibrary library(config);
   library.Rescan();
   CHECK(reads == 3);
   for (const Table& table : library.GetTables())
      CHECK(table.hasPupPack);
}

TEST_CASE("PupPackOptions: the layout batch files of a pack are applied without running them")
{
   Fixture f;
   const fs::path pack = f.tables / "pinupsystem" / "pupvideos" / "DarkestDungeon";
   Fixture::Touch(pack / "screens.pup", "default screens");
   Fixture::Touch(pack / "playlists.pup", "default playlists");
   Fixture::Touch(pack / "ScreenType.txt", "99\r\n");
   Fixture::Touch(pack / "pupinit.bat", "rem");
   Fixture::Touch(pack / "PuP-Pack_Options" / "3 Screen - VR" / "screens.pup", "vr screens");
   Fixture::Touch(pack / "PuP-Pack_Options" / "3 Screen - VR" / "playlists.pup", "vr playlists");
   Fixture::Touch(pack / "PuP-Pack_Options" / "Desktop" / "screens.pup", "desktop screens");
   Fixture::Touch(pack / "PuP-Pack_Options" / "Desktop" / "extra.png", "png");
   // The batch files of the Darkest Dungeon pack, and two that can not be done here
   Fixture::Touch(pack / "3 Screen - VR.bat", "rem echo off\r\nxcopy /y \"PuP-Pack_Options\\3 Screen - VR\\*.pup\"\r\ndel pupinit.bat\r\necho 2 > ScreenType.txt\r\n");
   Fixture::Touch(pack / "1 - Desktop.bat", "rem echo off\r\nxcopy /y \"pup-pack_options\\Desktop\\*\"\r\necho 2 > ScreenType.txt\"\r\n");
   Fixture::Touch(pack / "EditThisPuPPack.bat", "for %%* in (.) do SET packname=%%~n*\r\ncd ..\r\nStart PinUPPackEditor.exe %packname%\r\n");
   Fixture::Touch(pack / "Escape.bat", "copy ..\\..\\secret.txt .\r\n");

   CHECK(PupPackOptions::IsPack(pack));
   std::map<string, PupPackOptions::Option> options;
   for (const PupPackOptions::Option& option : PupPackOptions::List(pack))
      options[option.name] = option;
   REQUIRE(options.size() == 5); // With pupinit.bat, which only has a remark
   CHECK(options["3 Screen - VR"].supported);
   CHECK(options["3 Screen - VR"].recommended);
   CHECK(options["1 - Desktop"].supported);
   CHECK_FALSE(options["1 - Desktop"].recommended);
   CHECK_FALSE(options["EditThisPuPPack"].supported);
   CHECK_FALSE(options["Escape"].supported);
   CHECK_FALSE(options["pupinit"].supported);
   CHECK(PupPackOptions::Apply(pack, "Escape") != "");

   CHECK(PupPackOptions::Apply(pack, "3 Screen - VR") == "");
   CHECK(Fixture::Read(pack / "screens.pup") == "vr screens");
   CHECK(Fixture::Read(pack / "playlists.pup") == "vr playlists");
   CHECK(Fixture::Read(pack / "ScreenType.txt") == "2 \r\n"); // As cmd writes it
   CHECK(fs::exists(pack / "pupinit.bat")); // Batch files are never deleted
   CHECK(PupPackOptions::GetApplied(pack) == "3 Screen - VR");

   // Another option, then back to the pack as uploaded
   CHECK(PupPackOptions::Apply(pack, "1 - Desktop") == "");
   CHECK(Fixture::Read(pack / "screens.pup") == "desktop screens");
   CHECK(fs::exists(pack / "extra.png"));
   CHECK(PupPackOptions::Restore(pack) == "");
   CHECK(Fixture::Read(pack / "screens.pup") == "default screens");
   CHECK(Fixture::Read(pack / "playlists.pup") == "default playlists");
   CHECK(Fixture::Read(pack / "ScreenType.txt") == "99\r\n");
   CHECK_FALSE(fs::exists(pack / "extra.png"));
   CHECK_FALSE(fs::exists(pack / ".options-backup"));
   CHECK(PupPackOptions::GetApplied(pack) == "");
}

TEST_CASE("TableLibrary: imported PinUP Player packs get their VR option")
{
   const auto makePack = [](const fs::path& pack, const std::vector<string>& vrOptions) {
      Fixture::Touch(pack / "screens.pup", "default screens");
      Fixture::Touch(pack / "ScreenType.txt", "99\r\n");
      Fixture::Touch(pack / "1 - Desktop.bat", "echo 1 > ScreenType.txt\r\n");
      for (const string& option : vrOptions) {
         Fixture::Touch(pack / "PuP-Pack_Options" / option / "screens.pup", option + " screens");
         Fixture::Touch(pack / (option + ".bat"), "xcopy /y \"PuP-Pack_Options\\" + option + "\\*.pup\"\r\necho 2 > ScreenType.txt\r\n");
      }
   };
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   TableLibrary library(config);
   fs::create_directories(shared);

   // A pack with one VR layout, a pack with two (which one is for the player's setup is not known), and a table bundle with its pack
   makePack(f.root / "a" / "DarkestDungeon", { "3 Screen - VR" });
   Fixture::MakeZip(f.root / "a", shared / "DarkestDungeon.zip");
   makePack(f.root / "b" / "jaws", { "VR - FullDMD", "VR - Slim DMD" });
   Fixture::MakeZip(f.root / "b", shared / "jaws.zip");
   Fixture::Touch(f.root / "c" / "Bleach" / "Bleach.vpx");
   makePack(f.root / "c" / "Bleach" / "pupvideos" / "bleach", { "Bleach VR" });
   Fixture::MakeZip(f.root / "c", f.tables / "bleach.zip");

   library.Rescan();
   CHECK(PupPackOptions::GetApplied(shared / "DarkestDungeon") == "3 Screen - VR");
   CHECK(Fixture::Read(shared / "DarkestDungeon" / "screens.pup") == "3 Screen - VR screens");
   CHECK(Fixture::Read(shared / "DarkestDungeon" / "ScreenType.txt") == "2 \r\n");
   CHECK(PupPackOptions::GetApplied(shared / "jaws") == "");
   CHECK(Fixture::Read(shared / "jaws" / "screens.pup") == "default screens");
   CHECK(PupPackOptions::GetApplied(f.tables / "Bleach" / "pupvideos" / "bleach") == "Bleach VR");

   // An option chosen since then is kept by later imports of other packs, and the pack can still be restored as uploaded
   CHECK(PupPackOptions::Apply(shared / "DarkestDungeon", "1 - Desktop") == "");
   string error;
   CHECK(PupPackOptions::ApplyRecommended(shared / "DarkestDungeon", error) == "");
   CHECK(PupPackOptions::GetApplied(shared / "DarkestDungeon") == "1 - Desktop");
   CHECK(PupPackOptions::Restore(shared / "DarkestDungeon") == "");
   CHECK(Fixture::Read(shared / "DarkestDungeon" / "ScreenType.txt") == "99\r\n");

   // Uploaded again, the pack replaces the configured one and gets its VR option again
   CHECK(PupPackOptions::Apply(shared / "DarkestDungeon", "1 - Desktop") == "");
   makePack(f.root / "a" / "DarkestDungeon", { "3 Screen - VR" });
   Fixture::MakeZip(f.root / "a", shared / "DarkestDungeon.zip");
   library.Rescan();
   CHECK(PupPackOptions::GetApplied(shared / "DarkestDungeon") == "3 Screen - VR");
}

TEST_CASE("TableLibrary: imported PinUP Player packs are left as uploaded when asked")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   config.applyVrPupOptions = false;
   TableLibrary library(config);
   fs::create_directories(shared);
   Fixture::Touch(f.root / "a" / "DarkestDungeon" / "screens.pup", "default screens");
   Fixture::Touch(f.root / "a" / "DarkestDungeon" / "PuP-Pack_Options" / "VR" / "screens.pup", "vr screens");
   Fixture::Touch(f.root / "a" / "DarkestDungeon" / "VR.bat", "xcopy /y \"PuP-Pack_Options\\VR\\*.pup\"\r\n");
   Fixture::MakeZip(f.root / "a", shared / "DarkestDungeon.zip");

   library.Rescan();
   CHECK(PupPackOptions::GetApplied(shared / "DarkestDungeon") == "");
   CHECK(Fixture::Read(shared / "DarkestDungeon" / "screens.pup") == "default screens");
}

#endif
