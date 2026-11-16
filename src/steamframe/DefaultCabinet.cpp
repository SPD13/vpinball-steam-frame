// license:GPLv3+

#include "core/stdafx.h"
#include "DefaultCabinet.h"

#include "core/editablereg.h"
#include "core/player.h"
#include "core/VPApp.h"
#include "parts/flasher.h"
#include "parts/PartGroup.h"
#include "parts/plunger.h"
#include "parts/primitive.h"
#include "parts/ramp.h"
#include "parts/surface.h"
#include "physics/physconst.h"
#include "math/MeshUtils.h"
#include "renderer/Renderer.h"
#include "renderer/Texture.h"
#include "renderer/VRDevice.h"
#include "steamframe/CabinetBackglass.h"
#include "steamframe/CabinetDisplays.h"
#include "steamframe/Launcher.h"
#include "steamframe/Passthrough.h"
#include "ui/VPXFileFeedback.h"

#include "cgltf/cgltf.h"
#include <array>
#include <nlohmann/json.hpp>

namespace CF = VPX::CabinetFit;
using CF::Point;
using CF::Nominal;
using CF::Fit;

namespace
{

nlohmann::json Extras(const cgltf_extras& extras)
{
   if (extras.data == nullptr)
      return nlohmann::json::object();
   const nlohmann::json json = nlohmann::json::parse(extras.data, nullptr, false);
   return json.is_object() ? json : nlohmann::json::object();
}

Point ReadPoint(const nlohmann::json& json, const char* key, const Point& fallback)
{
   const auto it = json.find(key);
   if (it == json.end() || !it->is_array() || it->size() != 3)
      return fallback;
   return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
}

Nominal ReadNominal(const nlohmann::json& json)
{
   Nominal n;
   const auto get = [&](const char* key, float& value) { value = json.value(key, value); };
   get("playfield_width", n.playfieldWidth);
   get("playfield_length", n.playfieldLength);
   get("slope", n.slope);
   get("lockbar_top", n.lockbarTop);
   get("glass_bottom", n.glassBottom);
   get("half_width", n.halfWidth);
   get("leg_top", n.legTop);
   get("y_front", n.yFront);
   get("z_bottom", n.zBottom);
   get("z_front_top", n.zFrontTop);
   get("y_flat", n.yFlat);
   get("z_back_top", n.zBackTop);
   get("fit_x_keep", n.fitXKeep);
   get("fit_x_side", n.fitXSide);
   get("fit_y_front", n.fitYFront);
   get("fit_y_back", n.fitYBack);
   n.plunger = ReadPoint(json, "plunger", n.plunger);
   return n;
}

// How deep the playfield lies under the top of the lockbar, and how steeply the top edge of the sides rises along it. The tables made
// for desktops set their glass height freely, often far above a real cabinet's, or never (top and bottom equal): the playfield sat deep
// in the cabinet, under sides standing well above the table's own side rails. The cabinet takes the depth of a real one instead, and
// wraps around the long parts the table has along its sides (rails, side blades), its top edge as low as it can be over them: deeper, or
// rising faster toward the back, as they need. Toys standing near a side (a flasher dome in a corner) do not count. The side walls of a
// desktop table, taller than the sides of any cabinet (Metallica's stand 16 cm high along its whole length, for the view on a screen),
// are left to be hidden: the cabinet takes their place. Never deeper than the glass.
struct PlayfieldDepth
{
   float depth; // VPU
   float topAngle = 0.f; // Degrees to the playfield, 0 for the model's
   string raisedBy;
   vector<IEditable*> desktopSides; // To hide
   vector<IEditable*> desktopFront; // To hide too: the lockbar of the desktop table
};

// The box of what a part shows (ramps, walls, primitives), in table coordinates. False for the other parts and the hidden ones.
bool GetShownBounds(IEditable* part, Vertex3Ds& lo, Vertex3Ds& hi)
{
   lo = Vertex3Ds(FLT_MAX, FLT_MAX, FLT_MAX);
   hi = Vertex3Ds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
   const auto add = [&](float x, float y, float z)
   {
      lo.x = min(lo.x, x); lo.y = min(lo.y, y); lo.z = min(lo.z, z);
      hi.x = max(hi.x, x); hi.y = max(hi.y, y); hi.z = max(hi.z, z);
   };
   switch (part->GetItemType())
   {
   case eItemPrimitive:
   {
      Primitive* const primitive = static_cast<Primitive*>(part);
      if (!primitive->m_d.m_visible || primitive == g_pplayer->m_implicitPlayfieldMesh)
         return false;
      const Matrix3D& transform = primitive->RecalculateMatrices();
      for (const Vertex3D_NoTex2& v : primitive->m_mesh.m_vertices)
      {
         const vec3 p = transform * vec3(v.x, v.y, v.z);
         add(p.x, p.y, p.z);
      }
      break;
   }
   case eItemRamp:
   {
      const Ramp* const ramp = static_cast<Ramp*>(part);
      if (!ramp->m_d.m_visible)
         return false;
      const float walls = max(ramp->m_d.m_leftwallheightvisible, ramp->m_d.m_rightwallheightvisible);
      int count = 0;
      float* heights = nullptr;
      const Vertex2D* const edges = ramp->GetRampVertex(count, &heights, nullptr, nullptr, nullptr, HIT_SHAPE_DETAIL_LEVEL, true);
      for (int i = 0; i < count; i++)
      {
         add(edges[i].x, edges[i].y, heights[i] + walls);
         add(edges[count * 2 - 1 - i].x, edges[count * 2 - 1 - i].y, heights[i] + walls);
      }
      delete[] edges;
      delete[] heights;
      break;
   }
   case eItemSurface:
   {
      const Surface* const wall = static_cast<Surface*>(part);
      if (!wall->m_d.m_sideVisible && !wall->m_d.m_topBottomVisible)
         return false;
      vector<RenderVertex> outline;
      wall->m_curve.GetRgVertex(outline);
      for (const RenderVertex& v : outline)
         add(v.x, v.y, wall->m_d.m_heighttop);
      break;
   }
   default: return false;
   }
   return lo.x <= hi.x;
}

PlayfieldDepth GetPlayfieldDepth(PinTable* table, const Nominal& nominal, float k)
{
   const float rise = (nominal.lockbarTop - nominal.zFrontTop) * k; // Lockbar top above the top edge of the sides, at the front
   const float modelAngle = atan2f(nominal.zBackTop - nominal.zFrontTop, nominal.yFlat - nominal.yFront) - ANGTORAD(nominal.slope);
   constexpr float kMaxAngle = ANGTORAD(8.f);
   const float maxDepth = 0.10f * k; // A cabinet deeper than that is not a real one
   const float margin = 0.005f * k;
   const float inside = 0.04f * k, outside = (nominal.halfWidth - 0.5f * nominal.playfieldWidth) * k; // Along the sides, up to the inner face of the cabinet
   const float front = table->m_bottom - nominal.yFront * k; // The front face of the cabinet
   const float length = table->m_bottom - table->m_top;
   const float step = 0.02f * length; // Outlines are followed by steps, their long straight sides having no point between their ends

   // The highest point of the long side parts, by slices of the length from the front
   constexpr int kSlices = 64;
   std::array<float, kSlices> highest;
   std::array<string, kSlices> partOf;
   highest.fill(-FLT_MAX);
   PlayfieldDepth best { nominal.glassBottom * k };
   for (IEditable* part : table->GetParts())
   {
      vector<vec3> points;
      const auto submit = [&](float x, float y, float z)
      {
         if ((x > table->m_left + inside && x < table->m_right - inside) || x < table->m_left - outside || x > table->m_right + outside || y < table->m_top || y > front)
            return;
         points.emplace_back(x, y, z);
      };
      const auto submitSegment = [&](const Vertex2D& a, const Vertex2D& b, float za, float zb)
      {
         const int steps = max(1, static_cast<int>(sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)) / step));
         for (int i = 0; i <= steps; i++)
         {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            submit(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(za, zb, t));
         }
      };
      switch (part->GetItemType())
      {
      case eItemPrimitive:
      {
         Primitive* const primitive = static_cast<Primitive*>(part);
         if (!primitive->m_d.m_visible || primitive == g_pplayer->m_implicitPlayfieldMesh)
            break;
         const Matrix3D& transform = primitive->RecalculateMatrices();
         for (const Vertex3D_NoTex2& v : primitive->m_mesh.m_vertices)
         {
            const vec3 p = transform * vec3(v.x, v.y, v.z);
            submit(p.x, p.y, p.z);
         }
         break;
      }
      case eItemRamp:
      {
         const Ramp* const ramp = static_cast<Ramp*>(part);
         if (!ramp->m_d.m_visible)
            break;
         const float walls = max(ramp->m_d.m_leftwallheightvisible, ramp->m_d.m_rightwallheightvisible);
         int count = 0;
         float* heights = nullptr;
         const Vertex2D* const edges = ramp->GetRampVertex(count, &heights, nullptr, nullptr, nullptr, HIT_SHAPE_DETAIL_LEVEL, true);
         for (int i = 0; i + 1 < count; i++) // Both edges: the second one goes back along the ramp
         {
            submitSegment(edges[i], edges[i + 1], heights[i] + walls, heights[i + 1] + walls);
            submitSegment(edges[count * 2 - 1 - i], edges[count * 2 - 2 - i], heights[i] + walls, heights[i + 1] + walls);
         }
         delete[] edges;
         delete[] heights;
         break;
      }
      case eItemSurface:
      {
         const Surface* const wall = static_cast<Surface*>(part);
         if (!wall->m_d.m_sideVisible && !wall->m_d.m_topBottomVisible)
            break;
         vector<RenderVertex> outline;
         wall->m_curve.GetRgVertex(outline);
         for (size_t i = 0; i < outline.size(); i++)
            submitSegment(outline[i], outline[(i + 1) % outline.size()], wall->m_d.m_heighttop, wall->m_d.m_heighttop);
         break;
      }
      default: break;
      }
      if (points.empty())
         continue;
      const auto [first, last] = std::ranges::minmax(points, {}, [](const vec3& p) { return p.y; });
      if (last.y - first.y < 0.25f * length) // Not along the side
         continue;
      // Taller than a real cabinet could cover, even with its top edge rising as fast as it may: a side wall of the desktop table
      float need = 0.f;
      for (const vec3& p : points)
         need = max(need, p.z - max(0.f, table->m_bottom - p.y) * tanf(kMaxAngle) + rise + margin);
      if (need > maxDepth)
      {
         best.desktopSides.push_back(part);
         continue;
      }
      for (const vec3& p : points)
      {
         const int slice = clamp(static_cast<int>(max(0.f, table->m_bottom - p.y) / length * kSlices), 0, kSlices - 1);
         if (p.z > highest[slice])
         {
            highest[slice] = p.z;
            partOf[slice] = part->GetName();
         }
      }
   }

   // The top edge as low as it can be, on average along the length, from the angle of the model to the steepest
   float bestHeight = FLT_MAX;
   for (float angle = modelAngle; angle <= kMaxAngle + 1e-4f; angle += ANGTORAD(0.1f))
   {
      const float tanTop = tanf(angle);
      PlayfieldDepth candidate { nominal.glassBottom * k, angle == modelAngle ? 0.f : RADTOANG(angle) };
      for (int i = 0; i < kSlices; i++)
      {
         if (highest[i] == -FLT_MAX)
            continue;
         const float need = highest[i] - (static_cast<float>(i) / kSlices * length) * tanTop + rise + margin; // Front of the slice: the edge is lowest there
         if (need > candidate.depth)
         {
            candidate.depth = need;
            candidate.raisedBy = partOf[i];
         }
      }
      const float height = candidate.depth + 0.5f * length * tanTop;
      if (height < bestHeight - 0.5f) // A steeper edge only when it is lower
      {
         bestHeight = height;
         best.depth = candidate.depth;
         best.topAngle = candidate.topAngle;
         best.raisedBy = candidate.raisedBy;
      }
   }
   best.depth = min(best.depth, table->m_glassBottomHeight);

   // The lockbar of a desktop table: a part across the front edge of the playfield, about as wide as the table, standing above the top of
   // the cabinet's lockbar (Black Knight Sword of Rage's, 0.12 m over it, for the view on a screen). The cabinet has its own. The parts
   // further in front, or much higher, belong to a VR room.
   const float frontIn = 0.10f * k, frontOut = 0.15f * k, sideOut = 0.15f * k, tallest = 0.5f * k;
   for (IEditable* part : table->GetParts())
   {
      Vertex3Ds lo, hi;
      if (!GetShownBounds(part, lo, hi) || lo.y < table->m_bottom - frontIn || hi.y > table->m_bottom + frontOut || lo.x < table->m_left - sideOut
         || hi.x > table->m_right + sideOut || hi.x - lo.x < 0.5f * (table->m_right - table->m_left))
         continue;
      if (hi.z > best.depth + margin && hi.z < best.depth + tallest)
         best.desktopFront.push_back(part);
   }
   return best;
}

