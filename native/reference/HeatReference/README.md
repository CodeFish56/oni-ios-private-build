# Original Build 744825 foreground heat oracle

Windows x64 / .NET 10. The DLL is bound to SHA-256
`031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`.
No instructions are patched. A separate child process initializes cells,
copies updated cells to source, then directly calls `UpdateTemperature`
(RVA `0x36470`) or one temperature task (`0x458D0`).
The tool checks the original SimData flags after initialization: headless=1,
radiation=0. The candidate verifier requires those observed fields in each report.

```powershell
dotnet build HeatReference.csproj -c Release
dotnet ../bin/HeatReference/Release/net10.0/HeatReference.dll --suite --game-root GAME --capture-dir CAPTURE --output-dir OUTPUT
```

195 cases cover O2/CO2/water pairs, zero and very small mass, accumulated
updated temperatures, the 1 K task threshold, insulation values, a four-cell
temperature cross, vacuum and an insulated material. Source/updated cell
arrays, flow, timers, RNG flags and all 20 native event vectors are retained.
The candidate probe compares its nine owned cell arrays, flow/timers and
substance notifications; unsupported disease auxiliary grids are not claimed.

`../HeatFrameFixtures.py` separately records eight nonuniform-temperature
worlds through real positive frame messages. Original process affinity is
pinned to one available CPU by WorldgenReference for deterministic task order.
Gas regions avoid the cold top border because phase changes are not implemented.
This does not establish multithreaded, ARM64, iPhone or general-world behavior.
