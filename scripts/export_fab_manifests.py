# Copyright Buckley Builds LLC 2026 All Rights Reserved.

"""Run inside Unreal Python to export sanitized owned-manifest inspection results in bounded batches.

Input is the editor's own Fab library: the `results` of FabToolset.SearchFabMyLibrary
(assetFormats="unreal-engine", every page), saved as a JSON list or as {"results": [...]}.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from fab_asset_catalog import _assert_safe_manifest_result


def _safe_error(error: BaseException) -> str:
    text = re.sub(r"https?://\S+", "[redacted-location]", str(error), flags=re.IGNORECASE)
    text = re.sub(r"(?i)(bearer|token|credential|signature)\s*[:=]\s*\S+", r"\1=[redacted]", text)
    return text[:500]


def _listings(listings_path: Path) -> list[dict[str, Any]]:
    data = json.loads(listings_path.read_text(encoding="utf-8"))
    listings = data.get("results", []) if isinstance(data, dict) else data
    ids = [str(listing.get("listingId") or "") for listing in listings]
    if not all(ids) or len(set(ids)) != len(ids):
        raise ValueError("listing ids must be present and distinct")
    return listings


def _engine_version(listing: dict[str, Any]) -> str:
    """The version to resolve: Fab's install choice for this editor, else the newest the listing offers."""
    for option in listing.get("installOptions") or []:
        if option.get("assetFormat") == "unreal-engine" and option.get("engineVersion"):
            return str(option["engineVersion"])

    def order(version: str) -> tuple[int, ...]:
        return tuple(int(part) for part in re.findall(r"\d+", version))

    return max((str(version) for version in listing.get("engineVersions") or []), key=order, default="")


def _failure(listing_id: str, code: str, error: str) -> dict[str, Any]:
    return {
        "success": False,
        "listing_id": listing_id,
        "artifact_id": "unresolved:" + listing_id,
        "error_code": code,
        "error": error,
    }


def export_batch(listings_path: str, output_path: str, start: int, count: int = 10) -> dict[str, int]:
    """Inspect [start:start+count], appending safe JSONL; each listing gets exactly one classified row."""
    import unreal

    output = Path(output_path)
    listings = _listings(Path(listings_path))
    if start < 0 or count <= 0 or start > len(listings):
        raise ValueError("invalid batch range")
    output.parent.mkdir(parents=True, exist_ok=True)
    if start == 0:
        output.write_text("", encoding="utf-8")
    elif not output.is_file():
        raise ValueError("cannot append a later batch before batch zero")

    existing: set[str] = set()
    for line in output.read_text(encoding="utf-8").splitlines():
        if line.strip():
            existing.add(str(json.loads(line).get("listing_id") or ""))

    succeeded = failed = 0
    selected = listings[start : start + count]
    with output.open("a", encoding="utf-8", newline="\n") as stream:
        for listing in selected:
            listing_id = str(listing["listingId"])
            if listing_id in existing:
                raise ValueError(f"duplicate export attempt for listing {listing_id}")
            engine_version = _engine_version(listing)
            if not engine_version:
                result = _failure(listing_id, "NO_UNREAL_FORMAT", "The listing offers no Unreal Engine version.")
            else:
                try:
                    raw = unreal.FabManifestService.inspect_owned_manifest(listing_id, engine_version, 0)
                    parsed = json.loads(raw)
                    if not isinstance(parsed, dict):
                        raise ValueError("operation did not return a JSON object")
                    if parsed.setdefault("listing_id", listing_id) != listing_id:
                        raise ValueError("operation returned a mismatched listing identity")
                    if not parsed.get("artifact_id"):
                        if parsed.get("success"):
                            raise ValueError("operation succeeded without an artifact identity")
                        parsed["artifact_id"] = "unresolved:" + listing_id
                    _assert_safe_manifest_result(parsed)
                    result = parsed
                except Exception as error:
                    result = _failure(listing_id, "INSPECTION_FAILED", _safe_error(error))
            _assert_safe_manifest_result(result)
            stream.write(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")
            stream.flush()
            existing.add(listing_id)
            if result.get("success"):
                succeeded += 1
            else:
                failed += 1

    return {
        "total_listings": len(listings),
        "batch_start": start,
        "batch_returned": len(selected),
        "exported_total": len(existing),
        "succeeded_in_batch": succeeded,
        "failed_in_batch": failed,
    }


def validate_export(listings_path: str, output_path: str) -> dict[str, int]:
    expected = {str(listing["listingId"]) for listing in _listings(Path(listings_path))}
    results: dict[str, dict[str, Any]] = {}
    artifacts: set[str] = set()
    for line_number, line in enumerate(Path(output_path).read_text(encoding="utf-8").splitlines(), 1):
        result = json.loads(line)
        _assert_safe_manifest_result(result, f"line {line_number}")
        listing_id = str(result.get("listing_id") or "")
        artifact_id = str(result.get("artifact_id") or "")
        if not listing_id or listing_id in results or not artifact_id or artifact_id in artifacts:
            raise ValueError(f"missing or duplicate listing or artifact id on line {line_number}")
        results[listing_id] = result
        artifacts.add(artifact_id)
    missing = expected - set(results)
    extra = set(results) - expected
    if missing or extra:
        raise ValueError(f"manifest export mismatch: missing={len(missing)}, extra={len(extra)}")
    return {
        "expected": len(expected),
        "exported": len(results),
        "succeeded": sum(bool(result.get("success")) for result in results.values()),
        "failed": sum(not bool(result.get("success")) for result in results.values()),
    }
