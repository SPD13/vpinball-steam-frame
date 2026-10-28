# Memory maps: libpinmame masks 4-bit memory before taking its nibble (upstream PR candidate)

*2026-10-28. For `vpinball/pinmame` (`src/libpinmame/libpinmame.cpp`, `SetMemMapImpl`). The patch and the test tools are in
`test-hooks/memmap-nibble-mask/` of the workspace documents ([SPD13/vp_vr-steam-frame-docs](https://github.com/SPD13/vp_vr-steam-frame-docs)).*

libpinmame decodes the fields of a memory map (github.com/tomlogic/pinmame-nvram-maps) as if their `mask` applied to the byte as
PinMAME stores it. The format applies it after the nibble of a 4-bit memory is taken, and PinMAME keeps the 4-bit RAM of the Bally
AS-2518-17/-35 boards in the high nibble with the low one forced to `0xF`, so on those boards every masked field reads a constant. The
masked fields of 4-bit memories are the `game_over` and `tilted` lamps of 43 maps, used by 169 ROMs (Bally, and Stern MPU-100 whose
low nibble is not forced). A program relying on `game_over` (score recording, game end detection) never sees a game end on the Bally
machines. The change below applies the nibble first, as the format's reference parser does, and removes the workaround added in August
for Centaur (whose `game_over` read constant "over"), which only turned the constant into "in game".

## Symptom

On the Steam Frame fork, the leaderboards record a game of Evel Knievel (Bally 1977, ROM `evelknie`, AS-2518-17) when the machine's
`game_over` state of the map goes to 1. It never did: the state read 0 ("in game") from the boot on, through attract mode and after
the game, while the backglass showed GAME OVER. The game was only recorded when another signal ended it (the player count going back to
0, which this machine does not always do: on 2026-10-28 it stayed at 2 after the game).

The map (`maps/bally/as-2518-17/system-rom-20.map.json`, 8 ROMs) reads the game over lamp like every Bally map of the project:

```json
"game_over": { "label": "Game Over", "start": "0x20C", "encoding": "bool", "mask": "0x4", "invert": true }
```

and its platform (`platforms/bally-17-4K.json`) declares the NVRAM `0x200`-`0x2FF` with `"nibble": "high"`.

## How PinMAME stores the 4-bit RAM

The 5101 CMOS RAM of these boards is 4 bits wide, on the high data lines. PinMAME emulates it in `src/wpc/by35.c`:

```c
static WRITE_HANDLER(by35_CMOS_w) {
  by35_CMOS[offset] = data | ((core_gameData->gen & (GEN_STMPU100|GEN_STMPU200|GEN_ASTRO))? 0x00 : 0x0f);
}
```

so on Bally boards the low nibble always reads `0xF` (the unconnected lines), and the data is in the high nibble. Read from the running
emulation (`PinmameReadMainCPUMemory`, tool `rawbytes.cpp` of the workspace documents), Evel Knievel in attract mode: all 256 bytes of `0x200`-`0x2FF` end in
`F`, and the lamp byte `0x20C` is `0xAF`. During a game it is `0xEF`: the probe of the memory through a whole game (VPX score coverage
harness, the region read as high nibbles) gave `0xE` from the Start press to the end of the game, `0xA` or `0x8` in attract mode before
and after. Bit 2 of the high nibble (`0x40` of the byte) is the lamp, set while a game is played (inverted: it is the game over lamp
off).

The commit that added writing through memory (#634, 2026-10-15) says the same: "early Bally stores only the high nibble".

Stern MPU-100/200 and Astro boards store the byte as written (no `0x0F`): their low nibble is whatever the CPU wrote on lines the real
RAM does not keep.

## What the format says

- `README.md` of pinmame-nvram-maps, platform files: **nibble** "`high`" or "`low`" identifies which 4-bit nibble to use from each
  address. The `bally-stern-6800` platform uses `nibble=high`.
- Same file, entries: "These properties convert the value decoded from memory. Apply `mask`, then `scale`, then `offset`" and
  **mask**: "A mask to apply to each byte before further processing."
- The reference parser (github.com/tomlogic/py-pinmame-nvmaps, `nvram_parser.py`, `RamMapping.get_bytes`): reverses the bytes of a
  little-endian field, then, when the nibble is not `both`, keeps the selected nibble of each byte and packs them two by two into bytes,
  **then** applies the mask to these bytes:

```python
nibble = self.nibble()
if nibble != Nibble.BOTH:
    # combine nibbles of ba
    ...
        if nibble == Nibble.LOW: b = b & 0x0F
        else: b = b >> 4
    ...
if 'mask' in self.entry:
    mask = to_int(self.entry['mask'])
    ba = bytearray(map((lambda x: x & mask), ba))
```

So `mask 0x4` at `0x20C` is bit 2 of the high nibble: `(0xEF >> 4) & 0x4` = 4 during a game, `(0xAF >> 4) & 0x4` = 0 in attract. The
maps are right.

## Cause in libpinmame

`SetMemMapImpl` builds a getter per field. It applies the mask to the raw byte, then takes the nibble:

```cpp
const uint8_t num = (*(ptr + offset - baseOffset)) & byteMask;
...
else if (nibble == 2)
   v = (v << 4) | (num >> 4);
```

History (upstream `src/libpinmame/libpinmame.cpp`):

| Commit | Date | Effect on a masked field in a high-nibble region |
|---|---|---|
| `5eacb072` LibPinMAME: Add memory maps game state support | 2026-07-27 | The platform's `"nibble": "high"` was parsed as `"hight"` and never registered: nibble `both`, mask on the raw byte. `0xAF & 0x4` = 4: `game_over` constant 0 ("in game") |
| `685b9387` fix "hight" typo dropping high-nibble platform memory regions | 2026-08-21 | The region now registers. Mask then nibble: `(0xAF & 0x4) >> 4` = 0: `game_over` constant 1 ("over"), Centaur recorded empty games |
| `ee4a4372` do not let a platform-region nibble reinterpret a masked field (PR #621, merged 2026-08-30) | 2026-08-21 | Workaround: a field with a mask ignores the nibble of its region. Back to the first state: constant 0 ("in game") |

The author of `ee4a4372` left the question open in its message: "Which of mask and nibble should apply first, and whether PinMAME's
live image preserves the physical nibble packing the platform files describe at all, are open questions that need bytes off a machine
to answer [...] This restores what demonstrably worked while that is settled". The bytes above settle both for the AS-2518 boards: the
live image keeps the packing (data in the high nibble, `0xF` below), and only nibble-then-mask reads the lamp. With the workaround, the
`game_over` of these maps does not "work": it is constantly "in game", which a consumer may tolerate when it ends games by other
means.

## What is affected

The bundled set of maps (pinmame-nvram-maps revision `7e63610`, as copied in VPX `src/assets/pinmame/memmaps`) has 86 fields with a
`mask` at an address covered by a platform region with a `nibble`, in 43 maps used by 169 ROMs. All are the `game_over` and `tilted`
lamps of high-nibble platforms (at `0x20C`, and `0x200` with masks `0x8` and `0x2` on Gold Ball and Grand Slam):

| Platform | Maps | ROMs |
|---|---:|---:|
| bally-17-3K | 1 | 1 |
| bally-17-4K | 3 | 17 |
| bally-17-8K | 1 | 7 |
| bally-35-6K | 4 | 21 |
| bally-35-8K | 32 | 98 |
| stern-m100 | 2 | 25 |

No masked field sits in a low-nibble region (Williams System 3-7, Gottlieb System 80/80B, Zaccaria, Atari Gen 2), and no field has
both its own `nibble` and a `mask`. Fields without a mask, and masked fields of 8-bit memories, are not affected. The full list is in
the appendix.

## The change

`pinmame-memmap-nibble-then-mask.patch` in the workspace documents (against upstream master `aed5459b`, also applies to `652249e2` and `5386aa17`, whose memory
map code is the same):

- the workaround of `ee4a4372` is removed: the nibble of the region applies to every field (a field's own `nibble` or `packed=false`
  still overrides it);
- the getter collects the bytes of the field in big-endian order, packs the selected nibbles two by two for a 4-bit memory (a lone
  first nibble when their count is odd), **then** applies the mask, then decodes BCD or integer from these bytes: the steps of
  `get_bytes` in the reference parser.

```cpp
// As the format's reference parser (py-pinmame-nvmaps nvram_parser.py, get_bytes) does: the bytes of the field
// in big endian order; for a 4-bit memory (nibble of the field, or of the platform region holding it), the
// selected nibble of each byte, packed two by two into bytes (a lone first nibble when their count is odd); then
// the mask, which applies to these packed bytes. [...]
uint8_t bytes[32];
size_t count = 0;
uint8_t packed = 0;
size_t remaining = std::min(offsets.size(), sizeof(bytes) * (nibble == 0 ? 1 : 2));
const auto push = [&](unsigned int offset)
{
   if (remaining == 0)
      return;
   const uint8_t b = *(ptr + offset - baseOffset);
   remaining--;
   if (nibble == 0)
      bytes[count++] = b & byteMask;
   else
   {
      packed = static_cast<uint8_t>((packed << 4) | (nibble == 1 ? (b & 0x0F) : (b >> 4)));
      if (remaining % 2 == 0)
      {
         bytes[count++] = packed & byteMask;
         packed = 0;
      }
   }
};
// ... push each offset (reversed for little endian), then:
for (size_t i = 0; i < count; i++)
   v = isBCD ? (v * 100) + digits(bytes[i]) : (v << 8) | bytes[i];
```

Without a mask the result is the same number as before for every encoding: packing two nibbles into a byte and shifting by 8 equals
shifting each nibble by 4, and a BCD byte holds the two digits the old code took one by one (invalid digits `0xA`-`0xF` still count as
0). The field length is capped at 32 bytes (64 nibbles), far above any numeric field (an `int64_t` holds 8 bytes).

## Evidence

1. **Bytes of the running machine** (above): AS-2518-17, low nibble `0xF` on all 256 CMOS bytes, lamp byte `0xAF` in attract and `0xEF`
   in a game.
2. **Differential test** (`difftest.py`): for every numeric field (`int`, `bcd`, `bool`, `enum`, `bits`, up to 8 bytes) of every map of
   the bundled set, 20 random memory contents each, decoded by a line-by-line port of libpinmame as it is (OLD), of the patched getter
   (NEW), and by the reference parser itself (REF, `RamMapping.get_value`):

   | Fields | Decodings | OLD = REF | NEW = REF | OLD = NEW |
   |---|---:|---:|---:|---:|
   | mask in a nibble region | 1,720 | 873 | **1,720** | 873 |
   | nibble, no mask | 43,640 | 43,640 | 43,640 | 43,640 |
   | mask, 8-bit memory | 3,300 | 3,300 | 3,300 | 3,300 |
   | neither | 411,480 | 411,082 | 411,082 | 411,480 |

   The patch changes no decoding outside the masked nibble fields, and makes those agree with the reference everywhere (OLD agrees about
   half the time on random bytes, by chance for booleans). The 398 remaining differences are another, unrelated point, identical before
   and after: a field with a single `offsets` entry and a `length` (Capcom `bbb108`), which libpinmame expands into `length` bytes and
   the reference parser reads as one byte.
3. **In VPX** (Steam Frame fork on macOS, `-play` with the coverage harness of `Doc/test-hooks`, the original upstream map with mask
   `0x4`, a 2-player unattended game of Evel Knievel): the same build twice, with only `libpinmame.dylib` changed:

   | libpinmame | `game_over` at boot (attract) | During the game | After the game | Result |
   |---|---|---|---|---|
   | as it is (`5386aa17`) | 0, "in game" | 0 | 0 | No game recorded: "scores seen but no finished game" after 5 minutes |
   | patched | 1, "over" | 0 from the Start press | 1, 0.25 s after the last points | Recorded 1.5 s later (the tracker waits for 6 readings): P1 21,260, P2 29,300, "end of game from the game over flag of the machine" |

   The player count stayed at 2 after the game in both runs, so no other signal could end it.

## Points for the reviewer (upstream)

- The order is the format's, not a choice: README ("mask: A mask to apply to each byte before further processing", "Apply mask, then
  scale, then offset") and `get_bytes` of the reference parser. The maps are written for it: `0x4` is the lamp bit inside the nibble.
- The workaround of `ee4a4372` is not needed any more, and it was not a fix: with it, these `game_over` fields read constant "in game".
  Centaur's `current_ball` and `player_count` (own nibbles at `0x0B`) are unchanged by the patch: they have no mask.
- Stern MPU-100 maps (2 maps, 25 ROMs) are covered by the same rule but were not run: PinMAME stores their CMOS bytes as written, so
  the high nibble is what the real 4-bit RAM keeps, and what the maps describe. A run on one of them would be welcome.
- `tilted` follows the same path (mask `0x8`): it was constant too.
- Writing through the map (#634) is not concerned: it writes raw memory.

## What the fork does meanwhile

Until upstream PinMAME has the fix, the fork changed the one map its library uses: `system-rom-20.map.json` reads `game_over` with mask
`0x40` (fork master, 2026-10-28, `b022676ca`; `Table Scores and Leaderboards.md`, section 5), which gives the right value with libpinmame as it is (the mask on the raw byte). It
covers the 8 ROMs of that map only; the 42 other maps stay constant "in game". **That change must be reverted when libpinmame is
fixed**: with nibble-then-mask, `0x40` selects a bit the nibble does not have and reads constant "over".

Two ways for the fork:

1. Wait for upstream, keep the map change (only Evel Knievel's family covered).
2. Carry the patch (`steamframe/patches/pinmame/`, applied by the dependency scripts of macOS and Linux like the libspatialaudio
   patch), revert the map change in the same commit, and drop the patch once upstream merges it. All 169 ROMs are covered.

## Draft of the PR

**Title:** libpinmame: apply the nibble of 4-bit memory before the mask of a memory map field

**Body:**

> Memory map fields with a `mask` in a 4-bit memory region (platform `"nibble": "high"`) are decoded with the mask applied to the raw
> byte. The format applies it after the nibble is taken (README: "mask: A mask to apply to each byte before further processing";
> reference parser `nvram_parser.py`, `RamMapping.get_bytes`: nibbles first, then the mask).
>
> On the Bally AS-2518-17/-35 boards PinMAME keeps the 5101 RAM in the high nibble and forces the low one to `0xF`
> (`by35_CMOS_w`). Read from the running emulation, Evel Knievel's lamp byte `0x20C` is `0xAF` in attract mode and `0xEF` during a game:
> the maps' `game_over` (`mask 0x4`, bit 2 of the nibble) is `(0xEF >> 4) & 4` in a game and `(0xAF >> 4) & 4` = 0 after. Today it
> reads `0xAF & 4` = 4: constant "in game". This is the question #621 (`ee4a4372`) left open; its workaround (a masked field ignores the
> nibble of its region) restored that constant. It affects the `game_over` and `tilted` fields of 43 maps, 169 ROMs (Bally AS-2518-17,
> AS-2518-35, Stern MPU-100).
>
> This change removes the workaround and decodes as the reference parser: bytes in big-endian order, nibbles of a 4-bit memory packed
> two by two, then the mask, then BCD or integer. Without a mask the decoded numbers are the same as before.
>
> Checked: a differential test of every numeric field of every map of pinmame-nvram-maps `7e63610` against the reference parser
> (460,140 random decodings: the 1,720 of masked nibble fields now all agree, every other decoding is unchanged), and Evel Knievel
> played in VPX with the unmodified map: the game ends on `game_over`. Not run on a Stern MPU-100 game.

## Appendix: affected maps and ROMs

The fields are `game_over` (mask `0x4`) and `tilted` (mask `0x8`) at `0x20C`, except where noted. `system-rom-20` is listed with the upstream mask (`0x4`), not the fork's `0x40`. Map paths relative to `maps/`.

| Platform | Map | ROMs | Fields, when not the usual two |
|---|---|---|---|
| bally-17-3K | `bally/as-2518-17/freedom.map.json` | freedom |  |
| bally-17-4K | `bally/as-2518-17/freedom-freeplay.map.json` | freedoma |  |
| bally-17-4K | `bally/as-2518-17/system-rom-20-freeplay2.map.json` | blackjcd, eightblc, evelknic, freedomb, mataharc, nightrdc, pwerplac, stk_sprc |  |
| bally-17-4K | `bally/as-2518-17/system-rom-20.map.json` | blackjck, eightbll, evelknie, matahari, nightr20, nightrdr, pwerplay, stk_sprs |  |
| bally-17-8K | `bally/as-2518-17/system-rom-20-freeplay1.map.json` | blackjcb, eightblb, evelknib, mataharb, nightrdb, pwerplab, stk_sprb |  |
| bally-35-6K | `bally/as-2518-35/lostwrld.map.json` | lostwrld |  |
| bally-35-6K | `bally/as-2518-35/smman.map.json` | smman |  |
| bally-35-6K | `bally/as-2518-35/system-rom-30.map.json` | kiss, paragon, playboy, sexygirl, sst, startrek, voltan |  |
| bally-35-6K | `bally/as-2518-35/system-rom-35.map.json` | dollyptn, futurspa, hglbtrtr, hotdoggn, mystic, ngndshkr, rollston, slbmania, slbmanic, slbmanid, spaceinv, viking |  |
| bally-35-8K | `bally/as-2518-35/blakpyra-freeplay.map.json` | blakpyrb |  |
| bally-35-8K | `bally/as-2518-35/blakpyra.map.json` | blakpyra |  |
| bally-35-8K | `bally/as-2518-35/centaur-freeplay.map.json` | centaura, centaurb |  |
| bally-35-8K | `bally/as-2518-35/centaur.map.json` | centaur |  |
| bally-35-8K | `bally/as-2518-35/eballdlx-freeplay.map.json` | eballdla |  |
| bally-35-8K | `bally/as-2518-35/eballdlx.map.json` | eballd14, eballdld, eballdlx |  |
| bally-35-8K | `bally/as-2518-35/embryon-freeplay.map.json` | embryona |  |
| bally-35-8K | `bally/as-2518-35/embryon.map.json` | embryon |  |
| bally-35-8K | `bally/as-2518-35/frontier-freeplay.map.json` | frontiea |  |
| bally-35-8K | `bally/as-2518-35/frontier.map.json` | frontieg, frontier |  |
| bally-35-8K | `bally/as-2518-35/goldball.map.json` | goldball, goldbaln | game_over 0x8 @0x200, tilted 0x2 @0x200 |
| bally-35-8K | `bally/as-2518-35/gransla4.map.json` | gransla4 | game_over 0x8 @0x200, tilted 0x2 @0x200 |
| bally-35-8K | `bally/as-2518-35/hotdoggn-freeplay.map.json` | hotdogga |  |
| bally-35-8K | `bally/as-2518-35/kiss-freeplay.map.json` | kissc, kissd, kissf |  |
| bally-35-8K | `bally/as-2518-35/lostwrld-freeplay.map.json` | lostwldb |  |
| bally-35-8K | `bally/as-2518-35/newwave.map.json` | newwave |  |
| bally-35-8K | `bally/as-2518-35/skatebll-freeplay.map.json` | skatebla, skateblb |  |
| bally-35-8K | `bally/as-2518-35/skatebll.map.json` | skatebll |  |
| bally-35-8K | `bally/as-2518-35/smman-freeplay.map.json` | smmanc, smmand |  |
| bally-35-8K | `bally/as-2518-35/speakes4.map.json` | speakes4 |  |
| bally-35-8K | `bally/as-2518-35/speakesy.map.json` | speakesy |  |
| bally-35-8K | `bally/as-2518-35/spectrum.map.json` | spectru4 |  |
| bally-35-8K | `bally/as-2518-35/suprbowl.map.json` | suprbowl |  |
| bally-35-8K | `bally/as-2518-35/system-rom-30-7digit.map.json` | kissb, kisse, paragonb, playboyb, sstb, startreb, voltanb |  |
| bally-35-8K | `bally/as-2518-35/system-rom-30-freeplay.map.json` | paragonc, paragond, playboyc, playboyd, sstc, sstd, startrec, startred, voltanc, voltand |  |
| bally-35-8K | `bally/as-2518-35/system-rom-35-7digit.map.json` | dollyptb, futurspb, hglbtrtb, hotdoggb, mysticb, ngndshkb, rollstob, slbmanib, spaceinb, vikingb |  |
| bally-35-8K | `bally/as-2518-35/system-rom-52-freeplay.map.json` | fball_ia, flashgda, flashgfa, flashgva, flashgvffp |  |
| bally-35-8K | `bally/as-2518-35/system-rom-52.map.json` | embryond, fball_ii, flashgdf, flashgdn, flashgdv, flashgvf |  |
| bally-35-8K | `bally/as-2518-35/system-rom-53-freeplay.map.json` | bmxa, cybrnaua, elektraa, fathoma, fbclassa, kosteela, m_mpaca, medusaa, spyhunta, vectora, xsandosa |  |
| bally-35-8K | `bally/as-2518-35/system-rom-53.map.json` | bmx, cybrnaut, elektra, fathom, fbclass, kosteel, m_mpac, m_mpacb, medusa, medusaf, spyhuntr, vector, xsandos |  |
| bally-35-8K | `bally/as-2518-35/xenon-freeplay.map.json` | xenona, xenonfa |  |
| bally-35-8K | `bally/as-2518-35/xenon.map.json` | xenon, xenonf |  |
| stern-m100 | `stern/m100/freeplay.map.json` | draculfp, hothanfp, lectrofp, magicfp, memlanfp, nugentfp, pinbalfp, princefp, starsfp, stingrfp, tridenfp, wildfyfp |  |
| stern-m100 | `stern/m100/generic.map.json` | dracula, hothand, lectrono, magic, memlane, nugent, pinball, princess, stars, stingray, trident, tridenta, wildfyre |  |

## Tools

`test-hooks/memmap-nibble-mask/` of the workspace documents:

- `pinmame-memmap-nibble-then-mask.patch`: the change, against upstream master.
- `rawbytes.cpp`: runs a ROM with libpinmame and prints a range of CPU memory as PinMAME holds it (`PinmameReadMainCPUMemory`).
  `rawbytes <pinmame dir/> evelknie 8000 200 256`.
- `difftest.py`: the differential test (needs `nvram_parser.py` of py-pinmame-nvmaps: `NVRAM_PARSER_DIR=<its folder>`).
- In VPX: `test-hooks/score-coverage.py` (same repository, section 12 of `Table Scores and Leaderboards.md`) with `VPX_COVERAGE_APP=<app with the patched libpinmame.dylib>`, and the original map.
