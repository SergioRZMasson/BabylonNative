"""Validate real visible sandbox drops, selected identity and complete empty clears."""

import json
import sys
from pathlib import Path


def verify(directory, expected_path):
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    events = [json.loads(line) for line in (directory / "events.jsonl").read_text(
        encoding="utf-8").splitlines()]
    assert len(events) == len(expected) and events, "All selected drops must execute"
    successes = 0
    failures = 0
    empties = 0
    nonempty = 0
    private_empty = 0
    active = None
    active_file = None
    previous = None
    empty_reference = None
    for i, (event, case) in enumerate(zip(events, expected)):
        assert event["item"] == i and event["visible"], "Actual visible drop required"
        assert event["success"] == case["success"], f"Drop {i}: unexpected outcome"
        image = (directory / f"drop-{i}.bgra").read_bytes()
        assert len(image) == 1280 * 720 * 4, "Complete GPU framebuffer required"
        if case["success"]:
            assert event["status"] == 0 and event["activeAssetId"] != active
            assert event["activeFile"] == case["path"], "New selected source is active"
            active = event["activeAssetId"]
            active_file = event["activeFile"]
            successes += 1
            if case["empty"]:
                assert event["draws"] == 0 and event["selectedPrimitives"] == 0
                assert event["emptySceneCamera"], "Explicit finite empty host policy required"
                pixel = image[:4]
                assert image == pixel * (1280 * 720), "Old geometry remains in empty frame"
                assert pixel[3] == 255, "Expected opaque clear"
                if empty_reference is None:
                    empty_reference = image
                assert image == empty_reference, "Empty clear must be deterministic"
                empties += 1
                private_empty += case.get("localPrivateEmptyOriginal", False)
            else:
                assert event["draws"] > 0 and event["selectedPrimitives"] > 0
                assert not event["emptySceneCamera"], "Nonempty must retain auto-frame path"
                # A complete clear or process-only pass is not nonempty rendering evidence.
                assert image != image[:4] * (1280 * 720), "Nonempty model did not render"
                nonempty += 1
            if case.get("sourceMeshes") is not None:
                assert event["sourceMeshes"] == case["sourceMeshes"]
            if case.get("selectedNodes") is not None:
                assert event["selectedNodes"] == case["selectedNodes"]
            if case.get("selectedScene") is not None:
                assert event["selectedScene"] == case["selectedScene"]
        else:
            assert event["status"] != 0 and event["activeAssetId"] == active
            assert event["activeFile"] == active_file
            assert image == previous, "Failed replacement changed the actual retained GPU frame"
            failures += 1
        previous = image
    teardown = json.loads((directory / "teardown.json").read_text(encoding="utf-8"))
    assert teardown["runtimeDecoderAllocatorLiveBytes"] == 0
    assert teardown["successfulDrops"] == successes and teardown["failedDrops"] == failures
    assert teardown["visibleWindow"] and teardown["actualWM_DROPFILES"]
    assert successes and failures and empties and nonempty
    report = dict(drops=len(events), successes=successes, failures=failures,
                  successfulEmptyClearFrames=empties, successfulNonemptyFrames=nonempty,
                  localPrivateEmptyOriginalDrops=private_empty, failureRetentionByteExact=True,
                  emptyFrameCompleteUniformClear=True, selectedSceneIdentityVerified=True,
                  trackedTeardownBytes=0, privateNamesContentHashesOmitted=True)
    (directory / "verified.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    return report


if __name__ == "__main__":
    verify(Path(sys.argv[1]), Path(sys.argv[2]))
