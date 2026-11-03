// license:GPLv3+

#pragma once

// Steam Frame Edition: what the reflections leave out, and their size, switched live in the Reflections section of the Steam Frame screen of the VR menu
// (Settings Player/ExtraMirrors and Player/RoomReflection: Off by default in the build of the Steam Frame, On elsewhere; Player/ReflectionSize: Half
// in the build of the Steam Frame, Full elsewhere; docs/Performance on the Steam Frame.md, sections 8.6 and 8.8).
//
// A reflection draws the scene again every frame. What it costs depends mostly on the parts drawn, on the GPU and on the render thread, so leaving parts out
// saves a lot on the tables where these parts are heavy. Its size counts too where the GPU is the limit: half size per axis saves 0.45 ms per frame on
// Darkest Dungeon at 2160 x 2160, and halves its frames over the frame period, for a softer reflection (0.2 to 0.5 ms on Addams and Ghostbusters).
// - Extra mirrors: the planar reflections other than the playfield's, usually on the walls of a cabinet. Robot Invasion spends 11 ms per frame on three of them,
//   2001 spends 5 ms on two. Horizontal ones stay, as they are the reflection of another playfield (Game of Thrones).
// - Room reflection: the parts that reach beyond the cabinet in a reflection, which on tables with large VR rooms are most of what it draws: 2.7 to 3.3 ms on WildWest,
//   5 ms on Maverick. The culling of the reflection (RenderProbe::IsBoxReflected) cannot leave them out, as the box of a mesh as large as the room reaches the playfield.
//
// Automatic leaves a table as it is while it holds its full resolution. Once it did not for 10 of the last 60 seconds, the first minute of the table not
// counted, the extra mirrors go, then after 10 more such seconds the room, for the rest of the table.

#include "math/matrix.h"

class PinTable;
class RenderProbe;
class Settings;

namespace VPX::Reflections
{

// The values of the settings, in the order of their options
enum Mode { On = 0, Off = 1, Automatic = 2 };

void Load(const Settings& settings); // Table start: the settings, and the automatic mode decides again
int GetExtraMirrorsMode();
void SetExtraMirrorsMode(int mode); // From the menu, applied at once
int GetRoomReflectionMode();
void SetRoomReflectionMode(int mode); // From the menu, applied at once

// Size of the plane reflections (Settings Player/ReflectionSize, in the order of its options)
enum Size { Full = 0, Half = 1 };
int GetReflectionSize();
void SetReflectionSize(int size); // From the menu: the render targets of the probes are made again at their next render
float GetProbeScale(); // Of the plane reflection probes, per axis
unsigned int GetSizeRevision(); // Changes with the size, for the probes to make their render targets again

// Tells if a probe is to be rendered at all: false for an extra mirror left out, whose parts then show no reflection
bool IsProbeShown(const RenderProbe& probe);
// Tells if a part with this box, while it is drawn in a reflection, is left out as a part of the room
bool IsPartLeftOut(const Vertex3Ds& boxMin, const Vertex3Ds& boxMax, const Matrix3D& model, const PinTable& table);

// Each frame played in VR: decides the automatic mode. True while a part of it is still undecided (the foveated anti-aliasing, which is seen more, waits for it)
bool OnPlayedFrame(bool belowFullResolution, float frameMs);

// One line each for the menu
string GetExtraMirrorsStatus();
string GetRoomReflectionStatus();

}
