# Native simulation core — restricted positive frames

This is a C++17 implementation of the verified Build U59-744825-V data path.
It owns real cell, disease, backwall and CPU render buffers. It can initialize
a headless grid, load a restricted SIMSAVE v15 world and write it back.
It now advances restricted gas/water/phase/solid frames with real flow and event
output. **It cannot yet generate a playable colony.**

## Implemented and locally verified

- Element/disease table parsing; 14-byte InitializeFromCells header and original
  sender's optional six padding bytes; real one-cell simulation border.
- Headless AllocateCells, one full-grid world definition at zero offsets,
  ClearUnoccupiedCells in that fresh allocation, and complete v15 Load.
- Start and PrepareGameData: 12 owned game grids, six owned auxiliary arrays,
  all 29 initial event counts, valid pointer lifetimes and actual processed
  zero-time frame count. Backwalls are empty at Start and filled at Prepare.
- Actual CPU liquid colour/data/material encoding and world-scoped sunlight
  attenuation. Flow remains zero in the verified initialization/zero-time path.
- Queued insulation/strength values, including all-or-nothing invalid batches.
- Oxygen/CO2 pressure and inter-species displacement, low-mass evaporation,
  density/random swaps, frame scheduling/carry, flow textures and substance
  notifications through the C ABI.
- Water flow/postprocessing in the verified pressure/temperature range, liquid
  change events and persistent defined stability-timer bits; timer reset/countdown
  and shared RNG behavior follow the original. Verified headless unstable solids are listed below.
- Foreground cell heat exchange before pressure: fixed source, accumulated output,
  original area/insulation coefficients, binary32 capacities and binary64 exchange
  calculations. Audited whole-cell and partial transitions are listed below; backwall heat remains unsupported.
- Water/O2/CO2 interaction: tick-rotated gas displacement, ordered water diagonal
  exchanges and one shared water/gas postprocess traversal with the original RNG
  sequence. The uint16 update tick wraps and stays outside the v15 save format.
- Real SIMSAVE v15 output, zero offsets and verified saved-option bit 0.
- Seven `OniSim_*` C ABI entry points plus status/error/capability queries.
  Native exceptions are contained inside `noexcept` exports.

Eleven independent Windows-original fixtures pass byte comparisons through
the compiled shared library: each compares 18 arrays at Start and Prepare,
29 event counts and the entire saved byte stream. They include three
256×384 maps, smaller mixed-element maps, radiation on/off, nonzero raw
properties/strength, sunlight blockers/windows, a zero-time frame and a
abyssalite initialization diagnostic. AddressSanitizer and
UndefinedBehaviorSanitizer runs pass the same fixtures and malformed-input
checks. Details and file digests: [verification](../docs/native-sim-verification.json).

Twenty-one original-engine cases compare 86 subsequent frames, including water
fall/columns/basins/corners/low masses/full grids and O2/CO2
stripes, layers, checkerboards, pockets, low masses, variable frame lengths,
zero seconds and 32-cell chunk boundaries. Every frame matches all 18 arrays,
29 event counts, four event byte streams, RNG seed/directions and complete
saved bytes. The original evaporation loss is preserved rather than forced
to conserve mass. Defined timer bits also match; both sanitizers pass. Seven remaining
unsupported-feature tests preserve world/RNG/timers on failure; four former exclusions are now supported. See
[positive-frame scope](../docs/ios-water-timers.md)
and [record](../docs/gas-frame-verification.json).

The separate module probe matches 118 direct original PostProcessCell calls,
including evaporation followed by a random swap back into the cleared cell.
It checks nine owned grids, flow, defined timer bytes, raw substance-event order
and RNG. Disease infestation/error grids remain unimplemented. A further 100
original water-flow calls and 61,440 timer-getter calls match independent module
probes, including sanitizers. See [gas postprocess record](../docs/gas-postprocess-verification.json)
and [water/timer record](../docs/water-timer-verification.json).

These are observed fixture results on Linux x86-64 against Windows x86-64
reference output. They are not proof of every input or ARM64 runtime behavior.
The source was reconstructed using recovered producers, matching PDB types,
original instructions and independently executed original DLL output.
No REA runtime evidence is claimed for these ordinary local tools.

Foreground heat exchange additionally matches 195 original pair/task cases and
eight nonuniform-temperature worlds over 40 complete frames. Both sanitizers
pass. Combined with the preceding cases, this is 29 worlds and 126 frames.
See [heat scope](../docs/ios-heat-exchange.md) and
[heat verification](../docs/heat-exchange-verification.json).

The mixed-fluid stage adds 15 original worlds and 97 complete frames, including
two pure-water temperature regressions near freezing and boiling.
That stage verified 44 worlds / 223 frames. The current stage below adds new
physics and rollback checks.
Independent module probes match 288 water/gas calls and 96 ordered mixed
postprocess cases, including raw duplicate notification order and shared RNG.
The C ABI also reaches uint16 wrap via 65537 actual empty-region updates.
All pass normal and ASan/UBSan builds. See [mixed-fluid scope](../docs/ios-mixed-fluids.md),
[record](../docs/mixed-fluid-verification.json) and [tick record](../docs/tick-verification.json).

