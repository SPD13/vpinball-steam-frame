#!/bin/bash
# Regenerates the embedded bgfx shader headers (src/shaders/bgfx_*.h) from the shader sources, with upstream's own
# src/shaders/bgfx/shaders.sh, on macOS or Linux.
#
# The DirectX blobs (dxbc, dxil) can only be compiled on Windows: they are copied from the headers of a reference commit
# (upstream's, by default) instead, by a shaderc wrapper (shaderc-dx-from-ref.sh). The Metal, OpenGL, OpenGL ES and Vulkan
# blobs, the ones of the macOS and Steam Frame builds, are compiled from our sources. After a change to a shader source,
# the Direct3D builds keep the reference shaders until the headers are regenerated on Windows (src/shaders/bgfx/shaders.ps1).
#
# The generated headers are never merged: after a sync with upstream they are regenerated (see sync-upstream.sh).
#
# Only the headers built from the shader sources that the fork changes are regenerated, the other ones stay as upstream committed them
# (upstream does not always regenerate all of them: on 2026-10-18 its bgfx_mipmap.h lacked a variant that its shaders.sh builds).
# HEADERS lists them by the name of their switch in shaders.sh (gen_<name>, for bgfx_<name>.h): update it when the fork changes other shaders.
HEADERS="basic ball imgui" # fs_basic.sc, fs_ball.sc, material.sh, vs_imgui.sc
#
# Usage: steamframe/tools/regen-shaders.sh [--ref <commit>] [--shaderc <path>] [--headers "<names>"]
#   --ref      commit whose headers provide the DirectX blobs (default: origin/master, i.e. upstream)
#   --shaderc  shaderc binary to use (default: built once per bgfx version from the bgfx of external/, cached in ~/.cache/vpx-steamframe)
#   --headers  the headers to regenerate, instead of HEADERS

set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
REF=origin/master
SHADERC=""
while [ $# -gt 0 ]; do
   case "$1" in
      --ref) REF="$2"; shift 2 ;;
      --shaderc) SHADERC="$2"; shift 2 ;;
      --headers) HEADERS="$2"; shift 2 ;;
      *) echo "Unknown option: $1" >&2; exit 1 ;;
   esac
done

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/vpx-steamframe"

# shaderc is not shipped: build it once per bgfx version (BGFX_CMAKE_VERSION of platforms/config.sh), from the bgfx sources that
# external.sh downloaded for this platform
if [ -z "$SHADERC" ]; then
   BGFX_VERSION="$(grep -m1 '^BGFX_CMAKE_VERSION=' "$REPO/platforms/config.sh" | cut -d= -f2)"
   TOOLS="$CACHE/bgfx-tools-$BGFX_VERSION"
   SHADERC="$TOOLS/cmake/bgfx/shaderc"
   if [ ! -x "$SHADERC" ]; then
      BGFX_CMAKE=""
      for dir in "$REPO"/external/*/Release/bgfx; do
         if [ -d "$dir/bgfx.cmake" ] && grep -q "^$BGFX_VERSION-" "$dir/cache.txt" 2>/dev/null; then
            BGFX_CMAKE="$dir/bgfx.cmake"
            break
         fi
      done
      if [ -z "$BGFX_CMAKE" ]; then
         echo "No bgfx $BGFX_VERSION sources in external/: run the external.sh of this platform first, or pass --shaderc" >&2
         exit 1
      fi
      # On Apple silicon, the Homebrew cmake of /opt/homebrew is the arm64 one
      CMAKE="$(command -v /opt/homebrew/bin/cmake || command -v cmake)"
      echo "Building shaderc from $BGFX_CMAKE (once per bgfx version, about 10 minutes)"
      mkdir -p "$CACHE"
      "$CMAKE" -S "$BGFX_CMAKE" -B "$TOOLS" -DCMAKE_BUILD_TYPE=Release -DBGFX_BUILD_TOOLS=ON -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_INSTALL=OFF -DBGFX_CUSTOM_TARGETS=OFF > "$TOOLS.log"
      "$CMAKE" --build "$TOOLS" --target shaderc -j8 >> "$TOOLS.log"
   fi
fi

# Work on a copy of the shader folder, as shaders.sh runs ./shaderc and writes the headers to ..
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/ref" "$WORK/shaders"
cp -R "$REPO/src/shaders/bgfx" "$WORK/shaders/bgfx"
rm -f "$WORK/shaders/bgfx/shaderc" "$WORK/shaders/bgfx/shaderc.exe"
# Only the switches of HEADERS on
sed -i.bak -E 's/^(gen_[a-z_]+)=true/\1=false/' "$WORK/shaders/bgfx/shaders.sh"
for name in $HEADERS; do
   grep -q "^gen_${name}=false" "$WORK/shaders/bgfx/shaders.sh" || { echo "No switch gen_${name} in shaders.sh" >&2; exit 1; }
   sed -i.bak -E "s/^gen_${name}=false/gen_${name}=true/" "$WORK/shaders/bgfx/shaders.sh"
done
for header in "$REPO"/src/shaders/bgfx_*.h; do
   name="$(basename "$header")"
   git -C "$REPO" show "$REF:src/shaders/$name" > "$WORK/ref/$name" 2>/dev/null || true
done

cat > "$WORK/shaders/bgfx/shaderc" <<EOF
#!/bin/bash
exec "$REPO/steamframe/tools/shaderc-dx-from-ref.sh" "$SHADERC" "$WORK/ref" "\$@"
EOF
chmod +x "$WORK/shaders/bgfx/shaderc"

echo "Regenerating the shader headers $HEADERS (DirectX blobs from $REF)"
(cd "$WORK/shaders/bgfx" && bash ./shaders.sh > "$WORK/shaders.log" 2>&1) || { tail -20 "$WORK/shaders.log" >&2; exit 1; }

for name in $HEADERS; do
   cp "$WORK/shaders/bgfx_${name}.h" "$REPO/src/shaders/"
done
git -C "$REPO" status --short -- 'src/shaders/bgfx_*.h'
