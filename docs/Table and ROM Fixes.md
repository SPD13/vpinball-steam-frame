# Table and ROM fixes

Oct 25, 2026 · @SPD13

## Summary

Some tables and ROMs do not work on the Steam Frame as they are, for reasons outside of VPX: a bug in a table script that newer
scripts of VPX refuse, a ROM that needs to be set up from its service menu. The application fixes them with **recipes**: each one
says which tables or ROMs it applies to and what it changes, and ships with the application (`src/assets/fixes/fixes.json`). This
document is the record of the recipes: for each one, the symptom, the cause, what the fix changes, and how it was checked. It is
kept with the recipes, and every recipe must have its section here (`tests/test-table-fixes.cpp` checks it).

The code is `src/steamframe/TableFixes.h/.cpp` (recipes, matching, script patches, state) and `plugins/pinmame/RomFix.h/.cpp` (the
ROM procedures, run by the PinMAME plugin).

## How it works

### Recipes

`src/assets/fixes/fixes.json` holds a list of recipes:

```json
{
  "id": "bttf-vr-room-double-vpminit",
  "version": 1,
  "title": "Back to the Future VR Room: vpmInit called twice",
  "symptom": "...", "cause": "...", "change": "...",
  "doc": "bttf-vr-room-double-vpminit",
  "match": { "files": ["vr room back to the future"] },
  "script": [ { "find": "\tBackdrop_Init\n\n\tvpmInit me\n", "replace": "...", "count": 1 } ]
}
```

- `match`: `files` are parts of the table file name, ignoring case; `roms` are PinMAME ROM names (the `cGameName` of the script).
  When both are given, both must match. A ROM procedure also matches the ROMs of its sequences.
- `version`: raised whenever the recipe changes. The tables patched with an older version offer to be patched again.
- `symptom`, `cause`, `change`: shown by the web page of the fixes. `doc`: the anchor of the recipe's section below.
- A recipe makes one of two kinds of change, below.

A `fixes.json` in the preferences folder (next to `VPinballX.ini`) adds recipes in the same format, replaces a shipped recipe that
has the same id, and turns shipped ones off: `"disable": ["gts3-zero-settings"]`. It is the way to try a recipe on the Frame before
shipping it. It is saved by the backup page, with the settings.

### Script patches

A list of text replacements in the table script. They are applied to the base script and written as the table's `.vbs`, which VPX
loads instead of the script inside the `.vpx` (`PinTable::LoadGameFromFilename`). The `.vpx` is never changed.

- The base script is the `.vbs` the table came with, if any, kept as `<table>.vbs.orig` once patched, otherwise the script inside the
  `.vpx` (`PupPacks::ReadEmbeddedScript`, which only reads the script stream).
- A replacement applies only when its `find` text is found exactly `count` times (1 by default). A new version of the table, already
  fixed by its author, is then left alone and reported as "not needed", instead of being patched blindly.
- Line ends are written `\n` in the recipe, and match the `\r\n` of the script (most scripts): the patched script keeps its line ends.
- Patching again rebuilds the `.vbs` from the base script with all the script recipes matching the table.
- The `.vbs` written by the patches is recognized by its hash. When the `.vpx` file changes (a new version uploaded), the `.vbs` built
  for the previous one is removed (the `.vbs.orig` is put back) before the table is checked again, so that an old script never
  replaces a new one.

### ROM procedures

Switch presses run by the PinMAME plugin once the emulation started, as an operator would in the service menu of the game, then a
reset of the machine, so that the game starts again with what the procedure changed (a reset keeps the battery RAM, hence the NVRAM).

```json
"rom": {
  "bootMs": 25000, "holdMs": 100, "waitMs": 1200, "reset": true, "runWithoutNvram": true,
  "notice": "Patching this table for compatibility: ...",
  "sequences": [ { "roms": ["sfight2", "stargat5"], "switches": [-8, -8, 143, 143, 143, 141, 4, 4] } ]
}
```

- Switch numbers are libpinmame's (`PinmameSetSwitch`), the ones of the table scripts.
- Before starting the emulation (`Controller::Run`), the plugin asks VPX for a procedure (`VPXPluginAPI::GetRomFix`, with whether the
  ROM has an NVRAM). VPX gives one when it was requested by **Patch table**, or when the ROM has no NVRAM yet and the recipe has
  `runWithoutNvram` (a new System 3 NVRAM is all zeros). The NVRAM it is about to change is kept as `<rom>.nv.bak-<date>`.
