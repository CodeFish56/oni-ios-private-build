# Build 744825 stability timer oracle

Windows x64, .NET 10. SHA-guards the exact original SimDLL before execution.
Runs original functions in its own console process; never opens the game UI.

```powershell
dotnet build native/reference/TimerReference/TimerReference.csproj -c Release
dotnet native/reference/bin/TimerReference/Release/net10.0/TimerReference.dll --leaf-suite --game-root '<Build744825 game directory>' --output-dir porting/analysis/timer-reference-744825
dotnet native/reference/bin/TimerReference/Release/net10.0/TimerReference.dll --suite --game-root '<Build744825 game directory>' --capture-dir '<captured tables directory>' --output-dir porting/analysis/timer-post-reference-744825
```

`--leaf-suite` calls original GetStableTicksRemaining RVA 284D0 over all timer
byte values, six seeds and forty successive calls: 1536 groups / 61440 calls.
Diagnostic allocated memory provides only fields read by that leaf function.

`--suite` initializes the real DLL and captures single original PostProcessCell
calls over 52 oxygen/vacuum/sand cases with both headless values. It records
actual internal initialization flags, grids, flow, timers and events. No physics
frame is submitted. Complete outputs are local analysis fixtures, not production
game resources.

The native `Verify-Timers.py` compares a compiled `TimerProbe.cpp` against the
**actual original return/timer/seed fields** in leaf-suite.json. See
`porting/analysis/SimTimers-744825.md` for conclusions and coverage limits.
