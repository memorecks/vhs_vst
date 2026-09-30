#!/bin/bash
# Builds the macOS installer dist/VHS-<version>-mac.pkg (universal VST3 + AU + Standalone).
#
#   ./package_mac.sh
#       Unsigned installer: the plug-ins keep the ad-hoc signature from the build. Users have to
#       allow the installer once in System Settings > Privacy & Security (see README).
#
#   DEV_ID_APP="Developer ID Application: Name (TEAMID)" \
#   DEV_ID_INSTALLER="Developer ID Installer: Name (TEAMID)" \
#   NOTARY_PROFILE=vhs-notary ./package_mac.sh
#       Signs the bundles (hardened runtime) and the installer, notarizes and staples it, so it
#       opens without warnings. NOTARY_PROFILE is a keychain profile created once with
#       xcrun notarytool store-credentials vhs-notary --apple-id <id> --team-id <TEAMID>
#
# Pass --no-build to package the existing build.
set -euo pipefail
cd "$(dirname "$0")"

[ "${1:-}" == "--no-build" ] || ./build_mac.sh

VERSION=$(sed -n 's/^project(VHS VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ART=build/VHS_artefacts/Release
WORK=build/pkg
PKG=dist/VHS-$VERSION-mac.pkg
rm -rf "$WORK"
mkdir -p "$WORK" dist

if [ -n "${DEV_ID_APP:-}" ] && [ -z "${DEV_ID_INSTALLER:-}" ]; then
    echo "DEV_ID_APP is set but DEV_ID_INSTALLER is not" >&2; exit 1
fi

# sign BUNDLE [ENTITLEMENTS]
sign() {
    if [ -n "${DEV_ID_APP:-}" ]; then
        codesign --force --timestamp --options runtime ${2:+--entitlements "$2"} --sign "$DEV_ID_APP" "$1"
    else
        # re-seal ad-hoc: incremental builds can leave a signature that no longer matches Info.plist
        codesign --force --sign - "$1"
    fi
    codesign --verify --strict "$1"
}

# component ID BUNDLE INSTALL_DIR [SCRIPTS_DIR]
component() {
    local root="$WORK/root_$1"
    mkdir -p "$root"
    ditto "$2" "$root/$(basename "$2")"
    # Without this, Installer "upgrades" a copy of the bundle wherever the user moved it instead.
    pkgbuild --analyze --root "$root" "$WORK/$1.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$1.plist"
    pkgbuild --quiet --root "$root" --component-plist "$WORK/$1.plist" \
        --identifier "com.memorecks.vhs.$1" --version "$VERSION" --install-location "$3" \
        ${4:+--scripts "$4"} "$WORK/$1.pkg"
}

sign "$ART/VST3/VHS.vst3"
sign "$ART/AU/VHS.component"
sign "$ART/Standalone/VHS.app" packaging/Standalone.entitlements

component vst3 "$ART/VST3/VHS.vst3" /Library/Audio/Plug-Ins/VST3
component au "$ART/AU/VHS.component" /Library/Audio/Plug-Ins/Components packaging/au_scripts
component app "$ART/Standalone/VHS.app" /Applications

sed "s/@VERSION@/$VERSION/g" packaging/distribution.xml > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" \
    ${DEV_ID_INSTALLER:+--sign "$DEV_ID_INSTALLER"} "$PKG"

if [ -n "${DEV_ID_APP:-}" ]; then
    if [ -n "${NOTARY_PROFILE:-}" ]; then
        out=$(xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait)
        echo "$out"
        grep -q "status: Accepted" <<< "$out" || { echo "Notarization failed (xcrun notarytool log <id> --keychain-profile $NOTARY_PROFILE)" >&2; exit 1; }
        xcrun stapler staple "$PKG"
        spctl --assess --type install -vv "$PKG"
    else
        echo "NOTARY_PROFILE not set: the installer is signed but not notarized, Gatekeeper will still warn." >&2
    fi
fi
echo "Built: $PKG"
