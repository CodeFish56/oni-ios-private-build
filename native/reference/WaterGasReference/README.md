# Original water/gas interaction oracle

This Windows x64 / .NET 10 tool directly calls original Build 744825
`UpdateLiquid` (RVA `0x498B0`) after initializing and copying updated to source.
It requires the same SHA-bound original DLL and captured tables as LiquidReference.
No original instruction is patched and no complete frame is submitted.
The actual native flags must be headless=1/radiation=0 after initialization.

288 cases have been recorded: O2/CO2 below water, side displacement with
tick values 0..3 and 65535, gas merge, blocked destinations, zero/tiny gas mass,
water masses .005 through 4000, temperature mixes, diagonal candidates and
water/diagonal gas exchanges. Tick is explicitly controlled
in this isolated diagnostic process. Inputs, source/updated arrays, events,
RNG and timers are retained locally.

```powershell
dotnet build WaterGasReference.csproj -c Release
dotnet bin/Release/net10.0/WaterGasReference.dll --suite --game-root GAME --capture-dir CAPTURE --output-dir OUTPUT
```

Verify-WaterGas.py now compares all 288 cases with the real C++ flow module.
Original reports are SHA-bound by suite.json and all retained array SHA values
are checked. Direct tests compare 9 source and 9 updated owned grids, flow,
timer bytes and exact duplicate event order. They do not establish arbitrary
source/output differences or permit the full core's pressure-break path.
Separate complete mixed-world frame validation is documented in
docs/ios-mixed-fluids.md in the private build repository.
