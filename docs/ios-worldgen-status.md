# iOS new-game checkpoint — 2026-10-15

## Observed on the iPhone

- The user reaches the main menu and starts a new game.
- In the recorded retry, the menu log appears at 09:59:35; story-trait
  processing follows at 10:00:06–10:00:08.
- At 10:00:08.737, Unity reports `DllNotFoundException: SimDLL` from
  `Sim.SIM_Initialize` → `WorldGenSimUtil.DoSettleSim`.
- The loading animation continues, but no world-generation completion is
  recorded. The simulation initialization failure is the confirmed blocker
  for this retry. The earlier attempt's exact failure is not established.
- DVT reports a physical footprint of 3,615,542,824 bytes, approximately
  3.37 GiB, during the stalled retry. Memory is a separate unresolved risk.
- A retrieved 09:52 exit report is a background termination watchdog in
  Metal texture cleanup. It is not proof that this foreground retry ran out
  of memory. The retrieved 09:06 jetsam report is from an earlier session
  and does not explain this retry.

Raw device logs, memory-region paths, crash reports and signing material remain
local. These findings are from USB tools and local code inspection, not a new
REA evidence record.

## Why the loading screen never completes

The native initialization exception escapes `Cluster.ThreadMain`, which calls
`BeginGeneration` without a general failure boundary. The expected error callback
and failure status are not set. Rendering can keep running after this worker
has stopped.

There are two additional failure-path problems: `WorldGen.RenderOffline`
does not propagate a false settle result, and `OfflineWorldGen.DoExitFlow`
calls `Sim.Shutdown` before displaying errors. If the library was unavailable
at initialization, that cleanup call can throw another missing-library error.
`OfflineWorldGen.Update` also checked a null progress string before queued
errors, preventing early failures from being displayed.

## Changes prepared

The source recovery script and a SHA-bound patch for the current Xcode export
cover those boundaries. Missing-library and missing-entry exceptions during
simulator initialization are reported via the existing error callback and
return false. `RenderOffline` stops rather than reporting completion. The
UI checks queued errors even before progress text is available; cleanup ignores
only missing-library/entry exceptions, allowing the dialog to appear. The
existing confirm action returns to the menu. Other exceptions are rethrown.

This is failure handling. It does not provide a simulation backend, skip
settlement, produce a valid map or validate gameplay. Only landscape left/right
remain enabled in the build workflow.

## Local verification

- Replay of the captured Unity log returns `blocked_by_missing_simdll` with
  `menu_reached=true`, `worldgen_completed=false`, and one matching exception.
- All three affected generated C++ translation units compile using the
  exported Unity headers on Linux/GCC.
- The production exception helper passes initialization success, missing
  library, missing entry, failure cleanup and unrelated-exception propagation
  cases. This is a focused boundary test, not an end-to-end worldgen test.
- Patch order, repeatability and changed-source rejection are checked against
  the actual four-part LFS archive used by Actions.
- macOS compilation and the updated dialog/return behavior remain unverified
  for this patch. No cloud build was triggered during this investigation.

## Simulation work required for a map

| Area | Present state | Next acceptance condition |
| --- | --- | --- |
| Native initialization and ABI | Declarations and data-layout checks; no iOS game backend | Link an ARM64 implementation and initialize/shutdown safely |
| Worldgen cell initialization | Managed producer recovered; no complete native consumer | Decode elements, diseases, cells and backwalls from the worldgen messages |
| Start and prepared game data | Native layout mapped; no integrated producer | Return valid, owned arrays and correct synchronization, never null placeholder data |
| World settlement | Single-gas pressure/postprocessing subsets only | Validate mixed solids, gases, liquids, heat, phase changes and disease during the 500 settle steps |
| Save/load | Limited version-15 decoder; no complete game round trip | Write a valid generated-world simulation save and load it back |
| Continuous gameplay | Full physics, components and rendering bridge incomplete | Run sustained frames, buildings, creatures and save/reload on device |
| Memory and input | High footprint measured; basic menu taps work | Reduce measured peaks and validate map controls and long-running stability |

An empty implementation of the exported functions would only conceal the
missing backend. The next functional work belongs at the headless worldgen
message/array boundary, followed by settlement and save round-trip tests against
the original Windows reference. Device builds should follow meaningful local
acceptance checks instead of each speculative edit.
