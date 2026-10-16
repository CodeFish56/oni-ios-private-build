# Original Build 744825 direct liquid oracle

This Windows x64 project loads the SHA-256-checked **original installed** SimDLL,
sets up 5 x 5 game cells (7 x 7 native cells including border), and calls original
`UpdateLiquid` at RVA `0x498B0` once at native center (3,3). It does not open the
game, submit a physics frame, alter DLL instructions, or start an Actions build.

Before the direct call it invokes original `CellSOA.CopyFrom` at RVA `0x6390` to
copy updated to fixed source. This explicitly recreates the mid-frame precondition
of UpdateData; the direct oracle must not assume Start already performed it.

The 100 cases cover:

- Center masses .005, .01, .02, 1, 500, 1000, 2000, 4000.
- Same Water below, including zero-mass Water, at 0 / 500 / 1000 / 2000.
- Water/Vacuum/Unobtanium floor and lateral block configurations.
- Upper Water and high pressure upward flow.
- Open and closed falling corners, lateral falling event fallback.
- Neighbor temperatures 299.9 / 300.1 K and a dense Water cross.

Initialization is headless, radiation disabled, no properties, no disease, empty
Vacuum backwall. Some diagnostic cases deliberately contain interior Unobtanium
solids and masses above 1000; those isolated-flow cases do **not** imply the full
frame implementation supports their unimplemented pressure-break paths.
The oracle now checks actual native headless/radiation flags, and the verifier
requires those checks in the report. The earlier writer had these two input
booleans reversed; all 100 reference cases have been recaptured correctly.

## Run locally

From the private build repository root in PowerShell:

```powershell
dotnet build native/reference/LiquidReference/LiquidReference.csproj -c Release
dotnet native/reference/bin/LiquidReference/Release/net10.0/LiquidReference.dll `
  --game-root '<Build744825 game directory>' `
  --capture-dir '<captured tables directory>' `
  --suite --output-dir porting/analysis/liquid-direct-reference-744825
```

Use `--list-cases`, or `--case isolated-500` instead of `--suite`, for a single
original process. Each suite case has a 20-second deadline and is a fresh child.

## Evidence format

Each directory records the exact initialization wire and its case cells (X/Y are
native simulation coordinates), report, logs, and before/after binaries:

- 11 fixed source arrays and 11 updated arrays.
- Four-component native flow, timers and accumulatedFlow.
- All 20 native SimEvents vectors, including empty streams.
- RNG seed and both iteration/displacement directions in report.
- Snapshot byte lengths and SHA-256 values.

Direct ChangeSubstance records use placeholder old/new indices `0xFFFF` until
later event consolidation; this tool intentionally does not run consolidation.
Notifications also set timer low five bits to 31, preserving high bits. The
center's diagnostic timer starts `0xA2`, native flow `[1.25,2.5,3.75,5]`; outputs
therefore exercise preservation and additions, not only all-zero flow inputs.

This tool captures original behavior. A separate candidate checker must compare
actual production C++ output before claiming equivalence. It does not validate
PostProcess, heat exchange, disease, mixed liquids, iOS ABI, or playable games.

## Candidate module comparison

`porting/NativeSim/WaterFlowProbe.cpp` links the actual `OniSimCore` library and
calls `UpdateWaterCellV744825`, passing the retained timer grid. Compile after
the library, then run through WSL from the workspace root:

```powershell
wsl.exe g++ -std=c++17 -O2 -Wall -Wextra -Werror -ffp-contract=off `
  porting/NativeSim/WaterFlowProbe.cpp porting/analysis/native-sim-build/libOniSimCore.a `
  -pthread -o porting/analysis/native-sim-build/WaterFlowProbe
wsl.exe python3 porting/NativeSim/Verify-WaterFlow.py `
  --probe porting/analysis/native-sim-build/WaterFlowProbe `
  --fixtures porting/analysis/liquid-direct-reference-744825 `
  --elements /mnt/c/Users/DELL/Documents/Klei/OxygenNotIncluded/porting_capture/elements.bin `
  --output porting/analysis/water-flow-candidate `
  --result porting/analysis/water-flow-module-check.json
```

The initial 100-case module comparison passed for 9 source + 9 updated owned
grids, native flow, timers, and substance event order. The checker does not
claim coverage of unmodeled disease infestation/error or accumulatedFlow arrays.
