# Score Recording Test of the Steam Frame Library

2026-11-01

89 of the 91 tables that stay on the Frame are now confirmed: 81 record a score in the harness, and 8 more work when played by hand in the headset. The 20 tables added since the first run were played on 2026-10-31: 15 record, X-Men LE among them with a score rule added to the fork on 2026-11-01, not played on the Frame yet, and TombRaider was confirmed by hand. Dragon Ball is not compatible with the Frame and was removed. TENACIOUSD Flex Mod records since its libpinmame fix, confirmed by hand on the Frame (59,010). CyberRace was also played by hand and does not record; World JOKER Tour has no memory map. The original Star Wars (Sonic 1987), the last table left from the first run, is no longer in the Frame's library; the Hybrid MOD of the same ROM records. The first run, on 2026-10-27, recorded 59; 7 more recorded after the fixes of 2026-10-28.

| The 14 tables that failed on 2026-10-27 | Tables | Count |
| --- | --- | --- |
| Fixed: they record in the harness | 2001 A Space Odyssey, Diablo 2, Evel Knievel, Star Trek LE, Terminator 2, World Cup Soccer, Verne's World | 7 |
| The game cannot be finished unattended; works by hand | Lord of the Rings, James Bond 007, Street Fighter 2 | 3 |
| Needs its PuP pack; records by hand | DarkestDungeon (176,440), Futurama (44,549,770, with a new score rule) | 2 |
| No ball served on the Mac; works by hand | WaterWorld | 1 |
| Plays by hand since a fix; no game finished yet | Star Wars (Sonic 1987) | 1 |

Two of these were real gaps. Futurama keeps its scores in a variable the tracker did not know, which a score rule fixed. Star Wars (Sonic 1987) took no coin because VPX saved its DIP switches as zeros, which a table fix and a change of the PinMAME plugin fixed. Its backglass digits stay at zero because they are painted in the image of the table; live digits are postponed.

The same DIP switch bug zeroed 15 other ROMs in the Mac's copy of the NVRAMs. What that changes in those games has not been checked. Evel Knievel had recorded in October: its regression came from libpinmame, which the fork now patches.

| The 20 tables added since 2026-10-27 | Tables | Count |
| --- | --- | --- |
| Record in the harness | Aladdin, Bad Girls Tooned-Up, Baywatch, Cannes, CityHunter, DUNE, Eight Ball, Last Action Hero, Legend of Zelda, Medusa, Star Wars Hybrid MOD, Stranger Things, Surf Champ, WildWest | 14 |
| Records with a new score rule, in the fork since 2026-11-01; not played on the Frame yet | X-Men LE | 1 |
| The game cannot be finished unattended; records by hand | TombRaider (20,761 / 14,852) | 1 |
| No score source; needs a table fix or a memory map | CyberRace, World JOKER Tour | 2 |
| Its memory map read no score; records by hand since the libpinmame fix | TENACIOUSD Flex Mod (59,010) | 1 |
| Not compatible with the Frame; removed | Dragon Ball Super Saiyan Edition (mesh over the load limits) | 1 |

The library now lists 92 entries for its 91 tables: Predator (Gottlieb 1986) is there twice, in Predator-Gottlieb-1986 and Predator-Gottlieb-1986-2, and the two .vpx files are identical, so the copy was not played. The Frame's leaderboard also holds a 100,000 for Dragon Ball, from the high score source with a duration of 0 s: the table does not start on the current build, so this is not a game played on it.

## Results

One unattended game per table on 2026-10-27. The 12 failing tables without a PuP pack were played again on 2026-10-28, after the fixes; the last column says what fixed them. Score is the score of the recorded game, one per player for 2-player games. By hand marks a table that failed unattended and was confirmed in the headset on 2026-10-31; a score is given when it was read from the Frame. The 20 tables added since then were played on 2026-10-31.

