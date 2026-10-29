# Score Recording Test of the Steam Frame Library

2026-10-28

66 of the 73 tables on the Frame now record a score in the harness. The first run, on 2026-10-27, recorded 59; 7 more record after the fixes of 2026-10-28. Of the 7 left, none points to a score tracking problem: 3 games cannot be finished unattended, 2 tables have another problem, and 2 were not played again.

| The 14 tables that failed on 2026-10-27 | Tables | Count |
| --- | --- | --- |
| Fixed: they record now | 2001 A Space Odyssey, Diablo 2, Evel Knievel, Star Trek LE, Terminator 2, World Cup Soccer, Verne's World | 7 |
| The game cannot be finished unattended | Lord of the Rings, James Bond 007, Street Fighter 2 | 3 |
| Problem outside score tracking, still open | WaterWorld (no ball is served), Star Wars (Sonic 1987) (no coin or Start) | 2 |
| Not played again: they need their PuP packs | DarkestDungeon, Futurama | 2 |

Evel Knievel had recorded in October: its regression came from libpinmame, which the fork now patches. Star Wars (Sonic 1987) had recorded in October too; why it takes no coin now is open. James Bond 007 and Street Fighter 2 also failed unattended in October, and recorded fine when played by hand on the Frame.

## Results

One unattended game per table on 2026-10-27. The 12 failing tables without a PuP pack were played again on 2026-10-28, after the fixes; the last column says what fixed them. Score is the score of the recorded game, one per player for 2-player games.

