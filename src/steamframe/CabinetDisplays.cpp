// license:GPLv3+

#include "core/stdafx.h"
#include "CabinetDisplays.h"

#include "core/editablereg.h"
#include "core/player.h"
#include "parts/flasher.h"
#include "parts/primitive.h"
#include "renderer/Renderer.h"
#include "steamframe/Launcher.h"
#include "steamframe/PupPacks.h"
#include "ui/live/LiveUI.h"
#include "ui/live/ingameui/InGameUIItem.h"
#include "ui/live/ingameui/InGameUIPage.h"

#include <fstream>

namespace
{

// Aspect choices of the placement settings (index 0 is Auto)
struct AspectChoice
{
   const char* label;
   float ratio;
};
constexpr AspectChoice kAspects[] = { { "Auto", 0.f }, { "4:1 (DMD)", 4.f }, { "16:9", 16.f / 9.f }, { "4:3", 4.f / 3.f }, { "21:9", 21.f / 9.f }, { "32:9", 32.f / 9.f } };

// PuP draws while a clip plays, then nothing until the next one: a surface stays shown this long after the last draw, so it
// does not blink between clips, and the table's DMD does not flash back
constexpr uint64_t kShownHoldMs = 1000;

constexpr const char* kPageId = "vrui/cabinet-displays";

uint64_t NowMs() { return usec() / 1000; }

bool ContainsNoCase(const string& text, const string& lowerNeedle) { return lowerCase(text).find(lowerNeedle) != string::npos; }

// Whether the pack defines its FullDMD screen (5): PuP then draws the score view in 16:9, instead of its 4:1 DMD screen (1)
bool HasFullDMDScreen(const std::filesystem::path& packFolder, const string& lowerScript)
{
   std::error_code ec;
   for (const auto& entry : std::filesystem::directory_iterator(packFolder, ec))
   {
      if (lowerCase(entry.path().filename().string()) != "screens.pup")
         continue;
      std::ifstream file(entry.path());
      string line;
      while (std::getline(file, line))
      {
         // ScreenNum,ScreenDes,PlayList,PlayFile,Loopit,Active,Priority,CustomPos
         vector<string> parts;
         size_t start = 0;
         for (size_t comma = line.find(','); comma != string::npos && parts.size() < 6; comma = line.find(',', start))
         {
            parts.push_back(line.substr(start, comma - start));
            start = comma + 1;
         }
         parts.push_back(line.substr(start));
         for (string& part : parts)
         {
            part.erase(std::remove_if(part.begin(), part.end(), [](char c) { return c == '"' || c == ' ' || c == '\t' || c == '\r'; }), part.end());
            part = lowerCase(part);
         }
         if (parts.size() >= 6 && parts[0] == "5")
            return parts[5] != "off" && parts[5] != "disable" && parts[5] != "musiconly" && parts[5] != "forcehide";
      }
   }
   // Screens defined by the script
   return lowerScript.find("pfulldmd") != string::npos;
}

vec3 RotateDirection(const vec3& v, float rotX, float rotY, float rotZ)
{
   const Matrix3D rot = (Matrix3D::MatrixRotateZ(ANGTORAD(rotZ)) * Matrix3D::MatrixRotateY(ANGTORAD(rotY))) * Matrix3D::MatrixRotateX(ANGTORAD(rotX));
   return rot.MultiplyVectorNoTranslate(v);
}

}

///////////////////////////////////////////////////////////////////////////////
// Settings

