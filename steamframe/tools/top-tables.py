#!/usr/bin/env python3
"""Builds docs/steam-frame/Top Tables.md from steamframe/tools/top-tables.json.

For each machine of the ranking, picks a table in the Virtual Pinball Spreadsheet (VPS) database:
VR tagged VPX tables only, then the nFozzy tag, then JPSalas in the comment or authors, then the latest
update. Tables of the machine itself come first; tables of `fan` are used only when the machine has none.
The Frame's list comes from its tables.json (over ssh, or --frame-tables FILE). The Pinside ranking is
behind Cloudflare and is refreshed by hand in top-tables.json (see the end of the generated file).

  steamframe/tools/top-tables.py [--vpsdb FILE] [--frame-tables FILE] [--review]
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "steamframe", "tools", "top-tables.json")
OUT = os.path.join(ROOT, "docs", "steam-frame", "Top Tables.md")
VPSDB_URL = "https://virtualpinballspreadsheet.github.io/vps-db/db/vpsdb.json"
VPS_GAME = "https://virtualpinballspreadsheet.github.io/games?game="
PINSIDE = "https://pinside.com/pinball/machine/"
FRAME = "steamos@frame.local"
FRAME_TABLES = ".local/share/VPinballX/10.8/tables.json"

# Wheels that are not an image a table's thumbnail can use, or that belong to another program.
WHEEL_NEVER = re.compile(r"animated|apng|zen studios|pinball fx|zaccaria", re.I)
WHEEL_BUNDLE = re.compile(r"pack|mega\.nz/#F!", re.I)

INTRO = """# Top Tables for the Steam Frame

