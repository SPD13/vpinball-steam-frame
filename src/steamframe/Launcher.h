// license:GPLv3+

#pragma once

// Steam Frame Edition: the application as a launcher, with a table library to pick tables from, a lobby to pick them in, the scores of
// their games, and a web server to manage them from a browser. One instance (g_launcher), owned by VPApp, from its creation to its
// destruction. Built with VPX_STEAM_FRAME only (see steamframe/steamframe.cmake).

#include "core/AppCommands.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace VPinballLib { class TableLibrary; class ScoreStore; }
class WebServer; // Only with VPX_TABLE_WEBSERVER, defined by the macOS, Linux and windows-mingw builds (mobile builds have their own instance)
class PinTable;
class IEditable;
class Player;
namespace VPX::InGameUI { class InGameUIPage; }

class Launcher final
{
public:
   Launcher();
   ~Launcher();

   // Tables managed by the application (in-game table picker), created on first use
   VPinballLib::TableLibrary& GetTableLibrary();

   // Player profiles and the scores of their games (profiles.json, scores.json), created on first use
   VPinballLib::ScoreStore& GetScoreStore();
   // Scores recorded while playing the last table, shown by the lobby when it gets back (see ScoreTracker), then cleared
   struct SessionResult
   {
      string tableUuid;
      vector<string> scoreIds;
   };
   std::optional<SessionResult> m_lastSessionResult;

#ifdef VPX_TABLE_WEBSERVER
   // Web server to manage the table library from a browser. It runs from its activation in the table picker until the application is closed,
   // across the lobby and the tables, and is started with the application when set to be always on (Standalone.WebServerAlwaysOn).
   WebServer& GetWebServer();
   void StartWebServerIfAlwaysOn();
#endif

   // When no PinMAME folder is defined, use a 'pinmame' folder shared by all tables inside the tables folder (created if needed),
   // so that ROMs can be added like tables, for example from a browser. Must be called before the plugins are loaded.
   void SetupSharedPinMAMEFolder();
   bool IsSharedPinMAMEFolderUsed() const { return m_isSharedPinMAMEFolderApplied; }

   // Table to play after the running one is closed, which allows to switch table without leaving the application
   std::filesystem::path m_nextTableFilename;
   // Plays the tables selected by the in-game table picker, one after the other, until one is closed without selecting another
   void PlaySelectedTables();

   // Launcher mode ('-Launcher' command line option): the application starts on a lobby table with the table picker opened,
   // and gets back to it when a table is closed, until the user quits from the lobby or the picker
   bool m_launcherMode = false;
   std::filesystem::path GetLobbyTablePath() const;

   // The VR room of a table can replace the default room of the lobby: the lobby then loads that table, and only keeps the parts of its room
   // which were visible when it was chosen (kept in lobby-room.json). Without that table, the lobby gets back to the default room.
   struct LobbyRoom
   {
      std::filesystem::path tablePath;
      string tableName;
      vector<string> parts;
   };
   std::optional<LobbyRoom> GetLobbyRoom();
   int UseTableRoomInLobby(PinTable* table); // Returns the number of room parts kept, 0 if the table has no VR room (then nothing changes)
   void ResetLobbyRoom();
   static vector<IEditable*> GetRoomParts(PinTable* table); // All the parts of its VR room, visible or not
   // The 'Visible' property that scripts use, for the parts which have one
   static std::optional<bool> GetPartVisible(IEditable* part);
   static void SetPartVisible(IEditable* part, bool visible);
   bool m_playingLobby = false; // Set while the lobby is played

   // What the images of the tables captured in VR show (Standalone.TableImageFocus), for the menus that choose it
   static string GetTableImageFocusLabel();
   static void NextTableImageFocus();
   bool m_reloadLobby = false; // Load the lobby again when it closes, for example to apply a new room

   // Message of the loading screen shown while switching tables (see LiveUI::SetLoadingText): for a table (its name in the library), or for what follows
   // the table being closed (the next table, or the lobby in launcher mode; empty when the application quits)
   string GetLoadingText(const std::filesystem::path& tablePath, bool lobby);
   string GetNextLoadingText(bool playingLobby);

   // Steps of PlayTableCommand::Play: before the table is loaded, once it is loaded (the lobby is built from it), once its player is created
   // (the lobby opens the table picker), and after the player is closed
   void OnBeforeLoadTable();
   void OnTableLoaded(PinTable* table, const std::filesystem::path& tableFilename, bool isLobby);
   void OnPlayerCreated(Player* player, bool isLobby);
   void OnTableClosed();

   // Items of the home page of the in-game menu: at the top (quit the launcher, play another table), and after the table options
   static void AddHomePageLauncherItems(VPX::InGameUI::InGameUIPage& page);
   static void AddHomePageTableItems(VPX::InGameUI::InGameUIPage& page);

private:
   std::unique_ptr<VPinballLib::TableLibrary> m_tableLibrary;
   std::unique_ptr<VPinballLib::ScoreStore> m_scoreStore;
   bool m_isSharedPinMAMEFolderApplied = false;
#ifdef VPX_TABLE_WEBSERVER
   std::unique_ptr<WebServer> m_webServer;
#endif
};

extern Launcher* g_launcher;

// Play the lobby table with the table picker opened, then the tables selected by the user, until the user quits
class LauncherCommand : public AppCommand
{
public:
   LauncherCommand();
   ~LauncherCommand() override = default;
   void Execute() override;
};
