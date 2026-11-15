// license:GPLv3+

// CabinetFit only depends on the standard library, so these tests do not need the VPX application.
// Build alone: clang++ -std=c++20 -DDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN -Ithird-party/include tests/test-cabinet-fit.cpp src/steamframe/CabinetFit.cpp

#ifndef _WIN32

#include "doctest.h"

#include "../src/steamframe/CabinetFit.h"

using namespace VPX::CabinetFit;

static Table NominalTable(const Nominal& n)
{
   return { n.playfieldWidth, n.playfieldLength, n.slope, n.lockbarTop, n.glassBottom };
}

TEST_CASE("Cabinet fit: the nominal table leaves the model as it is")
{
   const Nominal n;
   const Fit fit(n, NominalTable(n));
   for (const Point p : { Point { 0.f, 0.f, 0.f }, Point { -0.2794f, -0.0381f, 0.894f }, Point { 0.2794f, 1.27f, 1.094f }, Point { 0.1f, 1.2f, 1.8f },
           Point { 0.28f, 0.5f, 0.3f } })
   {
      const Point q = fit.Deform(p);
      CHECK(q[0] == doctest::Approx(p[0]).epsilon(1e-5));
      CHECK(q[1] == doctest::Approx(p[1]).epsilon(1e-5));
      CHECK(q[2] == doctest::Approx(p[2]).epsilon(1e-5));
   }
   const Point o = fit.LauncherOffset(n.plunger[0]);
   CHECK(o[0] == doctest::Approx(0.f));
   CHECK(o[1] == doctest::Approx(0.f));
   CHECK(o[2] == doctest::Approx(0.f).epsilon(1e-4));
}

TEST_CASE("Cabinet fit: a wide body moves the sides, keeps the coin door and stretches between")
{
   const Nominal n;
   Table t = NominalTable(n);
   t.playfieldWidth = n.playfieldWidth + 0.0762f; // Widebody: 3" wider
   const Fit fit(n, t);
   CHECK(fit.MapX(0.1f) == doctest::Approx(0.1f));           // Coin door
   CHECK(fit.MapX(-0.1f) == doctest::Approx(-0.1f));
   CHECK(fit.MapX(0.2794f) == doctest::Approx(0.2794f + 0.0381f)); // Outside of the right side
   CHECK(fit.MapX(-0.2794f) == doctest::Approx(-0.2794f - 0.0381f));
   const float mid = fit.MapX(0.5f * (n.fitXKeep + n.fitXSide));
   CHECK(mid > 0.5f * (n.fitXKeep + n.fitXSide));
   CHECK(mid < 0.5f * (n.fitXKeep + n.fitXSide) + 0.0381f);
}

TEST_CASE("Cabinet fit: a longer playfield moves the back, keeps the front, and the top edge follows the slope")
{
   const Nominal n;
   Table t = NominalTable(n);
   t.playfieldLength = n.playfieldLength + 0.0762f; // 45"
   const Fit fit(n, t);
   CHECK(fit.MapY(n.yFront) == doctest::Approx(n.yFront));
   CHECK(fit.MapY(1.27f) == doctest::Approx(1.27f + 0.0762f)); // Back face
   // The front top edge stays, the back top edge rises with the longer slope
   const Point front = fit.Deform({ 0.2794f, n.yFront, n.zFrontTop });
   CHECK(front[2] == doctest::Approx(n.zFrontTop));
   const Point back = fit.Deform({ 0.2794f, n.yFlat, n.zBackTop });
   const float rise = (n.zBackTop - n.zFrontTop) / (n.yFlat - n.yFront);
   CHECK(back[2] == doctest::Approx(n.zFrontTop + (n.yFlat + 0.0762f - n.yFront) * rise).epsilon(1e-4));
   // The bottom stays level, and the backbox moves up as a whole
   CHECK(fit.Deform({ 0.2794f, 1.27f, n.zBottom })[2] == doctest::Approx(n.zBottom));
   const float lift = back[2] - n.zBackTop;
   CHECK(fit.Deform({ 0.f, 1.2f, 1.6f })[2] == doctest::Approx(1.6f + lift).epsilon(1e-4));
}

TEST_CASE("Cabinet fit: a steeper table raises the back")
{
   const Nominal n;
   Table t = NominalTable(n);
   t.slope = n.slope + 2.f;
   const Fit fit(n, t);
   CHECK(fit.Deform({ 0.f, 1.2f, 1.6f })[2] > 1.6f + 0.03f);
   CHECK(fit.Deform({ 0.f, n.yFront, n.zFrontTop })[2] == doctest::Approx(n.zFrontTop));
}

