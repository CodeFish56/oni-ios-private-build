"""Link the staged OniSim C++ modules into the audited iOS export.

Only seven Sim.SIM_* imports change. Other SimDLL consumers (profiling,
system information and conduit temperature) retain their original boundaries.
The incomplete engine is capability-gated before any callback is marshalled.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re

from disable_ios_fmod_native import digest, functions
from patch_ios_startup import transform


SOURCE = "Il2CppOutputProject/Source/il2cppOutput/Assembly-CSharp__122.cpp"
PBX = "Unity-iPhone.xcodeproj/project.pbxproj"
PREFIX = "ONI_PORT_LINKED_SIM_CORE"
ROOT = "Libraries/OniSimCore"
CPP_FILES = (
    "FrameCellValues.cpp", "FrameStepSchedule.cpp", "GasPostProcess.cpp",
    "GasPressure.cpp", "LiquidFlow.cpp", "PressureFlux.cpp", "SaveWorldV15.cpp",
    "SimCore.cpp", "SimCoreExports.cpp", "StabilityTimers.cpp",
)
SIM_HEADERS = tuple(name.replace(".cpp", ".h") for name in CPP_FILES)
ABI_HEADERS = ("GameDataUpdate.h", "SimExports.h", "SimWireABI.h")
IMPORTS = (
    "Initialize", "Shutdown", "HandleMessage", "HandleMessages",
    "BeginSave", "EndSave", "DebugCrash",
)
REQUIRED_CAPABILITIES = (1 << 16) | (1 << 17) | (1 << 18)
UNSUPPORTED = (
    "ONI iOS simulation core is linked, but world generation settlement, "
    "complete game simulation and game render data are not implemented. "
    "Creating or loading a colony is unavailable in this build."
)
PREAMBLE = '''// ONI_PORT_LINKED_SIM_CORE: the C ABI returns before managed exceptions are raised.
#include "../../../Libraries/OniSimCore/sim/SimCoreExports.h"
#include "vm/Exception.h"

static void OniPortCheckLinkedSimStatus(const RuntimeMethod* method)
{
    if (OniSim_GetLastStatus() == 0) return;
    const char* error = OniSim_GetLastError();
    IL2CPP_RAISE_MANAGED_EXCEPTION(il2cpp_codegen_get_invalid_operation_exception(
        error != nullptr && *error != '\\0' ? error : "OniSim core returned an unspecified failure"), method);
}

'''


def wrapper(name: str) -> str:
    lines = ["{", f"\t// {PREFIX}"]
    if name == "Initialize":
        lines += [
            f"\tconstexpr uint64_t required = {REQUIRED_CAPABILITIES}ULL;",
            "\tif ((OniSim_GetCapabilities() & required) != required)",
            "\t{",
            f'\t\tstatic const char16_t message[] = u"{UNSUPPORTED}";',
            "\t\tconst il2cpp::utils::StringView<Il2CppChar> text(",
            "\t\t\treinterpret_cast<const Il2CppChar*>(message), sizeof(message) / sizeof(message[0]) - 1);",
            "\t\tIL2CPP_RAISE_MANAGED_EXCEPTION(reinterpret_cast<Exception_t*>(",
            "\t\t\til2cpp::vm::Exception::GetPlatformNotSupportedException(text)), method);",
            "\t}",
            "\t// AOT callback support is still required before this capability gate can open.",
            "\tIl2CppMethodPointer callback = il2cpp_codegen_marshal_delegate(",
            "\t\treinterpret_cast<MulticastDelegate_t*>(___0_callback));",
            "\tOniSim_Initialize(reinterpret_cast<OniSim_GameMessageHandler>(callback));",
        ]
    elif name == "Shutdown":
        lines.append("\tOniSim_Shutdown();")
    elif name == "HandleMessage":
        lines.append("\tauto* result = OniSim_HandleMessage(___0_sim_msg_id, ___1_msg_length, ___2_msg);")
    elif name == "HandleMessages":
        lines.append("\tauto* result = OniSim_HandleMessages(___0_sim_msg_id, ___1_msg_length, ___2_msg_count, ___3_msg);")
    elif name == "BeginSave":
        lines += [
            "\tuint8_t* result = OniSim_BeginSave(___0_size, ___1_x, ___2_y);",
        ]
    elif name == "EndSave":
        lines.append("\tOniSim_EndSave();")
    elif name == "DebugCrash":
        lines.append("\tOniSim_DebugCrash();")
    else:
        raise ValueError(f"Unreviewed Sim import: {name}")
    lines.append("\tOniPortCheckLinkedSimStatus(method);")
    if name in ("HandleMessage", "HandleMessages"):
        lines.append("\treturn reinterpret_cast<intptr_t>(result);")
    elif name == "BeginSave":
        lines += [
            "\tif (result == nullptr || ___0_size == nullptr || *___0_size <= 0)",
            "\t{",
            "\t\tIL2CPP_RAISE_MANAGED_EXCEPTION(il2cpp_codegen_get_invalid_operation_exception(",
            '\t\t\t"OniSim core returned no valid save buffer"), method);',
            "\t}",
            "\treturn result;",
        ]
    return "\n".join(lines + ["}"])


def modify_generated(source: str, methods: list[dict]) -> str:
    changed = transform(source, methods)
    anchor = '#include "pch-cpp.hpp"\n'
    if changed.count(anchor) != 1:
        raise ValueError("Generated source preamble changed")
    return changed.replace(anchor, anchor + PREAMBLE)


def identifier(kind: str, name: str) -> str:
    return hashlib.sha256(f"{PREFIX}:{kind}:{name}".encode()).hexdigest()[:24].upper()


def block(source: str, object_id: str) -> tuple[int, int, str]:
    match = re.search(r"^\t\t" + re.escape(object_id) + r" /\*[^\n]*\*/ = \{\n", source, re.M)
    if not match:
        raise ValueError(f"Missing Xcode object {object_id}")
    end = source.find("\n\t\t};", match.end())
    if end < 0:
        raise ValueError(f"Unclosed Xcode object {object_id}")
    return match.start(), end + len("\n\t\t};"), source[match.start():end + len("\n\t\t};")]


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"Xcode source shape changed: {old[:70]}")
    return source.replace(old, new)


def project_objects(source: str) -> tuple[str, str]:
    targets = re.findall(r"^\t\t([0-9A-F]{24}) /\* UnityFramework \*/ = \{\n\t\t\tisa = PBXNativeTarget;", source, re.M)
    if len(targets) != 1:
        raise ValueError("Expected exactly one UnityFramework native target")
    _, _, target = block(source, targets[0])
    phase = re.findall(r"([0-9A-F]{24}) /\* Sources \*/", target)
    if len(phase) != 1:
        raise ValueError("Expected exactly one UnityFramework source phase")
    root = re.search(r"^\trootObject = ([0-9A-F]{24}) /\* Project object \*/;", source, re.M)
    if root is None:
        raise ValueError("Missing Xcode root object")
    _, _, project = block(source, root[1])
    group = re.search(r"mainGroup = ([0-9A-F]{24})(?: /\*[^\n]*?\*/)?;", project)
    if group is None:
        raise ValueError("Missing Xcode main group")
    return phase[0], group[1]


def pbx_insertions(source: str) -> list[tuple[int, str]]:
    phase_id, group_id = project_objects(source)
    compile_objects = []
    references = []
    group_children = []
    compile_children = []
    files = [f"sim/{name}" for name in CPP_FILES + SIM_HEADERS] + [f"abi/{name}" for name in ABI_HEADERS]
    for name in files:
        ref_id = identifier("file", name)
        filename = Path(name).name
        file_type = "sourcecode.cpp.cpp" if name.endswith(".cpp") else "sourcecode.c.h"
        references.append(f'\t\t{ref_id} /* {filename} */ = {{isa = PBXFileReference; lastKnownFileType = {file_type}; name = {filename}; path = "{ROOT}/{name}"; sourceTree = SOURCE_ROOT; }};\n')
        group_children.append(f"\t\t\t\t{ref_id} /* {filename} */,\n")
        if name.endswith(".cpp"):
            build_id = identifier("build", name)
            compile_objects.append(f'\t\t{build_id} /* {filename} in Sources */ = {{isa = PBXBuildFile; fileRef = {ref_id} /* {filename} */; settings = {{COMPILER_FLAGS = "-std=c++17 -fexceptions -ffp-contract=off -I\\"$(SRCROOT)/Libraries/OniSimCore/abi\\""; }}; }};\n')
            compile_children.append(f"\t\t\t\t{build_id} /* {filename} in Sources */,\n")
    sim_group_id = identifier("group", ROOT)
    group_object = (f"\t\t{sim_group_id} /* OniSimCore */ = {{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                    + "".join(group_children) + "\t\t\t);\n\t\t\tname = OniSimCore;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n")
    insertions = []
    for section, content in (("PBXBuildFile", "".join(compile_objects)),
                             ("PBXFileReference", "".join(references)), ("PBXGroup", group_object)):
        anchor = f"/* End {section} section */"
        if source.count(anchor) != 1:
            raise ValueError(f"Missing Xcode section: {section}")
        insertions.append((source.index(anchor), content))
    start, _, phase = block(source, phase_id)
    anchor = "\t\t\tfiles = (\n"
    if phase.count(anchor) != 1 or "isa = PBXSourcesBuildPhase;" not in phase:
        raise ValueError("UnityFramework sources structure changed")
    insertions.append((start + phase.index(anchor) + len(anchor), "".join(compile_children)))
    start, _, group = block(source, group_id)
    anchor = "\t\t\tchildren = (\n"
    if group.count(anchor) != 1 or "isa = PBXGroup;" not in group:
        raise ValueError("Main Xcode group structure changed")
    insertions.append((start + group.index(anchor) + len(anchor), f"\t\t\t\t{sim_group_id} /* OniSimCore */,\n"))
    return insertions


def modify_pbx(source: str) -> str:
    for offset, content in sorted(pbx_insertions(source), reverse=True):
        source = source[:offset] + content + source[offset:]
    return source


def create_plan(project: Path, output: Path) -> dict:
    raw = (project / SOURCE).read_bytes()
    source = raw.decode("utf-8").replace("\r\n", "\n")
    entries = []
    for match, start, end in functions(source):
        name = next((name for name in IMPORTS if match["function"].startswith(f"Sim_SIM_{name}_m")), None)
        if name is None:
            continue
        body = source[start:end]
        resolver = f'IL2CPP_NATIVE_STRING("SimDLL"), "SIM_{name}"'
        if body.count(resolver) != 1 or match["parameters"].find("bool ") >= 0:
            raise ValueError(f"Sim import ABI changed: {match['function']}")
        entries.append({"function": match["function"], "signature": match["signature"],
                        "body_sha256": digest(body.encode()), "replacement": wrapper(name)})
    if len(entries) != len(IMPORTS):
        raise ValueError("Expected exactly seven Sim import wrappers")
    callback = re.search(r"^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR int32_t GAME_MessageHandler_Invoke_\w+ \([^\n]*int32_t ___0_message_id, intptr_t ___1_data[^\n]*\) ", source, re.M)
    if callback is None:
        raise ValueError("Managed callback signature changed")
    pbx_raw = (project / PBX).read_bytes()
    pbx_source = pbx_raw.decode("utf-8").replace("\r\n", "\n")
    plan = {
        "version": 1, "source": SOURCE, "source_sha256": digest(raw), "methods": entries,
        "patched_sha256": digest(modify_generated(source, entries).encode()),
        "pbx_source_sha256": digest(pbx_raw), "pbx_patched_sha256": digest(modify_pbx(pbx_source).encode()),
        "native_cpp_files": list(CPP_FILES), "required_capabilities": REQUIRED_CAPABILITIES,
        "callback_evidence": callback[0],
        "conduit_bool_evidence": "Assembly-CSharp__31.cpp: native ConduitTemperatureManager_Add uses int32_t for managed bool; not linked by this patch",
    }
    output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan


def patch(project: Path, core_dir: Path, plan: dict, check_only: bool = False) -> dict:
    if plan["version"] != 1 or plan["native_cpp_files"] != list(CPP_FILES) or plan["required_capabilities"] != REQUIRED_CAPABILITIES:
        raise ValueError("The native integration plan does not match this script")
    previews = {}
    for rel, source_key, patched_key, change in (
        (SOURCE, "source_sha256", "patched_sha256", lambda text: modify_generated(text, plan["methods"])),
        (PBX, "pbx_source_sha256", "pbx_patched_sha256", modify_pbx),
    ):
        path = project / rel
        raw = path.read_bytes()
        if digest(raw) == plan[source_key]:
            updated = change(raw.decode("utf-8").replace("\r\n", "\n")).encode()
            if digest(updated) != plan[patched_key]:
                raise ValueError(f"Patched source digest differs from plan: {rel}")
        elif digest(raw) == plan[patched_key]:
            updated = raw
        else:
            raise ValueError(f"Unreviewed iOS export version: {rel}")
        previews[path] = updated
    existing_cpp = {path.name for path in (core_dir / "sim").glob("*.cpp")}
    if existing_cpp != set(CPP_FILES):
        raise ValueError(f"Native source modules differ from reviewed build list: {sorted(existing_cpp)}")
    native_files = {}
    for folder, files in (("sim", CPP_FILES + SIM_HEADERS), ("abi", ABI_HEADERS)):
        for filename in files:
            relative = f"{folder}/{filename}"
            data = (core_dir / relative).read_bytes()
            if not data:
                raise ValueError(f"Empty native core source: {relative}")
            native_files[relative] = digest(data)
            previews[project / ROOT / relative] = data
    # This is source integration, not permission to bypass the capability gate.
    exports = (core_dir / "sim/SimCoreExports.h").read_text(encoding="utf-8")
    for name in IMPORTS:
        if not re.search(r"\bOniSim_" + name + r"\s*\([^;{}]*\)\s*(?:noexcept|ONI_SIM_NOEXCEPT)\s*;", exports):
            raise ValueError(f"Missing noexcept C ABI declaration: OniSim_{name}")
    for name in ("GetCapabilities", "GetLastError", "GetLastStatus"):
        if not re.search(r"\bOniSim_" + name + r"\s*\([^;{}]*\)\s*(?:noexcept|ONI_SIM_NOEXCEPT)\s*;", exports):
            raise ValueError(f"Missing noexcept status/capability declaration: OniSim_{name}")
    changed = any(not path.exists() or path.read_bytes() != data for path, data in previews.items())
    patched_source = previews[project / SOURCE].decode()
    leftovers = re.findall(r'il2cpp_codegen_resolve_pinvoke[^\n]*IL2CPP_NATIVE_STRING\("SimDLL"\), "([^"]+)"', patched_source)
    if leftovers != ["SYSINFO_Acquire", "SYSINFO_Release"]:
        raise ValueError(f"Unexpected remaining SimDLL imports in audited source: {leftovers}")
    if not check_only:
        for path, data in previews.items():
            if not path.exists() or path.read_bytes() != data:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
    return {"changed": changed, "check_only": check_only, "linked_sim_imports": len(IMPORTS),
            "compiled_cpp_modules": len(CPP_FILES), "copied_source_files": len(native_files),
            "native_source_sha256": native_files, "target": "UnityFramework",
            "required_gameplay_capabilities": REQUIRED_CAPABILITIES,
            "missing_capabilities": "raise PlatformNotSupportedException before callback marshaling",
            "remaining_simdll_imports_in_target_source": leftovers,
            "other_simdll_consumers": "unchanged; no global FORCE_PINVOKE define",
            "world_generation_available": False, "ios_compile_validation": "pending"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--core-dir", type=Path, default=Path(__file__).resolve().parents[1] / "native")
    parser.add_argument("--plan", type=Path, default=Path(__file__).with_name("ios-sim-core-plan.json"))
    parser.add_argument("--create-plan", action="store_true")
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.create_plan:
        create_plan(args.project, args.plan)
        print(f"Sim core integration plan written: {args.plan}")
        return
    result = patch(args.project, args.core_dir, json.loads(args.plan.read_text(encoding="utf-8")), args.check_only)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.write_text(encoded, encoding="utf-8")
    print(encoded, end="")


if __name__ == "__main__":
    main()
