// license:GPLv3+

#pragma once

// Steam Frame Edition: images of the tables (thumbnails, playfield and backglass pictures) as ImGui textures, for the pages of the in-game UI
// which show many of them (the cabinet launcher). Decoding takes a few milliseconds, too long for a frame: it runs on worker threads, a few at
// a time, and the decoded images are uploaded to the GPU one per frame at most. The images not displayed for the longest time are released
// beyond a budget, so that long lists do not fill the GPU memory. Same scheme as the thumbnails of TablePickerPage.

#include "imgui/imgui.h"
#include "unordered_dense.h"

#include <filesystem>
#include <future>

class BaseTexture;
class Player;

namespace VPX
{

class TableImageCache final
{
public:
   TableImageCache(Player* player, size_t maxKept);
   ~TableImageCache();

   // The texture of an image file, decoded to at most 'maxSize' pixels on its longest side. 'stamp' tells when the file changed (its modification
   // date): a new stamp decodes it again, the previous image being returned meanwhile. Null until decoded, or when it cannot be decoded.
   ImTextureID Get(const std::filesystem::path& path, int64_t stamp, int maxSize);
   bool IsLoading(const std::filesystem::path& path) const; // Not decoded yet (no previous image either)
   // Size of the decoded image, (0, 0) until decoded
   ImVec2 GetSize(const std::filesystem::path& path) const;

   // Once per frame, before the images are requested: takes a decoded image, and releases the ones beyond the budget
   void Update();

private:
   struct Entry
   {
      std::shared_ptr<BaseTexture> texture;
      int64_t stamp = 0; // Of the decoded image
      bool decoded = false; // Once decoded (or failed to), with 'stamp'
      std::future<std::shared_ptr<BaseTexture>> loading; // Valid while decoding
      int64_t loadingStamp = 0;
      int lastUsedFrame = 0;
   };
   void Release(Entry& entry);

   Player* const m_player;
   const size_t m_maxKept;
   ankerl::unordered_dense::map<string, Entry> m_entries;
   vector<std::future<std::shared_ptr<BaseTexture>>> m_abandonedLoads; // Of images no longer displayed, kept until done not to wait for them
};

}