## Earlier phase, pressure and solid verification (2026-10-19)

The new three modules match 164 phase-related cases, 456 directional pressure
cases and 191 solid scenarios (353 original calls). Full-frame coverage adds
21 worlds /161 frames, totaling 65 worlds /384 frames. All pass normal and
ASan/UBSan builds, with the same arrays, defined event fields, saves, RNG,
directions, tick and defined timer bits. Ore byte 7 is native uninitialized
padding and is normalized separately. Four new unsupported-path checks roll
back the complete pending frame; saved-option set/clear/rejection/v15 restore
also pass. See [scope](../docs/ios-phase-pressure-solids.md) and
[record](../docs/phase-pressure-solid-verification.json).

## Current transition and pressure completion (2026-10-19)

Two additional modules implement nontrivial partial heat/ice-melt paths and
Water overpressure redistribution. Whole-cell transitions now cover captured
material chains, byproduct ore and the 4-byte CellMeltedInfo stream for property
0x40 (NotifyOnMelt). The corrected pressure trigger is strictly 1.5f * maxMass;
transport uses current mass after displacement * 0.49751242995262146f.
Native false is distinct from unsupported API failure and can retain prior gas
movement. Ordered duplicate notifications and immediate timer resets remain.

The actual staged static library passes 555 general phase cases (199 materials,
180 ore positives, six double-ore sequences, eight melt-event positives),
434 partial cases (22 native-false cases with effects), and 197 redistribution
cases. Another 30 worlds /157 complete frames bring coverage to **95 /541**.
Normal and ASan/UBSan outputs match. The four negative physical inputs were
corrected to set diseaseIdx=255 and insulation in the actual cell byte; their
specific error reasons and complete rollback are now asserted. Integrated solid
visits validate once and use the outer frame transaction instead of copying the
whole grid for every visit. A dense 40x40 sand regression matches 12 frames.
See [scope](../docs/ios-transition-completion.md) and
[record](../docs/transition-completion-verification.json).

## Build locally

```sh
cmake -S native/sim -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native -j4
python3 native/sim/Verify-Bridge.py \
  --library build-native/libOniSimCoreShared.so \
  --fixtures /path/to/original-fixtures \
  --elements /path/to/elements.bin --diseases /path/to/diseases.bin
```

For retained direct postprocess cases, build and run the separate module probe:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -ffp-contract=off \
  -Inative/sim -Inative/abi native/tests/GasPostProbe.cpp \
  build-native/libOniSimCore.a -o build-native/gas-post-probe
python3 native/sim/Verify-GasPost.py \
  --probe build-native/gas-post-probe --fixtures /path/to/direct-cases \
  --elements /path/to/elements.bin --output build-native/post-output \
  --result build-native/post-check.json
```

`reference/WorldgenReference.csproj` builds the isolated .NET 10 Windows oracle.
It verifies the original DLL SHA before loading it, runs in its own process,
and does not launch or modify the game. Raw fixtures and the game's element
tables stay local. The private repository includes only test-tool source and
digest reports, not personal phone/signing logs.

## iOS integration and remaining work

The workflow copies sixteen production `.cpp` modules and their headers into
the Xcode export, adds them to UnityFramework and redirects seven audited
`Sim.SIM_*` wrappers to this static C ABI. See [link details](../docs/ios-sim-link.md).
No new 1.66 GB archive upload is needed.

Current capabilities are **3735551**, including audited partial transitions,
material whole-cell conversions and Water pressure redistribution.
The game wrapper requires world settlement, game simulation and game render
capabilities (**458752**); all remain absent. It raises a managed capability
error before marshaling the callback. World-generation failure handling
displays the existing error dialog. Unsupported physics/messages fail
explicitly and do not advance a pretend simulation.

Remaining acceptance gates, in order:

1. Extend remaining liquid flow and partial-melt targets, sublimation, backwall
   heat, disease and radiation, then verify complete 500-step settlement.
   General phase conversion does not establish later flow for every material.
   Partial helpers still reject border mutation. If displaced gas enters the
   top border while unstable solids remain, the next frame rejects that domain.
   Large-colony memory and performance remain unmeasured.
2. Generate and check the IL2CPP reverse P/Invoke callback. The recovery script
   now adds the iOS `MonoPInvokeCallback` attribute; the current archive was
   exported earlier and still lacks that callback entry.
3. Complete ongoing gameplay messages, native components and remaining SimDLL
   consumers (conduit, profiling/system information as actually needed), with
   owned event queues and full render-data synchronization.
4. Verify an actual macOS ARM64 build and iPhone initialize/settle/save/reload
   cycle, then memory peaks and sustained touch gameplay. Local probes do not
   establish that the previously measured 3.37 GiB footprint has improved.

The capability check must only open when these game prerequisites have actual
implementations and validation. A fresh IPA at this stage will still not enter
a colony. The workflow remains manual; no cloud build is triggered by this work.