const CabinetDisplays::PropIds& CabinetDisplays::GetPropIds(Surface surface)
{
   static std::array<PropIds, SurfaceCount> ids;
   static bool registered = false;
   if (!registered)
   {
      registered = true;
      auto& registry = Settings::GetRegistry();
      vector<string> aspects;
      for (const AspectChoice& choice : kAspects)
         aspects.emplace_back(choice.label);
      for (int i = 0; i < SurfaceCount; i++)
      {
         const string prefix = GetSurfaceName(static_cast<Surface>(i));
         const string section = "CabinetDisplays"s;
         ids[i].show = registry.Register(std::make_unique<VPX::Properties::EnumPropertyDef>(section, prefix + "Show", "Show"s,
            "Auto: shown while PinUP Player draws on it. Off: never shown"s, false, 0, 0, vector<string> { "Auto"s, "Off"s }));
         ids[i].offsetX = registry.Register(std::make_unique<VPX::Properties::FloatPropertyDef>(section, prefix + "OffsetX", "Left / right"s,
            "Moves the display to the right (or the left), in centimeters"s, false, CMTOVPU(-100.f), CMTOVPU(100.f), CMTOVPU(0.5f), 0.f));
         ids[i].offsetY = registry.Register(std::make_unique<VPX::Properties::FloatPropertyDef>(section, prefix + "OffsetY", "Up / down"s,
            "Moves the display up (or down), in centimeters"s, false, CMTOVPU(-100.f), CMTOVPU(100.f), CMTOVPU(0.5f), 0.f));
         ids[i].offsetZ = registry.Register(std::make_unique<VPX::Properties::FloatPropertyDef>(section, prefix + "OffsetZ", "Forward / back"s,
            "Moves the display toward the player (or away), in centimeters"s, false, CMTOVPU(-50.f), CMTOVPU(50.f), CMTOVPU(0.5f), 0.f));
         ids[i].size = registry.Register(std::make_unique<VPX::Properties::FloatPropertyDef>(section, prefix + "Size", "Size"s,
            "Size of the display, relative to its default size"s, false, 0.25f, 3.f, 0.01f, 1.f));
         ids[i].tilt = registry.Register(std::make_unique<VPX::Properties::FloatPropertyDef>(section, prefix + "Tilt", "Tilt"s,
            "Tilts the display backward (or forward), in degrees"s, false, -45.f, 45.f, 0.5f, 0.f));
         ids[i].aspect = registry.Register(std::make_unique<VPX::Properties::EnumPropertyDef>(section, prefix + "Aspect", "Aspect"s,
            "Width to height ratio of the display. Auto: the one of PinUP Player's screen (16:9 for a full DMD, 4:1 for a DMD)"s, false, 0, 0, aspects));
      }
   }
   return ids[surface];
}

VPX::Properties::PropertyRegistry::PropId CabinetDisplays::GetHideTableDMDPropId()
{
   static const auto id = Settings::GetRegistry().Register(std::make_unique<VPX::Properties::BoolPropertyDef>("CabinetDisplays"s, "HideTableDMD"s,
      "Hide the table's DMD"s, "Hide the DMD of the table while PinUP Player shows its own in its place"s, false, true));
   return id;
}

const char* CabinetDisplays::GetSurfaceName(Surface surface)
{
   switch (surface)
   {
   case Backglass: return "Backglass";
   case ScoreView: return "DMD";
   case Topper: return "Topper";
   default: return "";
   }
}

float CabinetDisplays::GetAspect(int aspectIndex) { return (aspectIndex > 0 && aspectIndex < (int)std::size(kAspects)) ? kAspects[aspectIndex].ratio : 0.f; }

void CabinetDisplays::LoadPlacements()
{
   const Settings& settings = m_player->m_ptable->GetSettings();
   for (int i = 0; i < SurfaceCount; i++)
   {
      const PropIds& ids = GetPropIds(static_cast<Surface>(i));
      Placement& p = m_surfaces[i].placement;
      p.show = settings.GetInt(ids.show);
      p.offsetX = settings.GetFloat(ids.offsetX);
      p.offsetY = settings.GetFloat(ids.offsetY);
      p.offsetZ = settings.GetFloat(ids.offsetZ);
      p.size = settings.GetFloat(ids.size);
      p.tilt = settings.GetFloat(ids.tilt);
      p.aspect = settings.GetInt(ids.aspect);
   }
   m_hideTableDMD = settings.GetBool(GetHideTableDMDPropId());
}

///////////////////////////////////////////////////////////////////////////////
// Creation

