# Lobby rooms

The rooms shown behind the table picker in launcher mode, built in Blender from CC0 assets of Poly Haven and from procedural models
and artwork (see `CREDITS.md`), with their lighting baked into their textures: VPX shows them unlit, which costs almost nothing per
frame.

| Room | Table | Scene script |
|---|---|---|
| Modern room: oak slat wall, cove light, sofa and lounge chair | `src/assets/lobby.vpx` | `tools/build_room.py` |
| Game room: a night lounge with arcade cabinets, a pinball machine and a city view | `src/assets/lobby-gameroom.vpx` | `tools/build_gameroom.py` (with `roomkit.py`, `gameroom_art.py`) |

The player chooses the room in the menu (`Standalone.LobbyRoom`); "Empty room" is the previous lobby, the blank table emptied and
given a tiled floor (see `BuildLobby` in `src/steamframe/Launcher.cpp`). The game room is the default.

**To design a new room**, read `DESIGNING-ROOMS.md`: the process from the brief to the headset, the licensing rules, the design
guidelines for VR, the bake and export details and the traps already met.

## Pipeline

Blender 5.2 (`brew install --cask blender`) and [vpxtool](https://github.com/francisdb/vpxtool) (its macOS aarch64 release binary,
here in `~/.local/bin`). The downloads and bakes take several GB, so they go to a work folder outside the repository; all the steps
run headless from that folder:

```sh
B=/Applications/Blender.app/Contents/MacOS/Blender
T=<vpinball>/steamframe/lobby-room/tools
# 1. Assets (CC0), with their credits in downloads/credits.json
python3 -I $T/fetch_polyhaven.py downloads texture:laminate_floor_02 texture:plastered_wall_04 texture:oak_veneer_01 \
  texture:poly_wool_herringbone model:sofa_02 model:modern_coffee_table_01 model:potted_plant_04 model:mid_century_lounge_chair \
  model:side_table_01 model:modern_ceiling_lamp_01 model:calathea_orbifolia_01
python3 -I $T/fetch_polyhaven.py downloads hdri:shanghai_bund:8k texture:herringbone_parquet texture:dark_wooden_planks \
  texture:quatrefoil_jacquard_fabric texture:leather_red_02 texture:jersey_melange model:throw_pillows_01 model:anthurium_botany_01 \
  model:dartboard
# 2. Scene, and previews from the player's place (front, left, right, back, corner)
$B -b --factory-startup --python $T/build_gameroom.py -- downloads build/gameroom.blend      # or build_room.py, build/lobby.blend
$B -b build/gameroom.blend --python $T/render_views.py -- build/preview_game 64
# 3. Bake: one texture per group (the scene lists its groups and their sizes), linear EXR
$B -b build/gameroom.blend --python $T/bake.py -- build/bake_game 128
# 4. VPX table, from the blank table of the application extracted with vpxtool
vpxtool extract -o build/base <vpinball>/src/assets/blankTable.vpx
$B -b build/bake_game/baked.blend --python $T/export_vpx.py -- build/bake_game build/base build/lobby-gameroom.vpx 1 "Game room"
cp build/lobby-gameroom.vpx <vpinball>/src/assets/
```

`preview_art.py` saves the generated artwork of the game room as PNG files, `inspect_props.py` prints the size and triangle count of
the downloaded models, `exr_preview.py` turns a baked EXR into a PNG. `lobby-shot-hooks.py` adds a temporary hook (run from
`vpinball/`, `apply` or `remove`) that saves a screenshot of the lobby, with `VPX_TEST_NOUI=1` without the menu: it made the
thumbnails of the room chooser (`src/assets/lobby-rooms/`).

## How it fits VPX

- **Axes.** In Blender the player stands at the origin, faces +Y, Z up, in meters. The export maps that to the lobby's room space:
  VPX x = table center + x, VPX y = table bottom − y (Y points toward the player in VPX), z = floor height, in VP units.
- **One primitive per bake group**, in the part group `LobbyRoom` (room space reference, visible in every mode), unlit
  (`disable_lighting_top = 1`), static, no back faces, with a white material: the texture is shown as baked. Faces must face the
  inside of the room (VPX culls back faces).
- **Exposure.** The bake holds linear scene values; the room surfaces stay below 1, so the export stores them as they are (`scale`
  1) with the AgX tone mapper that the Blender previews use. Brighter lights (neon, screens) are scaled down to keep their hue.
- **The window of the game room** shows an HDRI on a cylinder 70 m away, baked as emission from the player's eye. The panorama
  was taken at ground level with a crowd along the river: the backdrop looks it up 5° higher than the real direction, which sinks
  the crowd below the parapet.
- **Near plane.** Outside of VR, VPX derives the near clipping plane from the bounding boxes of the parts: with a few large parts it
  would clip the floor in front of the player, so the export adds a tiny quad hidden under the floor half a meter ahead.
- **Launcher.** `Launcher::GetLobbyTablePath()` takes the chosen room, and `BuildLobby` keeps its parts as they are (it only clears
  the script, sets a level camera for the desktop view and adds the invisible playfield).
