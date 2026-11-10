// license:GPLv3+

#include "core/stdafx.h"
#include "Passthrough.h"

#include "core/VPApp.h"
#include "steamframe/Launcher.h"
#include "parts/Collection.h"
#include "parts/PartGroup.h"
#include "parts/flasher.h"
#include "parts/light.h"
#include "parts/pintable.h"
#include "parts/primitive.h"
#include "parts/ramp.h"
#include "parts/surface.h"

#include <atomic>
#include <mutex>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace VPX::Passthrough
{

// The volume of the cabinet, around the playfield (m_left, m_top, m_right, m_bottom), in VP units (50 per inch): the side rails, the backbox behind
// (its backglass stands up to 900 behind the playfield: James Bond's digits), the lockbar and the buttons in front, the legs below. A primitive entirely
// outside it is a part of the room (vpx-inspect.py survey, for a playfield of 1000 x 2200: -400..1400 x -600..2800, above -900; the back pushed further).
static constexpr float kVolumeSide = 400.f;
static constexpr float kVolumeBack = 1000.f;
static constexpr float kVolumeFront = 600.f;
static constexpr float kVolumeFloor = -900.f; // A part entirely below is on the floor (the playfield is at 0, the floor is about 1700 below)
// A part wider or deeper than this is a part of the room (a cabinet is about 2500 long with its backbox). One higher than this too, unless it
// stands over the cabinet: a cabinet with its legs and backbox is about 3500 high (Addams' TAFLegs), as are the other machines of a game room
static constexpr float kRoomSize = 3000.f;
// Smaller primitives are kept wherever they are: tables park small parts out of the cabinet until their script moves them (ball shadows)
static constexpr float kOutsideMinSize = 200.f;
// A part of a 'VR...' collection is a part of the room when it reaches this far beyond the playfield (as the room left out of the reflections,
// src/steamframe/Reflections.cpp): the collection may also hold parts of the cabinet, like VR_Plunger or the buttons
static constexpr float kCollectionSide = 300.f;
static constexpr float kCollectionBack = 600.f;
static constexpr float kCollectionFront = 700.f;

static std::atomic<bool> s_wanted = false;
static std::atomic<bool> s_known = false;
static std::atomic<bool> s_supported = false;
static int s_preview = -1;

static std::unordered_set<const IEditable*> s_roomParts;

void Load()
{
   s_wanted = g_settingsService.GetAppSettings().GetStandalone_Passthrough();
}

bool IsWanted() { return s_wanted; }

void SetWanted(bool wanted)
{
   s_wanted = wanted;
   Settings& settings = g_settingsService.GetAppSettings();
   settings.SetStandalone_Passthrough(wanted, false);
   settings.Save();
   PLOGI << "Passthrough " << (wanted ? "on" : "off") << (wanted && s_known && !s_supported ? " (not offered by the runtime of the headset)" : "");
}

bool Toggle()
{
   SetWanted(!s_wanted);
   return s_wanted;
}

void SetRuntimeSupport(bool alphaBlend)
{
   s_supported = alphaBlend;
   s_known = true;
}

bool IsKnown() { return s_known; }

bool IsSupported() { return s_supported; }

bool IsActive() { return s_wanted && s_supported; }

bool IsPreview()
{
   if (s_preview < 0)
   {
      const char* const env = std::getenv("VPX_PASSTHROUGH_PREVIEW");
      s_preview = env != nullptr && env[0] != '\0' && env[0] != '0' ? 1 : 0;
   }
   return s_preview == 1;
}

bool IsInRoomGroup(const IEditable* part)
{
   for (const PartGroup* group = part->GetPartGroup(); group != nullptr; group = group->GetPartGroup())
      if (group->m_d.m_spaceReference == PartGroupData::SpaceReference::SR_ROOM || lowerCase(group->GetName()).find("room") != string::npos)
         return true;
   return false;
}

bool IsRoomCollectionName(const string& collectionName)
{
   const string name = lowerCase(collectionName);
   return name.starts_with("vr")
      && !std::ranges::any_of(std::initializer_list<const char*> { "cab", "table", "backglass", "bg" }, [&name](const char* cabinetWord) { return name.find(cabinetWord) != string::npos; });
}

namespace
{

struct Box
{
   Vertex3Ds min { FLT_MAX, FLT_MAX, FLT_MAX };
   Vertex3Ds max { -FLT_MAX, -FLT_MAX, -FLT_MAX };
   bool IsValid() const { return min.x <= max.x; }
   void Add(const Vertex3Ds& v)
   {
      min.x = std::min(min.x, v.x);
      min.y = std::min(min.y, v.y);
      min.z = std::min(min.z, v.z);
      max.x = std::max(max.x, v.x);
      max.y = std::max(max.y, v.y);
      max.z = std::max(max.z, v.z);
   }
   float Size() const { return std::max({ max.x - min.x, max.y - min.y, max.z - min.z }); }
   float Width() const { return std::max(max.x - min.x, max.y - min.y); }
   float Height() const { return max.z - min.z; }
};

// The box of a part in the coordinates of the table (or of its space reference), visible or not: the scripts often show the room parts of the option chosen
Box GetBox(IEditable* part)
{
   Box box;
   switch (part->GetItemType())
   {
   case eItemPrimitive:
   {
      Primitive* const prim = static_cast<Primitive*>(part);
      if (!prim->m_d.m_use3DMesh)
         break; // Built-in shapes are small, made at the size of the playfield parts
      prim->m_mesh.UpdateBounds();
      if (prim->m_mesh.m_minAABound.x == FLT_MAX)
         break;
      const Matrix3D& model = prim->RecalculateMatrices();
      const Vertex3Ds& lo = prim->m_mesh.m_minAABound;
      const Vertex3Ds& hi = prim->m_mesh.m_maxAABound;
      for (int i = 0; i < 8; i++)
         box.Add(model.MultiplyVectorNoPerspective(Vertex3Ds((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z)));
      break;
   }
   case eItemSurface:
   {
      const Surface* const surface = static_cast<Surface*>(part);
      const Vertex2D& lo = surface->m_curve.GetMinBound();
      const Vertex2D& hi = surface->m_curve.GetMaxBound();
      box.Add(Vertex3Ds(lo.x, lo.y, surface->m_d.m_heightbottom));
      box.Add(Vertex3Ds(hi.x, hi.y, surface->m_d.m_heighttop));
      break;
   }
   case eItemRamp:
   {
      const Ramp* const ramp = static_cast<Ramp*>(part);
      const Vertex2D& lo = ramp->m_curve.GetMinBound();
      const Vertex2D& hi = ramp->m_curve.GetMaxBound();
      box.Add(Vertex3Ds(lo.x, lo.y, std::min(ramp->m_d.m_heightbottom, ramp->m_d.m_heighttop)));
      box.Add(Vertex3Ds(hi.x, hi.y, std::max(ramp->m_d.m_heightbottom, ramp->m_d.m_heighttop)));
      break;
   }
   case eItemFlasher:
   {
      // Flashers are turned by their rotations (posters on the walls, backglass images standing up): a cube around their center, as large as
      // their largest side, holds them in any direction
      const Flasher* const flasher = static_cast<Flasher*>(part);
      const Vertex2D& lo = flasher->m_curve.GetMinBound();
      const Vertex2D& hi = flasher->m_curve.GetMaxBound();
      const float half = 0.5f * std::max(hi.x - lo.x, hi.y - lo.y);
      const Vertex3Ds center(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), flasher->m_d.m_height);
      box.Add(Vertex3Ds(center.x - half, center.y - half, center.z - half));
      box.Add(Vertex3Ds(center.x + half, center.y + half, center.z + half));
      break;
   }
   case eItemLight:
   {
      const Light* const light = static_cast<Light*>(part);
      box.Add(Vertex3Ds(light->m_d.m_vCenter.x, light->m_d.m_vCenter.y, light->m_d.m_height));
      break;
   }
   default: break;
   }
   return box;
}

enum class Reason { None, Group, Collection, Size, Outside };

const char* GetReasonName(Reason reason)
{
   switch (reason)
   {
   case Reason::Group: return "room group";
   case Reason::Collection: return "VR collection, beyond the cabinet";
   case Reason::Size: return "larger than a cabinet";
   case Reason::Outside: return "outside the cabinet";
   default: return "";
   }
}

// Where a part is tells little for the flashers, walls and lights: tables park them out of the cabinet (lights of the desktop backglass, helpers
// moved by their scripts), so only their size counts (a flasher or a wall larger than a cabinet is a sky or a floor), and nothing for the lights.
// Only the primitives, the parts rooms are made of, are also taken by where they are.
Reason Classify(IEditable* part, const PinTable& table, bool inRoomCollection)
{
   if (IsInRoomGroup(part))
      return Reason::Group;
   const Box box = GetBox(part);
   if (!box.IsValid() || part->GetItemType() == eItemLight)
      return inRoomCollection ? Reason::Collection : Reason::None;
   // Over the footprint of the cabinet: the playfield, with the margins of the collections (the rails, the backbox, the lockbar)
   const bool overCabinet = box.max.x > table.m_left - kCollectionSide && box.min.x < table.m_right + kCollectionSide && box.max.y > table.m_top - kCollectionBack
      && box.min.y < table.m_bottom + kCollectionFront;
   if (box.Width() > kRoomSize || (box.Height() > kRoomSize && !overCabinet))
      return Reason::Size;
   // Parts placed relative to the feet of the cabinet have their height from the floor
   const bool fromFloor = part->GetPartGroup() && part->GetPartGroup()->GetReferenceSpace() == PartGroupData::SpaceReference::SR_CABINET_FEET;
   if (part->GetItemType() == eItemPrimitive && box.Size() >= kOutsideMinSize
      && (box.max.x < table.m_left - kVolumeSide || box.min.x > table.m_right + kVolumeSide || box.max.y < table.m_top - kVolumeBack
         || box.min.y > table.m_bottom + kVolumeFront || (!fromFloor && !overCabinet && box.max.z < kVolumeFloor))) // The casters of the cabinet are on the floor
      return Reason::Outside;
   if (inRoomCollection
      && (box.min.x < table.m_left - kCollectionSide || box.max.x > table.m_right + kCollectionSide || box.min.y < table.m_top - kCollectionBack
         || box.max.y > table.m_bottom + kCollectionFront))
      return Reason::Collection;
   return Reason::None;
}

// Exceptions per table: { "tables": [ { "files": [ "part of the file name", ... ], "hide": [ "part name", ... ], "keep": [ ... ] } ] }
void LoadExceptions(const std::filesystem::path& path, const string& tableFile, std::unordered_set<string>& hide, std::unordered_set<string>& keep)
{
   std::ifstream file(path);
   if (!file)
      return;
   try
   {
      const nlohmann::json json = nlohmann::json::parse(file);
      const auto tables = json.find("tables");
      if (tables == json.end() || !tables->is_array())
         return;
      const auto addNames = [](const nlohmann::json& entry, const char* key, std::unordered_set<string>& names)
      {
         if (const auto it = entry.find(key); it != entry.end() && it->is_array())
            for (const auto& name : *it)
               if (name.is_string())
                  names.insert(lowerCase(name.get<string>()));
      };
      for (const auto& entry : *tables)
      {
         const auto files = entry.find("files");
         if (files == entry.end() || !files->is_array())
            continue;
         if (std::ranges::any_of(*files, [&tableFile](const nlohmann::json& f) { return f.is_string() && tableFile.find(lowerCase(f.get<string>())) != string::npos; }))
         {
            addNames(entry, "hide", hide);
            addNames(entry, "keep", keep);
         }
      }
   }
   catch (const std::exception& e)
   {
      PLOGE << "Passthrough: " << path << " could not be read: " << e.what();
   }
}

}

void ClassifyTable(PinTable* table)
{
   // The setting is read when the first table starts (the settings are not loaded yet when the application object is made); it is only changed
   // by SetWanted afterwards, which saves it
   static std::once_flag loaded;
   std::call_once(loaded, Load);
   s_roomParts.clear();
   if (table == nullptr)
      return;
   const uint64_t start = usec();

   std::unordered_set<const IEditable*> inRoomCollection;
   for (CComObject<Collection>* collection : table->GetCollections())
      if (IsRoomCollectionName(collection->m_name))
         for (IEditable* part : collection->GetParts())
            inRoomCollection.insert(part);

   std::unordered_set<string> hide, keep;
   const string tableFile = lowerCase(table->m_filename.filename().string());
   LoadExceptions(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "passthrough.json"), tableFile, hide, keep);
   LoadExceptions(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Preferences, "passthrough.json"), tableFile, hide, keep);

   int counts[5] = {};
   int hidden = 0, kept = 0;
   const bool preview = IsPreview();

   // The lobby is a room and nothing else (a room made in Blender, the room of a table, or the floor of the empty room): it all goes, the table
   // picker floats in the player's room
   if (g_launcher && g_launcher->m_playingLobby)
   {
      for (IEditable* part : table->GetParts())
         if (part->GetItemType() != eItemPartGroup)
            s_roomParts.insert(part);
      PLOGI << "Passthrough: lobby, its " << s_roomParts.size() << " parts are the room";
      return;
   }
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || part->m_desktopBackdrop) // Backdrop parts are not drawn in VR
         continue;
      const string name = lowerCase(part->GetName());
      Reason reason = Classify(part, *table, inRoomCollection.contains(part));
      if (reason != Reason::None && keep.contains(name))
      {
         reason = Reason::None;
         kept++;
      }
      else if (reason == Reason::None && hide.contains(name))
      {
         reason = Reason::Outside;
         hidden++;
      }
      if (reason == Reason::None)
         continue;
      s_roomParts.insert(part);
      counts[static_cast<int>(reason)]++;
      if (preview)
      {
         const Box box = GetBox(part);
         PLOGI << "Passthrough: room part '" << part->GetName() << "' (" << GetReasonName(reason) << ") type " << static_cast<int>(part->GetItemType())
               << (part->m_desktopBackdrop ? " backdrop" : "") << " box " << box.min.x << ',' << box.min.y << ',' << box.min.z << " .. " << box.max.x << ','
               << box.max.y << ',' << box.max.z;
      }
   }

   // Primitives drawn together are drawn by the first one (collections grouping their elements: the primitives with a 3D mesh, not static, of the
   // material and image of the first one, see Primitive::RenderSetup): all of them stay unless all of them are parts of the room
   int regrouped = 0;
   for (CComObject<Collection>* collection : table->GetCollections())
   {
      if (!collection->m_groupElements)
         continue;
      vector<const Primitive*> group;
      const Material* groupMaterial = nullptr;
      const Texture* groupImage = nullptr;
      for (IEditable* part : collection->GetParts())
      {
         if (part->GetItemType() != eItemPrimitive)
            continue;
         const Primitive* const prim = static_cast<Primitive*>(part);
         if (!prim->m_d.m_use3DMesh || prim->m_d.m_staticRendering)
            continue;
         const Material* const material = table->GetMaterial(prim->m_d.m_szMaterial);
         const Texture* const image = table->GetImage(prim->m_d.m_szImage);
         if (groupMaterial == nullptr)
            groupMaterial = material;
         if (groupImage == nullptr)
            groupImage = image;
         if (material == groupMaterial && image == groupImage)
            group.push_back(prim);
      }
      if (group.size() <= 1 || std::ranges::all_of(group, [](const Primitive* prim) { return s_roomParts.contains(prim); }))
         continue;
      for (const Primitive* prim : group)
         regrouped += static_cast<int>(s_roomParts.erase(prim));
   }

   PLOGI << "Passthrough: " << s_roomParts.size() << " parts of the room found out of " << table->GetParts().size() << " (room group "
         << counts[static_cast<int>(Reason::Group)] << ", VR collection " << counts[static_cast<int>(Reason::Collection)] << ", size "
         << counts[static_cast<int>(Reason::Size)] << ", outside " << counts[static_cast<int>(Reason::Outside)] << "; exceptions: " << hidden
         << " hidden, " << kept << " kept; " << regrouped << " kept as drawn with parts of the cabinet) in " << (usec() - start) / 1000 << "ms";

   // The part groups are drawn with the visibility mask of mixed reality: one shown in VR but not in mixed reality disappears with the passthrough
   for (const IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemPartGroup)
         continue;
      const unsigned int mask = static_cast<const PartGroup*>(part)->GetPlayerModeVisibilityMask();
      if ((mask & PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY) && !(mask & PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY))
      {
         PLOGI << "Passthrough: part group '" << part->GetName() << "' is shown in VR, not in mixed reality";
      }
   }
}

bool IsRoom(const IEditable* part) { return s_roomParts.contains(part); }

void Clear() { s_roomParts.clear(); }

string GetStatus()
{
   if (s_known && !s_supported)
      return "Not offered by the runtime of this headset"s;
   return s_wanted ? "On: your room around the cabinet"s : "Off: the room of the table"s;
}

}
