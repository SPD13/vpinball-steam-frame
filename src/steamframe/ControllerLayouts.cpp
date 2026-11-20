// license:GPLv3+

#include "core/stdafx.h"
#include "ControllerLayouts.h"

#include "core/VPApp.h"
#include "input/InputManager.h"
#include "input/PlungerHandler.h"
#include "physics/cabinet/GamepadNudge.h"
#include "steamframe/PinOneDevices.h"

#include <SDL3/SDL.h>

#include <unordered_map>

namespace VPX::ControllerLayouts
{

static constexpr uint16_t NONE = 0xFFFF;

// The inputs of a controller the PinOne layout uses, by the Xbox control the PinOne sends them as (its Bluetooth Xbox mode; on USB, the
// raw button numbers of the same PinOne inputs). Kept by settings id, which stays the same from one table to the next, while the
// InputManager and its device ids are made again for each table.
struct PinOneInputs
{
   uint16_t lb, rb, leftTrigger, rightTrigger; // Flippers, magna-saves
   bool triggersAreAxes; // Xbox: axes, pressed past -0.3 (they rest at -1), as in upstream's gamepad layout; USB: buttons
   uint16_t a, b, start, y, view; // Launch ball and start, start, start (the fire button), coin, menu
   uint16_t dpadLeft, dpadDown, dpadUp, dpadRight; // Menu navigation, and the PinMAME service buttons with dpadServiceButtons
   uint16_t plungerAxis, nudgeXAxis, nudgeYAxis;
   uint16_t x; // Front button 7: free in the game, previous item in the menus
   // The D-pad also presses the PinMAME service buttons (as upstream's gamepad layout). Not in Xbox mode: the PinOne's tilt buttons may be
   // set to its D-pad inputs (25 to 28), and a hard nudge then pressed a service button, which changes the volume in a WPC game or opens
   // its test menu (docs/steam-frame/PinOne Integration.md, section 5)
   bool dpadServiceButtons = true;
};

static std::unordered_map<string, PinOneInputs> s_pinOneInputs;

// The element id VPX gives the physical input bound to a gamepad button or axis (as SDLInputHandler::GetGamepadButton/Axis do)
static uint16_t GamepadButton(SDL_GamepadButton button, SDL_GamepadBinding** bindings, int count)
{
   for (int i = 0; i < count; i++)
   {
      if (bindings[i]->output_type != SDL_GAMEPAD_BINDTYPE_BUTTON || bindings[i]->output.button != button)
         continue;
      if (bindings[i]->input_type == SDL_GAMEPAD_BINDTYPE_BUTTON)
         return static_cast<uint16_t>(bindings[i]->input.button);
      if (bindings[i]->input_type == SDL_GAMEPAD_BINDTYPE_HAT)
      {
         const uint16_t hat = static_cast<uint16_t>(bindings[i]->input.hat.hat * 4);
         switch (bindings[i]->input.hat.hat_mask)
         {
         case SDL_HAT_LEFT: return 0x0100 | hat;
         case SDL_HAT_RIGHT: return 0x0101 | hat;
         case SDL_HAT_UP: return 0x0102 | hat;
         case SDL_HAT_DOWN: return 0x0103 | hat;
         default: break;
         }
      }
   }
   return NONE;
}

static uint16_t GamepadAxis(SDL_GamepadAxis axis, SDL_GamepadBinding** bindings, int count)
{
   for (int i = 0; i < count; i++)
      if (bindings[i]->output_type == SDL_GAMEPAD_BINDTYPE_AXIS && bindings[i]->output.axis.axis == axis && bindings[i]->input_type == SDL_GAMEPAD_BINDTYPE_AXIS)
         return 0x0200 | static_cast<uint16_t>(bindings[i]->input.axis.axis);
   return NONE;
}

void OnJoystickAdded(InputManager& input, uint16_t deviceId, SDL_Joystick* joystick)
{
   const string& settingsId = input.GetDeviceSettingId(deviceId);
   const SDL_JoystickID id = SDL_GetJoystickID(joystick);
   const SDL_GamepadType type = SDL_IsGamepad(id) ? SDL_GetGamepadTypeForID(id) : SDL_GAMEPAD_TYPE_UNKNOWN;
   // Only a PinOne gets the layout: an Xbox pad that answered the PinOne's identity report (src/steamframe/PinOneDevices.h), not a real
   // Xbox pad, which may share its settings id (same ids, name and version)
   s_pinOneInputs.erase(settingsId);
   if (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE)
   {
      if (!VPX::PinOneDevices::Get(deviceId).xbox)
         return;
      SDL_Gamepad* const gamepad = SDL_OpenGamepad(id);
      if (gamepad == nullptr)
         return;
      int count = 0;
      if (SDL_GamepadBinding** const bindings = SDL_GetGamepadBindings(gamepad, &count); bindings)
      {
         PinOneInputs in { GamepadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, bindings, count), GamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, bindings, count),
            GamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, bindings, count), GamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, bindings, count), true,
            GamepadButton(SDL_GAMEPAD_BUTTON_SOUTH, bindings, count), GamepadButton(SDL_GAMEPAD_BUTTON_EAST, bindings, count),
            GamepadButton(SDL_GAMEPAD_BUTTON_START, bindings, count), GamepadButton(SDL_GAMEPAD_BUTTON_NORTH, bindings, count),
            GamepadButton(SDL_GAMEPAD_BUTTON_BACK, bindings, count), GamepadButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT, bindings, count),
            GamepadButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, bindings, count), GamepadButton(SDL_GAMEPAD_BUTTON_DPAD_UP, bindings, count),
            GamepadButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, bindings, count), GamepadAxis(SDL_GAMEPAD_AXIS_RIGHTY, bindings, count),
            GamepadAxis(SDL_GAMEPAD_AXIS_LEFTX, bindings, count), GamepadAxis(SDL_GAMEPAD_AXIS_LEFTY, bindings, count),
            GamepadButton(SDL_GAMEPAD_BUTTON_WEST, bindings, count) };
         in.dpadServiceButtons = false;
         // The flippers and magna-saves are what makes it a PinOne layout; any other control the pad lacks is left out
         if (in.lb != NONE && in.rb != NONE && in.leftTrigger != NONE && in.rightTrigger != NONE)
            s_pinOneInputs[settingsId] = in;
         SDL_free(bindings);
      }
      SDL_CloseGamepad(gamepad);
      return;
   }

   // The PinOne's USB controller (the Bluetooth one in its own mode has the same ids, wireless and with another report): raw button n is
   // PinOne input n + 1
   if (SDL_GetJoystickVendor(joystick) == 0x39f5 && SDL_GetJoystickProduct(joystick) == 0x9208 && SDL_GetJoystickConnectionState(joystick) == SDL_JOYSTICK_CONNECTION_WIRED
      && SDL_GetNumJoystickButtons(joystick) >= 29 && SDL_GetNumJoystickAxes(joystick) >= 5)
   {
      // Its axes, in SDL's order (the report lists X, Y, Rx, Ry, Z; SDL numbers them X, Y, Z, Rx, Ry), with the firmware's defaults
      input.RegisterElementName(deviceId, true, 0x0200, "Nudge X"s);
      input.RegisterElementName(deviceId, true, 0x0201, "Nudge Y"s);
      input.RegisterElementName(deviceId, true, 0x0202, "Plunger"s);
      input.RegisterElementName(deviceId, true, 0x0203, "Nudge X Velocity"s);
      input.RegisterElementName(deviceId, true, 0x0204, "Nudge Y Velocity"s);
      input.RegisterElementName(deviceId, false, 0, "Right Flipper"s);
      input.RegisterElementName(deviceId, false, 1, "Right Magna-Save"s);
      input.RegisterElementName(deviceId, false, 2, "Left Flipper"s);
      input.RegisterElementName(deviceId, false, 3, "Left Magna-Save"s);
      input.RegisterElementName(deviceId, false, 8, "Fire"s);
      // Firmware's Xbox assignment: input 5 Y, 6 A, 7 X, 8 B, 9 Start, 25 D-pad left, 26 right, 27 up, 28 down, 29 View
      s_pinOneInputs[settingsId] = PinOneInputs { 2, 0, 3, 1, false, 5, 7, 8, 4, 28, 24, 27, 26, 25, 0x0202, 0x0200, 0x0201, 6 };
   }
}

