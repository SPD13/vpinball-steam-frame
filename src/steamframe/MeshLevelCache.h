// license:GPLv3+

#pragma once

// Steam Frame Edition: the simplified levels of a table's meshes (MeshSimplifier) kept on disk, next to its texture cache
// (cache/<table>/simplified-meshes.bin, when Player.CacheMode is on), so that a table only pays the simplification on its first load.
//
// KISS (Stern 2015) 2.0 has a statue of 1.34 M triangles in its VR room: making its levels took 15 s of every load on the Steam Frame
// (docs/VR Rooms on the Steam Frame.md, section 9).
//
// One entry per group of coinciding parts, found by a 128-bit hash (XXH3) of everything its levels are made from: the format, meshoptimizer's
// version, the first part's mesh (vertices and indices), size and placement, the meshes and placements of the layers simplified with it, and
// the finest error kept for it. A change to any of them (the table edited, other settings) makes another entry; the outcomes without levels
// are kept too, so that they are not tried again. The indices are stored with meshoptimizer's index sequence codec, which keeps their exact
// order: a layer draws exactly the triangles of its part. Entries are checked when read (counts, indices within the mesh): a damaged file
// is ignored. The file is written again, with the entries this load used, when the load made new ones.

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace VPX
{

// What the simplifier made for a group: the levels of its first part (indices over its vertices) and of each layer, from finest to coarsest
struct SimplifiedMesh final
{
   enum class Outcome : uint8_t
   {
      NoLevels = 0, // Nothing worth keeping
      LeftOut = 1, // A layer on the part is not made of its triangles
      Levels = 2,
   };
   Outcome outcome = Outcome::NoLevels;
   std::vector<float> errors; // Error of each level, in VP units
   std::vector<std::vector<unsigned int>> levels; // Indices of each level over the part's vertices
   std::vector<std::vector<std::vector<unsigned int>>> layerLevels; // [layer][level]: indices over the layer's vertices
};

struct MeshLevelKey final
{
   uint64_t low = 0, high = 0;
   bool operator==(const MeshLevelKey& other) const { return low == other.low && high == other.high; }
};

class MeshLevelCache final
{
public:
   // Reads the file of the cache folder, if any (an empty folder: no cache)
   void Open(const std::filesystem::path& folder);
   // Forgets the entries
   void Close();
   bool IsOpen() const { return m_open; }

   // The entry of a key, decoded and checked against the vertex counts of the part and of its layers. Thread safe.
   bool Find(const MeshLevelKey& key, size_t partVertices, const std::vector<size_t>& layerVertices, SimplifiedMesh& out);
   // Keeps a new entry. Thread safe.
   void Store(const MeshLevelKey& key, const SimplifiedMesh& mesh);

   // Writes the file when new entries were stored: the entries found or stored since Open. Returns false when nothing was written.
   bool Save(size_t& entries, size_t& bytes);

   int GetFound() const { return m_found; }
   int GetStored() const { return static_cast<int>(m_stored.size()); }

private:
   struct KeyHash
   {
      size_t operator()(const MeshLevelKey& key) const { return static_cast<size_t>(key.low ^ (key.high * 0x9E3779B97F4A7C15ull)); }
   };
   struct Span
   {
      size_t offset = 0, size = 0;
   };

   bool m_open = false;
   std::filesystem::path m_folder;
   std::vector<uint8_t> m_file; // The file as read
   std::unordered_map<MeshLevelKey, Span, KeyHash> m_entries; // Entries of the file, within m_file
   std::mutex m_mutex;
   std::unordered_map<MeshLevelKey, Span, KeyHash> m_used; // Entries of the file found since Open
   std::unordered_map<MeshLevelKey, std::vector<uint8_t>, KeyHash> m_stored; // Entries made since Open, encoded
   int m_found = 0;
};

}
