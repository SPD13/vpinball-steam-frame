// license:GPLv3+

#pragma once

#include <filesystem>
#include <string>
#include <vector>

// PinUP Player packs of the tables: which pack a table uses, and where it is.
//
// A pack is a folder named after the game, in a 'pupvideos' folder along the table or in the one of the PinUP Player folder (the
// tables folder's 'pinupsystem', see Launcher::SetupSharedPupFolder), found ignoring case as the PUP plugin does (PUPManager::FindGameDir).
// The name comes from the table script: the pack given to PuPlayer.Init, or the cPuPPack and cGameName variables. A PinMAME game which
// does not use PuP from its script still gets its pack, started by the plugin from the ROM name (cGameName).
namespace PupPacks
{

struct TableUse
{
   bool usesPup = false; // The script creates PinUP Player (CreateObject("PinUpPlayer.PinDisplay"))
   std::vector<std::string> names; // Candidate pack names, by order of preference
   std::string rom; // The ROM of a PinMAME game (cGameName), empty for the other tables
   std::vector<std::string> games; // The other PinMAME game names the script quotes: Controller.Games("..."), cGameName values of other lines
};

TableUse GetTableUse(const std::string& script);

// Script of a table: its .vbs when there is one along it (it replaces the embedded script), otherwise the one of the .vpx file
std::string ReadTableScript(const std::filesystem::path& vpxPath);

// Folder of a directory named 'name', ignoring case, or an empty path
std::filesystem::path FindFolderNoCase(const std::filesystem::path& parent, const std::string& name);

// The pack folder for the candidate names: along the table, then in 'sharedPupVideos'. Empty if none is there.
std::filesystem::path FindPackFolder(const std::vector<std::string>& names, const std::filesystem::path& tableFolder, const std::filesystem::path& sharedPupVideos);

}
