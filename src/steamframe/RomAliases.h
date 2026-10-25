// license:GPLv3+

#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

// PinMAME ROM aliases of the table library.
//
// A table script asks PinMAME for a game by name (cGameName). Mods often rename their game, 'simpprtyh' for the Simpsons Pinball Party
// 'simpprty', and tell the user to map it in VPinMAME's alias file (VPMAlias.txt on Windows). libpinmame reads the same 'alias,game' lines from
// 'alias.txt' in its PinMAME folder (CheckGameAlias in libpinmame.cpp). Without the alias, PinMAME answers "Game name not found" and the
// table does not start. These helpers find the game a name stands for and read and write that file.
namespace VPinballLib::RomAliases {

// The game names PinMAME knows, from the driver catalog shipped with its plugin (gamedefs/catalog/pinmame.json), loaded on first use
class GameCatalog final
{
public:
   explicit GameCatalog(std::filesystem::path path) : m_path(std::move(path)) { }

   bool IsAvailable();
   bool IsGame(const std::string& name);
   // The longest game name that 'name' starts with, which is at least half of it ('simpprty' for 'simpprtyh'), or an empty string
   std::string GetGamePrefix(const std::string& name);

private:
   void Load();

   const std::filesystem::path m_path;
   std::mutex m_mutex;
   bool m_loaded = false;
   std::set<std::string> m_games; // Lower case
};

// What an unknown game name stands for, and the clue it was found from (for the log and the comment of the alias)
struct Evidence {
   std::string game;
   std::string source;
};

// The game 'name' maps to in an 'alias,game' pair of the text files (readme, instructions...) of 'folder' and its sub folders
std::optional<Evidence> FindPairInTexts(const std::filesystem::path& folder, const std::string& name, GameCatalog& catalog);

// The game a table asking for the unknown 'name' needs, from the clues found along it, in this order:
// - an 'alias,game' pair in 'hints' (found in the bundle the table was imported from) or in the text files of the table folder,
// - the games quoted by the script ('scriptGames', see PupPacks::TableUse::games),
// - the games named by the files and folders of the table folder (ROM archive, 'altcolor/<game>', 'pupvideos/<game>'...) that 'name'
//   starts with, or the only one there is,
// - the game that 'name' starts with.
std::optional<Evidence> FindGame(const std::string& name, const std::vector<std::string>& scriptGames, const std::filesystem::path& tableFolder,
   const std::map<std::string, Evidence>& hints, GameCatalog& catalog);

// The game 'name' is an alias of in the alias file, if any (the first line wins, names compared ignoring case, as libpinmame does)
std::optional<std::string> FindAlias(const std::filesystem::path& aliasFile, const std::string& name);

// Add the line 'alias,game' to the alias file, after a comment line
bool AppendAlias(const std::filesystem::path& aliasFile, const std::string& alias, const std::string& game, const std::string& comment, std::string& error);

// A line of the alias file that libpinmame would misread (error) or that may not do what the user wants (warning), numbered from 1
struct Issue {
   int line;
   std::string message;
};

// The content of an alias file normalized so that libpinmame reads it safely: no blank at the start or end of lines, new lines only, ending
// with a new line. libpinmame reads lines of up to 127 characters and dereferences the second name without checking that there is one,
// so a line it would split or that has a single name is an error. Unknown games and names given twice are warnings, except for the lines
// already in 'previous' (the file being replaced, whose lines the user did not change: VPinMAME's list has some).
std::string Normalize(const std::string& content, GameCatalog& catalog, std::vector<Issue>& errors, std::vector<Issue>& warnings, const std::string& previous = std::string());

}
