"""Compare the headless unstable-solid branch with original DLL snapshots."""
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
    assert report['observed_diagonal']==int(report['case']['Diagonal'])
    assert report['elements_sha256']==sha(args.elements.read_bytes())
    assert report['new_game_frames_submitted']==0 and report['complete']
    assert report['stages'][-1]=='Shutdown: returned'
    wire=args.elements.read_bytes(); native_post=(fixture/'native-post-table.bin').read_bytes()
    element_count=struct.unpack_from('<I',wire)[0]
    assert len(native_post)==element_count*44
    assert all(wire[4+i*164+44:4+i*164+48]==native_post[i*44+36:i*44+40] for i in range(element_count))
    before,after=report['phases']['before'],report['phases']['after']
    for phase in ('before','after'):
        for name,item in report['phases'][phase]['arrays'].items():
            data=(fixture/f'{phase}.{name}.bin').read_bytes()
            assert len(data)==item['bytes'] and sha(data)==item['sha256']
    width,height=report['sim_width'],report['sim_height']; output=args.output/fixture.name
    subprocess.run([str(args.probe.resolve()),str(args.elements),str(fixture),str(output),str(width),str(height),
                    str(report['center_sim_cell']),str(int(report['case']['Diagonal'])),str(report['case'].get('Steps',1)),str(before['seed']),str(before['displacementDirection']),str(report['case']['Tick'])],check=True,timeout=20)
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
    steps=report['case'].get('Steps',1)
    for step in range(steps if steps>1 else 0):
        phase=f'step-{step}'; native=report['phases'][phase]
        for name in ['updated.'+x for x in FIELDS]+['flow','timers']:
            assert (output/f'{phase}.{name}.bin').read_bytes()==(fixture/f'{phase}.{name}.bin').read_bytes(),(fixture.name,phase,name,'byte mismatch')
        current=json.loads((output/f'{phase}.state.json').read_bytes())
        assert current['seed']==native['seed'] and current['displacementDirection']==native['displacementDirection']
        changes_step=(output/f'{phase}.changes.sim.bin').read_bytes()
        events_step=b''.join(struct.pack('<iHH',(s//width-1)*(width-2)+s%width-1,65535,65535) for (s,) in struct.iter_unpack('<I',changes_step))
        assert events_step==(fixture/f'{phase}.events.substanceChangeInfo.bin').read_bytes(),(fixture.name,phase,'event bytes/order')
        for name in ['source.'+x for x in FIELDS]+['updated.diseaseInfestationTickCount','updated.diseaseGrowthAccumulatedError','accumulatedFlow']:
            assert (fixture/f'before.{name}.bin').read_bytes()==(fixture/f'{phase}.{name}.bin').read_bytes(),(fixture.name,phase,name,'unexpected unmodeled change')
        for name in native['event_vectors']:
            if name!='substanceChangeInfo': assert not (fixture/f'{phase}.events.{name}.bin').read_bytes()
    hashes['events.substanceChangeInfo']=sha(events)
    rows.append(dict(case=fixture.name,steps=steps,report_sha256=sha(raw_report),arrays_sha256=hashes,state=state,passed=True))
assert len(rows)>=191,len(rows)
result=dict(all_passed=True,original_dll_sha256=DLL_SHA,probe_sha256=sha(args.probe.read_bytes()),original_calls=sum(row['steps'] for row in rows),cases=rows,
            scope='single headless unstable-solid branch: 9 owned cell grids, flow, defined timer bytes, raw substance event order, seed/direction/RNG count; unchanged original source and extra arrays checked',
            limits='Not full UpdateData, ARM64, disease, nonheadless UnstableCellInfo or phase transition support')
args.result.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(f'Unstable solid: PASS ({len(rows)} original scenarios)')