// Moves of the animated parts, model units (meters)
constexpr float kPlungerTravel = 2.8f * 0.0254f; // Knob, from rest to fully pulled
constexpr float kLaunchButtonTravel = 0.15f * 0.0254f;
constexpr float kFlipperButtonTravel = 0.2f * 0.0254f;
constexpr float kStartButtonTravel = 0.12f * 0.0254f;

}

bool DefaultCabinet::IsEnabled()
{
   const char* env = getenv("VPX_DEFAULT_CABINET");
   return !(env && env[0] == '0');
}

std::unique_ptr<DefaultCabinet> DefaultCabinet::Create(Player* player)
{
   const char* env = getenv("VPX_DEFAULT_CABINET");
   if (!IsEnabled())
      return nullptr;
   const bool forced = env && (env[0] == '1' || env[0] == '2'); // 2: also on the tables made for VR (tests)
   if ((!player->IsVR() && !forced) || player->m_isLobby)
      return nullptr;
   if (Launcher::IsMadeForVR(player->m_ptable) && !(env && env[0] == '2'))
   {
      PLOGI << "[DefaultCabinet] Not added: the table has its own VR room or cabinet";
      return nullptr;
   }
   auto cabinet = std::make_unique<DefaultCabinet>(player);
   bool loaded = false;
   try
   {
      loaded = cabinet->Load(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "vr-cabinet"));
   }
   catch (const std::exception& e) // Unexpected data in the model: the table plays without the cabinet
   {
      PLOGE << "[DefaultCabinet] Model not loaded: " << e.what();
   }
   if (!loaded)
   {
      cabinet->Remove();
      return nullptr;
   }
   // The VR far plane was set from the table alone when it was attached: the room's far walls and the view out of its windows were cut off
   if (player->m_vrDevice)
      player->m_vrDevice->UpdateSceneSize();
   return cabinet;
}

