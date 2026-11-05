// license:GPLv3+

#pragma once

// Steam Frame Edition: light maps without their dimmest triangles, switched live in the Light maps section of the Device Performance screen
// (Settings Player/LightMapPruning: Off, Low, Medium, High; Medium by default, as in the Low and Medium performance profiles, Off in High). In the headset on
// Medieval Madness (2026-11-04), Medium and High made the game clearly crisper (dynamic resolution holds a higher resolution) for little visible loss.
//
// A baked table (VLM) lights its parts with light maps: additive copies of the geometry each light reaches, textured with what that light adds. They are
// drawn whenever their light is on, and on Medieval Madness they cost about 2.3 ms per frame in play and 5 ms with every light on, half per triangle and half
// per pixel. Most of their triangles add almost nothing: the far edges of the light, the walls it barely reaches. While the table loads, the triangles of each
// light map, of its mesh and of each of its simplified levels, are sorted from the one adding the most light down, so that a level of pruning is only a
// shorter draw: no rebuild when the setting changes. The light of a triangle is the brightest texel within one texel of it in the light map's image (a max
// pyramid, at the level where the triangle spans 64 texels at most), times the brightest channel of the part's color and its opacity: what the light adds at
// its full strength, in linear units. Low leaves out the triangles adding less than 0.02, Medium 0.05, High 0.08. On Medieval Madness that is 43, 56 and 62 %
// of the light-map triangles; the faint glow of the lights on dark walls then ends along triangle edges (screenshots: Mac captures of a game, 2026-11-04).
// The sort decodes the light-map images again while the table loads, a few at a time (4.9 s for the 7 atlases of Medieval Madness on a Mac), so that the
// level can be switched live; with the setting Off when the table loads nothing is sorted, and a level chosen then applies to the next table.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class PinTable;
class Primitive;

namespace VPX
{

class LightMapPruning final
{
public:
   static LightMapPruning& Get();

   static constexpr int kLevels = 3; // Low, Medium, High (the options of the setting after Off)
   static constexpr float kThresholds[kLevels] = { 0.02f, 0.05f, 0.08f };

   // The table is loaded and its simplified levels made, its index buffers not yet: reads the setting and, unless it is Off, sorts the triangles
   // of its light maps (the images decoded again, a few at a time)
   void Prepare(PinTable* table);

   int GetLevel() const { return m_level; }
   void SetLevel(int level); // From the menu, applied at the next frame

   // The index range drawn by a light map (its mesh or one of its levels, from its start): shortened to the triangles of the current level
   void Trim(const Primitive* part, uint32_t first, uint32_t& count) const;

   static const char* GetLevelName(int level); // Off, Low, Medium, High
   size_t GetLightMapCount() const { return m_lightMaps; } // Light maps sorted when the table loaded
   bool WasOffAtLoad() const { return m_offAtLoad; } // Nothing sorted: a level chosen now applies to the next table
   float GetLeftOutShare() const; // Share of their triangles the current level leaves out (0 when Off)
   std::string GetStatus() const; // For the Light maps section of the menu

private:
   struct Counts
   {
      std::array<uint32_t, kLevels> mesh {}; // Indices kept at each level
      std::vector<std::array<uint32_t, kLevels>> levels; // The same for each simplified level
   };
   std::unordered_map<const Primitive*, Counts> m_counts;
   int m_level = 0;
   size_t m_lightMaps = 0;
   std::array<size_t, kLevels + 1> m_triangles {}; // Triangles drawn by the light maps at their finest (levels when they have some), then kept at each level
   double m_prepareMs = 0.;
   bool m_offAtLoad = false; // The setting was Off when the table loaded: nothing sorted
};

}