std::unique_ptr<CabinetDisplays> CabinetDisplays::Create(Player* player)
{
   const char* forced = getenv("VPX_CABINET_DISPLAYS");
   if (!player->IsVR() && !(forced && forced[0] == '1'))
      return nullptr;
   PinTable* const table = player->m_ptable;
   const PupPacks::TableUse use = PupPacks::GetTableUse(table->m_script_text);
   const std::filesystem::path packFolder
      = PupPacks::FindPackFolder(use.names, table->m_filename.parent_path(), g_launcher ? g_launcher->GetSharedPupVideosPath() : std::filesystem::path());
   // PuP is used through the script, or started by the plugin itself for a PinMAME game which has a pack
   if (!use.usesPup && packFolder.empty())
      return nullptr;

   // The ancillary window flashers of the table: a topper one may have been converted as a backglass (legacy 'backglassimage' primitive)
   bool hasTableBackglass = false, hasTableTopper = false;
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemFlasher)
         continue;
      Flasher* const flasher = static_cast<Flasher*>(part);
      if (flasher->m_d.m_renderMode != FlasherData::EXT_RENDER || flasher == player->m_implicitVRBackglass)
         continue;
      if (flasher->m_d.m_renderStyle == VPXWindowId::VPXWINDOW_Backglass && ContainsNoCase(flasher->GetName(), "topper"s))
      {
         PLOGI << "[CabinetDisplays] Flasher '" << flasher->GetName() << "' shows the topper, not the backglass";
         flasher->m_d.m_renderStyle = VPXWindowId::VPXWINDOW_Topper;
      }
      hasTableBackglass |= flasher->m_d.m_renderStyle == VPXWindowId::VPXWINDOW_Backglass;
      hasTableTopper |= flasher->m_d.m_renderStyle == VPXWindowId::VPXWINDOW_Topper;
   }

   auto displays = std::make_unique<CabinetDisplays>(player);
   displays->CreateSurfaces(packFolder, hasTableBackglass, hasTableTopper);
   if (!displays->HasSurfaces())
      return nullptr;
   PLOGI << "[CabinetDisplays] PuP pack " << (packFolder.empty() ? "not found yet (the script may start it later)"s : packFolder.string());
   return displays;
}

CabinetDisplays::CabinetDisplays(Player* player)
   : m_player(player)
{
}

bool CabinetDisplays::HasSurfaces() const
{
   return std::any_of(m_surfaces.begin(), m_surfaces.end(), [](const SurfaceState& s) { return s.flasher != nullptr; });
}

CabinetDisplays::Rect CabinetDisplays::GetFlasherRect(const Flasher* flasher)
{
   Rect rect;
   const Vertex2D center = flasher->m_curve.GetCenter();
   rect.center = vec3(center.x, center.y, flasher->m_d.m_height);
   rect.width = flasher->m_curve.GetMaxBound().x - flasher->m_curve.GetMinBound().x;
   rect.height = flasher->m_curve.GetMaxBound().y - flasher->m_curve.GetMinBound().y;
   rect.rotX = flasher->m_d.m_rotX;
   rect.rotY = flasher->m_d.m_rotY;
   rect.rotZ = flasher->m_d.m_rotZ;
   return rect;
}

// Same place as the implicit VR backglass of the player (see Player::Player)
CabinetDisplays::Rect CabinetDisplays::GetDefaultBackglassRect() const
{
   const PinTable* const table = m_player->m_ptable;
   Rect rect;
   rect.width = 1.2f * (table->m_right - table->m_left);
   rect.height = rect.width * 0.75f;
   rect.center = vec3(0.5f * (table->m_right - table->m_left), 0.f, rect.height * 0.5f + table->m_glassTopHeight);
   rect.rotX = -90.f;
   return rect;
}

