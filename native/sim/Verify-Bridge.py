"""Exercise the compiled C ABI against independently captured original-engine outputs."""

import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct


ARRAYS = {
    "elementIdx": (4, 2), "temperature": (12, 4), "mass": (20, 4),
    "properties": (28, 1), "insulation": (36, 1), "strengthInfo": (44, 1),
    "radiation": (52, 4), "diseaseIdx": (60, 1), "diseaseCount": (68, 4),
    "backwallElement": (76, 2), "backwallMass": (84, 4), "backwallTemperature": (92, 4),
    "accumulatedFlow": (448, 4), "propertyTextureFlow": (456, 8),
    "propertyTextureLiquid": (464, 4), "propertyTextureLiquidData": (472, 4),
    "propertyTextureMaterialData": (480, 4), "propertyTextureExposedToSunlight": (488, 1),
}


class Bridge:
    def __init__(self, path: Path):
        self.library = c.CDLL(str(path.resolve()))
        signatures = {
            "GetCapabilities": (c.c_uint64, []), "GetLastStatus": (c.c_int32, []),
            "GetLastError": (c.c_char_p, []), "Initialize": (None, [c.c_void_p]),
            "Shutdown": (None, []), "HandleMessage": (c.c_void_p, [c.c_int32, c.c_int32, c.c_void_p]),
            "HandleMessages": (c.c_void_p, [c.c_int32, c.c_int32, c.c_int32, c.c_void_p]),
            "BeginSave": (c.c_void_p, [c.POINTER(c.c_int32), c.c_int32, c.c_int32]), "EndSave": (None, []),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.library, "OniSim_" + name)
            function.restype, function.argtypes = result, arguments
            setattr(self, name, function)

    def check(self, expected=0):
        status = self.GetLastStatus()
        assert status == expected, (status, expected, self.GetLastError().decode("utf-8"))
        if status:
            assert self.GetLastError()

    def send(self, message: int, data=b"", expected=0):
        payload = c.create_string_buffer(data) if data else None
        result = self.HandleMessage(message, len(data), payload)
        self.check(expected)
        return result

    def save(self):
        size = c.c_int32(-1)
        pointer = self.BeginSave(c.byref(size), 0, 0)
        self.check()
        assert pointer and size.value > 29
        result = c.string_at(pointer, size.value)
        self.EndSave()
        self.check()
        return result


def compare_snapshot(pointer, fixture: Path, phase: str, cells: int, frames: int):
    assert pointer, f"{phase}: missing GameDataUpdate pointer"
    descriptor = c.string_at(pointer, 496)
    assert struct.unpack_from("<i", descriptor)[0] == frames
    for index in range(29):
        assert struct.unpack_from("<i", descriptor, 100 + index * 12)[0] == 0, "Unexpected initial event count"
    rows = {}
    for name, (offset, size) in ARRAYS.items():
        address = struct.unpack_from("<Q", descriptor, offset)[0]
        assert address, f"{phase}: missing {name} buffer"
        actual = c.string_at(address, cells * size)
        expected = (fixture / f"{phase}.{name}.bin").read_bytes()
        assert len(expected) == len(actual), (phase, name, "length")
        if actual != expected:
            differences = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
            raise AssertionError((fixture.name, phase, name, len(differences), "first byte", differences[0],
                                  "actual", actual[differences[0]], "expected", expected[differences[0]]))
        rows[name] = hashlib.sha256(actual).hexdigest()
    return rows


