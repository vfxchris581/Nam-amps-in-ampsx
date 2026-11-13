#!/usr/bin/env bash
# Build Nam Knobs (Standalone + VST3 + AU, universal arm64/x86_64), run tests, make a DMG.
# Requires: Xcode command line tools, CMake >= 3.22.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -G Xcode
cmake --build build --config Release --parallel
(cd build && ctest -C Release --output-on-failure)
bash BuildScripts/make_dmg.sh build
echo "Plug-ins: build/Source/Plugin/NamKnobs_artefacts/Release/{VST3,AU,Standalone}"
