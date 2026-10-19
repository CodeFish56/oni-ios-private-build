# Build 744825 partial heat and melting reference

`PartialReference.cs.txt` calls the owned original `SimDLL.dll` directly:

| Entry | RVA | Scope |
| --- | --- | --- |
| `DoPartialHeatTransition` | `0x4B7F0` | Convert a 5 kg portion using a hot neighbour, gas/liquid displacement and headless placement |
| `DoPartialMelt` | `0x4BFF0` | Melt a 5 kg portion of ice using gas heat capacity, displacing the current gas |

The tool checks the exact Build U59-744825-V DLL SHA-256 before loading it. It starts a fresh native simulator for each case, initializes captured element/disease tables, verifies headless=1 and radiation=0, copies updated cells to fixed source with the original `CopyFrom` helper, and invokes exactly one partial helper. No game UI or complete physics frame runs.

The original debug `forceOre` byte is not reliably initialized by the constructor in a fresh process. Every case records its raw value, explicitly writes zero at `SimData+0xB8`, and records that override. This is a controlled reference condition, not a claim about its default value.

## Matrix and observations

The 434 retained cases cover heat thresholds, capacity failures, the 5 kg threshold, positive remaining masses either side of the 0.01 kg displacement threshold, four neighbour orientations, tick values 0/1/2/3/65535, cardinal/diagonal gas targets, blocked displacement, property 0x40, and the melt-specific 0x48 property guard. Hot ice fixtures deliberately retain 500 K in the owned grids to test the helper in isolation; they do not claim that such ice survives a complete thermal update.

Native false and unsupported API failure have different meanings. A failed native liquid displacement may already have moved adjacent gas; its cells, notifications and timer changes must still commit. Unsupported input must leave the entire caller-owned world, output buffers, RNG and native-result parameter unchanged.

Two original details are preserved:

- Steam prefers the cell above the transforming cold cell, regardless of which direction contains the hot neighbour. It does not prefer the cell above the hot neighbour.
- The successful headless partial-melt fallback writes the cooled gas temperature into the newly cleared cell, then immediately overwrites it with the target water temperature. The displaced gas retains its original temperature.

Both helpers leave the fixed source, flow, RNG seed/directions/calls and tick unchanged. In the supported disease-free headless domain, the only emitted event stream is ordered `SubstanceChangeInfo`, including duplicates; 19 other captured native event vectors remain empty.

## Reproduce

```powershell
dotnet build porting/SimReference/PartialReference/PartialReference.csproj -c Release
dotnet porting/SimReference/bin/PartialReference/Release/net10.0/PartialReference.dll --suite --game-root F:/Softwares/Steam/steamapps/common/OxygenNotIncluded --capture-dir C:/Users/DELL/Documents/Klei/OxygenNotIncluded/porting_capture --output-dir porting/analysis/partial-direct-reference-744825
tar.exe -cf porting/analysis/partial-direct-fixtures.tar -C porting/analysis/partial-direct-reference-744825 .
wsl.exe bash porting/analysis/verify-partial-module.sh
```

The suite uses at most four independent native processes and resumes only complete cases with an identical serialized case definition and the explicit force-ore override. The checker validates every retained binary against its report hash, compares nine source and nine updated grids, flow, timers, ordered event bytes, native result, and unchanged original diagnostics. It also checks the original's unowned infestation/error, accumulated-flow and visibility storage stayed unchanged. Every candidate case exercises a separate unsupported-border failure after provisional heat/mass edits and verifies atomic rejection. Normal and ASan/UBSan builds use strict float order and disable fused operations.

The reference covers Water/Steam/Ice with oxygen/CO2/vacuum and solid obstacles, properties 0 or 0x40 on active paths, no disease, radiation or backwalls, and headless force-ore=0. It is a module-level x86_64 comparison; it does not establish complete `UpdateData`, ARM64 or iPhone behavior.
