// license:GPLv3+

// Steam Frame Edition: the routes of the web server for the table library and the leaderboards, used by the tables and scores pages
// (assets/web/tables.html, scores.html). Members of WebServer, declared in lib/src/WebServer.h.

#include "core/stdafx.h"
#include "lib/src/WebServer.h"

#include "core/player.h"
#include "core/VPApp.h"
#include "core/vpversion.h"
#include "parts/pintable.h"
#include "steamframe/Launcher.h"
#include "steamframe/ScoreStore.h"
#include "steamframe/TableLibrary.h"
#include "steamframe/vrui/UpdateCheck.h"

#include <nlohmann/json.hpp>
#include <filesystem>

using json = nlohmann::json;

namespace {
   // Same as WebServer.cpp
   constexpr const char* HEADER_JSON = "Content-Type: application/json\r\n";

   constexpr const char* RESPONSE_OK = "OK";
   constexpr const char* RESPONSE_BAD_REQUEST = "Bad request";
   constexpr const char* RESPONSE_NOT_FOUND = "File not found";
   constexpr const char* RESPONSE_METHOD_NOT_ALLOWED = "Method Not Allowed";
   constexpr const char* RESPONSE_CONFLICT = "Conflict";
   constexpr const char* RESPONSE_INTERNAL_SERVER_ERROR = "Server error";

   constexpr int STATUS_OK = 200;
   constexpr int STATUS_BAD_REQUEST = 400;
   constexpr int STATUS_NOT_FOUND = 404;
   constexpr int STATUS_METHOD_NOT_ALLOWED = 405;
   constexpr int STATUS_CONFLICT = 409;
   constexpr int STATUS_INTERNAL_SERVER_ERROR = 500;
}

string WebServer::GetVersionLabel()
{
   // Numbered releases (since 0.5.0) get a "v", the dated ones before them are shown as they are
   const string installed = VPX::VRUI::UpdateCheck::GetInstalledVersion();
   const bool numbered = !installed.empty() && std::isdigit(static_cast<unsigned char>(installed[0])) && installed.find('.') != string::npos;
   const string edition = installed.empty() ? "(development build)"s : (numbered ? "v" : "") + installed;
   return "Steam Frame Edition " + edition + " - vpinball v" STR(VP_VERSION_MAJOR) "." STR(VP_VERSION_MINOR) "." STR(VP_VERSION_REV) " Beta";
}

// The handler of a route of the table library or of the leaderboards, nullptr for another route. They all need a paired browser.
WebServer::RouteHandler WebServer::GetLibraryRoute(struct mg_http_message* hm)
{
   static constexpr std::pair<const char*, RouteHandler> routes[] = {
      { "/missing-roms", &WebServer::MissingRoms },
      { "/tables", &WebServer::Tables },
      { "/table-image", &WebServer::TableImage },
      { "/table-favorite", &WebServer::TableFavorite },
      { "/table-name", &WebServer::TableName },
      { "/table-delete", &WebServer::TableDelete },
      { "/scores", &WebServer::Scores },
      { "/score-delete", &WebServer::ScoreDelete },
      { "/scores-clear", &WebServer::ScoresClear },
      { "/score-assign", &WebServer::ScoreAssign },
      { "/profile-add", &WebServer::ProfileAdd },
      { "/profile-rename", &WebServer::ProfileRename },
      { "/profile-delete", &WebServer::ProfileDelete },
      { "/profile-active", &WebServer::ProfileActive },
   };
   for (const auto& [uri, handler] : routes)
      if (mg_match(hm->uri, mg_str(uri), NULL))
         return handler;
   return nullptr;
}

// GET: the ROMs that tables need and could not find (see TableLibrary::AddMissingRom), POST with 'clear': empty the list
void WebServer::MissingRoms(struct mg_connection *c, struct mg_http_message* hm)
{
   json list = json::array();
   VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
   if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
      char clear[8];
      mg_http_get_var(&hm->query, "clear", clear, sizeof(clear));
      if (*clear == '\0') {
         mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
         return;
      }
      library.ClearMissingRoms();
      PLOGI.printf("Missing ROM list cleared");
   }
   for (const VPinballLib::TableLibrary::MissingRom& missing : library.GetMissingRoms())
      list.push_back({ { "rom", missing.rom }, { "folder", missing.folder }, { "files", missing.files }, { "table", missing.table }, { "reportedAt", missing.reportedAt } });
   const string response = json { { "missingRoms", list } }.dump();
   mg_http_reply(c, STATUS_OK, HEADER_JSON, "%s", response.c_str());
}

///////////////////////////////////////////////////////////////////////////////
// Table library: what the table picker of the application shows, with the names and favorites it displays