Pinside Top 100 of {ranking_date}, table data from the [Virtual Pinball Spreadsheet](https://virtualpinballspreadsheet.github.io/games) and the Frame's library read on {today}.

{machine} machines have a VR table of the machine itself and {fan} more only have a fan table on the same theme. The other {none} have no VR table that can be downloaded, mostly recent Stern, Jersey Jack and Spooky machines. {frame} of the 100 are on the Frame.

**VR version.** "Yes" means a VR table of the machine can be downloaded, "Fan table" that only a fan table on the same theme can, "No" that neither can. These come from the VR tag on VPS: a table without the tag can still have a VR room, and "Not accessible" means the only VR table refuses access (see the end).

**Which table is listed.** Among the VPX tables tagged VR, the one tagged nFozzy, otherwise one with JPSalas in its comment or authors, otherwise the latest; with several candidates at one step, the latest of them. The rule that decided is shown in parentheses after the link ("only VR" when there was one candidate). "Latest" uses the date VPS shows as "Updated at". Patches that need another table (VPU Patch), files marked unavailable and dead links are left out, as are two files whose page refuses access (see the end).

**Fan tables.** A fan table, named in the Download column, shares the theme but is not the machine Pinside rates: it is usually a remix or a reskin of another playfield. It is listed only when VPS has no VR table of the machine. A machine of the same name by another maker is not used, nor counted as installed (see the end).

**PuP pack.** A pack VPS lists for that game, the latest when there are several, or "In the table's download" when the table is tagged as including it. Add-ons, fixes, translations and packs made for another version of the table were left out by hand. None was tested with the listed table on the Frame: a pack made for another version can still need changes in the script.

**Wheel art.** A still image, usable with **Upload thumbnail** in the Tables page: plain wheels first, then styled ones, then bundles of many wheels. Animated wheels and those for Zen Studios or Zaccaria are left out. The wheel can be of a similar table when the listed one has none.

**On the Frame.** "Yes" means the machine or the listed fan table is installed. The note says which version, which can differ from the one listed here.
"""

OUTRO = """## Same name, different machine

These are on the Frame and do not count for the machine of the same name above: Godzilla (Sega 1998), James Bond 007 (Gottlieb 1980), Pirates of the Caribbean (Stern 2006), Eight Ball (Bally 1977), Star Wars Trilogy (Sega 1997) and Star Wars (Sonic 1987). VPS also has VR tables of these other machines, not listed above: Jurassic Park (Data East 1993), King Kong (Data East 1990), Guns N' Roses (Data East 1994), Dungeons & Dragons (Bally 1987), Transformers (Stern 2011), The Avengers (Stern 2012), James Cameron's Avatar (Stern 2010) and Star Wars (Data East 1992).

## Files left out

vpuniverse.com answered "you do not have permission" on {refused_date}, to a signed-in account, for two files the rule would have chosen: the Attack from Mars VR room 4.0 by Solters and the Elton John fan table 1.0.0 by Tonyfox, the only VR table of Elton John. All the other vpuniverse.com links opened. The links to mega.nz, vpforums.org and orbitalpin.com were not checked.

## Updating the list

`steamframe/tools/top-tables.py` rebuilds this file from the VPS database and the Frame's library (wake the Frame first, or give `--frame-tables`); `--review` prints each choice and the packs to check. A new VPS table is picked up by itself.

The ranking and its links to VPS are kept by hand in `steamframe/tools/top-tables.json`, because Pinside blocks scripts: read the list in a browser, then for each new machine add a row with its VPS game ids, `machine` for the machine itself and `fan` for tables on the same theme. Add further rows the same way to go past 100. `frame` is a part of the table's path on the Frame, `frameNote` the note to show, `tableExclude` the tables to leave out, `pupExclude` the packs to leave out, `pup` a pack to use instead of the latest.
"""


def first_url(item):
    """The first working link of a VPS file, or None."""
    for u in item.get("urls") or []:
        if u.get("url") and not u.get("broken"):
            return u["url"]
    return None


def released(item):
    # The date VPS shows as "Updated at" is createdAt; updatedAt moves with any edit of the database entry.
    return (item.get("createdAt") or 0, item.get("updatedAt") or 0)


def vr_tables(game, exclude=()):
    """VR tagged VPX tables that can be downloaded and played as they are."""
    out = []
    for t in game.get("tableFiles") or []:
        features = t.get("features") or []
        if t.get("tableFormat") != "VPX" or "VR" not in features or t.get("id") in exclude:
            continue
        if "VPU Patch" in features or "unavailable" in (t.get("comment") or "").lower() or not first_url(t):
            continue
        out.append(t)
    return out


def mentions_jp(t):
    text = " ".join([t.get("comment") or ""] + (t.get("authors") or []))
    return re.search(r"jp\s*salas", text, re.I) is not None


def pick_table(tables):
    """Returns (table, the rule that chose it)."""
    if not tables:
        return None, None
    if len(tables) == 1:
        return tables[0], "only VR"
    nfozzy = [t for t in tables if "nfozzy" in [f.lower() for f in t.get("features") or []]]
    if nfozzy:
        return max(nfozzy, key=released), "nFozzy"
    jp = [t for t in tables if mentions_jp(t)]
    if jp:
        return max(jp, key=released), "JPSalas"
    return max(tables, key=released), "latest"


def wheel_rank(wheel):
    """Plain wheels first, then styled ones (Diagonal, Logo...), then bundles of many wheels; then the latest."""
    comment = wheel.get("comment") or ""
    if WHEEL_BUNDLE.search(comment + " " + first_url(wheel)):
        tier = 0
    else:
        tier = 1 if comment else 2
    return (tier, released(wheel))


def pick_wheel(games):
    """A still wheel of the first game that has one."""
    for game in games:
        wheels = [w for w in game.get("wheelArtFiles") or []
                  if first_url(w) and not WHEEL_NEVER.search((w.get("comment") or "") + " " + first_url(w))]
        if wheels:
            return max(wheels, key=wheel_rank)
    return None


def pick_pup(game, table, row):
    """(pack, included): a table with the pack in its download needs no other link, unless the row names one."""
    packs = [p for p in game.get("pupPackFiles") or [] if first_url(p) and p.get("id") not in row.get("pupExclude", [])]
    named = [p for p in packs if p.get("id") == row.get("pup")]
    if named:
        return named[0], False
    if "incl. PuP" in (table.get("features") or []):
        return None, True
    return (max(packs, key=released) if packs else None), False


def cell(text):
    return text.replace("|", "\\|").strip()


def link(text, url):
    # A link target with spaces or parentheses would end the markdown link early.
    safe = urllib.parse.quote(url, safe=":/?&=%#+,;@!$'*~-_.")
    return f"[{cell(text)}]({safe})"


def file_label(item):
    authors = item.get("authors") or []
    name = authors[0] if authors else "file"
    version = item.get("version") or ""
    return f"{name} {version}".strip()


def load_vpsdb(path):
    if path:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    with urllib.request.urlopen(VPSDB_URL, timeout=120) as r:
        return json.load(r)


def load_frame(path):
    if path:
        with open(path, encoding="utf-8") as f:
            return json.load(f)["tables"]
    out = subprocess.run(["ssh", "-o", "ConnectTimeout=8", FRAME, f"cat {FRAME_TABLES}"],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"Cannot read the Frame's tables ({out.stderr.strip()}). Wake it, or use --frame-tables.")
    return json.loads(out.stdout)["tables"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vpsdb")
    parser.add_argument("--frame-tables")
    parser.add_argument("--review", action="store_true", help="print the choices and the PuP packs to check")
    args = parser.parse_args()

    with open(DATA, encoding="utf-8") as f:
        data = json.load(f)
    games = {g["id"]: g for g in load_vpsdb(args.vpsdb)}
    frame = load_frame(args.frame_tables)

    lines = []
    counts = {"machine": 0, "fan": 0, "none": 0, "frame": 0}
    for row in data["ranking"]:
        machine = [games[i] for i in row.get("machine", [])]
        fan = [games[i] for i in row.get("fan", [])]
        exclude = row.get("tableExclude", [])
        table, rule = pick_table([t for g in machine for t in vr_tables(g, exclude)])
        kind = "machine"
        if not table:
            table, rule = pick_table([t for g in fan for t in vr_tables(g, exclude)])
            kind = "fan" if table else "none"
        counts[kind] += 1
        vr = {"machine": "Yes", "fan": "Fan table"}.get(kind, "Not accessible" if exclude else "No")

        vps = machine or fan
        if table:
            game = games[table["game"]["id"]]
            prefix = f"Fan table {cell(game['name'].strip())}: " if kind == "fan" else ""
            label = f"{prefix}{link(file_label(table), first_url(table))} ({rule})"
            download = f"{label} · {link('VPS', VPS_GAME + game['id'])}"
            pack, included = pick_pup(game, table, row)
            if included:
                pup = "In the table's download"
            elif pack:
                pup = link(file_label(pack), first_url(pack))
            else:
                pup = "—"
        else:
            game = None
            pack, included = None, False
            others = sum(1 for g in vps for t in g.get("tableFiles") or [] if t.get("tableFormat") == "VPX")
            if exclude:
                download = f"VR table not accessible, see the end ({link('VPS', VPS_GAME + vps[0]['id'])})"
            elif others:
                download = f"No VR table ({link(f'{others} on VPS without VR', VPS_GAME + vps[0]['id'])})"
            elif vps:
                download = f"No VPX table ({link('VPS', VPS_GAME + vps[0]['id'])})"
            else:
                download = "Not on VPS"
            pup = "—"

        wheel_item = pick_wheel(([game] if game else []) + machine + fan)
        wheel = link(file_label(wheel_item), first_url(wheel_item)) if wheel_item else "—"

        on_frame = "No"
        if row.get("frame"):
            found = [t for t in frame if row["frame"] in t["path"]]
            if found:
                counts["frame"] += 1
                on_frame = "Yes"
                if row.get("frameNote"):
                    on_frame += f": {row['frameNote']}"
            else:
                print(f"#{row['rank']}: {row['frame']!r} is no longer on the Frame", file=sys.stderr)

        name = f"{link(row['name'], PINSIDE + row['pinside'])} ({row['maker']})"
        lines.append(f"| {row['rank']} | {name} | {vr} | {download} | {pup} | {wheel} | {on_frame} |")

        if args.review:
            print(f"{row['rank']:>3} {row['name']} [{kind}/{rule}]",
                  table and (table.get("version"), table.get("comment"), table.get("features"), (table.get("authors") or [])[:2]),
                  "\n     wheel:", wheel_item and (wheel_item.get("comment"), first_url(wheel_item)), "| frame:", on_frame)
            if game:
                for p in game.get("pupPackFiles") or []:
                    mark = "*" if pack and p["id"] == pack["id"] else " "
                    print(f"     pup{mark} {p['id']} {p.get('version')} {p.get('comment')!r} {(p.get('authors') or [])[:2]} {first_url(p)}")

    text = INTRO.format(ranking_date=data["rankingDate"], today=datetime.date.today().isoformat(), **counts)
    header = "| Pinside rank | Game | VR version | Download (VR table) | PuP pack | Wheel art | On the Frame |\n| --- | --- | --- | --- | --- | --- | --- |\n"
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text + "\n" + header + "\n".join(lines) + "\n\n" + OUTRO.replace("{refused_date}", "2026-11-01"))
    print(f"Wrote {OUT}: {counts}")


if __name__ == "__main__":
    main()
