#!/usr/bin/env sh
set -eu

EMSDK_DIR="${EMSDK_DIR:-$HOME/GitHub/emsdk}"
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
PORT=8090
CDP_PORT=9222
BUILD_DIR="build-wasm"
TIMEOUT="${1:-15}"

if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
  echo "emsdk not found at $EMSDK_DIR"
  echo "Set EMSDK_DIR to point to your emsdk installation."
  exit 1
fi

# shellcheck disable=SC1091
. "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1

echo "Configuring wasm build..."
emcmake cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release

echo "Building..."
cmake --build "$BUILD_DIR" --parallel 8

echo ""
echo "Build complete. Running headless test (${TIMEOUT}s timeout)..."

# Clean up on exit
cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
  kill "$CHROME_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Start HTTP server
python3 -m http.server "$PORT" -d "$BUILD_DIR" > /dev/null 2>&1 &
SERVER_PID=$!
sleep 1

# Launch headless Chrome with remote debugging
"$CHROME" \
  --headless=new \
  --disable-gpu-sandbox \
  --enable-unsafe-webgpu \
  --enable-features=Vulkan,UseSkiaRenderer \
  --disable-vulkan-surface \
  --use-angle=swiftshader \
  --run-all-compositor-stages-before-draw \
  --disable-extensions \
  --no-first-run \
  --disable-background-networking \
  --remote-debugging-port="$CDP_PORT" \
  "http://localhost:$PORT/flecs_engine.html" \
  > /dev/null 2>&1 &
CHROME_PID=$!

# Wait for Chrome to be ready
echo "Waiting for Chrome..."
for i in $(seq 1 15); do
  sleep 1
  curl -s "http://localhost:$CDP_PORT/json" > /dev/null 2>&1 && break
done

# Stream console output
python3 "$(dirname "$0")/wasm_console.py" "$CDP_PORT" "$TIMEOUT"

echo "Done."
