"""Capture original complete frames for whole-cell phases, pressure and solids."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('oracle', 'game-root', 'capture-dir', 'source-save', 'output-dir'):
        p.add_argument('--' + name, required=True, type=Path)
    a = p.parse_args()
    table = (a.capture_dir / 'elements.bin').read_bytes()
    ids = {struct.unpack_from('<I', table, 4 + 164*i)[0]: i
           for i in range(struct.unpack_from('<I', table)[0])}
    vacuum, water, steam, ice, sand, granite, dirt, unob, oxygen = [ids[h & 0xffffffff] for h in
        (0x2D39BF75, 0x6D796197, 0xCA627A30, 873952427, 381796644, -105943486,
         1624244999, 0x6D95058C, 0xA4E0B340)]
    cases = [('freeze-small', water, 100, 268), ('freeze-large', water, 1000, 268),
             ('boil', water, 100, 380), ('condense', steam, 1, 360),
             ('melt', ice, 1000, 280), ('hysteresis-water-low', water, 100, 269.5),
             ('hysteresis-water-high', water, 100, 375.5), ('hysteresis-ice', ice, 1000, 275.5),
             ('hysteresis-steam', steam, 1, 369.5), ('sand-fall', sand, 1000, 300),
             ('sand-gas', sand, 1000, 300), ('sand-water', sand, 1000, 300),
             ('sand-diagonal', sand, 1000, 300), ('sand-pile', sand, 1000, 300),
             ('pressure-one-wall', water, 2000, 300), ('pressure-two-walls', water, 2000, 300),
             ('pressure-resistant', water, 2000, 300), ('sand-mixed', sand, 1000, 300),
             ('sand-diagonal-enabled', sand, 1000, 300), ('sand-pile-diagonal-enabled', sand, 1000, 300),
             ('pressure-density-return', water, 2000, 300)]
    a.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, element, mass, temp in cases:
        width = height = 12
        count = width * height
        cells = [(vacuum, 0., 0.)] * count
        def put(x, y, e, m, t): cells[y*width+x] = (e, m, t)
        if name.startswith('hysteresis') or name in ('freeze-small', 'freeze-large', 'boil', 'condense', 'melt'):
            put(6, 6, element, mass, temp)
        else:
            put(6, 8, element, mass, temp)
            if name in ('sand-gas', 'sand-mixed'):
                cells = [(oxygen, 1., 300.)] * count
                put(6, 8, sand, mass, temp)
            if name in ('sand-water', 'sand-mixed'):
                for y in (2, 3):
                    for x in range(2, 10): put(x, y, water, 500, 300)
            if name.startswith('sand-diagonal'): put(6, 7, granite, 1840, 300)
            if name.startswith('sand-pile'):
                for y in (7, 8, 9):
                    for x in (5, 6, 7): put(x, y, sand, 1000, 300)
            if name.startswith('pressure'):
                put(6, 8, vacuum, 0, 0)
                put(6, 6, water, mass, temp)
                for x, y in ((5, 6), (7, 6), (6, 7)): put(x, y, unob, 9999, 300)
                put(6, 5, granite if name == 'pressure-resistant' else dirt,
                    1840 if name == 'pressure-resistant' else 100, 300)
                if name == 'pressure-two-walls': put(6, 4, dirt, 100, 300)
                if name == 'pressure-density-return':
                    put(6, 5, water, 2000, 320)
                    put(5, 5, dirt, 100, 300)
                    put(7, 5, unob, 9999, 300)
                    put(6, 4, unob, 9999, 300)
        payload = bytearray(20 + 40*count)
        struct.pack_into('<III??', payload, 0, width, height, 123456, False, True)
        for i, (e, m, t) in enumerate(cells):
            struct.pack_into('<HBB4xff', payload, 14+16*i, e, 0, 255, t, m)
            payload[14+16*count+12*i] = 255
            struct.pack_into('<H', payload, 14+28*count+12*i, vacuum)
        dest = a.output_dir / name
        dest.mkdir(exist_ok=True)
        wire = dest / 'fixture-input.bin'
        wire.write_bytes(payload)
        steps = ','.join(['0.2'] * 12) if name.startswith('sand') else '0.2,0.4,0,0.2,0.6'
        cmd = ['dotnet', str(a.oracle), '--game-root', str(a.game_root), '--capture-dir', str(a.capture_dir),
               '--source-save', str(a.source_save), '--synthetic', f'{width}x{height}', '--no-radiation',
               '--define-world', '--initialize-input', str(wire), '--step-seconds', steps,
               '--frame-region', f'0,0,{width},{height-2}', '--output-dir', str(dest)]
        if name.endswith('enabled'): cmd += ['--saved-options', '1']
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        (dest / 'process.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode: raise RuntimeError((name, result.returncode, 'see process.log'))
        rows.append({'case': name, 'frames': len(steps.split(','))})
        print('Recorded ' + name, flush=True)
    (a.output_dir / 'phase-solid-suite.json').write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__': main()
