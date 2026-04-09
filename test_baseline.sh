#!/usr/bin/env bash
#
# Render a baseline frame for every scene in etc/assets/scenes and store the
# result as a PPM image under test/scenes/<scene>.ppm.
#
# Usage: ./test_baseline.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RUN="./run.sh"
SCENES_DIR="etc/assets/scenes"
OUT_DIR="test/scenes"

if [[ ! -x "$RUN" ]]; then
  echo "error: $RUN not found or not executable" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

shopt -s nullglob
scenes=("$SCENES_DIR"/*.flecs)
shopt -u nullglob

if [[ ${#scenes[@]} -eq 0 ]]; then
  echo "error: no scenes found in $SCENES_DIR" >&2
  exit 1
fi

failures=()

for scene in "${scenes[@]}"; do
  name="$(basename "$scene" .flecs)"
  out="$OUT_DIR/$name.ppm"
  echo "==> Rendering $name -> $out"
  if "$RUN" --scene "$scene" --frame-out "$out"; then
    echo "    ok"
  else
    echo "    FAILED" >&2
    failures+=("$name")
  fi
done

echo
if [[ ${#failures[@]} -eq 0 ]]; then
  echo "All ${#scenes[@]} scene(s) rendered successfully."
else
  echo "${#failures[@]} scene(s) failed:" >&2
  for f in "${failures[@]}"; do
    echo "  - $f" >&2
  done
  exit 1
fi
