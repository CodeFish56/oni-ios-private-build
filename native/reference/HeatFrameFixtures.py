"""Record original positive frames with nonuniform foreground temperatures.

Runs the SHA-checked WorldgenReference in a separate Windows process per case.
No native code is patched; all inputs, grids, events and saves are retained.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("oracle", "game-root", "capture-dir", "source-save", "output-dir"):
        p.add_argument("--" + name, required=True, type=Path)
    a = p.parse_args()
    elements = (a.capture_dir / "elements.bin").read_bytes()
    ids = {struct.unpack_from("<I", elements, 4 + i * 164)[0]: i
           for i in range(struct.unpack_from("<I", elements)[0])}
    vacuum, oxygen, co2, water, solid = [ids[h] for h in
        (0x2D39BF75, 0xA4E0B340, 0x74DC00EF, 1836671383, 1838482828)]
    cases = [("heat-oxygen", 8, 8, "oxygen", None),
             ("heat-co2", 12, 10, "co2", None),
             ("heat-mixed", 16, 16, "mixed", None),
             ("heat-chunk33", 35, 34, "mixed", None),
             ("heat-subregion", 35, 34, "oxygen", "3,5,30,29"),
             ("heat-water-full", 8, 8, "water-full", None),
             ("heat-water-column", 10, 10, "water-column", None),
             ("heat-water-basin", 12, 12, "water-basin", None)]
    a.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, width, height, layout, region in cases:
        count = width * height
        payload = bytearray(20 + 40 * count)
        struct.pack_into("<III??", payload, 0, width, height, 123456, False, True)
        for i in range(count):
            x, y = i % width, i // width
            elem = co2 if layout == "co2" else (co2 if layout == "mixed" and (x + y) % 2 else oxygen)
            mass = 1 + (i % 7) * .125
            temp = 250 + ((x * 7 + y * 11) % 20) * 8
            if layout.startswith("water"):
                elem, mass, temp = water, 400, 280 + ((x * 3 + y * 7) % 9) * 9
                if layout != "water-full" and not (x == width // 2 and abs(y - height // 2) <= 2):
                    elem, mass, temp = vacuum, 0, 0
                if layout == "water-basin" and y == height // 2 - 3:
                    elem, mass, temp = solid, 9999, 0
            struct.pack_into("<HBB4xff", payload, 14 + 16 * i, elem, 0, 255, temp, mass)
            payload[14 + 16 * count + 12 * i] = 255
            struct.pack_into("<H", payload, 14 + 28 * count + 12 * i, vacuum)
        dest = a.output_dir / name
        dest.mkdir(exist_ok=True)
        wire = dest / "fixture-input.bin"
        wire.write_bytes(payload)
        cmd = ["dotnet", str(a.oracle), "--game-root", str(a.game_root),
               "--capture-dir", str(a.capture_dir), "--source-save", str(a.source_save),
               "--synthetic", f"{width}x{height}", "--no-radiation", "--define-world",
               "--initialize-input", str(wire), "--step-seconds", "0.2,0.4,0,0.2,0.6",
               "--output-dir", str(dest)]
        if region:
            cmd += ["--frame-region", region]
        elif not layout.startswith("water"):
            # Keep gas pressure away from the original cold, massive top
            # vacuum border; that case needs the still-unimplemented phases.
            cmd += ["--frame-region", f"0,0,{width},{height-2}"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        (dest / "process.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name}: oracle exit {result.returncode}; see process.log")
        rows.append({"case": name, "frames": 5, "width": width, "height": height, "layout": layout})
        print(f"Recorded {name}", flush=True)
    (a.output_dir / "heat-suite.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
