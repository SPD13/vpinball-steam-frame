#!/bin/bash
# Compares the games libpinmame runs with the names the table library knows (RomAliases::GameCatalog): the driver catalog of PinMAME, which
# leaves out the unofficial mods, and the list GAMES_NOT_IN_CATALOG of src/steamframe/RomAliases.cpp. A table asking for a game missing
# from both is reported as unknown, or given a wrong alias, although it runs.
#
# Reads the driver symbols of the macOS library, which keeps them (the Linux one may be stripped). Run it after platforms/macos-arm64/external.sh.
#
# Usage: steamframe/tools/check-pinmame-catalog.sh [<libpinmame library>]

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

LIBRARY="${1:-third-party/runtime-libs/macos-arm64/libpinmame.dylib}"

nm "$LIBRARY" | sed -n 's/.* _driver_//p' | python3 -I -c '
import json, re, sys

drivers = {name.strip().lower() for name in sys.stdin} - {"0"} # driver_0 ends the list of drivers
if not drivers:
    sys.exit("No driver symbol in the library")

catalog = json.load(open("third-party/gamedefs/catalog/pinmame.json"))
known = {driver["id"].lower() for driver in catalog["drivers"]}
systems = {parent["id"].lower() for parent in catalog["abstract_parents"]} # Boards, not games: no table asks for them

source = open("src/steamframe/RomAliases.cpp").read()
listed = set(re.findall(r"\"(\w+)\"", re.search(r"GAMES_NOT_IN_CATALOG\[\] = \{(.*)\}", source).group(1)))

missing = sorted(drivers - known - systems - listed)
obsolete = sorted((listed - drivers) | (listed & known))
print(f"{len(drivers)} drivers in the library, {len(known)} in the catalog, {len(listed)} in GAMES_NOT_IN_CATALOG")
if missing:
    print("To add to GAMES_NOT_IN_CATALOG:", " ".join(missing))
if obsolete:
    print("To remove from GAMES_NOT_IN_CATALOG:", " ".join(obsolete))
sys.exit(1 if missing or obsolete else 0)
'
