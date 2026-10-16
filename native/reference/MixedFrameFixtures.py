"""Capture original positive mixed Water/O2/CO2 frames, with actual tick state."""
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
    cases = [("mixed-water-oxygen-fall", 12, 12, "fall", 123456, "0.2,0.4,0,0.2,0.6"),
             ("pure-water-cold-regression", 12, 12, "cold", 70, "0.2,0.4,0,0.2,0.6"),
             ("pure-water-hot-regression", 12, 12, "hot", 70, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-co2-fall", 12, 12, "co2-fall", 70, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-gases-basin", 12, 12, "basin", 9020, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-evaporated-basin", 12, 12, "evaporated-basin", 70, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-checker", 12, 12, "checker", 123456, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-layers", 16, 16, "layers", 0, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-low-mass", 12, 12, "low", 70, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-temperature", 12, 12, "temperature", 9020, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-chunk33", 35, 34, "checker", 123456, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-subregion", 35, 34, "subregion", 70, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-time-carry", 12, 12, "checker", 123456, "0.01,0.19,0.4,0,0.2,1,0.2,0.2"),
             ("mixed-water-empty-region", 8, 8, "empty", 123456, "0.2,0.4,0,0.2,0.6"),
             ("mixed-water-long", 8, 10, "fall", 123456, ",".join(["0.2"] * 24))]
    a.output_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, width, height, layout, seed, steps in cases:
        count = width * height
        payload = bytearray(20 + 40 * count)
        struct.pack_into("<III??", payload, 0, width, height, seed, False, True)
        for i in range(count):
            x, y = i % width, i // width
            elem = co2 if layout == "co2-fall" or (x // 2 + y // 3) % 2 else oxygen
            mass, temp = 1 + (i % 5) * .125, 300
            wet = x in (width // 2 - 1, width // 2) and y in (height - 5, height - 4)
            if layout in ("cold", "hot"):
                wet = True
                temp = 273.25 if layout == "cold" else 371
            if layout in ("checker", "low", "temperature", "subregion", "empty"):
                wet = (x + 2 * y) % 5 == 0 and 1 < x < width - 2 and 1 < y < height - 3
            if layout == "layers":
                wet = y in (3, 4) and 2 <= x < width - 2
            if wet:
                elem, mass = water, 100
                if layout == "low":
                    mass = [.005, .01, .02, 1][i % 4]
                elif layout == "evaporated-basin":
                    mass = .005
            elif layout == "low" and i % 3 == 0:
                mass = .0005
            if layout == "temperature":
                temp = 280 + ((x * 3 + y * 7) % 9) * 9
            if layout in ("basin", "evaporated-basin") and y == 2:
                elem, mass, temp = solid, 9999, 0
            struct.pack_into("<HBB4xff", payload, 14 + 16 * i, elem, 0, 255, temp, mass)
            payload[14 + 16 * count + 12 * i] = 255
            struct.pack_into("<H", payload, 14 + 28 * count + 12 * i, vacuum)
        dest = a.output_dir / name
        dest.mkdir(exist_ok=True)
        wire = dest / "fixture-input.bin"
        wire.write_bytes(payload)
        region = "0,0,0,0" if layout == "empty" else \
            "3,5,30,29" if layout == "subregion" else f"0,0,{width},{height-2}"
        cmd = ["dotnet", str(a.oracle), "--game-root", str(a.game_root),
               "--capture-dir", str(a.capture_dir), "--source-save", str(a.source_save),
               "--synthetic", f"{width}x{height}", "--no-radiation", "--define-world",
               "--initialize-input", str(wire), "--step-seconds", steps,
               "--frame-region", region, "--output-dir", str(dest)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        (dest / "process.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name}: oracle exit {result.returncode}; see process.log")
        rows.append({"case": name, "frames": len(steps.split(',')), "width": width, "height": height, "layout": layout})
        print(f"Recorded {name}", flush=True)
    (a.output_dir / "mixed-suite.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
