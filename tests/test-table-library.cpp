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

TEST_CASE("TableLibrary: a pack with its .pup files in its option folders only is one pack, set up for the VR cabinet")
{
   // The Austin Powers pack: no .pup file at its root, five layouts copying theirs from 'PuP-Pack_Options', none named VR
   const std::vector<string> layouts { "Option 1 - DMD and Topper on BG", "Option 2 - DMD on BG Topper Separate", "Option 3 - BG Topper Separate",
      "Option 4 - Topper on BG No DMD", "Option 5 - DMD on BG No Topper" };
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   fs::create_directories(shared);
   const fs::path source = f.root / "a" / "austin";
   Fixture::Touch(source / "Topper" / "Topper.mp4");
   Fixture::Touch(source / "PUPCapture" / "1.bmp");
   Fixture::Touch(source / "EditThisPuPPack.bat", "for %%* in (.) do SET packname=%%~n*\r\ncd ..\r\nStart PinUPPackEditor.exe %packname%\r\n");
   for (const string& layout : layouts) {
      Fixture::Touch(source / "PuP-Pack_Options" / layout / "screens.pup", layout + " screens");
      Fixture::Touch(source / "PuP-Pack_Options" / layout / "triggers.pup", layout + " triggers");
      Fixture::Touch(source / (layout + ".bat"), "rem echo off\r\nxcopy /y \"PuP-Pack_Options\\" + layout + "\\*.pup\"\r\n");
   }

   CHECK(PupPackOptions::IsPack(source));
   CHECK(PupPackOptions::PackSources(source).size() == layouts.size());
   std::vector<string> recommended;
   for (const PupPackOptions::Option& option : PupPackOptions::List(source))
      if (option.recommended)
         recommended.push_back(option.name);
   CHECK(recommended == std::vector<string> { "Option 3 - BG Topper Separate" });

   Fixture::MakeZip(f.root / "a", shared / "austin.zip");
   TableLibrary library(config);
   library.Rescan();
   std::vector<string> packs;
   for (const auto& entry : fs::directory_iterator(shared))
      packs.push_back(entry.path().filename().string());
   CHECK(packs == std::vector<string> { "austin" });
   CHECK(fs::exists(shared / "austin" / "Topper" / "Topper.mp4"));
   CHECK(fs::exists(shared / "austin" / "PUPCapture" / "1.bmp"));
   CHECK(PupPackOptions::GetApplied(shared / "austin") == "Option 3 - BG Topper Separate");
   CHECK(Fixture::Read(shared / "austin" / "screens.pup") == "Option 3 - BG Topper Separate screens");
   CHECK(Fixture::Read(shared / "austin" / "triggers.pup") == "Option 3 - BG Topper Separate triggers");
}