| Table | Recorded | Source | Score | Why it failed, or what fixed it |
| --- | --- | --- | --: | --- |
| 2001 A Space Odyssey ChuckRibbits Mod VR Room - 1.2 | ✅ | Script | 32,150 | Fixed: the tracker used the backglass as its source, which only showed 0 and a credit digit (the table counts its balls on backglass player 5). Backglass players 5 and 6 now count only for machines of more than 4 players |
| Ace Of Speed 1.1 | ✅ | ROM memory | 236,010 |  |
| Addams Family (Bally 1992) | ✅ | ROM memory | 531,000 |  |
| Albator78 (VR ROOM) | ✅ | Script | 72,030 |  |
| Apollo (Williams 1967) | ✅ | Backglass | 1,334 |  |
| Attack from Mars Minimal (Bally 1995) | ✅ | ROM memory | 330,092,730 |  |
| Austin Powers (Stern 2001) | ✅ | ROM memory | 15,716,740 |  |
| Blade Runner 2049 | ✅ | Script | 1,128,550 |  |
| BleachVR | ✅ | Script | 9,320,720 |  |
| Bond 60th Dr.No | ✅ | Script | 35,080,050 |  |
| Bram Stoker's Dracula (Williams 1993) | ✅ | ROM memory | 3,604,280 |  |
| DarkestDungeon 3.06 | ❌ | – | – | Not played again: thousands of script errors a second (Object required); its PuP pack is not on the Mac |
| Diablo 2 (ChuckRibbits Original 2026) 1.2.1 | ✅ | Script | 200,000 | No tracking problem: the game waits for a difficulty choice confirmed by a second Start, which the first run did not press. Recorded with 2 Start presses |
| Evel Knievel (Bally 1977) | ✅ | ROM memory | 21,260 / 29,300 | Fixed: libpinmame applied the mask of the game over lamp before taking the nibble of the 4-bit memory, so the game never ended. Recorded with the fork's libpinmame patch and the map as upstream has it |
| Friday the 13th | ✅ | Script | 324,680 |  |
| Futurama (Original 2024) v1.2.2 | ❌ | – | – | Not played again: the game never started (no reaction to Start, 3 tries); its PuP pack is not on the Mac |
| Game of Thrones LE (Stern 2015) VPW 1.2.1 | ✅ | Script | 358,040 |  |
| Genesis (Gottlieb1986) VPW v1.0 | ✅ | ROM memory | 114,120 |  |
| Ghostbusters slimer 2.1 | ✅ | Script | 359,110 |  |
| Godzilla (Sega 1998) | ✅ | ROM memory | 5,370,780 |  |
| Goldeneye (Sega 1996) VPW 1.2.1 | ✅ | ROM memory | 14,701,320 |  |
| Harry Potter (Original 2020) VR Mega Room | ✅ | Script | 42,172 |  |
| Indiana Jones - The Pinball Adventure (Williams 1993) | ✅ | ROM memory | 553,220 |  |
| Iron Man Minimal (Stern 2010) | ✅ | ROM memory | 1,262,490 |  |
| James Bond 007 (Gottlieb 1980) | ❌ | – | – | Autoplay limit: game still running at 8 minutes (timed play). Recorded by hand on the Frame in October |
| JAWS 50Th Anniversary (Original 2025) | ✅ | Script | 4,074,680 |  |
| JohnWick (BABAYAGA Pinball edition, 2023) (VR ROOM Minimal) Sphere | ✅ | Script | 26,090 |  |
| Lord of the Rings (Stern 2003) VPW 1.6 | ❌ | – | – | Autoplay limit: a ball stays trapped and keeps scoring (482,940 at 8 minutes), so the game never ends unattended |
| Mass Effect 1.0.8 | ✅ | ROM memory | 167,087,530 |  |
| Masters of the Universe-custom (VR ROOM edition) | ✅ | Script | 714,000 |  |
| Maverick (Data East 1994) | ✅ | ROM memory | 34,653,930 |  |
| One Piece | ✅ | Script | 3,392,920 |  |
| Pirates of the Caribbean | ✅ | ROM memory | 3,305,780 |  |
| Playboy 35th Anniversary (Data East 1989) v4.3 JP Salas, Ext2k VRROOM | ✅ | ROM memory | 87,200 |  |
| Pokemon Pinball | ✅ | Script | 664,050 |  |
| Predator (Gottlieb 1986) | ✅ | ROM memory | 66,090 |  |
| Ready Player One | ✅ | Script | 3,975,611 |  |
| Robocop (Data East 1989) | ✅ | ROM memory | 220,200 |  |
| Room Rocky (Gottlieb 1982) | ✅ | ROM memory | 35,940 |  |
| Scooby Doo 2022 | ✅ | Script | 3,042,270 |  |
| Simpsons Treehouse of Horror MOD v2023.3 (Simpsons Pinball Party, The (Stern 2003) VPW 2.0.3 base) | ✅ | ROM memory | 622,800 |  |
| South Park (Sega 1999) | ✅ | ROM memory | 23,781,170 |  |
| Space Cadet (Original 2021) | ✅ | Script | 156,250 |  |
| Star Trek LE (Stern 2013) v1.09 | ✅ | ROM memory | 1,712,390 | Fixed: Stern SAM map with no game over flag. New game on solenoid rule (`st_161h`, solenoid 33) |
| Star Wars (Sonic 1987) | ❌ | – | – | Open: the machine takes no coin or Start and its displays stay dark, with any NVRAM and PinMAME version tried. Recorded in October |
| Star Wars Trilogy | ✅ | ROM memory | 1,358,780 |  |
| StarCraft (Sega 1997) v1.2.1 | ✅ | ROM memory | 10,095,250 |  |
| Stargate Minimal (Gottlieb 1995) | ✅ | ROM memory | 23,153,060 |  |
| Starship Troopers (Sega 1997) | ✅ | ROM memory | 7,255,390 |  |
| Street Fighter 2 | ❌ | – | – | Autoplay limit: ball stuck after the first points. Recorded by hand on the Frame in October |
| Super Mario Bros v 1.2 | ✅ | ROM memory | 1,559,000 |  |
| Super Mario Bros. Mushroom World (Gottlieb 1992) | ✅ | ROM memory | 3,180,000 |  |
| Symphony IX - The Machine of Joy (Original 2026) | ✅ | Script | 190,200 |  |
| Terminator 3 Rise of the Machines | ✅ | ROM memory | 7,688,680 |  |
| The Flintstones | ✅ | ROM memory | 25,619,660 |  |
| The Goonies Never Say Die Pinball (VPW 2021) 1.5 | ✅ | Script | 248,650 |  |
| The Lost World Jurassic Park (Sega 1997) | ✅ | ROM memory | 631,590 |  |
| The Mandalorian | ✅ | Script | 285,870 |  |
| The Simpsons (Data East 1990) | ✅ | ROM memory | 94,900 |  |
| The Walking Dead | ✅ | ROM memory | 1,531,140 |  |
| Tmnt | ✅ | ROM memory | 881,950 |  |
| TopGun (2019)-(VR ROOM) | ✅ | ROM memory | 69,224 |  |
| Twilight Zone | ✅ | ROM memory | 4,800,000 |  |
| Ulysse 31 | ✅ | ROM memory | 166,040 |  |
| Verne's World (Spinball 1996)1.0.1 | ✅ | ROM memory | 255,005 / 393,013 | Fixed: no memory map for Spinball games. New Spinball platform and `vrnwrld` map (the first launch hang of this ROM remains) |
| VP Cooks- The Matrix PuPevents-v1.0.14 | ✅ | Script | 74,680 |  |
| VR ROOM Back to the Future (Data East 1990) V103 | ✅ | ROM memory | 160,000 |  |
| VR Room Terminator 2 (Williams 1991) g5k v1.11 | ✅ | ROM memory | 411,490 | No tracking problem: on its first launch without NVRAM the machine stays on FACTORY SETTINGS RESTORED. Records from the second launch |
| VR ROOM The Fifth Element | ✅ | Script | 43,550 |  |
| VR ROOM WaterWorld (Gottlieb 1995) 1.2 | ❌ | – | – | Open: the ROM started with zero settings, so no game could start; `waterwld` was added to the Gottlieb System 3 factory settings fix, and the tracker now ignores what a ROM fix shows (it had recorded a 1,400,000,000 point game). A game now starts, but no ball is served |
| World Cup Soccer (Bally 1994) VPW v1.5 | ✅ | ROM memory | 19,300,000 | Same as Terminator 2: records from the second launch |
| Wrath of Olympus (Original 2022) | ✅ | Script | 4,080,770 |  |
| X-Files (Sega 1997) Ejse83 LED Mod (Siggis Mod Table) | ✅ | ROM memory | 1,262,780 |  |

