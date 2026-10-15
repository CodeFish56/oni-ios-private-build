"""Report missing SimDLL instead of leaving the iOS world-generation screen spinning.

This does not implement SimDLL or make world generation succeed. Apply only to
the audited export; unknown source versions are rejected before any write.
"""

import argparse
import json
from pathlib import Path

from disable_ios_fmod_native import digest, functions
from patch_ios_startup import transform


HEADER = "OniSimFailure.h"
INCLUDE = f'#include "{HEADER}"\n'
MISSING_SIM_MESSAGE = (
    "This iOS build cannot start the simulation engine (SimDLL). "
    "World generation has stopped. Return to the main menu."
)
TARGETS = {
    "Assembly-CSharp__177.cpp": ("WorldGenSimUtil_DoSettleSim_",),
    "Assembly-CSharp__176.cpp": ("WorldGen_RenderOffline_",),
    "Assembly-CSharp__93.cpp": ("OfflineWorldGen_Update_", "OfflineWorldGen_DoExitFlow_"),
}


def replace_exact(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"Worldgen source shape changed: {old[:85]}")
    return source.replace(old, new)


def repair(name: str, body: str) -> str:
    if name.startswith("WorldGenSimUtil_DoSettleSim_"):
        initialize = "\t\tSim_SIM_Initialize_m136F1E495235DD9542F75CA6C1DF81A5DA188123(L_0, NULL);"
        guarded = (
            "\t\tif (!OniPortInitializeSim([&]() {\n" + initialize + "\n"
            "\t\t}, [&](Il2CppException* exception) {\n"
            "\t\t\tErrorInfo_tCB694DA808DCFB1912046E295B175C15D4D233D7 error = {};\n"
            f'\t\t\terror.___errorDesc = il2cpp_codegen_string_new_wrapper("{MISSING_SIM_MESSAGE}");\n'
            "\t\t\terror.___exception = reinterpret_cast<Exception_t*>(exception);\n"
            "\t\t\tif (___7_error_cb != nullptr) {\n"
            "\t\t\t\tAction_1_Invoke_m4C23DACF62613602768D413A57D82F5D5613A8D4_inline(___7_error_cb, error, NULL);\n"
            "\t\t\t}\n\t\t})) { return false; }"
        )
        return replace_exact(body, initialize, guarded)
    if name.startswith("WorldGen_RenderOffline_"):
        return replace_exact(body, "\t\t__this->___running = L_86;",
                             "\t\t__this->___running = L_86;\n\t\tif (!L_86) { return false; }")
    if name.startswith("OfflineWorldGen_DoExitFlow_"):
        return replace_exact(body,
                             "\t\tSim_Shutdown_m71AC5230FB27D521B55D49D1033E36160460FE2E(NULL);",
                             "\t\tOniPortShutdownAfterWorldgenFailure([]() {\n"
                             "\t\t\tSim_Shutdown_m71AC5230FB27D521B55D49D1033E36160460FE2E(NULL);\n\t\t});")
    if name.startswith("OfflineWorldGen_Update_"):
        body = replace_exact(body,
                             "\t\tG_B5_0 = ((((RuntimeObject*)(String_t*)L_6) == ((RuntimeObject*)(RuntimeObject*)NULL))? 1 : 0);",
                             "\t\tG_B5_0 = 0; // Check queued errors even before the first progress update.")
        return replace_exact(body, "IL_007a:\n\t{\n",
                             "IL_007a:\n\t{\n\t\tif (__this->___currentConvertedCurrentStage.___String == nullptr) { return; }\n")
    raise ValueError(f"Unreviewed worldgen method: {name}")


def create_plan(project: Path, output: Path) -> None:
    sources = []
    root = project / "Il2CppOutputProject/Source/il2cppOutput"
    for filename, prefixes in TARGETS.items():
        path = root / filename
        original = path.read_bytes()
        source = original.decode("utf-8").replace("\r\n", "\n")
        entries = []
        for match, start, end in functions(source):
            if match["function"].startswith(prefixes):
                body = source[start:end]
                entries.append({"function": match["function"], "signature": match["signature"],
                                "body_sha256": digest(body.encode("utf-8")),
                                "replacement": repair(match["function"], body)})
        if len(entries) != len(prefixes):
            raise ValueError(f"Worldgen method count changed: {filename}")
        patched = transform(source, entries)
        if filename != "Assembly-CSharp__176.cpp":
            patched = replace_exact(patched, '#include "pch-cpp.hpp"\n', '#include "pch-cpp.hpp"\n' + INCLUDE)
        sources.append({"source": path.relative_to(project).as_posix(),
                        "source_sha256": digest(original),
                        "patched_sha256": digest(patched.encode("utf-8")), "methods": entries})
    plan = {"sources": sources, "header_sha256": digest(Path(__file__).with_name(HEADER).read_bytes())}
    output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")


def patch(project: Path, plan: dict) -> dict:
    if not (project / "Unity-iPhone.xcodeproj/project.pbxproj").is_file():
        raise ValueError(f"Not an iOS Xcode export: {project}")
    header = Path(__file__).with_name(HEADER).read_bytes()
    if digest(header) != plan["header_sha256"]:
        raise ValueError("Worldgen exception helper differs from the audited plan")
    previews = {}
    for entry in plan["sources"]:
        path = project / entry["source"]
        original = path.read_bytes()
        checksum = digest(original)
        if checksum == entry["source_sha256"]:
            source = original.decode("utf-8").replace("\r\n", "\n")
            patched = transform(source, entry["methods"])
            if path.name != "Assembly-CSharp__176.cpp":
                patched = replace_exact(patched, '#include "pch-cpp.hpp"\n', '#include "pch-cpp.hpp"\n' + INCLUDE)
            patched = patched.encode("utf-8")
            if digest(patched) != entry["patched_sha256"]:
                raise ValueError(f"Worldgen patch output changed: {path.name}")
        elif checksum == entry["patched_sha256"]:
            patched = original
        else:
            raise ValueError(f"Worldgen source changed; audit before building: {path.name}")
        previews[path] = patched
    header_path = project / "Il2CppOutputProject/Source/il2cppOutput" / HEADER
    if (header_path.exists() and header_path.read_bytes() != header
            and digest(header_path.read_bytes()) not in plan.get("previous_header_sha256", [])):
        raise ValueError("An unknown worldgen exception header already exists")
    previews[header_path] = header
    changed = False
    for path, data in previews.items():
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
            changed = True
    return {"changed": changed, "patched_methods": 4, "missing_simulator": "report failure and return to menu",
            "unrelated_exceptions": "rethrow", "simulator_implemented": False, "device_validation": "pending"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--plan", type=Path, default=Path(__file__).with_name("ios-worldgen-failure-plan.json"))
    parser.add_argument("--create-plan", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.create_plan:
        create_plan(args.project, args.plan)
        print(f"Worldgen error plan written: {args.plan}")
        return
    result = json.dumps(patch(args.project, json.loads(args.plan.read_text(encoding="utf-8"))), indent=2) + "\n"
    if args.report:
        args.report.write_text(result, encoding="utf-8")
    print(result, end="")


if __name__ == "__main__":
    main()