| Table | Recorded | Source | Score | Why it failed, or what fixed it |
| --- | --- | --- | --: | --- |
| 2001 A Space Odyssey ChuckRibbits Mod VR Room - 1.2 | ✅ | Script | 32,150 | Fixed: the tracker used the backglass as its source, which only showed 0 and a credit digit (the table counts its balls on backglass player 5). Backglass players 5 and 6 now count only for machines of more than 4 players |
| Ace Of Speed 1.1 | ✅ | ROM memory | 236,010 |  |
| Addams Family (Bally 1992) | ✅ | ROM memory | 531,000 |  |
| Aladdin (Original 2026) Tombg | ✅ | Script | 71,410 | Its script uses PuP; the Frame has no pack for it either, so it ran without one |
| Albator78 (VR ROOM) | ✅ | Script | 72,030 |  |
| Apollo (Williams 1967) | ✅ | Backglass | 1,334 |  |
| Attack from Mars Minimal (Bally 1995) | ✅ | ROM memory | 330,092,730 |  |
| Austin Powers (Stern 2001) | ✅ | ROM memory | 15,716,740 |  |
| Bad Girls Tooned-Up (Gottlieb 1988) | ✅ | ROM memory | 395,000 |  |
| Baywatch (Sega 1995) | ✅ | ROM memory | 23,555,260 |  |
| Blade Runner 2049 | ✅ | Script | 1,128,550 |  |
| BleachVR | ✅ | Script | 9,320,720 |  |
| Bond 60th Dr.No | ✅ | Script | 35,080,050 |  |
| Bram Stoker's Dracula (Williams 1993) | ✅ | ROM memory | 3,604,280 |  |
| Cannes (Segasa 1977) Klodo81 V2.0 DT-FS-VR-MR Ext2k Conversion | ✅ | Backglass | 213,370 |  |
| CityHunter | ✅ | Backglass | 58,641 | Played with its PuP pack |
| CyberRace | ❌ | – | – | No score source: the game starts and ends (about 100 s), but the script keeps its scores in a dictionary of dictionaries (playerState), which the tracker cannot read. It has no ROM or backglass scores either. Confirmed by hand on the Frame (2026-10-31): no score source found. Needs a table fix that exposes the scores, or tracker support |
| DarkestDungeon 3.06 | By hand | Script | 176,440 | Not played again: thousands of script errors a second (Object required); its PuP pack is not on the Mac. Those errors come from the missing pack: the script reads a file of the pack at its start. Recorded when played by hand in the headset with its pack: 176,440 (2026-10-31) |
| Diablo 2 (ChuckRibbits Original 2026) 1.2.1 | ✅ | Script | 200,000 | No tracking problem: the game waits for a difficulty choice confirmed by a second Start, which the first run did not press. Recorded with 2 Start presses |
| Dragon Ball Super Saiyan Edition 1.3.3 (VR ROOM Mega) (Original 2025) | Not compatible | – | – | Not compatible with the Frame, and removed from its library (owner, 2026-10-31). VPX refuses the table: its VR_Room mesh (5,024,320 vertices) is over the upstream load limits. With limits 8 times higher it loaded but was unplayable on the Frame (8 GB of memory) |
| DUNE v1.4 | ✅ | Script | 101,200 |  |
| Eight Ball (Bally 1977) | ✅ | ROM memory | 24,960 |  |
| Evel Knievel (Bally 1977) | ✅ | ROM memory | 21,260 / 29,300 | Fixed: libpinmame applied the mask of the game over lamp before taking the nibble of the 4-bit memory, so the game never ended. Recorded with the fork's libpinmame patch and the map as upstream has it |
| Friday the 13th | ✅ | Script | 324,680 |  |
| Futurama (Original 2024) v1.2.2 | By hand | Script | 44,549,770 | Not played again: the game never started (no reaction to Start, 3 tries); its PuP pack is not on the Mac. Its scores have their own name (anScore), which the tracker did not know. Fixed by a score rule (fork master 8cc9b503f): recorded when played by hand in the headset, 44,549,770 (2026-10-31) |
| Game of Thrones LE (Stern 2015) VPW 1.2.1 | ✅ | Script | 358,040 |  |
| Genesis (Gottlieb1986) VPW v1.0 | ✅ | ROM memory | 114,120 |  |
| Ghostbusters slimer 2.1 | ✅ | Script | 359,110 |  |
| Godzilla (Sega 1998) | ✅ | ROM memory | 5,370,780 |  |
| Goldeneye (Sega 1996) VPW 1.2.1 | ✅ | ROM memory | 14,701,320 |  |
| Harry Potter (Original 2020) VR Mega Room | ✅ | Script | 42,172 |  |
| Indiana Jones - The Pinball Adventure (Williams 1993) | ✅ | ROM memory | 553,220 |  |
| Iron Man Minimal (Stern 2010) | ✅ | ROM memory | 1,262,490 |  |
| James Bond 007 (Gottlieb 1980) | By hand | – | – | Autoplay limit: game still running at 8 minutes (timed play). Recorded by hand on the Frame in October. Works when played by hand in the headset (2026-10-31) |
| JAWS 50Th Anniversary (Original 2025) | ✅ | Script | 4,074,680 |  |
| JohnWick (BABAYAGA Pinball edition, 2023) (VR ROOM Minimal) Sphere | ✅ | Script | 26,090 |  |
| Last Action Hero (Data East 1993) | ✅ | ROM memory | 8,511,370 | Also recorded 29,766,490 / 21,620,050 on the Frame, 2 players, with the libpinmame patch of banks |
| Legend of Zelda Pup VR | ✅ | Script | 41,000 | Played without its PuP pack (2 GB, too big for the Mac) |
| Lord of the Rings (Stern 2003) VPW 1.6 | By hand | – | – | Autoplay limit: a ball stays trapped and keeps scoring (482,940 at 8 minutes), so the game never ends unattended. Works when played by hand in the headset (2026-10-31) |
| Mass Effect 1.0.8 | ✅ | ROM memory | 167,087,530 |  |
| Masters of the Universe-custom (VR ROOM edition) | ✅ | Script | 714,000 |  |
| Maverick (Data East 1994) | ✅ | ROM memory | 34,653,930 |  |
| Medusa (Bally 1981) UnclePaulie v2.0.1 DT-FS-VR-MR Ext2k Conversion | ✅ | ROM memory | 117,640 |  |
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
| Star Wars (Sonic 1987) | ❌ | – | – | On the Mac on 2026-10-28: the machine took no coin or Start. Recorded in October. Cause found: VPX saved its DIP switches as zeros. Fixed by a table fix (fork master fbba1dd57): plays in the headset (2026-10-31). No game finished there yet. The digits of its backglass stay at zero, as they are painted in the image. No longer in the Frame's library on 2026-10-31: see the Hybrid MOD below |
| Star Wars (Sonic 1987) Physics Sound Hybrid MOD | ✅ | ROM memory | 642,000 | Played with the Frame's NVRAM (DIP switches 3C 25) and the PinMAME plugin change. Same ROM as the row above. Its script updates digits on its VR backglass, not seen in the headset yet |
| Star Wars Trilogy | ✅ | ROM memory | 1,358,780 |  |
| StarCraft (Sega 1997) v1.2.1 | ✅ | ROM memory | 10,095,250 |  |
| Stargate Minimal (Gottlieb 1995) | ✅ | ROM memory | 23,153,060 |  |
| Starship Troopers (Sega 1997) | ✅ | ROM memory | 7,255,390 |  |
| Stranger Things (Original 2020) | ✅ | UltraDMD | 273,050 | Played without its PuP pack (2.6 GB, too big for the Mac) |
| Street Fighter 2 | By hand | – | – | Autoplay limit: ball stuck after the first points. Recorded by hand on the Frame in October. Works when played by hand in the headset (2026-10-31) |
| Super Mario Bros v 1.2 | ✅ | ROM memory | 1,559,000 |  |
| Super Mario Bros. Mushroom World (Gottlieb 1992) | ✅ | ROM memory | 3,180,000 |  |
| Surf Champ (Gottlieb 1976) EMU v2.0.1 Hybrid | ✅ | Backglass | 15,090 |  |
| Symphony IX - The Machine of Joy (Original 2026) | ✅ | Script | 190,200 |  |
| TENACIOUSD Flex Mod | ✅ | ROM memory | 59,010 | Not played by the harness: the ROM fpwr2_l2.zip was missing. Once it was added, the owner played it on the Frame (2026-10-31): the tracker loads the map (Williams System 7, 10 KB) but never sees a score. Cause, confirmed on 2026-11-01 by driving the ROM directly on the Mac: the map reads the scores at 0x38, but System 7 banks that range onto its RAM at 0x1000, and libpinmame did not follow the bank, so it read memory the game never writes. Fix in fork master (libpinmame patch 02-memmap-follow-banks, cache tag -memmap2): the same memory then reads correctly, with game over going to 0 at Start and the score climbing to 207,300. Confirmed by hand on the Frame on 2026-11-01: a 1-player game recorded 59,010, with the end of the game read from the game over flag. A 2-player game has not been played |
| Terminator 3 Rise of the Machines | ✅ | ROM memory | 7,688,680 |  |
| The Flintstones | ✅ | ROM memory | 25,619,660 |  |
| The Goonies Never Say Die Pinball (VPW 2021) 1.5 | ✅ | Script | 248,650 |  |
| The Lost World Jurassic Park (Sega 1997) | ✅ | ROM memory | 631,590 |  |
| The Mandalorian | ✅ | Script | 285,870 |  |
| The Simpsons (Data East 1990) | ✅ | ROM memory | 94,900 |  |
| The Walking Dead | ✅ | ROM memory | 1,531,140 |  |
| Tmnt | ✅ | ROM memory | 881,950 |  |
| TombRaider | By hand | Backglass | 20,761 / 14,852 | Autoplay limit: played twice on the Mac with its PuP pack, the score stops a few seconds after the launch (2,175, then 2,775 with a longer plunger pull) and the game is still running at 8 minutes; the cause was not seen. Works when played by hand in the headset (2026-10-31) |
| TopGun (2019)-(VR ROOM) | ✅ | ROM memory | 69,224 |  |
| Twilight Zone | ✅ | ROM memory | 4,800,000 |  |
| Ulysse 31 | ✅ | ROM memory | 166,040 |  |
| Verne's World (Spinball 1996)1.0.1 | ✅ | ROM memory | 255,005 / 393,013 | Fixed: no memory map for Spinball games. New Spinball platform and `vrnwrld` map (the first launch hang of this ROM remains) |
| VP Cooks- The Matrix PuPevents-v1.0.14 | ✅ | Script | 74,680 |  |
| VR ROOM Back to the Future (Data East 1990) V103 | ✅ | ROM memory | 160,000 |  |
| VR Room Terminator 2 (Williams 1991) g5k v1.11 | ✅ | ROM memory | 411,490 | No tracking problem: on its first launch without NVRAM the machine stays on FACTORY SETTINGS RESTORED. Records from the second launch |
| VR ROOM The Fifth Element | ✅ | Script | 43,550 |  |
| VR ROOM WaterWorld (Gottlieb 1995) 1.2 | By hand | – | – | On the Mac: the ROM started with zero settings, so no game could start; `waterwld` was added to the Gottlieb System 3 factory settings fix, and the tracker now ignores what a ROM fix shows (it had recorded a 1,400,000,000 point game). A game now starts, but no ball is served. Works when played by hand in the headset (2026-10-31) |
| WildWest | ✅ | Script | 18,500 | Its script uses PuP; the Frame has no pack for it either, so it ran without one. Also recorded 131,900 on the Frame |
| World Cup Soccer (Bally 1994) VPW v1.5 | ✅ | ROM memory | 19,300,000 | Same as Terminator 2: records from the second launch |
| World JOKER Tour (Stern 2006) | ❌ | – | – | No score source: there is no memory map for its ROM, wpt_140a (World Poker Tour), and the script and backglass give no scores. Needs a new memory map |
| Wrath of Olympus (Original 2022) | ✅ | Script | 4,080,770 |  |
| X-Files (Sega 1997) Ejse83 LED Mod (Siggis Mod Table) | ✅ | ROM memory | 1,262,780 |  |
| X-Men LE (Stern 2012) | ✅ | ROM memory | 735,370 | Stern SAM map with no game over flag: the first try saw scores but never the end of the game. Recorded with a game on solenoid rule (xmn_151h, solenoid 33), as for the other SAM tables. The rule is in fork master since 2026-11-01, tried on the Mac only: a game still has to be played on the Frame with the next build |

