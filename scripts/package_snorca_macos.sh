#!/bin/bash
# Package the existing FOrca build as Snorca without renaming its internal binary.
set -euo pipefail
cd "$(dirname "$0")/.."
case "${1:-}" in
    arm64|x86_64) arch=$1 ;;
    *) echo "Usage: bash scripts/package_snorca_macos.sh arm64|x86_64" >&2; exit 2 ;;
esac
[ "$(uname -s)" = Darwin ] || { echo "Packaging requires macOS." >&2; exit 1; }
source_app="build/$arch/FOrcaSlicer/FOrcaSlicer.app"
[ -x "$source_app/Contents/MacOS/FOrcaSlicer" ]
[ -d "$source_app/Contents/Resources" ]
[ ! -L "$source_app/Contents/Resources" ]

mkdir -p dist
stage="$(mktemp -d "$PWD/dist/stage.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
app="$stage/Snorca MultiAce.app"
ditto "$source_app" "$app"
plist="$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName Snorca MultiAce' "$plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.mnemonic3d.snorca-multiace' "$plist"
/usr/libexec/PlistBuddy -c 'Add :CFBundleDisplayName string Snorca MultiAce' "$plist"
/usr/libexec/PlistBuddy -c "Add :LSMinimumSystemVersion string ${OSX_DEPLOYMENT_TARGET:-12.0}" "$plist"
/usr/libexec/PlistBuddy -c 'Add :NSLocalNetworkUsageDescription string Snorca connects to your printer on the local network.' "$plist"
plutil -lint "$plist"

# Fail if any bundled executable is the wrong architecture, links to a build
# machine's dependency, or contains a symlink outside the portable app bundle.
python3 scripts/verify_macos_bundle.py "$app" "$arch" | tee dist/bundle-audit.txt

# Ad-hoc signatures validate bundle integrity on Apple Silicon. This is not
# Developer ID signing or Apple notarization; Gatekeeper may still block it.
while IFS= read -r -d '' file_path; do
    if file -b "$file_path" | grep -q 'Mach-O'; then
        codesign --force --sign - "$file_path"
    fi
done < <(find "$app" -type f -print0)
codesign --force --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

revision=$(git rev-parse --short=12 HEAD)
cat > dist/BUILD-INFO.txt <<EOF
Snorca MultiAce — experimental macOS build
Architecture: $arch
Minimum macOS: ${OSX_DEPLOYMENT_TARGET:-12.0}
Source commit: $(git rev-parse HEAD)
Source repository: ${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-hprisback/Snorca-MultiAce}
Signing: ad-hoc; not Apple notarized
Settings: shared with FOrcaSlicer; back up existing profiles before testing.
Printer operation has not been validated by this build pipeline.
EOF
cp dist/BUILD-INFO.txt "$stage/BUILD-INFO.txt"
cp LICENSE.txt "$stage/LICENSE.txt"
ln -s /Applications "$stage/Applications"
dmg="Snorca-MultiAce-${revision}-macOS-${arch}.dmg"
hdiutil create -volname 'Snorca MultiAce' -srcfolder "$stage" \
    -ov -format UDZO -imagekey zlib-level=9 "dist/$dmg"
hdiutil verify "dist/$dmg"
(cd dist && shasum -a 256 "$dmg" > "$dmg.sha256")
# Keep the validated bundle for the workflow's native CLI smoke test.
mkdir -p dist/staging
ditto "$app" 'dist/staging/Snorca MultiAce.app'
