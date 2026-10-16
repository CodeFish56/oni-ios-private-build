# Original water/gas interaction oracle — investigation only

This Windows x64 / .NET 10 tool directly calls original Build 744825
`UpdateLiquid` (RVA `0x498B0`) after initializing and copying updated to source.
It requires the same SHA-bound original DLL and captured tables as LiquidReference.
No original instruction is patched and no complete frame is submitted.
The actual native flags must be headless=1/radiation=0 after initialization.

40 cases have been recorded: O2/CO2 below water, side displacement with
tick values 0..3, gas merge, blocked destinations, zero gas mass, diagonal
candidates and water/diagonal gas exchanges. Tick is explicitly controlled
in this isolated diagnostic process. Inputs, source/updated arrays, events,
RNG and timers are retained locally.

```powershell
dotnet build WaterGasReference.csproj -c Release
dotnet bin/Release/net10.0/WaterGasReference.dll --suite --game-root GAME --capture-dir CAPTURE --output-dir OUTPUT
```

These runs establish original behavior only. No matching reconstructed
water/gas implementation or full-frame validation is claimed yet.
