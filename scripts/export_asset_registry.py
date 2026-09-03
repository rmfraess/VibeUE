# Copyright Buckley Builds LLC 2026 All Rights Reserved.

"""Run inside Unreal Python to export on-disk Asset Registry evidence for the current project."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

PACKAGE_EXTENSIONS = {".uasset", ".umap"}


def _package_rows(project_root: Path) -> list[tuple[str, Path]]:
    rows: list[tuple[str, Path]] = []
    content = project_root / "Content"
    if content.is_dir():
        for path in sorted(content.rglob("*")):
            if path.is_file() and path.suffix.lower() in PACKAGE_EXTENSIONS:
                rows.append(("/Game/" + path.relative_to(content).with_suffix("").as_posix(), path))

    plugins = project_root / "Plugins"
    if plugins.is_dir():
        for descriptor in sorted(plugins.rglob("*.uplugin")):
            plugin_content = descriptor.parent / "Content"
            if not plugin_content.is_dir():
                continue
            mount = descriptor.stem
            for path in sorted(plugin_content.rglob("*")):
                if path.is_file() and path.suffix.lower() in PACKAGE_EXTENSIONS:
                    rows.append((f"/{mount}/" + path.relative_to(plugin_content).with_suffix("").as_posix(), path))
    return rows


def export_current_project(project_root: str, output_path: str) -> dict[str, int]:
    """Query only already-mounted, on-disk packages; never load, save, mount, or import an asset."""
    import unreal

    root = Path(project_root).resolve()
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    enriched = failed = 0
    for package_name, file_path in _package_rows(root):
        parsed = json.loads(unreal.FabService.inspect_asset_registry_package(package_name))
        parsed["filesystem_path"] = str(file_path)
        parsed["expected_package_name"] = package_name
        if parsed.get("success"):
            enriched += 1
        else:
            failed += 1
        rows.append(parsed)
    output.write_text("".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n" for row in rows), encoding="utf-8")
    return {"packages": len(rows), "enriched": enriched, "failed": failed}
