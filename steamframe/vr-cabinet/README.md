# Default VR cabinet

The cabinet shown in a headset around the tables that have no VR room or cabinet of their own (see `src/steamframe/DefaultCabinet.h`):
a Williams WPC standard body, with generic art, both kinds of ball launchers (spring plunger, launch button), and windows in the backbox
for the table's backglass and display. It is built in Blender by the scripts of this folder and shipped as `src/assets/vr-cabinet/`
(a glTF binary and its WEBP textures, about 0.6 MB).

## Licensing

Everything is made by the scripts: no outside model, texture or font. The dimensions are those of the real cabinet, taken from the
measured plans of [jonaskello/wpc-cabinet](https://github.com/jonaskello/wpc-cabinet) (figures only; its files are not used, they have
no license). The art (`tools/cabinet_art.py`) is abstract on purpose: no text, no logo, nothing looking like a real machine. The cabinets
of the VR rooms made by the community (Sixtoe's PinCab templates, the VPW example table, VPX VR Injector) have no license that allows
redistributing them, see `steamframe/lobby-room/DESIGNING-ROOMS.md`, section 2.2.

## Build

Blender 5.2 (`/Applications/Blender.app`), headless; the work folder is outside the repository:

```sh
B=/Applications/Blender.app/Contents/MacOS/Blender
T=<vpinball>/steamframe/vr-cabinet/tools
W=<work folder>
$B -b --factory-startup --python $T/build_cabinet.py -- $W --blend      # $W/asset: vr-cabinet.glb + textures, $W/art: PNG previews
$B -b $W/cabinet.blend --python $T/render_cabinet.py -- $W/preview 64 spring threequarter player side back plunger
$B -b $W/cabinet.blend --python $T/render_cabinet.py -- $W/preview 64 button plunger
cp $W/asset/* <vpinball>/src/assets/vr-cabinet/
```

| File | |
|---|---|
| `tools/dims.py` | Dimensions (WPC standard body), the nominal playfield the model is drawn around, the zones of the fit |
| `tools/build_cabinet.py` | Geometry, materials, export with the extras the application reads |
| `tools/cabinet_art.py` | Side, front and backbox art, default backglass, speaker panel, coin door inserts (numpy) |
| `tools/render_cabinet.py` | Preview renders in a plain studio with a stand-in playfield |

## What the application reads

The model is in meters, X to the player's right, Y away from the player, Z up, X = 0 the middle of the playfield, Y = 0 its front edge,
Z = 0 the floor (exported without the glTF Y-up conversion). Each node has extras:

- `vpx_fit`: `deform` (every vertex goes through the fit, `src/steamframe/CabinetFit.h`) or `rigid` (moved as a whole from `vpx_anchor`);
- `vpx_space`: `cabinet` or `feet` (the legs, stretched to the floor by the VR cabinet feet space);
- `vpx_variant`: `spring` or `button`, the parts of one launcher;
- `vpx_role`: parts animated or placed by the application (`plunger_rod`, `plunger_spring`, `launch_button`, `flipper_left`,
  `flipper_right`, `start_button`), and the two rectangles `backglass` and `display` (`vpx_corners`);
- materials: `vpx_image` (texture file, without `.webp`), `vpx_unlit` (0..1), `vpx_clearcoat`.

The scene extras hold the figures of `dims.nominal()` (`vpx_cabinet`), so the fit follows the model when it changes.

## Fit to a table

The model keeps its shape by zones: across, the coin door keeps its size, the sides move with the playfield width, the band between is
stretched; lengthwise, the front keeps its place, the back moves with the playfield length; in height, the cabinet follows the lockbar
height of the table, the top edge rises along the table's slope (the backbox moves with it), the bottom stays level. The start button
and the launchers are rigid; the launcher stands in front of the table's plunger (`CabinetFit::LauncherX`).
