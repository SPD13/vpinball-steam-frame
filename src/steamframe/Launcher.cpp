// license:GPLv3+

#include "core/stdafx.h"
#include "Launcher.h"

#include "core/editablereg.h"
#include "core/VPApp.h"
#include "steamframe/ScoreStore.h"
#include "steamframe/TableLibrary.h"
#include "steamframe/TablePickerPage.h"
#include "steamframe/vrui/VRUI.h"
#include "parts/Collection.h"
#include "parts/Material.h"
#include "parts/PartGroup.h"
#include "parts/pintable.h"
#include "parts/primitive.h"
#include "ui/live/LiveUI.h"
#include "core/player.h"
#include "ui/live/ingameui/InGameUIItem.h"
#include "ui/live/ingameui/InGameUIPage.h"
#include "steamframe/ScoresPage.h"
#ifdef VPX_TABLE_WEBSERVER
#include "lib/src/WebServer.h"
#include "lib/src/ZipUtils.h"
#endif

#include <fstream>
#include <nlohmann/json.hpp>

Launcher* g_launcher = nullptr;

// Same definition as the one of the PinMAME plugin, which reads its value when it is loaded
static VPX::Properties::PropertyRegistry::PropId GetPinMAMEPathPropId()
{
   return Settings::GetRegistry().Register(std::make_unique<VPX::Properties::StringPropertyDef>(
      "Plugin.PinMAME"s, "PinMAMEPath"s, "PinMAME Path"s, "Folder that contains PinMAME subfolders (roms, nvram, ...)"s, false, ""s));
}

Launcher::Launcher()
{
   assert(g_launcher == nullptr);
   g_launcher = this;
}

Launcher::~Launcher()
{
#ifdef VPX_TABLE_WEBSERVER
   m_webServer = nullptr; // Uses the table library
#endif
   if (m_isSharedPinMAMEFolderApplied)
      g_settingsService.GetAppSettings().Reset(GetPinMAMEPathPropId());
   g_launcher = nullptr;
}

VPinballLib::TableLibrary& Launcher::GetTableLibrary()
{
   if (m_tableLibrary == nullptr)
   {
      // The default tables folder is the user's documents folder, which must neither be scanned as a whole nor have
      // its archives imported, so the library owns a dedicated sub folder
      VPinballLib::TableLibrary::Config config;
      const string tablesPath = g_settingsService.GetAppSettings().GetStandalone_TablesPath();
      config.tablesPath = tablesPath.empty() ? g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Tables, std::filesystem::path("VPinballX") / "Tables") : std::filesystem::path(tablesPath);
      config.jsonPath = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "tables.json");
#ifdef VPX_TABLE_WEBSERVER
      config.zip = [](const std::filesystem::path& source, const std::filesystem::path& dest, VPinballLib::TableLibrary::ZipProgressCallback callback) { return ZipUtils::Zip(source, dest, callback); };
      config.unzip = [](const std::filesystem::path& source, const std::filesystem::path& dest, VPinballLib::TableLibrary::ZipProgressCallback callback) { return ZipUtils::Extract(source, dest, callback); };
      config.archiveExtensions.clear();
      for (const string& ext : ZipUtils::GetExtractableExtensions()) // .zip and .vpxz, and .rar and .7z when built with libarchive
         config.archiveExtensions.push_back('.' + ext);
#endif
      config.log = [](VPinballLib::TableLibrary::LogLevel level, const string& message)
      {
         switch (level)
         {
         case VPinballLib::TableLibrary::LogLevel::Info: PLOGI << "TableLibrary: " << message; break;
         case VPinballLib::TableLibrary::LogLevel::Warn: PLOGW << "TableLibrary: " << message; break;
         case VPinballLib::TableLibrary::LogLevel::Error: PLOGE << "TableLibrary: " << message; break;
         }
      };
      m_tableLibrary = std::make_unique<VPinballLib::TableLibrary>(std::move(config));
   }
   return *m_tableLibrary;
}

