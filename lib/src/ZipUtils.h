// license:GPLv3+

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class ZipUtils {
public:
   typedef std::function<void(int current, int total, const char* filename)> ProgressCallback;

   static bool Zip(const std::filesystem::path& sourcePath, const std::filesystem::path& destPath, ProgressCallback callback = nullptr);
   static bool Unzip(const std::filesystem::path& sourcePath, const std::filesystem::path& destPath, ProgressCallback callback = nullptr);

   // Any supported archive: .zip and .vpxz, and .rar and .7z when built with libarchive (VPX_ARCHIVE_SUPPORT). Progress of RAR and 7z
   // archives is given in kilobytes read from the archive.
   static bool Extract(const std::filesystem::path& sourcePath, const std::filesystem::path& destPath, ProgressCallback callback = nullptr);
   static bool IsExtractable(const std::filesystem::path& path);
   // The files of an archive Extract supports (UTF-8 names with '/' separators, as they would be extracted: no folders, no
   // __MACOSX entries), without extracting them. False when the archive cannot be read.
   static bool ListFiles(const std::filesystem::path& sourcePath, std::vector<std::string>& files);
   static const std::vector<std::string>& GetExtractableExtensions(); // Lower case, without dot
};
