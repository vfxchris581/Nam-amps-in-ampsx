#!/usr/bin/env bash
# Linux build (development / CI). Needs: libasound2-dev libx11-dev libxrandr-dev libxinerama-dev
# libxcursor-dev libfreetype6-dev libfontconfig1-dev
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNAMKNOBS_BUILD_SCREENSHOT=ON
cmake --build build --parallel
(cd build && ctest --output-on-failure)
