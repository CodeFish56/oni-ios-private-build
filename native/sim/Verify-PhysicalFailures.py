"""Verify rollback and saved-option state at the new physical boundaries."""
import argparse
import ctypes as c
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('frames', Path(__file__).with_name('Verify-GasFrames.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('library', 'elements', 'diseases', 'result'):
        p.add_argument('--' + name, required=True, type=Path)
    a = p.parse_args()
    elements, diseases = a.elements.read_bytes(), a.diseases.read_bytes()
    ids = {struct.unpack_from('<I', elements, 4 + 164*i)[0]: i
           for i in range(struct.unpack_from('<I', elements)[0])}
    vacuum, water, ice, dirt, granite, unob, oxygen = [ids[h & 0xffffffff] for h in
        (0x2D39BF75, 0x6D796197, 873952427, 1624244999, -105943486, 0x6D95058C, 0xA4E0B340)]
    b = m.module.Bridge(a.library)
    b.library.OniSim_GetDiagnostics.argtypes = [c.POINTER(m.Diagnostics)]
    b.library.OniSim_CopyStabilityTimers.argtypes = [c.c_void_p, c.c_int32]
    b.library.OniSim_GetTickCount.restype = c.c_uint32
    width = height = 8
    n = width * height
    visible = bytes([255]) * n

    def initialize(changes, properties=None, infected=None):
        properties, infected = properties or {}, infected or {}
        payload = bytearray(20 + 40*n)
        struct.pack_into('<III??', payload, 0, width, height, 123456, False, True)
        for i in range(n):
            e, mass, temp = changes.get(i, (vacuum, 0., 0.))
            struct.pack_into('<HBB4xff', payload, 14+16*i, e, properties.get(i, 0), 0, temp, mass)
            payload[14+16*n+12*i] = 255 # disease-free; insulation is cell-data byte 3
            if i in infected:
                struct.pack_into('<B3xi', payload, 14+16*n+12*i, 0, 1)
            struct.pack_into('<H', payload, 14+28*n+12*i, vacuum)
        b.Initialize(None)
        b.send(1108437482, elements); b.send(825301935, diseases)
        b.send(2062421945, bytes(payload)); b.send(-931446686)
        return b.send(1078620451, visible)

    center = 4*width+4
    cases = {
        'remaining-nonwater-liquid-after-ore': ({center: (water, 100, 268), 5*width+5: (granite, 1840, 1500)}, {}, {}),
        'partial-melt-nonwater-target': ({center: (oxygen, 1000, 2500), center-width: (granite, 1840, 300)}, {}, {}),
        'unknown-properties-after-phase-events': ({center: (water, 100, 268), 5*width+5: (oxygen, 1, 300)}, {5*width+5: 1}, {}),
        'disease-after-phase-events': ({center: (water, 100, 268), 5*width+5: (oxygen, 1, 300)}, {}, {5*width+5: True}),
    }
    rows = []
    for name, (changes, properties, infected) in cases.items():
        pointer = initialize(changes, properties, infected)
        saved, state, timers, descriptor = b.save(), m.diagnostics(b), m.timer_snapshot(b), c.string_at(pointer, 496)
        try:
            b.send(-775326397, struct.pack('<fiiiiff', .2, 0, 0, width, height-2, 1, 0), expected=4)
        except AssertionError as ex:
            raise AssertionError((name, str(ex))) from ex
        expected_reason = {'remaining-nonwater-liquid-after-ore': 'Remaining non-water liquid',
                           'partial-melt-nonwater-target': 'Partial melt currently supports',
                           'unknown-properties-after-phase-events': 'without properties',
                           'disease-after-phase-events': 'without disease'}[name]
        actual_reason = b.GetLastError().decode('utf-8')
        assert expected_reason in actual_reason, (name, actual_reason)
        assert b.save() == saved and m.diagnostics(b) == state and m.timer_snapshot(b) == timers
        assert c.string_at(pointer, 496) == descriptor
        rows.append({'case': name, 'expected_error_fragment': expected_reason,
                     'actual_error': actual_reason, 'world_rng_tick_timers_events_unchanged': True})
        b.Shutdown()

    initialize({})
    baseline = b.save()
    b.send(-1683118492, struct.pack('<ff4x', 1., 1.))
    b.send(-1683118492, struct.pack('<ffB3x', 1., 1., 1), expected=4)
    assert b.save() == baseline
    b.send(1154135737, bytes([255, 1]))
    enabled = b.save()
    assert enabled[28] == 1 and enabled[:28] == baseline[:28] and enabled[29:] == baseline[29:]
    b.send(1154135737, bytes([0, 2]), expected=4)
    assert b.save() == enabled
    b.send(1154135737, bytes([1, 0]))
    assert b.save() == baseline
    b.Shutdown()
    # v15 restores the option byte into a newly allocated runtime.
    b.Initialize(None); b.send(1108437482, elements)
    b.send(1092408308, struct.pack('<ii??', width, height, False, True))
    b.send(-895846551, struct.pack('<iiiii', 1, 0, 0, width, height))
    b.send(825301935, diseases); b.send(-1836204275)
    b.send(-672538170, enabled)
    assert b.save() == enabled
    b.Shutdown()
    result = {'passed': True, 'library_sha256': hashlib.sha256(a.library.read_bytes()).hexdigest(),
              'failure_cases': rows, 'saved_options_set_clear_roundtrip': True,
              'unknown_option_bit_rejected_atomically': True, 'v15_option_restore': True}
    a.result.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Physical failures: PASS (4 rollback boundaries, saved options and v15 roundtrip)')


if __name__ == '__main__': main()
