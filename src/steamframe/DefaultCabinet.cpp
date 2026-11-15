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
#include "renderer/Renderer.h"
#include "renderer/Texture.h"
#include "steamframe/CabinetBackglass.h"
#include "steamframe/Launcher.h"
#include "steamframe/Passthrough.h"
#include "ui/VPXFileFeedback.h"

#include "cgltf/cgltf.h"
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

// Moves of the animated parts, model units (meters)
constexpr float kPlungerTravel = 2.8f * 0.0254f; // Knob, from rest to fully pulled
constexpr float kLaunchButtonTravel = 0.15f * 0.0254f;
constexpr float kFlipperButtonTravel = 0.2f * 0.0254f;
constexpr float kStartButtonTravel = 0.12f * 0.0254f;

}

std::unique_ptr<DefaultCabinet> DefaultCabinet::Create(Player* player)
{
   const char* env = getenv("VPX_DEFAULT_CABINET");
   if (env && env[0] == '0')
      return nullptr;
   const bool forced = env && (env[0] == '1' || env[0] == '2'); // 2: also on the tables made for VR (tests)
   if ((!player->IsVR() && !forced) || player->m_isLobby || (Launcher::IsMadeForVR(player->m_ptable) && !(env && env[0] == '2')))
      return nullptr;
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
   geometry.glassBottom = table->m_glassBottomHeight / k;
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
      if (corners == extras.end() || !corners->is_array() || corners->size() != 12)
         continue;
      const std::vector<float> c = corners->get<std::vector<float>>();
      if (role == "backglass" && !VPX::Cabinet::FindDirectB2S(table->m_filename).empty())
         CreateFlasher(c, fit, VPXWindowId::VPXWINDOW_Backglass, "SF_Cab_Backglass"s);
      else if (role == "display")
         CreateFlasher(c, fit, VPXWindowId::VPXWINDOW_ScoreView, "SF_Cab_Display"s);
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

Flasher* DefaultCabinet::CreateFlasher(const std::vector<float>& corners, const Fit& fit, int window, const std::string& name)
{
   PinTable* const table = m_player->m_ptable;
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

   Flasher* const flasher = static_cast<Flasher*>(EditableRegistry::CreateAndInit(eItemFlasher, table, cx, cy));
   if (flasher == nullptr)
      return nullptr;
   flasher->SetName(table->GetUniqueName(name));
   constexpr float defaultSize = 100.f; // Size of a new flasher
   flasher->Scale(width / defaultSize, height / defaultSize, Vertex2D {}, true);
   flasher->m_d.m_rotX = -90.f - lean;
   flasher->m_d.m_height = cz;
   flasher->m_d.m_renderMode = FlasherData::EXT_RENDER;
   flasher->m_d.m_renderStyle = window;
   flasher->m_d.m_isVisible = true;
   table->AddPart(flasher);
   flasher->Release();
   flasher->SetPartGroup(m_cabinetGroup);
   m_parts.push_back(flasher);
   PLOGI << "[DefaultCabinet] " << name << ": " << width << " x " << height << " VPU at " << cx << ", " << cy << ", " << cz << ", leaning " << lean << " degrees";
   return flasher;
}

void DefaultCabinet::Update()
{
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
   table->RemoveInvalidReferences();
}