bool CabinetDisplays::FindDMDAnchor(Rect& rect)
{
   PinTable* const table = m_player->m_ptable;
   // A flasher showing the DMD of the VR room: named like a DMD, or rendering a display, and standing above the glass (the desktop DMD
   // flashers of the tables lie over the playfield or under the apron)
   const Flasher* best = nullptr;
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemFlasher)
         continue;
      Flasher* const flasher = static_cast<Flasher*>(part);
      const FlasherData::RenderMode mode = flasher->m_d.m_renderMode;
      if (mode == FlasherData::EXT_RENDER || flasher->m_desktopBackdrop)
         continue;
      const bool isDisplay = mode == FlasherData::DMD || mode == FlasherData::DISPLAY || mode == FlasherData::ALPHASEG;
      if (!isDisplay && !ContainsNoCase(flasher->GetName(), "dmd"s))
         continue;
      if (flasher->m_d.m_height < table->m_glassTopHeight)
         continue;
      m_tableDMDs.push_back(flasher);
      if (best == nullptr || flasher->m_d.m_height > best->m_d.m_height)
         best = flasher;
   }
   if (best)
   {
      rect = GetFlasherRect(best);
      PLOGI << "[CabinetDisplays] DMD placed over the flasher '" << best->GetName() << '\'';
      return true;
   }

   // A primitive of the cabinet named like a DMD (PinCab_DMD...): its bounds, assumed to stand facing the player
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemPrimitive || !ContainsNoCase(part->GetName(), "dmd"s))
         continue;
      Primitive* const primitive = static_cast<Primitive*>(part);
      if (primitive->m_mesh.m_vertices.empty())
         continue;
      const Matrix3D& transform = primitive->RecalculateMatrices();
      vec3 minV(FLT_MAX, FLT_MAX, FLT_MAX), maxV(-FLT_MAX, -FLT_MAX, -FLT_MAX);
      for (const Vertex3D_NoTex2& v : primitive->m_mesh.m_vertices)
      {
         const vec3 p = transform * vec3(v.x, v.y, v.z);
         minV = vec3(std::min(minV.x, p.x), std::min(minV.y, p.y), std::min(minV.z, p.z));
         maxV = vec3(std::max(maxV.x, p.x), std::max(maxV.y, p.y), std::max(maxV.z, p.z));
      }
      if (maxV.z < table->m_glassTopHeight || maxV.x - minV.x <= 0.f || maxV.z - minV.z <= 0.f)
         continue;
      rect.width = maxV.x - minV.x;
      rect.height = maxV.z - minV.z;
      rect.center = vec3(0.5f * (minV.x + maxV.x), maxV.y, 0.5f * (minV.z + maxV.z));
      rect.rotX = -90.f;
      PLOGI << "[CabinetDisplays] DMD placed over the primitive '" << primitive->GetName() << '\'';
      return true;
   }
   return false;
}

Flasher* CabinetDisplays::CreateFlasher(Surface surface)
{
   PinTable* const table = m_player->m_ptable;
   Flasher* const flasher = (Flasher*)EditableRegistry::CreateAndInit(ItemTypeEnum::eItemFlasher, table, 0.f, 0.f);
   if (flasher == nullptr)
      return nullptr;
   flasher->SetName(table->GetUniqueName("SF_PuP_"s + GetSurfaceName(surface)));
   flasher->m_d.m_renderMode = FlasherData::EXT_RENDER;
   flasher->m_d.m_renderStyle = surface == Backglass ? VPXWindowId::VPXWINDOW_Backglass : surface == ScoreView ? VPXWindowId::VPXWINDOW_ScoreView : VPXWindowId::VPXWINDOW_Topper;
   flasher->m_d.m_isVisible = true; // Always 'visible', for its renderer to be called: it only draws while PuP draws on it (see Flasher::RenderCabinetDisplay)
   table->AddPart(flasher);
   flasher->Release();
   return flasher;
}

