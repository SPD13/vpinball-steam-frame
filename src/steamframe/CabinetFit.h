// license:GPLv3+

#pragma once

// Steam Frame Edition: the default VR cabinet (see DefaultCabinet.h) fitted to a table, and the choice of its ball launcher. Standard
// library only (tests/test-cabinet-fit.cpp).
//
// The cabinet model (steamframe/vr-cabinet, a Williams WPC standard body) is drawn around a nominal playfield, in meters, with X to the
// player's right, Y away from the player and Z up; X = 0 is the middle of the playfield, Y = 0 its front edge and Z = 0 the floor. A
// table has its own playfield width, length and slope, lockbar height and playfield depth under the lockbar: the model follows them by
// zones, so that the parts keep their shape:
// - across, the middle (the coin door) keeps its size, the sides move by half the difference of width, and the band between them
//   is stretched;
// - lengthwise, the front (lockbar, buttons, coin door, front legs) keeps its place, the back (backbox, back legs) moves by the
//   difference of length, and the middle (the sides) is stretched;
// - in height, the whole cabinet follows the table's lockbar height; the top of the sides rises from the front along the table's slope
//   (plus the flare of the glass of the model, or the angle the table asks for), so the top edge at the back and the backbox move up or
//   down; the bottom stays level.
// Parts that must not be stretched at all (the start button, the launchers) are 'rigid': they move as a whole with their anchor.

#include <array>
#include <optional>
#include <vector>

namespace VPX::CabinetFit
{

using Point = std::array<float, 3>;

// Figures of the model (saved by steamframe/vr-cabinet/tools/build_cabinet.py in the glTF scene extras, see dims.py)
struct Nominal
{
   float playfieldWidth = 0.51435f;
   float playfieldLength = 1.0668f;
   float slope = 6.5f; // Degrees
   float lockbarTop = 0.91f; // Above the floor
   float glassBottom = 0.0762f; // Lockbar top to playfield front edge
   float halfWidth = 0.2794f; // Outside of the sides
   float legTop = 0.7289f; // Top of the legs
   float yFront = -0.0381f; // Front face of the body
   float zBottom = 0.4971f; // Bottom of the body
   float zFrontTop = 0.894f; // Top edge of the body at the front ...
   float yFlat = 1.0890f; // ... rising up to there, then flat ...
   float zBackTop = 1.094f; // ... at that height
   float fitXKeep = 0.175f; // Zones, see above
   float fitXSide = 0.2552f;
   float fitYFront = 0.10f;
   float fitYBack = 1.0668f;
   Point plunger { 0.237f, -0.0381f, 0.806f }; // Where the launchers are drawn (rod hole on the front face)
};

// The table, in the same units (see DefaultCabinet::Create)
struct Table
{
   float playfieldWidth = 0.f;
   float playfieldLength = 0.f;
   float slope = 0.f; // Degrees
   float lockbarTop = 0.f;
   float glassBottom = 0.f;
   float topAngle = 0.f; // Degrees, of the top edge of the sides to the playfield: 0 for the model's (DefaultCabinet raises it along the table's own side rails)
   float launcherRod = -1.f; // Height above the playfield of the axis of the table's plunger rod (the surface it stands on included), where the launcher's
                             // rod comes out. Negative: no plunger pulled by the player, the launcher keeps the nominal depth under the playfield
};

class Fit
{
public:
   Fit(const Nominal& nominal, const Table& table);

   // A point of a deformed part (zones above)
   Point Deform(const Point& p) const;
   // The move of a rigid part, from its anchor
   Point RigidOffset(const Point& anchor) const;
   // The move of a launcher drawn at Nominal::plunger to the table's shooter lane (x: center of the rod, model units)
   Point LauncherOffset(float x) const;

   // The table as used (sanitized: tables can hold odd values)
   const Table& GetTable() const { return m_table; }
   float MapX(float x) const;
   float MapY(float y) const;
   float OuterHalfWidth() const { return MapX(m_nominal.halfWidth); } // Outside face of a side
   float HeightShift() const { return m_table.lockbarTop - m_nominal.lockbarTop; }

private:
   float TopZ(float y) const; // Top edge of the sides of the model
   float TargetTopZ(float mappedY) const; // Same, fitted

   Nominal m_nominal;
   Table m_table;
   float m_tanTop = 0.f; // Slope of the top edge, fitted
};

// The ball launcher of the cabinet, from the plungers of the table
enum class Launcher
{
   Spring, // A plunger the player pulls
   Button, // A launch button: the table only has automatic plungers (or none)
};
struct PlungerPart
{
   float x = 0.f; // Center of the rod, model units (0 = middle of the playfield)
   bool autoPlunger = false; // 'Auto Plunger' option: fired by the script or a button, the player cannot pull it
};
struct LauncherChoice
{
   Launcher launcher = Launcher::Spring;
   std::optional<float> x; // Center of the launcher, when the table tells (its plunger)
};
// The rightmost manual plunger makes a spring plunger in front of it. Without one, a launch button, in front of the automatic plunger if
// there is one.
LauncherChoice ChooseLauncher(const std::vector<PlungerPart>& plungers);
// Where the launcher is drawn: the table's plunger, kept on the front between the coin door and the side (or the nominal place)
float LauncherX(const LauncherChoice& choice, const Fit& fit, const Nominal& nominal);

}
