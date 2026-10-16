"""Compare the restricted postprocessor to retained original single-cell runs."""

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
        assert report["elements_sha256"] == sha(args.elements.read_bytes())
        assert report["new_game_frames_submitted"] == 0
        assert report["stages"][-1] == "Shutdown: returned"
        for phase in ("before", "after"):
            for name, item in report["phases"][phase]["arrays"].items():
                data = (fixture / f"{phase}.{name}.bin").read_bytes()
                assert len(data) == item["bytes"] and sha(data) == item["sha256"]
        width, height = report["sim_width"], report["sim_height"]
        cell = report["center_sim_cell"]
        x, y = cell % width - 1, cell // width - 1
        before, after = report["phases"]["before"], report["phases"]["after"]
        output = args.output / fixture.name
        subprocess.run([str(args.probe.resolve()), str(args.elements), str(fixture), str(output),
                        str(width), str(height), str(x), str(y), str(x), str(y),
                        str(before["seed"]), str(before["displacementDirection"])], check=True, timeout=20)
        hashes = {}
        for name in ["updated." + field for field in FIELDS] + ["flow", "timers"]:
            actual = (output / f"after.{name}.bin").read_bytes()
            expected = (fixture / f"after.{name}.bin").read_bytes()
            assert actual == expected, (fixture.name, name, "byte mismatch")
            hashes[name] = sha(actual)
        state = json.loads((output / "state.json").read_bytes())
        assert state["seed"] == after["seed"]
        assert state["displacementDirection"] == after["displacementDirection"]
        seed = before["seed"]
        for _ in range(state["rngCalls"]):
            seed = (seed * 214013 + 2531011) & 0xffffffff
        assert seed == after["seed"]
        changes = (output / "after.changes.sim.bin").read_bytes()
        assert len(changes) == 4 * state["changeCount"]
        events = b"".join(struct.pack("<iHH", (s // width - 1) * (width - 2) + s % width - 1, 65535, 65535)
                          for (s,) in struct.iter_unpack("<I", changes))
        expected = (fixture / "after.events.substanceChangeInfo.bin").read_bytes()
        assert events == expected, (fixture.name, "event order or bytes")
        assert not (fixture / "before.events.substanceChangeInfo.bin").read_bytes()
        for name in after["event_vectors"]:
            if name != "substanceChangeInfo":
                assert not (fixture / f"after.events.{name}.bin").read_bytes()
        hashes["events.substanceChangeInfo"] = sha(events)
        rows.append({"case": fixture.name, "report_sha256": sha(raw_report),
                     "arrays_sha256": hashes, "state": state, "passed": True})
    assert len(rows) == 118, len(rows)
    result = {"all_passed": True, "original_dll_sha256": DLL_SHA,
              "probe_sha256": sha(args.probe.read_bytes()), "cases": rows,
              "scope": "9 owned cell grids, flow, defined timer bytes, raw substance event order, seed/direction and RNG calls; direct single-cell calls",
              "not_implemented_by_candidate": ["disease infestation/error grids"],
              "limits": "This is a module probe, not full UpdateData, the C ABI or an iPhone run"}
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Postprocess: PASS ({len(rows)} original single-cell scenarios)", flush=True)


if __name__ == "__main__":
    main()