TEST_CASE("PupPackOptions: without a VR layout, the layout using the most displays of the cabinet is chosen")
{
   // The Aliens pack: "Seperate", option folders named apart from the batch files, and two layouts keeping the screens apart, told
   // apart by their screens.pup only (the first one adds the full DMD)
   const string header = "ScreenNum,ScreenDes,PlayList,PlayFile,Loopit,Active,Priority,CustomPos\r\n";
   const string topper = "13,Topper,,,0,ForceOn,,\"0,0,0,100,100\"\r\n", topperOff = "13,Topper,,,0,off,,\"0,0,0,100,100\"\r\n";
   const string overlay = "14,Overlay,,,0,ForceOn,,\"2,0,0,100,100\"\r\n", backglass = "15,\"Backglass Videos\",,,0,ForceBack,,\"2,3,6,93.6,83.4\"\r\n";
   const struct { string bat, folder, screens; } layouts[] {
      { "Option 1 - Topper BG  with a FULLDMD Video - All Seperate", "Option1-BGTopperFullDMDSeperate", header + topper + overlay + backglass + "16,FULLDMD,,,0,ForceBack,,\"5,0,0,100,100\"\r\n" },
      { "Option 2 - Topper BG Seperate", "Option2-BGTopperSeperate", header + topper + overlay + backglass + "16,FULLDMD,,,0,off,,\"5,0,0,100,100\"\r\n" },
      { "Option 3 - PupStyle On FULLDMD use OriginalB2S", "Option3-PupStyleOnFULLDMD", header + topperOff + "14,Overlay,,,0,ForceOn,,\"5,0,0,100,100\"\r\n" },
      { "Option 4 - 2screen DMD on BG", "Option4-2screenDMDonBG", header + topperOff + overlay + backglass },
   };
   Fixture f;
   const fs::path pack = f.root / "alienspupver";
   for (const auto& layout : layouts) {
      Fixture::Touch(pack / "PuP-Pack_Options" / layout.folder / "screens.pup", layout.screens);
      Fixture::Touch(pack / (layout.bat + ".bat"), "rem echo off\r\nxcopy /y \"PuP-Pack_Options\\" + layout.folder + "\\*.pup\"");
   }
   const auto recommended = [&pack]() {
      std::vector<string> names;
      for (const PupPackOptions::Option& option : PupPackOptions::List(pack))
         if (option.recommended)
            names.push_back(option.name);
      return names;
   };
   CHECK(recommended() == std::vector<string> { layouts[0].bat });
   string error;
   CHECK(PupPackOptions::ApplyRecommended(pack, error) == layouts[0].bat);
   CHECK(error.empty());
   CHECK(Fixture::Read(pack / "screens.pup") == layouts[0].screens);

   SUBCASE("a pack playing as uploaded is left alone when no layout is better than another")
   {
      const fs::path other = f.root / "other";
      Fixture::Touch(other / "screens.pup", "default screens");
      for (const string name : { "A", "B" }) {
         Fixture::Touch(other / "Options" / name / "screens.pup", header + topper + backglass);
         Fixture::Touch(other / (name + ".bat"), "xcopy /y \"Options\\" + name + "\\*.pup\"\r\n");
      }
      for (const PupPackOptions::Option& option : PupPackOptions::List(other))
         CHECK_FALSE(option.recommended);
      fs::remove(other / "screens.pup");
      CHECK(PupPackOptions::List(other)[0].recommended); // Without .pup files of its own, it gets the first of the best layouts
   }
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


TEST_CASE("TableLibrary: the ROMs and packs of uploaded bundles go where PinMAME and PinUP Player look for them")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path shared = f.tables / "pinupsystem" / "pupvideos";
   const fs::path roms = f.tables / "pinmame" / "roms";
   config.excludedPaths.push_back(f.tables / "pinupsystem");
   config.sharedPupVideosPath = shared;
   config.romsPath = roms;
   TableLibrary library(config);

   // A ROM folder beside the table folder, and a ROM next to a table
   Fixture::Touch(f.root / "a" / "AFM" / "AFM.vpx");
   Fixture::Touch(f.root / "a" / "roms" / "afm_113b.zip", "afm rom");
   Fixture::MakeZip(f.root / "a", f.tables / "afm-bundle.zip");
   Fixture::Touch(f.root / "b" / "Tron" / "Tron.vpx");
   Fixture::Touch(f.root / "b" / "Tron" / "trn_174h.zip", "tron rom");
   Fixture::MakeZip(f.root / "b", f.tables / "tron-bundle.zip");
   // A Windows install: Tables, VPinMAME/roms and PinUPSystem/PUPVideos
   Fixture::Touch(f.root / "c" / "Tables" / "Motu.vpx");
   Fixture::Touch(f.root / "c" / "VPinMAME" / "roms" / "motu.zip", "motu rom");
   Fixture::Touch(f.root / "c" / "PinUPSystem" / "PUPVideos" / "motu" / "screens.pup");
   Fixture::MakeZip(f.root / "c", f.tables / "motu-bundle.zip");
   // A pack beside the table, in its folder but not in its 'pupvideos' folder
   Fixture::Touch(f.root / "d" / "Jaws" / "Jaws.vpx");
   Fixture::Touch(f.root / "d" / "Jaws" / "jaws" / "screens.pup");
   Fixture::Touch(f.root / "d" / "Jaws" / "pupvideos" / "jaws_alt" / "screens.pup"); // Where the plugin looks: stays
   Fixture::MakeZip(f.root / "d", f.tables / "jaws-bundle.zip");
   // A table with its own ROM folder (PinMAME then only looks there): its other ROMs join it, its own stay
   Fixture::Touch(f.root / "e" / "Bleach" / "Bleach.vpx");
   Fixture::Touch(f.root / "e" / "Bleach" / "pinmame" / "roms" / "bleach.zip", "bleach rom");
   Fixture::Touch(f.root / "e" / "Bleach" / "roms" / "bleach_sound.zip", "bleach sound");
   Fixture::MakeZip(f.root / "e", f.tables / "bleach-bundle.zip");
   // Only ROMs, in a 'roms' folder
   Fixture::Touch(f.root / "g" / "roms" / "ss_15.zip", "ss rom");
   Fixture::MakeZip(f.root / "g", f.tables / "rom-pack.zip");

   library.AddMissingRom("afm_113b.zip", roms, { "afm_113b.zip" }, "AFM");
   library.Rescan();
   CHECK(Paths(library) == std::vector<string> { "AFM/AFM.vpx", "Bleach/Bleach.vpx", "Jaws/Jaws.vpx", "Motu/Motu.vpx", "Tron/Tron.vpx" });
   CHECK(Fixture::Read(roms / "afm_113b.zip") == "afm rom");
   CHECK(Fixture::Read(roms / "trn_174h.zip") == "tron rom");
   CHECK_FALSE(fs::exists(f.tables / "Tron" / "trn_174h.zip"));
   CHECK(Fixture::Read(roms / "motu.zip") == "motu rom");
   CHECK(fs::exists(shared / "motu" / "screens.pup"));
   CHECK(fs::exists(shared / "jaws" / "screens.pup"));
   CHECK_FALSE(fs::exists(f.tables / "Jaws" / "jaws"));
   CHECK(fs::exists(f.tables / "Jaws" / "pupvideos" / "jaws_alt" / "screens.pup"));
   CHECK(Fixture::Read(f.tables / "Bleach" / "pinmame" / "roms" / "bleach.zip") == "bleach rom");
   CHECK(Fixture::Read(f.tables / "Bleach" / "pinmame" / "roms" / "bleach_sound.zip") == "bleach sound");
   CHECK(Fixture::Read(roms / "ss_15.zip") == "ss rom");
   for (const char* bundle : { "afm-bundle.zip", "tron-bundle.zip", "motu-bundle.zip", "jaws-bundle.zip", "bleach-bundle.zip", "rom-pack.zip" })
      CHECK_FALSE(fs::exists(f.tables / bundle));
   CHECK(library.GetMissingRoms().empty()); // The ROM was added
}

