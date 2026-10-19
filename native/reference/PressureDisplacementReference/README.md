# Build 744825 liquid overpressure oracle

This tool loads the exact original Steam `SimDLL.dll` (SHA256
`031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727`)
inside its own process and calls `PostProcessCell` at RVA `0x4DAF0` once.
It does not run the game UI or submit a physics frame.

The 197 cases cover the strict 1.5 × maximum-mass threshold, the
`max(1.01 × aboveMass, maxMass)` threshold and adjacent floats, recipient
combinations, density/RNG short circuits, pressure-damage short circuits,
property `0x40`, and tick-dependent sub-0.01 kg liquid/gas displacement.
The native headless and radiation bytes are asserted as 1 and 0. The direct
probe explicitly sets and records the otherwise uninitialized debug
`forceOre` byte as 0.

The fixed source center is deliberately replaced with oxygen while the
updated center remains water, demonstrating that this branch reads the
accumulated updated cells. All 11 fixed and 11 updated arrays, flow, timers,
accumulated flow, 20 event vectors, RNG directions and tick are retained.

Run the oracle with `--suite --game-root ... --capture-dir ... --output-dir ...`.
Then archive its output directory to
`porting/analysis/pressure-displacement-direct-fixtures.tar` and run
`wsl.exe bash porting/analysis/verify-pressure-displacement-modules.sh`.
The script compiles and checks both normal and ASan/UBSan C++ probes on an
ext4 temporary directory. The checker verifies raw cell bytes, event order
and duplicates, RNG state, direction and tick; it checks that unrelated
source/auxiliary arrays and the other 18 event vectors remain unchanged.

Important native details preserved by the reconstruction:

- The source threshold is strictly greater than 1.5 × maximum mass.
- Transported mass is `currentAfterDisplacement * 0.49751242995262146f`,
  not one half of the initial mass.
- An upper liquid mass in `(0, 0.01)` can displace an adjacent gas and
  still return false; those preceding mutations and notifications survive.
- Gas may enter the top vacuum border. Its existing 9999 kg participates
  in temperature mixing. The border timer is reset, while an out-of-range
  game-cell conversion emits no substance-change record.

Scope: Water, properties 0/0x40, no disease, fixture temperatures that
exclude subsequent partial heat/offgas actions. This tool does not prove
whole-frame behavior, other liquids, event application by managed code,
ARM64 behavior or playability on the device.

Retained validation: all 197 original cases pass both normal and ASan/UBSan
probes, with 544 raw ordered substance notifications and three pressure
damage records. The candidate reports 99 completed overpressure moves;
trace-liquid cases preserve their gas displacement while leaving the
overpressure completion flag false. Reports are retained locally as
`porting/analysis/pressure-displacement-module-check-{normal,sanitized}.json`.
