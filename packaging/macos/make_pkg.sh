#!/usr/bin/env bash
# =============================================================================
#  Build a signed + notarised macOS installer (.pkg) from the build artefacts.
#
#  Usage:
#    packaging/macos/make_pkg.sh <artefacts-dir> <version>
#      e.g. packaging/macos/make_pkg.sh build-mac/AeroGlow_artefacts/Release 1.0.0
#
#  Optional environment:
#    APP_SIGN_ID   "Developer ID Application: Your Name (TEAMID)"   — signs bundles
#    PKG_SIGN_ID   "Developer ID Installer: Your Name (TEAMID)"     — signs the .pkg
#    NOTARY_PROFILE  keychain profile created with `xcrun notarytool store-credentials`
#    AAX: must be PACE-wrapped (wraptool) BEFORE running this script.
#  Without the IDs the script still builds an unsigned .pkg (local testing only;
#  Gatekeeper will block it on other machines).
# =============================================================================
set -euo pipefail
ART="${1:?artefacts dir}"; VER="${2:?version}"
NAME="AeroGlow Saturator"; ID="com.aeroglowaudio.aeroglow"
OUT="dist/macos"; WORK="$(mktemp -d)"
mkdir -p "$OUT"

sign() { [[ -n "${APP_SIGN_ID:-}" ]] && codesign --force --deep --timestamp --options runtime --sign "$APP_SIGN_ID" "$1" || true; }

component() {   # $1 = bundle path, $2 = install location, $3 = id suffix
    [[ -e "$1" ]] || { echo "skip (not built): $1"; return; }
    sign "$1"
    local root="$WORK/root-$3"; mkdir -p "$root"
    cp -R "$1" "$root/"
    pkgbuild --root "$root" --install-location "$2" --identifier "$ID.$3" --version "$VER" "$WORK/$3.pkg"
    CHOICES+=("$3")
}

CHOICES=()
component "$ART/VST3/$NAME.vst3"             "/Library/Audio/Plug-Ins/VST3"                 vst3
component "$ART/AU/$NAME.component"          "/Library/Audio/Plug-Ins/Components"           au
component "$ART/AAX/$NAME.aaxplugin"         "/Library/Application Support/Avid/Audio/Plug-Ins" aax
component "$ART/Standalone/$NAME.app"        "/Applications"                                standalone

# Distribution file: one checkbox per format
DIST="$WORK/distribution.xml"
{
  echo '<?xml version="1.0" encoding="utf-8"?><installer-gui-script minSpecVersion="2">'
  echo "<title>$NAME $VER</title><options customize=\"allow\" require-scripts=\"false\" hostArchitectures=\"arm64,x86_64\"/>"
  echo '<choices-outline>'; for c in "${CHOICES[@]}"; do echo "<line choice=\"$c\"/>"; done; echo '</choices-outline>'
  for c in "${CHOICES[@]}"; do
    echo "<choice id=\"$c\" title=\"$(echo "$c" | tr a-z A-Z)\"><pkg-ref id=\"$ID.$c\"/></choice>"
    echo "<pkg-ref id=\"$ID.$c\" version=\"$VER\">$c.pkg</pkg-ref>"
  done
  echo '</installer-gui-script>'
} > "$DIST"

PKG="$OUT/AeroGlow-$VER-macOS.pkg"
SIGNARGS=(); [[ -n "${PKG_SIGN_ID:-}" ]] && SIGNARGS=(--sign "$PKG_SIGN_ID")
productbuild --distribution "$DIST" --package-path "$WORK" "${SIGNARGS[@]}" "$PKG"

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi
rm -rf "$WORK"
echo "Created $PKG"
