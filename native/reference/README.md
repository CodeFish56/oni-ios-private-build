# Original-engine data oracle (Windows x64)

This tool loads only the Build744825 DLL with SHA-256
`031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`.
It executes in an isolated process without launching/modifying the game.
Input tables and SIMSAVE v15 data come from local captures.

```powershell
dotnet build native/reference/WorldgenReference.csproj -c Release
dotnet native/bin/WorldgenReference/Release/net10.0/WorldgenReference.dll `
  --game-root '<Build744825 game directory>' `
  --capture-dir '<directory containing elements.bin and diseases.bin>' `
  --source-save '<complete raw SIMSAVE v15 file>' `
  --synthetic 8x6 --output-dir '<new output directory>'
python native/reference/WorldgenAnalyze.py '<output directory>'
```

Omit `--synthetic` to initialize from the source save's actual cells.
`--load-world` uses AllocateCells→DefineWorldOffsets→Load instead.
For its analyzer, also supply `--capture-dir` to resolve element/disease hashes.

Other diagnostic switches: `--no-radiation`, `--zero-frame`, `--define-world`
(after InitializeFromCells), `--raw-fields`, `--sun-fields` and
`--thermal-fields`. The last three deliberately change raw wire data; do not
describe them as the original managed Cell.Write's default output.

Each completed case saves the full input, Start/Prepare grids and auxiliary
arrays, 29 event counts, internal-border arrays and an original-engine Save.
`report.json` binds the target, input and output digests. `Verify-Bridge.py`
compares the new compiled C ABI to these independent outputs.

The tool covers initialization, restricted load/save and zero-time frames.
It does not validate positive-time simulation or iPhone gameplay.