## How the tables were tested

Each table was played once, unattended, on the Mac build of fork master (5d6abe801) with the score coverage harness (`Doc/test-hooks/score-coverage.py`), using the Frame's library file so every table keeps its Frame id.

- **What the harness does:** waits for the machine to boot, adds 2 credits, starts a 1-player game, then launches the ball and flips every 5 s until the balls drain. It quits 4 s after a game is recorded, or after 480 s.
- **Green check:** the score tracker recorded a finished game with a score above zero.
- **Red mark:** no game was recorded. The reason column gives the tracker's last log line.
- **Limits:** a red mark can also come from the autoplay (ball stuck, single-player machine, timed play) rather than the tracker. James Bond 007 and Street Fighter 2 failed this way in October and recorded fine when played by hand. The Mac has the Frame's ROMs and aliases, but only the TopGun PuP pack, so PuP tables ran without their videos. The first run fixed nothing.
- **Follow-up (2026-10-28):** the 12 failing tables without a PuP pack were played again on the Mac, after the fixes were pushed to fork master (d0ad00ca7 to 62b5fcc75). Diablo 2 got 2 Start presses. Terminator 2 and World Cup Soccer were played a second time with the NVRAM of their first launch. Evel Knievel was played with the patched libpinmame and the original map; the fork carries that patch since 142670600. Details: `docs/Table Scores and Leaderboards.md` (sections 3, 4, 5, 12 and appendix A), `docs/Table and ROM Fixes.md` and `docs/PinMAME Memory Maps on 4-bit Memory.md`.
- **Not checked on the Frame yet:** the fixes of the follow-up.