TEST_CASE("TableLibrary: a ROM uploaded with the tables goes to the ROM folder once a table needs it")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path roms = f.tables / "pinmame" / "roms";
   config.romsPath = roms;
   config.readPupUse = [](const fs::path& path) {
      TableLibrary::PupUse use;
      if (path.stem() == "AFM")
         use.rom = "afm_113b";
      return use;
   };
   // The ROM first, alone: nothing tells yet that it is a ROM
   Fixture::Touch(f.root / "a" / "afm_113b.bin", "rom data");
   Fixture::MakeZip(f.root / "a", f.tables / "afm_113b.zip");
   Fixture::Touch(f.root / "b" / "readme.txt");
   Fixture::MakeZip(f.root / "b", f.tables / "readme.zip");
   {
      TableLibrary library(config);
      library.Rescan();
      CHECK(fs::exists(f.tables / "afm_113b.zip"));
      CHECK(f.unzipCount == 2);
   }

   // After a restart, the archives already looked into are not extracted again; the table comes, and its ROM goes to the ROM folder
   TableLibrary library(config);
   Fixture::Touch(f.tables / "AFM" / "AFM.vpx");
   library.Rescan();
   CHECK(f.unzipCount == 2);
   CHECK_FALSE(fs::exists(f.tables / "afm_113b.zip"));
   CHECK(fs::exists(roms / "afm_113b.zip"));
   CHECK(fs::exists(f.tables / "readme.zip")); // Not a ROM a table needs: left untouched
   for (const Table& table : library.GetTables())
      CHECK(table.rom == "afm_113b");

   // A ROM reported missing is recognized as well
   library.AddMissingRom("ss_15.zip", roms, { "ss_15.zip" }, "Scared Stiff");
   Fixture::Touch(f.root / "c" / "ss_15.bin");
   Fixture::MakeZip(f.root / "c", f.tables / "SS_15.zip");
   library.Rescan();
   CHECK(fs::exists(roms / "SS_15.zip"));
   CHECK(library.GetMissingRoms().empty());
}
namespace {

// A driver catalog of PinMAME with a few games, in the format of gamedefs/catalog/pinmame.json
fs::path WriteCatalog(const fs::path& folder)
{
   const fs::path path = folder / "pinmame.json";
   nlohmann::json drivers = nlohmann::json::array();
   for (const char* id : { "simpprty", "simpprtg", "sorcr_l1", "sorcr_l2", "afm_113b", "tz_94h", "simp_a27" })
      drivers.push_back({ { "id", id }, { "description", id } });
   Fixture::Touch(path, nlohmann::json { { "drivers", drivers } }.dump());
   return path;
}

}