- The steps run on the main thread (`RunOnMainThread`), like the table script: wait `bootMs`, press each switch for `holdMs`, wait
  `waitMs` after each release. `notice` is shown as a notification for the whole time, then "Table patched for compatibility: the game
  restarts".
- While it runs, the changes of the switches it presses from the table script are held, and applied at its end: a player pressing a
  flipper in the adjustment screens would change a setting.
- The end is reported to VPX (`VPXPluginAPI::ReportRomFix`). A procedure interrupted by the end of the table is reported as such, and
  runs again at the next start.

### When fixes are applied

- **Tables added to the library** (found by a scan, imported, uploaded) get the script patches matching them at once. A table whose
  `.vpx` changed is checked again the same way.
- **The tables already in the library** when the fixes first run are only recorded: they are patched on request. Same for the tables
  matching a recipe shipped after they were added. The requests:
  - **Tables page** of the web server: the tile shows **Fix available** or **Patched**; "..." > **Patch table** or **Re-patch table**.
  - **Files page** of the web server, ⋮ > **Advanced** > **Table and ROM fixes**: the tables matching a recipe with the state of each
    fix and a Patch button, and every recipe with its symptom, cause, change, targets and a link to its section here
    (`vpx.html?fixes` opens it; the badges of the Tables page link there).
  - **Menu of the table** in VR: "Patched for compatibility: ..." and **Patch table** / **Re-patch table**, its tooltip listing the fixes
    and their state.
- **ROM procedures** run by themselves at the first start of a game without NVRAM; Patch requests them for the next start.

The state of a fix for a table:

| State | Meaning |
| --- | --- |
| Available | Matches the table, not applied |
| Applied | Applied with the current version of the recipe |
| Update available | Applied with an older version of the recipe |
| At the next start | ROM procedure requested, runs when the table starts |
| Not needed | Matches the table by name or ROM, but its script does not have the text to patch |

What was applied is kept in `table-fixes.json` in the preferences folder: for each table (by uuid), the date of the `.vpx` file when
it was last checked and when its `.vbs` was built, the hash of that `.vbs`, and the recipes applied (version, date); for each ROM, the
procedure requested and the ones run. It is not saved by the backup page: the uuids and file dates change with a restore, and the
restored tables are checked again as new ones (a `.vbs` already patched has no text left to patch, and stays as it is).

The log tells everything the fixes do, on lines starting with `[Fixes]` (and `ROM fix` for the plugin).

## Adding a fix

1. Find the cause (the log, the script, the ROM), and write it down in a section below.
2. Add the recipe to `src/assets/fixes/fixes.json`, with `doc` set to its id, and a section here that starts with
   `<a id="<id>"></a>`. A ROM sequence must be checked on each ROM it lists with the driver of
   `Doc/test-hooks/gts3-factory-reset/drive.cpp` (it prints the display as text): a wrong press changes a setting of the game.
3. Run `tests/test-table-fixes.cpp` (the command is at its top) from the root of the repository.
4. Check it on the Mac with the table: a `fixes.json` in the preferences folder of a test, or the shipped one, the Tables and Files
   pages, then `-play` and the `[Fixes]` lines of the log. Then on the Frame.
5. When a recipe changes, raise its `version` and add a line to its history.

## Record

### <a id="bttf-vr-room-double-vpminit"></a>Back to the Future VR Room: vpmInit called twice

Applies to: tables whose file name contains `VR ROOM Back to the Future` (checked with *VR ROOM Back to the Future (Data East 1990)
V103*, ROM `bttf_a27`).

**Symptom.** The table loads, then closes as soon as it starts, back to the lobby, as if it crashed. No crash is recorded.

**Cause.** The table's init routine calls `vpmInit Me` twice: once at its start, and once more after `Backdrop_Init`:

```vb
Sub bttf_Init
    vpmInit Me
    ...
	SetDeloreanColorMod
	Backdrop_Init

	vpmInit me
```

`vpmInit` initializes the flippers (`cvpmFlips2.Init` in `core.vbs`), which refuses a second init since the scripts of VPX 10.8:

