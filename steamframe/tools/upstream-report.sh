#!/bin/bash
# Lists the upstream commits to review for the Steam Frame: the ones that touch the VR, renderer, in-game UI, player, settings, web
# server, PinMAME and build areas, or the upstream files the fork changes, or that talk about VR in their subject. They may merge
# without any conflict and still change what the fork relies on (for example, upstream making ResFactor relative to the recommended
# resolution): test them on the Frame.
#
# sync-upstream.sh runs it at the end of a sync, for the commits the sync brought in. Run alone, it previews the upstream commits
# that are not in the branch yet.
#
# Usage: steamframe/tools/upstream-report.sh [--from <ref>] [--upstream <ref>]
#        --from: first commit excluded (default: where the branch leaves upstream), --upstream: last commit included (default: origin/master)

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

UPSTREAM=origin/master
FROM=""
while [ $# -gt 0 ]; do
   case "$1" in
      --from) FROM="$2"; shift 2 ;;
      --upstream) UPSTREAM="$2"; shift 2 ;;
      *) echo "Unknown option: $1" >&2; exit 1 ;;
   esac
done
BASE="$(git merge-base HEAD "$UPSTREAM")"
if [ -z "$FROM" ]; then
   FROM="$BASE"
fi

# Areas to watch: label, then the paths (git pathspecs)
AREAS=(
   "vr src/renderer/VRDevice.* src/renderer/XR* src/input/XR* src/ui/live/ingameui/VRSettingsPage.* src/shaders/bgfx/vs_vr_mask.sc apps/android/app/src/quest"
   "renderer src/renderer src/shaders/bgfx"
   "ui src/ui/live"
   "player src/core/player.* src/core/AppCommands.* src/core/VPApp.* src/input"
   "settings src/core/Settings*"
   "web lib/src/WebServer* src/assets/web"
   "pinmame plugins/pinmame"
   "build CMakeLists.txt make platforms/linux-aarch64 platforms/linux-x64 platforms/macos-arm64 platforms/macos-x64 platforms/windows-mingw-x64"
)

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Upstream files the fork changes (not the generated shader headers, which are regenerated at each sync)
git diff --name-only --diff-filter=M "$BASE" HEAD | grep -Ev '^src/shaders/bgfx_[a-z_]+\.h$' > "$WORK/ours" || true

for area in "${AREAS[@]}"; do
   label="${area%% *}"
   read -r -a paths <<< "${area#* }"
   git log --format="%H $label" "$FROM..$UPSTREAM" -- "${paths[@]}" >> "$WORK/tags"
done
if [ -s "$WORK/ours" ]; then
   tr '\n' '\0' < "$WORK/ours" | xargs -0 git log --format="%H ours" "$FROM..$UPSTREAM" -- >> "$WORK/tags"
fi
git log --format="%H %s" "$FROM..$UPSTREAM" | awk '{ s = tolower($0); sub(/^[^ ]+ /, "", s); if (s ~ /(^|[^a-z])(vr|xr|openxr|quest|stereo|headset|foveat)/) print $1 " subject" }' >> "$WORK/tags"

TOTAL="$(git rev-list --count "$FROM..$UPSTREAM")"
COUNT=0
# Lists the tagged commits, oldest first; $1 = vr for the ones tagged vr or subject, other for the rest
list_commits() {
   local before=$COUNT
   for commit in $(git rev-list --reverse "$FROM..$UPSTREAM"); do
      tags="$(grep "^$commit " "$WORK/tags" | cut -d' ' -f2 | tr '\n' ' ' | sed 's/ $//' || true)"
      [ -n "$tags" ] || continue
      if echo " $tags " | grep -Eq ' (vr|subject) '; then [ "$1" = vr ] || continue; else [ "$1" = other ] || continue; fi
      COUNT=$((COUNT + 1))
      echo "   $(git log -1 --format='%h %cs %s' "$commit" | cut -c1-140)"
      files="$(git diff-tree --no-commit-id --name-only -r "$commit" | grep -Fx -f "$WORK/ours" | head -4 | tr '\n' ' ' | sed 's/ $//' || true)"
      if [ -n "$files" ]; then
         echo "      [$tags] files of the fork: $files"
      else
         echo "      [$tags]"
      fi
   done
   if [ "$COUNT" = "$before" ]; then
      echo "   none"
   fi
}
echo "Upstream commits to review for the Steam Frame, $(git rev-parse --short "$FROM")..$(git rev-parse --short "$UPSTREAM")"
echo "VR and XR (test these on the Frame first):"
list_commits vr
echo "Other areas the fork relies on:"
list_commits other
echo "$COUNT of $TOTAL upstream commits. [ours]: changes an upstream file the fork changes too; [subject]: mentions VR/XR/stereo."
