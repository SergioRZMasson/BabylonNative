import pathlib
import subprocess
import sys
import uuid

build = pathlib.Path(sys.argv[1]).resolve()
name = sys.argv[2]
settings = {
    "idle180": ["--frames=180"],
    "idle1380": ["--frames=1380"],
    "replay240": ["--frames=240", "--replay"],
    "save-load240": ["--frames=240", "--replay", "--save-load-test"],
    "toast-expiry500": ["--frames=500", "--replay", "--save-load-test"],
    "resize180": ["--frames=180", "--resize-frame=60", "--density=1.5"],
    "underwater180": ["--frames=180", "--underwater-ui-test"],
}
assert name in settings
assert build.name.startswith("Native") and build.parent.name == "rmlui-minecraft"
output = build / "Cases" / f"{name}-{uuid.uuid4().hex[:12]}"
output.mkdir(parents=True)
command = [str(build / "LiteMinecraftRmlUiNative.exe"), "--headless", "--validate-bindings",
           f"--capture-root={output}", f"--summary={output / 'summary.json'}", *settings[name]]
with (output / "run.log").open("w") as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=300)
print(output)
if result.returncode:
    print((output / "run.log").read_text(errors="replace"))
    sys.exit(result.returncode)
verification = [sys.executable, str(pathlib.Path(__file__).with_name("verify-run.py")),
                str(output / "summary.json")]
if name == "idle180":
    verification.append(str(build.parent / "BaselineQualification180" / "SDL" / "render.json"))
subprocess.run(verification, check=True)
