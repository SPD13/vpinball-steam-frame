// license:GPLv3+

#include "RomAliases.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using std::string;

namespace VPinballLib::RomAliases {

namespace {

constexpr size_t MAX_LINE_LENGTH = 126; // libpinmame reads the file with fgets(line, 128), new line included
constexpr uintmax_t MAX_TEXT_SIZE = 1024 * 1024; // Readme files: larger text files are logs or data, not instructions
constexpr int MAX_DEPTH = 4;
constexpr int MAX_ENTRIES = 20000; // Table folders may hold thousands of media files

string ToLower(string value)
{
   std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return value;
}

bool IsNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

bool IsHidden(const fs::path& path)
{
   const string name = path.filename().string();
   return name.empty() || name[0] == '.' || name == "__MACOSX";
}

bool IsTextFile(const fs::path& path)
{
   const string ext = ToLower(path.extension().string());
   return ext == ".txt" || ext == ".md" || ext == ".nfo" || ext == ".rtf";
}

// The files and folders of a table folder, without entering the packs of PinUP Player ('pupvideos/<pack>' folders are listed, not their
// media), hidden folders, or too deep
void Walk(const fs::path& folder, const std::function<void(const fs::directory_entry& entry)>& visit)
{
   std::error_code ec;
   int entries = 0;
   for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (++entries > MAX_ENTRIES)
         break;
      const bool isDirectory = it->is_directory(ec);
      if (IsHidden(it->path())) {
         if (isDirectory)
            it.disable_recursion_pending();
         continue;
      }
      visit(*it);
      if (isDirectory && (it.depth() >= MAX_DEPTH || ToLower(it->path().parent_path().filename().string()) == "pupvideos"))
         it.disable_recursion_pending();
   }
}

string ReadFile(const fs::path& path)
{
   std::ifstream file(path, std::ios::binary);
   std::stringstream buffer;
   buffer << file.rdbuf();
   return buffer.str();
}

// The game of an 'alias,game' pair of the text naming 'lowerName'
std::optional<string> FindPairInText(const string& text, const string& lowerName, GameCatalog& catalog)
{
   const string lower = ToLower(text);
   for (size_t pos = lower.find(lowerName); pos != string::npos; pos = lower.find(lowerName, pos + 1)) {
      size_t i = pos + lowerName.size();
      if ((pos > 0 && IsNameChar(lower[pos - 1])) || (i < lower.size() && IsNameChar(lower[i])))
         continue;
      while (i < lower.size() && (lower[i] == ' ' || lower[i] == '\t'))
         i++;
      if (i >= lower.size() || lower[i] != ',')
         continue;
      i++;
      while (i < lower.size() && (lower[i] == ' ' || lower[i] == '\t'))
         i++;
      const size_t start = i;
      while (i < lower.size() && IsNameChar(lower[i]))
         i++;
      const string game = lower.substr(start, i - start);
      if (!game.empty() && game != lowerName && catalog.IsGame(game))
         return game;
   }
   return std::nullopt;
}

// The two names of a line of the alias file, split as libpinmame does (CheckGameAlias): the first one ends at a comma or a space, the second
// one at a space, comma, new line, '#', ';' or quote
std::pair<string, string> SplitAliasLine(const string& line)
{
   const auto isFirstDelimiter = [](char c) { return c == ',' || c == ' '; };
   const auto isSecondDelimiter = [](char c) { return c == ' ' || c == ',' || c == '\r' || c == '\n' || c == '#' || c == ';' || c == '\''; };
   size_t i = 0;
   while (i < line.size() && isFirstDelimiter(line[i]))
      i++;
   size_t start = i;
   while (i < line.size() && !isFirstDelimiter(line[i]))
      i++;
   const string first = line.substr(start, i - start);
   while (i < line.size() && isSecondDelimiter(line[i]))
      i++;
   start = i;
   while (i < line.size() && !isSecondDelimiter(line[i]))
      i++;
   return { first, line.substr(start, i - start) };
}

string Trim(const string& value)
{
   const size_t start = value.find_first_not_of(" \t\r");
   if (start == string::npos)
      return string();
   return value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
}

}

