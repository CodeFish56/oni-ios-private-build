"""Compare the real Water/Gas UpdateLiquid module to exact retained original calls."""

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


def equal(actual, expected, context):
    if actual != expected:
        first = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b),
                     min(len(actual), len(expected)))
        raise AssertionError(f"{context}: first differing byte {first}; "
                             f"actual={actual[first:first+16].hex()} "
                             f"original={expected[first:first+16].hex()}; "
                             f"lengths {len(actual)}/{len(expected)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("probe", "fixtures", "elements", "output", "result"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    suite_bytes = (args.fixtures / "suite.json").read_bytes()
    suite = json.loads(suite_bytes)
    assert suite["build"] == "U59-744825-V"
    assert suite["failed"] == 0 and suite["completed"] == len(suite["cases"]) > 0
    names = [item["name"] for item in suite["cases"]]
    assert len(set(names)) == len(names)
    elements_sha = sha(args.elements.read_bytes())
    rows = []
    for item in suite["cases"]:
        assert item["exit_code"] == 0
        assert Path(item["name"]).name == item["name"]
        fixture = args.fixtures / item["name"]
        raw_report = (fixture / "report.json").read_bytes()
        assert sha(raw_report) == item["report_sha256"], (item["name"], "suite report SHA")
        report = json.loads(raw_report)
        assert report["case"]["Name"] == item["name"] and report["complete"] is True
        assert report["dll_sha256"] == DLL_SHA and report["elements_sha256"] == elements_sha
        assert report["observed_headless"] == 1 and report["observed_radiation"] == 0
        assert report["new_game_frames_submitted"] == 0 and report["native_entry_rva"] == "0x498B0"
        assert report["stages"][-1] == "Shutdown: returned"
        for phase in ("before", "after"):
            # Validate every retained raw array, including fields outside the
            # candidate's ownership, against its original-runtime report.
            for name, metadata in report["phases"][phase]["arrays"].items():
                data = (fixture / f"{phase}.{name}.bin").read_bytes()
                assert len(data) == metadata["bytes"] and sha(data) == metadata["sha256"]
        width, height = report["sim_width"], report["sim_height"]
        cell = report["center_sim_cell"]
        before, after = report["phases"]["before"], report["phases"]["after"]
        tick = report["case"]["Tick"]
        assert tick == before["tick"] == after["tick"]
        output = args.output / fixture.name
        hashes = {}
        try:
            subprocess.run([str(args.probe.resolve()), str(args.elements), str(fixture), str(output),
                            str(width), str(height), str(cell), str(tick)], check=True, timeout=20,
                           capture_output=True, text=True)
            for name in [view + "." + field for view in ("source", "updated") for field in FIELDS] + ["flow", "timers"]:
                actual = (output / f"after.{name}.bin").read_bytes()
                expected = (fixture / f"after.{name}.bin").read_bytes()
                equal(actual, expected, f"{fixture.name}: {name}")
                hashes[name] = sha(actual)
            state = json.loads((output / "state.json").read_bytes())
            for key in ("seed", "displacementDirection", "iterateDirection"):
                assert before[key] == after[key], (fixture.name, key, "original mutated unexpectedly")
            for field in FIELDS:
                equal((output / f"after.source.{field}.bin").read_bytes(),
                      (fixture / f"before.source.{field}.bin").read_bytes(), fixture.name + ": source immutable")
            for field in ("diseaseInfestationTickCount", "diseaseGrowthAccumulatedError"):
                for view in ("source", "updated"):
                    equal((fixture / f"after.{view}.{field}.bin").read_bytes(),
                          (fixture / f"before.{view}.{field}.bin").read_bytes(), fixture.name + ": original unmodeled unchanged")
            equal((fixture / "after.accumulatedFlow.bin").read_bytes(),
                  (fixture / "before.accumulatedFlow.bin").read_bytes(), fixture.name + ": accumulatedFlow")
            changes = (output / "after.changes.sim.bin").read_bytes()
            assert len(changes) == 4 * state["changeCount"]
            events = b"".join(struct.pack("<iHH", (s // width - 1) * (width - 2) + s % width - 1, 65535, 65535)
                              for (s,) in struct.iter_unpack("<I", changes))
            equal(events, (fixture / "after.events.substanceChangeInfo.bin").read_bytes(), fixture.name + ": raw event order")
            assert not (fixture / "before.events.substanceChangeInfo.bin").read_bytes()
            for name in after["event_vectors"]:
                if name != "substanceChangeInfo":
                    assert not (fixture / f"after.events.{name}.bin").read_bytes(), (fixture.name, name)
            hashes["events.substanceChangeInfo"] = sha(events)
            rows.append({"case": fixture.name, "report_sha256": sha(raw_report),
                         "arrays_sha256": hashes, "state": state, "passed": True})
        except (AssertionError, subprocess.SubprocessError, OSError) as error:
            details = str(error)
            if isinstance(error, subprocess.CalledProcessError):
                details += ": " + error.stdout + error.stderr
            print(details, flush=True)
            rows.append({"case": fixture.name, "report_sha256": sha(raw_report),
                         "passed": False, "error": details})
    result = {"all_passed": all(row["passed"] for row in rows),
              "original_dll_sha256": DLL_SHA, "original_suite_sha256": sha(suite_bytes),
              "probe_sha256": sha(args.probe.read_bytes()), "case_count": len(rows), "cases": rows,
              "scope": "9 source and 9 updated cell grids, flow, timers, raw substance event order; source immutable; original seed/directions unchanged; original unmodeled fields unchanged",
              "not_implemented_by_candidate": ["disease infestation/error grids", "accumulatedFlow storage", "other SimEvents streams", "RNG state or iteration directions (not exposed to this module)"],
              "limits": "Single original UpdateLiquid calls in a headless fixture; not full UpdateData, the C ABI, ARM64 or an iPhone run"}
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    passed = sum(row["passed"] for row in rows)
    print(f"Water/Gas flow: {passed}/{len(rows)} original single-cell scenarios passed", flush=True)
    return 0 if result["all_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