// The table being played holds its files, and saves its settings when closing (the lobby is not a table, even when it is loaded from one to use its VR room)
static bool IsPlayed(const VPinballLib::TableLibrary& library, const VPinballLib::Table& table)
{
   if (g_pplayer == nullptr || g_pplayer->m_isLobby)
      return false;
   std::error_code ec;
   return std::filesystem::equivalent(g_pplayer->m_ptable->m_filename, library.GetFullPath(table), ec);
}

// The table named by the 'uuid' parameter, replying an error if there is none
static std::optional<VPinballLib::Table> GetRequestedTable(struct mg_connection *c, struct mg_http_message* hm)
{
   char uuid[64];
   mg_http_get_var(&hm->query, "uuid", uuid, sizeof(uuid));
   std::optional<VPinballLib::Table> table = *uuid == '\0' ? std::nullopt : g_launcher->GetTableLibrary().GetTable(uuid);
   if (!table)
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
   return table;
}

// GET: the tables of the library, with the revision of the list for the page to know when it changed
void WebServer::Tables(struct mg_connection *c, struct mg_http_message* hm)
{
   VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
   json tables = json::array();
   for (const VPinballLib::Table& table : library.GetTables())
      tables.push_back({
         { "uuid", table.uuid },
         { "name", table.name },
         { "defaultName", VPinballLib::TableLibrary::GetDefaultName(table) },
         { "path", table.path },
         { "hasImage", !table.image.empty() },
         { "createdAt", table.createdAt },
         { "modifiedAt", table.modifiedAt },
         { "lastPlayedAt", table.lastPlayedAt },
         { "playCount", table.playCount },
         { "favorite", table.favorite },
         { "playing", IsPlayed(library, table) } });
   const string response = json { { "revision", library.GetRevision() }, { "scanning", library.IsScanning() }, { "tables", tables } }.dump();
   mg_http_reply(c, STATUS_OK, HEADER_JSON, "%s", response.c_str());
}

// GET: the image of a table (the page adds its modification date to the URL, so that browsers may cache it)
void WebServer::TableImage(struct mg_connection *c, struct mg_http_message* hm)
{
   const std::optional<VPinballLib::Table> table = GetRequestedTable(c, hm);
   if (!table)
      return;
   const std::filesystem::path imagePath = g_launcher->GetTableLibrary().GetImagePath(*table);
   std::error_code ec;
   if (imagePath.empty() || !std::filesystem::is_regular_file(imagePath, ec)) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
      return;
   }
   struct mg_http_serve_opts opts = {};
   opts.extra_headers = "Cache-Control: max-age=86400\r\n";
   mg_http_serve_file(c, hm, PathToUTF8(imagePath).c_str(), &opts);
}

// POST with 'uuid' and 'favorite' (1 or 0)
void WebServer::TableFavorite(struct mg_connection *c, struct mg_http_message* hm)
{
   if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
      mg_http_reply(c, STATUS_METHOD_NOT_ALLOWED, "", "%s", RESPONSE_METHOD_NOT_ALLOWED);
      return;
   }
   char favorite[8];
   mg_http_get_var(&hm->query, "favorite", favorite, sizeof(favorite));
   if (*favorite == '\0') {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return;
   }
   const std::optional<VPinballLib::Table> table = GetRequestedTable(c, hm);
   if (!table)
      return;
   g_launcher->GetTableLibrary().SetFavorite(table->uuid, strcmp(favorite, "0") != 0); // False when unchanged, which is not an error
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

// POST with 'uuid' and 'name', the name displayed for the table by the table picker. An empty name gives back the one derived from the file name.
void WebServer::TableName(struct mg_connection *c, struct mg_http_message* hm)
{
   if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
      mg_http_reply(c, STATUS_METHOD_NOT_ALLOWED, "", "%s", RESPONSE_METHOD_NOT_ALLOWED);
      return;
   }
   // The parameter is required, even empty (a name which does not fit in the buffer fails to decode)
   char buffer[512];
   if (mg_http_var(hm->query, mg_str("name")).buf == nullptr || mg_http_get_var(&hm->query, "name", buffer, sizeof(buffer)) < 0) {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return;
   }
   // One line, without the spaces around it
   string name = buffer;
   std::replace_if(name.begin(), name.end(), [](char ch) { return ch == '\r' || ch == '\n' || ch == '\t'; }, ' ');
   name.erase(0, name.find_first_not_of(' '));
   name.erase(name.find_last_not_of(' ') + 1);

   const std::optional<VPinballLib::Table> table = GetRequestedTable(c, hm);
   if (!table)
      return;
   if (g_launcher->GetTableLibrary().Rename(table->uuid, name)) // False when unchanged, which is not an error
      PLOGI.printf("Table display name changed: %s -> %s", table->name.c_str(), name.empty() ? VPinballLib::TableLibrary::GetDefaultName(*table).c_str() : name.c_str());
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

