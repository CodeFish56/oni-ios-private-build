"""Compare the mixed single-pass postprocessor with original DLL snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

FIELDS = ('elementIdx', 'temperature', 'mass', 'properties', 'insulation',
          'strengthInfo', 'radiation', 'diseaseIdx', 'diseaseCount')
DLL_SHA = '031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727'
def sha(data): return hashlib.sha256(data).hexdigest()

parser = argparse.ArgumentParser(description=__doc__)
for name in ('probe', 'fixtures', 'elements', 'output', 'result'):
    parser.add_argument('--'+name, required=True, type=Path)
args=parser.parse_args(); args.output.mkdir(exist_ok=True,parents=True)
rows=[]
for fixture in sorted(args.fixtures.iterdir()):
    if not (fixture/'report.json').is_file(): continue
    raw_report=(fixture/'report.json').read_bytes(); report=json.loads(raw_report)
    assert report['dll_sha256']==DLL_SHA
    assert report['observed_headless']==1 and report['observed_radiation']==0
    assert report['elements_sha256']==sha(args.elements.read_bytes())
    assert report['new_game_frames_submitted']==0 and report['complete']
    assert report['stages'][-1]=='Shutdown: returned'
    before,after=report['phases']['before'],report['phases']['after']
    for phase in ('before','after'):
        for name,item in report['phases'][phase]['arrays'].items():
            data=(fixture/f'{phase}.{name}.bin').read_bytes()
            assert len(data)==item['bytes'] and sha(data)==item['sha256']
    width,height=report['sim_width'],report['sim_height']; output=args.output/fixture.name
    subprocess.run([str(args.probe.resolve()),str(args.elements),str(fixture),str(output),str(width),str(height),
                    *map(str,report['postprocess_game_region']),str(before['seed']),str(before['displacementDirection'])],check=True,timeout=20)
    hashes={}
    for name in ['updated.'+x for x in FIELDS]+['flow','timers']:
        actual=(output/f'after.{name}.bin').read_bytes(); expected=(fixture/f'after.{name}.bin').read_bytes()
        assert actual==expected,(fixture.name,name,'byte mismatch')
        hashes[name]=sha(actual)
    for name in ['source.'+x for x in FIELDS]+['updated.diseaseInfestationTickCount','updated.diseaseGrowthAccumulatedError','accumulatedFlow']:
        assert (fixture/f'before.{name}.bin').read_bytes()==(fixture/f'after.{name}.bin').read_bytes(),(fixture.name,name,'unexpected unmodeled change')
    state=json.loads((output/'state.json').read_bytes())
    assert state['seed']==after['seed'] and state['displacementDirection']==after['displacementDirection'],fixture.name
    seed=before['seed']
    for _ in range(state['rngCalls']): seed=(seed*214013+2531011)&0xffffffff
    assert seed==after['seed']
    changes=(output/'after.changes.sim.bin').read_bytes(); assert len(changes)==4*state['changeCount']
    events=b''.join(struct.pack('<iHH',(s//width-1)*(width-2)+s%width-1,65535,65535)
                    for (s,) in struct.iter_unpack('<I',changes))
    assert events==(fixture/'after.events.substanceChangeInfo.bin').read_bytes(),(fixture.name,'event bytes/order')
    for name in after['event_vectors']:
        if name!='substanceChangeInfo': assert not (fixture/f'after.events.{name}.bin').read_bytes()
    assert not (fixture/'before.events.substanceChangeInfo.bin').read_bytes()
    hashes['events.substanceChangeInfo']=sha(events)
    rows.append(dict(case=fixture.name,report_sha256=sha(raw_report),arrays_sha256=hashes,state=state,passed=True))
assert len(rows)==96,len(rows)
result=dict(all_passed=True,original_dll_sha256=DLL_SHA,probe_sha256=sha(args.probe.read_bytes()),cases=rows,
            scope='single mixed ordered pass: 9 owned cell grids, flow, defined timer bytes, raw substance event order, seed/direction/RNG count; unchanged original source and extra arrays checked',
            limits='Not full UpdateData, ARM64, disease or phase transition support')
args.result.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(f'Mixed postprocess: PASS ({len(rows)} original scenarios)')