///////////////////////////////////////////////////////////////////////////////
// Game catalog

void GameCatalog::Load()
{
   if (m_loaded)
      return;
   m_loaded = true;
   std::ifstream file(m_path);
   if (!file.is_open())
      return;
   try {
      const nlohmann::json json = nlohmann::json::parse(file);
      for (const auto& driver : json.at("drivers"))
         if (const string id = driver.value("id", string()); !id.empty())
            m_games.insert(ToLower(id));
   }
   catch (const std::exception&) {
      m_games.clear();
   }
}

bool GameCatalog::IsAvailable()
{
   std::lock_guard lock(m_mutex);
   Load();
   return !m_games.empty();
}

bool GameCatalog::IsGame(const string& name)
{
   std::lock_guard lock(m_mutex);
   Load();
   return m_games.contains(ToLower(name));
}

string GameCatalog::GetGamePrefix(const string& name)
{
   std::lock_guard lock(m_mutex);
   Load();
   const string lower = ToLower(name);
   for (size_t length = lower.size() - (lower.empty() ? 0 : 1); length >= 3 && length * 2 >= lower.size(); length--)
      if (m_games.contains(lower.substr(0, length)))
         return lower.substr(0, length);
   return string();
}

///////////////////////////////////////////////////////////////////////////////
// Finding the game of an unknown name

std::optional<Evidence> FindPairInTexts(const fs::path& folder, const string& name, GameCatalog& catalog)
{
   const string lowerName = ToLower(name);
   std::optional<Evidence> found;
   std::vector<fs::path> texts;
   Walk(folder, [&](const fs::directory_entry& entry) {
      std::error_code ec;
      if (entry.is_regular_file(ec) && IsTextFile(entry.path()) && entry.file_size(ec) <= MAX_TEXT_SIZE)
         texts.push_back(entry.path());
   });
   std::sort(texts.begin(), texts.end());
   for (const fs::path& text : texts)
      if (const std::optional<string> game = FindPairInText(ReadFile(text), lowerName, catalog))
         return Evidence { *game, text.lexically_relative(folder).generic_string() };
   return std::nullopt;
}

std::optional<Evidence> FindGame(const string& name, const std::vector<string>& scriptGames, const fs::path& tableFolder, const std::map<string, Evidence>& hints,
   GameCatalog& catalog)
{
   const string lowerName = ToLower(name);
   if (const auto hint = hints.find(lowerName); hint != hints.end())
      return hint->second;
   if (std::optional<Evidence> pair = FindPairInTexts(tableFolder, name, catalog))
      return pair;

   for (const string& game : scriptGames)
      if (ToLower(game) != lowerName && catalog.IsGame(game))
         return Evidence { ToLower(game), "the table script" };

   // Games named by the files and folders: ROM archives, NVRAM, colorizations and packs are named after the game
   std::vector<std::pair<string, string>> named; // Game, relative path
   Walk(tableFolder, [&](const fs::directory_entry& entry) {
      std::error_code ec;
      const string stem = ToLower(entry.is_directory(ec) ? entry.path().filename().string() : entry.path().stem().string());
      if (stem != lowerName && catalog.IsGame(stem))
         named.emplace_back(stem, entry.path().lexically_relative(tableFolder).generic_string());
   });
   const std::pair<string, string>* best = nullptr;
   for (const auto& candidate : named)
      if (lowerName.starts_with(candidate.first) && (best == nullptr || candidate.first.size() > best->first.size()))
         best = &candidate;
   if (best == nullptr && !named.empty() && std::all_of(named.begin(), named.end(), [&](const auto& candidate) { return candidate.first == named.front().first; }))
      best = &named.front();
   if (best != nullptr)
      return Evidence { best->first, best->second };

   if (const string prefix = catalog.GetGamePrefix(name); !prefix.empty())
      return Evidence { prefix, "its name" };
   return std::nullopt;
}

///////////////////////////////////////////////////////////////////////////////
// Alias file

