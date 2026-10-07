import argparse
import hashlib
import json
import pathlib
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("root", type=pathlib.Path)
parser.add_argument("qualified_runtime", type=pathlib.Path)
args = parser.parse_args()
assert "no-ui-comparison" in args.root.parts
source = args.root / "DawnBuild"
stock = args.qualified_runtime / "stock-dawn"
provider = args.qualified_runtime / "bgfx-dawn"
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
records = {}
for name, provider_path in [("StockDawn", stock), ("CompatibleBgfx", provider)]:
    destination = args.root / name
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / "bblite_native.exe", destination / "bblite_native.exe")
    for folder in ["assets", "shaders"]:
        shutil.copytree(source / folder, destination / folder, dirs_exist_ok=True)
    for file in stock.glob("*.dll"):
        shutil.copyfile(file, destination / file.name)
    shutil.copyfile(provider_path / "webgpu_dawn.dll", destination / "webgpu_dawn.dll")
    records[name] = {
        "exeSha256": sha(destination / "bblite_native.exe"),
        "providerSha256": sha(destination / "webgpu_dawn.dll"),
        "dependencies": {file.name: sha(file) for file in destination.glob("*.dll")},
        "sameCanonicalUserCpp": True,
        "hostBackend": "DAWN",
        "providerBinaryCommitted": False,
    }
for file in stock.glob("*.dll"):
    if file.name.lower() != "webgpu_dawn.dll":
        shutil.copyfile(file, args.root / "SDL" / file.name)
assert records["StockDawn"]["exeSha256"] == records["CompatibleBgfx"]["exeSha256"]
(args.root / "deployment.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
print("Deployed byte-identical stock/provider host exes and isolated runtime DLLs.")
