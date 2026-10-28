#!/bin/bash
# Configure and build the KICK app (Kick target) and the simulator (KT_KickEM) on macOS.
# Needs CMake and Ninja on PATH (or set CMAKE / NINJA), and network access the first time
# to fetch JUCE 7.0.12 and CPM.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMAKE="${CMAKE:-cmake}"
NINJA="${NINJA:-ninja}"
BUILD="$ROOT/Kick/build-mac"
JUCE_DIR="$ROOT/.3rdPartySourceCache/JUCE-7.0.12"
[ -d "$JUCE_DIR" ] || git clone --depth 1 --branch 7.0.12 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"

# JUCE 7.0.12's juceaide calls an API removed in macOS 15; target 12.0 as Kick itself does.
export MACOSX_DEPLOYMENT_TARGET=12.0
"$CMAKE" -S "$ROOT/Kick" -B "$BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$(command -v "$NINJA" || echo "$NINJA")" \
    -DCMAKE_BUILD_TYPE=Release -DTIMESTAMP=OFF -DKICK_SIMULATOR=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCPM_JUCE_SOURCE="$JUCE_DIR" -DCPM_AudIO_SOURCE="$ROOT/AudIO" -DCPM_COM_PROTOCOL_SOURCE="$ROOT/ComProtocol"
"$NINJA" -C "$BUILD" Kick KT_KickEM

# On macOS the app finds ./assets, ./demos and ./templates relative to the working directory,
# and the build copies them into the build folder, so start the apps from there.
echo "Built: $BUILD/Kick.app and $BUILD/KT_KickEM.app"
echo "Run the simulator with:"
echo "  cd \"$BUILD\" && KT_KickEM.app/Contents/MacOS/KT_KickEM"
