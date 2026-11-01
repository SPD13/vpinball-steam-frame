# Memory maps: libpinmame reads under the banks of the main CPU (upstream PR candidate)

*2026-11-01. For `vpinball/pinmame` (`src/memory.c`, `memory_get_read_ptr`, and `src/libpinmame/libpinmame.cpp`, `SetMemMapImpl`).*

The fields of a memory map are read at the address the game uses, with `memory_find_base`, which ignores banks. When the main CPU reads
an address through a bank, the map reads the page under the bank, which nothing ever writes. All the Williams System 7 games are concerned:
their map loads, every field reads 0, no score ever shows and no game ends. The change below follows the bank.

## Symptom

On the Steam Frame fork, the leaderboards record nothing on TENACIOUSD Flex Mod (a mod of Firepower II, ROM `fpwr2_l2`), played by hand in
the headset: the log says `Memory map of the ROM: 4 player score(s), game over flag, player count`, then `no score source found`. The
map is the generic one of System 7 (`maps/williams/system7/generic-10KB.map.json`), with scores at `0x38`, game over at `0xC9`, player
count at `0xCC`. Its platform says of that page: "Mapped to a portion of RAM at 0x1000".

## Cause

`src/wpc/s7.c` runs the first page of the 6808 on a bank of the RAM at `0x1000`:

```c
{ 0x0000, 0x00ff, MRA_BANKNO(S7_BANK0)},   /* read, and MWA_BANKNO for write */
{ 0x1000, 0x13ff, MRA_RAM},                /* CMOS, and MWA_RAM, &s7_rambankptr */
...
cpu_setbank(S7_BANK0, s7_rambankptr);
```

Two functions stand between a map and that memory:

- `SetMemMapImpl` asks `memory_find_base(0, address)`, the plain base of the CPU plus the address: the page under the bank.
- `memory_get_read_ptr`, which does know the read table, returns NULL for a bank on an 8-bit bus (`minbits == 0 && entry != STATIC_RAM`,
  and `minbits` is 0 on all of them). The read handlers of the same file serve banks the same way on every bus width
  (`if (entry <= STATIC_RAM) MEMREADEND(cpu_bankbase[entry][...])`). `PinmameReadMainCPUMemory` then falls back to `memory_find_base` too,
  so that API reads under the bank as well.

Fixing the reader alone changes nothing, which is how we learned of the second one.

## Change

- `memory_get_read_ptr` gives the pointer for a bank on every bus width, and NULL for the invalid entry or a bank that is not set.
- `SetMemMapImpl` asks `memory_get_read_ptr` first, and `memory_find_base` when it gets no pointer: a read handler, as Whitestar and Sega
  route all their main RAM through `ram_r`, with the memory behind it at the base (the reasoning of `ReadThroughRamBase`).

`memory_get_write_ptr` keeps its behaviour: nothing here needs it.

What else reads differently: only addresses under a bank of CPU 0. In the drivers, the main CPU's banks over the low memory are System 7
and `kissproto` (`0x0000`-`0x0FFF`); the banks of WPC and Data East are ROM and DMD ranges no map field points at. For those,
`PinmameReadMainCPUMemory` now returns the bank the CPU sees, not the page under it.

## Checks

Driving the patched libpinmame directly (a small program: `PinmameRun("fpwr2_l2")`, switches, `PinmameReadMainCPUMemory`, no table):

| | Before | After |
|---|---|---|
| Page `0x00` compared to `0x1000` | all 0, while `0x1000` holds 50 to 70 non-zero bytes | identical |
| Game over `0xC9` | 0 | 1 in attract mode, 0 from the Start button on |
| Player count `0xCC` | 0 | 0, then 1, 2, 3 with each further press of Start |
| Player 1 `0x38` | `00000000` | `ffffff00` at the start of the game, climbing to `ff207300` (207,300: the existing BCD decoding reads `f` as 0) |

Regression in VPX, a game played by the autoplay of the test tools and recorded through the map, on 8 machines: Addams Family (WPC), Evel
Knievel (Bally 4-bit), Star Trek LE (Stern SAM), Terminator 3 (Stern Whitestar), The Lost World (Sega), Rocky (Gottlieb System 80), Super
Mario Mushroom World (Gottlieb System 3), Verne's World (Spinball): all 8 recorded a score.

Not run so far: a full game of a System 7 table through VPX, so the record at the end of the game and game over going back to 1 have not
been seen (the program above could not drain the balls, and the table is on the Frame, asleep at the time); Data East and Peyper.
On the first boot with no NVRAM, `0xC9` stayed 0 and `0xCB` 1, and the program's coins and Start got no reaction in that run; the
boots after it behave as in the table.

## What the fork does meanwhile

The fork carries the patch: `steamframe/patches/pinmame/02-memmap-follow-banks.patch`, after the
[patch of 4-bit memory](PinMAME%20Memory%20Maps%20on%204-bit%20Memory.md), with the cache tag `-memmap2`. How it is kept at each sync:
`steamframe/README.md`, "Patches of the dependencies".

## Draft of the PR

**Title:** libpinmame: follow the banks of the main CPU when reading memory maps

**Body:**

> Williams System 7 runs its first page of memory (`0x0000`-`0x00FF`) on a bank of its RAM at `0x1000`. The memory map reader uses
> `memory_find_base`, which ignores banks, so with the generic System 7 map the scores (`0x38`), game over (`0xC9`) and player count (`0xCC`)
> read a page that nothing writes: 0 forever. `memory_get_read_ptr` cannot be used as it is, since it returns NULL for a bank on an 8-bit
> bus, although the read handlers of `memory.c` serve banks the same on every bus width.
>
> This change lets `memory_get_read_ptr` return the pointer of a bank on every bus width, and makes the memory map getters use it, with
> `memory_find_base` as the fallback for read handlers (`se.c`). `PinmameReadMainCPUMemory` gets the bank the CPU sees as a result.
>
> Checked: `fpwr2_l2` driven through libpinmame (page `0x00` identical to `0x1000`, game over and player count follow the game, score
> climbing), and a game recorded through the map on WPC, Bally AS-2518-17, Stern SAM and Whitestar, Sega, Gottlieb System 3 and System 80, and
> Spinball machines.
