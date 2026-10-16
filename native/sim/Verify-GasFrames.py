"""Compare complete restricted positive-frame C ABI output to original runs."""

import argparse
import ctypes as c
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location("bridge_verifier", Path(__file__).with_name("Verify-Bridge.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
EVENTS = {"solidInfo": (100, 8), "liquidChangeInfo": (112, 4),
          "solidSubstanceChangeInfo": (124, 4), "substanceChangeInfo": (136, 8)}


class Diagnostics(c.Structure):
    _fields_ = [("randomSeed", c.c_uint32), ("iterateDirection", c.c_int32),
                ("displacementDirection", c.c_int32), ("carrySeconds", c.c_float), ("physicsSteps", c.c_uint64)]


def diagnostics(bridge):
    state = Diagnostics()
    bridge.library.OniSim_GetDiagnostics(c.byref(state))
    bridge.check()
    return {name: getattr(state, name) for name, _ in Diagnostics._fields_}


def snapshot(pointer, fixture, phase, report):
    assert pointer, (phase, "missing descriptor")
    cells = report["width"] * report["height"]
    descriptor = c.string_at(pointer, 496)
    assert struct.unpack_from("<i", descriptor)[0] == report["phases"][phase]["numFramesProcessed"]
    digests = {}
    for name, (offset, size) in module.ARRAYS.items():
        address = struct.unpack_from("<Q", descriptor, offset)[0]
        assert address
        actual = c.string_at(address, cells * size)
        expected = (fixture / f"{phase}.{name}.bin").read_bytes()
        if actual != expected:
            differences = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
            if name == "propertyTextureFlow":
                print([(i, struct.unpack_from("<f", actual, i * 4)[0], struct.unpack_from("<f", expected, i * 4)[0])
                       for i in range(len(actual) // 4) if actual[i*4:i*4+4] != expected[i*4:i*4+4]][:12], flush=True)
            raise AssertionError((fixture.name, phase, name, len(differences), differences[:5]))
        digests[name] = hashlib.sha256(actual).hexdigest()
    for offset in range(100, 448, 12):
        expected = report["phases"][phase]["event_counts"][hex(offset)]
        actual = struct.unpack_from("<i", descriptor, offset)[0]
        assert actual == expected, (fixture.name, phase, hex(offset), actual, expected)
    for name, (offset, size) in EVENTS.items():
        count = struct.unpack_from("<i", descriptor, offset)[0]
        address = struct.unpack_from("<Q", descriptor, offset + 4)[0]
        actual = c.string_at(address, count * size) if count else b""
        expected = (fixture / f"{phase}.{name}.bin").read_bytes()
        assert actual == expected, (fixture.name, phase, name, "event bytes differ")
        digests[name] = hashlib.sha256(actual).hexdigest()
    return digests


def run(bridge, fixture, elements, diseases):
    report = json.loads((fixture / "report.json").read_text(encoding="utf-8"))
    assert report["dll_sha256"] == "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727"
    assert hashlib.sha256(elements).hexdigest() == report["elements_sha256"]
    assert hashlib.sha256(diseases).hexdigest() == report["diseases_sha256"]
    payload = (fixture / "initialize-from-cells.bin").read_bytes()
    assert hashlib.sha256(payload).hexdigest() == report["wire_sha256"]
    bridge.Initialize(None)
    bridge.check()
    bridge.send(1108437482, elements)
    bridge.send(825301935, diseases)
    bridge.send(2062421945, payload)
    if report.get("world_offsets_defined"):
        bridge.send(-895846551, (fixture / "world-offsets.bin").read_bytes())
    rows = [{"phase": "start", "arrays": snapshot(bridge.send(-931446686), fixture, "start", report)}]
    visible = bytes([255]) * (report["width"] * report["height"])
    rows.append({"phase": "prepare", "arrays": snapshot(bridge.send(1078620451, visible), fixture, "prepare", report)})
    initial_mass = sum(struct.unpack_from("<f", payload, 26 + 16 * i)[0] for i in range(len(visible)))
    for index in range(len(report["step_seconds"])):
        phase = f"frame{index + 1:03d}"
        frame = (fixture / f"{phase}.input.bin").read_bytes()
        bridge.send(-775326397, frame)
        before = bridge.save()
        state = diagnostics(bridge)
        # A second pending positive frame is rejected, not partially consumed.
        if struct.unpack_from("<f", frame)[0] > 0:
            bridge.send(-775326397, frame, expected=1)
            zero = bytearray(frame)
            struct.pack_into("<f", zero, 0, 0)
            bridge.send(-775326397, bytes(zero), expected=1)
            assert bridge.save() == before and diagnostics(bridge) == state
        arrays = snapshot(bridge.send(1078620451, visible), fixture, phase, report)
        assert bridge.save() == (fixture / f"{phase}.save.bin").read_bytes(), (fixture.name, phase, "save differs")
        sim_width, sim_height = struct.unpack_from("<ii", before, 12)
        mass = sum(struct.unpack_from("<f", before, 37 + 16 * (y * sim_width + x))[0]
                   for y in range(1, sim_height - 1) for x in range(1, sim_width - 1))
        original_mass = sum(struct.unpack("<" + "f" * len(visible), (fixture / f"{phase}.mass.bin").read_bytes()))
        assert mass == original_mass, (fixture.name, phase, "original mass differs", mass, original_mass)
        expected = report[phase + "_sim_flags"]
        for name, original in (("randomSeed", "seed"), ("iterateDirection", "iterateDirection"),
                               ("displacementDirection", "displacementDirection")):
            assert state[name] == expected[original], (fixture.name, phase, name, state[name], expected[original])
        rows.append({"phase": phase, "arrays": arrays, "state": state, "mass": mass, "initial_mass": initial_mass,
                     "original_mass_change": original_mass - initial_mass,
                     "save_sha256": hashlib.sha256(before).hexdigest()})
    assert bridge.save() == (fixture / "original-save.bin").read_bytes()
    bridge.Shutdown()
    bridge.check()
    return {"case": fixture.name, "cells": report["width"] * report["height"],
            "frames": len(report["step_seconds"]), "snapshots": rows, "passed": True}


def rejected_features(bridge, fixture, elements, diseases):
    base = (fixture / "initialize-from-cells.bin").read_bytes()
    width, height = struct.unpack_from("<ii", base)
    count = width * height
    gas = next(i for i in range(count) if struct.unpack_from("<f", base, 26 + i * 16)[0] > 0)
    indices = {struct.unpack_from("<I", elements, 4 + i * 164)[0]: i for i in range(struct.unpack_from("<I", elements)[0])}
    mutations = {
        "radiation": lambda p: p.__setitem__(12, 1),
        "temperature_exchange": lambda p: struct.pack_into("<f", p, 22 + gas * 16, 310),
        "unsupported_gas": lambda p: struct.pack_into("<H6xff", p, 14, next(i for i in range(len(indices))
            if elements[14+i*164] & 3 == 1 and struct.unpack_from("<I", elements, 4+i*164)[0]
            not in (0xA4E0B340, 0x74DC00EF) and struct.unpack_from("<f", elements, 60+i*164)[0] < 300
            < struct.unpack_from("<f", elements, 64+i*164)[0]), 300, 1),
        "liquid": lambda p: struct.pack_into("<H6xff", p, 14, indices[1836671383], 300, 1),
        "properties": lambda p: p.__setitem__(16, 1),
        "disease": lambda p: struct.pack_into("<B3xi", p, 14 + count * 16, 0, 1),
        "backwall": lambda p: struct.pack_into("<H2xff", p, 14 + count * 28, indices[(-105943486) & 0xffffffff], 1, 300),
    }
    rows = []
    for name, mutate in mutations.items():
        payload = bytearray(base)
        mutate(payload)
        bridge.Initialize(None)
        bridge.send(1108437482, elements)
        bridge.send(825301935, diseases)
        bridge.send(2062421945, bytes(payload))
        bridge.send(-931446686)
        bridge.send(1078620451, bytes([255]) * count)
        before, state = bridge.save(), diagnostics(bridge)
        bridge.send(-775326397, struct.pack("<fiiiiff", .2, 0, 0, width, height, 1, 0), expected=4)
        assert bridge.save() == before and diagnostics(bridge) == state, (name, "partial update on failure")
        bridge.Shutdown()
        rows.append({"feature": name, "explicitly_rejected": True, "world_and_rng_unchanged": True})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "fixtures", "elements", "diseases", "result"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    bridge = module.Bridge(args.library)
    bridge.library.OniSim_GetDiagnostics.argtypes = [c.POINTER(Diagnostics)]
    bridge.library.OniSim_GetDiagnostics.restype = None
    rows = []
    for fixture in sorted(args.fixtures.iterdir()):
        if not (fixture / "report.json").is_file():
            continue
        row = run(bridge, fixture, args.elements.read_bytes(), args.diseases.read_bytes())
        rows.append(row)
        print(f"{fixture.name}: PASS ({row['frames']} frames, 18 arrays, 29 event counts, event bytes, RNG and saves)", flush=True)
    assert rows
    denied = rejected_features(bridge, args.fixtures / "oxygen-single", args.elements.read_bytes(), args.diseases.read_bytes())
    result = {"all_passed": True, "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
              "cases": rows, "rejected_features": denied, "capabilities": bridge.GetCapabilities(),
              "limits": "Restricted non-radiating O2/CO2/vacuum fixtures; not general physics, world settlement or iPhone execution"}
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