void CabinetDisplays::CreateSurfaces(const std::filesystem::path& packFolder, bool hasTableBackglass, bool hasTableTopper)
{
   LoadPlacements();

   // The backglass of the cabinet: the table's, the implicit VR one, or the same place as that one
   m_backglass = GetDefaultBackglassRect();
   const Flasher* tableBackglass = nullptr;
   for (IEditable* part : m_player->m_ptable->GetParts())
      if (part->GetItemType() == eItemFlasher && static_cast<Flasher*>(part)->m_d.m_renderMode == FlasherData::EXT_RENDER
         && static_cast<Flasher*>(part)->m_d.m_renderStyle == VPXWindowId::VPXWINDOW_Backglass && part != m_player->m_implicitVRBackglass)
      {
         tableBackglass = static_cast<Flasher*>(part);
         break;
      }
   if (tableBackglass)
      m_backglass = GetFlasherRect(tableBackglass);
   if (!hasTableBackglass)
   {
      m_surfaces[Backglass].anchor = m_backglass;
      m_surfaces[Backglass].flasher = CreateFlasher(Backglass);
   }

   // The DMD, in front of the table's one (fitted to its height, wider for a 16:9 full DMD), otherwise under the backglass
   Rect dmd;
   const bool hasAnchor = FindDMDAnchor(dmd);
   const bool fullDMD = !packFolder.empty() ? HasFullDMDScreen(packFolder, lowerCase(m_player->m_ptable->m_script_text)) : false;
   if (!hasAnchor)
   {
      dmd = m_backglass;
      dmd.width = 0.5f * m_backglass.width;
      dmd.height = dmd.width * 0.25f;
      const vec3 down = RotateDirection(vec3(0.f, 1.f, 0.f), dmd.rotX, dmd.rotY, dmd.rotZ); // Local +y is the bottom of the flasher
      dmd.center = m_backglass.center + down * (0.5f * m_backglass.height + 0.6f * dmd.height);
      PLOGI << "[CabinetDisplays] No DMD in the cabinet: DMD placed under the backglass";
   }
   // In front of the anchor, toward the player (+y)
   vec3 normal = RotateDirection(vec3(0.f, 0.f, 1.f), dmd.rotX, dmd.rotY, dmd.rotZ);
   if (normal.y < 0.f)
      normal = -normal;
   dmd.center = dmd.center + normal * 1.5f;
   m_surfaces[ScoreView].anchor = dmd;
   m_surfaces[ScoreView].defaultAspect = fullDMD ? 16.f / 9.f : 0.f;
   m_surfaces[ScoreView].flasher = CreateFlasher(ScoreView);

   // The topper, above the backglass
   if (!hasTableTopper)
   {
      Rect topper = m_backglass;
      topper.width = 0.7f * m_backglass.width;
      topper.height = topper.width * 9.f / 16.f;
      const vec3 up = RotateDirection(vec3(0.f, -1.f, 0.f), topper.rotX, topper.rotY, topper.rotZ);
      topper.center = m_backglass.center + up * (0.5f * m_backglass.height + 0.5f * topper.height + 0.02f * m_backglass.height);
      m_surfaces[Topper].anchor = topper;
      m_surfaces[Topper].defaultAspect = 16.f / 9.f;
      m_surfaces[Topper].flasher = CreateFlasher(Topper);
   }

   for (int i = 0; i < SurfaceCount; i++)
      if (m_surfaces[i].flasher)
      {
         ApplyPlacement(static_cast<Surface>(i));
         PLOGI << "[CabinetDisplays] " << GetSurfaceName(static_cast<Surface>(i)) << " surface created: " << m_surfaces[i].flasher->GetName();
      }
}

void CabinetDisplays::ApplyPlacement(Surface surface)
{
   SurfaceState& s = m_surfaces[surface];
   const Placement& p = s.placement;
   Rect rect = s.anchor;
   // Aspect: chosen, or the default of the surface; the height of the anchor is kept (a full DMD over a 4:1 DMD gets wider)
   const float aspect = p.aspect > 0 ? GetAspect(p.aspect) : s.defaultAspect;
   if (aspect > 0.f && rect.height > 0.f)
      rect.width = rect.height * aspect;
   rect.width *= p.size;
   rect.height *= p.size;
   rect.center = rect.center + vec3(p.offsetX, p.offsetZ, p.offsetY); // Right, toward the player (+y), up
   rect.rotX += p.tilt;
   s.flasher->SetCabinetDisplayRect(Vertex2D(rect.center.x, rect.center.y), rect.width, rect.height);
   s.flasher->m_d.m_height = rect.center.z;
   s.flasher->m_d.m_rotX = rect.rotX;
   s.flasher->m_d.m_rotY = rect.rotY;
   s.flasher->m_d.m_rotZ = rect.rotZ;
   s.applied = p;
   s.hasApplied = true;
}

