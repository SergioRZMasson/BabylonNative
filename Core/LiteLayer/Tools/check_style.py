import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def command_arguments(command):
    if os.name != "nt":
        return shlex.split(command)
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    count = ctypes.c_int()
    values = shell.CommandLineToArgvW(command, ctypes.byref(count))
    if not values:
        raise OSError(ctypes.get_last_error(), "Cannot parse the compiler command")
    try:
        return [values[index] for index in range(count.value)]
    finally:
        kernel.LocalFree(values)


def location(node):
    value = node.get("loc", node.get("range", {}).get("begin", {}))
    return value.get("expansionLoc", value)


def start_offset(node):
    value = node.get("range", {}).get("begin", {})
    return value.get("spellingLoc", value).get("offset")


def multiple_declarations(nodes, errors, label):
    groups = {}
    for node in nodes:
        offset = start_offset(node)
        if offset is not None:
            groups.setdefault(offset, []).append(node)
    for group in groups.values():
        if len(group) > 1:
            names = ", ".join(node.get("name", "?") for node in group)
            errors.add(f"{label}: use separate declarations for {names}")


def inspect_body(node, errors, label):
    children = node.get("inner", [])
    kind = node.get("kind")
    if kind == "DeclStmt":
        variables = [child for child in children if child.get("kind") == "VarDecl"]
        if len(variables) > 1:
            names = ", ".join(child.get("name", "?") for child in variables)
            errors.add(f"{label}: use separate local declarations for {names}")
    if kind == "IfStmt" and children:
        if node.get("hasElse"):
            branches = [children[-2], children[-1]]
        else:
            branches = [children[-1]]
        for index, branch in enumerate(branches):
            if branch.get("kind") != "CompoundStmt" and not (
                node.get("hasElse") and index == 1 and branch.get("kind") == "IfStmt"
            ):
                errors.add(f"{label}: if/else bodies must use braces")
    elif kind in ("ForStmt", "WhileStmt", "SwitchStmt", "CXXForRangeStmt"):
        if children and children[-1].get("kind") != "CompoundStmt":
            errors.add(f"{label}: {kind} body must use braces")
    elif kind == "DoStmt":
        if children and children[0].get("kind") != "CompoundStmt":
            errors.add(f"{label}: do/while body must use braces")
    for child in children:
        inspect_body(child, errors, label)


def inspect_ast(tree, source, root, errors):
    record_names = set()
    declaration = re.compile(
        r"(?m)^\s*(?:typedef\s+)?(?:struct|union|class)\s+([A-Za-z_]\w*)"
        r"(?:\s+final)?(?:\s*:\s*[^{]+)?\s*\{"
    )
    for path in root.rglob("*"):
        if path == root / "json" / "json.hpp":
            continue
        if path.suffix in (".h", ".hpp", ".c", ".cpp") and path.is_file():
            record_names.update(declaration.findall(path.read_text(encoding="utf-8-sig")))

    def visit(node):
        children = node.get("inner", [])
        name = node.get("name", "")
        if node.get("kind") in ("RecordDecl", "CXXRecordDecl") and name in record_names:
            fields = [child for child in children if child.get("kind") == "FieldDecl"]
            multiple_declarations(fields, errors, name)
        if node.get("kind") in ("FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl",
                                "CXXDestructorDecl"):
            where = location(node)
            if not where.get("includedFrom") and (
                not where.get("file") or Path(where["file"]).resolve() == source
            ):
                for child in children:
                    if child.get("kind") == "CompoundStmt":
                        inspect_body(child, errors, name)
        if node.get("kind") in ("TranslationUnitDecl", "LinkageSpecDecl"):
            globals_ = []
            for child in children:
                where = location(child)
                if child.get("kind") == "VarDecl" and not where.get("includedFrom") and (
                    not where.get("file") or Path(where["file"]).resolve().is_relative_to(root)
                ):
                    globals_.append(child)
            multiple_declarations(globals_, errors, str(source))
        for child in children:
            visit(child)

    visit(tree)


def compiler_ast(entry, clang, source):
    args = entry.get("arguments") or command_arguments(entry["command"])
    msvc = Path(args[0]).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")
    args = args[1:]
    if msvc:
        args = [
            arg for arg in args
            if not arg.lower().startswith(("/fo", "/fd")) and
            arg.lower() not in ("/showincludes", "/mp", "/zc:preprocessor", "/c")
        ]
    command = [clang]
    command += ["--driver-mode=cl", "/Zs"] if msvc else ["-fsyntax-only"]
    command += args + ["-Wno-unused-command-line-argument", "-Xclang", "-ast-dump=json"]
    result = subprocess.run(command, cwd=entry["directory"], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{source}: Clang parsing failed:\n{result.stderr}")
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--clang-format", required=True)
    parser.add_argument("--ui-enabled", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    files = sorted(
        path for directory in ("Include", "Source")
        for path in (root / directory).iterdir()
        if path.suffix in (".h", ".hpp", ".c", ".cpp")
    )
    subprocess.run(
        [args.clang_format, "--dry-run", "--Werror", *map(str, files)], check=True
    )
    database = json.loads(args.database.read_text(encoding="utf-8-sig"))
    sources = {path.resolve() for path in files if path.suffix in (".cpp", ".c")}
    inactive_ui = ("UiDisabled.cpp",) if args.ui_enabled else (
        "UiGpu.cpp", "UiRmlAdapter.cpp", "UiLayers.cpp"
    )
    sources -= {(root / "Source" / name).resolve() for name in inactive_ui}
    checked = set()
    errors = set()
    for entry in database:
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        source = source.resolve()
        if source not in sources or source in checked:
            continue
        inspect_ast(compiler_ast(entry, args.clang, source), source, root, errors)
        checked.add(source)
    if checked != sources:
        missing = ", ".join(map(str, sorted(sources - checked)))
        raise RuntimeError(f"Missing LiteLayer compiler commands: {missing}")
    if errors:
        raise RuntimeError("\n".join(sorted(errors)))
    print(f"LiteLayer style: {len(files)} formatted files, {len(checked)} parsed translation units.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
