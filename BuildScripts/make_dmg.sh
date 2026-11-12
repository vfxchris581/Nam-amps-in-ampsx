#!/usr/bin/env bash
# Packages the macOS build (Standalone app + VST3 + AU) into dist/Namified-macOS.dmg
# Usage: BuildScripts/make_dmg.sh [build-dir]
set -euo pipefail
BUILD="${1:-build}"
ART="$BUILD/Source/Plugin/Namified_artefacts/Release"
STAGE="dist/dmg"
rm -rf "$STAGE" && mkdir -p "$STAGE/Plug-Ins"

cp -R "$ART/Standalone/Namified.app" "$STAGE/"
cp -R "$ART/VST3/Namified.vst3" "$STAGE/Plug-Ins/"
[ -d "$ART/AU/Namified.component" ] && cp -R "$ART/AU/Namified.component" "$STAGE/Plug-Ins/"
ln -s /Applications "$STAGE/Applications"
cp Documentation/INSTALL.md "$STAGE/READ ME - Install.md"

# Ad-hoc signature so Gatekeeper treats the bundles consistently (use your Developer ID to distribute).
codesign --force --deep -s - "$STAGE/Namified.app" || true
for b in "$STAGE"/Plug-Ins/*; do codesign --force --deep -s - "$b" || true; done

mkdir -p dist
hdiutil create -volname "Namified" -srcfolder "$STAGE" -ov -format UDZO "dist/Namified-macOS.dmg"
echo "Created dist/Namified-macOS.dmg"