///////////////////////////////////////////////////////////////////////////////
// Frame updates and rendering

void CabinetDisplays::Update()
{
   m_placementPageOpened = m_player->m_liveUI->m_inGameUI.IsOpened(kPageId);
   for (int i = 0; i < SurfaceCount; i++)
   {
      SurfaceState& s = m_surfaces[i];
      if (s.flasher == nullptr)
         continue;
      const Placement& a = s.applied;
      const Placement& p = s.placement;
      if (!s.hasApplied || a.offsetX != p.offsetX || a.offsetY != p.offsetY || a.offsetZ != p.offsetZ || a.size != p.size || a.tilt != p.tilt || a.aspect != p.aspect)
         ApplyPlacement(static_cast<Surface>(i));
   }
   // One surface per window, as the size of PuP's screens follows it: the implicit VR backglass, when shown, is the backglass
   if (m_surfaces[Backglass].flasher)
      m_surfaces[Backglass].flasher->m_d.m_isVisible = !(m_player->m_implicitVRBackglass && m_player->m_implicitVRBackglass->m_d.m_isVisible);

   const bool suppress = m_hideTableDMD && HasSurface(ScoreView) && IsShown(ScoreView) && !m_placementPageOpened && m_surfaces[ScoreView].everDrawn;
   if (suppress != m_tableDMDSuppressed)
   {
      m_tableDMDSuppressed = suppress;
      for (Flasher* flasher : m_tableDMDs)
         flasher->m_cabinetSuppressed = suppress;
      PLOGI << "[CabinetDisplays] Table DMD " << (suppress ? "hidden: PuP shows its DMD" : "shown again");
   }
}

int CabinetDisplays::GetSurfaceIndex(const Flasher* flasher) const
{
   for (int i = 0; i < SurfaceCount; i++)
      if (m_surfaces[i].flasher == flasher)
         return i;
   return -1;
}

const AncillaryRendererDef* CabinetDisplays::GetPupRenderer(Surface surface) const
{
   const int window = surface == Backglass ? VPXWindowId::VPXWINDOW_Backglass : surface == ScoreView ? VPXWindowId::VPXWINDOW_ScoreView : VPXWindowId::VPXWINDOW_Topper;
   for (const AncillaryRendererDef& renderer : m_player->m_ancillaryWndRenderers[window])
      if (renderer.id && strcmp(renderer.id, "PUP") == 0)
         return &renderer;
   return nullptr;
}

bool CabinetDisplays::IsShown(Surface surface) const
{
   const SurfaceState& s = m_surfaces[surface];
   if (s.flasher == nullptr || s.placement.show == 1)
      return false;
   return m_placementPageOpened || (s.everDrawn && NowMs() - s.lastDrawMs < kShownHoldMs);
}

void CabinetDisplays::OnRendered(Surface surface, bool drew)
{
   SurfaceState& s = m_surfaces[surface];
   if (!drew)
      return;
   if (!s.everDrawn)
      PLOGI << "[CabinetDisplays] " << GetSurfaceName(surface) << " shown: PuP draws on it";
   s.everDrawn = true;
   s.lastDrawMs = NowMs();
}

void CabinetDisplays::GetCanvasSize(Surface surface, float width, float height, float& canvasWidth, float& canvasHeight) const
{
   // About the resolution of PuP's videos (1920 wide for the backglass and full DMD), without going over it
   constexpr float targetLongSide = 1280.f;
   const float longSide = std::max(width, height);
   const float scale = longSide > 0.f ? targetLongSide / longSide : 1.f;
   canvasWidth = std::round(width * scale);
   canvasHeight = std::round(height * scale);
}

///////////////////////////////////////////////////////////////////////////////
// Flasher members of the fork (see flasher.h)

