"""Summarize the isolated original-DLL worldgen initialization output."""
import argparse
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("case_dir", type=Path)
parser.add_argument("--capture-dir", type=Path, help="Required to resolve hashes in --load-world cases")
args = parser.parse_args()
root = args.case_dir
report = json.loads((root / "report.json").read_text(encoding="utf-8"))
mode = report.get("wire_input", {}).get("mode", "initialize_from_cells")
wire = (root / report.get("wire_input", {}).get("file", "initialize-from-cells.bin")).read_bytes()
w, h = report["width"], report["height"]
n = w*h
fields = {"elementIdx": ("H", 0, 16, 14), "properties": ("B", 2, 16, 14),
          "insulation": ("B", 3, 16, 14), "strengthInfo": ("B", 4, 16, 14),
          "temperature": ("f", 8, 16, 14), "mass": ("f", 12, 16, 14),
          "diseaseIdx": ("B", 0, 12, 14+16*n), "diseaseCount": ("i", 4, 12, 14+16*n),
          "backwallElement": ("H", 0, 12, 14+28*n), "backwallMass": ("f", 4, 12, 14+28*n),
          "backwallTemperature": ("f", 8, 12, 14+28*n)}
expected = {}
if mode == "load_world":
    if args.capture_dir is None:
        parser.error("--capture-dir is required for load_world")
    element_table=(args.capture_dir / "elements.bin").read_bytes()
    disease_table=(args.capture_dir / "diseases.bin").read_bytes()
    ec=struct.unpack_from("<I",element_table)[0]
    elements={struct.unpack_from("<I",element_table,4+i*164)[0]:i for i in range(ec)}
    diseases={}
    at=8
    for i in range(struct.unpack_from("<I",disease_table)[0]):
        length=struct.unpack_from("<I",disease_table,at)[0]
        at+=4+length
        diseases[struct.unpack_from("<I",disease_table,at)[0]]=i
        at+=76+ec*29
    full_n=(w+2)*(h+2)
    dstart=29+16*full_n
    bstart=dstart+8*full_n
    values={k:[] for k in fields}
    for y in range(1,h+1):
        for x in range(1,w+1):
            i=y*(w+2)+x
            e,t,m,_=struct.unpack_from("<Ifff",wire,29+16*i)
            d,dc=struct.unpack_from("<Ii",wire,dstart+8*i)
            b,bm,bt=struct.unpack_from("<Iff",wire,bstart+12*i)
            for name,value in {"elementIdx":elements[e],"temperature":t,"mass":m,"properties":0,
                               "insulation":255,"strengthInfo":132,"diseaseIdx":255 if d==0 else diseases[d],
                               "diseaseCount":dc,"backwallElement":elements[b],"backwallMass":bm,"backwallTemperature":bt}.items():
                values[name].append(value)
    expected={name:struct.pack("<"+fields[name][0]*n,*values[name]) for name in fields}
else:
    for name, (fmt, at, stride, start) in fields.items():
        expected[name] = b"".join(wire[start+i*stride+at:start+i*stride+at+struct.calcsize(fmt)] for i in range(n))
summary = {"dimensions": [w,h], "mode": mode, "wire_bytes": len(wire), "phases": {}}
for phase in report["phases"]:
    row = {}
    for name, (fmt, at, stride, start) in fields.items():
        wanted = expected[name]
        actual = (root / f"{phase}.{name}.bin").read_bytes()
        row[name] = {"matches_wire": wanted == actual,
                     "mismatched_values": sum(a!=b for a,b in zip(struct.iter_unpack("<"+fmt,wanted),struct.iter_unpack("<"+fmt,actual))),
                     "sample": [v[0] for v in list(struct.iter_unpack("<"+fmt,actual))[:12]]}
    info = report["phases"][phase]
    row["numFramesProcessed"] = info["numFramesProcessed"]
    row["nonzero_event_counts"] = {k:v for k,v in info["event_counts"].items() if v}
    if w <= 32 and h <= 32:
        internal = (root / f"{phase}.internal.elementIdx.bin").read_bytes()
        values = [v[0] for v in struct.iter_unpack("<H",internal)]
        row["boundary_element_rows"] = [[values[y*(w+2)+x] if (x==0 or y==0 or x==w+1 or y==h+1) else None for x in range(w+2)] for y in range(h+2)]
    summary["phases"][phase] = row
(root / "comparison.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
print(json.dumps({"dimensions":[w,h],"wire_bytes":len(wire),"phases":{p:{"differences_from_wire":{k:v["mismatched_values"] for k,v in row.items() if isinstance(v,dict) and "mismatched_values" in v and v["mismatched_values"]},"frames":row["numFramesProcessed"]} for p,row in summary["phases"].items()}}))