TEST_CASE("RomAliases: the alias file is read and checked as libpinmame reads it")
{
   Fixture f;
   RomAliases::GameCatalog catalog(WriteCatalog(f.root));
   CHECK(catalog.IsAvailable());
   CHECK(catalog.IsGame("SimpPrty"));
   CHECK_FALSE(catalog.IsGame("simpprtyh"));
   CHECK(catalog.GetGamePrefix("simpprtyh") == "simpprty");
   CHECK(catalog.GetGamePrefix("tz_94hmod") == "tz_94h");
   CHECK(catalog.GetGamePrefix("simpprtyhorrorhalloween") == ""); // A prefix shorter than half of the name is no clue
   CHECK(catalog.GetGamePrefix("simpprty") == "");

   const fs::path aliasFile = f.root / "pinmame" / "alias.txt";
   Fixture::Touch(aliasFile, "# comment\nsimp,simp_a27\nSIMPPRTYH , simpprty # mod\nnocomma\n");
   CHECK(RomAliases::FindAlias(aliasFile, "simp") == "simp_a27");
   CHECK(RomAliases::FindAlias(aliasFile, "simpprtyh") == "simpprty");
   CHECK_FALSE(RomAliases::FindAlias(aliasFile, "nocomma"));
   CHECK_FALSE(RomAliases::FindAlias(aliasFile, "comment"));

   // Appended after a missing final new line, the comment line kept short enough for libpinmame
   Fixture::Touch(aliasFile, "simp,simp_a27");
   string error;
   CHECK(RomAliases::AppendAlias(aliasFile, "alienspupver", "sorcr_l1", "From readme.txt, for " + string(200, 'x'), error));
   const string content = Fixture::Read(aliasFile);
   CHECK(content.starts_with("simp,simp_a27\n# From readme.txt, for xxx"));
   CHECK(content.ends_with("...\nalienspupver,sorcr_l1\n"));
   for (std::istringstream lines(content); !lines.eof();) {
      string line;
      std::getline(lines, line);
      CHECK(line.size() <= 126);
   }

   std::vector<RomAliases::Issue> errors, warnings;
   const string normalized = RomAliases::Normalize("  # aliases \r\nsimpprtyh, simpprty\r\n\r\nalone\nsimpprtyh,simpprtg\nfoo,notagame # x\nsimpprty,simpprtg\n" + string(130, '#') + "\n  \n\n", catalog, errors, warnings);
   CHECK(normalized == "# aliases\nsimpprtyh, simpprty\n\nalone\nsimpprtyh,simpprtg\nfoo,notagame # x\nsimpprty,simpprtg\n" + string(130, '#') + "\n");
   REQUIRE(errors.size() == 2);
   CHECK(errors[0].line == 4); // A single name: libpinmame would crash on it
   CHECK(errors[1].line == 8); // Read in parts
   REQUIRE(warnings.size() == 3);
   CHECK(warnings[0].line == 5); // Given twice
   CHECK(warnings[1].line == 6); // Unknown game
   CHECK(warnings[2].line == 7); // Replaces a game
   errors.clear();
   warnings.clear();
   // The lines of the replaced file get no warning: the user did not write them
   RomAliases::Normalize("foo,notagame\nbar,notagame\n", catalog, errors, warnings, "foo,notagame\n");
   REQUIRE(warnings.size() == 1);
   CHECK(warnings[0].line == 2);
   CHECK(RomAliases::Normalize("a,simp_a27", catalog, errors, warnings) == "a,simp_a27\n");
   CHECK(RomAliases::Normalize("", catalog, errors, warnings).empty());
   CHECK(errors.empty());
}

