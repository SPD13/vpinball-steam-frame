// license:GPLv3+

#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Options of the PinUP Player packs: the Windows batch files at the root of a pack, which choose its screen layout ("3 Screen - VR.bat",
// "1 - Desktop.bat"...) by copying a set of .pup files from a sub folder of the pack, and writing the chosen layout to a file the table
// script reads (for example 'echo 2 > ScreenType.txt').
//
// They cannot run on the Frame, and are never run: their commands are read, and only a safe subset is done, inside the pack folder only:
// - copy and xcopy of files (wildcards, /s /e for sub folders; the options only matter for their recursion),
// - echo <text> > file (and >>), as cmd writes it (the text up to the redirection, then CR LF),
// - del, erase,
// - rem, @echo off, echo off, cls, pause, exit, empty lines.
// An option with any other command, or a path leaving the pack, is listed as unsupported and is not applied.
//
// The files of the pack that the first applied option overwrites or deletes are saved in its '.options-backup' folder, so that the pack
// can be restored as uploaded. The applied option is remembered in '.options-applied'.
//
// Only depends on the standard library (see tests/test-table-library.cpp).
namespace PupPackOptions
{

struct Option
{
   std::string name; // File name without .bat
   std::filesystem::path script;
   bool supported = false;
   std::string reason; // Why it is not supported
   bool recommended = false; // A VR layout, or when the pack has none, the layout that best suits the VR cabinet (screens kept apart, see List)
};

std::vector<Option> List(const std::filesystem::path& packFolder);
std::string GetApplied(const std::filesystem::path& packFolder);

// Applies the option named 'name'; returns an empty string on success, otherwise what failed
std::string Apply(const std::filesystem::path& packFolder, const std::string& name);
// Puts back the files saved before the first option was applied
std::string Restore(const std::filesystem::path& packFolder);

// For a pack just imported: applies its VR option when it has exactly one recommended option (a VR layout) and none was applied yet.
// Returns the name of the applied option, or an empty string when none was. Failures are reported in 'error'.
std::string ApplyRecommended(const std::filesystem::path& packFolder, std::string& error);

// A folder holding screens.pup, playlists.pup or triggers.pup, or whose options copy them from one of its sub folders
bool IsPack(const std::filesystem::path& folder);

// The sub folders of 'folder' holding .pup files that its options copy from. Some packs have their .pup files in these folders only
// ('austin/PuP-Pack_Options/Option 3 - BG Topper Separate/screens.pup'): the pack is the folder of the batch files, not these.
std::vector<std::filesystem::path> PackSources(const std::filesystem::path& folder);

}
