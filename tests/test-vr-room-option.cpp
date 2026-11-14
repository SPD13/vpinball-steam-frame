// license:GPLv3+

// VRRoomOption only depends on the standard library, so these tests do not need the VPX application. The scripts are lines of the tables
// of the Steam Frame library (surveyed 2026-11-14).
// Build alone: clang++ -std=c++20 -DDOCTEST_CONFIG_IMPLEMENT_WITH_MAIN -Ithird-party/include tests/test-vr-room-option.cpp src/steamframe/VRRoomOption.cpp

#ifndef _WIN32

#include "doctest.h"

#include "../src/steamframe/VRRoomOption.h"

using VPX::VRRoomOption::TurnOn;

TEST_CASE("VR room option: the off options of the library are turned on")
{
   SUBCASE("Tron Legacy (Stern 2011) VPW Mod: Const, CRLF")
   {
      const std::string script = "Option Explicit\r\n'*****VR Options *********\r\n"
                                 "Const VRRoom = 0 \t\t\t\t\t'0 - VR Room Off, 1 - Tron Room 2 - Minimal Room, 3 - Ultra Minimal\r\n"
                                 "Const Scratches = 1\r\nIf VRRoom <> 0 Then UseVPMDMD = True Else UseVPMDMD = DesktopMode\r\n";
      const auto change = TurnOn(script);
      REQUIRE(change.has_value());
      CHECK(change->line == 3);
      CHECK(change->option == "Const VRRoom = 0 \t\t\t\t\t'0 - VR Room Off, 1 - Tron Room 2 - Minimal Room, 3 - Ultra Minimal");
      std::string expected = script;
      expected.replace(expected.find("VRRoom = 0"), 10, "VRRoom = 1");
      CHECK(change->script == expected);
   }
   SUBCASE("Whirlwind 4K")
   {
      const auto change = TurnOn("Dim VRObject, CellarOpt\nConst VRRoom = 0\t\t\t\t'0 = Off, 1 = VR Room On\nIf VRRoom > 0 Then LoadVRRoom() Else SetNoVRRoom() : End If\n");
      REQUIRE(change.has_value());
      CHECK(change->line == 2);
      CHECK(change->script.find("Const VRRoom = 1\t") != std::string::npos);
   }
   SUBCASE("Getaway mod: declared, then assigned, with a sub before")
   {
      const auto change = TurnOn("Dim VRRoom \nSub Table1_Init\n  VRRoom = 2 '0 = off 1 = on\nEnd Sub\nVRRoom = 0        '0 = Desktop/FS    1 = VR Room\n");
      REQUIRE(change.has_value());
      CHECK(change->line == 5);
      CHECK(change->script.find("VRRoom = 1        '0 = Desktop") != std::string::npos);
   }
   SUBCASE("Dim and assignment on one line, VR_Room")
   {
      const auto change = TurnOn("Dim VR_Room : VR_Room = 0 ' 0 = Off, 1 = Room\n");
      REQUIRE(change.has_value());
      CHECK(change->script == "Dim VR_Room : VR_Room = 1 ' 0 = Off, 1 = Room\n");
   }
}

TEST_CASE("VR room option: the other tables are left alone")
{
   // TOTAN4k 1.5: 0 is a room
   CHECK_FALSE(TurnOn("Const VRRoom = 1\t\t\t\t\t' 0 = big room, 1 = small room\n").has_value());
   CHECK_FALSE(TurnOn("Const VRRoom = 0 ' 0 = big room, 1 = small room\n").has_value());
   // VR ROOM Maverick: already on
   CHECK_FALSE(TurnOn("Const VRRoom = 1\n").has_value());
   // No comment saying what 0 is
   CHECK_FALSE(TurnOn("Const VRRoom = 0\n").has_value());
   // VR Room Rocky: loaded from saved data, 0 is the first room
   CHECK_FALSE(TurnOn("VRRoom = LoadValue(\"Rocky\", \"V1.0.0\")\nSub VRChangeRoom()\n If VRRoom=\"\" Then\n  VRRoom = 0\n End if\nEnd Sub\n").has_value());
   // Avengers Pro: the script chooses with RenderingMode
   CHECK_FALSE(TurnOn("If RenderingMode = 2 Then\n VR_Room = 1\nElse\n VR_Room = 0 '0 = off, 1 = on\nEnd If\n").has_value());
   CHECK_FALSE(TurnOn("Dim VRRoom: VRRoom = 0 ' 0 = off, 1 = on\nIf Table1.RenderingMode=2 Then VRRoom = 1\n").has_value());
   // VRRoomChoice is the option of the tables that read RenderingMode
   CHECK_FALSE(TurnOn("Dim VRRoomChoice : VRRoomChoice = 0 '0 - VR Room Off, 1 - Minimal Room\n").has_value());
   // Only assigned inside a sub
   CHECK_FALSE(TurnOn("Sub Table1_Init()\n VRRoom = 0 '0 = Off, 1 = On\nEnd Sub\n").has_value());
}

TEST_CASE("VR room option: RenderingMode in a comment or a string does not count")
{
   CHECK(TurnOn("' This table does not use RenderingMode\nConst VRRoom = 0 '0 = Off, 1 = VR Room On\n").has_value());
   CHECK(TurnOn("Debug.Print \"x\" ' RenderingMode\nConst VRRoom = 0 '0 = Off, 1 = VR Room On\n").has_value());
}

#endif
