# XR simulator (macOS)

The macOS build can simulate a headset and two Steam Frame controllers, so that the VR code of the fork runs on a Mac: the room and the
cabinet seen from the player's head, the menu panel standing in the room, the pointer ray, the controller models. It was made to capture
the VR trailer on a Mac, and it is also a quick way to look at the VR UI without a headset.

It is an OpenXR runtime of its own, linked into the application (no loader): `src/steamframe/xrsim/`.

It lives on the `xr-sim` branch only, with the frame recorder and the tools that make the videos: see
[The xr-sim branch](XR%20Simulator%20Branch.md).

| File | What |
|---|---|
| `XRSim.cpp` | The OpenXR functions VPX uses: instance, system, session and its states, reference and action spaces, swapchains, frame loop, actions, and `XR_EXT_render_model` for the controller models. The poses and inputs come from the application (`XRSim.h`). |
| `XRSimBackend.h` | Graphics backend: bgfx creates its Metal device and the swapchain images itself, as plain render targets (the runtime has no compositor and never reads them). |
| `XRSimMirror.cpp` | The desktop window shows the left eye once the UI panel and the pointer are drawn in it (upstream mirrors the eyes before the UI). |
| `XRSimScript.cpp` | Scripted sessions: head and controller poses, inputs, keys, tables to play, one scene per table played. |

## Build and run

```sh
cmake -S . -B build -DPLATFORM=macos -DARCH=arm64 -DRENDERER=BGFX -DCMAKE_BUILD_TYPE=Release -DVPX_XR_SIM=ON
cmake --build build --target vpinball
VPX_XRSIM=1 build/VPinballX_BGFX.app/Contents/MacOS/VPinballX_BGFX -Launcher
```

Without `VPX_XRSIM=1`, the runtime reports no headset and the application runs on the desktop as usual. The settings should set
`PlayerVR.AskToTurnOn = 0` (VR on) and `PlayerVR.DesktopDisplay = 3` (left eye).

Environment: `VPX_XRSIM_EYE=1920x1080` (per eye), `VPX_XRSIM_FOV=90` (horizontal degrees), `VPX_XRSIM_FPS=60`, `VPX_XRSIM_POSE=x,y,z,yaw,pitch`
(initial head pose), `VPX_XRSIM_CONTROLLERS=<folder>` (`left.glb`, `right.glb`: controller models), `VPX_XRSIM_SCRIPT=<file>`.

## Controller models

The models are made from the SteamVR render models of the Frame controllers (OBJ components and a JSON layout in
`/opt/steamvr/drivers/frame_controller/resources/rendermodels` on the Frame) by a converter that writes one glTF node per component, the
OpenXR aim and grip frames of the model and the motions of the trigger and buttons in the node extras. The script sets the aim pose of a
controller; the grip pose and the model follow from those frames, and the trigger and buttons move with the inputs.

## Scripts

`#` starts a comment, `scene` the commands of the next table played (the lobby first), then `<seconds from the table start> <command> <args>`,
run in time order:

| Command | |
|---|---|
| `head x y z yaw pitch [roll] [dur]` | Head pose in the room (meters; degrees, yaw to the left, pitch up), eased over `dur` seconds |
| `look dyaw dpitch [dur]` | Turns the head from its current target |
| `sway amplitude` | Small head motion (about centimeters and degrees), 0 to stop |
| `aim l\|r x y z yaw pitch [dur]`, `aimat l\|r x y z [dur]` | Aim pose of a controller, or points it at a position of the room |
| `aimui l\|r u v [dur]` | Points a controller at a position of the UI panel (0..1 from its top left) |
| `hand l\|r 0\|1`, `trig l\|r [ms]`, `input <path> <value>` | Shows or hides a controller, pulls its trigger, sets any input of the Frame controller profile |
| `key <scancode> [ms]`, `keydown`, `keyup` | Keyboard |
| `autoflip 0\|1 [keyboard]` | Flips when a ball comes to a flipper, with the triggers (or the keyboard) |
| `menu [page]`, `table <name>`, `exit`, `lobbyonscore`, `code`, `mark <label>`, `waitfile <path>` | In-game menu, next table, quit, back to the lobby once the game's score is recorded, pairing code and address to a file (`VPX_XRSIM_CODE`), marks in the log (`XRSIM-MARK <epoch ms> <label>`), wait for a file |

The time of a scene starts when its table is ready. `waitfile` stops it until the file exists (to wait for another tool, a browser
script for example).

## Limits

- No foveated rendering, dynamic resolution, eye gaze, passthrough or visibility mask (the extensions are not offered): the VR settings
  pages say "not supported by the runtime".
- Metal on macOS: the bgfx Metal backend needs a patch to accept stereo shaders drawing into a single layer target (the desktop window):
  the pipeline must give a primitive topology class when the vertex shader writes the render target array index.
