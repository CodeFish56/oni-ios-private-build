# Original Build 744825 phase transition oracle

## Current retained suites (2026-10-19)

The legacy 164 cases below and a separate `--materials-suite` of 555 cases
are now retained. The material suite covers 199 initial materials, 180 ore
positives, six ordered double-ore positives and eight CellMeltedInfo positives:
247 basic conversions, 276 byproduct-threshold cases, 14 property-0x40 cases
and 18 border cases. Use `Verify-Phase.py --expected-cases 555` for this suite;
the default remains 164. Both pass normal and ASan/UBSan probes linked to the
staged core. The active partial helpers are covered by PartialReference.

Every retained case records the raw debug force-ore byte and explicitly writes
zero in its owned process; fresh native initialization is not reliably zero.
Property cases explicitly restore and observe source/updated properties after
initialization clears them. The checker asserts actual element, mass,
temperature and properties in both original before-grids. Case names alone do
not establish coverage. It also compares raw 4-byte CellMeltedInfo.

## Legacy suite and invocation

Windows x64 / .NET 10. The original SimDLL is bound to SHA-256
`031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`.
No instructions are patched and no game UI or physics frame is run. Each
scenario uses a separate process, initializes cells, copies updated cells to
source, and directly calls `DoStateTransition` (RVA `0x359E0`) or
`DoPartialHeatTransition` (RVA `0x4B7F0`). Initialization explicitly requests
headless=1 and radiation=0 and checks both observed native fields. Visible
cases additionally check force-ore=0 and set centre visibility to 255.

Run from this directory:

```powershell
dotnet build PhaseReference.csproj -c Release
dotnet ../bin/PhaseReference/Release/net10.0/PhaseReference.dll --suite --game-root GAME --capture-dir CAPTURE --output-dir OUTPUT
```

`GAME` must contain the matching original
`OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll`; `CAPTURE` supplies the
captured `elements.bin` and `diseases.bin`. Use `--list-cases` to list scenarios
or replace `--suite` with `--case NAME` to record one scenario.

The 164 scenarios comprise:

- 91 invisible full transitions in the Water/Steam/Ice chain, covering adjacent
  representable threshold temperatures, zero and positive mass, the solid
  0.8-default-mass boundary and temperature limits.
- 21 visible full transitions, including four small-mass freezes that spawn
  Ice ore and clear the cell to Vacuum. Headless does not suppress ore creation
  for a visible cell.
- 46 partial-transition early exits that leave owned output unchanged.
- Six nontrivial partial paths that the candidate guard rejects. These paths
  can still fail or do nothing later in the original; their inclusion does not
  claim that active partial transitions or displacement are implemented.

Reports retain source/updated cell arrays, flow, timers, RNG state and 20 native
event vectors. The candidate `PhaseProbe.cpp` / `Verify-Phase.py` compares nine
source and nine updated owned grids, flow, timers, substance-event bytes and
the defined fields of native 20-byte ore events for the 158 supported cases.
Original ore byte 7 is uninitialized padding: it is explicitly normalized to
zero, matching deterministic candidate padding. Padding is not claimed byte
exact. Auxiliary disease grids and accumulated flow are retained evidence,
not owned-module coverage.

The general candidate module now handles the audited byproducts and other
material chains above. Forced main low-temperature ore, property bits other
than 0x40 and disease remain rejected. Active partial heat transitions use a
separate module; backwall transitions and full-world simulation remain outside
this oracle's scope. Border helper cases do not establish full-frame border
packing. These checks do not establish ARM64 or iPhone behavior.