// POST with 'uuid': delete the table and its files (the whole folder when it is the only table in it)
void WebServer::TableDelete(struct mg_connection *c, struct mg_http_message* hm)
{
   if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
      mg_http_reply(c, STATUS_METHOD_NOT_ALLOWED, "", "%s", RESPONSE_METHOD_NOT_ALLOWED);
      return;
   }
   const std::optional<VPinballLib::Table> table = GetRequestedTable(c, hm);
   if (!table)
      return;
   VPinballLib::TableLibrary& library = g_launcher->GetTableLibrary();
   if (IsPlayed(library, *table)) {
      mg_http_reply(c, STATUS_CONFLICT, "", RESPONSE_CONFLICT);
      return;
   }
   if (!library.Delete(table->uuid)) {
      mg_http_reply(c, STATUS_INTERNAL_SERVER_ERROR, "", RESPONSE_INTERNAL_SERVER_ERROR);
      return;
   }
   PLOGI.printf("Table deleted: %s (%s)", table->name.c_str(), table->path.c_str());
   SetLastUpdate(); // Files are gone: the file manager refreshes
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

///////////////////////////////////////////////////////////////////////////////
// Leaderboards: the scores recorded at the end of the games (see ScoreTracker), and the profiles of the players

static bool IsPost(struct mg_connection *c, struct mg_http_message* hm)
{
   if (mg_strcmp(hm->method, mg_str("POST")) == 0)
      return true;
   mg_http_reply(c, STATUS_METHOD_NOT_ALLOWED, "", "%s", RESPONSE_METHOD_NOT_ALLOWED);
   return false;
}

// A query parameter, which must be given (even empty) unless 'optional'. Replies an error and returns nothing when it is missing.
static std::optional<string> GetParameter(struct mg_connection *c, struct mg_http_message* hm, const char* name, bool optional = false)
{
   char buffer[512];
   if (mg_http_var(hm->query, mg_str(name)).buf == nullptr) {
      if (optional)
         return string();
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return std::nullopt;
   }
   if (mg_http_get_var(&hm->query, name, buffer, sizeof(buffer)) < 0) {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return std::nullopt;
   }
   return string(buffer);
}

// A name of profile on one line, without the spaces around it (callers limit it to 32 characters, as the lobby shows them in lists)
static string CleanProfileName(string name)
{
   std::replace_if(name.begin(), name.end(), [](char ch) { return ch == '\r' || ch == '\n' || ch == '\t'; }, ' ');
   name.erase(0, name.find_first_not_of(' '));
   name.erase(name.find_last_not_of(' ') + 1);
   return name;
}

// GET: the profiles, the tables (of the library, and the ones which are gone but still have scores), and all the scores, best first, with their rank on their table
void WebServer::Scores(struct mg_connection *c, struct mg_http_message* hm)
{
   VPinballLib::ScoreStore& store = g_launcher->GetScoreStore();
   const uint64_t revision = store.GetRevision(); // Before reading, so that a change made meanwhile is seen at the next poll
   const std::vector<VPinballLib::Score> scores = store.GetScores();
   const std::optional<VPinballLib::Profile> active = store.GetActiveProfile();

   std::map<string, int> scoreCounts;
   for (const VPinballLib::Score& score : scores)
      scoreCounts[score.profileId]++;
   json profiles = json::array();
   for (const VPinballLib::Profile& profile : store.GetProfiles())
      profiles.push_back({ { "id", profile.id }, { "name", profile.name }, { "createdAt", profile.createdAt }, { "scoreCount", scoreCounts[profile.id] } });

   json tables = json::array();
   std::set<string> knownTables;
   for (const VPinballLib::Table& table : g_launcher->GetTableLibrary().GetTables()) {
      knownTables.insert(table.uuid);
      tables.push_back({ { "uuid", table.uuid }, { "name", table.name }, { "inLibrary", true } });
   }

   json list = json::array();
   std::map<string, int> ranks;
   for (const VPinballLib::Score& score : scores) {
      if (knownTables.insert(score.tableUuid).second)
         tables.push_back({ { "uuid", score.tableUuid }, { "name", score.tableName }, { "inLibrary", false } });
      list.push_back({
         { "id", score.id },
         { "tableUuid", score.tableUuid },
         { "profileId", score.profileId },
         { "playerSlot", score.playerSlot },
         { "playerCount", score.playerCount },
         { "score", score.score },
         { "playedAt", score.playedAt },
         { "durationSec", score.durationSec },
         { "source", score.source },
         { "rank", ++ranks[score.tableUuid] } });
   }
   const string response = json {
      { "revision", revision },
      { "activeProfileId", active ? active->id : string() },
      { "players", store.GetPlayerProfileIds() }, // Profile id of each player of the games, empty if unassigned
      { "profiles", profiles },
      { "tables", tables },
      { "scores", list } }.dump();
   mg_http_reply(c, STATUS_OK, HEADER_JSON, "%s", response.c_str());
}

