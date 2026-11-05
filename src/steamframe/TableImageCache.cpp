// license:GPLv3+

#include "core/stdafx.h"
#include "TableImageCache.h"

#include "renderer/Renderer.h"
#include "renderer/Texture.h"

namespace VPX
{

// Each decode keeps a core busy: a few at a time leave the others to the game and rendering threads
static constexpr int MAX_LOADS = 2;

static bool IsReady(const std::future<std::shared_ptr<BaseTexture>>& future) { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

TableImageCache::TableImageCache(Player* player, size_t maxKept)
   : m_player(player)
   , m_maxKept(maxKept)
{
}

TableImageCache::~TableImageCache()
{
   // Loads still running are waited for by their futures (a few milliseconds)
   for (auto& [key, entry] : m_entries)
      if (entry.texture)
         m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(entry.texture.get());
}

ImTextureID TableImageCache::Get(const std::filesystem::path& path, int64_t stamp, int maxSize)
{
   if (path.empty())
      return nullptr;
   Entry& entry = m_entries[path.string()];
   entry.lastUsedFrame = ImGui::GetFrameCount();
   // First use, or the file was replaced: decode it on a worker thread
   if ((!entry.decoded || entry.stamp != stamp) && !entry.loading.valid())
   {
      int loads = static_cast<int>(m_abandonedLoads.size());
      for (const auto& [key, other] : m_entries)
         loads += other.loading.valid() ? 1 : 0;
      if (loads < MAX_LOADS)
      {
         entry.loadingStamp = stamp;
         entry.loading = std::async(std::launch::async, [path, maxSize]() { return BaseTexture::CreateFromFile(path, static_cast<unsigned int>(maxSize)); });
      }
   }
   // Until then, the previous image if it was replaced
   return entry.texture ? m_player->m_renderer->m_renderDevice->m_texMan.LoadTexture(entry.texture.get(), false) : nullptr;
}

bool TableImageCache::IsLoading(const std::filesystem::path& path) const
{
   if (path.empty())
      return false;
   const auto it = m_entries.find(path.string());
   return it == m_entries.end() || (it->second.texture == nullptr && !it->second.decoded);
}

ImVec2 TableImageCache::GetSize(const std::filesystem::path& path) const
{
   const auto it = m_entries.find(path.string());
   if (it == m_entries.end() || it->second.texture == nullptr)
      return ImVec2(0.f, 0.f);
   return ImVec2(static_cast<float>(it->second.texture->width()), static_cast<float>(it->second.texture->height()));
}

void TableImageCache::Update()
{
   std::erase_if(m_abandonedLoads, [](const auto& loading) { return IsReady(loading); });
   // One decoded image per frame at most, as it is uploaded to the GPU when drawn
   for (auto& [key, entry] : m_entries)
   {
      if (entry.loading.valid() && IsReady(entry.loading))
      {
         if (entry.texture)
            m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(entry.texture.get());
         entry.texture = entry.loading.get(); // Null if the image could not be decoded: not tried again until it changes
         entry.stamp = entry.loadingStamp;
         entry.decoded = true;
         break;
      }
   }

   // Beyond the budget, release the images displayed the longest time ago (never the ones of the last frame)
   if (m_entries.size() <= m_maxKept)
      return;
   vector<std::pair<int, string>> byAge;
   byAge.reserve(m_entries.size());
   for (const auto& [key, entry] : m_entries)
      byAge.emplace_back(entry.lastUsedFrame, key);
   std::ranges::sort(byAge);
   const int lastFrame = ImGui::GetFrameCount() - 1;
   for (size_t i = 0; i < byAge.size() - m_maxKept && byAge[i].first < lastFrame; i++)
   {
      const auto it = m_entries.find(byAge[i].second);
      Release(it->second);
      m_entries.erase(it);
   }
}

void TableImageCache::Release(Entry& entry)
{
   if (entry.texture)
      m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(entry.texture.get());
   if (entry.loading.valid())
      m_abandonedLoads.push_back(std::move(entry.loading));
}

}