std::optional<string> FindAlias(const fs::path& aliasFile, const string& name)
{
   std::ifstream file(aliasFile);
   const string lowerName = ToLower(name);
   for (string line; std::getline(file, line);) {
      if (line.empty() || line[0] == '#')
         continue;
      const auto [alias, game] = SplitAliasLine(line);
      if (ToLower(alias) == lowerName && !game.empty())
         return game;
   }
   return std::nullopt;
}

bool AppendAlias(const fs::path& aliasFile, const string& alias, const string& game, const string& comment, string& error)
{
   std::error_code ec;
   fs::create_directories(aliasFile.parent_path(), ec);
   const string content = fs::exists(aliasFile, ec) ? ReadFile(aliasFile) : string();
   string line = "# " + comment;
   std::replace_if(line.begin(), line.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
   if (line.size() > MAX_LINE_LENGTH - 4)
      line = line.substr(0, MAX_LINE_LENGTH - 7) + "...";
   std::ofstream file(aliasFile, std::ios::app | std::ios::binary);
   if (!content.empty() && content.back() != '\n')
      file << '\n';
   file << line << '\n' << alias << ',' << game << '\n';
   file.close();
   if (!file) {
      error = "Failed to write " + aliasFile.string();
      return false;
   }
   return true;
}

string Normalize(const string& content, GameCatalog& catalog, std::vector<Issue>& errors, std::vector<Issue>& warnings, const string& previous)
{
   std::vector<string> lines;
   std::istringstream input(content);
   for (string line; std::getline(input, line);)
      lines.push_back(Trim(line));
   while (!lines.empty() && lines.back().empty())
      lines.pop_back();
   std::set<string> previousLines;
   std::istringstream previousInput(previous);
   for (string line; std::getline(previousInput, line);)
      previousLines.insert(Trim(line));

   const bool checkGames = catalog.IsAvailable();
   std::map<string, int> aliases; // Lower case alias, line where it is given
   string normalized;
   for (size_t index = 0; index < lines.size(); index++) {
      const string& line = lines[index];
      const int number = static_cast<int>(index) + 1;
      normalized += line + '\n';
      if (line.size() > MAX_LINE_LENGTH) {
         errors.push_back({ number, "The line is longer than " + std::to_string(MAX_LINE_LENGTH) + " characters: PinMAME would read it in parts" });
         continue;
      }
      if (line.empty() || line[0] == '#')
         continue;
      // alias,game, with optional spaces around the comma, then an optional comment
      size_t i = 0;
      while (i < line.size() && IsNameChar(line[i]))
         i++;
      const string alias = line.substr(0, i);
      while (i < line.size() && line[i] == ' ')
         i++;
      const bool hasComma = i < line.size() && line[i] == ',';
      if (hasComma)
         i++;
      while (i < line.size() && line[i] == ' ')
         i++;
      const size_t gameStart = i;
      while (i < line.size() && IsNameChar(line[i]))
         i++;
      const string game = line.substr(gameStart, i - gameStart);
      while (i < line.size() && line[i] == ' ')
         i++;
      if (alias.empty() || !hasComma || game.empty() || (i < line.size() && line[i] != '#')) {
         errors.push_back({ number, "Expected 'alias,game' (letters, digits, '_' and '-'), optionally followed by a # comment" });
         continue;
      }
      const string lowerAlias = ToLower(alias);
      const auto first = aliases.find(lowerAlias);
      if (first == aliases.end())
         aliases[lowerAlias] = number;
      if (previousLines.contains(line))
         continue;
      if (first != aliases.end())
         warnings.push_back({ number, "'" + alias + "' is already given on line " + std::to_string(first->second) + ": PinMAME only uses the first line" });
      if (checkGames && !catalog.IsGame(game))
         warnings.push_back({ number, "'" + game + "' is not a game PinMAME knows" });
      if (checkGames && catalog.IsGame(alias))
         warnings.push_back({ number, "'" + alias + "' is a PinMAME game: the tables asking for it get '" + game + "' instead" });
   }
   return normalized;
}

}
