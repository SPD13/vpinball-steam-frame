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
// own: primitives in two part groups placed in the VR cabinet spaces (the legs in the 'cabinet feet' space, which stretches them to the
// floor), with their images and materials. They are fitted to the table (CabinetFit: playfield width, length and slope, lockbar height,
// playfield depth under the lockbar) and removed at the end of the session; the table file never sees them.
//
// The playfield lies as deep under the lockbar as in a real cabinet (PinTable::m_cabinetGlassBottomHeight, for the VR spaces), not at
// the glass height of the table, and the sides wrap around the table's own side rails. The side walls of a desktop table, taller than any
// cabinet, are hidden (not removed: they still hold the ball in); their art stays, on side blades cut along the top edge of the cabinet.
//
// The ball launcher matches the table (CabinetFit::ChooseLauncher): a spring plunger in front of the table's manual plunger, or a launch
// button when it only has automatic plungers. The plunger rod follows the table's plunger, the buttons move when pressed.
//
// The implicit VR backglass (B2S) moves to the backglass window of the backbox, and a score view display (DMD, segments) is added in the
// window of the speaker panel: a 4:1 DMD window, or a 16:9 full DMD screen when the table's PinUP Player pack draws one there
// (CabinetDisplays::DrawsFullDMD).
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
   // The part group in the VR cabinet space: the windows are in its coordinates, so the displays placed on them must join it
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
   PartGroup* m_feetGroup = nullptr;
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
