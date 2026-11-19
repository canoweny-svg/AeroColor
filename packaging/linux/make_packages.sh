#!/usr/bin/env bash
# =============================================================================
#  Linux packages for AEROSOUND
#    dist/linux/aerosound_<ver>_amd64.deb      Ubuntu / Debian / Mint / Pop!_OS
#        /usr/lib/vst3/AEROSOUND.vst3   (system VST3 folder, every Linux DAW scans it)
#        /usr/bin/aerosound             (standalone app) + desktop menu entry
#    dist/linux/AEROSOUND-<ver>-Linux.tar.gz   any distro: run ./install.sh
#        (installs per user: ~/.vst3, ~/.local/bin, no root needed)
#
#  Usage: packaging/linux/make_packages.sh <artefacts-dir> <version>
# =============================================================================
set -euo pipefail
ART="${1:?artefacts dir}"; VER="${2:?version}"
OUT="dist/linux"; WORK="$(mktemp -d)"
mkdir -p "$OUT"
VST3="$ART/VST3/AEROSOUND.vst3"; APP="$ART/Standalone/AEROSOUND"
[[ -d "$VST3" ]] || { echo "missing $VST3"; exit 1; }

DESKTOP='[Desktop Entry]
Type=Application
Name=AEROSOUND
Comment=Glass, bubbles and air effect
Exec=aerosound
Icon=audio-x-generic
Categories=AudioVideo;Audio;
Terminal=false'

# ---------------------------------------------------------------- .deb ------
D="$WORK/deb"
mkdir -p "$D/DEBIAN" "$D/usr/lib/vst3" "$D/usr/bin" "$D/usr/share/applications"
cp -R "$VST3" "$D/usr/lib/vst3/"
[[ -f "$APP" ]] && install -m 755 "$APP" "$D/usr/bin/aerosound"
echo "$DESKTOP" > "$D/usr/share/applications/aerosound.desktop"
cat > "$D/DEBIAN/control" <<CTL
Package: aerosound
Version: $VER
Architecture: amd64
Section: sound
Priority: optional
Maintainer: AeroSound <support@example.com>
Depends: libasound2t64 | libasound2, libfreetype6, libfontconfig1, libx11-6, libxext6
Description: AEROSOUND - glass, bubbles and air audio effect (VST3 + standalone)
 Turns any sound into a bright, glassy, bubbly early-2000s "Aero" world.
CTL
find "$D" -type d -exec chmod 755 {} +
dpkg-deb --build --root-owner-group "$D" "$OUT/aerosound_${VER}_amd64.deb" >/dev/null

# ------------------------------------------------------------- .tar.gz -----
T="$WORK/AEROSOUND-$VER-Linux"
mkdir -p "$T"
cp -R "$VST3" "$T/"
[[ -f "$APP" ]] && install -m 755 "$APP" "$T/AEROSOUND"
echo "$DESKTOP" > "$T/aerosound.desktop"
cat > "$T/install.sh" <<'INS'
#!/usr/bin/env bash
# Installs AEROSOUND for the current user (no root needed).
set -e
cd "$(dirname "$0")"
mkdir -p ~/.vst3 ~/.local/bin ~/.local/share/applications
rm -rf ~/.vst3/AEROSOUND.vst3 && cp -R AEROSOUND.vst3 ~/.vst3/
if [ -f AEROSOUND ]; then
  install -m 755 AEROSOUND ~/.local/bin/aerosound
  cp aerosound.desktop ~/.local/share/applications/
fi
echo "AEROSOUND installed. Rescan plug-ins in your DAW (VST3 folder: ~/.vst3)."
INS
cat > "$T/uninstall.sh" <<'UNS'
#!/usr/bin/env bash
rm -rf ~/.vst3/AEROSOUND.vst3 ~/.local/bin/aerosound ~/.local/share/applications/aerosound.desktop
echo "AEROSOUND removed."
UNS
chmod 755 "$T/install.sh" "$T/uninstall.sh"
tar -C "$WORK" -czf "$OUT/AEROSOUND-$VER-Linux.tar.gz" "AEROSOUND-$VER-Linux"

rm -rf "$WORK"
ls -la "$OUT"
