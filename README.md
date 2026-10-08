# GitHub Actions iOS build experiment

This folder stages the locally generated Build 744825 Xcode export for a **private**
GitHub repository. The workflow compiles an **unsigned** IPA on a hosted macOS
runner; it does not make Oxygen Not Included playable or sign the app.

## Prepare locally

From the main workspace root, run:

```powershell
python .\porting\GitHubIPA\prepare.py
```

The script writes `package/manifest.json` and 512 MiB archive parts. Review the
contents before uploading. Keep this repository private because the archive
contains the game's code and assets. Use Git LFS for all `package/*.part-*`
files; `.gitattributes` already declares them. GitHub Actions must be enabled.

The source is the **entire** `porting/analysis/unity-export-744825/ios-xcode-744825`
directory, not just `Unity-iPhone.xcodeproj`. Do not publish a public repository,
Release, or artifact containing these assets.

## Build

Create a private repository with this folder as its root and push it. Run
**Actions → Build unsigned iOS IPA → Run workflow**. The macOS job verifies the
archive SHA-256, extracts it, invokes `xcodebuild` for `generic/platform=iOS`
with `CODE_SIGNING_ALLOWED=NO`, and uploads `ONI-unsigned.ipa` for one day.

The IPA is unsigned. For a regular Apple ID on Windows, AltStore Classic's
AltServer can sideload an IPA with the phone connected and Developer Mode
enabled. This signing method has a seven-day expiry. Enter Apple credentials
only in AltServer on your own PC, never in GitHub Actions or this workspace.

The first GitHub build may expose Xcode link errors that Unity's Windows export
cannot detect. Even a successful IPA may fail at game startup because `SimDLL`
and other Windows native dependencies have no iOS implementation yet.