DefaultCabinet::DefaultCabinet(Player* player)
   : m_player(player)
{
}

DefaultCabinet::~DefaultCabinet() = default;

bool DefaultCabinet::Load(const std::filesystem::path& folder)
{
   PinTable* const table = m_player->m_ptable;
   const std::filesystem::path path = folder / "vr-cabinet.glb";
   cgltf_options options {};
   cgltf_data* data = nullptr;
   if (cgltf_parse_file(&options, path.string().c_str(), &data) != cgltf_result_success || cgltf_load_buffers(&options, data, path.string().c_str()) != cgltf_result_success)
   {
      PLOGE << "[DefaultCabinet] Cannot load " << path;
      if (data)
         cgltf_free(data);
      return false;
   }

   // The table, in the units of the model
   m_vpuPerMeter = CMTOVPU(100.f);
   const float k = m_vpuPerMeter;
   m_centerX = 0.5f * (table->m_left + table->m_right);
   m_bottomY = table->m_bottom;
   // The figures of the model: a JSON text in the scene extras
   nlohmann::json figures = nlohmann::json::object();
   if (data->scenes_count)
      if (const nlohmann::json extras = Extras(data->scenes[0].extras); extras.contains("vpx_cabinet") && extras["vpx_cabinet"].is_string())
         figures = nlohmann::json::parse(extras["vpx_cabinet"].get<string>(), nullptr, false);
   if (!figures.is_object())
      figures = nlohmann::json::object();
   const Nominal nominal = ReadNominal(figures);
   CF::Table geometry;
   geometry.playfieldWidth = (table->m_right - table->m_left) / k;
   geometry.playfieldLength = (table->m_bottom - table->m_top) / k;
   geometry.slope = table->GetPlayfieldSlope();
   geometry.lockbarTop = table->m_groundToLockbarHeight / k;
   // The depth of the playfield under the lockbar, for the VR spaces (and the desktop view of the cabinet) too
   const PlayfieldDepth depth = GetPlayfieldDepth(table, nominal, k);
   table->m_cabinetGlassBottomHeight = depth.depth;
   PLOGI << "[DefaultCabinet] Playfield " << depth.depth / k << " m under the lockbar (table glass " << table->m_glassBottomHeight / k << " m), top edge "
         << (depth.topAngle > 0.f ? std::to_string(depth.topAngle) + " degrees to it" : "of the model"s) << (depth.raisedBy.empty() ? ""s : ", for '" + depth.raisedBy + '\'');
   geometry.glassBottom = depth.depth / k;
   geometry.topAngle = depth.topAngle;
   vector<IEditable*> toHide = depth.desktopSides;
   toHide.insert(toHide.end(), depth.desktopFront.begin(), depth.desktopFront.end());
   for (IEditable* part : toHide)
   {
      // Hidden only: they still hold the ball in
      HiddenPart hidden { part };
      if (part->GetItemType() == eItemSurface)
      {
         Surface* const wall = static_cast<Surface*>(part);
         hidden.sideVisible = wall->m_d.m_sideVisible;
         hidden.topVisible = wall->m_d.m_topBottomVisible;
         wall->m_d.m_sideVisible = wall->m_d.m_topBottomVisible = false;
      }
      else if (part->GetItemType() == eItemPrimitive)
         static_cast<Primitive*>(part)->m_d.m_visible = false;
      else if (part->GetItemType() == eItemRamp)
         static_cast<Ramp*>(part)->m_d.m_visible = false;
      m_hiddenParts.push_back(hidden);
      const bool front = std::ranges::find(depth.desktopFront, part) != depth.desktopFront.end();
      PLOGI << "[DefaultCabinet] '" << part->GetName() << "' hidden: " << (front ? "the lockbar of the desktop table, above the cabinet's" : "a side of the desktop table, taller than the cabinet");
   }
   // The art of the walls (Metallica's side blades), cut along the top edge of the sides
   const float topAngle = depth.topAngle > 0.f ? ANGTORAD(depth.topAngle) : atan2f(nominal.zBackTop - nominal.zFrontTop, nominal.yFlat - nominal.yFront) - ANGTORAD(nominal.slope);
   const float sideTop = depth.depth - (nominal.lockbarTop - nominal.zFrontTop) * k - 0.002f * k;
   for (const HiddenPart& hidden : m_hiddenParts)
      if (hidden.part->GetItemType() == eItemSurface && hidden.sideVisible && std::ranges::find(depth.desktopFront, hidden.part) == depth.desktopFront.end())
         AddSideBlade(static_cast<Surface*>(hidden.part), sideTop, tanf(topAngle));
   const Fit fit(nominal, geometry);

   // The launcher, from the plungers of the table
   std::vector<CF::PlungerPart> plungers;
   std::vector<Plunger*> plungerParts;
   for (IEditable* part : table->GetParts())
      if (part->GetItemType() == eItemPlunger)
      {
         Plunger* const plunger = static_cast<Plunger*>(part);
         plungers.push_back({ (plunger->m_d.m_v.x - m_centerX) / k, plunger->m_d.m_autoPlunger });
         plungerParts.push_back(plunger);
      }
   const CF::LauncherChoice choice = CF::ChooseLauncher(plungers);
   m_launcher = choice.launcher;
   for (size_t i = 0; i < plungers.size(); i++)
      if (m_launcher == CF::Launcher::Spring && !plungers[i].autoPlunger && choice.x && plungers[i].x == *choice.x)
         m_plunger = plungerParts[i];
   const float launcherX = CF::LauncherX(choice, fit, nominal);
   // The speaker panel: a 16:9 full DMD screen when the PinUP Player pack draws one, the 4:1 DMD window otherwise
   m_fullDMD = CabinetDisplays::DrawsFullDMD(table);
   PLOGI << "[DefaultCabinet] Speaker panel: " << (m_fullDMD ? "16:9 full DMD screen (PinUP Player pack)" : "DMD window");
   PLOGI << "[DefaultCabinet] Table " << geometry.playfieldWidth << " x " << geometry.playfieldLength << " m, slope " << geometry.slope << ", lockbar "
         << geometry.lockbarTop << " m, glass " << geometry.glassBottom << " m (used: " << fit.GetTable().playfieldWidth << " x " << fit.GetTable().playfieldLength
         << ", " << fit.GetTable().slope << ", " << fit.GetTable().lockbarTop << ", " << fit.GetTable().glassBottom << "); "
         << (m_launcher == CF::Launcher::Spring ? "spring plunger" : "launch button") << " at x " << launcherX << " m (" << plungers.size() << " plunger(s))";

   // Two part groups: the cabinet, and its legs, stretched to the floor
   m_cabinetGroup = NewGroup("SF_DefaultCabinet"s, PartGroupData::SpaceReference::SR_CABINET);
   m_feetGroup = NewGroup("SF_DefaultCabinetFeet"s, PartGroupData::SpaceReference::SR_CABINET_FEET);
   if (m_cabinetGroup == nullptr || m_feetGroup == nullptr)
   {
      cgltf_free(data);
      return false;
   }

   const string folderName = PathToUTF8(folder);
   for (cgltf_size i = 0; i < data->nodes_count; i++)
      LoadNode(data, data->nodes[i], fit, launcherX, folderName);

   // The backglass of the table (B2S) in the window of the backbox, the score view (DMD, segment displays) in the one of the speaker panel.
   // An external render flasher draws black where nothing draws on it: the backglass only when the table has one, the generic art stays
   // otherwise.
   for (cgltf_size i = 0; i < data->nodes_count; i++)
   {
      const nlohmann::json extras = Extras(data->nodes[i].extras);
      const string role = extras.value("vpx_role", ""s);
      const auto corners = extras.find("vpx_corners");
      if (corners == extras.end() || !corners->is_array() || corners->size() != 12 || !IsDisplayChosen(extras.value("vpx_display", ""s)))
         continue;
      const std::vector<float> c = corners->get<std::vector<float>>();
      if (role == "backglass")
      {
         m_backglassWindow = GetWindow(c, fit);
         if (!VPX::Cabinet::FindDirectB2S(table->m_filename).empty())
            CreateFlasher(m_backglassWindow, VPXWindowId::VPXWINDOW_Backglass, "SF_Cab_Backglass"s);
      }
      else if (role == "display")
      {
         m_displayWindow = GetWindow(c, fit);
         m_displayFlasher = CreateFlasher(m_displayWindow, VPXWindowId::VPXWINDOW_ScoreView, "SF_Cab_Display"s);
      }
   }
   cgltf_free(data);
   // The implicit VR backglass would float above the backbox (black without a backglass to show)
   if (m_player->m_implicitVRBackglass && m_player->m_implicitVRBackglass->m_d.m_isVisible)
   {
      m_player->m_implicitVRBackglass->m_d.m_isVisible = false;
      m_hidImplicitBackglass = true;
   }

   // The room around it
   LoadRoom();

   // Images and materials are looked up by name through maps built when the table was loaded
   table->RemoveInvalidReferences();
   PLOGI << "[DefaultCabinet] Added " << m_parts.size() << " parts, " << m_images.size() << " images, " << m_materials.size() << " materials";
   return true;
}

