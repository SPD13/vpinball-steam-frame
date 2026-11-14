// license:GPLv3+

#include "VRRoomOption.h"

#include <regex>
#include <string_view>

namespace VPX::VRRoomOption
{

namespace
{

// The code of a line, without its comment (a quote outside a string starts a comment)
std::string_view CodeOf(std::string_view line, std::string_view* comment = nullptr)
{
   bool inString = false;
   for (size_t i = 0; i < line.size(); ++i)
   {
      if (line[i] == '"')
         inString = !inString;
      else if (line[i] == '\'' && !inString)
      {
         if (comment)
            *comment = line.substr(i + 1);
         return line.substr(0, i);
      }
   }
   if (comment)
      *comment = {};
   return line;
}

bool ReadsRenderingMode(const std::string& script)
{
   static const std::regex renderingMode(R"(\brenderingmode\b)", std::regex::icase);
   size_t start = 0;
   while (start < script.size())
   {
      size_t end = script.find('\n', start);
      if (end == std::string::npos)
         end = script.size();
      const std::string_view code = CodeOf(std::string_view(script).substr(start, end - start));
      if (std::regex_search(code.begin(), code.end(), renderingMode))
         return true;
      start = end + 1;
   }
   return false;
}

}

std::optional<Change> TurnOn(const std::string& script)
{
   if (ReadsRenderingMode(script))
      return std::nullopt;

   // A sub or function starts (not 'Exit Sub', 'End Sub' or a call), or ends
   static const std::regex procStart(R"(^\s*(?:(?:public|private)\s+)?(?:default\s+)?(?:sub|function)\s+\w)", std::regex::icase);
   static const std::regex procEnd(R"(^\s*end\s+(?:sub|function)\b)", std::regex::icase);
   // An assignment of VRRoom or VR_Room: group 1 is the value
   static const std::regex assignment(R"(^\s*(?:const\s+|dim\s+vr_?room\s*:\s*)?vr_?room\s*=\s*(\S+)\s*$)", std::regex::icase);
   // The comment says that 0 is off (or desktop), and names a choice 1
   static const std::regex zeroOff(R"(\b0\s*[-=:]?\s*(?:vr\s*room\s*)?(?:off|desktop)\b)", std::regex::icase);
   static const std::regex oneChoice(R"(\b1\s*[-=:])", std::regex::icase);

   bool inProc = false;
   int lineNumber = 0;
   size_t start = 0;
   while (start < script.size())
   {
      ++lineNumber;
      size_t end = script.find('\n', start);
      if (end == std::string::npos)
         end = script.size();
      std::string_view line = std::string_view(script).substr(start, end - start);
      if (!line.empty() && line.back() == '\r')
         line.remove_suffix(1);
      std::string_view comment;
      const std::string_view code = CodeOf(line, &comment);
      if (inProc)
      {
         if (std::regex_search(code.begin(), code.end(), procEnd))
            inProc = false;
      }
      else if (std::regex_search(code.begin(), code.end(), procStart))
         inProc = !std::regex_search(code.begin(), code.end(), procEnd); // A sub written on one line
      else if (std::match_results<std::string_view::const_iterator> match; std::regex_match(code.begin(), code.end(), match, assignment))
      {
         // The first assignment decides: the option as the author wrote it
         if (match.str(1) != "0" || !std::regex_search(comment.begin(), comment.end(), zeroOff) || !std::regex_search(comment.begin(), comment.end(), oneChoice))
            return std::nullopt;
         Change change;
         change.line = lineNumber;
         change.option = std::string(line);
         change.script = script;
         change.script[start + static_cast<size_t>(match.position(1))] = '1';
         return change;
      }
      start = end + 1;
   }
   return std::nullopt;
}

}
