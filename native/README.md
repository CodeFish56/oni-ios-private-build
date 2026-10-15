# Native simulation core — first integration stage

This is a C++17 implementation of the verified Build U59-744825-V data path.
It owns real cell, disease, backwall and CPU render buffers. It can initialize
a headless grid, load a restricted SIMSAVE v15 world and write it back.
**It cannot yet generate a playable colony or advance positive-time physics.**

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
- Real SIMSAVE v15 output, with verified zero header option and zero offsets.
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

These are observed fixture results on Linux x86-64 against Windows x86-64
reference output. They are not proof of every input or ARM64 runtime behavior.
The source was reconstructed using recovered producers, matching PDB types,
original instructions and independently executed original DLL output.
No REA runtime evidence is claimed for these ordinary local tools.

## Build locally

```sh
cmake -S native/sim -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native -j4
python3 native/sim/Verify-Bridge.py \
  --library build-native/libOniSimCoreShared.so \
  --fixtures /path/to/original-fixtures \
  --elements /path/to/elements.bin --diseases /path/to/diseases.bin
```

`reference/WorldgenReference.csproj` builds the isolated .NET 10 Windows oracle.
It verifies the original DLL SHA before loading it, runs in its own process,
and does not launch or modify the game. Raw fixtures and the game's element
tables stay local. The private repository includes only test-tool source and
digest reports, not personal phone/signing logs.

## iOS integration and remaining work

The workflow copies eight production `.cpp` modules and their headers into
the Xcode export, adds them to UnityFramework and redirects seven audited
`Sim.SIM_*` wrappers to this static C ABI. See [link details](../docs/ios-sim-link.md).
No new 1.66 GB archive upload is needed.

Current capabilities are **63**, covering the limited data operations above.
The game wrapper requires world settlement, game simulation and game render
capabilities (**458752**); all remain absent. It raises a managed capability
error before marshaling the callback. World-generation failure handling
displays the existing error dialog. Unsupported physics/messages fail
explicitly and do not advance a pretend simulation.

Remaining acceptance gates, in order:

1. Implement mixed solid/liquid/gas time steps, heat exchange, phase changes,
   disease and events. Validate one frame, then the complete 500-step world
   settlement against controlled original runs, including conservation checks.
   Existing gas-pressure/postprocessing modules are linked but not dispatched
   from positive-time frames.
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
