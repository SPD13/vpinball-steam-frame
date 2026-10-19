# PinMAME ROM aliases: table does not start

Oct 3, 2026 · @SPD13

## Summary

VR Room – The Simpsons (Data East 1990) v1.04 loads, but its backglass stays dark and Start never begins a game. The table script asks PinMAME for the game `simp`, which is a short alias, not a PinMAME game name. On Windows, VPinMAME maps `simp` to `simp_a27` through its built-in alias list; the PinMAME plugin used by VPX standalone reads that list from `pinmame/alias.txt` instead, and the Steam Frame had no such file. The alias list is now installed on the Frame. The table also needs the ROM `simp_a27.zip`, which is not on the device yet.

## The issue

On the Steam Frame, the table loads and coins add credits, but the backglass stays off and Start does nothing, so no ball is served. The credits come from the table script, not from the ROM: the ROM never runs.

The VPX log (`~/.local/share/VPinballX/10.8/vpinball.log`) of the session shows the error:

```
ERROR [VPXPluginAPIImpl::OnScriptError] Script error reported by plugin (Failure): Game name not found: 'simp'.
```

The table script hides it: it sets `.GameName` and calls `.Run` under `On Error Resume Next`, so the table keeps running without a ROM.

```vb
Const cGameName="simp", ...
With Controller
   .GameName = cGameName
   ...
   On Error Resume Next
   .Run GetPlayerHWnd
```

## Root cause

PinMAME's game names for The Simpsons are `simp_a27` (2.7) and its clone `simp_a20` (2.0), in `src/wpc/degames.c`. `simp` is an alias, one of about 120 that VPinMAME defines in `src/win32com/Alias.cpp`, so the same table works on Windows. Table authors often use these older or shorter names.

The PinMAME plugin does not use VPinMAME's built-in list. `Controller::SetGameName` (`plugins/pinmame/Controller.cpp`) calls `PinmameGetGame`, and libpinmame resolves aliases in `CheckGameAlias` (`src/libpinmame/libpinmame.cpp`) by reading `<vpmPath>/alias.txt`, where `vpmPath` is the shared `pinmame` folder in the tables folder. The file holds one `alias,romname` pair per line, with `#` for comments. Without that file, no alias is known and the game name is rejected.

## Fix applied

PinMAME ships its alias list as `release/VPMAlias.txt`, in the format libpinmame reads (`simp,simp_a27` is line 98). It was copied to the Frame as the plugin's alias file:

| File | Content |
| --- | --- |
| `Documents/VPinballX/Tables/pinmame/alias.txt` | Copy of PinMAME's `release/VPMAlias.txt` (the macOS build has it in `external/macos-arm64/Release/pinmame/pinmame/release/`) |

The other file in that folder, `release/alias.txt`, is a text description of the aliases (`simp = simp_a27`), not the format libpinmame parses: do not use it as `alias.txt`.

The ROM is still missing: `pinmame/roms/` on the Frame has no `simp_a27.zip`. Once it is added (web upload or `scp`), the table resolves `simp` to `simp_a27` and starts. Not verified on the device yet, for lack of the ROM.

A check of every table on the Frame (its `cGameName`, resolved through `alias.txt`, against `pinmame/roms/`) found no other PinMAME table blocked by an alias. The other names without a ROM belong to original tables that do not use PinMAME.

## Follow-ups

- [ ] Add `simp_a27.zip` to the Frame and check that The Simpsons starts.
- [x] New installs get the aliases (2026-10-24): the application creates `pinmame/alias.txt` from its copy of `VPMAlias.txt` (`assets/pinmame/VPMAlias.txt`) when the file is missing, instead of the installer.
- [ ] The web upload's list of missing ROMs could resolve aliases too, so that it asks for `simp_a27` rather than `simp`. (The missing ROM reports come from PinMAME after it resolved the alias, so they should already name `simp_a27`; not checked.)

## Mods with a game name of their own (Oct 24, 2026)

Two more tables stopped with "Game name not found", for another reason: their game name is not in VPinMAME's list at all. Mods rename their game and their readme asks the user to add the alias to `VPMAlias.txt`:

| Table | `cGameName` | Alias given by the download | Where |
| --- | --- | --- | --- |
| Simpsons Treehouse of Horror MOD v2023.3 (VPW 2.0.3 base) | `simpprtyh` | `simpprtyh,simpprty` | `readme.txt` in the table folder |
| Aliens PuP 1.0.1.7 | `alienspupver` | `alienspupver,sorcr_l1` | `INSTALLATION INSTRUCTIONS.txt`, beside the table |

On the Frame the Simpsons log showed `Game name not found: 'simpprtyh'` on line 1399 (`.GameName = cGameName` in `Table1_Init`), then `Object required` on line 262 at every frame: `Table1_Init` stops before it creates the captive ball, which a timer moves (`bart1a.X = capBall.X`). Its ROM was not installed either: the bundle ships a muted `simpprty.zip` in a `MutedRom` folder, where the import did not look, and `pinmame/roms` only had `simp_a27.zip` (the Data East Simpsons, another game).

There is no list of such aliases to download: PinMAME's `release/VPMAlias.txt` is the only official one (identical to the Frame's `alias.txt`), and mod names are made up by each author. The library now finds them in the table's files and writes them to `alias.txt`, moves a ROM it finds in the table folder when none is installed, and the file can be edited from the browser (file manager, **⋮ > Advanced > PinMAME ROM aliases**), where a save is refused for lines libpinmame would misread. Details in section 6 of `docs/Steam Frame Branch.md`.

- [ ] Start both tables on the Frame with the new build (the Simpsons gets its alias from its readme and the muted ROM; Aliens gets its alias from `INSTALLATION INSTRUCTIONS.txt`, which is in its table folder on the Frame, and still needs `sorcr_l1.zip`, which is not on the Frame).
