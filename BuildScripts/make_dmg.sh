#!/usr/bin/env bash
# Packages the macOS build (Standalone app + VST3 + AU) into dist/NamKnobs-macOS.dmg
# Usage: BuildScripts/make_dmg.sh [build-dir]
set -euo pipefail
BUILD="${1:-build}"
ART="$BUILD/Source/Plugin/NamKnobs_artefacts/Release"
STAGE="dist/dmg"
rm -rf "$STAGE" && mkdir -p "$STAGE/Plug-Ins"

cp -R "$ART/Standalone/Nam Knobs.app" "$STAGE/"
cp -R "$ART/VST3/Nam Knobs.vst3" "$STAGE/Plug-Ins/"
[ -d "$ART/AU/Nam Knobs.component" ] && cp -R "$ART/AU/Nam Knobs.component" "$STAGE/Plug-Ins/"
ln -s /Applications "$STAGE/Applications"
cp Documentation/INSTALL.md "$STAGE/READ ME - Install.md"

# Ad-hoc signature so Gatekeeper treats the bundles consistently (use your Developer ID to distribute).
codesign --force --deep -s - "$STAGE/Nam Knobs.app" || true
for b in "$STAGE"/Plug-Ins/*; do codesign --force --deep -s - "$b" || true; done

mkdir -p dist
hdiutil create -volname "Nam Knobs" -srcfolder "$STAGE" -ov -format UDZO "dist/NamKnobs-macOS.dmg"
echo "Created dist/NamKnobs-macOS.dmg"