def run_fixture(bridge: Bridge, fixture: Path, elements: bytes, diseases: bytes):
    report = json.loads((fixture / "report.json").read_text(encoding="utf-8"))
    assert hashlib.sha256(elements).hexdigest() == report["elements_sha256"]
    assert hashlib.sha256(diseases).hexdigest() == report["diseases_sha256"]
    load_world = report.get("wire_input", {}).get("mode") == "load_world"
    payload = (fixture / ("load-world.bin" if load_world else "initialize-from-cells.bin")).read_bytes()
    assert hashlib.sha256(payload).hexdigest() == report["wire_sha256"]
    width, height = report["width"], report["height"]
    cells = width * height
    bridge.Initialize(None)
    bridge.check()
    bridge.send(1108437482, b"bad", expected=2)
    malformed_elements = bytearray(elements)
    struct.pack_into("<ff", malformed_elements, 4 + 56, 1e30, 1e30)
    bridge.send(1108437482, bytes(malformed_elements), expected=2)
    bridge.send(1108437482, elements)
    if load_world:
        bridge.send(1092408308, struct.pack("<iiBB", width, height, bool(report["radiation"]), 1))
        bridge.send(-895846551, struct.pack("<iiiii", 1, 0, 0, width, height))
        bridge.send(825301935, diseases)
        bridge.send(-1836204275)
        bridge.send(-931446686, expected=1)
        bridge.send(-672538170, payload[:20], expected=2)
        malformed = bytearray(payload)
        struct.pack_into("<f", malformed, 33, float("nan"))
        bridge.send(-672538170, bytes(malformed), expected=4)
        malformed = bytearray(payload)
        malformed[28] = 1
        bridge.send(-672538170, bytes(malformed), expected=4)
        assert bridge.send(-672538170, payload)
    else:
        bridge.send(825301935, diseases)
        bridge.send(2062421945, payload[:-1], expected=2)
        malformed = bytearray(payload)
        malformed[13] = 0
        bridge.send(2062421945, bytes(malformed), expected=4)
        assert bridge.send(2062421945, payload) == cells
        if report.get("world_offsets_defined"):
            bridge.send(-895846551, struct.pack("<iiiii", 1, 0, 0, width, height))
    start = bridge.send(-931446686)
    start_arrays = compare_snapshot(start, fixture, "start", cells, 0)
    frames = report["phases"]["prepare"]["numFramesProcessed"]
    if frames:
        assert frames == 1
        bridge.send(-775326397, struct.pack("<fiiiiff", 0, 0, 0, width, height, 0, 0))
    prepared = bridge.send(1078620451, bytes([255]) * cells)
    prepared_arrays = compare_snapshot(prepared, fixture, "prepare", cells, frames)
    saved = bridge.save()
    expected_save = (fixture / "original-save.bin").read_bytes()
    assert saved == expected_save, (fixture.name, "native save differs from original")
    # Unsupported physics must fail explicitly, preserve the world and never
    # claim a processed frame or silently advance an incomplete simulation.
    bridge.send(-775326397, struct.pack("<fiiiiff", .2, 0, 0, width, height, 0, 0), expected=4)
    assert bridge.save() == saved
    bridge.send(123456789, expected=3)
    assert bridge.save() == saved
    bridge.send(1078620451, b"short", expected=2)
    assert bridge.save() == saved
    # Reject the whole batch, including its valid first entry.
    invalid_batch = c.create_string_buffer(struct.pack("<ifif", 0, .25, cells, .75))
    bridge.HandleMessages(-898773121, 8, 2, invalid_batch)
    bridge.check(2)
    bridge.send(-775326397, struct.pack("<fiiiiff", 0, 0, 0, width, height, 0, 0))
    compare_snapshot(bridge.send(1078620451, bytes([255]) * cells), fixture, "prepare", cells, 1)
    bridge.send(-898773121, struct.pack("<if", 0, .5))
    bridge.send(1593243982, struct.pack("<if", 1, 33.9))
    bridge.send(-775326397, struct.pack("<fiiiiff", 0, 0, 0, width, height, 0, 0))
    changed = bridge.send(1078620451, bytes([255]) * cells)
    descriptor = c.string_at(changed, 496)
    insulation = struct.unpack_from("<Q", descriptor, 36)[0]
    strength = struct.unpack_from("<Q", descriptor, 44)[0]
    assert c.string_at(insulation, 1) == bytes([127])
    assert c.string_at(strength + 1, 1) == bytes([33])
    assert not bridge.BeginSave(None, 0, 0)
    bridge.check(2)
    bridge.Shutdown()
    bridge.check()
    bridge.send(-931446686, expected=1)
    return {"case": fixture.name, "cells": cells, "frames": frames,
            "start_arrays": start_arrays, "prepare_arrays": prepared_arrays,
            "save_sha256": hashlib.sha256(saved).hexdigest(), "passed": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--fixtures", required=True, type=Path)
    parser.add_argument("--elements", required=True, type=Path)
    parser.add_argument("--diseases", required=True, type=Path)
    parser.add_argument("--result", type=Path)
    args = parser.parse_args()
    bridge = Bridge(args.library)
    assert bridge.GetCapabilities() & ((1 << 16) | (1 << 17) | (1 << 18)) == 0
    rows = []
    for fixture in sorted(args.fixtures.iterdir()):
        if not (fixture / "report.json").is_file():
            continue
        row = run_fixture(bridge, fixture, args.elements.read_bytes(), args.diseases.read_bytes())
        rows.append(row)
        print(f"{fixture.name}: PASS ({row['cells']} cells, 18 arrays x 2 snapshots, save byte match)", flush=True)
    assert rows
    result = {"cases": rows, "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
              "capabilities": bridge.GetCapabilities(), "all_passed": True,
              "limits": "Compiled C ABI tested against original headless initialization, v15 load and zero-time fixtures; not positive-time physics or iPhone execution."}
    if args.result:
        args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