TEST_CASE("Cabinet fit: the lockbar height moves the body and stretches the legs to the floor")
{
   const Nominal n;
   Table t = NominalTable(n);
   t.lockbarTop = n.lockbarTop + 0.05f;
   const Fit fit(n, t);
   CHECK(fit.Deform({ 0.f, n.yFront, n.zFrontTop })[2] == doctest::Approx(n.zFrontTop + 0.05f));
   CHECK(fit.Deform({ 0.f, n.yFront, n.zBottom })[2] == doctest::Approx(n.zBottom + 0.05f));
   CHECK(fit.Deform({ 0.3f, n.yFront, 0.f })[2] == doctest::Approx(0.f)); // Feet on the floor
   CHECK(fit.Deform({ 0.3f, n.yFront, 0.5f * n.zBottom })[2] == doctest::Approx(0.5f * (n.zBottom + 0.05f)));
}

TEST_CASE("Cabinet fit: odd table values are kept to a real cabinet")
{
   const Nominal n;
   const Fit fit(n, { 0.f, -1.f, 90.f, 50.f, 0.f });
   CHECK(fit.GetTable().playfieldWidth == doctest::Approx(n.playfieldWidth));
   CHECK(fit.GetTable().playfieldLength == doctest::Approx(n.playfieldLength));
   CHECK(fit.GetTable().slope == doctest::Approx(12.f));
   CHECK(fit.GetTable().lockbarTop == doctest::Approx(1.3f));
   CHECK(fit.GetTable().glassBottom == doctest::Approx(n.glassBottom));
}

TEST_CASE("Cabinet fit: the launcher follows the table's playfield depth, within the front panel")
{
   const Nominal n;
   Table t = NominalTable(n);
   t.glassBottom = n.glassBottom + 0.02f; // Playfield 2 cm deeper under the lockbar
   const Fit fit(n, t);
   CHECK(fit.LauncherOffset(n.plunger[0])[2] == doctest::Approx(-0.02f).epsilon(1e-4));
   t.glassBottom = 0.2f; // Far too deep: stays above the legs
   const Fit deep(n, t);
   CHECK(n.plunger[2] + deep.LauncherOffset(n.plunger[0])[2] >= n.legTop + 0.03f - 1e-5f);
}

TEST_CASE("Launcher: a spring plunger in front of the manual plunger, a button for the automatic ones")
{
   SUBCASE("Manual plunger only")
   {
      const LauncherChoice c = ChooseLauncher({ { 0.23f, false } });
      CHECK(c.launcher == Launcher::Spring);
      CHECK(*c.x == doctest::Approx(0.23f));
   }
   SUBCASE("Manual plunger and an automatic one (ball saves): the spring plunger, at the manual one")
   {
      const LauncherChoice c = ChooseLauncher({ { 0.24f, true }, { 0.22f, false } });
      CHECK(c.launcher == Launcher::Spring);
      CHECK(*c.x == doctest::Approx(0.22f));
   }
   SUBCASE("Automatic plunger only: launch button")
   {
      const LauncherChoice c = ChooseLauncher({ { 0.235f, true } });
      CHECK(c.launcher == Launcher::Button);
      CHECK(*c.x == doctest::Approx(0.235f));
   }
   SUBCASE("No plunger (kicker launch): launch button at the usual place")
   {
      const LauncherChoice c = ChooseLauncher({});
      CHECK(c.launcher == Launcher::Button);
      CHECK(!c.x.has_value());
   }
   SUBCASE("Two manual plungers: the rightmost")
   {
      const LauncherChoice c = ChooseLauncher({ { -0.2f, false }, { 0.21f, false } });
      CHECK(*c.x == doctest::Approx(0.21f));
   }
}

TEST_CASE("Launcher: kept on the front between the coin door and the side")
{
   const Nominal n;
   const Fit fit(n, NominalTable(n));
   CHECK(LauncherX({ Launcher::Spring, 0.237f }, fit, n) == doctest::Approx(0.237f));
   CHECK(LauncherX({ Launcher::Spring, 0.05f }, fit, n) == doctest::Approx(0.21f));  // Would sit on the coin door
   CHECK(LauncherX({ Launcher::Spring, 0.4f }, fit, n) == doctest::Approx(0.2794f - 0.04f)); // Off the side
   CHECK(LauncherX({ Launcher::Spring, -0.23f }, fit, n) == doctest::Approx(-0.23f)); // Left shooter lane
   CHECK(LauncherX({ Launcher::Button, std::nullopt }, fit, n) == doctest::Approx(n.plunger[0]));
}

#endif
