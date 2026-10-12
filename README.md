# GitHub Actions iOS build experiment

This folder stages the locally generated Build 744825 Xcode export for a **private**
GitHub repository. The workflow compiles an **unsigned** IPA on a hosted macOS
runner; it does not make Oxygen Not Included playable or sign the app.

## Current staged iOS export (2026-10-12)

The workflow reads `package-resource/manifest.json` and its four Git LFS parts.
This archive contains the Unity Xcode export with the iOS Json.NET AOT startup
repair: before game initialization, Json.NET 7 is switched to its late-bound
reflection path so mod-state serialization does not call `DynamicMethod`.
Unity's local iOS export succeeded with zero build errors. The archive SHA-256
is `de22ad7acadb7facc8737b6a0838dc479d7263ec4f5e83112e5657addf34fce6`.
The next Actions run has not yet been performed or tested on the phone.

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

## Private sideload diagnostic

The full IPA is valid but Windows AltServer 1.8 reports `bad allocation` while
loading it. A deliberately incomplete 270 MB IPA, with large Unity streaming
resources and FMOD banks removed, is available only in the
[private diagnostic release](https://github.com/CodeFish56/oni-ios-private-build/releases/tag/oni-boot-diagnostic-20261009).
It is **not playable**. Its purpose is to isolate Apple ID signing and device
installation from the full package size. AltServer currently reports WinHTTP
12152 for the small IPA, while Sideloadly 0.70.1 receives an Apple GSA HTTP 503
during authentication. On-device AltStore import and refresh also fail with HTTP
503 before signing. Direct installation with 3uTools reaches iOS but the phone
rejects the unsigned IPA as unverifiable. Both generated IPA files have no
`_CodeSignature` directory or `embedded.mobileprovision`; the next installation
test needs a device-signed output IPA.

## Current iPhone diagnostic

The iOS-only offline platform adapter was exported from Unity and compiled by
[this build](https://github.com/CodeFish56/oni-ios-private-build/actions/runs/38015793088)
on 2026-10-10. The artifact downloaded through `download_artifact_parallel.py`
passed GitHub's SHA-256 check. A matching local development certificate and
provisioning profile signed the IPA offline; no Apple credentials or signing
material were uploaded. `repack_ipa.py` recompressed the signed ZIP without
changing any file CRCs. USB installation and process launch succeeded on an
iPhone 15 Pro, proving that the Steam startup exit was bypassed.

The app now reaches the game's error dialog. Device logs show missing native
`fmodstudio` audio code, a null reference in `Assets.LoadAnims`, and an async
personality loader lookup error after asset initialization stopped. This is a
boot diagnostic, not a playable port. A newer Unity export with iOS-only
checkpoints is being prepared to isolate the asset failure.
