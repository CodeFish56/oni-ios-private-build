# Original ordered mixed-fluid postprocess oracle

Windows x64 / .NET 10; SHA-bound original Build 744825 DLL. 96 cases call
original `PostProcessCell` in one ordered rectangle without submitting a frame.
The native headless/radiation flags are read and asserted as 1/0.

The test includes water between gas visits, same-water temperature mixing,
low masses, random gas candidates containing water, both displacement
directions and fixed seeds. Source/output arrays, events, flow, timers and
RNG flags are retained locally. `Verify-MixedPost.py` checks the real C++
single-pass implementation; no replacement algorithm is written in Python.

```powershell
dotnet build MixedPostReference.csproj -c Release
dotnet bin/Release/net10.0/MixedPostReference.dll --suite --game-root GAME --capture-dir CAPTURE --output-dir OUTPUT
```

This is a restricted ordered-postprocess test, not a general phase/disease,
full-frame, ARM64 or iPhone validation.