VPinballLib::ScoreStore& Launcher::GetScoreStore()
{
   if (m_scoreStore == nullptr)
   {
      VPinballLib::ScoreStore::Config config;
      config.profilesPath = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "profiles.json");
      config.scoresPath = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "scores.json");
      config.log = [](VPinballLib::ScoreStore::LogLevel level, const string& message)
      {
         switch (level)
         {
         case VPinballLib::ScoreStore::LogLevel::Info: PLOGI << "ScoreStore: " << message; break;
         case VPinballLib::ScoreStore::LogLevel::Warn: PLOGW << "ScoreStore: " << message; break;
         case VPinballLib::ScoreStore::LogLevel::Error: PLOGE << "ScoreStore: " << message; break;
         }
      };
      m_scoreStore = std::make_unique<VPinballLib::ScoreStore>(std::move(config));
   }
   return *m_scoreStore;
}

void Launcher::SetupSharedPinMAMEFolder()
{
   // The plugin looks for a 'pinmame' folder along the table, then for the folder of this setting, then for '~/.pinmame': leave existing setups alone
   const auto propId = GetPinMAMEPathPropId();
   const std::filesystem::path pinmamePath = GetTableLibrary().GetTablesPath() / "pinmame";
   // The default may have been written to the settings file, as settings pages save all the settings: it still is the default
   const string definedPath = g_settingsService.GetAppSettings().GetString(propId);
   const bool isDefault = definedPath.empty() || std::filesystem::path(definedPath).lexically_normal() == pinmamePath.lexically_normal();
   if (!m_isSharedPinMAMEFolderApplied && !isDefault)
      return;
   if (const char* home = getenv("HOME"); definedPath.empty() && home != nullptr && DirExists(std::filesystem::path(home) / ".pinmame" / "roms"))
      return;

   std::error_code ec;
   std::filesystem::create_directories(pinmamePath / "roms", ec);
   if (ec)
   {
      PLOGE << "Failed to create the shared PinMAME folder " << pinmamePath << ": " << ec.message();
      return;
   }
   // This is a default, not a user choice: it is reset when closing (see destructor) to follow the tables folder if it is changed. It may still
   // end up in the settings file if a settings page saves while it is applied, in which case it has to be edited along the tables folder.
   g_settingsService.GetAppSettings().Set(propId, pinmamePath.string(), false);
   if (!m_isSharedPinMAMEFolderApplied)
   {
      PLOGI << "PinMAME folder is not defined, using the one shared by the tables of the table library: " << pinmamePath;
   }
   m_isSharedPinMAMEFolderApplied = true;
}

// The rooms of the lobby, in the order of the room chooser, with their value of Standalone.LobbyRoom. The empty room has no file of its
// own: it is the blank table, emptied and given a floor (see BuildLobby).
struct LobbyRoomFile
{
   int value;
   const char* name;
   const char* table;
   const char* thumbnail; // In assets/lobby-rooms
};
static constexpr LobbyRoomFile LOBBY_ROOMS[] = {
   { 2, "Empty room", nullptr, "empty.jpg" },
   { 0, "Modern room", "lobby.vpx", "modern.jpg" },
   { 1, "Game room", "lobby-gameroom.vpx", "game.jpg" },
};

static const LobbyRoomFile* FindLobbyRoom(int value)
{
   const auto it = std::ranges::find(LOBBY_ROOMS, value, &LobbyRoomFile::value);
   return it != std::end(LOBBY_ROOMS) ? &*it : nullptr;
}

static std::filesystem::path GetLobbyRoomTable(const LobbyRoomFile& room)
{
   return g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, room.table ? room.table : "blankTable.vpx");
}

std::filesystem::path Launcher::GetLobbyTablePath() const
{
   // The chosen room, a table holding only a room baked in Blender (steamframe/lobby-room), or the blank table for the empty room, which is
   // also used when the file of the chosen room is missing
   const LobbyRoomFile* const room = FindLobbyRoom(GetLobbyRoomChoice());
   if (room && room->table && FileExists(GetLobbyRoomTable(*room)))
      return GetLobbyRoomTable(*room);
   return g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "blankTable.vpx");
}

bool Launcher::IsLobbyRoomFile(const std::filesystem::path& tablePath)
{
   return std::ranges::any_of(LOBBY_ROOMS, [&tablePath](const LobbyRoomFile& room) { return room.table && tablePath.filename() == room.table; });
}

