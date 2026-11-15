# Snorca MultiAce for Mac

This fork adds an experimental macOS build of the full Snorca slicer. It does
not include the optional ACE touch-screen label installer.

## Download a completed build

Open this fork's **Actions → Build Snorca for Mac**, select a successful run,
and download the artifact matching **Apple menu → About This Mac**:

| Your Mac | Artifact |
| --- | --- |
| Apple M-series chip | `Snorca-MultiAce-macOS-arm64` |
| Intel processor | `Snorca-MultiAce-macOS-x86_64` |

Requires macOS 12 or newer. Extract the artifact ZIP, open its DMG, and drag
**Snorca MultiAce.app** to **Applications**. The DMG contains the licence and
source revision. There is no completed installer until the workflow succeeds.

The app is ad-hoc signed, not Developer ID signed or Apple notarized. If macOS
blocks it, review the warning and use System Settings → Privacy & Security →
Open Anyway only if you trust the source and this build. Do not disable
Gatekeeper globally.

It retains FOrcaSlicer's internal executable and settings folder. Back up your
existing FOrcaSlicer profiles before testing. The inherited updater may offer
FOrcaSlicer releases; use this fork's build artifacts to stay on Snorca.

## Run the build

In this fork, enable Actions if GitHub asks, then select **Build Snorca for Mac
→ Run workflow → main**. Both architectures build independently on GitHub Mac
runners. Dependency prefixes are cached; first builds may take several hours.
The workflow only creates 30-day artifacts; it does not publish a release.

The pipeline checks every bundled Mach-O's architecture, rejects external
absolute library paths and escaped/broken symlinks, verifies the ad-hoc
signature and DMG, and runs `--help` for the native ARM64 app. These checks do
not validate the GUI, printer discovery, slicing or a physical print. Intel
execution and both versions' printer operation need testing on actual Macs.

## Build locally

Install Apple's Command Line Tools, Homebrew prerequisites and CMake 3.31.6
as described in `README-FOrcaSlicer-upstream.md` under macOS. Ensure gettext's
`msgfmt` is on PATH. From the repository root:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=3
SNORCA_ARCH=$(uname -m)
bash build_release_macos.sh -d -a "$SNORCA_ARCH" -t 12.0 -T
bash build_release_macos.sh -s -x -a "$SNORCA_ARCH" -t 12.0 -T
bash scripts/package_snorca_macos.sh "$SNORCA_ARCH"
```

The installer is written to `dist/`. Keep the source commit and SHA-256 file
with any installer you share. Original project credits and AGPL licence apply.