bool HasPinOneLayout(InputManager& input, uint16_t deviceId)
{
   return VPX::PinOneDevices::Get(deviceId).IsPinOne() && s_pinOneInputs.contains(input.GetDeviceSettingId(deviceId));
}

void ApplyPinOneLayout(InputManager& input, uint16_t deviceId)
{
   const auto found = s_pinOneInputs.find(input.GetDeviceSettingId(deviceId));
   if (found == s_pinOneInputs.end())
      return;
   const PinOneInputs& in = found->second;
   PLOGI << "Controller layouts: PinOne layout applied to '" << input.GetDeviceName(deviceId) << '\'';

   // As InputManager::ApplyDefaultDeviceMapping: the device's mappings are cleared, then the layout's are added and saved
   input.ClearDeviceMappings(deviceId);
   const auto map = [&input, deviceId](uint16_t element, unsigned int action, bool isTrigger = false)
   {
      if (element == NONE)
         return;
      input.GetInputActions()[action]->AddMapping(isTrigger ? ButtonMapping::Create(deviceId, element, -0.3f) : ButtonMapping::Create(deviceId, element));
      input.GetInputActions()[action]->SaveMapping(g_settingsService.GetAppSettings());
   };
   map(in.lb, input.GetLeftFlipperActionId());
   map(in.lb, input.GetStagedLeftFlipperActionId());
   map(in.rb, input.GetRightFlipperActionId());
   map(in.rb, input.GetStagedRightFlipperActionId());
   map(in.leftTrigger, input.GetLeftMagnaActionId(), in.triggersAreAxes);
   map(in.rightTrigger, input.GetRightMagnaActionId(), in.triggersAreAxes);
   map(in.a, input.GetLaunchBallActionId());
   map(in.a, input.GetStartActionId());
   map(in.b, input.GetStartActionId());
   map(in.start, input.GetStartActionId());
   map(in.y, input.GetAddCreditActionId(0));
   map(in.view, input.GetOpenInGameUIActionId());
   if (in.dpadServiceButtons)
   {
      map(in.dpadLeft, input.GetServiceActionId(0));
      map(in.dpadDown, input.GetServiceActionId(1));
      map(in.dpadUp, input.GetServiceActionId(2));
      map(in.dpadRight, input.GetServiceActionId(3));
   }
   map(in.dpadUp, input.GetUIUpActionId());
   map(in.dpadDown, input.GetUIDownActionId());
   map(in.dpadLeft, input.GetUILeftActionId());
   map(in.dpadRight, input.GetUIRightActionId());
   // The menus also on the PinOne's own buttons (owner's choice, 2026-11-19): Y and A up and down, X and B, like the left and right
   // flippers, the previous and next item, the fire button confirms, the magna-saves cancel. The game's actions on these buttons stay:
   // the menu actions only act while a menu is open, as on the keyboard, where the flipper and magna-save keys also move in the menus
   map(in.y, input.GetUIRowUpActionId());
   map(in.a, input.GetUIRowDownActionId());
   map(in.x, input.GetUIDownActionId()); // UI: Previous Item
   map(in.b, input.GetUIUpActionId()); // UI: Next Item
   map(in.lb, input.GetUIDownActionId());
   map(in.rb, input.GetUIUpActionId());
   map(in.start, input.GetUIRightActionId()); // UI: Confirm
   map(in.leftTrigger, input.GetUILeftActionId(), in.triggersAreAxes); // UI: Cancel
   map(in.rightTrigger, input.GetUILeftActionId(), in.triggersAreAxes);

   if (in.plungerAxis != NONE)
   {
      std::unique_ptr<PlungerSensor> plunger = std::make_unique<PlungerSensor>(&input);
      plunger->GetPositionSensor()->SetMapping(SensorMapping::Create(deviceId, in.plungerAxis, SensorMapping::Type::Position));
      input.m_plungerHandler->AddSensor(plunger);
   }
   if (in.nudgeXAxis != NONE && in.nudgeYAxis != NONE)
   {
      // As upstream's gamepad layout: the accelerometer moves the stick, read as a position
      auto gamepadNudge = std::make_unique<VPX::Physics::GamepadNudge>(&input);
      gamepadNudge->GetXSensor().SetMapping(SensorMapping::Create(deviceId, in.nudgeXAxis, SensorMapping::Type::Position));
      gamepadNudge->GetYSensor().SetMapping(SensorMapping::Create(deviceId, in.nudgeYAxis, SensorMapping::Type::Position));
      // A firm shake of the owner's PinOne Mini only reached about a quarter of the stick (a full stick is a strong nudge), too weak to
      // notice in a game: the strength starts at the setting's maximum, 200 %, for the moment (docs/steam-frame/PinOne Integration.md,
      // section 10.5)
      gamepadNudge->SetStrengthScale(2.f);
      std::unique_ptr<VPX::Physics::NudgeSensor> nudge = std::move(gamepadNudge);
      input.m_nudgeHandler->AddSensor(nudge);
   }
   g_settingsService.GetAppSettings().Save();
}

}
