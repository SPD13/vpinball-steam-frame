// license:GPLv3+

#include "core/stdafx.h"
#include "FlasherOutline.h"
#include "math/MeshUtils.h"

namespace VPX::FlasherOutline
{

size_t DropStraightPoints(std::vector<RenderVertex>& outline)
{
   // The drag points are the points marked controlPoint, each with its own 'smooth'; the points between two of them were added along the curve
   const size_t n = outline.size();
   size_t first = n;
   for (size_t i = 0; i < n; i++)
      if (outline[i].controlPoint)
      {
         first = i;
         break;
      }
   if (first == n)
      return 0;
   std::vector<char> keep(n, 1);
   size_t dropped = 0;
   for (size_t c = first, visited = 0; visited < n;)
   {
      // Next drag point after c, around the loop
      size_t next = (c + 1) % n, steps = 1;
      while (!outline[next].controlPoint && steps < n)
      {
         next = (next + 1) % n;
         steps++;
      }
      if (!outline[c].smooth && !outline[next].smooth)
         for (size_t i = (c + 1) % n; i != next; i = (i + 1) % n)
         {
            keep[i] = 0;
            dropped++;
         }
      visited += steps;
      c = next;
   }
   if (dropped == 0)
      return 0;
   std::vector<RenderVertex> kept;
   kept.reserve(n - dropped);
   for (size_t i = 0; i < n; i++)
      if (keep[i])
         kept.push_back(outline[i]);
   outline.swap(kept);
   return dropped;
}

}
