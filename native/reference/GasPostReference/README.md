# Original Build 744825 direct gas postprocess oracle

This Windows x64 console tool loads the SHA-256 pinned original `SimDLL.dll` in
an isolated process. It does not start the game, modify the DLL, queue physics
frames, or contact GitHub. Each suite case runs in its own child process with a
20 second timeout.

## Build and run

From the private build repository root:

```powershell
dotnet build native/reference/GasPostReference/GasPostReference.csproj -c Release
dotnet native/reference/bin/GasPostReference/Release/net10.0/GasPostReference.dll `
  --game-root '<Build744825 game directory>' `
  --capture-dir '<directory containing elements.bin and diseases.bin>' `
  --output-dir '<new output directory>' --suite
```

For one bounded case, replace `--suite` with `--case threshold-oxygen-small`
and use a case-specific output directory. `--list-cases` prints all names.

## Inputs and controlled execution

Required DLL SHA-256:
`031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`.
The captured element table is used to resolve gas/Vacuum indices dynamically;
the retained run's element/disease hashes are in each `report.json`.

Each case initializes a 5×5 headless world through original exported calls:
Initialize → Elements_CreateTable → Disease_CreateTable →
InitializeFromCells → Start. The SimThread is idle; no NewGameFrame is sent.
After checking the 7×7 internal grid and vector lengths, the oracle deliberately
sets the selected seed/direction, center timer `0xA2`, center flow
`[1.25,2.5,3.75,5]`, and neighbor flow `[6,7,8,9]` when present. It then directly
calls original RVA `0x4DAF0` with `gSimData`, `gSimData.simEvents`, and the center
sim-cell index. It captures before/after and shuts down the original simulator.
The current oracle explicitly requests radiation off and checks native
headless=1/radiation=0 after initialization; the verifier rejects older reports
without those observed flags.

The exact native locations are guarded by the DLL hash: gSimData pointer RVA
`0x115640`, updatedCells offset `0x28`, flow `0x68`, timers `0x70`, events `0xA8`.

## Cases and retained results

118 cases completed successfully on 2026-10-15:

- 18 center-mass threshold cases: O2 and CO2 at zero, 5e-10, predecessor/equal/
  successor of float32 1e-9, .0005, predecessor/equal/successor of float32 .001.
- 48 neighbor cases: each center gas, each neighbor gas, all four directions,
  and neighbor mass predecessor/equal/successor of float32 1.0.
- 48 continuation cases: seeds9020/10000/20000, both center/neighbor gases,
  right/below neighbors and two center masses. These cover RNG and swaps after
  evaporation already changed the center into Vacuum.
- 4 cases starting with displacementDirection=+1 to verify direction reversal.

Results: `porting/analysis/gas-lowmass-direct-reference-744825/suite.json` has
118 exits0, failures0. `audit.json` records 90 evaporation branches; flow clearing,
position-property preservation, timer OR operations and direction agree with
static rules in all118 cases. Observed RNG call counts per single call are
1/2/4/5; event vector counts are0/1/3.

The important continuation example is
`continuation-oxygen-oxygen-right-tiny-seed9020`: center first evaporates, then
the right neighbor's 1kg oxygen moves back into it. The internal event sequence
is center12, right13, center12; final seed4183230144. This disproves an early
`continue` after evaporation.

## Files and limits

Each case contains the exact initialization message, report/stages and full
binary arrays for both phases: 11 updatedCellSOA grids, float4 flow, timers,
accumulatedFlow, and20 internal SimEvents vectors. All grids include the internal
one-cell border. SubstanceChangeInfo is decoded in the report; its old/new are
internal FFFF placeholders, not final GameDataUpdate values.

This validates one controlled PostProcessCell call only. It does not validate
pressure before the call, full UpdateData, positive-time continuous frames,
multi-species pressure, final output event deduplication/packing, diseases,
thermal transitions, or iOS ARM64 behavior.

The project compiles an explicit `.cs.txt` file and writes generated code under
the parent `obj/GasPostReference` directory so it does not pollute the existing
SimReference project's default C# source collection.
