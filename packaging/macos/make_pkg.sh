#!/usr/bin/env bash
# =============================================================================
#  Build a signed + notarised macOS installer (.pkg) from the build artefacts.
#
#  Usage:
#    packaging/macos/make_pkg.sh <artefacts-dir> <version>
#      e.g. packaging/macos/make_pkg.sh build-mac/AeroSound_artefacts/Release 1.0.0
#
#  Optional environment:
#    APP_SIGN_ID   "Developer ID Application: Your Name (TEAMID)"   — signs bundles
#    PKG_SIGN_ID   "Developer ID Installer: Your Name (TEAMID)"     — signs the .pkg
#    NOTARY_PROFILE  keychain profile created with `xcrun notarytool store-credentials`
#    AAX: must be PACE-wrapped (wraptool) BEFORE running this script.
#  Without the IDs the script still builds an unsigned .pkg: it installs fine,
#  but on other Macs the user must allow it once (right-click → Open, or
#  System Settings → Privacy & Security → Open Anyway). Files installed by a
#  .pkg are NOT quarantined, so the plug-ins then load without any Terminal step.
# =============================================================================
set -euo pipefail
ART="${1:?artefacts dir}"; VER="${2:?version}"
NAME="AEROSOUND"; ID="com.aerosound.aerosound"
OUT="dist/macos"; WORK="$(mktemp -d)"
mkdir -p "$OUT"

sign() { [[ -n "${APP_SIGN_ID:-}" ]] && codesign --force --deep --timestamp --options runtime --sign "$APP_SIGN_ID" "$1" || true; }

component() {   # $1 = bundle path, $2 = install location, $3 = id suffix
    [[ -e "$1" ]] || { echo "skip (not built): $1"; return; }
    sign "$1"
    local root="$WORK/root-$3"; mkdir -p "$root"
    cp -R "$1" "$root/"
    # Never "relocate": without this, Installer may write into an older copy found
    # elsewhere on disk instead of the standard plug-in folder.
    local plist="$WORK/$3.plist"
    pkgbuild --analyze --root "$root" "$plist"
    # pkgbuild may list 0…n bundles (e.g. none for a .vst3): patch every entry present.
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null \
          || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"   # key may be absent
        i=$((i + 1))
    done
    pkgbuild --root "$root" --component-plist "$plist" --install-location "$2" \
             --identifier "$ID.$3" --version "$VER" "$WORK/$3.pkg"
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
    case "$c" in
      vst3) title="VST3 plug-in (Studio One, Ableton, FL Studio, Cubase, Bitwig...)" ;;
      au) title="Audio Unit (Logic Pro, GarageBand)" ;;
      aax) title="AAX (Pro Tools)" ;;
      *) title="AEROSOUND standalone app" ;;
    esac
    echo "<choice id=\"$c\" title=\"$title\"><pkg-ref id=\"$ID.$c\"/></choice>"
    echo "<pkg-ref id=\"$ID.$c\" version=\"$VER\">$c.pkg</pkg-ref>"
  done
  echo '</installer-gui-script>'
} > "$DIST"

PKG="$OUT/AEROSOUND-$VER-macOS.pkg"
SIGNARGS=(); [[ -n "${PKG_SIGN_ID:-}" ]] && SIGNARGS=(--sign "$PKG_SIGN_ID")
# ${arr[@]+...} form: safe with `set -u` on macOS's bash 3.2 when the array is empty
productbuild --distribution "$DIST" --package-path "$WORK" ${SIGNARGS[@]+"${SIGNARGS[@]}"} "$PKG"

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi
rm -rf "$WORK"
echo "Created $PKG"
