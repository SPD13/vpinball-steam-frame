// license:GPLv3+

#pragma once

#include "plugins/VPXPlugin.h"

#include <array>
#include <filesystem>

class Player;
class Flasher;
namespace VPX::InGameUI { class InGameUIPage; }

// Displays of PinUP Player (PuP) packs on the cabinet, in VR.
//
// On a desktop, PinUP Player shows its screens in windows (backglass, DMD, topper). In VR, they are only visible on the surfaces of the
// table's VR room that are flashers rendering an ancillary window (render mode 'External renderer'). The VR rooms of the tables were
// built for Windows, where PinUP Player draws in desktop windows the headset never shows: they have a backglass surface at best (the
// legacy 'backglassimage' primitive, converted to such a flasher when the table is loaded), never one for the DMD or the topper.
//
// When the table uses PuP, the cabinet gets the surfaces it lacks, created before the parts are set up for rendering:
// - the DMD (PuP's FullDMD or DMD screen, window 'ScoreView') in front of the table's DMD (a flasher or a primitive named like a DMD),
// - the topper above the backglass,
// - the backglass itself, when the table has no backglass surface and the implicit VR backglass is off.
// They call PuP's renderer only (B2S also claims the score view), and show only while PuP draws something on them. While PuP draws the
// DMD, the table's own DMD flashers are hidden (Flasher::m_cabinetSuppressed), so that the PinMAME or FlexDMD DMD does not show around it.
//
// Their placement (offsets, size, tilt, aspect) is adjusted from the in-game menu ('PuP displays', page 'vrui/cabinet-displays') and
// saved per table, in the 'CabinetDisplays' section of the table settings.
//
// Everything happens on the main thread (parts setup, frame preparation, flasher rendering and the in-game menu).
class CabinetDisplays final
{
public:
   enum Surface : int { Backglass, ScoreView, Topper, SurfaceCount };

   // Returns nullptr if the table does not use PuP, or when not in VR (unless VPX_CABINET_DISPLAYS=1, to test on a desktop)
   static std::unique_ptr<CabinetDisplays> Create(Player* player);
   // Whether the PinUP Player pack of the table draws its 16:9 full DMD screen in the score view (not its 4:1 DMD screen): the default
   // cabinet then shows a 16:9 screen in its speaker panel
   static bool DrawsFullDMD(const PinTable* table);
   explicit CabinetDisplays(Player* player);

   // In-game menu: the placement page, and its entry in the menu of the table (see Launcher::AddHomePageTableItems)
   static void RegisterPage(Player* player);
   static void AddMenuItem(VPX::InGameUI::InGameUIPage& page);

   // Each frame, before the parts are rendered: places the surfaces again when the script moved the parts they stand on (at its init,
   // or later for the table's DMD and backglass flashers), applies the placement settings, and hides the table's DMD while PuP draws
   // in its place
   void Update();

   bool HasSurface(Surface surface) const { return m_surfaces[surface].flasher != nullptr; }
   bool HasSurfaces() const;
   static const char* GetSurfaceName(Surface surface);

   // Rendering of the flashers of the surfaces (see Flasher::RenderCabinetDisplay)
   int GetSurfaceIndex(const Flasher* flasher) const;
   const AncillaryRendererDef* GetPupRenderer(Surface surface) const;
   bool IsShown(Surface surface) const; // PuP drew on it recently, or its placement is being adjusted
   bool IsBeingPlaced() const { return m_placementPageOpened; }
   void OnRendered(Surface surface, bool drew);
   // Size of the canvas PuP lays out its screen on: the flasher size in VPX units is far too small for a DMD, and PuP scales its videos to it
   void GetCanvasSize(Surface surface, float width, float height, float& canvasWidth, float& canvasHeight) const;

   // Placement settings
   struct Placement
   {
      int show = 0; // 0 = Auto (when PuP draws on it), 1 = Off
      float offsetX = 0.f, offsetY = 0.f, offsetZ = 0.f; // Right, up and toward the player, in VPX units
      float size = 1.f; // Scale of the default size
      float tilt = 0.f; // Added to the X rotation, in degrees
      int aspect = 0; // 0 = Auto, then see kAspects
   };
   struct PropIds
   {
      VPX::Properties::PropertyRegistry::PropId show, offsetX, offsetY, offsetZ, size, tilt, aspect;
   };
   static const PropIds& GetPropIds(Surface surface);
   static VPX::Properties::PropertyRegistry::PropId GetHideTableDMDPropId();
   Placement& GetPlacement(Surface surface) { return m_surfaces[surface].placement; }
   bool m_hideTableDMD = true;

private:
   struct Rect
   {
      vec3 center; // x, y on the table plane, z the height
      float width = 0.f, height = 0.f;
      float rotX = -90.f, rotY = 0.f, rotZ = 0.f;
   };
   struct SurfaceState
   {
      Flasher* flasher = nullptr;
      Rect anchor; // Default place, before the placement settings
      float defaultAspect = 0.f; // Width / height of the default size, 0 to keep the one of the anchor
      Placement placement;
      Placement applied; // Last placement given to the flasher
      bool hasApplied = false;
      uint64_t lastDrawMs = 0; // Last time PuP drew on it
      bool everDrawn = false;
   };

   void CreateSurfaces(const std::filesystem::path& packFolder, bool hasTableBackglass, bool hasTableTopper);
   void PlaceAnchors(std::array<Rect, SurfaceCount>& anchors);
   bool AnchorsMoved() const;
   void PlaceAgain();
   Flasher* CreateFlasher(Surface surface);
   void ApplyPlacement(Surface surface);
   void LoadPlacements();
   bool FindDMDAnchor(Rect& rect);
   bool FindCabinetBackglass(Rect& rect) const;
   bool FindCabinetTopper(Rect& rect) const;
   static bool FitUprightPanel(Primitive* primitive, float glassTopHeight, Rect& rect);
   bool KeepAboveDMD(Rect& glass, const Rect& dmd) const;
   Rect GetDefaultBackglassRect() const;
   static Rect GetFlasherRect(const Flasher* flasher);
   static bool SameRect(const Rect& a, const Rect& b);
   static float GetAspect(int aspectIndex);

   Player* const m_player;
   std::array<SurfaceState, SurfaceCount> m_surfaces;
   Rect m_backglass; // The backglass of the cabinet, used to place the other surfaces
   std::vector<Flasher*> m_tableDMDs; // Flashers of the table showing its DMD, hidden while PuP draws on the DMD surface
   bool m_tableDMDSuppressed = false;
   bool m_placementPageOpened = false;

   // The parts the surfaces stand on are found before the table script runs, which may move them (Total Nuclear Annihilation moves its VR
   // DMD 190 units toward the player at init): the surfaces are placed again at the first frame, then follow the flashers they stand on
   bool m_hasTableBackglass = false, m_hasTableTopper = false, m_fullDMD = false;
   const Flasher* m_dmdAnchorFlasher = nullptr; // The table's DMD flasher the DMD surface stands in front of, if any
   const Flasher* m_tableBackglassFlasher = nullptr; // The table's backglass surface, the other surfaces are placed from it
   Rect m_dmdAnchorSource, m_tableBackglassSource; // Their place when the surfaces were placed
   bool m_placedAfterInit = false;
   bool m_logPlacement = true; // Details of the placement in the log: at creation only
   uint64_t m_nextPlaceAgainMs = 0;
   int m_placedAgainLogs = 0;
};
