# Original uint16 tick lifecycle probe

Windows x64 / .NET 10; checks the original 744825 DLL SHA before loading.
Calls exported initialization, frames, save and load in its own process.
Between completed frames only, it modifies its own SimData tick to reach
65535 and to distinguish saved tick=11 from receiving tick=43210.
It does not patch DLL instructions or access the user's running game.

```powershell
dotnet build TickReference.csproj -c Release
dotnet bin/Release/net10.0/TickReference.dll --game-root GAME --capture-dir CAPTURE --output-dir OUTPUT
```

`Verify-TickFrames.py` validates this report and separately reaches the
candidate's wrap using 65537 real updates, through a read-only tick API.
The original's existing-state Load experiment is not a claim that the
candidate already supports positive-time load (v15 lacks the RNG seed).
