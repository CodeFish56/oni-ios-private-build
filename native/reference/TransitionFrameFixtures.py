"""Capture complete original frames for nontrivial transitions and pressure displacement."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('oracle', 'game-root', 'capture-dir', 'source-save', 'output-dir'):
        p.add_argument('--' + key, required=True, type=Path)
    p.add_argument('--case', action='append', default=[])
    a = p.parse_args()
    table = (a.capture_dir / 'elements.bin').read_bytes()
    ids = {struct.unpack_from('<I', table, 4+i*164)[0]: i for i in range(struct.unpack_from('<I', table)[0])}
    vacuum, water, steam, ice, sand, granite, dirt, unob, oxygen = [ids[h & 0xffffffff] for h in
        (0x2D39BF75, 0x6D796197, 0xCA627A30, 873952427, 381796644, -105943486, 1624244999, 0x6D95058C, 0xA4E0B340)]
    cases = []
    for i in (82, 83, 142, 163, 165, 145, 135, 153, 155, 161):
        high = struct.unpack_from('<f', table, 4+i*164+60)[0]
        cases.append((f'byproduct-high-{i}', 'single', i, 1000, high+10, 0))
    cases += [('sugar-freeze-double-ore', 'single', 153, 1000, 180, 0),
              ('dirt-to-sand', 'single', dirt, 1000, 700, 0),
              ('notify-melt-ice', 'single', ice, 1000, 280, 64),
              ('notify-melt-mud', 'single', 83, 1000, 400, 64),
              ('notify-melt-water', 'single', water, 1000, 380, 64),
              ('partial-boil-liquid-fallback', 'partial', water, 1000, 300, 0),
              ('partial-boil-steam-above-hot', 'partial-left', water, 1000, 300, 0),
              ('partial-false-with-gas-effect', 'partial', water, 5.000000476837158, 300, 0),
              ('partial-five-exact', 'partial', water, 5, 300, 0),
              ('partial-melt-oxygen', 'melt', oxygen, 1000, 400, 0),
              ('partial-melt-steam', 'melt', steam, 1000, 600, 0),
              ('partial-melt-notify-inhibited', 'melt-inhibit', oxygen, 1000, 400, 0),
              ('dense-sand-prevalidated', 'dense', sand, 1000, 300, 0)]
    for mass in (1624.9998779296875, 1625., 1625.0001220703125, 2125., 3125.000244140625):
        cases.append((f'pressure-after-flow-{mass}', 'pressure', water, mass, 300, 0))
    cases += [('pressure-density-short-circuit', 'pressure-density', water, 3000, 300, 0),
              ('pressure-trace-upper-gas', 'pressure-trace', water, 10000, 300, 0)]
    a.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, layout, element, mass, temp, prop in cases:
        if a.case and name not in a.case: continue
        width = height = 40 if layout == 'dense' else 12
        count = width*height
        cells = [(vacuum, 0., 0., 0)] * count
        def put(x, y, e, m, t, props=0): cells[y*width+x] = (e, m, t, props)
        put(6, 6, element, mass, temp, prop)
        if layout.startswith('partial'):
            for dx, dy in ((0,-1),(-1,0),(1,0),(0,1)): put(6+dx, 6+dy, unob, 9999, 300)
            if layout == 'partial':
                put(6,5,granite,1840,400); put(6,7,oxygen,1,400)
            else:
                put(5,6,granite,1840,400); put(5,7,oxygen,1,400)
        if layout.startswith('melt'):
            put(6,5,ice,1000,268,64 if layout.endswith('inhibit') else 0)
        if layout.startswith('pressure'):
            for dx,dy in ((0,-1),(-1,0),(1,0)): put(6+dx,6+dy,unob,9999,300)
            put(6,7,water,.005 if layout.endswith('trace') else 100,300)
            if layout == 'pressure-trace':
                # Seal liquid flow upward; postprocess sees trace upper Water.
                for x,y in ((5,7),(7,7),(6,8)): put(x,y,oxygen,1,300)
            if layout == 'pressure-density': put(6,5,water,3000,320)
        if layout == 'dense':
            for y in range(2,height-2):
                for x in range(2,width-2): put(x,y,sand,1000,300)
        payload = bytearray(20+40*count)
        struct.pack_into('<III??',payload,0,width,height,123456,False,True)
        for i,(e,m,t,props) in enumerate(cells):
            struct.pack_into('<HBB4xff',payload,14+16*i,e,props,0,t,m)
            payload[14+16*count+12*i] = 255 # disease index, not insulation; insulation is byte 3 of cell data
            struct.pack_into('<H',payload,14+28*count+12*i,vacuum)
        dest = a.output_dir/name; dest.mkdir(exist_ok=True)
        wire = dest/'fixture-input.bin'; wire.write_bytes(payload)
        steps = ','.join(['0.2']*12) if layout == 'dense' else '0.2,0.4,0,0.2,0.6'
        cmd = ['dotnet',str(a.oracle),'--game-root',str(a.game_root),'--capture-dir',str(a.capture_dir),
               '--source-save',str(a.source_save),'--synthetic',f'{width}x{height}','--no-radiation','--define-world',
               '--normal-debug-properties','--initialize-input',str(wire),'--step-seconds',steps,
               '--frame-region',f'0,0,{width},{height-2}','--output-dir',str(dest)]
        result = subprocess.run(cmd,capture_output=True,text=True,timeout=120)
        (dest/'process.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode: raise RuntimeError((name,result.returncode,'see process.log'))
        rows.append({'case':name,'frames':len(steps.split(','))})
        print('Recorded '+name,flush=True)
    (a.output_dir/'transition-suite.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__': main()
