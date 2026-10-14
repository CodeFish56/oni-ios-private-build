# GitHub Actions iOS build experiment

This folder stages the locally generated Build 744825 Xcode export for a **private**
GitHub repository. The workflow compiles an **unsigned** IPA on a hosted macOS
runner; it does not make Oxygen Not Included playable or sign the app.

## Current staged iOS export (2026-10-14)

The workflow reads `package-resource/manifest.json` and its four Git LFS parts.
This archive includes the Json.NET AOT startup repair and expanded silent iOS
audio handling. The reported startup failures were a null audio path passed
to `StringFormatter.Combine` while creating underwater audio events, and a
direct FMOD initialization in `AudioMixer.Start` called by the main menu.
The recovered iOS scripts now skip native FMOD access at the menu, mixer,
bank enumeration and playback boundaries. Audio-sheet event identities,
speech metadata and mouth animations remain available so resource and
element initialization can finish. Desktop and Editor audio paths remain.
Unity's local iOS export succeeded with zero build errors on 2026-10-14.
The archive SHA-256 is
`8c3cba4d4abc9c69fac31d7ae0c755f6f2609b694f119f2bea612f954d666c5a`.
Build #14 compiled this archive with the native audio boundary repair. The
phone reaches asset initialization and the error dialog; menu startup and
gameplay are not yet verified.

## Native FMOD boundary verification (2026-10-14)

A follow-up audit of the 64 iOS build assemblies found 178 external call sites
that can reach FMOD native imports through static calls. This includes slider
and scroll sound parameter updates after the silent game wrapper returned an
empty event instance. These counts describe compiled call candidates; they do
not prove that every site executes on the phone.

The workflow now runs `scripts/disable_ios_fmod_native.py` after extracting the
archive. Its plan matches all 585 FMOD native imports to the two generated C++
translation units and binds them by source SHA-256. The native wrappers return
`ERR_UNINITIALIZED`, or `false` for validity queries, initialize output values,
and preserve reference inputs. Managed FMOD types and non-audio game behavior
remain. The build stops if the source identity changes or any FMOD native
library resolver remains. No replacement audio engine is implemented.

Local verification found zero remaining FMOD native resolvers, and rerunning
the patch made no changes. Both patched C++ files compiled with Unity's headers
on the local Linux host; a linked probe executed representative generated
wrappers without an FMOD library and verified error results, invalid handles,
output values and null output pointers. This checks the native boundary; the
updated workflow passed macOS arm64 compilation in Build #14. Its verification
artifact confirms all 585 native wrappers were replaced. Device logs expose a
separate managed-component initialization failure, described below.
The workflow uploads `oni-fmod-native-boundary-check` alongside the IPA.

## Managed startup repair and landscape display (2026-10-14)

Build #14 reached animation-table registration (2,152 entries) and substance
initialization (202 elements) on the iPhone. A scene's `StudioListener.OnEnable`
still called `RuntimeManager.Initialize`. The recovered FMOD platform settings
could not support that path, producing a null reference and the error dialog.
Returning an error from native imports alone did not disable managed audio
components. A separate `SteamUGCService.Initialize` null reference occurred
because `Global.Update` treated the initialized local platform as Steam.

After the native audio patch, `scripts/patch_ios_startup.py` now replaces 139
methods across RuntimeManager and seven audio component types. Component
lifecycle callbacks and playback methods remain silent; audio queries return
invalid handles or inactive state. Bank-loading completion queries report no
pending work so callers can continue. FMOD initialization reports
`ERR_UNINITIALIZED`; it does not create a runtime object or access platform
settings. Serialized component types, constructors and non-audio game
initialization remain. The script also removes Steam workshop/achievement
registration from device `Global.Update` and skips the developer-tool reflection
scan. The plan verifies source, method-body and output hashes and rejects a
changed export before writing any files.

`scripts/configure_ios_landscape.py` restricts iPhone and iPad Info.plist entries
and Unity's runtime autorotation mask to landscape left/right. The local Unity
recovery/build scripts enforce the same orientations for future exports.
The workflow uploads `oni-startup-compatibility-check` with both reports. Only
the `ONI-unsigned-ipa` artifact contains the app to sign and install.

Local verification extracted the affected files from the actual LFS archive
and passed the workflow patch sequence, repeated startup/orientation checks,
and rejection of changed source without writes. All three changed C++ files
compiled with the exported Unity headers on Linux. A linked probe executed
the real generated listener, runtime, emitter and reflection functions without
Unity/FMOD initialization and checked invalid handles, completion state and
output parameters. This repair still needs a new macOS build and phone test.

Earlier foreground exits also have a distinct kernel `killing_highwater_process`
record at 3,458,227 KB (about 3.3 GiB). The latest user report stays on the error
dialog without exiting. These startup patches do not establish that memory
use is fixed; resource memory and menu stability remain to be measured.

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
