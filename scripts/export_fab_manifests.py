# Copyright Buckley Builds LLC 2026 All Rights Reserved.

"""Run inside Unreal Python to export sanitized owned-manifest inspection results in bounded batches."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from fab_asset_catalog import CONTENT_DISTRIBUTIONS, _assert_safe_manifest_result, _select_artifact


def _safe_error(error: BaseException) -> str:
    text = re.sub(r"https?://\S+", "[redacted-location]", str(error), flags=re.IGNORECASE)
    text = re.sub(r"(?i)(bearer|token|credential|signature)\s*[:=]\s*\S+", r"\1=[redacted]", text)
    return text[:500]


def _products(snapshot_path: Path) -> list[dict[str, Any]]:
    snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
    products = [
        product
        for product in snapshot.get("products", [])
        if str(product.get("distribution_method") or "").upper() in CONTENT_DISTRIBUTIONS
    ]
    ids = [str(product.get("id") or "") for product in products]
    if not all(ids) or len(set(ids)) != len(ids):
        raise ValueError("content product ids must be present and distinct")
    return products


def export_batch(
    snapshot_path: str,
    output_path: str,
    start: int,
    count: int = 10,
    engine_version: str = "5.8",
) -> dict[str, int]:
    """Inspect [start:start+count], appending safe JSONL; each product gets exactly one classified row."""
    import unreal

    snapshot = Path(snapshot_path)
    output = Path(output_path)
    products = _products(snapshot)
    if start < 0 or count <= 0 or start > len(products):
        raise ValueError("invalid batch range")
    output.parent.mkdir(parents=True, exist_ok=True)
    if start == 0:
        output.write_text("", encoding="utf-8")
    elif not output.is_file():
        raise ValueError("cannot append a later batch before batch zero")

    existing: set[str] = set()
    for line in output.read_text(encoding="utf-8").splitlines():
        if line.strip():
            existing.add(str(json.loads(line).get("artifact_id") or ""))

    succeeded = failed = 0
    selected = products[start : start + count]
    with output.open("a", encoding="utf-8", newline="\n") as stream:
        for product in selected:
            product_id = str(product["id"])
            artifact_id = _select_artifact(product, engine_version)
            if not artifact_id:
                result: dict[str, Any] = {
                    "success": False,
                    "product_id": product_id,
                    "artifact_id": "missing-artifact:" + product_id,
                    "error_code": "NO_ARTIFACT",
                    "error": "No artifact id was present in the owned-library snapshot.",
                }
            elif artifact_id in existing:
                raise ValueError(f"duplicate export attempt for artifact {artifact_id}")
            else:
                try:
                    raw = unreal.FabService.inspect_owned_manifest(product_id, engine_version, 0)
                    parsed = json.loads(raw)
                    if not isinstance(parsed, dict):
                        raise ValueError("operation did not return a JSON object")
                    parsed.setdefault("product_id", product_id)
                    parsed.setdefault("artifact_id", artifact_id)
                    if parsed["product_id"] != product_id or parsed["artifact_id"] != artifact_id:
                        raise ValueError("operation returned mismatched product/artifact identity")
                    _assert_safe_manifest_result(parsed)
                    result = parsed
                except Exception as error:
                    result = {
                        "success": False,
                        "product_id": product_id,
                        "artifact_id": artifact_id,
                        "error_code": "INSPECTION_FAILED",
                        "error": _safe_error(error),
                    }
            _assert_safe_manifest_result(result)
            stream.write(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")
            stream.flush()
            existing.add(str(result["artifact_id"]))
            if result.get("success"):
                succeeded += 1
            else:
                failed += 1

    return {
        "total_products": len(products),
        "batch_start": start,
        "batch_returned": len(selected),
        "exported_total": len(existing),
        "succeeded_in_batch": succeeded,
        "failed_in_batch": failed,
    }


def validate_export(snapshot_path: str, output_path: str, engine_version: str = "5.8") -> dict[str, int]:
    products = _products(Path(snapshot_path))
    expected = {_select_artifact(product, engine_version) for product in products}
    results: dict[str, dict[str, Any]] = {}
    for line_number, line in enumerate(Path(output_path).read_text(encoding="utf-8").splitlines(), 1):
        result = json.loads(line)
        _assert_safe_manifest_result(result, f"line {line_number}")
        artifact_id = str(result.get("artifact_id") or "")
        if not artifact_id or artifact_id in results:
            raise ValueError(f"missing or duplicate artifact id on line {line_number}")
        results[artifact_id] = result
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