## How the tables were tested

Each table was played once, unattended, on the Mac build of fork master (5d6abe801) with the score coverage harness (`Doc/test-hooks/score-coverage.py`), using the Frame's library file so every table keeps its Frame id.

- **What the harness does:** waits for the machine to boot, adds 2 credits, starts a 1-player game, then launches the ball and flips every 5 s until the balls drain. It quits 4 s after a game is recorded, or after 480 s.
- **Green check:** the score tracker recorded a finished game with a score above zero.
- **Red mark:** no game was recorded, and no hand test has confirmed the table. The reason column gives the tracker's last log line.
- **By hand:** no game was recorded unattended, and the owner confirmed the table in the headset on 2026-10-31.
- **Limits:** a red mark can also come from the autoplay (ball stuck, single-player machine, timed play) rather than the tracker. James Bond 007 and Street Fighter 2 failed this way in October and recorded fine when played by hand. The Mac has the Frame's ROMs and aliases, but only the TopGun PuP pack, so PuP tables ran without their videos. The first run fixed nothing.
- **Follow-up (2026-10-28):** the 12 failing tables without a PuP pack were played again on the Mac, after the fixes were pushed to fork master (d0ad00ca7 to 62b5fcc75). Diablo 2 got 2 Start presses. Terminator 2 and World Cup Soccer were played a second time with the NVRAM of their first launch. Evel Knievel was played with the patched libpinmame and the original map; the fork carries that patch since 142670600. Details: `docs/Table Scores and Leaderboards.md` (sections 3, 4, 5, 12 and appendix A), `docs/Table and ROM Fixes.md` and `docs/PinMAME Memory Maps on 4-bit Memory.md`.
- **Hand tests (2026-10-31):** the 7 tables left were played in the headset. The fixes found were pushed to fork master: the PinMAME plugin's DIP switches (315365313), the recipes for Star Wars (fbba1dd57) and for the startup message of DarkestDungeon (40ef0a7d9), and the score rule of Futurama (8cc9b503f). The recipes and the rule were tried on the Frame through files in its preferences folder.
- **Not checked on the Frame yet:** the fixes of the 2026-10-28 follow-up, and the PinMAME plugin change, which is in the Frame build of 2026-11-01 but has not been checked on Star Wars there yet.

  Added tables (2026-10-31): the 20 tables were played one at a time on the Mac build of fork master (0b156108a) with the same harness, after copying the 6 ROMs the Mac lacked, the Frame's aliases and its Star Wars NVRAM. CityHunter and TombRaider ran with their PuP packs. Legend of Zelda and Stranger Things ran without theirs, which are too big for the Mac, so they should still be played once in the headset with their packs. X-Men LE was played a second time with its rule, TombRaider with a plunger pull of 2.5 s. These results come from the Mac, except TombRaider: the owner played a 2-player game on the Frame, which recorded 20,761 / 14,852. Also played by the owner on the Frame: CyberRace, which recorded nothing; TENACIOUSD Flex Mod, which recorded nothing before its libpinmame fix and 59,010 after it, on the build of fork master 294aa8410 (2026-11-01); and Last Action Hero on the same build, 2 players, 29,766,490 / 21,620,050.
