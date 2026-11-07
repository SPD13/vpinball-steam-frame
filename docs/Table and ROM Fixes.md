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
- **Other ROMs without NVRAM** show a notice for 10 s at their first start: "First launch. The table is initializing, you may need
  to restart the table after the initialization is complete". Many ROMs initialize a new NVRAM, then only work from the next start:
  the WPC games stay on "Factory settings restored", the Spinball games (Verne's World) wait for a watchdog reset that PinMAME does
  not emulate.

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

Applies to the ROMs `sfight2` (Street Fighter II), `stargat5` (Stargate), `waterwld` (Waterworld), `smb` (Super Mario Bros) and
`smbmush` (Super Mario Bros Mushroom World).

**Symptom.** Coins are accepted, but the display stays at CREDITS 0 and no game can be started.

**Cause.** PinMAME creates the NVRAM of a Gottlieb System 3 game filled with zeros, and the ROM takes the zeros as its settings: every
price is 0. The full investigation is in [Gottlieb System 3 No Credits](Gottlieb%20System%203%20No%20Credits.md).

**Fix.** The game loads its factory settings from its own service menu, as in the manual solution of that document, then the machine
is reset. Sequences (libpinmame switches: −8 Self Test, 143 left flipper, 141 right flipper, 4 Start):

| ROMs | Menu | Switches |
| --- | --- | --- |
| `sfight2`, `stargat5`, `waterwld` | Long: Self Test twice, left flipper 3 times down to Game Adjustments, right flipper to select it, Start (load factory settings), Start (English) | −8, −8, 143, 143, 143, 141, 4, 4 |
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

- `waterwld`, 2026-10-27: with the driver from a new NVRAM, the long menu ends on FACTORY SETTINGS LOADED; before it the attract mode
  read REPLAY AT 00, and 2 coins and Start started no game (the score coverage harness saw no game on VR ROOM WaterWorld (Gottlieb
  1995) 1.2).

History: version 1, 2026-10-25; `waterwld` added 2026-10-27 (same version: tables that never had the fix see it as available).

### <a id="city-hunter-ultradmd"></a>City Hunter: scores on the cabinet's DMD

Applies to: tables whose file name contains `CityHunter` (checked with *CityHunter (Original 2025 Tombg)*, with its PinUP Player pack
`City_Hunter`).

**Symptom.** In VR the cabinet has a backglass and a DMD. The backglass shows the pack's frame, its videos and the scores; the DMD stays
black.

**Cause.** The pack has a single layout, and every one of its screens is on the backglass: its DMD screen (screen 1, "DMD - 16:9
FullDMD") is positioned on the whole of screen 2, and the scores are labels of it, in the bottom band of the frame
([PinUP Player Label Draw Order](PinUP%20Player%20Label%20Draw%20Order.md)). The cabinet's DMD is a legacy DMD flasher (`DMDPUP`), which
only shows what the script sends to the table's DMD. The script can send a scoreboard and its messages to UltraDMD, but ships with it
off:

```vb
Const UltraDMDUpdateTime	= 5000	'UltraDMD update time (msec).  Increase value if you encounter stutter with UltraDMD on
Const UseUltraDMD = 0				'0 = Don't use UltraDMD, 1=Use it
```

Moving screen 1 of the pack to the DMD was not an option: its labels are placed for the band of the backglass, and the backglass would
lose its scores.

**Fix.** `UseUltraDMD` is set to 1: the script creates UltraDMD (provided by the FlexDMD plugin), refreshes its scoreboard (the players'
scores, `Credits n`, `Ball n/3`) on a timer, and shows its messages (`UDMD "BALL SAVED"`, skill shot, modes, mystery awards). The
refresh goes from 5 s to 1 s (`UltraDMDUpdateTime`): the comment's stutter is that of UltraDMD on Windows, and with 5 s the score of
the DMD lagged the backglass. The plugin shows the scoreboard only when no message is queued (`UltraDMD::DisplayScoreboard`), so a
faster refresh does not cut the messages. The backglass and the PuP labels are unchanged (`usePuPDMD` stays on).

**Checked** on the Mac, 2026-10-28, with a temporary hook (credit, Start, ball launch, flippers) capturing the DMD in the score view
window: the recipe applied from the web API (`/table-fixes`, POST), the `.vbs` differs from the script of the table by those two lines,
CRLF kept. In attract mode the DMD reads `CREDITS 0 BALL 0/3`; in the game it showed `SKILLSHOT LOSE`, then the scoreboard with
12,855, the score of the backglass. The score tracker reads the scores from UltraDMD too (`Scores shown by ultradmd`). **To check** on
the Frame: the `DMDPUP` flasher of the cabinet in the headset.

History: version 1, 2026-10-28.

### <a id="aladdin-ultradmd"></a>Aladdin: scores on the cabinet's DMD

Applies to: tables whose file name contains `Aladdin` (checked with *Aladdin (Original 2026) Tombg*, with its PinUP Player pack
`Aladdin`). Another Aladdin table without these two lines is reported as "not needed".

**Symptom.** In VR the cabinet has a backglass and a DMD. The backglass shows the pack's videos and the scores; the DMD stays black.

**Cause.** The same as [City Hunter](#city-hunter-ultradmd), by the same author: every screen of the pack's layout is on the
backglass, its DMD screen included (screen 5, "FullDMD", positioned on the whole of screen 2), and the scores are labels of it. The
cabinet's DMD is a legacy DMD flasher (`DMDPUP`), which only shows what the script sends to the table's DMD, and the script ships
with UltraDMD off:

```vb
Const UltraDMDUpdateTime	= 5000	'UltraDMD update time (msec).  Increase value if you encounter stutter with UltraDMD on
Const UseUltraDMD = 0				'0 = Don't use UltraDMD, 1=Use it
```

Until 2026-11-02 the PUP plugin also drew screen 5 in the score view, because a screen (6, Off) is positioned on it: the DMD showed
the labels of the backglass, and the labels changed size on the backglass from frame to frame (the score looked doubled and wobbled).
The plugin now draws a screen positioned on the backglass on the backglass only ([PinUP Player on the Steam
Frame](PinUP%20Player%20on%20the%20Steam%20Frame.md), section 9), and the DMD went black, as City Hunter's.

**Fix.** As City Hunter's: `UseUltraDMD` is set to 1 and `UltraDMDUpdateTime` to 1000. The script then creates UltraDMD (the FlexDMD
plugin), refreshes its scoreboard (`DisplayScoreboard00`: the players' scores, `Credits n`, `Ball n/3`) every second, and shows its
messages (`UDMD "BALL SAVED"`, extra ball, ball locks, Jasmine bonuses). The backglass and the PuP labels are unchanged (`usePuPDMD`
stays on).

**Checked** on the Mac, 2026-11-02: both texts are found once in the script, CRLF kept; with them, the table starts without a script
error, UltraDMD is created and loads its fonts, and the PUP plugin draws on the backglass only. **To check** on the Frame: the DMD in
the headset.

History: version 1, 2026-11-02.

### <a id="sonic-star-wars-dip-switches"></a>Star Wars (Sonic 1987): DIP switches of the game

Applies to: the ROM `sonstwar` (checked with *Star Wars (Sonic 1987) mfuegemann DT-FS-VR-MR Ext2k Conversion v1.2*). Other tables of this
ROM with another script are reported as "not needed": the text to patch is `Controller.Run` as this script writes it.

**Symptom.** Coins seem to be ignored: slot 1 needs 3 coins for a credit, and Start does not start a game, even with credits.

**Cause.** The DIP switches of this game hold its coin settings and more, and it does not work with all of them off. Its factory values
are `3C` and `25`; they read `0` and `0` in the game:

1. At the start of a table, `core.vbs` builds the table options of the DIP switches (`vpmOptionEvent`, `cvpmDips`): it takes the value of
   `Controller.Dip` as their default, then writes the options back to the DIP switches.
2. At that moment the PinMAME plugin does not have the DIP switches of the machine yet: they come after `Controller.Run`
   (`Controller::OnStateSrcChanged`). `Controller::GetDip` then gave 0, so 0 was written.
3. PinMAME saves the DIP switches in the last bytes of the NVRAM (`core_nvram`), and restores them at the next start.

This code came with the table options of the DIP switches (upstream 23e9fc112, 2026-10-04); the table played in the first score tests of
October, with an earlier build (not bisected). The system scripts register their own DIP switches (`peyper.vbs`), so a table without a DIP form has the same problem. `GetDip` now gives the
value of the machine until the plugin has it, which keeps new NVRAMs right, but an NVRAM that already holds zeros keeps them: this is what
the recipe repairs.

**Fix.** One line before `Controller.Run` sets the factory values. A value set before the start wins over the NVRAM, and `core.vbs` then
takes it as the default of the options. It is written at every start; a choice made in the menu of the table still wins. The factory values
were read with a new NVRAM (`PinmameGetDIP`). The NVRAM is not changed by the patch, and the DIP switches saved in it become right when the
table is closed. Its other contents are kept.

**Checked** on the Mac, 2026-10-31, with a copy of the Frame's NVRAM (DIP switches at 0), 6 coins in slot 1, then Start. Before: 2 credits, no
game. With the recipe, on the build without the `GetDip` change and on the one with it: `3C`/`25`, 1 credit per coin, and the game starts
(ball served, flippers on). With `DIP - Balls per game` set to 5 balls in the settings of the table: `1C`/`25`. Patch from the web page: the
script of the table with that one line added, CRLF kept.

**Checked** on the Frame, 2026-10-31, with the recipe in a `fixes.json` of the preferences folder and **Patch table**, on a build without the
`GetDip` change: coins add credits and the game plays in the headset, the log read 93,000 points from the machine, and the NVRAM then held
`3C`/`25`. No game was finished in that session, so no score was recorded yet.

**Not fixed.** The digits of the backglass stay at zero: they are painted in the image `backglassimage` of the primitive
`VR_Backglass_Marcador`, a 3D box that VPX does not convert to a backglass surface, and the table has no other display in VR.

History: version 1, 2026-10-31.

### <a id="darkest-dungeon-coop-option"></a>Darkest Dungeon: error message when the table starts

Applies to: tables whose file name contains `DarkestDungeon` or `Darkest Dungeon` (checked with *DarkestDungeon 3.06*, an original table without
a ROM).

**Symptom.** A runtime error message shows when the table starts (`Runtime error on line 179, col 1: Description unavailable` in the log). The
option "Cooperative Mode" is missing from the menu of the table. The game itself plays, and records its scores.

**Cause.** A bug of the script, in the routine of its options:

```vb
bCoOp = Table1.Option("Cooperative Mode", 1, 2, 1, 0, 0, Array("False (Default)","True"))

EnableLUTSelector = Table1.Option("Cooperative Mode", 1, 2, 1, 1, 0, Array("False (Default)","True"))

If eventId = 3 And dspTriggered Then dspTriggered = False : DisableStaticPreRendering = False : End If
```

The default value (0) is outside the range (1 to 2), which `PinTable::RegisterOption` refuses:
`Table.Option("Cooperative Mode"): invalid arguments (minValue=1, maxValue=2, step=1, defaultValue=0)`. The error stops `Table1_OptionEvent`,
so the lines above never run. Changing the default to 1 would not do: the script tests `If bCoOp AND ...`, so 1 and 2 both turn the mode on.
The second line reads the same option, a copy and paste: the LUT selector was meant to have its own.

**Fix.** The option goes from 0 to 1, keeping its labels and its default, False: 0 leaves the mode off and 1 turns it on. The line of the LUT
selector is commented out: `EnableLUTSelector` stays empty and the selector off, as with the error. The last line of the routine now runs,
so that static pre-rendering is turned on again when the options page closes.

**Checked** on the Mac, 2026-10-31. The lines were first run in another table: the original one gives the error, and the fixed one gives 0 by
default and 1 with `Cooperative_Mode = 1` in the settings of the table, with `bCoOp AND ...` false and true. Patch from the web page, on the
table itself: the script with those two lines changed, line ends kept. The table cannot be played on the Mac without its PuP pack (other script
errors, which the Frame does not have).

**Checked** on the Frame, 2026-10-31, with the recipe in a `fixes.json` of the preferences folder and **Patch table**: the message no longer shows
when the table starts.

History: version 1, 2026-10-31.

### <a id="aloha-coin-key"></a>Aloha (Gottlieb 1961): coins from the controllers

Applies to: tables whose file name contains `Aloha` (checked with *Aloha (Gottlieb 1961) VRROOM Ext2k Conversion*, an EM table without a
ROM). Another Aloha table without the line is reported as "not needed".

**Symptom.** The table loads and asks for no ROM, but no game can be started: the coin button does nothing, the credits stay at 0, and
Start needs a credit.

**Cause.** The script adds a credit for a hard-coded key code:

```vb
	If Keycode= 6 then
		playsound "coin3"
		If credits<47 then Credits=Credits+1:DOF 119, DOFOn:end if
```

6 is the DirectInput code of the `5` key. VPX sends the DirectInput code of a key to `KeyDown` for the keys of a keyboard only
(`InputManager`, `m_keyboardDeviceId`). An action sends `0x10000 | action id`, the value of `AddCreditKey`, whatever the device: the coin
button of the VR controllers, or of a gamepad, never sends 6. Start uses `StartGameKey`, and works. Upstream VPX has the same behaviour
with a gamepad. In the Frame library of 2026-11-05 (110 tables), only one other table tests `Keycode = 6` (*VR Room The Flintstones*, a
PinMAME table whose coins go through `core.vbs` and `AddCreditKey`).

**Fix.** The line tests `AddCreditKey` instead of 6. The `5` key of a keyboard sends both its code and the action, so it still adds one
credit (testing both would add two).

**Checked** on the Mac, 2026-11-05, with the table and an isolated preferences folder, by a line added at the end of `Aloha_Init` that
calls `Aloha_KeyDown AddCreditKey` (what the coin action calls) then `Aloha_KeyDown StartGameKey`. Original script: no credit, no game.
With the recipe (the text found once, CRLF kept): 1 credit, and the game starts (`GameinProgress` 1, 1 player). **To check** on the Frame:
coin and Start with the controllers.

History: version 1, 2026-11-05.

### <a id="sonic-hedgehog-vr-dmd"></a>Sonic The Hedgehog (Brendan Bailey 2005) VPX MOD: DMD in the backglass

Applies to: tables whose file name contains `Sonic The Hedgehog` (checked with *Sonic The Hedgehog (Brendan Bailey 2005) VPX MOD 1.33*, an
original table with UltraDMD and a `.directb2s`). Another version without the line is reported as "not needed".

**Symptom.** In VR the DMD stands upright on the playfield, at its top right, and the window under the backglass picture reads "This
table requires a directB2S file".

**Cause.** The table, not VPX. Its VR backglass is the primitive `Primary_backglass01` (x -166 to 1113, z 270 to 1450, in front of the
backbox at y -68), textured with `BackglassImage`: a picture with a DMD window at its bottom (x 152 to 798, z 314 to 491), in which the
text is part of the picture. Its DMD is the flasher `DMD` (545 x 153, rotated -90 degrees around X, height 150), whose points put it at
x 397 to 942, y 27, z 74 to 226: on the playfield. The script only makes it visible in VR (`If RenderingMode = 2`), and never moves it.
VPX 10.8.0 placed it at the same spot: the center of a flasher has always been taken from its points, not from the center stored in the
file (`FLAX`, `FLAY`). The `.directb2s` is found and loaded by the B2S plugin, but VPX only shows it in VR with **Add Backglass** (off
by default), on a panel of its own that does not match this backbox: smaller than the table's backglass, which still shows around it.

**Fix.** The line that shows the DMD in VR also moves it to the center of the window, just in front of the glass:
`DMD.X = 474 : DMD.Y = -62 : DMD.Height = 402` (x 202 to 746, z 326 to 478). The DMD is opaque and hides the text, except its first
and last letters ("Th", "le"): the text is 614 units wide, the DMD 545, and a script cannot change the size of a flasher.

**Checked** on the Mac, 2026-11-05, with the simulated headset of the `xr-sim` branch (`VPX_XRSIM=1`, left eye recorded): before, the DMD
on the playfield and the text in the window; with the recipe (the text found once, CRLF kept), the UltraDMD scoreboard in the window,
the playfield clear. **To check** on the Frame.

History: version 1, 2026-11-05.

### <a id="batman66-vr-scores"></a>Batman 66 (Original 2018): scores on the cabinet's DMD

Applies to: tables whose file name contains `Batman 66` and whose script plays the ROM `flash_l1` (checked with *VR ROOM Batman 66
(Original 2018) 1.0.1*, by LuvtheCubs on JPSalas' Flash, mod by Onevox, with a `.directb2s`). The Batman 66 tables made from
Stern's machine play another ROM and do not match. Another version without the two lines is reported as "not needed".

**Symptom.** In VR the DMD of the cabinet stays black, and no score shows anywhere: the score windows of the backglass picture (players
1 and 3, credits, ball) are empty. The game itself plays (the log shows the scores read from the ROM).

**Cause.** The table, not VPX. It is a retheme of Williams' Flash (1978, System 4), whose ROM has 7-segment displays and no DMD: four
scores of 6 digits, then 2 digits of credits (24-25) and 2 of ball in play (26-27). The script reads them in `UpdateLEDs` and shows them on
28 reels (`a1` to `e4`) of the desktop backglass, which VR never draws. Its VR backglass (`PinCab_Backglass`, image `BackglassImage`) is a
picture whose score windows are black, and the window of the speaker panel (`PinCab_Grills`) holds the flasher `DMD`, a DMD flasher that
the script hides at its start (`dmd.visible=0 'change to 0 if no pup pack`): it is kept for a PuP pack, and none exists for the table.
Showing it would not show the segments: a flasher in DMD mode only shows a DMD (the segment mode of a flasher is set in the table file,
not by the script), and FlexDMD in a segment render mode sends segments too.

**Fix.** In VR only (`RenderingMode = 2`), the script creates a FlexDMD of 128 x 32 (4 shades, tinted red by the color of the `DMD`
flasher), shows the flasher, and draws on it the scores of players 1 and 2 at the top, 3 and 4 at the bottom, the ball in play and the
credits in the middle. `UpdateLEDs` passes every digit that changes to it, decoded with the same segment codes as the reels. The stage is
cleared at every frame (`Clear = True`): otherwise a label whose text changes leaves the old digits under the new ones. Desktop and cabinet
modes keep the reels and the `.directb2s`.

**Checked** on the Mac, 2026-11-06, with the simulated headset of the `xr-sim` branch (left eye recorded every second, the table started
with its ROM, coins, Start, the plunger and `autoflip`): the scoreboard in the window of the speaker panel, the attract mode alternating
the high score (550,000) and the scores, the credits counting the coins and dropping at Start, ball 1 then 2, the score of player 1
rising from 0 to 8,750. **To check** on the Frame.

History: version 1, 2026-11-06.
