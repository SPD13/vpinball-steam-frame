// license:GPLv3+

#include "core/stdafx.h"
#include "MeshLevelCache.h"

#include "meshoptimizer/meshoptimizer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <thread>

namespace VPX
{

namespace
{

constexpr char kMagic[8] = { 'V', 'P', 'X', 'M', 'L', 'C', 'A', 'C' };
constexpr uint32_t kFileVersion = 2; // 2: tileLocked
constexpr const char* kFileName = "simplified-meshes.bin";

void Put32(std::vector<uint8_t>& out, uint32_t v)
{
   const size_t at = out.size();
   out.resize(at + 4);
   memcpy(&out[at], &v, 4);
}

void PutFloat(std::vector<uint8_t>& out, float v)
{
   uint32_t u;
   memcpy(&u, &v, 4);
   Put32(out, u);
}

// An index list: count, size of its encoding, encoding (meshoptimizer's index sequence codec, exact order)
void PutIndices(std::vector<uint8_t>& out, const std::vector<unsigned int>& indices)
{
   unsigned int vertices = 0;
   for (const unsigned int i : indices)
      vertices = std::max(vertices, i + 1);
   std::vector<uint8_t> encoded(meshopt_encodeIndexSequenceBound(indices.size(), vertices));
   const size_t size = indices.empty() ? 0 : meshopt_encodeIndexSequence(encoded.data(), encoded.size(), indices.data(), indices.size());
   Put32(out, static_cast<uint32_t>(indices.size()));
   Put32(out, static_cast<uint32_t>(size));
   out.insert(out.end(), encoded.begin(), encoded.begin() + size);
}

// Reads within [data, data + size), failing past the end
struct Reader
{
   const uint8_t* data;
   size_t size, at = 0;
   bool ok = true;

