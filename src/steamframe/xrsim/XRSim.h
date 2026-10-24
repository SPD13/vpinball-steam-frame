// license:GPLv3+

#pragma once

// Simulated OpenXR runtime of the macOS build (VPX_XR_SIM): a headset and two controllers whose poses and inputs are set by the
// application itself, so that the VR code (room, cabinet, UI panel, pointer, controller models) runs and can be seen on a Mac without
// any headset. The left eye is shown by the desktop display (VR desktop display mode Left).
//
// The OpenXR functions are defined in XRSim.cpp and linked directly (no loader). The graphics binding is XR_KHR_metal_enable in name
// only: the swapchain images are textures created by bgfx itself (see XRSimBackend.h), the runtime never reads them.
//
// Environment:
// - VPX_XRSIM_EYE=<width>x<height> per eye resolution (default 1920x1080)
// - VPX_XRSIM_FOV=<degrees> horizontal field of view of each eye (default 90), the vertical one follows the eye aspect ratio
// - VPX_XRSIM_FPS=<rate> frame rate of the simulated display (default 60)
// - VPX_XRSIM_CONTROLLERS=<folder> controller models: left.glb, right.glb (glTF binaries as a runtime gives them, see
//   VPX_DUMP_CONTROLLER_MODELS) served through XR_EXT_render_model
// - VPX_XRSIM_POSE=<x,y,z,yaw,pitch> initial head pose (meters, degrees; yaw positive to the left, pitch positive up)

#include <string>

class Player;

namespace XRSim
{
   // Reference space (stage): meters, y up, the player faces -z at yaw 0. Yaw turns left (counterclockwise seen from above), pitch looks up.
   struct Pose
   {
      float x = 0.f, y = 1.65f, z = 0.f;
      float yaw = 0.f, pitch = 0.f, roll = 0.f; // Degrees
   };

   enum Hand { Left = 0, Right = 1 };

   // Moves are eased over the given duration (seconds), 0 jumps. The head may also sway a little (amplitude in cm and degrees) to look
   // like a real head; the hands follow the sway of the head when attached (held in front of the player).
   void SetHeadPose(const Pose& pose, float duration);
   Pose GetHeadPose();
   void SetHeadSway(float amplitude);
   // Aim pose of a controller (the pointing ray goes along its -z). Hidden controllers are not tracked.
   void SetAimPose(Hand hand, const Pose& pose, float duration);
   void SetHandVisible(Hand hand, bool visible);
   // Points the aim of a controller at a position of the reference space, from where it is
   void AimAt(Hand hand, float x, float y, float z, float duration);
   // Value of an input of the interaction profile, by path ("/user/hand/right/input/trigger/value", ".../a/click"): 0 or 1 for buttons
   void SetInput(const std::string& path, float value);
   float GetInput(const std::string& path);

   bool IsActive(); // A simulated instance exists

   // Scripted session (XRSimScript.cpp): called each frame by the in-game UI, and by the VR device when it places the UI panel
   void UpdateScript(Player* player);
   void SetUIPanel(const float origin[3], const float right[3], const float down[3]);
}
