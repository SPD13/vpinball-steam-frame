// license:GPLv3+

#pragma once

#include "CabinetFit.h"
#include "parts/PartGroup.h"

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
// The ball launcher matches the table (CabinetFit::ChooseLauncher): a spring plunger in front of the table's manual plunger, or a launch
// button when it only has automatic plungers. The plunger rod follows the table's plunger, the buttons move when pressed.
//
// The implicit VR backglass (B2S) moves to the backglass window of the backbox, and a score view display (DMD, segments) is added in the
// window of the speaker panel.
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
   explicit DefaultCabinet(Player* player);
   ~DefaultCabinet();

   void Update(); // Each frame: plunger rod and spring, buttons
   void Remove(); // At the end of the table session: the parts, images and materials leave the table

   VPX::CabinetFit::Launcher GetLauncher() const { return m_launcher; }

private:
   bool Load(const std::filesystem::path& folder);
   void LoadRoom();
   PartGroup* NewGroup(const std::string& name, PartGroupData::SpaceReference space);
   void LoadNode(const struct cgltf_data* data, const struct cgltf_node& node, const VPX::CabinetFit::Fit& fit, float launcherX, const std::string& folder);
   Flasher* CreateFlasher(const std::vector<float>& corners, const VPX::CabinetFit::Fit& fit, int window, const std::string& name);
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
   bool m_hidImplicitBackglass = false; // The implicit VR backglass, replaced by the one of the backbox
   std::vector<std::pair<const struct cgltf_material*, Material*>> m_materialMap;
   std::vector<std::pair<Material*, std::string>> m_materialImages;
   VPX::CabinetFit::Launcher m_launcher = VPX::CabinetFit::Launcher::Spring;
   Plunger* m_plunger = nullptr; // The manual plunger the rod follows
   float m_springLength = 0.f; // VPU
   float m_vpuPerMeter = 0.f;
   float m_centerX = 0.f, m_bottomY = 0.f; // Table coordinates of the model's origin (VPU)
};