vector<Launcher::LobbyRoomChoice> Launcher::GetLobbyRoomChoices()
{
   vector<LobbyRoomChoice> choices;
   for (const LobbyRoomFile& room : LOBBY_ROOMS)
      if (room.table == nullptr || FileExists(GetLobbyRoomTable(room)))
         choices.push_back({ room.value, room.name, g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "lobby-rooms/"s + room.thumbnail) });
   return choices;
}

int Launcher::GetLobbyRoomChoice()
{
   const int value = g_settingsService.GetAppSettings().GetStandalone_LobbyRoom();
   return FindLobbyRoom(value) ? value : 1; // The game room, the default of the setting
}

string Launcher::GetLobbyRoomName()
{
   return FindLobbyRoom(GetLobbyRoomChoice())->name;
}

void Launcher::SetLobbyRoomChoice(int value)
{
   g_settingsService.GetAppSettings().SetStandalone_LobbyRoom(value, false);
   g_settingsService.GetAppSettings().Save();
   // A room chosen explicitly replaces the room of a table
   ResetLobbyRoom();
}

// The parts of the VR room of a table:
// - in a part group using the room space reference,
// - for tables made before part groups existed, in a layer named after the room (like 'VR Room' or 'VR_Room', layers being loaded as part groups),
// - or in a collection named 'VR...' which is not the cabinet: tables often define their rooms as collections that their script shows according
//   to their options (for example VR_MinimalRoom, VR_PoolBar and VR_Sphere, beside VR_Table for the cabinet)
vector<IEditable*> Launcher::GetRoomParts(PinTable* table)
{
   const auto isRoomGroup = [](const IEditable* part)
   {
      for (const PartGroup* group = part->GetPartGroup(); group != nullptr; group = group->GetPartGroup())
         if (group->m_d.m_spaceReference == PartGroupData::SpaceReference::SR_ROOM || lowerCase(group->GetName()).find("room") != string::npos)
            return true;
      return false;
   };
   const auto isRoomCollection = [](const string& collectionName)
   {
      const string name = lowerCase(collectionName);
      return name.starts_with("vr") && !std::ranges::any_of(std::initializer_list<const char*> { "cab", "table", "backglass", "bg" },
         [&name](const char* cabinetWord) { return name.find(cabinetWord) != string::npos; });
   };

   vector<IEditable*> parts;
   for (IEditable* part : table->GetParts())
      if (part->GetItemType() != ItemTypeEnum::eItemPartGroup && isRoomGroup(part))
         parts.push_back(part);
   for (CComObject<Collection>* collection : table->GetCollections())
      if (isRoomCollection(collection->m_name))
         for (IEditable* part : collection->GetParts())
            if (part->GetItemType() != ItemTypeEnum::eItemPartGroup && std::ranges::find(parts, part) == parts.end())
               parts.push_back(part);
   return parts;
}

std::optional<bool> Launcher::GetPartVisible(IEditable* part)
{
   static wchar_t visibleName[] = L"Visible";
   LPOLESTR names = visibleName;
   DISPID dispid;
   IDispatch* const dispatch = part->GetIScriptable() ? part->GetIScriptable()->GetIDispatch() : nullptr;
   if (dispatch == nullptr || FAILED(dispatch->GetIDsOfNames(IID_NULL, &names, 1, 0, &dispid)))
      return std::nullopt;
   DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
   CComVariant result;
   if (FAILED(dispatch->Invoke(dispid, IID_NULL, 0, DISPATCH_PROPERTYGET, &noArgs, &result, nullptr, nullptr)) || FAILED(result.ChangeType(VT_BOOL)))
      return std::nullopt;
   return result.boolVal != VARIANT_FALSE;
}

void Launcher::SetPartVisible(IEditable* part, bool visible)
{
   static wchar_t visibleName[] = L"Visible";
   LPOLESTR names = visibleName;
   DISPID dispid;
   IDispatch* const dispatch = part->GetIScriptable() ? part->GetIScriptable()->GetIDispatch() : nullptr;
   if (dispatch == nullptr || FAILED(dispatch->GetIDsOfNames(IID_NULL, &names, 1, 0, &dispid)))
      return;
   CComVariant value(visible);
   DISPID putId = DISPID_PROPERTYPUT;
   DISPPARAMS args = { &value, &putId, 1, 1 };
   dispatch->Invoke(dispid, IID_NULL, 0, DISPATCH_PROPERTYPUT, &args, nullptr, nullptr, nullptr);
}

