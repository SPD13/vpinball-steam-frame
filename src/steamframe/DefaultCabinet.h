// license:GPLv3+

#pragma once

#include "CabinetFit.h"
#include "parts/PartGroup.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class Player;
class Primitive;
class PartGroup;
class Flasher;
class Texture;
class Material;
class Plunger;
class IEditable;

// Steam Frame Edition: the default VR cabinet, for the tables made for desktops and cabinets (see Launcher::IsMadeForVR). In a headset,
// such a table only shows its playfield, floating in the room; it gets a pinball cabinet instead, a Williams WPC standard body with
// generic art, built by steamframe/vr-cabinet (src/assets/vr-cabinet: a glTF file and its textures).
//
// When the table session starts, before the parts are set up for rendering, the cabinet is added to the played table as parts of its
// own: primitives in a part group of the playfield space (built upright, then placed as the former cabinet space placed them, see
// m_cabinetToPlayfield), with their images and materials. They are fitted to the table (CabinetFit: playfield width, length and slope, lockbar height,
// playfield depth under the lockbar) and removed at the end of the session; the table file never sees them.
//
// The playfield lies as deep under the lockbar as in a real cabinet (PinTable::m_cabinetGlassBottomHeight, for the VR spaces), not at
// the glass height of the table, and the sides wrap around the table's own side rails. The side walls of a desktop table, taller than any
// cabinet, are hidden (not removed: they still hold the ball in); their art stays, on side blades cut along the top edge of the cabinet.
// So is the lockbar of a desktop table, across the front edge and above the top of the cabinet's lockbar.
// When the table's plunger stands on a raised surface (a shooter lane on a wall), the playfield lies that much deeper, and the launcher
// rod comes out in line with the table's plunger rod. When a wall across the width marks the back of a desktop table's playfield, the
// cabinet is fitted to the playfield up to that wall, and the wall, when it stands above the cabinet, is cut along its top edge like the
// side blades. A playfield longer than the nominal one first reaches the front of the backbox, then makes the body longer (Stern's 45"
// playfields in a body about as long as a WPC's). The small parts left floating above the cabinet next to a hidden side are hidden too.
//
// The ball launcher matches the table (CabinetFit::ChooseLauncher): a spring plunger in front of the table's manual plunger, or a launch
// button when it only has automatic plungers. The plunger rod follows the table's plunger, the buttons move when pressed.
//
// The implicit VR backglass (B2S) moves to the backglass window of the backbox, and a score view display (DMD, segments) is added in the
// window of the speaker panel: a 4:1 DMD window, or a 16:9 full DMD screen when the table's PinUP Player pack draws one there
// (CabinetDisplays::DrawsFullDMD).
//
// A desktop table that draws its display with flashers of its own beside the playfield (JP Salas' tables: 40 digit flashers on a grid,
// to the left of the playfield for the desktop view), and uses FlexDMD only in cabinet mode, would show it floating outside the cabinet
// and leave the DMD window black. Such a display (FindDesktopDisplay) moves into the DMD window while nothing else draws there and it is
// shown, and goes back to its place if a renderer starts drawing in the window (MoveDesktopDisplay).
//
// The room chosen for the lobby (the game room by default, none for the empty room) is added around it, in the room space: its parts are
// moved from the lobby room's table (src/assets/lobby-*.vpx, steamframe/lobby-room) to the played one. Passthrough leaves it out.
//
// VPX_DEFAULT_CABINET=0 turns it off, =1 shows it out of a headset too (desktop tests), =2 on the tables made for VR too; VPX_DEFAULT_ROOM=0 leaves the room out.
class DefaultCabinet final
{
public:
   // nullptr when the table has its own VR room or cabinet, out of a headset, in the lobby, or when the model cannot be loaded
   static std::unique_ptr<DefaultCabinet> Create(Player* player);
   static bool IsEnabled(); // Unless turned off (VPX_DEFAULT_CABINET=0): the tables without a VR room get it in a headset
   explicit DefaultCabinet(Player* player);
   ~DefaultCabinet();

   void Update(); // Each frame: plunger rod and spring, buttons
   void Remove(); // At the end of the table session: the parts, images and materials leave the table

   VPX::CabinetFit::Launcher GetLauncher() const { return m_launcher; }
   bool HasFullDMD() const { return m_fullDMD; } // The speaker panel holds a 16:9 full DMD screen instead of a 4:1 DMD window
   bool IsOwnPart(const IEditable* part) const { return std::ranges::find(m_parts, part) != m_parts.end(); } // The cabinet's and the room's parts

