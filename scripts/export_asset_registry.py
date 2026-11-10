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


def _inspect(unreal: Any, registry: Any, options: Any, package_name: str) -> dict[str, Any]:
    """On-disk Asset Registry record for one package, read through the native registry; loads nothing."""
    assets = registry.get_assets_by_package_name(package_name, include_only_on_disk_assets=True) or []
    if not assets:
        return {
            "success": False,
            "error_code": "ASSET_REGISTRY_NOT_FOUND",
            "error": f"No on-disk Asset Registry data found for package '{package_name}'.",
        }

    def names(values: Any) -> list[str]:
        return sorted((str(value) for value in values or []), key=str.casefold)

    rows = []
    for asset in assets:
        object_path = f"{asset.package_name}.{asset.asset_name}"
        tags = unreal.EditorAssetLibrary.get_tag_values(object_path)
        rows.append({
            "package_name": str(asset.package_name),
            "package_path": str(asset.package_path),
            "object_path": object_path,
            "asset_name": str(asset.asset_name),
            "asset_class": f"{asset.asset_class_path.package_name}.{asset.asset_class_path.asset_name}",
            "tags": {str(key): str(value) for key, value in tags.items()},
        })
    return {
        "success": True,
        "package_name": package_name,
        "metadata_fidelity": "asset-registry",
        "registry_status": "enriched",
        "dependencies": names(registry.get_dependencies(package_name, options)),
        "referencers": names(registry.get_referencers(package_name, options)),
        "assets": rows,
    }


def export_current_project(project_root: str, output_path: str) -> dict[str, int]:
    """Query only already-mounted, on-disk packages; never load, save, mount, or import an asset."""
    import unreal

    root = Path(project_root).resolve()
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions()  # defaults: hard and soft package references
    rows: list[dict[str, Any]] = []
    enriched = failed = 0
    for package_name, file_path in _package_rows(root):
        parsed = _inspect(unreal, registry, options, package_name)
        parsed["filesystem_path"] = str(file_path)
        parsed["expected_package_name"] = package_name
        if parsed.get("success"):
            enriched += 1
        else:
            failed += 1
        rows.append(parsed)
    output.write_text("".join(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n" for row in rows), encoding="utf-8")
    return {"packages": len(rows), "enriched": enriched, "failed": failed}
