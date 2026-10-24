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

// The quoted string after 'key' on its line: 'key = "value"' for assignments, 'key x, "value"' for calls taking the name second
void FindQuotedValues(const string& script, const string& lowerScript, const string& lowerKey, bool afterComma, vector<string>& values)
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
         values.push_back(std::move(value));
   }
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
   FindQuotedValues(script, lowerScript, "cgamename"s, false, roms);
   for (const string& rom : roms)
      if (std::find(use.names.begin(), use.names.end(), rom) == use.names.end())
         use.names.push_back(rom);
   if (!roms.empty())
      use.rom = roms.front();
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