   // The windows of the backbox, where the displays of PinUP Player go (CabinetDisplays): the backglass window, even when it keeps the
   // generic art, and the score view window of the speaker panel. Table coordinates (VPU), as a flasher placed there.
   struct Window
   {
      float x = 0.f, y = 0.f, z = 0.f; // Center
      float width = 0.f, height = 0.f;
      float rotX = -90.f; // Upright, leaning back
      bool valid = false;
   };
   const Window& GetBackglassWindow() const { return m_backglassWindow; }
   const Window& GetDisplayWindow() const { return m_displayWindow; }
   Flasher* GetDisplayFlasher() const { return m_displayFlasher; } // The table's score view (DMD, segments), hidden while PinUP Player shows its DMD there
   // The part group of the cabinet (in the playfield space): the displays placed on its windows join it
   PartGroup* GetCabinetGroup() const { return m_cabinetGroup; }

private:
   bool Load(const std::filesystem::path& folder);
   void LoadRoom();
   PartGroup* NewGroup(const std::string& name, PartGroupData::SpaceReference space);
   void LoadNode(const struct cgltf_data* data, const struct cgltf_node& node, const VPX::CabinetFit::Fit& fit, float launcherX, const std::string& folder);
   // A node or rectangle of the model for one speaker panel only ('vpx_display' extra: "dmd" or "full") is loaded with that panel only
   bool IsDisplayChosen(const std::string& display) const { return display.empty() || display == (m_fullDMD ? "full" : "dmd"); }
   Window GetWindow(const std::vector<float>& corners, const VPX::CabinetFit::Fit& fit) const;
   Flasher* CreateFlasher(const Window& w, int window, const std::string& name);
   void AddSideBlade(class Surface* wall, float sideTop, float tanTop);
   void FindDesktopDisplay();
   void UpdateDesktopDisplay();
   void MoveDesktopDisplay(bool intoWindow);
   Material* GetMaterial(const struct cgltf_data* data, const struct cgltf_material* material, const std::string& folder, std::string& image, float& unlit);

   enum class Role { None, PlungerRod, PlungerSpring, LaunchButton, FlipperLeft, FlipperRight, StartButton };
   struct Animated
   {
      Primitive* part;
      Role role;
      float axis[3]; // Direction of the move, table coordinates (VPU)
   };

   Player* const m_player;
   PartGroup* m_cabinetGroup = nullptr;
   // The cabinet is built upright, Z up from the floor (the coordinates of the cabinet space, before upstream merged it into the playfield
   // space, cca3bd8c8), then placed in the playfield space with this transform (see Attach)
   Matrix3D m_cabinetToPlayfield;
   float m_cabinetToPlayfieldRotX = 0.f; // Its rotation, around X, in degrees
   Vertex3Ds CabinetToPlayfield(const Vertex3Ds& p) const { return m_cabinetToPlayfield * p; }
   std::vector<IEditable*> m_parts; // Everything added to the table (groups included)
   std::vector<Texture*> m_images;
   std::vector<Material*> m_materials;
   std::vector<Animated> m_animated;
   struct HiddenPart
   {
      IEditable* part;
      bool sideVisible = false, topVisible = false; // Of a wall
   };
   std::vector<HiddenPart> m_hiddenParts; // The side walls of the desktop table, shown again at the end of the session
   Window m_backglassWindow, m_displayWindow;
   Flasher* m_displayFlasher = nullptr;
   // The display a desktop table draws with flashers beside the playfield, and where its parts go in the DMD window
   struct DesktopDisplayPart
   {
      Flasher* flasher;
      PartGroup* group; // Its own part group, back when it leaves the window
      Vertex2D center; // Its place
      float height, rotX, rotY, rotZ;
      Vertex2D windowCenter; // Its place in the window
      float windowHeight;
   };
   std::vector<DesktopDisplayPart> m_desktopDisplay;
   float m_desktopDisplayScale = 1.f;
   float m_desktopDisplayRotX = 0.f; // Of its flashers in the window
   enum class DesktopDisplayState { Outside, InWindow, Back } m_desktopDisplayState = DesktopDisplayState::Outside;
   int m_updates = 0;
   bool m_hidImplicitBackglass = false; // The implicit VR backglass, replaced by the one of the backbox
   std::vector<std::pair<const struct cgltf_material*, Material*>> m_materialMap;
   std::vector<std::pair<Material*, std::string>> m_materialImages;
   VPX::CabinetFit::Launcher m_launcher = VPX::CabinetFit::Launcher::Spring;
   bool m_fullDMD = false;
   Plunger* m_plunger = nullptr; // The manual plunger the rod follows
   float m_springLength = 0.f; // VPU
   float m_vpuPerMeter = 0.f;
   float m_centerX = 0.f, m_bottomY = 0.f; // Table coordinates of the model's origin (VPU)
};
