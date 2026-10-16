"""Compare the compiled real timer module with retained original-DLL calls.

There is deliberately no timer/RNG implementation in this verifier. Expected
bytes come only from TimerReference's SHA-guarded original 744825 RVA calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    args = parser.parse_args()
    raw = args.reference.read_bytes()
    reference = json.loads(raw)
    expected_hash = "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727"
    assert reference["dll_sha256"] == expected_hash
    assert reference["entry_rva"] == "0x284D0" and reference["failures"] == 0
    inputs, expected, locations = bytearray(), bytearray(), []
    for case in reference["cases"]:
        steps = case["steps"]
        inputs.extend(struct.pack("<BII", case["initial_timer"], case["seed"], len(steps)))
        for step in steps:
            assert step["pass"]
            expected.extend(struct.pack("<BBI", step["result"], step["timer"], step["seed"]))
            locations.append((case["initial_timer"], case["seed"], step["step"]))
    assert len(locations) == reference["calls"]
    with tempfile.TemporaryDirectory(prefix="oni-timers-") as temp:
        source, destination = Path(temp) / "input.bin", Path(temp) / "actual.bin"
        source.write_bytes(inputs)
        subprocess.run([str(args.probe.resolve()), str(source), str(destination)], check=True)
        actual = destination.read_bytes()
    differences = []
    for index, location in enumerate(locations):
        offset = index * 6
        if actual[offset:offset + 6] != expected[offset:offset + 6]:
            differences.append({"initial_timer": location[0], "initial_seed": location[1],
                "call": location[2], "expected_hex": expected[offset:offset + 6].hex(),
                "actual_hex": actual[offset:offset + 6].hex()})
    report = {"build": reference["build"], "original_dll_sha256": expected_hash,
        "original_entry_rva": reference["entry_rva"], "reference_sha256": digest(raw),
        "probe_sha256": digest(args.probe.read_bytes()), "cases": len(reference["cases"]),
        "calls": len(locations), "input_sha256": digest(inputs),
        "expected_bytes": len(expected), "actual_bytes": len(actual),
        "expected_sha256": digest(expected), "actual_sha256": digest(actual),
        "mismatches": len(differences), "first_mismatches": differences[:8],
        "passed": actual == expected,
        "scope": "Compiled StabilityTimers module vs original direct calls; defined input upper bits compared in full. No full simulation frame or iOS device claim."}
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