void Flasher::SetCabinetDisplayRect(const Vertex2D& center, float width, float height)
{
   m_curve.ClearPoints();
   const float x0 = center.x - 0.5f * width, x1 = center.x + 0.5f * width;
   const float y0 = center.y - 0.5f * height, y1 = center.y + 0.5f * height;
   m_curve.PushPoint(std::make_unique<DragPoint>(&m_curve, x0, y0, 0.f, false));
   m_curve.PushPoint(std::make_unique<DragPoint>(&m_curve, x0, y1, 0.f, false));
   m_curve.PushPoint(std::make_unique<DragPoint>(&m_curve, x1, y1, 0.f, false));
   m_curve.PushPoint(std::make_unique<DragPoint>(&m_curve, x1, y0, 0.f, false));
   // Once set up for rendering, the quad of the external renderer mode is rebuilt as Flasher::RenderSetup does, and uploaded by the next render
   if (m_renderer && m_vertices.size() == 4)
   {
      m_vertices[0] = { x0, y0, 0.f, 0.f, 0.f, 1.0f, 0.f, 0.f };
      m_vertices[1] = { x0, y1, 0.f, 0.f, 0.f, 1.0f, 0.f, 1.f };
      m_vertices[2] = { x1, y1, 0.f, 0.f, 0.f, 1.0f, 1.f, 1.f };
      m_vertices[3] = { x1, y0, 0.f, 0.f, 0.f, 1.0f, 1.f, 0.f };
   }
   m_dynamicVertexBufferRegenerate = true;
}

bool Flasher::RenderCabinetDisplay(const Vertex3Ds& pos)
{
   CabinetDisplays* const displays = g_pplayer->m_cabinetDisplays.get();
   const int index = displays ? displays->GetSurfaceIndex(this) : -1;
   if (index < 0)
      return false; // Not one of ours: upstream's path
   const auto surface = static_cast<CabinetDisplays::Surface>(index);
   const AncillaryRendererDef* const pup = displays->GetPupRenderer(surface);
   if (pup == nullptr || displays->GetPlacement(surface).show == 1)
      return true;

   RenderDevice* const rd = m_renderer->m_renderDevice;
   const float width = m_curve.GetMaxBound().x - m_curve.GetMinBound().x;
   const float height = m_curve.GetMaxBound().y - m_curve.GetMinBound().y;
   // Background: black while PuP shows on it, dimmed green while it is being placed and PuP draws nothing, nothing otherwise
   const bool shown = displays->IsShown(surface);
   if (shown)
   {
      rd->SetRenderState(RenderState::ALPHABLENDENABLE, RenderState::RS_FALSE);
      rd->SetRenderState(RenderState::ZWRITEENABLE, RenderState::RS_FALSE);
      rd->m_basicShader->SetTechnique(ShaderTechnique::unshaded_without_texture);
      if (displays->IsBeingPlaced())
         rd->m_basicShader->SetVector(ShaderUniform::staticColor_Alpha, 0.f, 0.15f, 0.05f, 1.f);
      else
         rd->m_basicShader->SetVector(ShaderUniform::staticColor_Alpha, 0.f, 0.f, 0.f, 1.f);
      rd->DrawMesh(rd->m_basicShader, true, pos, m_d.m_depthBias, m_meshBuffer, RenderDevice::TRIANGLELIST, 0, m_numPolys * 3);
      rd->m_basicShader->SetVector(ShaderUniform::staticColor_Alpha, 1.f, 1.f, 1.f, 1.f);
   }

   // Same transform as the external renderer mode of upstream (see Flasher::Render), with a canvas sized for PuP
   const Matrix3D transform = Matrix3D::MatrixTranslate(-0.5f, -0.5f, 0.f) * Matrix3D::MatrixScale(width, height, 0.f) //
      * ((Matrix3D::MatrixRotateZ(ANGTORAD(m_d.m_rotZ)) * Matrix3D::MatrixRotateY(ANGTORAD(m_d.m_rotY))) * Matrix3D::MatrixRotateX(ANGTORAD(m_d.m_rotX))) //
      * Matrix3D::MatrixTranslate(m_curve.GetMinBound().x + 0.5f * width, m_curve.GetMinBound().y + 0.5f * height, m_d.m_height);
   float canvasWidth, canvasHeight;
   displays->GetCanvasSize(surface, width, height, canvasWidth, canvasHeight);
   VPXRenderContext2D* const context = &m_renderer->GetAncillaryRenderContext(static_cast<VPXWindowId>(m_d.m_renderStyle), canvasWidth, canvasHeight, false, true, m_d.m_depthBias - pos.z, transform);
   const RenderPass* const pass = rd->GetCurrentPass();
   const unsigned int commandsBefore = pass ? pass->GetCommandCount() : 0;
   pup->Render(context, pup->context);
   // PuP claims its windows as soon as its screens exist, even when nothing plays: what it actually drew tells if the surface is used
   const bool drew = rd->GetCurrentPass() != pass || (pass && pass->GetCommandCount() > commandsBefore);
   displays->OnRendered(surface, drew);
   return true;
}

