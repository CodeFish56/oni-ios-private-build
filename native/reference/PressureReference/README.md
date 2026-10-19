# Original 744825 pressure-break oracle

This Windows x64 tool loads the user's matching Steam `SimDLL.dll` and calls `DoPressureBreak` directly at RVA `0x4C4E0`. It does not start the game UI, submit a physics frame, sign/install an IPA or invoke GitHub Actions.

Required DLL SHA256: `031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`. The oracle also checks captured table hashes and asserts the native initialized flags are headless `1`, radiation `0`.

## Run

```powershell
dotnet build porting/SimReference/PressureReference/PressureReference.csproj -c Release
dotnet porting/SimReference/bin/PressureReference/Release/net10.0/PressureReference.dll --game-root F:/Softwares/Steam/steamapps/common/OxygenNotIncluded --capture-dir C:/Users/DELL/Documents/Klei/OxygenNotIncluded/porting_capture --output-dir porting/analysis/pressure-break-direct-reference-744825 --suite
```

Use `--list-cases` to list names or `--case NAME` instead of `--suite` for a single scenario. Each case runs in its own bounded child process and records before/after arrays, raw event vectors, seed/directions and the native boolean result.

## Scope and comparison

The retained suite contains **456 cases**: four directions, zero through three walls, six solid materials, strength byte and mass scaling, protected-cell flags, resistance thresholds, and downstream liquid pressure. Original output contains 316 damage records across 296 successful cases.

The helper leaves all grids, flow, timers, accumulated flow and RNG unchanged. It only appends eight-byte `WorldDamageInfo` records (target game-cell index, original liquid source game-cell index), preserving near-to-far order and duplicates. Managed game code later applies actual damage/destruction.

Candidate comparison uses `NativeSim/PressureBreakProbe.cpp` and `NativeSim/Verify-PressureBreak.py`. Local ordinary and ASan/UBSan runs are reproduced by `bash porting/analysis/verify-pressure-break.sh` with the retained fixture tar. The checker verifies the exact boolean, every damage-event byte, and all unchanged state.

These direct cases do not validate full-frame scheduling/event delivery, later high-pressure liquid displacement, general material physics, invalid native inputs, ARM64 float identity or iPhone gameplay. Production compatibility gates must remain in place for unsupported paths. Further evidence and instruction addresses are in `porting/analysis/PressureBreak-744825.md`.