TEST_CASE("RomAliases: the game of an unknown name is found along the table")
{
   Fixture f;
   RomAliases::GameCatalog catalog(WriteCatalog(f.root));
   const fs::path table = f.tables / "Simpsons";
   const std::map<string, RomAliases::Evidence> noHints;

   // The readme gives the pair, as it tells the user to add it to VPMAlias.txt
   Fixture::Touch(table / "readme.txt", "You must set an alias in your VPMAlias.txt file to \"simpprtyh,simpprty\" or change cGameName.");
   std::optional<RomAliases::Evidence> found = RomAliases::FindGame("simpprtyh", {}, table, noHints, catalog);
   REQUIRE(found);
   CHECK(found->game == "simpprty");
   CHECK(found->source == "readme.txt");
   // A pair whose game PinMAME does not know is no clue, nor a longer name: the next clue is used
   Fixture::Touch(table / "readme.txt", "simpprtyh,simpprtyx and simpprtyhx,simpprtg");
   found = RomAliases::FindGame("simpprtyh", {}, table, noHints, catalog);
   REQUIRE(found);
   CHECK(found->source == "its name");

   // A hint from the bundle comes first, then the script
   found = RomAliases::FindGame("simpprtyh", {}, table, { { "simpprtyh", { "simpprtg", "notes.txt of the uploaded bundle" } } }, catalog);
   CHECK(found->game == "simpprtg");
   found = RomAliases::FindGame("alienspupver", { "notagame", "sorcr_l1", "sorcr_l2" }, f.tables / "Aliens", noHints, catalog);
   REQUIRE(found);
   CHECK(found->game == "sorcr_l1");
   CHECK(found->source == "the table script");

   // Files and folders named after a game
   Fixture::Touch(f.tables / "Aliens" / "altcolor" / "sorcr_l2" / "pin2dmd.pal");
   found = RomAliases::FindGame("alienspupver", {}, f.tables / "Aliens", noHints, catalog);
   REQUIRE(found);
   CHECK(found->game == "sorcr_l2");
   CHECK(found->source == "altcolor/sorcr_l2");
   Fixture::Touch(f.tables / "Aliens" / "pinmame" / "nvram" / "afm_113b.nv");
   CHECK_FALSE(RomAliases::FindGame("alienspupver", {}, f.tables / "Aliens", noHints, catalog)); // Two games, none in the name

   // Nothing along the table: no game
   CHECK_FALSE(RomAliases::FindGame("zzzunknown", {}, f.tables / "Nothing", noHints, catalog));
}