///////////////////////////////////////////////////////////////////////////////
// In-game menu

namespace
{

class CabinetDisplaysPage final : public VPX::InGameUI::InGameUIPage
{
public:
   CabinetDisplaysPage()
      : InGameUIPage("PuP displays"s, "Where the displays of PinUP Player stand on the cabinet. Saved for this table."s, SaveMode::Table)
   {
   }

private:
   void BuildPage() override
   {
      using namespace VPX::InGameUI;
      CabinetDisplays* const displays = m_player->m_cabinetDisplays.get();
      if (displays == nullptr)
         return;
      AddItem(std::make_unique<InGameUIItem>(CabinetDisplays::GetHideTableDMDPropId(), //
         [displays]() { return displays->m_hideTableDMD; }, //
         [displays](bool v) { displays->m_hideTableDMD = v; }));
      for (int i = 0; i < CabinetDisplays::SurfaceCount; i++)
      {
         const auto surface = static_cast<CabinetDisplays::Surface>(i);
         if (!displays->HasSurface(surface))
            continue;
         const CabinetDisplays::PropIds& ids = CabinetDisplays::GetPropIds(surface);
         CabinetDisplays::Placement* const p = &displays->GetPlacement(surface);
         AddItem(std::make_unique<InGameUIItem>(InGameUIItem::LabelType::Header, CabinetDisplays::GetSurfaceName(surface)));
         AddItem(std::make_unique<InGameUIItem>(ids.show, [p]() { return p->show; }, [p](int, int v) { p->show = v; }));
         AddItem(std::make_unique<InGameUIItem>(ids.offsetX, VPUTOCM(1.f), "%4.1f cm"s, [p]() { return p->offsetX; }, [p](float, float v) { p->offsetX = v; }));
         AddItem(std::make_unique<InGameUIItem>(ids.offsetY, VPUTOCM(1.f), "%4.1f cm"s, [p]() { return p->offsetY; }, [p](float, float v) { p->offsetY = v; }));
         AddItem(std::make_unique<InGameUIItem>(ids.offsetZ, VPUTOCM(1.f), "%4.1f cm"s, [p]() { return p->offsetZ; }, [p](float, float v) { p->offsetZ = v; }));
         AddItem(std::make_unique<InGameUIItem>(ids.size, 100.f, "%4.0f %%"s, [p]() { return p->size; }, [p](float, float v) { p->size = v; }));
         AddItem(std::make_unique<InGameUIItem>(ids.tilt, 1.f, "%4.1f deg"s, [p]() { return p->tilt; }, [p](float, float v) { p->tilt = v; }));
         if (surface != CabinetDisplays::Backglass)
            AddItem(std::make_unique<InGameUIItem>(ids.aspect, [p]() { return p->aspect; }, [p](int, int v) { p->aspect = v; }));
      }
   }
};

}

void CabinetDisplays::RegisterPage(Player* player)
{
   player->m_liveUI->m_inGameUI.AddPage(kPageId, []() { return std::make_unique<CabinetDisplaysPage>(); });
}

void CabinetDisplays::AddMenuItem(VPX::InGameUI::InGameUIPage& page)
{
   if (page.m_player->m_cabinetDisplays)
      page.AddItem(std::make_unique<VPX::InGameUI::InGameUIItem>("PuP displays"s, "Place the displays of PinUP Player on the cabinet"s, kPageId));
}