// POST with 'id'
void WebServer::ScoreDelete(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetParameter(c, hm, "id");
   if (!id)
      return;
   VPinballLib::ScoreStore& store = g_launcher->GetScoreStore();
   const std::optional<VPinballLib::Score> score = store.GetScore(*id);
   if (!score || !store.DeleteScore(*id)) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
      return;
   }
   PLOGI.printf("Score deleted: %s on %s", VPinballLib::ScoreStore::FormatScore(score->score).c_str(), score->tableName.c_str());
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

// POST with 'uuid' to delete the scores of a table, or with 'all=1' to delete all the scores (an empty uuid is refused, so that a page which lost its table
// can not delete everything): replies the number of deleted scores as JSON
void WebServer::ScoresClear(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> uuid = GetParameter(c, hm, "uuid", true);
   const std::optional<string> all = uuid ? GetParameter(c, hm, "all", true) : std::nullopt;
   if (!all)
      return;
   if (uuid->empty() == (*all != "1")) {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return;
   }
   const size_t count = g_launcher->GetScoreStore().ClearScores(*uuid);
   if (uuid->empty())
      PLOGI.printf("All scores cleared: %zu deleted", count);
   else
      PLOGI.printf("Scores of table %s cleared: %zu deleted", uuid->c_str(), count);
   const string response = json { { "deleted", count } }.dump();
   mg_http_reply(c, STATUS_OK, HEADER_JSON, "%s", response.c_str());
}

// POST with 'id' and 'profile' (empty: the score belongs to nobody)
void WebServer::ScoreAssign(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetParameter(c, hm, "id");
   const std::optional<string> profile = id ? GetParameter(c, hm, "profile") : std::nullopt;
   if (!profile)
      return;
   if (!g_launcher->GetScoreStore().AssignScore(*id, *profile)) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
      return;
   }
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

// POST with 'name': replies the new profile as JSON, or 409 if the name is already used
void WebServer::ProfileAdd(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> name = GetParameter(c, hm, "name");
   if (!name)
      return;
   const string cleanName = CleanProfileName(*name);
   if (cleanName.empty() || cleanName.size() > 32) {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return;
   }
   const std::optional<VPinballLib::Profile> profile = g_launcher->GetScoreStore().AddProfile(cleanName);
   if (!profile) {
      mg_http_reply(c, STATUS_CONFLICT, "", RESPONSE_CONFLICT);
      return;
   }
   const string response = json { { "id", profile->id }, { "name", profile->name } }.dump();
   mg_http_reply(c, STATUS_OK, HEADER_JSON, "%s", response.c_str());
}

// POST with 'id' and 'name': 409 if the name is used by another profile
void WebServer::ProfileRename(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetParameter(c, hm, "id");
   const std::optional<string> name = id ? GetParameter(c, hm, "name") : std::nullopt;
   if (!name)
      return;
   const string cleanName = CleanProfileName(*name);
   if (cleanName.empty() || cleanName.size() > 32) {
      mg_http_reply(c, STATUS_BAD_REQUEST, "", "%s", RESPONSE_BAD_REQUEST);
      return;
   }
   VPinballLib::ScoreStore& store = g_launcher->GetScoreStore();
   if (!store.GetProfile(*id)) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
      return;
   }
   if (!store.RenameProfile(*id, cleanName)) {
      mg_http_reply(c, STATUS_CONFLICT, "", RESPONSE_CONFLICT);
      return;
   }
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

// POST with 'id': its scores are kept, and belong to nobody afterward
void WebServer::ProfileDelete(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetParameter(c, hm, "id");
   if (!id)
      return;
   if (!g_launcher->GetScoreStore().DeleteProfile(*id)) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
      return;
   }
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

// POST with 'id': the profile of player 1, to which the next scores of the one wearing the headset are given
void WebServer::ProfileActive(struct mg_connection *c, struct mg_http_message* hm)
{
   if (!IsPost(c, hm))
      return;
   const std::optional<string> id = GetParameter(c, hm, "id");
   if (!id)
      return;
   if (!g_launcher->GetScoreStore().SetActiveProfile(*id)) {
      mg_http_reply(c, STATUS_NOT_FOUND, "", RESPONSE_NOT_FOUND);
      return;
   }
   mg_http_reply(c, STATUS_OK, "", RESPONSE_OK);
}

