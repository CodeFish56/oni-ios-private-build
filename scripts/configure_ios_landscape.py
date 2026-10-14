"""Restrict the current iOS export to both landscape orientations."""

import argparse
import json
from pathlib import Path
import plistlib
import re


LANDSCAPE = ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"]
FUNCTION = re.compile(r"NSUInteger EnabledAutorotationInterfaceOrientations\(\)\n\{[\s\S]*?\n\}")
REPLACEMENT = ("NSUInteger EnabledAutorotationInterfaceOrientations()\n{\n"
               "    return UIInterfaceOrientationMaskLandscape;\n}")


def configure(project: Path) -> dict:
    if not (project / "Unity-iPhone.xcodeproj/project.pbxproj").is_file():
        raise ValueError(f"Not an iOS Xcode export: {project}")
    plist_path = project / "Info.plist"
    plist = plistlib.loads(plist_path.read_bytes())
    controller_path = project / "Classes/UI/UnityViewControllerBase+iOS.mm"
    controller = controller_path.read_text(encoding="utf-8")
    matches = list(FUNCTION.finditer(controller))
    if len(matches) != 1:
        raise ValueError("Expected one Unity autorotation mask implementation")
    original = matches[0][0]
    if original != REPLACEMENT and ("UnityIsOrientationEnabled(portrait)" not in original
                                     or "UnityIsOrientationEnabled(landscapeLeft)" not in original
                                     or "UnityIsOrientationEnabled(landscapeRight)" not in original):
        raise ValueError("Unrecognized Unity autorotation implementation")
    patched = controller[:matches[0].start()] + REPLACEMENT + controller[matches[0].end():]
    changed = False
    for key in ("UISupportedInterfaceOrientations", "UISupportedInterfaceOrientations~iphone",
                "UISupportedInterfaceOrientations~ipad"):
        if plist.get(key) != LANDSCAPE:
            plist[key] = LANDSCAPE.copy()
            changed = True
    if changed:
        plist_path.write_bytes(plistlib.dumps(plist, sort_keys=False))
    if patched != controller:
        controller_path.write_text(patched, encoding="utf-8", newline="\n")
        changed = True
    return {"supported_orientations": LANDSCAPE, "unity_autorotation_mask": "landscape",
            "changed": changed, "device_validation": "pending"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = json.dumps(configure(args.project), indent=2) + "\n"
    if args.report:
        args.report.write_text(result, encoding="utf-8")
    print(result, end="")


if __name__ == "__main__":
    main()