std::optional<Launcher::LobbyRoom> Launcher::GetLobbyRoom()
{
   std::ifstream file(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "lobby-room.json"));
   if (!file.is_open())
      return std::nullopt;
   try
   {
      const nlohmann::json json = nlohmann::json::parse(file);
      LobbyRoom room;
      const string path = json.value("table", ""s);
      room.tablePath = std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str()));
      if (room.tablePath.is_relative())
         room.tablePath = GetTableLibrary().GetTablesPath() / room.tablePath;
      room.tableName = json.value("name", ""s);
      room.parts = json.value("parts", vector<string>());
      // The table may have been removed since
      if (room.parts.empty() || !FileExists(room.tablePath))
         return std::nullopt;
      return room;
   }
   catch (const std::exception& e)
   {
      PLOGE << "Failed to read lobby-room.json: " << e.what();
      return std::nullopt;
   }
}

int Launcher::UseTableRoomInLobby(PinTable* table)
{
   // The parts visible now, as the table options (or its script) select which parts of the room are shown
   vector<string> parts;
   for (IEditable* part : GetRoomParts(table))
      if (GetPartVisible(part).value_or(true))
         parts.push_back(part->GetName());
   if (parts.empty())
      return 0;

   // Relative to the tables folder when inside, so that the folder can be moved
   std::filesystem::path path = table->m_filename;
   const std::filesystem::path relative = path.lexically_normal().lexically_relative(GetTableLibrary().GetTablesPath().lexically_normal());
   if (!relative.empty() && *relative.begin() != "..")
      path = relative;
   const std::u8string utf8Path = path.generic_u8string();
   nlohmann::ordered_json json = {
      { "table", string(utf8Path.begin(), utf8Path.end()) },
      { "name", !table->m_tableName.empty() ? table->m_tableName : table->m_filename.stem().string() },
      { "parts", parts },
   };
   std::ofstream file(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "lobby-room.json"), std::ios::trunc);
   file << json.dump(2);
   PLOGI << "Lobby room set to the room of " << table->m_filename << " (" << parts.size() << " parts)";
   return static_cast<int>(parts.size());
}

string Launcher::GetTableImageFocusLabel()
{
   static const char* const labels[] = { "Table image: Backglass", "Table image: Table (from above)", "Table image: Cabinet" };
   return labels[clamp(g_settingsService.GetAppSettings().GetStandalone_TableImageFocus(), 0, 2)];
}

void Launcher::NextTableImageFocus()
{
   g_settingsService.GetAppSettings().SetStandalone_TableImageFocus((clamp(g_settingsService.GetAppSettings().GetStandalone_TableImageFocus(), 0, 2) + 1) % 3, false);
}

string Launcher::GetLoadingText(const std::filesystem::path& tablePath, bool lobby)
{
   if (lobby)
      return "Loading the lobby..."s;
   string name = tablePath.stem().string();
   const std::filesystem::path normalPath = tablePath.lexically_normal();
   const VPinballLib::TableLibrary& library = GetTableLibrary();
   for (const auto& table : library.GetTables())
      if (library.GetFullPath(table).lexically_normal() == normalPath)
      {
         name = table.name;
         break;
      }
   return "Loading " + name + "...";
}

string Launcher::GetNextLoadingText(bool playingLobby)
{
   if (!m_nextTableFilename.empty())
      return GetLoadingText(m_nextTableFilename, false);
   if (m_launcherMode && (!playingLobby || m_reloadLobby))
      return GetLoadingText({}, true);
   return {};
}

void Launcher::ResetLobbyRoom()
{
   std::error_code ec;
   std::filesystem::remove(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "lobby-room.json"), ec);
   PLOGI << "Lobby room reset to the default one";
}

#ifdef VPX_TABLE_WEBSERVER
WebServer& Launcher::GetWebServer()
{
   if (m_webServer == nullptr)
   {
      m_webServer = std::make_unique<WebServer>();
      m_webServer->SetPairingRequired(g_settingsService.GetAppSettings().GetStandalone_WebServerPairing());
      GetScoreStore(); // Created here, as the web server thread uses it
   }
   return *m_webServer;
}