PartGroup* DefaultCabinet::NewGroup(const string& name, PartGroupData::SpaceReference space)
{
   PinTable* const table = m_player->m_ptable;
   IEditable* const part = EditableRegistry::CreateAndInit(eItemPartGroup, table, 0.f, 0.f);
   if (part == nullptr)
      return nullptr;
   PartGroup* const group = static_cast<PartGroup*>(part);
   group->SetName(table->GetUniqueName(name));
   group->m_d.m_spaceReference = space;
   // Out of a headset (desktop tests), shown in every mode
   group->m_d.m_playerModeVisibilityMask = !m_player->IsVR() ? PartGroupData::PMVM_ALL : (PartGroupData::PMVM_VIRTUAL_REALITY | PartGroupData::PMVM_MIXED_REALITY);
   table->AddPart(part);
   part->Release();
   part->SetPartGroup(nullptr);
   m_parts.push_back(part);
   return group;
}

// The room of the lobby (the one chosen for it, the game room by default; none for the empty room) around the cabinet: its parts are
// moved from its table to the played one, in the room space, where the player stands at the middle of the bottom edge of the table in
// both. Baked lighting, drawn unlit. Passthrough leaves them out like the room of a table.
void DefaultCabinet::LoadRoom()
{
   if (const char* env = getenv("VPX_DEFAULT_ROOM"); env && env[0] == '0')
      return;
   const std::filesystem::path path = g_launcher ? g_launcher->GetLobbyTablePath() : g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "lobby-gameroom.vpx");
   if (!Launcher::IsLobbyRoomFile(path) || !FileExists(path))
   {
      PLOGI << "[DefaultCabinet] No room (empty room chosen for the lobby)";
      return;
   }
   CComObject<PinTable>* room = nullptr;
   CComObject<PinTable>::CreateInstance(&room);
   if (room == nullptr)
      return;
   room->AddRef();
   VPXFileFeedback feedback;
   const HRESULT hr = room->LoadGameFromFilename(path, feedback);
   if (FAILED(hr))
   {
      PLOGE << "[DefaultCabinet] Cannot load the room " << path;
      room->Release();
      return;
   }

   PinTable* const table = m_player->m_ptable;
   PartGroup* const group = NewGroup("SF_DefaultRoom"s, PartGroupData::SpaceReference::SR_ROOM);
   const float dx = m_centerX - 0.5f * (room->m_left + room->m_right), dy = m_bottomY - room->m_bottom;
   std::vector<std::pair<string, string>> movedImages; // Name in the room, name in the table
   std::vector<std::pair<string, string>> copiedMaterials;
   int count = 0;
   for (IEditable* part : room->GetParts())
   {
      if (part->GetItemType() != eItemPrimitive || group == nullptr)
         continue;
      const Primitive* const source = static_cast<const Primitive*>(part);
      if (!source->m_d.m_visible) // The marker that keeps the near plane of the desktop view of the lobby close
         continue;
      Primitive* const prim = static_cast<Primitive*>(EditableRegistry::CreateAndInit(eItemPrimitive, table, 0.f, 0.f));
      if (prim == nullptr)
         continue;
      prim->m_d = source->m_d;
      prim->m_mesh = source->m_mesh;
      prim->m_desktopBackdrop = false;
      prim->m_d.m_vPosition.x += dx;
      prim->m_d.m_vPosition.y += dy;
      prim->SetName(table->GetUniqueName("SF_Room_"s + source->GetName()));
      // Its texture leaves the room's table for the played one
      if (!source->m_d.m_szImage.empty())
      {
         const auto moved = std::ranges::find_if(movedImages, [&](const auto& m) { return m.first == source->m_d.m_szImage; });
         if (moved != movedImages.end())
            prim->m_d.m_szImage = moved->second;
         else if (Texture* const image = room->GetImage(source->m_d.m_szImage); image)
         {
            RemoveFromVectorSingle(room->m_vimage, image);
            image->m_name = "SF_Room_" + image->m_name;
            table->m_vimage.push_back(image);
            m_images.push_back(image);
            movedImages.emplace_back(source->m_d.m_szImage, image->m_name);
            prim->m_d.m_szImage = image->m_name;
         }
         else
            prim->m_d.m_szImage.clear();
      }
      if (!source->m_d.m_szMaterial.empty())
      {
         const auto copied = std::ranges::find_if(copiedMaterials, [&](const auto& m) { return m.first == source->m_d.m_szMaterial; });
         if (copied != copiedMaterials.end())
            prim->m_d.m_szMaterial = copied->second;
         else if (const Material* const material = room->GetMaterial(source->m_d.m_szMaterial); material && material != room->GetMaterial(string()))
         {
            Material* const copy = new Material(*material);
            copy->m_name = "SF_Room_" + material->m_name;
            table->AddMaterial(copy);
            m_materials.push_back(copy);
            copiedMaterials.emplace_back(source->m_d.m_szMaterial, copy->m_name);
            prim->m_d.m_szMaterial = copy->m_name;
         }
         else
            prim->m_d.m_szMaterial.clear();
      }
      table->AddPart(prim);
      prim->Release();
      prim->SetPartGroup(group);
      m_parts.push_back(prim);
      VPX::Passthrough::AddRoomPart(prim);
      count++;
   }
   room->Release();
   PLOGI << "[DefaultCabinet] Room " << PathToUTF8(path.filename()) << ": " << count << " parts, " << movedImages.size() << " images";
}

