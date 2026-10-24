// license:GPLv3+

#include "PupPackOptions.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

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

}

bool IsPack(const fs::path& folder)
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
      const string lower = Lower(option.name);
      option.recommended = option.supported && (lower.find(" vr") != string::npos || lower.find("vr ") != string::npos || lower == "vr" || lower.find("-vr") != string::npos);
      options.push_back(option);
   }
   std::sort(options.begin(), options.end(), [](const Option& a, const Option& b) { return Lower(a.name) < Lower(b.name); });
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
