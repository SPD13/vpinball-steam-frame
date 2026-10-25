// license:GPLv3+

#include "PupPackOptions.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;
using std::string;
using std::vector;

namespace PupPackOptions
{

namespace
{

constexpr const char* BACKUP_FOLDER = ".options-backup";
constexpr const char* APPLIED_FILE = ".options-applied";

string Lower(string s)
{
   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return s;
}

string Trim(const string& s)
{
   const size_t start = s.find_first_not_of(" \t\r\n");
   if (start == string::npos)
      return {};
   const size_t end = s.find_last_not_of(" \t\r\n");
   return s.substr(start, end - start + 1);
}

// Arguments of a command line, the quotes removed ("a b" c -> [a b] [c])
vector<string> SplitArgs(const string& line)
{
   vector<string> args;
   string current;
   bool quoted = false, hasArg = false;
   for (const char c : line)
   {
      if (c == '"')
      {
         quoted = !quoted;
         hasArg = true;
      }
      else if ((c == ' ' || c == '\t') && !quoted)
      {
         if (hasArg)
            args.push_back(current);
         current.clear();
         hasArg = false;
      }
      else
      {
         current += c;
         hasArg = true;
      }
   }
   if (hasArg)
      args.push_back(current);
   return args;
}

// A Windows path relative to the pack, as a relative path with '/', or nothing if it is absolute or leaves the pack
std::optional<fs::path> PackRelative(string path)
{
   std::replace(path.begin(), path.end(), '\\', '/');
   if (path.empty() || path[0] == '/' || (path.size() >= 2 && path[1] == ':'))
      return std::nullopt;
   fs::path relative;
   for (const fs::path& part : fs::path(path))
   {
      if (part == "..")
         return std::nullopt;
      if (part != "." && !part.empty())
         relative /= part;
   }
   return relative;
}

// The entry of 'folder' named 'name' ignoring case (packs are made on Windows), or 'folder / name' if there is none
fs::path ResolveChild(const fs::path& folder, const string& name)
{
   std::error_code ec;
   if (fs::exists(folder / name, ec))
      return folder / name;
   const string lowerName = Lower(name);
   for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
      if (Lower(it->path().filename().string()) == lowerName)
         return it->path();
   return folder / name;
}

fs::path Resolve(const fs::path& pack, const fs::path& relative)
{
   fs::path result = pack;
   for (const fs::path& part : relative)
      result = ResolveChild(result, part.string());
   return result;
}

// Windows wildcards: * and ?, ignoring case
bool Matches(const string& pattern, const string& name)
{
   const string p = Lower(pattern), n = Lower(name);
   size_t pi = 0, ni = 0, star = string::npos, mark = 0;
   while (ni < n.size())
   {
      if (pi < p.size() && (p[pi] == '?' || p[pi] == n[ni]))
      {
         pi++;
         ni++;
      }
      else if (pi < p.size() && p[pi] == '*')
      {
         star = pi++;
         mark = ni;
      }
      else if (star != string::npos)
      {
         pi = star + 1;
         ni = ++mark;
      }
      else
         return false;
   }
   while (pi < p.size() && p[pi] == '*')
      pi++;
   return pi == p.size();
}

enum class StepType { Copy, Write, Delete };
struct Step
{
   StepType type;
   fs::path source; // Copy: folder; Delete: folder
   string pattern; // Copy, Delete: file name pattern
   bool recursive = false; // Copy: /s or /e
   fs::path target; // Copy: destination folder; Write: file
   string text; // Write
   bool append = false; // Write
};

// The steps of a batch file, or the reason it is not supported
std::optional<vector<Step>> Parse(const fs::path& script, string& reason)
{
   std::ifstream file(script, std::ios::binary);
   if (!file)
   {
      reason = "cannot be read";
      return std::nullopt;
   }
   vector<Step> steps;
   string rawLine;
   while (std::getline(file, rawLine))
   {
      string line = Trim(rawLine);
      if (!line.empty() && line[0] == '@')
         line = Trim(line.substr(1));
      const string lower = Lower(line);
      const bool isRemark = (lower.rfind("rem", 0) == 0 && (lower.size() == 3 || lower[3] == ' ')) || lower.rfind("::", 0) == 0;
      if (line.empty() || isRemark || lower == "echo off"
         || lower == "echo on" || lower == "cls" || lower == "pause" || lower == "exit" || lower.rfind("exit ", 0) == 0)
         continue;
      // echo <text> > file
      if (lower.rfind("echo ", 0) == 0)
      {
         const size_t redirect = line.find('>');
         if (redirect == string::npos)
            continue; // Displays a message
         const bool append = redirect + 1 < line.size() && line[redirect + 1] == '>';
         vector<string> target = SplitArgs(line.substr(redirect + (append ? 2 : 1)));
         std::optional<fs::path> path = target.size() == 1 ? PackRelative(target[0]) : std::nullopt;
         if (!path)
         {
            reason = "writes outside of the pack: " + line;
            return std::nullopt;
         }
         Step step { StepType::Write };
         step.text = line.substr(5, redirect - 5); // As cmd: the space before '>' is written too
         step.target = *path;
         step.append = append;
         steps.push_back(step);
         continue;
      }
      vector<string> args = SplitArgs(line);
      const string command = Lower(args[0]);
      vector<string> paths;
      bool recursive = false;
      for (size_t i = 1; i < args.size(); i++)
      {
         if (args[i].size() >= 2 && args[i][0] == '/')
         {
            const string option = Lower(args[i]);
            recursive |= option == "/s" || option == "/e";
         }
         else
            paths.push_back(args[i]);
      }
      if (command == "copy" || command == "xcopy")
      {
         if (paths.empty() || paths.size() > 2 || paths[0].find('+') != string::npos)
         {
            reason = "unsupported copy: " + line;
            return std::nullopt;
         }
         std::optional<fs::path> source = PackRelative(paths[0]);
         std::optional<fs::path> target = paths.size() == 2 ? PackRelative(paths[1]) : fs::path();
         if (!source || !target || source->empty())
         {
            reason = "copies outside of the pack: " + line;
            return std::nullopt;
         }
         Step step { StepType::Copy };
         string name = source->filename().string();
         // A folder copies its files ("xcopy Options\VR" is "xcopy Options\VR\*")
         if (name.find_first_of("*?") == string::npos && fs::is_directory(Resolve(script.parent_path(), *source)))
         {
            step.source = *source;
            step.pattern = "*";
         }
         else
         {
            step.source = source->parent_path();
            step.pattern = name;
         }
         step.recursive = recursive;
         step.target = *target;
         steps.push_back(step);
      }
      else if (command == "del" || command == "erase")
      {
         if (paths.size() != 1)
         {
            reason = "unsupported delete: " + line;
            return std::nullopt;
         }
         std::optional<fs::path> path = PackRelative(paths[0]);
         if (!path || path->empty())
         {
            reason = "deletes outside of the pack: " + line;
            return std::nullopt;
         }
         Step step { StepType::Delete };
         step.source = path->parent_path();
         step.pattern = path->filename().string();
         steps.push_back(step);
      }
      else
      {
         reason = "runs '" + args[0] + "', which is not supported";
         return std::nullopt;
      }
   }
   if (steps.empty())
   {
      reason = "does nothing that can be done on this device";
      return std::nullopt;
   }
   return steps;
}

// Saves a file of the pack before it is changed, the first time only: the backup holds the pack as uploaded
void Backup(const fs::path& pack, const fs::path& file)
{
   std::error_code ec;
   if (!fs::exists(file, ec))
      return;
   const fs::path relative = file.lexically_relative(pack);
   const fs::path saved = pack / BACKUP_FOLDER / relative;
   if (fs::exists(saved, ec) || fs::exists(pack / BACKUP_FOLDER / (relative.string() + ".absent"), ec))
      return;
   fs::create_directories(saved.parent_path(), ec);
   fs::copy_file(file, saved, fs::copy_options::skip_existing, ec);
}

// A file which did not exist before the first option is recorded as absent, so that restoring removes it
void BackupAbsent(const fs::path& pack, const fs::path& file)
{
   std::error_code ec;
   if (fs::exists(file, ec))
      return Backup(pack, file);
   const fs::path relative = file.lexically_relative(pack);
   const fs::path marker = pack / BACKUP_FOLDER / (relative.string() + ".absent");
   if (fs::exists(pack / BACKUP_FOLDER / relative, ec) || fs::exists(marker, ec))
      return;
   fs::create_directories(marker.parent_path(), ec);
   std::ofstream(marker).close();
}

void CopyFiles(const fs::path& pack, const fs::path& from, const string& pattern, const fs::path& to, bool recursive, int& count)
{
   std::error_code ec;
   for (auto it = fs::directory_iterator(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
   {
      const string name = it->path().filename().string();
      if (it->is_directory(ec))
      {
         if (recursive && name != BACKUP_FOLDER)
            CopyFiles(pack, it->path(), pattern, ResolveChild(to, name), recursive, count);
         continue;
      }
      if (!Matches(pattern, name))
         continue;
      fs::create_directories(to, ec);
      const fs::path dest = ResolveChild(to, name);
      BackupAbsent(pack, dest);
      fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing, ec);
      if (!ec)
         count++;
   }
}

bool HasPupFiles(const fs::path& folder)
{
   std::error_code ec;
   for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
   {
      const string name = Lower(it->path().filename().string());
      if (name == "screens.pup" || name == "playlists.pup" || name == "triggers.pup")
         return true;
   }
   return false;
}

bool IsVrName(const string& lower)
{
   return lower.find(" vr") != string::npos || lower.find("vr ") != string::npos || lower == "vr" || lower.find("-vr") != string::npos;
}

// How well a layout without VR in its name suits the VR cabinet, which has a backglass, a DMD and a topper of its own: layouts keeping
// the screens apart score higher, those merging the DMD or the topper into the backglass, dropping a screen, or made for a desktop lower
int CabinetScore(const string& lower)
{
   const auto has = [&](const char* text) { return lower.find(text) != string::npos; };
   int score = 0;
   if (has("separate") || has("seperate") || has("3 screen") || has("3screen") || has("three screen") || has("cabinet") || has("full cab"))
      score += 2;
   if (has("on bg") || has("on backglass") || has("on the backglass") || has("on playfield"))
      score -= 2;
   if (has("no dmd") || has("no topper") || has("no backglass") || has("no bg"))
      score -= 1;
   if (has("desktop") || has("full screen") || has("fullscreen") || has("1 screen") || has("1screen") || has("2 screen") || has("2screen")
      || has("single screen") || has("dual screen") || has("two screen"))
      score -= 2;
   return score;
}

// The fields of a line of a .pup file (comma separated, quotes around the fields holding commas)
vector<string> SplitCsv(const string& line)
{
   vector<string> fields(1);
   bool quoted = false;
   for (const char c : line)
   {
      if (c == '"')
         quoted = !quoted;
      else if (c == ',' && !quoted)
         fields.emplace_back();
      else
         fields.back() += c;
   }
   return fields;
}

// The screens.pup an option leaves at the root of the pack: the last one its copies bring there, otherwise the one already there
fs::path ScreensFileOf(const fs::path& pack, const vector<Step>& steps)
{
   fs::path screens = ResolveChild(pack, "screens.pup");
   std::error_code ec;
   for (const Step& step : steps)
   {
      if (step.type != StepType::Copy || !step.target.empty() || !Matches(step.pattern, "screens.pup"))
         continue;
      const fs::path file = ResolveChild(Resolve(pack, step.source), "screens.pup");
      if (fs::is_regular_file(file, ec))
         screens = file;
   }
   return screens;
}

// The screen a screen is finally drawn in: the end of its chain of CustomPos parents (bounded, as a pack may loop them)
int RootScreen(const vector<Screen>& screens, int number)
{
   for (int depth = 0; depth < 16; depth++)
   {
      const auto it = std::find_if(screens.begin(), screens.end(), [number](const Screen& s) { return s.number == number; });
      if (it == screens.end() || it->parent < 0)
         break;
      number = it->parent;
   }
   return number;
}

// How a layout suits the displays of the VR cabinet: the topper (0), the DMD (1, or 5, the full DMD, which share the DMD surface) and
// the backglass (2). A screen is on the display of the screen its CustomPos is relative to, otherwise on its own number, and is not
// counted when it is off. First, how many displays it uses ("BG Topper FullDMD Separate" 3, "DMD on BG" 1), then how many of them are
// the topper and the backglass: the cabinet's DMD surface stands in front of the table's DMD, so a layout playing its backglass videos
// on the full DMD screen ("3screen - FullDMD" of The Matrix) suits it less than one playing them on the backglass and its topper videos
// on the topper ("4screen - Topper DMD Separate"), although both use two displays.
std::pair<int, int> CabinetDisplayScore(const fs::path& screensFile)
{
   const vector<Screen> screens = ReadScreens(screensFile);
   std::set<int> displays;
   for (const Screen& screen : screens)
   {
      if (!screen.visible)
         continue;
      int display = RootScreen(screens, screen.number);
      if (display == 5)
         display = 1;
      if (display >= 0 && display <= 2)
         displays.insert(display);
   }
   return { static_cast<int>(displays.size()), static_cast<int>(displays.count(0) + displays.count(2)) };
}

}

bool IsPack(const fs::path& folder)
{
   return HasPupFiles(folder) || !PackSources(folder).empty();
}

vector<fs::path> PackSources(const fs::path& folder)
{
   vector<fs::path> sources;
   std::error_code ec;
   for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
   {
      if (!it->is_regular_file(ec) || Lower(it->path().extension().string()) != ".bat")
         continue;
      string reason;
      const std::optional<vector<Step>> steps = Parse(it->path(), reason);
      if (!steps)
         continue;
      for (const Step& step : *steps)
      {
         if (step.type != StepType::Copy || step.source.empty())
            continue;
         const fs::path source = Resolve(folder, step.source);
         if (HasPupFiles(source) && std::find(sources.begin(), sources.end(), source) == sources.end())
            sources.push_back(source);
      }
   }
   return sources;
}

vector<Screen> ReadScreens(const fs::path& screensFile)
{
   vector<Screen> screens;
   std::ifstream file(screensFile, std::ios::binary);
   string line;
   while (std::getline(file, line))
   {
      // ScreenNum,ScreenDes,PlayList,PlayFile,Loopit,Active,Priority,CustomPos
      const vector<string> fields = SplitCsv(Trim(line));
      if (fields.size() < 6 || !std::isdigit(static_cast<unsigned char>(Trim(fields[0]).c_str()[0]))) // The header, and broken lines
         continue;
      Screen screen;
      screen.number = std::atoi(Trim(fields[0]).c_str());
      const string mode = Lower(Trim(fields[5]));
      screen.visible = mode == "show" || mode == "forceon" || mode == "forcepop" || mode == "forceback" || mode == "forcepopback";
      const string customPos = fields.size() > 7 ? Trim(fields[7]) : string();
      if (!customPos.empty() && std::isdigit(static_cast<unsigned char>(customPos[0])) && std::atoi(customPos.c_str()) != screen.number)
         screen.parent = std::atoi(customPos.c_str());
      std::erase_if(screens, [&screen](const Screen& s) { return s.number == screen.number; });
      screens.push_back(screen);
   }
   // Parents named by a CustomPos and not defined: created by the plugin, visible except the playfield (3) and the music (4) screens
   for (size_t i = 0; i < screens.size(); i++)
      if (const int parent = screens[i].parent; parent >= 0 && std::none_of(screens.begin(), screens.end(), [parent](const Screen& s) { return s.number == parent; }))
         screens.push_back({ parent, parent != 3 && parent != 4, -1 });
   return screens;
}

bool DrawsFullDMD(const vector<Screen>& screens)
{
   const auto fullDMD = std::find_if(screens.begin(), screens.end(), [](const Screen& s) { return s.number == 5; });
   if (fullDMD == screens.end())
      return false;
   // A screen 5 drawn inside another screen is only a canvas when screens are positioned on it (see isUsableRoot in PUPManager::Render)
   const bool hasChildren = std::any_of(screens.begin(), screens.end(), [](const Screen& s) { return s.parent == 5; });
   if (fullDMD->parent >= 0 && !hasChildren)
      return false;
   // Something visible in its tree: itself, or a screen positioned on it, directly or through other screens
   return std::any_of(screens.begin(), screens.end(), [&screens](const Screen& s)
   {
      if (!s.visible)
         return false;
      int number = s.number;
      for (int depth = 0; depth < 16 && number >= 0; depth++)
      {
         if (number == 5)
            return true;
         const auto it = std::find_if(screens.begin(), screens.end(), [number](const Screen& p) { return p.number == number; });
         number = it != screens.end() ? it->parent : -1;
      }
      return false;
   });
}

vector<Option> List(const fs::path& packFolder)
{
   vector<Option> options;
   std::error_code ec;
   for (auto it = fs::directory_iterator(packFolder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
   {
      if (!it->is_regular_file(ec) || Lower(it->path().extension().string()) != ".bat")
         continue;
      Option option;
      option.name = it->path().stem().string();
      option.script = it->path();
      string reason;
      option.supported = Parse(it->path(), reason).has_value();
      option.reason = reason;
      option.recommended = option.supported && IsVrName(Lower(option.name));
      options.push_back(option);
   }
   std::sort(options.begin(), options.end(), [](const Option& a, const Option& b) { return Lower(a.name) < Lower(b.name); });
   // Without a VR layout, the one that best suits the VR cabinet: first the one showing its screens on the most displays of the cabinet,
   // then on its topper and backglass, then by its name ("BG Topper Separate" among "DMD and Topper on BG", "DMD on BG No Topper"...). A
   // pack with .pup files of its own already plays: it is only changed for a single best layout made for a cabinet. A pack without,
   // whose .pup files are in its option folders only, plays nothing until a layout is applied: it gets the first of the best ones.
   if (std::none_of(options.begin(), options.end(), [](const Option& o) { return o.recommended; }))
   {
      Option* best = nullptr;
      std::tuple<int, int, int> bestScore;
      int bestCount = 0;
      for (Option& option : options)
      {
         string reason;
         const std::optional<vector<Step>> steps = option.supported ? Parse(option.script, reason) : std::nullopt;
         if (!steps)
            continue;
         const auto [displays, topperAndBackglass] = CabinetDisplayScore(ScreensFileOf(packFolder, *steps));
         const std::tuple<int, int, int> score { displays, topperAndBackglass, CabinetScore(Lower(option.name)) };
         if (!best || score > bestScore)
         {
            best = &option;
            bestScore = score;
            bestCount = 1;
         }
         else if (score == bestScore)
            bestCount++;
      }
      if (best && (!HasPupFiles(packFolder) || (bestCount == 1 && (std::get<0>(bestScore) >= 2 || std::get<2>(bestScore) > 0))))
         best->recommended = true;
   }
   return options;
}

string GetApplied(const fs::path& packFolder)
{
   std::ifstream file(packFolder / APPLIED_FILE);
   string name;
   std::getline(file, name);
   return Trim(name);
}

string Apply(const fs::path& packFolder, const string& name)
{
   const vector<Option> options = List(packFolder);
   const auto option = std::find_if(options.begin(), options.end(), [&](const Option& o) { return o.name == name; });
   if (option == options.end())
      return "No option named '" + name + "'";
   string reason;
   const std::optional<vector<Step>> steps = Parse(option->script, reason);
   if (!steps)
      return "'" + name + "' can not be applied on this device: it " + reason;

   std::error_code ec;
   for (const Step& step : *steps)
   {
      switch (step.type)
      {
      case StepType::Copy:
      {
         const fs::path from = Resolve(packFolder, step.source);
         if (!fs::is_directory(from, ec))
            return "Folder not found in the pack: " + step.source.string();
         int count = 0;
         CopyFiles(packFolder, from, step.pattern, Resolve(packFolder, step.target), step.recursive, count);
         if (count == 0)
            return "No file matches " + (step.source / step.pattern).string();
         break;
      }
      case StepType::Write:
      {
         const fs::path file = Resolve(packFolder, step.target);
         BackupAbsent(packFolder, file);
         std::ofstream out(file, std::ios::binary | (step.append ? std::ios::app : std::ios::trunc));
         out << step.text << "\r\n";
         if (!out)
            return "Failed to write " + step.target.string();
         break;
      }
      case StepType::Delete:
      {
         const fs::path folder = Resolve(packFolder, step.source);
         for (auto it = fs::directory_iterator(folder, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
         {
            if (it->is_regular_file(ec) && Matches(step.pattern, it->path().filename().string()) && Lower(it->path().extension().string()) != ".bat")
            {
               Backup(packFolder, it->path());
               std::error_code removeEc;
               fs::remove(it->path(), removeEc);
            }
         }
         break;
      }
      }
   }
   std::ofstream(packFolder / APPLIED_FILE, std::ios::trunc) << name << '\n';
   return {};
}

string ApplyRecommended(const fs::path& packFolder, string& error)
{
   error.clear();
   if (!GetApplied(packFolder).empty())
      return {};
   const vector<Option> options = List(packFolder);
   if (std::count_if(options.begin(), options.end(), [](const Option& o) { return o.recommended; }) != 1)
      return {};
   const auto option = std::find_if(options.begin(), options.end(), [](const Option& o) { return o.recommended; });
   error = Apply(packFolder, option->name);
   return error.empty() ? option->name : string();
}

string Restore(const fs::path& packFolder)
{
   const fs::path backup = packFolder / BACKUP_FOLDER;
   std::error_code ec;
   if (!fs::is_directory(backup, ec))
      return "No option was applied to this pack";
   for (auto it = fs::recursive_directory_iterator(backup, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
   {
      if (!it->is_regular_file(ec))
         continue;
      fs::path relative = it->path().lexically_relative(backup);
      if (relative.extension() == ".absent")
      {
         relative.replace_extension();
         fs::remove(packFolder / relative, ec);
         ec.clear();
         continue;
      }
      fs::create_directories((packFolder / relative).parent_path(), ec);
      fs::copy_file(it->path(), packFolder / relative, fs::copy_options::overwrite_existing, ec);
      if (ec)
         return "Failed to restore " + relative.string() + ": " + ec.message();
   }
   fs::remove_all(backup, ec);
   fs::remove(packFolder / APPLIED_FILE, ec);
   return {};
}

}
