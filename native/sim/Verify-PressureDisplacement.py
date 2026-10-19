"""Compare the reconstructed liquid overpressure branch against original PostProcessCell."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

DLL_SHA = '031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727'

def sha(data):
    return hashlib.sha256(data).hexdigest()

parser = argparse.ArgumentParser(description=__doc__)
for key in ('probe', 'fixtures', 'elements', 'output', 'result'):
    parser.add_argument('--' + key, required=True, type=Path)
parser.add_argument('--case', action='append', help='Verify only these explicitly named cases during diagnosis')
parser.add_argument('--source-root', type=Path, help='Retained source snapshot used to compile this probe')
parser.add_argument('--probe-source', type=Path, help='Probe source when staged separately from the module sources')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
elements_sha = sha(args.elements.read_bytes())
rows = []
for fixture in sorted(args.fixtures.iterdir()):
    if not (fixture / 'report.json').is_file():
        continue
    if args.case and fixture.name not in args.case:
        continue
    report_bytes = (fixture / 'report.json').read_bytes()
    report = json.loads(report_bytes)
    assert report['dll_sha256'] == DLL_SHA
    assert report['elements_sha256'] == elements_sha
    assert report['observed_headless'] == 1 and report['observed_radiation'] == 0
    assert report['observed_force_ore'] == 0
    assert report['native_entry_rva'] == '0x4DAF0'
    assert report['new_game_frames_submitted'] == 0 and report['complete']
    assert report['stages'][-1] == 'Shutdown: returned'
    before, after = report['phases']['before'], report['phases']['after']
    for phase in ('before', 'after'):
        for key, info in report['phases'][phase]['arrays'].items():
            data = (fixture / f'{phase}.{key}.bin').read_bytes()
            assert len(data) == info['bytes'] and sha(data) == info['sha256'], (fixture.name, phase, key)
    width, height = report['sim_width'], report['sim_height']
    output = args.output / fixture.name
    case = report['case']
    subprocess.run([str(args.probe.resolve()), str(args.elements), str(fixture), str(output),
                    str(width), str(height), str(report['center_sim_cell']), str(case['Seed']),
                    str(case['Direction']), str(case['Tick'])], check=True, timeout=20)
    state = json.loads((output / 'state.json').read_bytes())
    assert state['seed'] == after['seed'], (fixture.name, 'seed', state['seed'], after['seed'])
    assert state['direction'] == after['displacementDirection'] == before['displacementDirection']
    assert state['tick'] == after['tick'] == before['tick']
    assert after['iterateDirection'] == before['iterateDirection']
    for key in ('elementIdx', 'temperature', 'mass', 'properties', 'insulation', 'strengthInfo',
                'radiation', 'diseaseIdx', 'diseaseCount'):
        expected = (fixture / f'after.updated.{key}.bin').read_bytes()
        actual = (output / f'after.updated.{key}.bin').read_bytes()
        assert expected == actual, (fixture.name, key, expected.hex(), actual.hex())
    for key in before['arrays']:
        if key.startswith('source.') or key in ('accumulatedFlow', 'updated.diseaseInfestationTickCount',
                                               'updated.diseaseGrowthAccumulatedError'):
            assert (fixture / f'before.{key}.bin').read_bytes() == (fixture / f'after.{key}.bin').read_bytes(), (fixture.name, key)
    for key in ('flow', 'timers'):
        assert (output / f'after.{key}.bin').read_bytes() == (fixture / f'after.{key}.bin').read_bytes(), (fixture.name, key)
    raw_changes = (output / 'after.changes.sim.bin').read_bytes()
    changes = [item[0] for item in struct.iter_unpack('<I', raw_changes)]
    expected_changes = (fixture / 'after.events.substanceChangeInfo.bin').read_bytes()
    expected = []
    for cell, old, new in struct.iter_unpack('<iHH', expected_changes):
        assert old == new == 65535
        expected.append((cell // (width - 2) + 1) * width + cell % (width - 2) + 1)
    assert changes == expected, (fixture.name, 'ordered substance notifications', changes, expected)
    damage = (output / 'after.events.worldDamageInfo.bin').read_bytes()
    assert damage == (fixture / 'after.events.worldDamageInfo.bin').read_bytes(), (fixture.name, 'damage events')
    assert len(changes) == state['changeCount'] and len(damage) == state['damageCount'] * 8
    for event in after['event_vectors']:
        assert not (fixture / f'before.events.{event}.bin').read_bytes()
        if event not in ('substanceChangeInfo', 'worldDamageInfo'):
            assert not (fixture / f'after.events.{event}.bin').read_bytes(), (fixture.name, 'unhandled event', event)
    rows.append(dict(case=fixture.name, report_sha256=sha(report_bytes), changes=len(changes),
                     damage_events=state['damageCount'], displaced=state['displaced'], passed=True))
assert len(rows) == (len(set(args.case)) if args.case else 197), len(rows)
result = dict(all_passed=True, original_dll_sha256=DLL_SHA, probe_sha256=sha(args.probe.read_bytes()),
              cases=rows,
              scope=f'{len(rows)} original full single-cell PostProcess calls; reconstructed density, pressure damage and overpressure displacement prefix; nine updated arrays, unchanged source and two disease aux arrays, raw flow/timers, RNG/directions/tick, ordered substance and damage events; 18 other empty native event vectors',
              limits='Water only, properties 0/0x40, no disease, no later partial heat/offgas action. This does not validate an entire frame, other liquids, managed application of events or ARM64.')
if args.source_root:
    names = ('PressureDisplacementProbe.cpp', 'PressureDisplacement.cpp', 'PressureDisplacement.h',
             'PressureBreak.cpp', 'PressureBreak.h', 'StabilityTimers.cpp', 'StabilityTimers.h',
             'LiquidFlow.h', 'SaveWorldV15.h')
    result['source_snapshot_sha256'] = {
        name: sha((args.probe_source if name == 'PressureDisplacementProbe.cpp' and args.probe_source
                   else args.source_root / name).read_bytes()) for name in names}
args.result.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f'Pressure displacement: PASS ({len(rows)} original single-cell cases; {sum(row["changes"] for row in rows)} notifications)')