Material* DefaultCabinet::GetMaterial(const cgltf_data* data, const cgltf_material* material, const std::string& folder, std::string& image, float& unlit)
{
   image.clear();
   unlit = 0.f;
   if (material == nullptr)
      return nullptr;
   const nlohmann::json extras = Extras(material->extras);
   unlit = extras.value("vpx_unlit", 0.f);
   const auto known = std::ranges::find_if(m_materialMap, [&](const auto& entry) { return entry.first == material; });
   if (known != m_materialMap.end())
   {
      for (const auto& [mat, name] : m_materialImages)
         if (mat == known->second)
            image = name;
      return known->second;
   }

   const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
   const string imageName = extras.value("vpx_image", ""s);
   const auto toSRGB = [](float c) { return static_cast<int>(powf(clamp(c, 0.f, 1.f), 1.f / 2.2f) * 255.f + 0.5f); };
   Material* const mat = new Material();
   mat->m_name = "SF_Cab_"s + (material->name ? material->name : "Material");
   mat->m_type = pbr.metallic_factor > 0.5f ? Material::METAL : Material::BASIC;
   mat->m_cBase = imageName.empty() ? RGB(toSRGB(pbr.base_color_factor[0]), toSRGB(pbr.base_color_factor[1]), toSRGB(pbr.base_color_factor[2])) : RGB(255, 255, 255);
   const float shininess = clamp(1.f - pbr.roughness_factor, 0.f, 1.f);
   mat->m_fRoughness = shininess; // The roughness of VPX materials is a shininess
   const int glossy = static_cast<int>(shininess * 0.3f * 255.f);
   mat->m_cGlossy = RGB(glossy, glossy, glossy);
   const int clearcoat = extras.value("vpx_clearcoat", 0) ? 40 : 0; // The art of the cabinet is printed under a clear coat
   mat->m_cClearcoat = RGB(clearcoat, clearcoat, clearcoat);
   mat->m_fGlossyImageLerp = 1.f;
   mat->m_fWrapLighting = 0.25f;
   mat->m_fEdge = 1.f;
   mat->m_fEdgeAlpha = 1.f;
   mat->m_fThickness = 0.05f;
   mat->m_fOpacity = 1.f;
   mat->m_bOpacityActive = false;
   m_player->m_ptable->AddMaterial(mat);
   m_materials.push_back(mat);
   m_materialMap.emplace_back(material, mat);

   if (!imageName.empty())
   {
      const std::filesystem::path imagePath = std::filesystem::path(folder) / (imageName + ".webp");
      if (Texture* const texture = Texture::CreateFromFile(imagePath); texture)
      {
         texture->m_name = "SF_Cab_" + imageName;
         m_player->m_ptable->m_vimage.push_back(texture);
         m_images.push_back(texture);
         image = texture->m_name;
         m_materialImages.emplace_back(mat, image);
      }
      else
         PLOGW << "[DefaultCabinet] Cannot load " << imagePath;
   }
   return mat;
}

