#!/bin/bash
# shaderc wrapper used by regen-shaders.sh: compiles with the real shaderc, except the DirectX targets (s_5_0 and s_6_0, which
# need the HLSL compiler of Windows) whose blob is copied from the reference headers, by its name (the --bin2c argument).
#
# Usage: shaderc-dx-from-ref.sh <shaderc> <reference headers folder> <shaderc arguments...>

set -euo pipefail

SHADERC="$1"
REF_DIR="$2"
shift 2

profile=""
name=""
output=""
args=("$@")
for ((i = 0; i < ${#args[@]}; i++)); do
   case "${args[$i]}" in
      -p) profile="${args[$((i + 1))]}" ;;
      --bin2c) name="${args[$((i + 1))]}" ;;
      -o) output="${args[$((i + 1))]}" ;;
   esac
done

if [ "$profile" != "s_5_0" ] && [ "$profile" != "s_6_0" ]; then
   exec "$SHADERC" "$@"
fi

# The array goes from 'static const uint8_t <name>[<size>] =' to the next '};'
awk -v name="$name" '
   index($0, "static const uint8_t " name "[") == 1 { copy = 1 }
   copy { print }
   copy && /^};/ { found = 1; exit }
   END { exit found ? 0 : 1 }
' "$REF_DIR"/bgfx_*.h > "$output" || {
   echo "No DirectX blob named '$name' in the reference headers: it is a new shader variant, regenerate the headers on Windows (src/shaders/bgfx/shaders.ps1)" >&2
   exit 1
}
