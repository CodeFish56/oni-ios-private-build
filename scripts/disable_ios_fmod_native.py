"""Make a verified iOS IL2CPP export independent of the FMOD native library.

This is a silent diagnostic boundary, not an audio implementation. Managed
FMOD types and callers remain. Native imports return ERR_UNINITIALIZED (or
false for validity queries), with out values initialized and ref inputs kept.
The plan binds this change to the audited generated source by SHA-256.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re


MARKER = "ONI_PORT_SILENT_FMOD_NATIVE"
FMOD_RESOLVER = re.compile(
    r'il2cpp_codegen_resolve_pinvoke[^;]*IL2CPP_NATIVE_STRING\("(?:lib)?fmod[^"\n]*"\)',
    re.IGNORECASE,
)
FUNCTION = re.compile(
    r"^(?P<signature>IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR "
    r"(?P<return_type>\S+) (?P<function>\w+) \((?P<parameters>[^\n]*)\) ?)[ \t]*\n\{",
    re.MULTILINE,
)
# Preserve character positions while ignoring braces in literals and comments.
LITERALS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def functions(source: str):
    masked = LITERALS.sub(lambda match: " " * len(match[0]), source)
    for match in FUNCTION.finditer(source):
        start = match.end() - 1
        end = start + 1
        depth = 1
        while depth:
            if end >= len(masked):
                raise ValueError(f"Unclosed function: {match['function']}")
            if masked[end] == "{":
                depth += 1
            elif masked[end] == "}":
                depth -= 1
            end += 1
        yield match, start, end


def stub(method: dict, error: int) -> str:
    lines = ["{", f"\t// {MARKER}: iOS has no native audio backend."]
    for parameter in method["out_parameters"]:
        lines.append(f"\tif ({parameter} != nullptr) {{ *{parameter} = {{}}; }}")
    if method["return_type"] == "int32_t":
        lines.append(f"\treturn {error}; // FMOD.RESULT.ERR_UNINITIALIZED")
    elif method["return_type"] == "bool":
        lines.append("\treturn false;")
    else:
        raise ValueError(f"Unsupported return type: {method['return_type']}")
    return "\n".join(lines + ["}"])


def transform(source: str, plan: dict) -> str:
    expected = {method["function"]: method for method in plan["methods"]}
    replacements = []
    for match, start, end in functions(source):
        body = source[start:end]
        if not FMOD_RESOLVER.search(body):
            continue
        method = expected.pop(match["function"], None)
        if method is None or match["signature"] != method["signature"]:
            raise ValueError(f"Unreviewed FMOD native entry: {match['function']}")
        replacements.append((start, end, stub(method, plan["err_uninitialized"])))
    if expected or len(replacements) != plan["native_import_count"]:
        raise ValueError(f"Incomplete native boundary: {len(replacements)} of {plan['native_import_count']}")
    for start, end, replacement in reversed(replacements):
        source = source[:start] + replacement + source[end:]
    if FMOD_RESOLVER.search(source):
        raise ValueError("An FMOD library resolver remains after the patch")
    if source.count(MARKER) != plan["native_import_count"]:
        raise ValueError("Native stub marker count mismatch")
    return source


def source_plan(source_file: Path, imports: list, error: int) -> tuple[dict, set]:
    source_bytes = source_file.read_bytes()
    source = source_bytes.decode("utf-8").replace("\r\n", "\n")
    methods = []
    consumed = set()
    for match, start, end in functions(source):
        body = source[start:end]
        if not FMOD_RESOLVER.search(body):
            continue
        names = re.findall(r"\b(___\d+_\w+)\b", match["parameters"])
        candidates = [method for method in imports
                      if f"_{method['name']}_m" in match["function"]
                      and names == [f"___{p['index']}_{p['name']}" for p in method["parameters"]]]
        # Same-name overloads with identical names can differ in input types.
        # Only accept them when every candidate requires the identical stub.
        out_sets = {tuple(f"___{p['index']}_{p['name']}" for p in method["parameters"]
                          if p["is_out"] and p["is_by_reference"])
                    for method in candidates}
        return_types = {method["return_type"] for method in candidates}
        if not candidates or len(out_sets) != 1 or len(return_types) != 1:
            raise ValueError(f"Ambiguous metadata for {match['function']}")
        expected_return = {"FMOD.RESULT": "int32_t", "System.Boolean": "bool"}[next(iter(return_types))]
        if match["return_type"] != expected_return:
            raise ValueError(f"Return type mismatch for {match['function']}")
        outs = list(next(iter(out_sets)))
        for parameter in outs:
            if not re.search(r"\*\s+" + re.escape(parameter) + r"(?:,|$)", match["parameters"]):
                raise ValueError(f"Out parameter is not a pointer: {parameter}")
        methods.append({"function": match["function"], "signature": match["signature"],
                        "return_type": match["return_type"], "out_parameters": outs})
        consumed.update(method["method"] for method in candidates)
    plan = {"source": f"Il2CppOutputProject/Source/il2cppOutput/{source_file.name}",
            "source_sha256": digest(source_bytes),
            "native_import_count": len(methods), "err_uninitialized": error, "methods": methods}
    patched = transform(source, plan).encode("utf-8") if methods else source_bytes
    plan["patched_sha256"] = digest(patched)
    return plan, consumed


def create_plan(project: Path, audit_file: Path, output: Path) -> dict:
    audit = json.loads(audit_file.read_text(encoding="utf-8"))
    if audit["other_assembly_fmod_imports"]:
        raise ValueError("FMOD native imports outside the expected assembly need separate review")
    sources = []
    consumed = set()
    for source_file in sorted((project / "Il2CppOutputProject/Source/il2cppOutput").glob("FMODUnity*.cpp")):
        entry, used = source_plan(source_file, audit["native_imports"], audit["err_uninitialized"])
        if entry["methods"]:
            sources.append(entry)
            consumed.update(used)
    count = sum(entry["native_import_count"] for entry in sources)
    if count != len(audit["native_imports"]) or len(consumed) != count:
        raise ValueError("Generated imports and audited IL metadata do not completely match")
    plan = {"native_import_count": count, "assembly_identities": audit["artifacts"], "sources": sources}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan


def patch(project: Path, plan: dict) -> dict:
    project = project.resolve(strict=True)
    xcode = project / "Unity-iPhone.xcodeproj/project.pbxproj"
    if not xcode.is_file() or "SDKROOT = iphoneos;" not in xcode.read_text(encoding="utf-8"):
        raise ValueError(f"Not the expected device iOS Xcode project: {project}")
    changed = False
    previews = {}
    for entry in plan["sources"]:
        source_file = project / entry["source"]
        before = source_file.read_bytes()
        before_hash = digest(before)
        if before_hash == entry["source_sha256"]:
            patched = transform(before.decode("utf-8").replace("\r\n", "\n"), entry).encode("utf-8")
            if digest(patched) != entry["patched_sha256"]:
                raise ValueError("Transformed source hash mismatch")
            previews[source_file] = patched
            changed = True
        elif before_hash == entry["patched_sha256"]:
            previews[source_file] = before
        else:
            raise ValueError(f"FMOD generated source changed; audit it before building: {entry['source']}")
    remaining = []
    for path in (project / "Il2CppOutputProject/Source").rglob("*.cpp"):
        source = previews[path].decode("utf-8") if path in previews else path.read_text(encoding="utf-8")
        if FMOD_RESOLVER.search(source):
            remaining.append(str(path.relative_to(project)))
    if remaining:
        raise ValueError(f"Remaining FMOD native resolvers: {remaining}")
    for path, preview in previews.items():
        if digest(path.read_bytes()) != digest(preview):
            path.write_bytes(preview)
    return {"native_imports_disabled": plan["native_import_count"],
            "remaining_fmod_native_resolvers": 0, "changed": changed,
            "sources": [{"path": entry["source"], "sha256": entry["patched_sha256"]}
                        for entry in plan["sources"]],
            "limits": "Generated iOS C++ boundary verification; device execution remains untested."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--plan", type=Path, default=Path(__file__).with_name("fmod-ios-native-plan.json"))
    parser.add_argument("--create-plan-from", type=Path, help="Audited iOS IL metadata JSON")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.create_plan_from:
        create_plan(args.project, args.create_plan_from, args.plan)
        print(f"Native boundary plan written: {args.plan}")
        return
    result = patch(args.project, json.loads(args.plan.read_text(encoding="utf-8")))
    rendered = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