void Launcher::StartWebServerIfAlwaysOn()
{
   if (g_settingsService.GetAppSettings().GetStandalone_WebServerAlwaysOn() && !GetWebServer().IsRunning())
      GetWebServer().Start();
}
#endif


///////////////////////////////////////////////////////////////////////////////
// Items of the home page of the in-game menu

void Launcher::AddHomePageLauncherItems(VPX::InGameUI::InGameUIPage& page)
{
   using namespace VPX::InGameUI;
   // Same as the quit action of the table picker menu: leave the launcher instead of getting back to the lobby
   page.AddItem(std::make_unique<InGameUIItem>("Quit Visual Pinball"s, ""s,
      [&page]()
      {
         g_launcher->m_launcherMode = false;
         page.m_player->SetCloseState(Player::CS_CLOSE_CAPTURE_SCREENSHOT);
      }));

   // Mobile builds select tables in their native launcher
   page.AddItem(std::make_unique<InGameUIItem>("Tables"s, "Play another table"s, "tables/picker"s));
}

void Launcher::AddHomePageTableItems(VPX::InGameUI::InGameUIPage& page)
{
   using namespace VPX::InGameUI;
#ifdef ENABLE_BGFX
   if (page.m_player->CanReplaceTableImage())
   {
      if (page.m_player->m_vrDevice)
         page.AddItem(std::make_unique<InGameUIItem>(GetTableImageFocusLabel(), "What the image shows: the backglass, the playfield seen from above, or the whole cabinet"s,
            [&page]()
            {
               NextTableImageFocus();
               page.RequestRebuild();
            }));
      page.AddItem(std::make_unique<InGameUIItem>("Replace table image"s,
         page.m_player->m_vrDevice ? "Capture a new image of this table for the table picker, framed as chosen above"s : "Use the current view as the image of this table in the table picker"s,
         [&page]() { page.m_player->ReplaceTableImage(); }));
   }
#endif

   // The room of this table as the room of the lobby, from the table picker
   if (g_launcher->m_launcherMode && !page.m_player->m_isLobby && !GetRoomParts(page.m_player->m_ptable).empty())
      page.AddItem(std::make_unique<InGameUIItem>("Use this VR room in the lobby"s, "The table picker will be shown in the room of this table, as it is now"s,
         [&page]()
         {
            const int nParts = g_launcher->UseTableRoomInLobby(page.m_player->m_ptable);
            page.m_player->m_liveUI->PushNotification(nParts > 0 ? "The lobby will use the room of this table"s : "This table has no visible VR room"s, 4000);
         }));
}


///////////////////////////////////////////////////////////////////////////////
// Lobby and launcher mode

// A primitive made of a single horizontal quad (the same way the player builds its implicit playfield)
static Primitive* CreateQuad(PinTable* table, const string& name, float left, float top, float right, float bottom)
{
   Primitive* quad = static_cast<Primitive*>(EditableRegistry::CreateAndInit(ItemTypeEnum::eItemPrimitive, table, 0, 0));
   quad->SetName(name);
   quad->m_desktopBackdrop = false;
   quad->m_d.m_collidable = false;
   quad->m_d.m_toy = true;
   quad->m_d.m_use3DMesh = true;
   quad->m_d.m_vSize.Set(1.0f, 1.0f, 1.0f);
   quad->m_mesh.m_vertices.resize(4);
   for (unsigned int i = 0; i < 4; i++)
   {
      quad->m_mesh.m_vertices[i].x = (i & 1) ? right : left;
      quad->m_mesh.m_vertices[i].y = (i & 2) ? bottom : top;
      quad->m_mesh.m_vertices[i].z = 0.f;
      quad->m_mesh.m_vertices[i].tu = (i & 1) ? 1.f : 0.f;
      quad->m_mesh.m_vertices[i].tv = (i & 2) ? 1.f : 0.f;
      quad->m_mesh.m_vertices[i].nx = 0.f;
      quad->m_mesh.m_vertices[i].ny = 0.f;
      quad->m_mesh.m_vertices[i].nz = 1.f;
   }
   quad->m_mesh.m_indices = { 0, 1, 2, 2, 1, 3 };
   quad->m_mesh.m_validBounds = false;
   return quad;
}

