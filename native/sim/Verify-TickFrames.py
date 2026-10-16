"""Check the native uint16 update tick through the compiled C ABI.

Uses the original isolated tick report for constructor/schedule/wrap/save facts.
The candidate has no tick writer: wrapping is reached by real empty-region steps.
"""
import argparse
import ctypes as c
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location("frame_checks", Path(__file__).with_name("Verify-GasFrames.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "fixture", "elements", "diseases", "original", "result"):
        p.add_argument("--" + name, required=True, type=Path)
    a = p.parse_args()
    original_raw = a.original.read_bytes()
    original = json.loads(original_raw)
    assert original["dll_sha256"] == "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727"
    assert original["initialized"] == original["started"] == 0
    assert [row["after"] - row["before"] for row in original["frames"]] == [0, 1, 1, 2, 1, 5, 0]
    assert [row["after"] for row in original["wrapFrames"]] == [65535, 0, 1, 1]
    assert original["save"]["identical_after_tick_edit"]
    assert original["load"]["preLoad"] == original["load"]["postLoad"] == original["load"]["postLoadZero"] == 43210
    b = m.module.Bridge(a.library)
    b.library.OniSim_GetTickCount.argtypes = []
    b.library.OniSim_GetTickCount.restype = c.c_uint32
    b.library.OniSim_GetDiagnostics.argtypes = [c.POINTER(m.Diagnostics)]
    def tick():
        value = b.library.OniSim_GetTickCount()
        b.check()
        return value
    assert b.library.OniSim_GetTickCount() == 0
    b.check(1)
    payload = (a.fixture / "initialize-from-cells.bin").read_bytes()
    width, height = struct.unpack_from("<ii", payload)
    visible = bytes([255]) * (width * height)
    def initialize():
        b.Initialize(None)
        b.check()
        assert tick() == 0
        b.send(1108437482, a.elements.read_bytes())
        b.send(825301935, a.diseases.read_bytes())
        b.send(2062421945, payload)
        b.send(-931446686)
        b.send(1078620451, visible)
        assert tick() == 0
    def frame(seconds, expected_delta, cosmic=0):
        before = tick()
        wire = struct.pack("<fiiiiff", seconds, 0, 0, 0, 0, 1, cosmic)
        b.send(-775326397, wire)
        b.send(1078620451, visible)
        assert tick() == (before + expected_delta) % 65536
    initialize()
    before_save = b.save()
    for row, delta in zip(original["frames"], [0, 1, 1, 2, 1, 5, 0]):
        frame(row["seconds"], delta)
    assert b.save() == before_save
    frame(.2, 1)
    current = m.diagnostics(b)
    saved = b.save()
    bad = struct.pack("<fiiiiff", .2, 0, 0, 0, 0, 1, 1)
    b.send(-775326397, bad, expected=4)
    assert m.diagnostics(b) == current and b.save() == saved
    b.Shutdown()
    b.check()
    initialize()
    for _ in range(655):
        frame(20, 100)
    for _ in range(34):
        frame(.2, 1)
    assert tick() == 65534
    sequence = []
    for seconds, delta in ((.2, 1), (.2, 1), (.2, 1), (0, 0)):
        frame(seconds, delta)
        sequence.append(tick())
    assert sequence == [65535, 0, 1, 1]
    assert m.diagnostics(b)["physicsSteps"] == 65537
    assert b.save() == before_save
    b.Shutdown()
    b.check()
    assert b.library.OniSim_GetTickCount() == 0
    b.check(1)
    a.result.write_text(json.dumps({"passed": True, "steps_to_wrap": 65537,
        "observed_wrap_sequence": sequence, "empty_region_saves_unchanged": True,
        "failure_rolls_back_tick": True, "original_report_sha256": hashlib.sha256(original_raw).hexdigest(),
        "library_sha256": hashlib.sha256(a.library.read_bytes()).hexdigest(),
        "limits": "Candidate startup load only; positive-time load has no RNG seed. Original existing-state Load tick retention is evidence for future implementation."}, indent=2) + "\n")
    print("Update tick: PASS (schedule, zero/empty frames, atomic rejection, 65537 real updates and wrap)", flush=True)


if __name__ == "__main__":
    main()
