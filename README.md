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

## Verified build

[Build #2](https://github.com/CodeFish56/oni-ios-private-build/actions/runs/37874520403)
on 2026-10-09 completed with Xcode 16.4: the arm64 app compiled and linked,
and `ONI-unsigned.ipa` was packaged at about 1.3 GB. The uploaded artifact is
`ONI-unsigned-ipa` (1,356,848,441 bytes, artifact ZIP SHA-256
`189b17f377bff41ea78dd098433415e906d2343e3ffde3c95f17047d5b36e496`).
GitHub retains the artifact for one day. On the authenticated Windows computer,
`download_artifact.py ARTIFACT_ID OUTPUT_ZIP` downloads it through the official
GitHub API and checks its digest. This file is still unsigned and has not been
installed or launched on an iPhone. `SimDLL` and other Windows native
dependencies still need iOS replacements for gameplay.