// The lobby is what is displayed behind the table picker of the launcher mode: a lobby room (see LOBBY_ROOMS), a table holding only a
// room made in Blender (see steamframe/lobby-room), whose parts are kept as they are. For the empty room, the lobby is made from the blank
// table shipped with the application, by removing everything it contains (parts, script, playfield) and adding a floor. The room of a
// table can be used instead (see Launcher::GetLobbyRoom): then the lobby is made from that table, keeping only the parts of its room.
static void BuildLobby(PinTable* table, const Launcher::LobbyRoom* room, bool isLobbyRoom)
{
   // Displayed by the home page of the in-game menu
   table->m_tableName = "Visual Pinball"s;
   table->m_blurb.clear();
   table->m_description = "Select 'Tables' to play a table."s;
   table->m_rules.clear();
   table->m_author.clear();
   table->m_version.clear();

   table->m_original_table_script = "Option Explicit\n"s;
   table->m_script_text = table->m_original_table_script;
   for (CComObject<Collection>* collection : table->GetCollections())
      collection->ClearParts();
   if (room)
   {
      // Only the parts of the room which were visible when it was chosen (the script which selects them is gone), along with the part groups
      // which place them in the room
      const vector<IEditable*> parts = table->GetParts();
      for (IEditable* part : parts)
      {
         if (part->GetItemType() == ItemTypeEnum::eItemPartGroup)
            continue;
         if (std::ranges::find(room->parts, part->GetName()) != room->parts.end())
            Launcher::SetPartVisible(part, true);
         else
            table->RemovePart(part);
      }
   }
   else if (!isLobbyRoom)
   {
      // Parts hold a reference on their group, so groups go last
      for (const bool removeGroups : { false, true })
      {
         const vector<IEditable*> parts = table->GetParts();
         for (IEditable* part : parts)
            if ((part->GetItemType() == ItemTypeEnum::eItemPartGroup) == removeGroups)
               table->RemovePart(part);
      }
   }

   // Outside of VR, look at the room like someone standing in front of a table. The legacy layout of the base table can not be used,
   // as it places the camera by fitting it to the parts that were just removed.
   for (ViewSetup& viewSetup : table->mViewSetups)
   {
      viewSetup = ViewSetup();
      viewSetup.mMode = VLM_CAMERA;
      // In percent of the table length: look a few meters ahead instead of down at a playfield, to see the floor up to its far edge, or
      // the far wall of the lobby room at eye level, with a wider view
      viewSetup.mLookAt = isLobbyRoom ? 1000.f : 300.f;
      if (isLobbyRoom)
      {
         // At the height of the playfield, the view of the room is level
         viewSetup.mFOV = 60.f;
         viewSetup.mViewZ = 0.f;
      }
   }

   // An invisible explicit playfield, otherwise the player adds the default one
   Primitive* playfield = CreateQuad(table, "playfield_mesh"s, table->m_left, table->m_top, table->m_right, table->m_bottom);
   playfield->m_d.m_visible = false;
   table->AddPart(playfield);
   playfield->Release();

   // The room of a table, and the lobby room, have their own floor
   if (room || isLobbyRoom)
      return;

   // The floor lies in the room space, which in VR is the real world: z = 0 is the ground, without the playfield inclination
   PartGroup* floorGroup = static_cast<PartGroup*>(EditableRegistry::CreateAndInit(ItemTypeEnum::eItemPartGroup, table, 0, 0));
   floorGroup->SetName("LobbyRoom"s);
   floorGroup->m_d.m_spaceReference = PartGroupData::SpaceReference::SR_ROOM;
   table->AddPart(floorGroup);

   // Two shades, as a uniform floor gives no sense of distance
   Material* floorMaterials[2];
   for (int i = 0; i < 2; i++)
   {
      floorMaterials[i] = new Material();
      floorMaterials[i]->m_name = i == 0 ? "LobbyFloorDark"s : "LobbyFloorLight"s;
      floorMaterials[i]->m_cBase = i == 0 ? RGB(58, 62, 70) : RGB(78, 83, 92);
      floorMaterials[i]->m_fRoughness = 0.8f;
      table->AddMaterial(floorMaterials[i]);
   }

   // 12 x 12 meters centered on the place where the player stands, made of tiles: outside of VR, the near clipping plane is derived
   // from the corners of the bounding box of each part, so a single large quad would be clipped away as its corners are all far away
   constexpr int nTiles = 6;
   const float tileSize = CMTOVPU(200.f);
   const float left = 0.5f * (table->m_left + table->m_right) - 0.5f * static_cast<float>(nTiles) * tileSize;
   const float top = table->m_bottom - 0.5f * static_cast<float>(nTiles) * tileSize;
   for (int y = 0; y < nTiles; y++)
   {
      for (int x = 0; x < nTiles; x++)
      {
         const float tileLeft = left + static_cast<float>(x) * tileSize, tileTop = top + static_cast<float>(y) * tileSize;
         Primitive* tile = CreateQuad(table, "LobbyFloor_"s + std::to_string(x) + '_' + std::to_string(y), tileLeft, tileTop, tileLeft + tileSize, tileTop + tileSize);
         tile->m_d.m_szMaterial = floorMaterials[(x + y) & 1]->m_name;
         tile->m_d.m_staticRendering = true;
         tile->m_d.m_disableLightingBelow = 1.f;
         tile->SetPartGroup(floorGroup);
         table->AddPart(tile);
         tile->Release();
      }
   }
   floorGroup->Release();
}

