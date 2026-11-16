// license:GPLv3+

#include "CabinetFit.h"

#include <algorithm>
#include <cmath>

namespace VPX::CabinetFit
{

static constexpr float kDegToRad = 3.14159265358979f / 180.f;

// Tables can hold any value (desktop tables were never made for a cabinet): keep them to what a pinball machine can be
static float Sanitize(float value, float fallback, float low, float high)
{
   if (!(value > 0.f) || !std::isfinite(value))
      return fallback;
   return std::clamp(value, low, high);
}

Fit::Fit(const Nominal& nominal, const Table& table)
   : m_nominal(nominal)
{
   m_table.playfieldWidth = Sanitize(table.playfieldWidth, nominal.playfieldWidth, 0.40f, 0.70f);
   m_table.playfieldLength = Sanitize(table.playfieldLength, nominal.playfieldLength, 0.80f, 1.40f);
   m_table.slope = Sanitize(table.slope, nominal.slope, 2.f, 12.f);
   m_table.lockbarTop = Sanitize(table.lockbarTop, nominal.lockbarTop, 0.60f, 1.30f);
   m_table.glassBottom = Sanitize(table.glassBottom, nominal.glassBottom, 0.02f, 0.20f);
   // The top edge keeps its angle to the playfield (the glass flares toward the back the same way), or takes the one asked for
   const float modelAngle = std::atan2(nominal.zBackTop - nominal.zFrontTop, nominal.yFlat - nominal.yFront) - nominal.slope * kDegToRad;
   m_table.topAngle = table.topAngle > 0.f ? std::clamp(table.topAngle, 0.f, 10.f) : modelAngle / kDegToRad;
   m_table.launcherLift = std::clamp(table.launcherLift, 0.f, m_table.glassBottom);
   m_tanTop = std::tan((m_table.slope + m_table.topAngle) * kDegToRad);
}

float Fit::MapX(float x) const
{
   const float ax = std::fabs(x), sign = x < 0.f ? -1.f : 1.f;
   const float keep = m_nominal.fitXKeep, side = m_nominal.fitXSide;
   const float d = 0.5f * (m_table.playfieldWidth - m_nominal.playfieldWidth);
   if (ax <= keep)
      return x;
   if (ax >= side)
      return sign * (ax + d);
   return sign * (keep + (ax - keep) * (side + d - keep) / (side - keep));
}

float Fit::MapY(float y) const
{
   const float front = m_nominal.fitYFront, back = m_nominal.fitYBack;
   const float d = m_table.playfieldLength - m_nominal.playfieldLength;
   if (y <= front)
      return y;
   if (y >= back)
      return y + d;
   return front + (y - front) * (back + d - front) / (back - front);
}

float Fit::TopZ(float y) const
{
   const float t = std::clamp((y - m_nominal.yFront) / (m_nominal.yFlat - m_nominal.yFront), 0.f, 1.f);
   return m_nominal.zFrontTop + t * (m_nominal.zBackTop - m_nominal.zFrontTop);
}

float Fit::TargetTopZ(float mappedY) const
{
   const float y = std::clamp(mappedY, m_nominal.yFront, MapY(m_nominal.yFlat));
   return m_nominal.zFrontTop + (y - m_nominal.yFront) * m_tanTop;
}

Point Fit::Deform(const Point& p) const
{
   const float y = MapY(p[1]);
   float z = p[2];
   if (z >= m_nominal.zBottom)
   {
      // The top edge follows the table; the bottom stays level, in between the shift is shared
      const float top = TopZ(p[1]);
      const float w = std::clamp((z - m_nominal.zBottom) / (top - m_nominal.zBottom), 0.f, 1.f);
      z += w * (TargetTopZ(y) - top) + HeightShift();
   }
   else // Legs: stretched for the feet to stay on the floor
      z *= (m_nominal.zBottom + HeightShift()) / m_nominal.zBottom;
   return { MapX(p[0]), y, z };
}

Point Fit::RigidOffset(const Point& anchor) const
{
   const Point moved = Deform(anchor);
   return { moved[0] - anchor[0], moved[1] - anchor[1], moved[2] - anchor[2] };
}

Point Fit::LauncherOffset(float x) const
{
   const Point& a = m_nominal.plunger;
   // The rod stays at the same depth under the table's playfield (or the raised surface its plunger stands on) as under the nominal one,
   // on the front panel, above the legs, under the lip of the lockbar
   const float depth = (m_nominal.lockbarTop - m_nominal.glassBottom) - a[2];
   const float frontTop = m_nominal.zFrontTop + HeightShift();
   const float low = std::max(m_nominal.zBottom + HeightShift() + 0.06f, m_nominal.legTop + HeightShift() + 0.03f);
   const float z = std::clamp(m_table.lockbarTop - m_table.glassBottom + m_table.launcherLift - depth, low, std::max(low, frontTop - 0.04f));
   return { x - a[0], MapY(a[1]) - a[1], z - a[2] };
}

LauncherChoice ChooseLauncher(const std::vector<PlungerPart>& plungers)
{
   LauncherChoice choice;
   const auto rightmost = [&](bool autoPlunger) -> std::optional<float>
   {
      std::optional<float> x;
      for (const PlungerPart& p : plungers)
         if (p.autoPlunger == autoPlunger && (!x || p.x > *x))
            x = p.x;
      return x;
   };
   if (const std::optional<float> manual = rightmost(false))
   {
      choice.launcher = Launcher::Spring;
      choice.x = manual;
   }
   else
   {
      choice.launcher = Launcher::Button;
      choice.x = rightmost(true);
   }
   return choice;
}

float LauncherX(const LauncherChoice& choice, const Fit& fit, const Nominal& nominal)
{
   const float x = choice.x ? *choice.x : fit.MapX(nominal.plunger[0]);
   // Between the coin door and the side, on the side of the table's plunger (a few tables have it on the left)
   constexpr float coinDoorHalf = 0.21f, sideMargin = 0.04f;
   const float outer = std::max(coinDoorHalf, fit.OuterHalfWidth() - sideMargin);
   const float ax = std::clamp(std::fabs(x), coinDoorHalf, outer);
   return x < 0.f ? -ax : ax;
}

}
