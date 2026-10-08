import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def run(command, cwd, success=True):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    if (result.returncode == 0) != success:
        raise AssertionError(result.stdout + result.stderr)
    return result


def main():
    repository = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    fixture = Path(sys.argv[3]).resolve()
    fixture.mkdir(parents=True, exist_ok=True)
    files = ["Source/Core/BoxShadowCache.h", "Source/Core/BoxShadowCache.cpp"]
    for name in ["CMakeLists.txt", "LICENSE.txt", *files]:
        destination = fixture / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text((source / name).read_text(), encoding="utf-8")
    patch = repository / "Dependencies/RmlUi/Patches/BoxShadowRenderManager.patch"
    run(["git", "apply", "--reverse", "--unsafe-paths", f"--directory={fixture.as_posix()}",
         str(patch)], repository)
    runner = fixture / "RunPatch.cmake"
    hook = repository / "Dependencies/RmlUi/ApplyBoxShadowPatch.cmake"
    runner.write_text(
        f'set(CMAKE_BINARY_DIR "{fixture.as_posix()}")\n'
        f'include("{hook.as_posix()}")\n'
        f'babylon_lite_patch_rmlui("{fixture.as_posix()}")\n', encoding="utf-8")
    run(["cmake", "-P", str(runner)], repository)
    first = {name: hashlib.sha256((fixture / name).read_bytes()).hexdigest() for name in files}
    run(["cmake", "-P", str(runner)], repository)
    assert first == {
        name: hashlib.sha256((fixture / name).read_bytes()).hexdigest() for name in files
    }, "Idempotent application changed source bytes"
    receipt = json.loads((fixture / "RmlUiBoxShadowPatch.json").read_text())
    assert receipt["verified"] and len(receipt["before"]) == 2 and len(receipt["after"]) == 2
    damaged = fixture / files[0]
    damaged.write_text(damaged.read_text() + "\n// hash mismatch fixture\n")
    rejected = run(["cmake", "-P", str(runner)], repository, success=False)
    assert "patch source mismatch" in rejected.stderr
    assert not json.loads((fixture / "RmlUiBoxShadowPatch.json").read_text())["verified"]
    assert damaged.read_text().endswith("// hash mismatch fixture\n")
    shutil.rmtree(fixture)
    print("RmlUI patch: pristine apply, exact receipt, idempotence and mismatch rejection passed")


if __name__ == "__main__":
    main()
