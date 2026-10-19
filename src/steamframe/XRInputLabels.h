// license:GPLv3+

#pragma once

#include <string>

// Steam Frame Edition: readable names of the inputs of the VR controllers, shown in the Controls page ("Right X button" instead of
// "user_hand_right_input_x_click"). XRInputHandler registers them in place of its own names, which are the OpenXR paths. Display only:
// the mappings are saved by the index of the input, so they do not depend on these names.
namespace VPX::SteamFrame
{

// "/user/hand/<left|right>/input/<component>/<feature>" -> "Left trigger", "Right X button", "Left d-pad up", "Right thumbstick up/down"...
// Other paths are returned as they are.
inline std::string GetXRInputLabel(const std::string& path)
{
   const std::string prefix = "/user/hand/";
   if (path.rfind(prefix, 0) != 0)
      return path;
   const std::string rest = path.substr(prefix.size()); // <hand>/input/<component>/<feature>
   const size_t handEnd = rest.find('/');
   const size_t componentStart = rest.find("/input/");
   if (handEnd == std::string::npos || componentStart == std::string::npos)
      return path;
   const std::string hand = rest.substr(0, handEnd) == "left" ? "Left" : "Right";
   const std::string componentAndFeature = rest.substr(componentStart + 7);
   const size_t slash = componentAndFeature.find('/');
   const std::string component = componentAndFeature.substr(0, slash);
   const std::string feature = slash == std::string::npos ? "" : componentAndFeature.substr(slash + 1);

   std::string name;
   if (component == "a" || component == "b" || component == "x" || component == "y")
      name = std::string(1, static_cast<char>(component[0] - 'a' + 'A')) + " button";
   else if (component.rfind("dpad_", 0) == 0)
      name = "d-pad " + component.substr(5);
   else if (component == "squeeze")
      name = "grip";
   else if (component == "menu" || component == "view" || component == "select" || component == "system")
      name = component + " button";
   else
      name = component; // trigger, thumbstick, trackpad, bumper

   if (feature == "click" && (component == "trigger" || component == "thumbstick" || component == "trackpad"))
      name += " click";
   else if (feature == "value" && component == "squeeze")
      name += " (pressure)";
   else if (feature == "x")
      name += " left/right";
   else if (feature == "y")
      name += " up/down";
   else if (feature != "click" && feature != "value" && !feature.empty())
      name += " " + feature;
   return hand + " " + name;
}

}
