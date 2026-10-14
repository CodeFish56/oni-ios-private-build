"""Disable managed audio initialization and desktop startup services in an audited iOS export."""

import argparse
import json
from pathlib import Path
import re

from disable_ios_fmod_native import digest, functions


AUDIO_TYPES = (
    "RuntimeManager", "StudioListener", "StudioEventEmitter", "StudioBankLoader",
    "StudioParameterTrigger", "StudioGlobalParameterTrigger", "EventHandler",
    "FMODRuntimeManagerOnGUIHelper",
)
PRESERVED_EMITTER_METHODS = {
    "get_PlayEvent", "set_PlayEvent", "get_StopEvent", "set_StopEvent",
}


def audio_replacement(match) -> str | None:
    name = match["function"].split("_m", 1)[0]
    owner, _, member = name.partition("_")
    if owner not in AUDIO_TYPES or member in ("_ctor", "_cctor"):
        return None
    if owner == "StudioListener" and match["return_type"] != "void":
        return None
    if owner == "StudioEventEmitter" and member in PRESERVED_EMITTER_METHODS:
        return None
    lines = ["{"]
    outputs = re.findall(r"\w+\* (___\d+_instance)(?:,|$)", match["parameters"])
    for parameter in outputs:
        lines.append(f"\tif ({parameter} != nullptr) {{ *{parameter} = {{}}; }}")
    if name in ("RuntimeManager_get_HaveAllBanksLoaded", "RuntimeManager_get_HaveMasterBanksLoaded"):
        lines.append("\treturn true;")
    elif name in ("RuntimeManager_Initialize", "RuntimeManager_DEBUG_CALLBACK", "RuntimeManager_ERROR_CALLBACK"):
        lines.append("\treturn 67;")
    elif match["return_type"] != "void":
        lines.append("\treturn {};")
    lines.append("}")
    return "\n".join(lines)


def global_replacement(body: str) -> str:
    start = body.index("IL_008d:")
    end = body.index("IL_00e2:", start)
    branch = body[start:end]
    required = ("SteamUGCService_Initialize_", "SteamUGCService_AddClient_",
                "SteamAchievementService_Initialize_", "DistributionPlatform_get_Initialized_")
    if any(branch.count(symbol) != 1 for symbol in required):
        raise ValueError("Global.Update Steam registration differs from the audited branch")
    replacement = ("IL_008d:\n\t{\n"
                   "\t\t__this->___updated_with_initialized_distribution_platform = (bool)1;\n"
                   "\t}\n\n")
    body = body[:start] + replacement + body[end:]
    for type_name in ("SteamUGCService", "Steam"):
        body = re.sub(r"^\t\til2cpp_codegen_initialize_runtime_metadata\(\(uintptr_t\*\)&"
                      + type_name + r"_t\w+_il2cpp_TypeInfo_var\);\n", "", body, flags=re.MULTILINE)
    if "SteamUGCService_" in body or "SteamAchievementService_" in body:
        raise ValueError("Steam startup calls remain in Global.Update")
    return body


def replacements(source: str, category: str) -> list[dict]:
    selected = []
    for match, start, end in functions(source):
        name = match["function"]
        original = source[start:end]
        replacement = None
        if category == "audio":
            replacement = audio_replacement(match)
        elif name.startswith("DevToolManager_RegisterAdditionalDevToolsByReflection_m"):
            replacement = "{\n}"
        elif name.startswith("Global_Update_m"):
            replacement = global_replacement(original)
        if replacement is not None:
            selected.append({"function": name, "signature": match["signature"],
                             "body_sha256": digest(original.encode("utf-8")),
                             "replacement": replacement})
    return selected


def transform(source: str, entries: list[dict]) -> str:
    expected = {entry["function"]: entry for entry in entries}
    edits = []
    for match, start, end in functions(source):
        entry = expected.pop(match["function"], None)
        if entry is None:
            continue
        if match["signature"] != entry["signature"]:
            raise ValueError(f"Generated signature changed: {match['function']}")
        if digest(source[start:end].encode("utf-8")) != entry["body_sha256"]:
            raise ValueError(f"Generated implementation changed: {match['function']}")
        edits.append((start, end, entry["replacement"]))
    if expected:
        raise ValueError(f"Missing startup implementations: {list(expected)}")
    for start, end, replacement in reversed(edits):
        source = source[:start] + replacement + source[end:]
    return source


def create_plan(project: Path, output: Path) -> dict:
    root = project / "Il2CppOutputProject/Source/il2cppOutput"
    targets = (("FMODUnity__1.cpp", "audio"),
               ("Assembly-CSharp__39.cpp", "devtools"),
               ("Assembly-CSharp__61.cpp", "global"))
    sources = []
    for filename, category in targets:
        path = root / filename
        original = path.read_bytes()
        source = original.decode("utf-8").replace("\r\n", "\n")
        entries = replacements(source, category)
        if not entries or (category != "audio" and len(entries) != 1):
            raise ValueError(f"Unexpected startup implementation count: {filename}")
        patched = transform(source, entries).encode("utf-8")
        sources.append({"source": str(path.relative_to(project).as_posix()),
                        "category": category, "source_sha256": digest(original),
                        "patched_sha256": digest(patched), "methods": entries})
    plan = {"sources": sources, "audio_types": list(AUDIO_TYPES)}
    output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan


def patch(project: Path, plan: dict) -> dict:
    if not (project / "Unity-iPhone.xcodeproj/project.pbxproj").is_file():
        raise ValueError(f"Not an iOS Xcode export: {project}")
    previews = {}
    counts = {}
    changed = False
    for entry in plan["sources"]:
        path = project / entry["source"]
        original = path.read_bytes()
        checksum = digest(original)
        if checksum == entry["source_sha256"]:
            patched = transform(original.decode("utf-8").replace("\r\n", "\n"), entry["methods"]).encode("utf-8")
            if digest(patched) != entry["patched_sha256"]:
                raise ValueError(f"Startup output digest mismatch: {path.name}")
        elif checksum == entry["patched_sha256"]:
            patched = original
        else:
            raise ValueError(f"Startup source changed; review the new export: {path.name}")
        if entry["category"] == "audio":
            candidate_methods = {match["function"] for match, _, _ in functions(patched.decode("utf-8"))
                                 if audio_replacement(match) is not None}
            if candidate_methods != {method["function"] for method in entry["methods"]}:
                raise ValueError("Managed audio method coverage differs from the plan")
        previews[path] = patched
        counts[entry["category"]] = len(entry["methods"])
        changed |= original != patched
    for path, patched in previews.items():
        if path.read_bytes() != patched:
            path.write_bytes(patched)
    return {"patched_method_counts": counts, "changed": changed,
            "audio_policy": "Silent; invalid audio handles, no RuntimeManager initialization or component audio callbacks.",
            "steam_workshop_registration": "disabled in device Global.Update",
            "developer_tool_reflection_scan": "disabled",
            "device_validation": "pending",
            "sources": [{"path": entry["source"], "sha256": entry["patched_sha256"]}
                        for entry in plan["sources"]]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--plan", type=Path, default=Path(__file__).with_name("ios-startup-plan.json"))
    parser.add_argument("--create-plan", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.create_plan:
        result = create_plan(args.project, args.plan)
        print(json.dumps({"plan": str(args.plan), "counts": {
            entry["category"]: len(entry["methods"]) for entry in result["sources"]}}, indent=2))
        return
    result = patch(args.project, json.loads(args.plan.read_text(encoding="utf-8")))
    rendered = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
