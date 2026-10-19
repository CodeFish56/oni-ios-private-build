# Build 744825 unstable solid oracle

This Windows x64 tool calls the unmodified, SHA-bound original `PostProcessCell`
at RVA `0x4DAF0`. It does not launch the game or submit a physics frame.

Inputs are the original Steam DLL and captured element/disease tables. Each case
runs in a separate process. InitializeFromCells explicitly requests radiation
off and headless on, then checks the actual flags at SimData `+0x85A/+0x859`.
The diagnostic saved-option byte (`+0x18`), center timer, random seed, and tick
are set as stated in the report. Updated cells are copied to the source snapshot
before invoking the target; no source array mutation is expected from this pass.

```
dotnet build porting/SimReference/SolidReference/SolidReference.csproj -c Release
dotnet porting/SimReference/SolidReference/bin/Release/net10.0/SolidReference.dll --game-root GAME_ROOT --capture-dir CAPTURE_DIR --output-dir OUTPUT_DIR --suite
```

There are 191 cases and 353 original PostProcessCell calls: 173 single-call cases
and 18 sequences of ten calls. Sequences retain a snapshot after every call,
including timer initialization, decrement, physical fall, and later no-op calls.
Use `--case NAME` to run one case or `--suite --sequence-suite` for the 18 sequences.

Coverage includes basic fall, saved-option-enabled diagonal fall and mass split,
obstacle and property-bit-4 checks, gas/water displacement, tick-dependent search,
timer upper-bit preservation and shared RNG consumption. The headless branch
emits substance notifications and physically moves material; the nonheadless
20-byte UnstableCellInfo event branch is outside this implementation.

`native-post-table.bin` records the full native 44-byte element postprocess
table. Its diagonal minimum-mass value at +36 equals input wire +44 for all
202 captured element records. Sand's value is 50 kg.

The candidate checker is `porting/NativeSim/Verify-Solid.py`. It compares owned
cell arrays, flow, complete timer bytes, random state, and raw event order, and
checks that source/extra arrays and the other event vectors remain unchanged.
It does not claim full UpdateData, disease, ARM64 or phone validation.
