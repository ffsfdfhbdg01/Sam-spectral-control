#!/usr/bin/env bash
# macOS installer of Sam Spectral Control (REL-1, ADR-0014).
#
#   packaging/macos/build-pkg.sh <plug-in artefacts dir> <version> <output dir>
#
# The artefacts dir is JUCE's SamSpectralControl_artefacts/Release with VST3/ and, when built, AU/.
# The result is SamSpectralControl-<version>-macOS.pkg with selectable components: the VST3
# plug-in, the Audio Unit (when built) and the uninstaller with the licences, installed to
# /Library/Application Support/Sam Spectral Control.
#
# Signing and notarisation run only when their variables are set. Otherwise the bundles get an
# ad-hoc signature (Apple silicon loads only signed code; Gatekeeper still treats them as
# unsigned) and the package stays unsigned:
#   MACOS_APP_IDENTITY        codesign identity, "Developer ID Application: <name> (<team>)"
#   MACOS_INSTALLER_IDENTITY  productbuild identity, "Developer ID Installer: <name> (<team>)"
#   MACOS_KEYCHAIN            keychain that holds them (optional)
#   NOTARY_KEY_FILE, NOTARY_KEY_ID, NOTARY_ISSUER_ID
#                             App Store Connect API key (.p8 file, key ID, issuer ID) for notarytool
# DRY_RUN=1 prints the macOS tool invocations instead of running them (to check the script elsewhere).
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <plug-in artefacts dir> <version> <output dir>" >&2
    exit 2
fi

artefacts="$1"
version="$2"
output="$3"
name="Sam Spectral Control"
identifier="com.samaudio.samspectralcontrol" # BUNDLE_ID in src/CMakeLists.txt (ADR-0016)
support="/Library/Application Support/$name"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
package="$output/SamSpectralControl-$version-macOS.pkg"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

run() {
    if [[ "${DRY_RUN:-0}" == 1 ]]; then
        printf '[dry run]'
        printf ' %q' "$@"
        printf '\n'
    else
        "$@"
    fi
}

copy() {
    if command -v ditto > /dev/null; then
        ditto "$1" "$2"
    else
        cp -R "$1" "$2"
    fi
}

keychain=()
if [[ -n "${MACOS_KEYCHAIN:-}" ]]; then
    keychain=(--keychain "$MACOS_KEYCHAIN")
fi

vst3="$artefacts/VST3/$name.vst3"
au="$artefacts/AU/$name.component"
if [[ ! -d "$vst3" ]]; then
    echo "missing $vst3" >&2
    exit 1
fi
components=(vst3)
if [[ -d "$au" ]]; then
    components+=(au)
fi

# Staging: one root per component.
mkdir -p "$work/root-vst3" "$work/root-uninstaller" "$work/packages" "$work/resources" "$output"
copy "$vst3" "$work/root-vst3/$name.vst3"
if [[ -d "$au" ]]; then
    mkdir -p "$work/root-au"
    copy "$au" "$work/root-au/$name.component"
fi
cp "$here/uninstall.command" "$work/root-uninstaller/Uninstall $name.command"
chmod 755 "$work/root-uninstaller/Uninstall $name.command"
cp "$repo/packaging/LICENSE.txt" "$repo/THIRD-PARTY-NOTICES.txt" "$work/root-uninstaller/"
cp "$repo/docs/manual.md" "$work/root-uninstaller/Manual.md"
mkdir -p "$work/root-uninstaller/images"
cp "$repo/docs/images/editor.png" "$repo/docs/images/editor-advanced.png" "$repo/docs/images/sidechain.png" \
    "$repo/docs/images/solo.png" "$work/root-uninstaller/images/"