   uint32_t Get32()
   {
      if (!ok || at + 4 > size)
      {
         ok = false;
         return 0;
      }
      uint32_t v;
      memcpy(&v, data + at, 4);
      at += 4;
      return v;
   }
   float GetFloat()
   {
      const uint32_t u = Get32();
      float v;
      memcpy(&v, &u, 4);
      return v;
   }
   bool GetIndices(std::vector<unsigned int>& indices, size_t vertices)
   {
      const uint32_t count = Get32(), encoded = Get32();
      if (!ok || count % 3 != 0 || at + encoded > size || (count > 0) != (encoded > 0))
         return ok = false;
      indices.resize(count);
      if (count > 0 && meshopt_decodeIndexSequence(indices.data(), count, sizeof(unsigned int), data + at, encoded) != 0)
         return ok = false;
      at += encoded;
      for (const unsigned int i : indices)
         if (i >= vertices)
            return ok = false;
      return true;
   }
};

}

void MeshLevelCache::Open(const std::filesystem::path& folder)
{
   Close();
   m_open = !folder.empty();
   m_folder = folder;
   if (!m_open)
      return;
   std::error_code ec;
   const std::filesystem::path path = folder / kFileName;
   if (!std::filesystem::exists(path, ec))
      return;
   std::ifstream in(path, std::ios::binary);
   m_file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
   Reader r { m_file.data(), m_file.size() };
   if (m_file.size() < sizeof(kMagic) || memcmp(m_file.data(), kMagic, sizeof(kMagic)) != 0)
   {
      PLOGW << "Simplified meshes cache: " << path << " is not a cache file, ignored";
      m_file.clear();
      return;
   }
   r.at = sizeof(kMagic);
   if (r.Get32() != kFileVersion)
   {
      m_file.clear(); // Another format: made again
      return;
   }
   const uint32_t count = r.Get32();
   for (uint32_t e = 0; e < count && r.ok; e++)
   {
      MeshLevelKey key;
      key.low = static_cast<uint64_t>(r.Get32()) | (static_cast<uint64_t>(r.Get32()) << 32);
      key.high = static_cast<uint64_t>(r.Get32()) | (static_cast<uint64_t>(r.Get32()) << 32);
      const uint32_t size = r.Get32();
      if (!r.ok || r.at + size > m_file.size())
      {
         r.ok = false;
         break;
      }
      m_entries[key] = Span { r.at, size };
      r.at += size;
   }
   if (!r.ok)
      PLOGW << "Simplified meshes cache: " << path << " is truncated, " << m_entries.size() << " entries read";
}

void MeshLevelCache::Close()
{
   m_open = false;
   m_folder.clear();
   m_file.clear();
   m_file.shrink_to_fit();
   m_entries.clear();
   m_used.clear();
   m_stored.clear();
   m_found = 0;
}

bool MeshLevelCache::Find(const MeshLevelKey& key, size_t partVertices, const std::vector<size_t>& layerVertices, SimplifiedMesh& out)
{
   const auto it = m_entries.find(key);
   if (it == m_entries.end())
      return false;
   Reader r { m_file.data() + it->second.offset, it->second.size };
   out = SimplifiedMesh();
   const uint32_t outcome = r.Get32(), nLevels = r.Get32(), nLayers = r.Get32(), tileLocked = r.Get32();
   if (!r.ok || outcome > static_cast<uint32_t>(SimplifiedMesh::Outcome::Levels) || nLayers != layerVertices.size() || nLevels > 64 || tileLocked > 1)
      return false;
   out.outcome = static_cast<SimplifiedMesh::Outcome>(outcome);
   out.tileLocked = tileLocked != 0;
   out.errors.resize(nLevels);
   out.levels.resize(nLevels);
   for (uint32_t l = 0; l < nLevels && r.ok; l++)
   {
      out.errors[l] = r.GetFloat();
      r.GetIndices(out.levels[l], partVertices);
   }
   out.layerLevels.assign(nLayers, std::vector<std::vector<unsigned int>>(nLevels));
   for (uint32_t layer = 0; layer < nLayers && r.ok; layer++)
      for (uint32_t l = 0; l < nLevels && r.ok; l++)
         r.GetIndices(out.layerLevels[layer][l], layerVertices[layer]);
   if (!r.ok || r.at != r.size)
      return false;
   const std::lock_guard<std::mutex> lock(m_mutex);
   m_used[key] = it->second;
   m_found++;
   return true;
}

void MeshLevelCache::Store(const MeshLevelKey& key, const SimplifiedMesh& mesh)
{
   if (!m_open)
      return;
   std::vector<uint8_t> blob;
   Put32(blob, static_cast<uint32_t>(mesh.outcome));
   Put32(blob, static_cast<uint32_t>(mesh.levels.size()));
   Put32(blob, static_cast<uint32_t>(mesh.layerLevels.size()));
   Put32(blob, mesh.tileLocked ? 1u : 0u);
   for (size_t l = 0; l < mesh.levels.size(); l++)
   {
      PutFloat(blob, mesh.errors[l]);
      PutIndices(blob, mesh.levels[l]);
   }
   for (const auto& layer : mesh.layerLevels)
      for (size_t l = 0; l < mesh.levels.size(); l++)
         PutIndices(blob, l < layer.size() ? layer[l] : std::vector<unsigned int>());
   const std::lock_guard<std::mutex> lock(m_mutex);
   m_stored[key] = std::move(blob);
}

bool MeshLevelCache::Save(size_t& entries, size_t& bytes)
{
   entries = bytes = 0;
   if (!m_open || m_stored.empty())
      return false;
   std::vector<uint8_t> out(kMagic, kMagic + sizeof(kMagic));
   Put32(out, kFileVersion);
   size_t count = m_stored.size();
   for (const auto& [key, span] : m_used)
      count += m_stored.find(key) == m_stored.end() ? 1 : 0;
   Put32(out, static_cast<uint32_t>(count));
   const auto putEntry = [&out](const MeshLevelKey& key, const uint8_t* data, size_t size)
   {
      Put32(out, static_cast<uint32_t>(key.low));
      Put32(out, static_cast<uint32_t>(key.low >> 32));
      Put32(out, static_cast<uint32_t>(key.high));
      Put32(out, static_cast<uint32_t>(key.high >> 32));
      Put32(out, static_cast<uint32_t>(size));
      out.insert(out.end(), data, data + size);
   };
   for (const auto& [key, span] : m_used)
      if (m_stored.find(key) == m_stored.end())
         putEntry(key, m_file.data() + span.offset, span.size);
   for (const auto& [key, blob] : m_stored)
      putEntry(key, blob.data(), blob.size());

   // Written aside, then moved over the old file: another instance may read it meanwhile
   std::error_code ec;
   std::filesystem::create_directories(m_folder, ec);
   const std::filesystem::path path = m_folder / kFileName;
   const std::filesystem::path temp = m_folder / (std::string(kFileName) + '.' + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()) & 0xFFFFFF) + ".tmp");
   {
      std::ofstream file(temp, std::ios::binary | std::ios::trunc);
      file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
      if (!file)
      {
         PLOGW << "Simplified meshes cache: could not write " << temp;
         file.close();
         std::filesystem::remove(temp, ec);
         return false;
      }
   }
   std::filesystem::rename(temp, path, ec);
   if (ec)
   {
      PLOGW << "Simplified meshes cache: could not replace " << path << ": " << ec.message();
      std::filesystem::remove(temp, ec);
      return false;
   }
   entries = count;
   bytes = out.size();
   return true;
}

}
