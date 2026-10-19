"""Compare owned partial heat/melt helpers against retained Build 744825 calls."""

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
    parser.add_argument("--allow-incomplete", action="store_true", help="Development only; result explicitly marks partial suite")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for fixture in sorted(args.fixtures.iterdir()):
        if not (fixture / "report.json").is_file():
            continue
        raw_report = (fixture / "report.json").read_bytes()
        report = json.loads(raw_report)
        if not report.get("complete"):
            assert args.allow_incomplete, (fixture.name, "Incomplete original fixture")
            continue
        assert report["dll_sha256"] == DLL_SHA
        assert report["observed_headless"] == 1 and report["observed_radiation"] == 0
        assert report["observed_force_ore"] == 0 and "initial_force_ore_raw" in report
        assert report["elements_sha256"] == sha(args.elements.read_bytes())
        assert report["new_game_frames_submitted"] == 0
        assert report["stages"][-1] == "Shutdown: returned"
        for phase in ("before", "after"):
            for name, item in report["phases"][phase]["arrays"].items():
                data = (fixture / f"{phase}.{name}.bin").read_bytes()
                assert len(data) == item["bytes"] and sha(data) == item["sha256"]
        width, height = report["sim_width"], report["sim_height"]
        case = report["case"]
        before, after = report["phases"]["before"], report["phases"]["after"]
        output = args.output / fixture.name
        subprocess.run([str(args.probe.resolve()), str(args.elements), str(fixture), str(output),
                        str(width), str(height), str(report["center_sim_cell"]), str(case["Neighbour"]),
                        str(int(case["Melt"])), str(case["Tick"]), str(before["seed"]),
                        str(before["displacementDirection"])], check=True, timeout=20)
        state = json.loads((output / "state.json").read_bytes())
        assert state["nativeResult"] == report["native_transition_result"], (fixture.name, "native result mismatch")
        hashes = {}
        for name in [view + "." + field for view in ("source", "updated") for field in FIELDS] + ["flow", "timers"]:
            actual = (output / f"after.{name}.bin").read_bytes()
            expected = (fixture / f"after.{name}.bin").read_bytes()
            if actual != expected:
                differences = [(at, a, b) for at, (a, b) in enumerate(zip(actual, expected)) if a != b]
                raise AssertionError((fixture.name, name, "byte mismatch", differences[:16]))
            hashes[name] = sha(actual)
        for key in ("seed", "displacementDirection", "iterateDirection", "tick"):
            assert before[key] == after[key], (fixture.name, key, "original mutated unexpectedly")
        assert state["seed"] == before["seed"] and state["displacementDirection"] == before["displacementDirection"] and state["rngCalls"] == 0
        assert state["atomicRejectionChecked"]
        for name in before["arrays"]:
            if name.startswith("source.") or name in ("updated.diseaseInfestationTickCount", "updated.diseaseGrowthAccumulatedError", "accumulatedFlow", "visibilityGame"):
                assert (fixture / f"before.{name}.bin").read_bytes() == (fixture / f"after.{name}.bin").read_bytes(), (fixture.name, name, "Unexpected original mutation")
        changes = (output / "after.changes.sim.bin").read_bytes()
        assert len(changes) == 4 * state["changeCount"]
        events = b"".join(struct.pack("<iHH", (s // width - 1) * (width - 2) + s % width - 1, 65535, 65535)
                          for (s,) in struct.iter_unpack("<I", changes))
        expected = (fixture / "after.events.substanceChangeInfo.bin").read_bytes()
        assert events == expected, (fixture.name, "event order or bytes", events.hex(), expected.hex())
        assert not (fixture / "before.events.substanceChangeInfo.bin").read_bytes()
        for name in after["event_vectors"]:
            if name != "substanceChangeInfo":
                assert not (fixture / f"after.events.{name}.bin").read_bytes(), (fixture.name, name)
        hashes["events.substanceChangeInfo"] = sha(events)
        rows.append({"case": fixture.name, "report_sha256": sha(raw_report), "arrays_sha256": hashes,
                     "native_result": state["nativeResult"], "false_with_side_effects": not state["nativeResult"] and bool(changes),
                     "melt": case["Melt"], "state": state, "passed": True})
    if not args.allow_incomplete:
        suite = json.loads((args.fixtures / "suite.json").read_bytes())
        assert suite["failed"] == 0 and suite["completed"] == len(rows), (len(rows), suite["completed"])
        assert len(rows) >= 400
        assert any(r["false_with_side_effects"] for r in rows)
        assert any(r["native_result"] and r["melt"] for r in rows)
        assert any(r["native_result"] and not r["melt"] for r in rows)
    result = {"all_passed": True, "complete_suite": not args.allow_incomplete, "original_dll_sha256": DLL_SHA,
              "probe_sha256": sha(args.probe.read_bytes()), "cases": rows,
              "case_counts": {"total": len(rows), "melt": sum(r["melt"] for r in rows),
                              "true": sum(r["native_result"] for r in rows), "false_with_side_effects": sum(r["false_with_side_effects"] for r in rows)},
              "scope": "9 source and 9 updated grids, flow, timers, raw ordered duplicate substance events, native bool distinct from API success, seed/directions/tick unchanged; exact owned-process forceOre=0 override recorded",
              "limits": "Headless, captured Water/Steam/Ice/O2/CO2/vacuum/stable solids, no disease/radiation/backwall; owned world omits disease infestation/error grids and accumulatedFlow, verified unchanged in original. Module probe, not full UpdateData or ARM/iPhone validation."}
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Partial heat/melt: PASS ({len(rows)} original direct scenarios)", flush=True)


if __name__ == "__main__":
    main()