void Launcher::OnBeforeLoadTable()
{
   SetupSharedPinMAMEFolder();
}

void Launcher::OnTableLoaded(PinTable* table, const std::filesystem::path& tableFilename, bool isLobby)
{
   if (isLobby)
   {
      // The room of a table, if the lobby was loaded from that table (see LauncherCommand)
      const std::optional<LobbyRoom> room = GetLobbyRoom();
      const bool isRoomOfTable = room && room->tablePath == tableFilename;
      BuildLobby(table, isRoomOfTable ? &*room : nullptr, !isRoomOfTable && IsLobbyRoomFile(tableFilename));
   }
   else
      GetTableLibrary().RecordPlay(tableFilename); // For the 'Recent' and 'Most played' lists of the table picker
   // The lobby may be loaded from a table of the library: it must not be taken for that table (image, texture cache)
   m_playingLobby = isLobby;
}

void Launcher::OnPlayerCreated(Player* player, bool isLobby)
{
   VPX::VRUI::OnPlayerCreated(player, isLobby);
   player->m_liveUI->m_inGameUI.AddPage("lobby/rooms"s, []() { return VPX::InGameUI::CreateLobbyRoomsPage(); });
   if (!isLobby)
      return;
   player->m_liveUI->OpenInGameUI("tables/picker"s);
   // Back from a table: the scores of its games and their rank. Without any profile, nothing is asked: the scores stay unassigned, and can
   // be given to a profile later (from the browser, or the 'Who is playing?' page).
   if (m_lastSessionResult)
   {
      VPX::InGameUI::ScoreResultPage::SetResult(*std::exchange(m_lastSessionResult, std::nullopt));
      player->m_liveUI->m_inGameUI.Navigate("scores/result"s);
   }
}

void Launcher::OnTableClosed()
{
   m_playingLobby = false;
}

// The in-game table picker selects the next table to play, then closes the player
void Launcher::PlaySelectedTables()
{
   while (!m_nextTableFilename.empty())
   {
      PlayTableCommand nextTable(std::exchange(m_nextTableFilename, std::filesystem::path()));
      nextTable.Play();
   }
}

LauncherCommand::LauncherCommand()
{
}

void LauncherCommand::Execute()
{
   // Closing a table gets back to the lobby, closing the lobby without selecting a table quits
   g_launcher->m_launcherMode = true;
   // Switching between the lobby and the tables keeps the window and the VR device (see VPApp::m_keepDisplayBetweenTables)
   g_app->m_keepDisplayBetweenTables = true;
   while (g_launcher->m_launcherMode)
   {
      // The room of a table replaces the default lobby room while that table is available
      const std::optional<Launcher::LobbyRoom> room = g_launcher->GetLobbyRoom();
      PlayTableCommand lobby(room ? room->tablePath : g_launcher->GetLobbyTablePath());
      lobby.Play(true);
      if (g_launcher->m_nextTableFilename.empty())
      {
         if (std::exchange(g_launcher->m_reloadLobby, false))
            continue;
         break;
      }
      g_launcher->PlaySelectedTables();
   }
}