void DefaultCabinet::LoadNode(const cgltf_data* data, const cgltf_node& node, const Fit& fit, float launcherX, const std::string& folder)
{
   if (node.mesh == nullptr)
      return;
   const nlohmann::json extras = Extras(node.extras);
   const string variant = extras.value("vpx_variant", ""s);
   if (!variant.empty() && variant != (m_launcher == CF::Launcher::Spring ? "spring" : "button"))
      return;
   if (!IsDisplayChosen(extras.value("vpx_display", ""s)))
      return;
   const string roleName = extras.value("vpx_role", ""s);
   const Role role = roleName == "plunger_rod" ? Role::PlungerRod : roleName == "plunger_spring" ? Role::PlungerSpring : roleName == "launch_button" ? Role::LaunchButton
      : roleName == "flipper_left" ? Role::FlipperLeft : roleName == "flipper_right" ? Role::FlipperRight : roleName == "start_button" ? Role::StartButton : Role::None;
   const bool rigid = extras.value("vpx_fit", "deform"s) == "rigid";
   const Point anchor = ReadPoint(extras, "vpx_anchor", { 0.f, 0.f, 0.f });
   const Point offset = !rigid ? Point { 0.f, 0.f, 0.f } : !variant.empty() ? fit.LauncherOffset(launcherX) : fit.RigidOffset(anchor);
   PartGroup* const group = extras.value("vpx_space", "cabinet"s) == "feet" ? m_feetGroup : m_cabinetGroup;
   const float k = m_vpuPerMeter;
   const auto toTable = [&](const Point& p) { return Vertex3Ds(m_centerX + p[0] * k, m_bottomY - p[1] * k, p[2] * k); };
   const auto fitPoint = [&](const Point& p) { return rigid ? Point { p[0] + offset[0], p[1] + offset[1], p[2] + offset[2] } : fit.Deform(p); };

   // Outward direction of the moving part (model axes), and the point it scales from (the spring)
   Point axis = ReadPoint(extras, "vpx_axis", { 0.f, 0.f, 0.f });
   if (role == Role::FlipperLeft)
      axis = { -1.f, 0.f, 0.f };
   else if (role == Role::FlipperRight)
      axis = { 1.f, 0.f, 0.f };
   else if (role == Role::StartButton)
      axis = { 0.f, -1.f, 0.f };
   Vertex3Ds pivot(0.f, 0.f, 0.f);
   if (role == Role::PlungerSpring)
   {
      pivot = toTable(fitPoint(ReadPoint(extras, "vpx_spring_start", anchor)));
      m_springLength = extras.value("vpx_spring_length", 0.04f) * k;
   }

   float world[16];
   cgltf_node_transform_world(&node, world);
   const cgltf_mesh& mesh = *node.mesh;
   for (cgltf_size p = 0; p < mesh.primitives_count; p++)
   {
      const cgltf_primitive& primitive = mesh.primitives[p];
      if (primitive.type != cgltf_primitive_type_triangles)
         continue;
      const cgltf_accessor *position = nullptr, *normal = nullptr, *uv = nullptr;
      for (cgltf_size a = 0; a < primitive.attributes_count; a++)
      {
         const cgltf_attribute& attribute = primitive.attributes[a];
         if (attribute.type == cgltf_attribute_type_position)
            position = attribute.data;
         else if (attribute.type == cgltf_attribute_type_normal)
            normal = attribute.data;
         else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
            uv = attribute.data;
      }
      if (position == nullptr)
         continue;

      string image;
      float unlit = 0.f;
      const Material* const material = GetMaterial(data, primitive.material, folder, image, unlit);

      Primitive* const prim = static_cast<Primitive*>(EditableRegistry::CreateAndInit(eItemPrimitive, m_player->m_ptable, 0.f, 0.f));
      if (prim == nullptr)
         continue;
      prim->SetName(m_player->m_ptable->GetUniqueName("SF_Cab_"s + (node.name ? node.name : "Part")));
      prim->m_desktopBackdrop = false;
      prim->m_d.m_use3DMesh = true;
      prim->m_d.m_vPosition = pivot;
      prim->m_d.m_vSize.Set(1.f, 1.f, 1.f);
      for (float& v : prim->m_d.m_aRotAndTra)
         v = 0.f;
      prim->m_d.m_staticRendering = role == Role::None;
      prim->m_d.m_collidable = false;
      prim->m_d.m_toy = true;
      prim->m_d.m_reflectionEnabled = false;
      prim->m_d.m_visible = true;
      prim->m_d.m_backfacesEnabled = false;
      prim->m_d.m_disableLightingTop = unlit;
      prim->m_d.m_szMaterial = material ? material->m_name : string();
      prim->m_d.m_szImage = image;

      vector<Vertex3D_NoTex2>& vertices = prim->m_mesh.m_vertices;
      vertices.resize(position->count);
      for (cgltf_size v = 0; v < position->count; v++)
      {
         float value[3] = { 0.f, 0.f, 0.f };
         cgltf_accessor_read_float(position, v, value, 3);
         const Point local { world[0] * value[0] + world[4] * value[1] + world[8] * value[2] + world[12], world[1] * value[0] + world[5] * value[1] + world[9] * value[2] + world[13],
            world[2] * value[0] + world[6] * value[1] + world[10] * value[2] + world[14] };
         const Vertex3Ds pos = toTable(fitPoint(local)) - pivot;
         Vertex3D_NoTex2& out = vertices[v];
         out.x = pos.x;
         out.y = pos.y;
         out.z = pos.z;
         float n[3] = { 0.f, 0.f, 1.f };
         if (normal)
            cgltf_accessor_read_float(normal, v, n, 3);
         const float nx = world[0] * n[0] + world[4] * n[1] + world[8] * n[2], ny = world[1] * n[0] + world[5] * n[1] + world[9] * n[2], nz = world[2] * n[0] + world[6] * n[1] + world[10] * n[2];
         out.nx = nx;
         out.ny = -ny; // Y of the table points toward the player
         out.nz = nz;
         float t[2] = { 0.f, 0.f };
         if (uv)
            cgltf_accessor_read_float(uv, v, t, 2);
         out.tu = t[0];
         out.tv = t[1];
      }
      // The table's Y is mirrored from the model's: the triangles turn the other way
      const cgltf_size count = primitive.indices ? primitive.indices->count : position->count;
      vector<unsigned int>& indices = prim->m_mesh.m_indices;
      indices.resize(count);
      for (cgltf_size i = 0; i + 2 < count; i += 3)
      {
         const auto index = [&](cgltf_size j) { return primitive.indices ? static_cast<unsigned int>(cgltf_accessor_read_index(primitive.indices, j)) : static_cast<unsigned int>(j); };
         indices[i] = index(i);
         indices[i + 1] = index(i + 2);
         indices[i + 2] = index(i + 1);
      }
      prim->m_mesh.m_validBounds = false;

      m_player->m_ptable->AddPart(prim);
      prim->Release();
      prim->SetPartGroup(group);
      m_parts.push_back(prim);
      if (role != Role::None)
      {
         // Outward direction in table coordinates (Y mirrored)
         Animated animated { prim, role, { axis[0], -axis[1], axis[2] } };
         m_animated.push_back(animated);
      }
   }
}