TEST_CASE("TableLibrary: a table asking for an unknown game gets its alias and its ROM")
{
   Fixture f;
   TableLibrary::Config config = f.Config();
   const fs::path roms = f.tables / "pinmame" / "roms";
   config.romsPath = roms;
   config.pinmameCatalogPath = WriteCatalog(f.root);
   config.defaultAliasesPath = f.root / "VPMAlias.txt";
   Fixture::Touch(config.defaultAliasesPath, "# VPinMAME aliases\nsimp,simp_a27\n");
   config.readPupUse = [](const fs::path& path) {
      TableLibrary::PupUse use;
      const string name = path.stem().string();
      if (name == "Simpsons TOH")
         use.rom = "simpprtyh";
      else if (name == "Aliens") {
         use.rom = "alienspupver";
         use.games = { "sorcr_l2" }; // Commented cGameName
      } else if (name == "Mystery")
         use.rom = "zzzunknown";
      else if (name == "AFM")
         use.rom = "afm_113b";
      return use;
   };

   // The Simpsons Treehouse of Horror mod: the alias in its readme, a muted ROM in a folder of its own
   Fixture::Touch(f.root / "toh" / "Simpsons TOH.vpx");
   Fixture::Touch(f.root / "toh" / "readme.txt", "set an alias in your VPMAlias.txt file to \"simpprtyh,simpprty\"");
   Fixture::Touch(f.root / "toh" / "MutedRom" / "simpprty.zip", "muted rom");
   Fixture::MakeZip(f.root / "toh", f.tables / "toh.zip");
   // Aliens: the table in a sub folder, the alias only in the instructions at the root of the bundle
   Fixture::Touch(f.root / "aliens" / "Aliens" / "Aliens.vpx");
   Fixture::Touch(f.root / "aliens" / "INSTALLATION INSTRUCTIONS.txt", "Add this line to VPMAlias.txt: alienspupver,sorcr_l1");
   Fixture::MakeZip(f.root / "aliens", f.tables / "aliens.zip");
   // A table asking for an unknown game without any clue, and one whose ROM is already installed (a ROM in its folder must not replace it)
   Fixture::Touch(f.tables / "Mystery" / "Mystery.vpx");
   Fixture::Touch(f.tables / "AFM" / "AFM.vpx");
   Fixture::Touch(f.tables / "AFM" / "extras" / "afm_113b.zip", "other rom");
   Fixture::Touch(roms / "afm_113b.zip", "installed rom");

   TableLibrary library(config);
   library.Rescan();
   const fs::path aliasFile = f.tables / "pinmame" / "alias.txt";
   const string aliases = Fixture::Read(aliasFile);
   CHECK(aliases.starts_with("# VPinMAME aliases\nsimp,simp_a27\n")); // Created from VPinMAME's aliases
   CHECK(RomAliases::FindAlias(aliasFile, "simpprtyh") == "simpprty");
   CHECK(RomAliases::FindAlias(aliasFile, "alienspupver") == "sorcr_l1");
   CHECK(aliases.find("# From INSTALLATION INSTRUCTIONS.txt of the uploaded bundle, for Aliens") != string::npos);
   CHECK(aliases.find("# From readme.txt of the uploaded bundle, for Simpsons TOH") != string::npos);
   CHECK(Fixture::Read(roms / "simpprty.zip") == "muted rom");
   CHECK(Fixture::Read(roms / "afm_113b.zip") == "installed rom");
   CHECK(fs::exists(f.tables / "AFM" / "extras" / "afm_113b.zip"));

   std::vector<TableLibrary::UnknownGame> unknown = library.GetUnknownGames();
   REQUIRE(unknown.size() == 1);
   CHECK(unknown[0].table == "Mystery");
   CHECK(unknown[0].rom == "zzzunknown");

   // Saved from the web server: refused with an error, then accepted, and the table is no longer unknown
   std::vector<RomAliases::Issue> errors, warnings;
   string error;
   CHECK_FALSE(library.SaveAliasFile(aliases + "zzzunknown\n", errors, warnings, error));
   CHECK(Fixture::Read(aliasFile) == aliases);
   errors.clear();
   CHECK(library.SaveAliasFile(aliases + "zzzunknown,tz_94h\n", errors, warnings, error));
   CHECK(errors.empty());
   CHECK(library.ReadAliasFile() == aliases + "zzzunknown,tz_94h\n");
   CHECK(library.GetUnknownGames().empty());

   // The alias file exists: it is never replaced by the default one, and an alias is not added twice
   library.Rescan();
   CHECK(library.ReadAliasFile() == aliases + "zzzunknown,tz_94h\n");
}

#endif
