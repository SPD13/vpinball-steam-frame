#!/bin/bash
# Brings the Steam Frame branch up to date with upstream (vpinball/vpinball master) by rebasing our commits on it.
#
# - The generated shader headers (src/shaders/bgfx_*.h) are never merged: their conflicts are resolved with upstream's version,
#   and they are regenerated from our shader sources once the rebase is done (regen-shaders.sh).
# - Other conflicts stop the script: resolve them, 'git add' the files, then run it again with --continue. rerere is turned on,
#   so a conflict resolved once is resolved the same way at the next sync.
# - Commits that upstream made redundant (for example a fix that was merged upstream) become empty and are dropped.
# - The branch as it was before is kept as a local tag (pre-sync-<date>), to compare with 'git range-diff' or to go back to.
# - During the rebase, the working tree holds older commits, which may not have this script: it copies itself to the git folder
#   (.git/steamframe-sync), and --continue and --abort are run from there (the script prints the command).
#
# Usage: steamframe/tools/sync-upstream.sh [--upstream <ref>] [--no-fetch]
#        .git/steamframe-sync/sync-upstream.sh --continue | --abort
# Remotes: 'origin' is upstream (github.com/vpinball/vpinball), 'fork' is ours (github.com/SPD13/vpinball-steam-frame).

set -euo pipefail

REPO="$(git rev-parse --show-toplevel)"
cd "$REPO"
STATE="$(cd "$(git rev-parse --git-path .)" && pwd)/steamframe-sync"

UPSTREAM=origin/master
MODE=start
FETCH=true
while [ $# -gt 0 ]; do
   case "$1" in
      --continue) MODE=continue; shift ;;
      --abort) MODE=abort; shift ;;
      --upstream) UPSTREAM="$2"; shift 2 ;;
      --no-fetch) FETCH=false; shift ;;
      *) echo "Unknown option: $1" >&2; exit 1 ;;
   esac
done

GENERATED='^src/shaders/bgfx_[a-z_]+\.h$'

rebase_in_progress() { [ -d "$(git rev-parse --git-path rebase-merge)" ] || [ -d "$(git rev-parse --git-path rebase-apply)" ]; }

if [ "$MODE" = abort ]; then
   git rebase --abort
   rm -rf "$STATE"
   exit 0
fi

if [ "$MODE" = continue ]; then
   UPSTREAM="$(cat "$STATE/upstream")"
fi

if [ "$MODE" = start ]; then
   if rebase_in_progress; then
      echo "A rebase is in progress: run with --continue or --abort" >&2
      exit 1
   fi
   if ! git diff --quiet || ! git diff --cached --quiet; then
      echo "The working tree has uncommitted changes" >&2
      exit 1
   fi
   git config rerere.enabled true
   git config rerere.autoUpdate true
   if [ "$FETCH" = true ]; then
      git fetch origin
   fi
   BRANCH="$(git branch --show-current)"
   TAG="pre-sync-$(date +%Y%m%d-%H%M)"
   git tag "$TAG" HEAD
   rm -rf "$STATE"
   mkdir -p "$STATE"
   cp "$REPO"/steamframe/tools/*.sh "$STATE/"
   echo "$UPSTREAM" > "$STATE/upstream"
   echo "$BRANCH: $(git rev-list --count "$UPSTREAM..HEAD") commits of ours, $(git rev-list --count "HEAD..$UPSTREAM") new upstream commits. Previous state: tag $TAG"
   git rebase "$UPSTREAM" || true
fi

while rebase_in_progress; do
   # Generated headers: upstream's version (during a rebase, 'ours' is the upstream side)
   for path in $(git diff --name-only --diff-filter=U | grep -E "$GENERATED" || true); do
      git checkout --ours -- "$path"
      git add -- "$path"
   done

   conflicts="$(git diff --name-only --diff-filter=U)"
   if [ -n "$conflicts" ]; then
      echo
      echo "Conflicts to resolve in $(git log -1 --format='%h %s' REBASE_HEAD 2>/dev/null || echo 'the current commit'):"
      echo "$conflicts" | sed 's/^/   /'
      echo "Resolve them, 'git add' the files, then run: $STATE/sync-upstream.sh --continue (or --abort)"
      exit 2
   fi

   if git diff --cached --quiet; then
      git rebase --skip || true # Nothing left of this commit
   else
      GIT_EDITOR=true git rebase --continue || true
   fi
done

echo
echo "Rebased on $(git log -1 --format='%h %cs %s' "$UPSTREAM")"
rm -rf "$STATE"

"$REPO/steamframe/tools/regen-shaders.sh" --ref "$UPSTREAM"
if ! git diff --quiet -- src/shaders; then
   git add src/shaders/bgfx_*.h
   git commit -q -m "Steam Frame: regenerate the bgfx shader headers from our shader sources (DirectX blobs from upstream)"
   echo "Shader headers regenerated and committed"
fi

PREVIOUS="$(git tag --list 'pre-sync-*' --sort=-creatordate | head -1)"
echo
echo "$(git rev-list --count "$UPSTREAM..HEAD") commits of ours on top of upstream. Next:"
echo "   - review what changed in our commits: git range-diff $(git merge-base "$PREVIOUS" "$UPSTREAM" | cut -c1-12)..$PREVIOUS $UPSTREAM..HEAD"
echo "   - build: macOS (cmake --build build), Steam Frame (Doc/steam-frame-build/build-linux-arm64.sh app), and test on the Frame"
echo "   - publish: git push --force-with-lease fork HEAD, and tag the release (frame-<date>)"