// The sides of a wall of the table, as the wall draws them (Surface::GenerateMesh: outline, side image and material, texture coordinates),
// up to the top edge of the sides of the cabinet at most: the art is cut, not squeezed. In the playfield space, drawn only.
void DefaultCabinet::AddSideBlade(Surface* wall, float sideTop, float tanTop)
{
   PinTable* const table = m_player->m_ptable;
   vector<RenderVertex> outline;
   wall->m_curve.GetRgVertex(outline);
   if (outline.size() < 2)
      return;
   float* texcoords = nullptr;
   if (table->GetImage(wall->m_d.m_szSideImage))
      wall->m_curve.GetTextureCoords(outline, &texcoords);
   if (NormalizeWindingOrder(outline) && texcoords)
      std::reverse(texcoords, texcoords + outline.size());
   const float bottom = wall->m_d.m_heightbottom, top = wall->m_d.m_heighttop;
   const auto cutTop = [&](const RenderVertex& v) { return clamp(sideTop + max(0.f, table->m_bottom - v.y) * tanTop, bottom, top); };

   Primitive* const prim = static_cast<Primitive*>(EditableRegistry::CreateAndInit(eItemPrimitive, table, 0.f, 0.f));
   if (prim == nullptr)
   {
      delete[] texcoords;
      return;
   }
   prim->SetName(table->GetUniqueName("SF_Cab_Blade_"s + wall->GetName()));
   prim->m_desktopBackdrop = false;
   prim->m_d.m_use3DMesh = true;
   prim->m_d.m_vPosition = Vertex3Ds(0.f, 0.f, 0.f);
   prim->m_d.m_vSize.Set(1.f, 1.f, 1.f);
   for (float& v : prim->m_d.m_aRotAndTra)
      v = 0.f;
   prim->m_d.m_staticRendering = true;
   prim->m_d.m_collidable = false;
   prim->m_d.m_toy = true;
   prim->m_d.m_visible = true;
   prim->m_d.m_backfacesEnabled = true;
   prim->m_d.m_disableLightingBelow = wall->m_d.m_disableLightingBelow;
   prim->m_d.m_szMaterial = wall->m_d.m_szSideMaterial;
   prim->m_d.m_szImage = texcoords ? wall->m_d.m_szSideImage : string();

   const size_t n = outline.size();
   vector<Vertex3D_NoTex2>& vertices = prim->m_mesh.m_vertices;
   vector<unsigned int>& indices = prim->m_mesh.m_indices;
   vertices.resize(n * 4);
   indices.resize(n * 6);
   for (size_t i = 0; i < n; i++)
   {
      const RenderVertex& a = outline[i];
      const RenderVertex& b = outline[(i + 1) % n];
      const float dx = a.x - b.x, dy = a.y - b.y, len = sqrtf(dx * dx + dy * dy);
      const float nx = len > 0.f ? dy / len : 0.f, ny = len > 0.f ? -dx / len : 0.f;
      const float ta = cutTop(a), tb = cutTop(b);
      const float ua = texcoords ? texcoords[i] : 0.f, ub = texcoords ? texcoords[(i + 1) % n] : 0.f;
      const auto tv = [&](float z) { return top > bottom ? 1.f - (z - bottom) / (top - bottom) : 0.f; };
      Vertex3D_NoTex2* const v = &vertices[i * 4];
      v[0] = { a.x, a.y, bottom, nx, ny, 0.f, ua, 1.f };
      v[1] = { a.x, a.y, ta, nx, ny, 0.f, ua, tv(ta) };
      v[2] = { b.x, b.y, tb, nx, ny, 0.f, ub, tv(tb) };
      v[3] = { b.x, b.y, bottom, nx, ny, 0.f, ub, 1.f };
      const unsigned int o = static_cast<unsigned int>(i * 4);
      const unsigned int quad[6] = { o, o + 1, o + 2, o, o + 2, o + 3 };
      std::copy(quad, quad + 6, &indices[i * 6]);
   }
   delete[] texcoords;
   prim->m_mesh.m_validBounds = false;
   table->AddPart(prim);
   prim->Release();
   m_parts.push_back(prim);
   PLOGI << "[DefaultCabinet] Side blade from '" << wall->GetName() << "' (" << wall->m_d.m_szSideImage << "), cut at " << sideTop << " VPU at the front";
}

DefaultCabinet::Window DefaultCabinet::GetWindow(const std::vector<float>& corners, const Fit& fit) const
{
   // Corners counter clockwise from the bottom left, model units
   Point c[4];
   for (int i = 0; i < 4; i++)
      c[i] = fit.Deform({ corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2] });
   const auto sub = [](const Point& a, const Point& b) { return Point { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; };
   const auto length = [](const Point& a) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); };
   const Point across = sub(c[1], c[0]), up = sub(c[3], c[0]);
   const float k = m_vpuPerMeter;
   const float width = length(across) * k, height = length(up) * k;
   const float lean = RADTOANG(atan2f(-up[1], up[2])); // Top toward the player
   const float cx = m_centerX + 0.25f * (c[0][0] + c[1][0] + c[2][0] + c[3][0]) * k;
   const float cy = m_bottomY - 0.25f * (c[0][1] + c[1][1] + c[2][1] + c[3][1]) * k;
   const float cz = 0.25f * (c[0][2] + c[1][2] + c[2][2] + c[3][2]) * k;
   return Window { cx, cy, cz, width, height, -90.f - lean, true };
}

