// license:GPLv3+

#pragma once

// Steam Frame Edition: the straight edges of a flasher kept as one edge each.
//
// IHaveDragPoints::GetRgVertex cuts each edge of a part's outline into pieces until FlatWithAccuracy finds every piece straight (twice the
// area of the triangle made with the piece's middle point, squared, under 4, in floats). Far from the table, the rounding of the middle
// point alone fails that test: the straight edges of the pulsing blue floor of "Tron Legacy (Stern 2011) VPW Mod" (VRBlueFloor, 4 drag
// points, a square of 1.2 million VP units) came out as 25,622 points in a row, and the floor as about 25,000 long, thin triangles spread
// over its 660 m. The Steam Frame's GPU took 36 ms per frame to draw them at 2160 x 2160 (6.3 ms for the whole table without its room):
// with this fix, the table with its room takes 7.8 ms and keeps its full resolution (headless series of 2026-11-14,
// docs/VR Rooms on the Steam Frame.md, section 9).
//
// Between two drag points that are not smooth, GetRgVertex's curve is the straight segment between them (its Catmull-Rom curve is built
// with each end repeated), so the points it adds there all lie on that segment: the outline is the same without them. Only the drawn
// polygon of a flasher changes (Flasher::RenderSetup); its hit testing, the editor and the other parts keep IHaveDragPoints' outline.
// The tables' own flashers, near the table, get no extra point: nothing changes for them.

#include <cstddef>
#include <vector>

class RenderVertex;

namespace VPX::FlasherOutline
{

// Removes from an outline made by IHaveDragPoints::GetRgVertex (a loop) the points it added between two drag points that are not
// smooth. Returns how many were removed.
size_t DropStraightPoints(std::vector<RenderVertex>& outline);

}