```vb
Sub Init()	'called by vpmInit sub
	If Initialized Then
		MsgBox "Table script bug: cvpmFlips2.Init is called twice." & vblf & vblf & "Maybe 'vpmInit Me' is duplicated in the code ?"
		QuitPlayer 2 'CS_STOP_PLAY
	End If
```

On the Frame the message only goes to the log, and the table stops (log of 2026-10-25, 16:47):

```
vbscript: MsgBox prompt=Table script bug: cvpmFlips2.Init is called twice.
Player::Player@434] Startup done
Player::~Player@1482] Closing player...
```

**Fix.** The second `vpmInit me` of `bttf_Init` is commented out. Only that line changes; the first call already did everything
`vpmInit` does.

**Checked** on the Mac, 2026-10-25: the table stopped as on the Frame before the fix; with the patched `.vbs`, the script was loaded
from it, without the message, and the table played until it was closed (45 s). Patch from the web page, and the automatic patch of
a copy added to the library afterwards, gave the same `.vbs`: the script of the table with that one line changed, CRLF kept. **To
check** on the Frame.

History: version 1, 2026-10-25.

### <a id="gts3-zero-settings"></a>Gottlieb System 3: factory settings of the game

Applies to the ROMs `sfight2` (Street Fighter II), `stargat5` (Stargate), `smb` (Super Mario Bros) and `smbmush` (Super Mario Bros
Mushroom World).

**Symptom.** Coins are accepted, but the display stays at CREDITS 0 and no game can be started.

**Cause.** PinMAME creates the NVRAM of a Gottlieb System 3 game filled with zeros, and the ROM takes the zeros as its settings: every
price is 0. The full investigation is in [Gottlieb System 3 No Credits](Gottlieb%20System%203%20No%20Credits.md).

**Fix.** The game loads its factory settings from its own service menu, as in the manual solution of that document, then the machine
is reset. Sequences (libpinmame switches: −8 Self Test, 143 left flipper, 141 right flipper, 4 Start):

| ROMs | Menu | Switches |
| --- | --- | --- |
| `sfight2`, `stargat5` | Long: Self Test twice, left flipper 3 times down to Game Adjustments, right flipper to select it, Start (load factory settings), Start (English) | −8, −8, 143, 143, 143, 141, 4, 4 |
| `smb`, `smbmush` | Short: Self Test, left flipper to "Credit button to load factory settings", Start, Start | −8, 143, 4, 4 |

Each press lasts 100 ms, followed by 1.2 s. The first press comes 25 s after the start of the emulation: the boot of these games takes
about 20 s, and the emulation of a table starts while the table is still loading. The whole procedure takes about 35 s, with a
notice in the game asking not to press any button.

The reset: once the factory settings are loaded, the ROM stays in its adjustment screens, where coins are ignored, and Self Test does
not leave them. The manual solution left the table, so that the next start used the new NVRAM. A reset of the machine (`PinmameReset`)
does the same without leaving: it restarts the ROM and keeps the battery RAM.

It runs by itself at the first start of these games without NVRAM, and on request. The NVRAMs fixed by hand on the Frame on
2026-10-03 do not need it (the fix shows as available for those tables: patching them runs it again, harmless, and keeps the NVRAM
they had as `<rom>.nv.bak-<date>`).

**Checked** on the Mac, 2026-10-25:

- With the driver of `Doc/test-hooks/gts3-factory-reset/drive.cpp` and a reset added, from a new NVRAM: `sfight2` reads CREDITS 1 after
  2 coins, `smbmush` shows credits after 2 coins, and `stargat5` and `smb` start a game (BALL 1) after 2 coins and Start.
- In VPX, JP's Street Fighter 2 VR Room 1.1 without NVRAM: the procedure ran by itself (done 35 s after the start of the emulation);
  the NVRAM it saved, run again with the driver, gives CREDITS 1 after 2 coins. Requested from the web page with an NVRAM present: it
  ran at the next start, after keeping the NVRAM as `sfight2.nv.bak-<date>`. Table closed during the procedure: reported as
  interrupted, still requested.

**To check** on the Frame: the notice in the headset, and the timing with the Frame's longer table loading.

Other Gottlieb System 3 ROMs are not listed: their menus may differ, so each one must be checked with the driver before it is added.

History: version 1, 2026-10-25.
