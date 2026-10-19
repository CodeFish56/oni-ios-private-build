"""Check original 744825 directional pressure damage against reconstructed C++."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
DLL_SHA='031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727'
def sha(b):return hashlib.sha256(b).hexdigest()
a=argparse.ArgumentParser(description=__doc__)
for key in ('probe','fixtures','elements','output','result'):a.add_argument('--'+key,required=True,type=Path)
args=a.parse_args();args.output.mkdir(parents=True,exist_ok=True)
rows=[]
for fixture in sorted(args.fixtures.iterdir()):
    if not (fixture/'report.json').is_file():continue
    report_bytes=(fixture/'report.json').read_bytes();r=json.loads(report_bytes)
    assert r['dll_sha256']==DLL_SHA and r['observed_headless']==1 and r['observed_radiation']==0
    assert r['elements_sha256']==sha(args.elements.read_bytes()) and r['new_game_frames_submitted']==0 and r['complete']
    assert r['native_entry_rva']=='0x4C4E0' and r['stages'][-1]=='Shutdown: returned'
    before,after=r['phases']['before'],r['phases']['after']
    for phase in ('before','after'):
        for key,info in r['phases'][phase]['arrays'].items():
            b=(fixture/f'{phase}.{key}.bin').read_bytes()
            assert len(b)==info['bytes'] and sha(b)==info['sha256'],(fixture.name,phase,key)
    w,h=r['sim_width'],r['sim_height'];case=r['case'];offset={'below':-w,'left':-1,'right':1,'above':w}[case['Direction']]
    output=args.output/fixture.name
    subprocess.run([str(args.probe.resolve()),str(args.elements),str(fixture),str(output),str(w),str(h),str(r['center_sim_cell']),str(case['Pressure']),str(offset)],check=True,timeout=20)
    state=json.loads((output/'state.json').read_bytes())
    assert state['broke']==bool(r['native_return']),(fixture.name,'return',state,r['native_return'])
    expected=(fixture/'after.events.worldDamageInfo.bin').read_bytes()
    actual=(output/'after.events.worldDamageInfo.bin').read_bytes()
    assert expected==actual,(fixture.name,'damage bytes',expected.hex(),actual.hex())
    assert len(actual)==8*state['eventCount']
    assert before['seed']==after['seed'] and before['displacementDirection']==after['displacementDirection']
    for key in before['arrays']:
        if not key.startswith('events.'):
            assert (fixture/f'before.{key}.bin').read_bytes()==(fixture/f'after.{key}.bin').read_bytes(),(fixture.name,key,'unexpected original mutation')
    for key in ('elementIdx','temperature','mass','properties','insulation','strengthInfo','radiation','diseaseIdx','diseaseCount'):
        assert (output/f'after.updated.{key}.bin').read_bytes()==(fixture/f'after.updated.{key}.bin').read_bytes(),(fixture.name,key)
    for key in ('flow','timers'):
        assert (output/f'after.{key}.bin').read_bytes()==(fixture/f'after.{key}.bin').read_bytes()
    for name in after['event_vectors']:
        assert not (fixture/f'before.events.{name}.bin').read_bytes()
        if name!='worldDamageInfo':assert not (fixture/f'after.events.{name}.bin').read_bytes(),(fixture.name,name)
    rows.append(dict(case=fixture.name,report_sha256=sha(report_bytes),damage_sha256=sha(actual),native_return=r['native_return'],event_count=state['eventCount'],passed=True))
assert len(rows)==456,len(rows)
result=dict(all_passed=True,original_dll_sha256=DLL_SHA,probe_sha256=sha(args.probe.read_bytes()),cases=rows,
    scope='directional DoPressureBreak: exact boolean, all raw ordered WorldDamageInfo bytes; unchanged 11 grids, flow, timer, accumulated flow, seed/direction and all other 19 events',
    limits='Not full PostProcessCell or UpdateData, managed damage/destruction, ARM64 or device validation')
args.result.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(f'Pressure break: PASS ({len(rows)} original scenarios; {sum(x["event_count"] for x in rows)} events)')