Flasher* DefaultCabinet::CreateFlasher(const Window& w, int window, const std::string& name)
{
   PinTable* const table = m_player->m_ptable;
   Flasher* const flasher = static_cast<Flasher*>(EditableRegistry::CreateAndInit(eItemFlasher, table, w.x, w.y));
   if (flasher == nullptr)
      return nullptr;
   flasher->SetName(table->GetUniqueName(name));
   constexpr float defaultSize = 100.f; // Size of a new flasher
   flasher->Scale(w.width / defaultSize, w.height / defaultSize, Vertex2D {}, true);
   flasher->m_d.m_rotX = w.rotX;
   flasher->m_d.m_height = w.z;
   flasher->m_d.m_renderMode = FlasherData::EXT_RENDER;
   flasher->m_d.m_renderStyle = window;
   flasher->m_d.m_isVisible = true;
   table->AddPart(flasher);
   flasher->Release();
   flasher->SetPartGroup(m_cabinetGroup);
   m_parts.push_back(flasher);
   PLOGI << "[DefaultCabinet] " << name << ": " << w.width << " x " << w.height << " VPU at " << w.x << ", " << w.y << ", " << w.z << ", leaning " << -90.f - w.rotX << " degrees";
   return flasher;
}

void DefaultCabinet::Update()
{
   // The side walls of the desktop table stay hidden: scripts show them again with their options (Metallica's 'Side Blades')
   for (const HiddenPart& hidden : m_hiddenParts)
      switch (hidden.part->GetItemType())
      {
      case eItemSurface: static_cast<Surface*>(hidden.part)->m_d.m_sideVisible = static_cast<Surface*>(hidden.part)->m_d.m_topBottomVisible = false; break;
      case eItemPrimitive: static_cast<Primitive*>(hidden.part)->m_d.m_visible = false; break;
      case eItemRamp: static_cast<Ramp*>(hidden.part)->m_d.m_visible = false; break;
      default: break;
      }
   const InputManager& input = m_player->m_pininput;
   const float k = m_vpuPerMeter;
   float pull = 0.f; // Knob travel out of its rest place, model units (negative when the rod shoots forward)
   if (m_plunger)
   {
      float position = 0.f; // 0..25, 25 when fully pulled
      m_plunger->Position(&position);
      pull = (position / 25.f - m_plunger->m_d.m_parkPosition) * kPlungerTravel;
   }
   for (const Animated& a : m_animated)
   {
      float out = 0.f; // Move along the outward axis, model units
      switch (a.role)
      {
      case Role::PlungerRod: out = pull; break;
      case Role::PlungerSpring:
         // The spring rests on the sleeve: it opens up a little when the knob is pulled, and is crushed when the rod shoots
         a.part->m_d.m_vSize.y = clamp(1.f + pull * k / max(m_springLength, 1.f), 0.45f, 1.1f);
         break;
      case Role::LaunchButton: out = input.IsPressed(input.GetLaunchBallActionId()) ? -kLaunchButtonTravel : 0.f; break;
      case Role::FlipperLeft: out = input.IsPressed(input.GetLeftFlipperActionId()) ? -kFlipperButtonTravel : 0.f; break;
      case Role::FlipperRight: out = input.IsPressed(input.GetRightFlipperActionId()) ? -kFlipperButtonTravel : 0.f; break;
      case Role::StartButton: out = input.IsPressed(input.GetStartActionId()) ? -kStartButtonTravel : 0.f; break;
      default: break;
      }
      if (a.role != Role::PlungerSpring)
      {
         a.part->m_d.m_aRotAndTra[3] = a.axis[0] * out * k;
         a.part->m_d.m_aRotAndTra[4] = a.axis[1] * out * k;
         a.part->m_d.m_aRotAndTra[5] = a.axis[2] * out * k;
      }
   }
}

void DefaultCabinet::Remove()
{
   PinTable* const table = m_player->m_ptable;
   // The parts that joined its groups (the displays of PinUP Player, CabinetDisplays) stay in the table: they leave them
   for (IEditable* part : table->GetParts())
      if (part->GetPartGroup() && (part->GetPartGroup() == m_cabinetGroup || part->GetPartGroup() == m_feetGroup) && !IsOwnPart(part))
         part->SetPartGroup(nullptr);
   // Parts first (in reverse order: the groups were added first), then what they use
   for (auto it = m_parts.rbegin(); it != m_parts.rend(); ++it)
      if (FindIndexOf(table->GetParts(), *it) != -1)
         table->RemovePart(*it);
   m_parts.clear();
   m_animated.clear();
   m_cabinetGroup = m_feetGroup = nullptr;
   for (Texture* image : m_images)
   {
      if (m_player->m_renderer)
         m_player->m_renderer->m_renderDevice->m_texMan.UnloadTexture(image);
      RemoveFromVectorSingle(table->m_vimage, image);
      delete image;
   }
   m_images.clear();
   for (Material* material : m_materials)
      table->RemoveMaterial(material);
   m_materials.clear();
   m_materialMap.clear();
   m_materialImages.clear();
   if (m_hidImplicitBackglass && m_player->m_implicitVRBackglass)
      m_player->m_implicitVRBackglass->m_d.m_isVisible = true; // It was visible
   m_hidImplicitBackglass = false;
   table->m_cabinetGlassBottomHeight = 0.f;
   for (const HiddenPart& hidden : m_hiddenParts)
      if (FindIndexOf(table->GetParts(), hidden.part) != -1)
      {
         if (hidden.part->GetItemType() == eItemSurface)
         {
            static_cast<Surface*>(hidden.part)->m_d.m_sideVisible = hidden.sideVisible;
            static_cast<Surface*>(hidden.part)->m_d.m_topBottomVisible = hidden.topVisible;
         }
         else if (hidden.part->GetItemType() == eItemPrimitive)
            static_cast<Primitive*>(hidden.part)->m_d.m_visible = true;
         else if (hidden.part->GetItemType() == eItemRamp)
            static_cast<Ramp*>(hidden.part)->m_d.m_visible = true;
      }
   m_hiddenParts.clear();
   table->RemoveInvalidReferences();
}
