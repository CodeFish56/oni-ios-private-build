"""Compare the real state transition and partial no-op guard to retained original full/partial phase transition runs."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

FIELDS = ("elementIdx", "temperature", "mass", "properties", "insulation",
          "strengthInfo", "radiation", "diseaseIdx", "diseaseCount")
DLL_SHA = "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("probe", "fixtures", "elements", "output", "result"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for fixture in sorted(args.fixtures.iterdir()):
        if not (fixture / "report.json").is_file():
            continue
        raw_report = (fixture / "report.json").read_bytes()
        report = json.loads(raw_report)
        assert report["dll_sha256"] == DLL_SHA
        assert report["observed_headless"] == 1 and report["observed_radiation"] == 0
        assert report["elements_sha256"] == sha(args.elements.read_bytes())
        assert report["new_game_frames_submitted"] == 0
        assert report["stages"][-1] == "Shutdown: returned"
        for phase in ("before", "after"):
            for name, item in report["phases"][phase]["arrays"].items():
                data = (fixture / f"{phase}.{name}.bin").read_bytes()
                assert len(data) == item["bytes"] and sha(data) == item["sha256"]
        width, height = report["sim_width"], report["sim_height"]
        cell = report["center_sim_cell"]
        before, after = report["phases"]["before"], report["phases"]["after"]
        output = args.output / fixture.name
        subprocess.run([str(args.probe.resolve()), str(args.elements), str(fixture), str(output),
                        str(width), str(height), str(cell), str(int(report["case"]["Partial"]))], check=True, timeout=20)
        state = json.loads((output / "state.json").read_bytes())
        is_partial = report["case"]["Partial"]
        if is_partial:
            assert state["partialNoop"] == report["case"]["ExpectedNoop"], fixture.name
            if not state["partialNoop"]:
                rows.append({"case": fixture.name,"report_sha256": sha(raw_report),
                             "guard_rejects_nontrivial_partial_path": True,"passed": True})
                continue
        else:
            assert state["transitioned"] == report["native_transition_result"], fixture.name
        hashes = {}
        for name in [view + "." + field for view in ("source", "updated") for field in FIELDS] + ["flow", "timers"]:
            actual = (output / f"after.{name}.bin").read_bytes()
            expected = (fixture / f"after.{name}.bin").read_bytes()
            assert actual == expected, (fixture.name, name, "byte mismatch")
            hashes[name] = sha(actual)
        state = json.loads((output / "state.json").read_bytes())
        for key in ("seed", "displacementDirection", "iterateDirection"):
            assert before[key] == after[key], (fixture.name, key, "original mutated unexpectedly")
        for field in FIELDS:
            assert (fixture / f"before.source.{field}.bin").read_bytes() == (output / f"after.source.{field}.bin").read_bytes()
        changes = (output / "after.changes.sim.bin").read_bytes()
        assert len(changes) == 4 * state["changeCount"]
        events = b"".join(struct.pack("<iHH", (s // width - 1) * (width - 2) + s % width - 1, 65535, 65535)
                          for (s,) in struct.iter_unpack("<I", changes))
        expected = (fixture / "after.events.substanceChangeInfo.bin").read_bytes()
        assert events == expected, (fixture.name, "event order or bytes")
        assert not (fixture / "before.events.substanceChangeInfo.bin").read_bytes()
        for name in after["event_vectors"]:
            if name not in ("substanceChangeInfo", "spawnOreInfo"):
                assert not (fixture / f"after.events.{name}.bin").read_bytes()
        ore = (output / "after.oreSpawns.bin").read_bytes()
        original_ore = (fixture / "after.events.spawnOreInfo.bin").read_bytes()
        def defined_ore(data):
            assert len(data) % 20 == 0
            return b"".join(data[at:at+7]+b"\0"+data[at+8:at+20] for at in range(0,len(data),20))
        assert ore == defined_ore(original_ore), (fixture.name, "defined ore event fields mismatch")
        assert not (fixture / "before.events.spawnOreInfo.bin").read_bytes()
        hashes["events.spawnOreInfo_defined_fields"] = sha(ore)
        hashes["events.substanceChangeInfo"] = sha(events)
        rows.append({"case": fixture.name, "report_sha256": sha(raw_report),
                     "arrays_sha256": hashes, "state": state, "passed": True})
    assert len(rows) == 164, len(rows)
    result = {"all_passed": True, "original_dll_sha256": DLL_SHA,
              "probe_sha256": sha(args.probe.read_bytes()), "cases": rows,
              "case_counts": {"total": len(rows), "full_state": sum("full-" in r["case"] for r in rows),
                              "visible_state": sum(r["case"].startswith("visible-") for r in rows),
                              "byte_compared": sum("arrays_sha256" in r for r in rows),
                              "partial_early_noop": sum(r["case"].startswith("partial-") and "arrays_sha256" in r for r in rows),
                              "partial_guard_rejected": sum(r.get("guard_rejects_nontrivial_partial_path",False) for r in rows)},
              "ore_padding": "Original SpawnOreInfo byte 7 is uninitialized; compare all defined fields after zero normalization; candidate padding is zero",
              "scope": "full transitions and exact partial early-exit scenarios: 9 source and 9 updated owned cell grids, flow, timers, raw substance event order, defined SpawnOreInfo fields (padding byte 7 excluded); original seed/directions remain unchanged; candidate API has no RNG state",
              "not_implemented_by_candidate": ["disease infestation/error grids", "accumulatedFlow storage", "other SimEvents streams", "active partial heat transitions (guard rejects)", "RNG state or iteration directions (not exposed to this module)"],
              "limits": "This is a module probe, not full UpdateData, the C ABI or an iPhone run"}
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Full transitions / partial no-op guard: PASS ({len(rows)} original full/partial transition scenarios)", flush=True)


if __name__ == "__main__":
    main()
