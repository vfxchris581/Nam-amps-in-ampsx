#!/usr/bin/env bash
# Builds the macOS installer: dist/NamKnobs-macOS.dmg containing
#   Install Nam Knobs.pkg      - installs the VST3, Audio Unit and standalone app (each optional)
#   Uninstall Nam Knobs.command
#   READ ME - Install.md
#
# Usage: BuildScripts/make_dmg.sh [build-dir]
#
# Optional signing / notarisation (all unset = ad-hoc signed bundles, unsigned installer):
#   MACOS_APP_IDENTITY        "Developer ID Application: Name (TEAMID)"   signs the app + plug-ins
#   MACOS_INSTALLER_IDENTITY  "Developer ID Installer: Name (TEAMID)"     signs the .pkg
#   NOTARY_APPLE_ID, NOTARY_TEAM_ID, NOTARY_PASSWORD (app-specific password) → notarise + staple
set -euo pipefail

BUILD="${1:-build}"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"
ART="$BUILD/Source/Plugin/NamKnobs_artefacts/Release"
INST="Installer/macOS"
VERSION="$(sed -nE 's/^project\(NamKnobs VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)"
WORK="dist/pkg-work"
rm -rf "$WORK" dist/dmg && mkdir -p "$WORK"/{roots,pkgs,resources} dist/dmg

sign_bundle() {
    if [ -n "${MACOS_APP_IDENTITY:-}" ]; then
        codesign --force --deep --timestamp --options runtime -s "$MACOS_APP_IDENTITY" "$1"
    else
        codesign --force --deep -s - "$1"   # ad-hoc
    fi
}

# component <id> <bundle source> <install location>
component() {
    local id="$1" src="$2" dest="$3"
    local root="$WORK/roots/$id"
    mkdir -p "$root"
    cp -R "$src" "$root/"
    sign_bundle "$root/$(basename "$src")"
    # Never let Installer "relocate" the bundle to some other copy it finds on disk.
    pkgbuild --analyze --root "$root" "$WORK/$id.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$id.plist" 2>/dev/null || true
    pkgbuild --root "$root" \
             --component-plist "$WORK/$id.plist" \
             --identifier "com.vfxchris.namknobs.$id" \
             --version "$VERSION" \
             --install-location "$dest" \
             --scripts "$INST/scripts/$id" \
             "$WORK/pkgs/$id.pkg"
}

component vst3 "$ART/VST3/Nam Knobs.vst3"           "/Library/Audio/Plug-Ins/VST3"
component au   "$ART/AU/Nam Knobs.component"        "/Library/Audio/Plug-Ins/Components"
component app  "$ART/Standalone/Nam Knobs.app"      "/Applications"

# Installer pages + artwork (a multi-resolution TIFF keeps the artwork sharp on Retina screens)
cp "$INST"/resources/*.html "$WORK/resources/"
sed -i '' "s/__VERSION__/$VERSION/g" "$WORK"/resources/*.html
BACKGROUND="background.png"
if tiffutil -cathidpicheck "$INST/resources/background.png" "$INST/resources/background@2x.png" \
            -out "$WORK/resources/background.tiff" 2>/dev/null; then
    BACKGROUND="background.tiff"
else
    cp "$INST/resources/background.png" "$WORK/resources/"
fi
sed -e "s/__VERSION__/$VERSION/g" -e "s/__BACKGROUND__/$BACKGROUND/g" "$INST/Distribution.xml" > "$WORK/Distribution.xml"

PKG="dist/dmg/Install Nam Knobs.pkg"
SIGN_ARGS=()
[ -n "${MACOS_INSTALLER_IDENTITY:-}" ] && SIGN_ARGS=(--sign "$MACOS_INSTALLER_IDENTITY" --timestamp)
productbuild --distribution "$WORK/Distribution.xml" \
             --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" \
             --version "$VERSION" \
             "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" \
             "$PKG"

if [ -n "${MACOS_INSTALLER_IDENTITY:-}" ] && [ -n "${NOTARY_APPLE_ID:-}" ]; then
    xcrun notarytool submit "$PKG" --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
          --password "$NOTARY_PASSWORD" --wait
    xcrun stapler staple "$PKG"
fi

cp "$INST/Uninstall Nam Knobs.command" dist/dmg/
chmod +x "dist/dmg/Uninstall Nam Knobs.command"
cp Documentation/INSTALL.md "dist/dmg/READ ME - Install.md"

hdiutil create -volname "Nam Knobs $VERSION" -srcfolder dist/dmg -ov -format UDZO "dist/NamKnobs-macOS.dmg"

# Sanity check: list what the installer will put on disk.
pkgutil --payload-files "$WORK/pkgs/vst3.pkg" | head -5
echo "Created dist/NamKnobs-macOS.dmg (installer version $VERSION)"