# Code signature of the bundles: Developer ID, hardened runtime, secure timestamp.
for component in "${components[@]}"; do
    for bundle in "$work/root-$component"/*; do
        if [[ -n "${MACOS_APP_IDENTITY:-}" ]]; then
            run codesign --force --sign "$MACOS_APP_IDENTITY" ${keychain[@]+"${keychain[@]}"} --options runtime --timestamp "$bundle"
            run codesign --verify --strict --verbose=2 "$bundle"
        else
            echo "warning: MACOS_APP_IDENTITY is not set; $(basename "$bundle") gets an ad-hoc signature" >&2
            run codesign --force --sign - "$bundle"
            run codesign --verify --strict --verbose=2 "$bundle"
        fi
    done
done

# Component packages. Bundles are not relocatable: the installer always writes to the standard
# plug-in folders, even if a copy of the bundle exists elsewhere.
component_package() { # <component> <install location>
    local component="$1" location="$2" plist=()
    if [[ "$component" != uninstaller ]]; then
        run pkgbuild --analyze --root "$work/root-$component" "$work/$component.plist"
        run plutil -replace 0.BundleIsRelocatable -bool NO "$work/$component.plist"
        plist=(--component-plist "$work/$component.plist")
    fi
    run pkgbuild --root "$work/root-$component" ${plist[@]+"${plist[@]}"} --identifier "$identifier.$component" \
        --version "$version" --install-location "$location" "$work/packages/$component.pkg"
}
component_package vst3 "/Library/Audio/Plug-Ins/VST3"
if [[ -d "$au" ]]; then
    component_package au "/Library/Audio/Plug-Ins/Components"
fi
component_package uninstaller "$support"

# Product archive: welcome, licence, component choice (REL-1).
sed "s/@VERSION@/$version/g" "$here/welcome.txt" > "$work/resources/welcome.txt"
cp "$repo/packaging/LICENSE.txt" "$work/resources/LICENSE.txt"
{
    cat << EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$name $version</title>
    <organization>com.samaudio</organization>
    <domains enable_localSystem="true" enable_anywhere="false" enable_currentUserHome="false"/>
    <options customize="always" require-scripts="false" rootVolumeOnly="true" hostArchitectures="arm64,x86_64"/>
    <allowed-os-versions>
        <os-version min="11.0"/>
    </allowed-os-versions>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <choices-outline>
        <line choice="vst3"/>
EOF
    if [[ -d "$au" ]]; then
        echo '        <line choice="au"/>'
    fi
    cat << EOF
        <line choice="uninstaller"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plug-in" description="Installs $name.vst3 to /Library/Audio/Plug-Ins/VST3." start_selected="true">
        <pkg-ref id="$identifier.vst3"/>
    </choice>
EOF
    if [[ -d "$au" ]]; then
        cat << EOF
    <choice id="au" title="Audio Unit" description="Installs $name.component to /Library/Audio/Plug-Ins/Components." start_selected="true">
        <pkg-ref id="$identifier.au"/>
    </choice>
EOF
    fi
    cat << EOF
    <choice id="uninstaller" title="Uninstaller, licences and manual" description="Installs the uninstaller, the licence texts and the user manual to $support. Always installed." start_selected="true" enabled="false">
        <pkg-ref id="$identifier.uninstaller"/>
    </choice>
    <pkg-ref id="$identifier.vst3" version="$version" onConclusion="none">vst3.pkg</pkg-ref>
EOF
    if [[ -d "$au" ]]; then
        echo "    <pkg-ref id=\"$identifier.au\" version=\"$version\" onConclusion=\"none\">au.pkg</pkg-ref>"
    fi
    cat << EOF
    <pkg-ref id="$identifier.uninstaller" version="$version" onConclusion="none">uninstaller.pkg</pkg-ref>
</installer-gui-script>
EOF
} > "$work/distribution.xml"
if command -v xmllint > /dev/null; then
    xmllint --noout "$work/distribution.xml"
fi

sign=()
if [[ -n "${MACOS_INSTALLER_IDENTITY:-}" ]]; then
    sign=(--sign "$MACOS_INSTALLER_IDENTITY" ${keychain[@]+"${keychain[@]}"} --timestamp)
else
    echo "warning: MACOS_INSTALLER_IDENTITY is not set; the package stays unsigned" >&2
fi
run productbuild --distribution "$work/distribution.xml" --package-path "$work/packages" --resources "$work/resources" \
    --version "$version" ${sign[@]+"${sign[@]}"} "$package"

# Notarisation and stapling (needs a signed package).
if [[ -n "${NOTARY_KEY_FILE:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER_ID:-}" ]]; then
    if [[ ${#sign[@]} -eq 0 ]]; then
        echo "notarisation needs a signed package (MACOS_INSTALLER_IDENTITY)" >&2
        exit 1
    fi
    if [[ "${DRY_RUN:-0}" == 1 ]]; then
        run xcrun notarytool submit "$package" --key "$NOTARY_KEY_FILE" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID" --wait
    else
        xcrun notarytool submit "$package" --key "$NOTARY_KEY_FILE" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID" \
            --wait --timeout 1h --output-format json > "$work/notary.json"
        status="$(plutil -extract status raw -o - "$work/notary.json")"
        if [[ "$status" != Accepted ]]; then
            submission="$(plutil -extract id raw -o - "$work/notary.json")"
            xcrun notarytool log "$submission" --key "$NOTARY_KEY_FILE" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER_ID" || true
            echo "notarisation failed: $status" >&2
            exit 1
        fi
    fi
    run xcrun stapler staple "$package"
    run xcrun stapler validate "$package"
    run spctl --assess --type install --verbose=2 "$package"
else
    echo "warning: notarisation skipped (NOTARY_KEY_FILE, NOTARY_KEY_ID, NOTARY_ISSUER_ID)" >&2
fi

if [[ "${DRY_RUN:-0}" == 1 ]]; then
    cp "$work/distribution.xml" "$output/distribution.xml"
fi
echo "$package"
