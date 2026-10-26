// license:GPLv3+

#include "core/stdafx.h"
#include "PupPacks.h"

#include "parts/pintable.h"
#include "utils/BiffReader.h"
#include "pole/pole.h"

#include <fstream>

namespace PupPacks
{

namespace
{

// Whether the position is in a comment: after a quote (') which is not inside a string, or on a line starting with 'Rem'
bool IsCommented(const string& script, size_t pos)
{
   const size_t lineStart = script.rfind('\n', pos) == string::npos ? 0 : script.rfind('\n', pos) + 1;
   size_t i = lineStart;
   while (i < pos && (script[i] == ' ' || script[i] == '\t'))
      i++;
   if (pos - i >= 4 && lowerCase(script.substr(i, 4)) == "rem ")
      return true;
   bool inString = false;
   for (; i < pos; i++)
   {
      if (script[i] == '"')
         inString = !inString;
      else if (script[i] == '\'' && !inString)
         return true;
   }
   return false;
}

// The quoted string after 'key' on its line: 'key = "value"' for assignments, 'key x, "value"' for calls taking the name second
void FindQuotedValues(const string& script, const string& lowerScript, const string& lowerKey, bool afterComma, vector<string>& values, vector<bool>* commented = nullptr)
{
   for (size_t pos = lowerScript.find(lowerKey); pos != string::npos; pos = lowerScript.find(lowerKey, pos + 1))
   {
      size_t i = pos + lowerKey.size();
      const size_t lineEnd = std::min(script.find('\n', i), script.size());
      const auto skipBlanks = [&]() { while (i < lineEnd && (script[i] == ' ' || script[i] == '\t')) i++; };
      if (afterComma)
      {
         i = script.find(',', i);
         if (i == string::npos || i >= lineEnd)
            continue;
         i++;
      }
      else
      {
         skipBlanks();
         if (i >= lineEnd || script[i] != '=')
            continue;
         i++;
      }
      skipBlanks();
      if (i >= lineEnd || script[i] != '"')
         continue;
      const size_t end = script.find('"', i + 1);
      if (end == string::npos || end > lineEnd || end == i + 1)
         continue;
      string value = script.substr(i + 1, end - i - 1);
      if (std::find(values.begin(), values.end(), value) == values.end())
      {
         values.push_back(std::move(value));
         if (commented)
            commented->push_back(IsCommented(script, pos));
      }
   }
}

// The strings given to the calls 'lowerCall("value")', like Controller.Games("name")
void FindCallValues(const string& script, const string& lowerScript, const string& lowerCall, vector<string>& values)
{
   for (size_t pos = lowerScript.find(lowerCall); pos != string::npos; pos = lowerScript.find(lowerCall, pos + 1))
   {
      size_t i = pos + lowerCall.size();
      while (i < script.size() && (script[i] == ' ' || script[i] == '\t'))
         i++;
      if (i >= script.size() || script[i] != '"')
         continue;
      const size_t end = script.find('"', i + 1);
      if (end == string::npos || end == i + 1 || script.find('\n', i) < end)
         continue;
      string value = script.substr(i + 1, end - i - 1);
      if (std::find(values.begin(), values.end(), value) == values.end())
         values.push_back(std::move(value));
   }
}

// Name of the Sub or Function whose body holds the position, empty outside of them: the closest line before it starting with 'Sub'/'Function'
// (or 'Private'/'Public' ones), if no 'End Sub'/'End Function' line comes between them
string GetEnclosingProcedure(const string& lowerScript, size_t pos)
{
   size_t lineStart = lowerScript.rfind('\n', pos) == string::npos ? 0 : lowerScript.rfind('\n', pos) + 1;
   while (true)
   {
      size_t i = lineStart;
      while (i < lowerScript.size() && (lowerScript[i] == ' ' || lowerScript[i] == '\t'))
         i++;
      const auto startsWith = [&](const char* word) { return lowerScript.compare(i, strlen(word), word) == 0; };
      if (startsWith("end sub") || startsWith("end function"))
         return {};
      for (const char* prefix : { "private ", "public " })
         if (startsWith(prefix))
            i += strlen(prefix);
      for (const char* keyword : { "sub ", "function " })
         if (startsWith(keyword))
         {
            i += strlen(keyword);
            while (i < lowerScript.size() && (lowerScript[i] == ' ' || lowerScript[i] == '\t'))
               i++;
            size_t end = i;
            while (end < lowerScript.size() && (isalnum(static_cast<unsigned char>(lowerScript[end])) || lowerScript[end] == '_'))
               end++;
            return lowerScript.substr(i, end - i);
         }
      if (lineStart == 0)
         return {};
      const size_t previousEnd = lineStart < 2 ? string::npos : lowerScript.rfind('\n', lineStart - 2);
      lineStart = previousEnd == string::npos ? 0 : previousEnd + 1;
   }
}

// Whether the script starts PinMAME: a call to LoadVPM (the loader of Controller.vbs), or PinMAME's controller created or loaded by the table
// itself. Many original tables paste Controller.vbs in their script for its DOF helpers and only call LoadEM: its procedures quoting
// PinMAME (LoadVPM, LoadVPinMAME, LoadController) do not count, nor the declaration of LoadVPM.
bool StartsPinMame(const string& script, const string& lowerScript)
{
   static const char* const controllerVbsProcedures[] = { "loadvpm", "loadvpmalt", "loadvpinmame", "loadcontroller", "loadproc" };
   for (const char* key : { "loadvpm", "vpinmame.controller", "loadcontroller(\"vpm" })
      for (size_t pos = lowerScript.find(key); pos != string::npos; pos = lowerScript.find(key, pos + 1))
      {
         if (IsCommented(script, pos))
            continue;
         const string procedure = GetEnclosingProcedure(lowerScript, pos);
         if (std::find_if(std::begin(controllerVbsProcedures), std::end(controllerVbsProcedures), [&](const char* name) { return procedure == name; })
            == std::end(controllerVbsProcedures))
            return true;
      }
   return false;
}

}

TableUse GetTableUse(const string& script)
{
   TableUse use;
   const string lowerScript = lowerCase(script);
   use.usesPup = lowerScript.find("pinupplayer") != string::npos;
   FindQuotedValues(script, lowerScript, "puplayer.init"s, true, use.names);
   FindQuotedValues(script, lowerScript, "cpuppack"s, false, use.names);
   std::vector<string> roms;
   std::vector<bool> commented;
   FindQuotedValues(script, lowerScript, "cgamename"s, false, roms, &commented);
   for (const string& rom : roms)
      if (std::find(use.names.begin(), use.names.end(), rom) == use.names.end())
         use.names.push_back(rom);
   // The ROM is the first name which is not commented out ('Const cGameName = "..." lines of other versions are often kept as comments),
   // for the tables starting PinMAME only: original tables name themselves with cGameName too, for their PuP pack or their backglass
   const bool usesPinMame = StartsPinMame(script, lowerScript);
   const auto active = std::find(commented.begin(), commented.end(), false);
   if (usesPinMame && !roms.empty())
      use.rom = active != commented.end() ? roms[active - commented.begin()] : roms.front();
   FindCallValues(script, lowerScript, ".games("s, use.games);
   for (const string& rom : roms)
      if (rom != use.rom && std::find(use.games.begin(), use.games.end(), rom) == use.games.end())
         use.games.push_back(rom);
   return use;
}

string ReadTableScript(const std::filesystem::path& vpxPath)
{
   std::filesystem::path vbsPath = vpxPath;
   vbsPath.replace_extension(".vbs");
   std::error_code ec;
   if (std::filesystem::is_regular_file(vbsPath, ec))
   {
      std::ifstream file(vbsPath, std::ios::binary);
      return string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
   }
   return ReadEmbeddedScript(vpxPath);
}

string ReadEmbeddedScript(const std::filesystem::path& vpxPath)
{
   // Same reading as ExportVBSCommand (core/AppCommands.cpp): only the GameData stream, not the whole table
   string script;
   POLE::Storage rootStorage(POLE::PathToFilename(vpxPath).c_str());
   rootStorage.open();
   if (rootStorage.result() != POLE::Storage::Ok || !rootStorage.exists("GameStg/Version") || !rootStorage.exists("GameStg/GameData"))
      return script;
   int fileVersion = 0;
   POLE::Stream versionStream(&rootStorage, "GameStg/Version");
   versionStream.read(reinterpret_cast<unsigned char*>(&fileVersion), sizeof(int));
   bool isProtected = false;
   POLE::Stream gameStream(&rootStorage, "GameStg/GameData");
   BiffReader reader(&gameStream, fileVersion, nullptr, 0);
   reader.AsObject(
      [&script, &isProtected](int tag, IObjectReader& reader)
      {
         switch (tag)
         {
         case FID(SECB):
         {
            struct ProtectionData
            {
               int32_t fileversion;
               int32_t size;
               uint8_t paraphrase[16 + 8];
               uint32_t flags;
               int32_t keyversion;
               int32_t spare1;
               int32_t spare2;
            } protectionData;
            reader.AsRaw(&protectionData, sizeof(ProtectionData));
            isProtected = ((protectionData.flags & DISABLE_EVERYTHING) == DISABLE_EVERYTHING) || ((protectionData.flags & DISABLE_SCRIPT_EDITING) == DISABLE_SCRIPT_EDITING);
            break;
         }
         case FID(CODE): script = reader.AsScript(isProtected); break;
         }
         return true;
      });
   return script;
}

std::filesystem::path FindFolderNoCase(const std::filesystem::path& parent, const string& name)
{
   std::error_code ec;
   if (name.empty() || !std::filesystem::is_directory(parent, ec))
      return {};
   const string lowerName = lowerCase(name);
   for (const auto& entry : std::filesystem::directory_iterator(parent, ec))
      if (entry.is_directory(ec) && lowerCase(entry.path().filename().string()) == lowerName)
         return entry.path();
   return {};
}

std::filesystem::path FindPackFolder(const vector<string>& names, const std::filesystem::path& tableFolder, const std::filesystem::path& sharedPupVideos)
{
   vector<std::filesystem::path> roots;
   if (std::filesystem::path local = FindFolderNoCase(tableFolder, "pupvideos"s); !local.empty())
      roots.push_back(local);
   if (!sharedPupVideos.empty())
      roots.push_back(sharedPupVideos);
   for (const auto& root : roots)
      for (const string& name : names)
         if (std::filesystem::path folder = FindFolderNoCase(root, name); !folder.empty())
            return folder;
   return {};
}

}
