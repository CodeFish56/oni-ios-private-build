# GitHub Actions iOS build experiment

This folder stages the locally generated Build 744825 Xcode export for a **private**
GitHub repository. The workflow compiles an **unsigned** IPA on a hosted macOS
runner; it does not make Oxygen Not Included playable or sign the app.

## Latest device result (2026-10-15)

The user can now enter the main menu. A USB-recorded new-game attempt reaches
world generation, then throws `DllNotFoundException: SimDLL` at
`Sim.SIM_Initialize`, called by `ProcGenGame.WorldGenSimUtil.DoSettleSim`.
The generation worker stops while the loading animation continues. Waiting
longer or clearing other apps cannot supply the missing simulation library.
The first native simulation data core is now implemented and wired into the
build workflow. Restricted gas or water positive frames now work locally, including
inter-species gas displacement, water flow and stability-timer state.
General colony physics is incomplete.

## Phase transitions, pressure damage and solids (2026-10-19)

Whole-cell Water/Steam/Ice transitions, visibility-dependent frozen ore,
ordered liquid pressure damage and headless solid movement now run through
the native core. Saved option bit 0 selects original basic/diagonal falling
sand behavior and is preserved by v15 save/load.

811 independent module cases and 21 new worlds /161 complete frames pass
normal and ASan/UBSan checks. The cumulative regression is 65 worlds /384
frames, plus 11 initialization cases and 65537 actual tick updates. Original
ore padding byte 7 is excluded; all defined event fields are compared.

Fourteen production modules are linked, capabilities are 65535, and the full
game requirement 458752 remains unavailable. Nontrivial partial melting,
byproduct ore, other liquids, disease/radiation/backwall heat and complete
settlement/gameplay remain unfinished. No cloud build is needed solely to
try entering a colony. No Actions run was triggered.
See [current scope](docs/ios-phase-pressure-solids.md) and
[verification](docs/phase-pressure-solid-verification.json).

The sections below record the earlier stages and their original test domains.

## Mixed water/gas physics (2026-10-16)

The core now handles Water with Oxygen/CO2, including gas displacement in tick
order, diagonal water swaps and a single ordered fluid postprocess traversal.
The original shared RNG order is retained across water and gas visits.
288 original water/gas module calls and 96 mixed postprocess cases match;
15 additional worlds match all 97 complete frames, including two pure-water
temperature regressions. The cumulative regression
is 44 worlds /223 frames plus 11 initialization cases. ASan/UBSan pass.

The uint16 tick increments per internal update, wraps at 65535 and is absent
from v15 saves. The candidate reaches wrap with 65537 real updates and has a
read-only diagnostic API; the existing diagnostics struct remains 24 bytes.
Eleven unsupported inputs preserve world/RNG/timers/tick on failure.

Capabilities are now 8191. Full-game requirement 458752 is still unavailable.
Mixed fluids remain restricted to 275..360 K, Water<=maxMass after flow,
empty backwalls and no disease/radiation. Phase changes, other materials,
world settlement, gameplay and ARM64 device validation remain incomplete.
No Actions run was triggered. See [scope](docs/ios-mixed-fluids.md) and
[verification](docs/mixed-fluid-verification.json).

## Foreground heat exchange (2026-10-16)

The core now runs original-order heat exchange before pressure. It preserves
insulation/area coefficients, the 1 K task threshold, float/double rounding,
accumulated temperature deltas and the original clamps. 195 direct original
pair/task cases match byte for byte; eight additional worlds match 40 full
frames, including all 18 arrays, 29 event counts, events, RNG and saves.
The combined positive-frame regression is 29 worlds / 126 frames.
AddressSanitizer and UndefinedBehaviorSanitizer pass the same cases.

Eleven modules are wired into UnityFramework. That stage added foreground
heat exchange; the subsequent mixed-fluid stage above raises capabilities to
8191. Phase changes, backwall heat, disease and complete settlement remain incomplete.
No Actions run was triggered. See [scope](docs/ios-heat-exchange.md) and
[verification](docs/heat-exchange-verification.json).

## Water and stability timers (2026-10-16)

The native C ABI now advances restricted water/vacuum/Unobtanium worlds as well
as the earlier O2/CO2 gas worlds. Twenty-one original-engine cases match all
86 subsequent frames: 18 arrays, 29 event counts, event records, RNG state,
full saves and the defined five timer bits. Eleven initialization/load
regressions pass. Ten unsupported-feature inputs roll back world/RNG/timers.

Independent module probes match 100 original UpdateLiquid cases, 118 gas
postprocess cases (now including timer bytes), and 61,440 timer getter calls.
AddressSanitizer and UndefinedBehaviorSanitizer pass the same local checks.

Backwall heat exchange, phase transitions, unstable
solid movement and world settlement are incomplete. Water mass after flow
must stay within the supported pressure range. The game capability gate is
still closed; another IPA cannot yet create a playable colony.
[Scope and evidence](docs/ios-water-timers.md). No Actions run was triggered.

## Native simulation integration (2026-10-15)

`native/sim` now contains the real C++ data core: headless initialization,
restricted v15 load/save, owned cell and CPU texture arrays, sunlight and
queued zero-time cell-value updates. Eleven independently captured original
DLL cases match all 18 arrays at Start and Prepare and the complete save bytes.
Address/undefined-behavior sanitizer runs pass the same cases and error checks.

The workflow adds eleven production modules to UnityFramework and redirects
seven audited Sim imports to the static C ABI. The actual generated wrapper
has been compiled and linked against the core locally; its capability error
reaches the worldgen handler before callback marshaling. The entire patch
sequence also passes against the real LFS archive.

**Creating/loading a colony remains disabled.** Mixed physics, world settlement,
ongoing gameplay/rendering and the exported AOT callback are incomplete.
macOS ARM64 compilation and device execution of this new core remain unverified.
There is no reason to spend a cloud build solely to try to enter a colony at
this stage. See [core scope and next gates](native/README.md) and
[iOS link details](docs/ios-sim-link.md). No Actions run was triggered.

`scripts/patch_ios_worldgen_failure.py` adds failure handling to this export:
report the native initialization failure to the existing dialog, stop further
generation, check queued errors before the first progress string, and allow
the error dialog to appear when shutdown also encounters the missing library.
The linked core's capability exception is handled by the same initialization
boundary. Unrelated exceptions propagate. Successful native initialization continues
through the original world-generation code. This patch does not synthesize
world data or implement SimDLL.

The affected generated C++ compiles locally, the actual exception helper passes
focused tests, and the patch sequence passes against the archived source.
Its new dialog/return flow still needs an iPhone test. No Actions run is needed
merely to confirm the already-observed missing backend.

Current measured process physical footprint during the stalled attempt is
3,615,542,824 bytes (about 3.37 GiB). This indicates substantial memory use;
it does not establish the cause of every reported exit. See
[current verification and remaining work](docs/ios-worldgen-status.md).

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
Build #14 compiled this archive with the native audio boundary repair.
The follow-up managed startup repair reached the main menu on the iPhone
on 2026-10-15; new-game generation stops at the missing SimDLL backend.

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
output parameters. The user's subsequent IPA reaches the main menu; the
next confirmed blocker is new-game simulation initialization, described above.

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
