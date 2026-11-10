// license:GPLv3+

#pragma once

// Steam Frame Edition: passthrough, the real room of the player around the cabinet instead of the virtual room of the lobby and the tables
// (Setting Standalone/Passthrough, toggled by the Passthrough input action, Y on the Steam Frame by default, and in the Room screen of the menu).
//
// - The headset's runtime blends the frames over its passthrough when they are submitted with XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
//   (VRDevice::GetFrameBlendMode). The frames are made transparent where nothing was drawn by the post pass of the color keyed passthrough
//   (fs_pp_passthrough.sc: alpha 0 where the depth is cleared), and the projection layer already blends on alpha.
// - VPX knows no room or cabinet, only parts. The parts of the room are found when the table starts (ClassifyTable), by their part group or
//   collection, as the lobby does for the room of a table (Launcher::GetRoomParts), and by where they are: a part much larger than a cabinet, or
//   entirely outside the volume of the cabinet, is a part of the room. The renderer leaves them out (Renderer::RenderItem), whatever the script
//   does with them. Exceptions per table are listed in assets/passthrough.json (and passthrough.json of the preferences folder).
// - The visibility mask of the parts groups is the one of mixed reality, so tables made with a mixed reality room show it as their authors meant.
//
// Test hook: VPX_PASSTHROUGH_PREVIEW=1 leaves the room out on a desktop too (no passthrough there, the background is black), and logs the
// parts found, for checking the classification of a table library on a computer.

class IEditable;
class PinTable;

namespace VPX::Passthrough
{

void Load(); // The saved setting: read by the first query (at the latest when the first table starts), so the application starts as it was left
bool IsWanted(); // The setting
void SetWanted(bool wanted); // From the menu or the input action: applied at once, saved
bool Toggle(); // The input action: returns the new state

// What the runtime of the headset offers, told by VRDevice once it knows its blend modes
void SetRuntimeSupport(bool alphaBlend);
bool IsKnown(); // A headset was started, IsSupported is meaningful
bool IsSupported();
bool IsActive(); // Wanted (or forced) and supported: frames blended over the passthrough, room left out (in VR)
// The cabinet positioning mode shows the passthrough while the cabinet is placed, whatever the setting, which it does not change
void SetForced(bool forced);
bool IsPreview(); // VPX_PASSTHROUGH_PREVIEW: room left out on a desktop

// The rules by name, shared with the lobby (Launcher::GetRoomParts): a part in a part group using the room space reference or named after the
// room (layers of tables made before part groups are loaded as part groups), and a collection named 'VR...' which is not the cabinet
bool IsInRoomGroup(const IEditable* part);
bool IsRoomCollectionName(const string& collectionName);

// Table start (Renderer constructor): finds the parts of the room of the played table
void ClassifyTable(PinTable* table);
bool IsRoom(const IEditable* part);
void Clear();

// One line for the menu, and for the notification of the input action
string GetStatus();

}
