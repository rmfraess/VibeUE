#!/usr/bin/env python
# Copyright Buckley Builds LLC 2026 All Rights Reserved.

"""Build and query a local, provenance-preserving Fab asset catalog."""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

SCHEMA_VERSION = 3
CONTENT_DISTRIBUTIONS = {"ASSET_PACK", "COMPLETE_PROJECT", "CODE_PLUGIN"}
LOCAL_EXTENSIONS = {".uasset", ".umap"}
PROHIBITED_KEYS = {
    "account_id",
    "authorization",
    "base_urls",
    "cookie",
    "download_url",
    "entitlement_id",
    "epic_account_id",
    "manifest_url",
    "signed_url",
    "token",
    "url",
}
PROHIBITED_VALUE_MARKERS = (
    "authorization:",
    "bearer ",
    "https://",
    "http://",
    "x-amz-",
    "signature=",
)
SKIP_DIRECTORY_NAMES = {
    ".git", ".worktrees", ".venv", "Binaries", "DerivedDataCache", "Intermediate",
    "node_modules", "Saved", "__pycache__",
}

SCHEMA = """
PRAGMA foreign_keys = ON;
CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE products (
    product_id TEXT PRIMARY KEY,
    artifact_id TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL,
    distribution_method TEXT NOT NULL,
    listing_type TEXT NOT NULL,
    source TEXT NOT NULL,
    engine_version TEXT NOT NULL,
    engine_compatible INTEGER NOT NULL CHECK (engine_compatible IN (0, 1)),
    installed_state TEXT NOT NULL DEFAULT 'not-cached',
    description TEXT NOT NULL,
    publisher_name TEXT NOT NULL,
    source_product_id TEXT NOT NULL,
    listing_url TEXT NOT NULL,
    preview_references_json TEXT NOT NULL,
    evidence_reference TEXT NOT NULL,
    reported_engine_versions_json TEXT NOT NULL,
    reported_platforms_json TEXT NOT NULL,
    project_versions_json TEXT NOT NULL,
    metadata_fidelity TEXT NOT NULL,
    compatibility_status TEXT NOT NULL,
    adoption_state TEXT NOT NULL,
    corpus TEXT NOT NULL,
    asset_namespace TEXT NOT NULL,
    thumbnail_url TEXT NOT NULL
);
CREATE TABLE manifest_attempts (
    artifact_id TEXT PRIMARY KEY REFERENCES products(artifact_id),
    product_id TEXT NOT NULL REFERENCES products(product_id),
    status TEXT NOT NULL,
    error_code TEXT NOT NULL DEFAULT '',
    error TEXT NOT NULL DEFAULT '',
    manifest_bytes INTEGER,
    file_count INTEGER,
    build_size_bytes INTEGER
);
CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    product_id TEXT NOT NULL REFERENCES products(product_id),
    artifact_id TEXT NOT NULL REFERENCES products(artifact_id),
    path TEXT NOT NULL,
    file_name TEXT NOT NULL,
    extension TEXT NOT NULL,
    size_bytes INTEGER,
    source_tier TEXT NOT NULL,
    metadata_fidelity TEXT NOT NULL,
    installed_state TEXT NOT NULL,
    type_hint TEXT NOT NULL,
    type_inference TEXT NOT NULL,
    corpus TEXT NOT NULL,
    source TEXT NOT NULL,
    compatibility_status TEXT NOT NULL,
    adoption_state TEXT NOT NULL,
    asset_registry_status TEXT NOT NULL,
    UNIQUE (artifact_id, path, source_tier)
);
CREATE TABLE corpora (
    corpus_id TEXT PRIMARY KEY,
    corpus_type TEXT NOT NULL,
    name TEXT NOT NULL,
    root_path TEXT NOT NULL,
    descriptor_path TEXT NOT NULL,
    include_by_default INTEGER NOT NULL CHECK (include_by_default IN (0, 1)),
    source TEXT NOT NULL,
    adoption_state TEXT NOT NULL,
    engine_association TEXT NOT NULL,
    supported_platforms_json TEXT NOT NULL
);
CREATE TABLE local_packages (
    id INTEGER PRIMARY KEY,
    corpus_id TEXT NOT NULL REFERENCES corpora(corpus_id),
    filesystem_path TEXT NOT NULL UNIQUE,
    relative_path TEXT NOT NULL,
    package_name TEXT NOT NULL,
    file_name TEXT NOT NULL,
    extension TEXT NOT NULL,
    size_bytes INTEGER NOT NULL,
    source TEXT NOT NULL,
    metadata_fidelity TEXT NOT NULL,
    compatibility_status TEXT NOT NULL,
    adoption_state TEXT NOT NULL,
    asset_registry_status TEXT NOT NULL
);
CREATE TABLE asset_registry_assets (
    id INTEGER PRIMARY KEY,
    local_package_id INTEGER NOT NULL REFERENCES local_packages(id),
    package_name TEXT NOT NULL,
    package_path TEXT NOT NULL,
    object_path TEXT NOT NULL,
    asset_name TEXT NOT NULL,
    asset_class TEXT NOT NULL,
    tags_json TEXT NOT NULL,
    dependencies_json TEXT NOT NULL,
    referencers_json TEXT NOT NULL,
    metadata_fidelity TEXT NOT NULL,
    UNIQUE (local_package_id, object_path)
);
CREATE TABLE fab_asset_registry_assets (
    id INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id),
    package_name TEXT NOT NULL,
    package_path TEXT NOT NULL,
    object_path TEXT NOT NULL,
    asset_name TEXT NOT NULL,
    asset_class TEXT NOT NULL,
    tags_json TEXT NOT NULL,
    metadata_fidelity TEXT NOT NULL,
    UNIQUE (file_id, object_path)
);
CREATE VIRTUAL TABLE catalog_fts USING fts5(
    product_id UNINDEXED,
    artifact_id UNINDEXED,
    product_title UNINDEXED,
    search_title,
    path,
    file_name,
    source_tier UNINDEXED,
    metadata_fidelity UNINDEXED,
    installed_state UNINDEXED,
    distribution_method UNINDEXED,
    engine_compatible UNINDEXED,
    type_hint UNINDEXED,
    record_kind UNINDEXED,
    description,
    corpus_id UNINDEXED,
    corpus_type UNINDEXED,
    source UNINDEXED,
    compatibility_status UNINDEXED,
    adoption_state UNINDEXED,
    asset_registry_status UNINDEXED,
    object_path,
    asset_class,
    evidence_reference,
    publisher_name,
    source_product_id UNINDEXED,
    listing_url,
    preview_references,
    reported_engine_versions,
    reported_platforms,
    asset_namespace,
    thumbnail_url
);
"""


def _select_artifact(product: dict[str, Any], engine_version: str) -> str:
    versions = product.get("project_versions") or []
    for version in versions:
        advertised = version.get("engine_versions") or []
        if any(str(item).endswith(engine_version) for item in advertised):
            artifact_id = str(version.get("artifact_id") or "")
            if artifact_id:
                return artifact_id
    for version in versions:
        artifact_id = str(version.get("artifact_id") or "")
        if artifact_id:
            return artifact_id
    return ""


def _type_hint(path: str) -> tuple[str, str]:
    extension = Path(path).suffix.lower()
    if extension == ".umap":
        return "unreal-map", "heuristic"
    if extension == ".uasset":
        return "unreal-package", "heuristic"
    return extension.removeprefix(".") or "file", "heuristic"


def _assert_safe_manifest_result(value: Any, location: str = "result") -> None:
    if isinstance(value, dict):
        for key, item in value.items():
            if key.casefold() in PROHIBITED_KEYS:
                raise ValueError(f"prohibited manifest field at {location}.{key}")
            _assert_safe_manifest_result(item, f"{location}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _assert_safe_manifest_result(item, f"{location}[{index}]")
    elif isinstance(value, str):
        lowered = value.casefold()
        if any(marker in lowered for marker in PROHIBITED_VALUE_MARKERS):
            raise ValueError(f"prohibited secret or URL value at {location}")


def _load_manifest_results(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    results: dict[str, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            result = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"malformed manifest result on line {line_number}: {error.msg}") from error
        _assert_safe_manifest_result(result, f"line {line_number}")
        artifact_id = str(result.get("artifact_id") or "")
        if not artifact_id:
            raise ValueError(f"manifest result on line {line_number} has no artifact_id")
        if artifact_id in results:
            raise ValueError(f"duplicate manifest result for artifact_id {artifact_id}")
        results[artifact_id] = result
    return results


def _assert_safe_public_reference(value: str, location: str) -> None:
    lowered = value.casefold()
    if any(marker in lowered for marker in ("bearer ", "x-amz-", "signature=", "credential=", "token=")):
        raise ValueError(f"prohibited signed or credential reference at {location}")


def _load_registry_results(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    results: dict[str, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        result = json.loads(line)
        package_name = str(result.get("expected_package_name") or result.get("package_name") or "")
        if not package_name or package_name in results:
            raise ValueError(f"missing or duplicate Asset Registry package on line {line_number}")
        if result.get("success") and result.get("package_name") != package_name:
            raise ValueError(f"Asset Registry package identity mismatch on line {line_number}")
        results[package_name] = result
    return results


def _fab_package_name_from_content_path(value: str) -> str:
    path = PurePosixPath(value.replace("\\", "/"))
    if (
        path.is_absolute()
        or ".." in path.parts
        or len(path.parts) < 2
        or path.parts[0] != "Content"
        or path.suffix.lower() not in LOCAL_EXTENSIONS
    ):
        raise ValueError(f"invalid expanded Fab Content package path: {value}")
    return "/Game/" + path.relative_to("Content").with_suffix("").as_posix()


def _load_fab_registry_results(path: Path | None) -> dict[str, dict[str, dict[str, Any]]]:
    if path is None:
        return {}
    results: dict[str, dict[str, dict[str, Any]]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            result = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"malformed Fab Asset Registry result on line {line_number}: {error.msg}") from error
        if not isinstance(result, dict):
            raise ValueError(f"Fab Asset Registry result on line {line_number} must be an object")
        _assert_safe_manifest_result(result, f"line {line_number}")
        artifact_id = str(result.get("artifact_id") or "")
        product_id = str(result.get("product_id") or "")
        package_name = str(result.get("expected_package_name") or result.get("package_name") or "")
        relative_path = str(result.get("path") or "")
        if not artifact_id or not product_id or not package_name or not relative_path:
            raise ValueError(f"Fab Asset Registry result on line {line_number} is missing package identity")
        if _fab_package_name_from_content_path(relative_path) != package_name:
            raise ValueError(f"Fab Asset Registry package/path mismatch on line {line_number}")
        success = result.get("success")
        assets = result.get("assets")
        if not isinstance(success, bool) or not isinstance(assets, list):
            raise ValueError(f"invalid Fab Asset Registry result on line {line_number}")
        if result.get("package_name") and str(result["package_name"]) != package_name:
            raise ValueError(f"Fab Asset Registry package identity mismatch on line {line_number}")
        if not success and assets:
            raise ValueError(f"failed Fab Asset Registry package has asset rows on line {line_number}")
        package_results = results.setdefault(artifact_id, {})
        if package_name in package_results:
            raise ValueError(f"duplicate Fab Asset Registry package {package_name} for artifact {artifact_id}")
        package_results[package_name] = result
    return results


def _walk_descriptors(roots: Iterable[Path], include_engine: bool, engine_root: Path | None) -> tuple[list[Path], list[Path]]:
    projects: set[Path] = set()
    plugins: set[Path] = set()
    search_roots = [Path(root).resolve() for root in roots]
    if include_engine and engine_root is not None:
        search_roots.append(engine_root.resolve())
    for root in search_roots:
        if not root.is_dir():
            continue
        for directory, children, names in os.walk(root):
            children[:] = [child for child in children if child not in SKIP_DIRECTORY_NAMES and not child.startswith(".")]
            current = Path(directory)
            for name in names:
                lower = name.casefold()
                if lower.endswith(".uproject"):
                    projects.add((current / name).resolve())
                elif lower.endswith(".uplugin"):
                    plugins.add((current / name).resolve())
    return sorted(projects), sorted(plugins)


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _discover_local_corpora(
    roots: Iterable[Path], current_project: Path | None, include_engine: bool, engine_root: Path | None,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    projects, plugins = _walk_descriptors(roots, include_engine, engine_root)
    current_root = current_project.resolve().parent if current_project is not None else None
    resolved_engine = engine_root.resolve() if engine_root is not None else None
    corpora: list[dict[str, Any]] = []
    packages: list[dict[str, Any]] = []

    def add_corpus(descriptor: Path, kind: str, content_root: Path, mount: str) -> None:
        data = json.loads(descriptor.read_text(encoding="utf-8-sig"))
        under_current = current_root is not None and _is_relative_to(descriptor, current_root)
        under_engine = resolved_engine is not None and _is_relative_to(descriptor, resolved_engine)
        test_corpus = "test" in {part.casefold() for part in descriptor.parts}
        if under_engine:
            corpus_type = f"engine-{kind}"
        elif kind == "project" and descriptor.parent == current_root:
            corpus_type = "wot-project"
        elif under_current:
            corpus_type = "wot-tooling-plugin"
        elif test_corpus:
            corpus_type = f"tooling-test-{kind}"
        else:
            corpus_type = f"local-{kind}"
        corpus_id = descriptor.as_posix()
        adoption_state = "existing-project" if corpus_type == "wot-project" else "source"
        source = "engine" if under_engine else "local"
        supported = data.get("SupportedTargetPlatforms") or data.get("TargetPlatforms") or []
        corpora.append(
            {
                "corpus_id": corpus_id,
                "corpus_type": corpus_type,
                "name": descriptor.stem,
                "root_path": str(descriptor.parent),
                "descriptor_path": str(descriptor),
                "include_by_default": not under_engine,
                "source": source,
                "adoption_state": adoption_state,
                "engine_association": str(data.get("EngineAssociation") or data.get("EngineVersion") or ""),
                "supported_platforms_json": json.dumps(supported, sort_keys=True),
            }
        )
        if not content_root.is_dir():
            return
        current_registry_corpus = corpus_type in {"wot-project", "wot-tooling-plugin"}
        for file_path in sorted(content_root.rglob("*")):
            if not file_path.is_file() or file_path.suffix.casefold() not in LOCAL_EXTENSIONS:
                continue
            relative = file_path.relative_to(content_root)
            package_name = mount + "/" + relative.with_suffix("").as_posix()
            type_hint, inference = _type_hint(relative.as_posix())
            packages.append(
                {
                    "corpus_id": corpus_id,
                    "corpus_type": corpus_type,
                    "corpus_name": descriptor.stem,
                    "filesystem_path": str(file_path),
                    "relative_path": relative.as_posix(),
                    "package_name": package_name,
                    "file_name": file_path.name,
                    "extension": file_path.suffix.casefold(),
                    "size_bytes": file_path.stat().st_size,
                    "source": source,
                    "metadata_fidelity": "filesystem",
                    "compatibility_status": "current-project" if current_registry_corpus else "not-evaluated",
                    "adoption_state": adoption_state,
                    "asset_registry_status": "not-attempted" if current_registry_corpus else "unavailable-not-current-project",
                    "type_hint": type_hint,
                    "type_inference": inference,
                }
            )

    for descriptor in projects:
        add_corpus(descriptor, "project", descriptor.parent / "Content", "/Game")
    for descriptor in plugins:
        under_engine = resolved_engine is not None and _is_relative_to(descriptor, resolved_engine)
        if under_engine and not include_engine:
            continue
        add_corpus(descriptor, "plugin", descriptor.parent / "Content", "/" + descriptor.stem)
    if include_engine and resolved_engine is not None:
        content_root = resolved_engine / "Engine" / "Content"
        corpus_id = (content_root / "<engine-content>").as_posix()
        corpora.append(
            {
                "corpus_id": corpus_id,
                "corpus_type": "engine-content",
                "name": "Unreal Engine Content",
                "root_path": str(content_root),
                "descriptor_path": "",
                "include_by_default": False,
                "source": "engine",
                "adoption_state": "source",
                "engine_association": "",
                "supported_platforms_json": "[]",
            }
        )
        if content_root.is_dir():
            for file_path in sorted(content_root.rglob("*")):
                if not file_path.is_file() or file_path.suffix.casefold() not in LOCAL_EXTENSIONS:
                    continue
                relative = file_path.relative_to(content_root)
                type_hint, inference = _type_hint(relative.as_posix())
                packages.append(
                    {
                        "corpus_id": corpus_id,
                        "corpus_type": "engine-content",
                        "corpus_name": "Unreal Engine Content",
                        "filesystem_path": str(file_path),
                        "relative_path": relative.as_posix(),
                        "package_name": "/Engine/" + relative.with_suffix("").as_posix(),
                        "file_name": file_path.name,
                        "extension": file_path.suffix.casefold(),
                        "size_bytes": file_path.stat().st_size,
                        "source": "engine",
                        "metadata_fidelity": "filesystem",
                        "compatibility_status": "engine-provided",
                        "adoption_state": "source",
                        "asset_registry_status": "unavailable-engine-not-exported",
                        "type_hint": type_hint,
                        "type_inference": inference,
                    }
                )
    return corpora, packages


def _insert_search_row(connection: sqlite3.Connection, **row: Any) -> None:
    columns = (
        "product_id", "artifact_id", "product_title", "search_title", "path", "file_name", "source_tier",
        "metadata_fidelity", "installed_state", "distribution_method", "engine_compatible", "type_hint",
        "record_kind", "description", "corpus_id", "corpus_type", "source", "compatibility_status",
        "adoption_state", "asset_registry_status", "object_path", "asset_class", "evidence_reference",
        "publisher_name", "source_product_id", "listing_url",
        "preview_references", "reported_engine_versions", "reported_platforms",
        "asset_namespace", "thumbnail_url",
    )
    search_row = dict(row)
    search_row.setdefault("search_title", search_row.get("product_title", ""))
    connection.execute(
        f"INSERT INTO catalog_fts VALUES ({','.join('?' for _ in columns)})",
        tuple(str(search_row.get(column, "")) for column in columns),
    )


def sync_catalog(
    snapshot_path: Path,
    vault_cache: Path,
    manifest_results_path: Path | None,
    database_path: Path,
    engine_version: str,
    *,
    scan_roots: Iterable[Path] = (),
    current_project: Path | None = None,
    registry_results_path: Path | None = None,
    fab_registry_results_path: Path | None = None,
    include_engine: bool = False,
    engine_root: Path | None = None,
) -> dict[str, int]:
    """Atomically rebuild from read-only product, manifest, filesystem, and Asset Registry evidence."""
    snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
    products = [
        product
        for product in snapshot.get("products", [])
        if str(product.get("distribution_method") or "").upper() in CONTENT_DISTRIBUTIONS
    ]
    product_ids = [str(product.get("id") or "") for product in products]
    if not all(product_ids) or len(set(product_ids)) != len(product_ids):
        raise ValueError("content product ids must be present and distinct")

    selected: dict[str, dict[str, Any]] = {}
    for product in products:
        artifact_id = _select_artifact(product, engine_version)
        if not artifact_id:
            raise ValueError(f"content product {product['id']} has no artifact id")
        if artifact_id in selected:
            raise ValueError(f"duplicate artifact id {artifact_id}")
        selected[artifact_id] = product

    manifest_results = _load_manifest_results(manifest_results_path)
    unknown_results = sorted(set(manifest_results) - set(selected))
    if unknown_results:
        raise ValueError(f"manifest results contain unknown artifact ids: {', '.join(unknown_results)}")
    registry_results = _load_registry_results(registry_results_path)
    fab_registry_results = _load_fab_registry_results(fab_registry_results_path)
    unknown_fab_results = sorted(set(fab_registry_results) - set(selected))
    if unknown_fab_results:
        raise ValueError(f"Fab Asset Registry results contain unknown artifact ids: {', '.join(unknown_fab_results)}")
    corpora, local_packages = _discover_local_corpora(scan_roots, current_project, include_engine, engine_root)

    database_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = database_path.with_name(database_path.name + ".tmp")
    temporary.unlink(missing_ok=True)
    connection: sqlite3.Connection | None = None
    try:
        with sqlite3.connect(temporary) as connection:
            connection.row_factory = sqlite3.Row
            connection.executescript(SCHEMA)
            connection.execute("INSERT INTO metadata VALUES ('schema_version', ?)", (str(SCHEMA_VERSION),))
            connection.execute("INSERT INTO metadata VALUES ('engine_version', ?)", (engine_version,))
            connection.execute("INSERT INTO metadata VALUES ('snapshot_path', ?)", (str(snapshot_path),))
            connection.execute("INSERT INTO metadata VALUES ('fab_registry_results_path', ?)", (str(fab_registry_results_path or ""),))

            for artifact_id, product in selected.items():
                project_versions = product.get("project_versions") or []
                reported_engines = sorted(
                    {str(value) for version in project_versions for value in (version.get("engine_versions") or [])}
                )
                reported_platforms = sorted(
                    {str(value) for version in project_versions for value in (version.get("target_platforms") or [])}
                )
                listing_url = str(product.get("url") or "")
                thumbnail_url = str(product.get("thumbnail_url") or "")
                previews = product.get("images") or []
                _assert_safe_public_reference(listing_url, f"product {product['id']} listing")
                _assert_safe_public_reference(thumbnail_url, f"product {product['id']} thumbnail")
                for index, preview in enumerate(previews):
                    _assert_safe_public_reference(str(preview.get("url") or ""), f"product {product['id']} preview {index}")
                compatible = bool(product.get("compatible"))
                connection.execute(
                    """INSERT INTO products
                       (product_id, artifact_id, title, distribution_method, listing_type, source, engine_version,
                        engine_compatible, installed_state, description, publisher_name, source_product_id,
                        listing_url, preview_references_json, evidence_reference, reported_engine_versions_json,
                        reported_platforms_json, project_versions_json, metadata_fidelity, compatibility_status,
                        adoption_state, corpus, asset_namespace, thumbnail_url)
                       VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'not-cached', ?, ?, ?, ?, ?, ?, ?, ?, ?,
                               'listing', ?, 'source', 'fab-owned', ?, ?)""",
                    (
                        str(product["id"]),
                        artifact_id,
                        str(product.get("title") or ""),
                        str(product.get("distribution_method") or ""),
                        str(product.get("listing_type") or ""),
                        str(product.get("source") or ""),
                        engine_version,
                        int(compatible),
                        str(product.get("description") or ""),
                        str(product.get("seller") or product.get("publisher") or ""),
                        str(product["id"]),
                        listing_url,
                        json.dumps(previews, ensure_ascii=False, sort_keys=True),
                        f"{snapshot_path.name}#product:{product['id']}",
                        json.dumps(reported_engines),
                        json.dumps(reported_platforms),
                        json.dumps(project_versions, ensure_ascii=False, sort_keys=True),
                        str(product.get("compatibility_status") or ("reported-compatible" if compatible else "reported-incompatible")),
                        str(product.get("asset_namespace") or ""),
                        thumbnail_url,
                    ),
                )

            for artifact_id, product in selected.items():
                product_id = str(product["id"])
                result = manifest_results.get(artifact_id)
                if result is None:
                    connection.execute(
                        "INSERT INTO manifest_attempts (artifact_id, product_id, status) VALUES (?, ?, 'not_attempted')",
                        (artifact_id, product_id),
                    )
                    continue
                if str(result.get("product_id") or "") != product_id:
                    raise ValueError(f"manifest result identity mismatch for artifact {artifact_id}")
                success = bool(result.get("success"))
                status = "succeeded" if success else "failed"
                connection.execute(
                    """INSERT INTO manifest_attempts
                       (artifact_id, product_id, status, error_code, error, manifest_bytes, file_count, build_size_bytes)
                       VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
                    (
                        artifact_id,
                        product_id,
                        status,
                        str(result.get("error_code") or ""),
                        str(result.get("error") or "")[:500],
                        result.get("manifest_bytes"),
                        result.get("total_files"),
                        result.get("build_size_bytes"),
                    ),
                )
                if not success:
                    continue
                for item in result.get("files") or []:
                    path = str(item.get("path") or "").replace("\\", "/")
                    if not path:
                        raise ValueError(f"empty manifest path for artifact {artifact_id}")
                    type_hint, inference = _type_hint(path)
                    connection.execute(
                        """INSERT INTO files
                           (product_id, artifact_id, path, file_name, extension, size_bytes, source_tier,
                            metadata_fidelity, installed_state, type_hint, type_inference, corpus, source,
                            compatibility_status, adoption_state, asset_registry_status)
                           VALUES (?, ?, ?, ?, ?, ?, 'manifest', 'manifest', 'not-cached', ?, ?,
                                   'fab-owned-manifest', 'fab', ?, 'source', 'unavailable-not-local')""",
                        (
                            product_id,
                            artifact_id,
                            path,
                            Path(path).name,
                            Path(path).suffix.lower(),
                            item.get("size_bytes"),
                            str(item.get("type_hint") or type_hint),
                            str(item.get("type_inference") or inference),
                            str(product.get("compatibility_status") or
                                ("reported-compatible" if product.get("compatible") else "reported-incompatible")),
                        ),
                    )

            connection.execute(
                """INSERT INTO corpora VALUES
                   ('fab-owned', 'fab-owned', 'Owned Fab products', ?, ?, 1,
                    'fab', 'source', ?, '[\"Windows\"]')""",
                (str(snapshot_path.parent), str(snapshot_path), engine_version),
            )
            connection.execute(
                """INSERT INTO corpora VALUES
                   ('fab-owned-manifest', 'fab-owned-manifest', 'Owned Fab BuildPatch manifests', ?, ?, 1,
                    'fab', 'source', ?, '[\"Windows\"]')""",
                (
                    str(manifest_results_path.parent) if manifest_results_path is not None else "",
                    str(manifest_results_path or ""),
                    engine_version,
                ),
            )
            connection.execute(
                """INSERT INTO corpora VALUES
                   ('fab-vault-cache', 'fab-vault-cache', 'Epic Games Launcher Vault Cache', ?, '', 1,
                    'fab', 'source', ?, '[\"Windows\"]')""",
                (str(vault_cache), engine_version),
            )

            for artifact_id, product in selected.items():
                product_root = vault_cache / artifact_id
                if not product_root.is_dir():
                    continue
                product_id = str(product["id"])
                connection.execute(
                    "UPDATE products SET installed_state = 'expanded-cache' WHERE artifact_id = ?",
                    (artifact_id,),
                )
                connection.execute(
                    "UPDATE files SET installed_state = 'expanded-cache' WHERE artifact_id = ?",
                    (artifact_id,),
                )
                data_root = product_root / "data"
                relative_root = data_root if data_root.is_dir() else product_root
                for file_path in sorted(relative_root.rglob("*")):
                    if not file_path.is_file() or file_path.suffix.lower() not in LOCAL_EXTENSIONS:
                        continue
                    path = file_path.relative_to(relative_root).as_posix()
                    type_hint, inference = _type_hint(path)
                    connection.execute(
                        """INSERT INTO files
                           (product_id, artifact_id, path, file_name, extension, size_bytes, source_tier,
                            metadata_fidelity, installed_state, type_hint, type_inference, corpus, source,
                            compatibility_status, adoption_state, asset_registry_status)
                           VALUES (?, ?, ?, ?, ?, ?, 'vault-cache', 'local-file', 'expanded-cache', ?, ?,
                                   'fab-vault-cache', 'fab', ?, 'source', 'unavailable-unmounted-vault')""",
                        (
                            product_id,
                            artifact_id,
                            path,
                            file_path.name,
                            file_path.suffix.lower(),
                            file_path.stat().st_size,
                            type_hint,
                            inference,
                            str(product.get("compatibility_status") or
                                ("reported-compatible" if product.get("compatible") else "reported-incompatible")),
                        ),
                    )

            for corpus in corpora:
                connection.execute(
                    "INSERT INTO corpora VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        corpus["corpus_id"], corpus["corpus_type"], corpus["name"], corpus["root_path"],
                        corpus["descriptor_path"], int(corpus["include_by_default"]), corpus["source"],
                        corpus["adoption_state"], corpus["engine_association"], corpus["supported_platforms_json"],
                    ),
                )

            known_registry_packages: set[str] = set()
            for package in local_packages:
                registry = (
                    registry_results.get(package["package_name"])
                    if package["corpus_type"] in {"wot-project", "wot-tooling-plugin"}
                    else None
                )
                if registry is not None:
                    known_registry_packages.add(package["package_name"])
                    package["asset_registry_status"] = (
                        "enriched" if registry.get("success") else
                        "failed:" + str(registry.get("error_code") or "unknown")
                    )
                cursor = connection.execute(
                    """INSERT INTO local_packages
                       (corpus_id, filesystem_path, relative_path, package_name, file_name, extension, size_bytes,
                        source, metadata_fidelity, compatibility_status, adoption_state, asset_registry_status)
                       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                    (
                        package["corpus_id"], package["filesystem_path"], package["relative_path"],
                        package["package_name"], package["file_name"], package["extension"], package["size_bytes"],
                        package["source"], package["metadata_fidelity"], package["compatibility_status"],
                        package["adoption_state"], package["asset_registry_status"],
                    ),
                )
                local_package_id = cursor.lastrowid
                if registry is None or not registry.get("success"):
                    continue
                dependencies = json.dumps(registry.get("dependencies") or [], ensure_ascii=False, sort_keys=True)
                referencers = json.dumps(registry.get("referencers") or [], ensure_ascii=False, sort_keys=True)
                for asset in registry.get("assets") or []:
                    tags = json.dumps(asset.get("tags") or {}, ensure_ascii=False, sort_keys=True)
                    _assert_safe_public_reference(tags, f"Asset Registry tags for {package['package_name']}")
                    connection.execute(
                        """INSERT INTO asset_registry_assets
                           (local_package_id, package_name, package_path, object_path, asset_name, asset_class,
                            tags_json, dependencies_json, referencers_json, metadata_fidelity)
                           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'asset-registry')""",
                        (
                            local_package_id, str(asset.get("package_name") or ""),
                            str(asset.get("package_path") or ""), str(asset.get("object_path") or ""),
                            str(asset.get("asset_name") or ""), str(asset.get("asset_class") or ""),
                            tags, dependencies, referencers,
                        ),
                    )
            extra_registry = sorted(set(registry_results) - known_registry_packages)
            if extra_registry:
                raise ValueError(f"Asset Registry results contain unknown local packages: {', '.join(extra_registry)}")

            for artifact_id, package_results in fab_registry_results.items():
                product = selected[artifact_id]
                product_id = str(product["id"])
                cached_files = connection.execute(
                    "SELECT * FROM files WHERE artifact_id = ? AND source_tier = 'vault-cache' ORDER BY path",
                    (artifact_id,),
                ).fetchall()
                if not cached_files:
                    raise ValueError(f"Fab Asset Registry results have no expanded-cache files for artifact {artifact_id}")
                files_by_package: dict[str, sqlite3.Row] = {}
                for file in cached_files:
                    package_name = _fab_package_name_from_content_path(file["path"])
                    if package_name in files_by_package:
                        raise ValueError(f"duplicate expanded-cache package path for artifact {artifact_id}: {package_name}")
                    files_by_package[package_name] = file
                if set(package_results) != set(files_by_package):
                    missing = sorted(set(files_by_package) - set(package_results))
                    extra = sorted(set(package_results) - set(files_by_package))
                    raise ValueError(
                        f"Fab Asset Registry package set mismatch for artifact {artifact_id}: "
                        f"missing={len(missing)}, extra={len(extra)}"
                    )
                for package_name, result in package_results.items():
                    if str(result.get("product_id") or "") != product_id:
                        raise ValueError(f"Fab Asset Registry product identity mismatch for artifact {artifact_id}")
                    if result.get("product_title") and str(result["product_title"]) != str(product.get("title") or ""):
                        raise ValueError(f"Fab Asset Registry product title mismatch for artifact {artifact_id}")
                    file = files_by_package[package_name]
                    success = bool(result["success"])
                    error_code = re.sub(r"[^A-Za-z0-9_.-]", "_", str(result.get("error_code") or "unknown"))[:80]
                    status = "enriched" if success else "failed:" + (error_code or "unknown")
                    connection.execute("UPDATE files SET asset_registry_status = ? WHERE id = ?", (status, file["id"]))
                    if not success:
                        continue
                    for asset in result.get("assets") or []:
                        if not isinstance(asset, dict):
                            raise ValueError(f"invalid Fab Asset Registry asset in {package_name}")
                        asset_package = str(asset.get("package_name") or "")
                        asset_path = str(asset.get("package_path") or "")
                        object_path = str(asset.get("object_path") or "")
                        asset_name = str(asset.get("asset_name") or "")
                        asset_class = str(asset.get("asset_class") or "")
                        expected_package_path = package_name.rpartition("/")[0]
                        if (
                            asset_package != package_name
                            or asset_path != expected_package_path
                            or object_path != f"{package_name}.{asset_name}"
                            or not asset_name
                            or not asset_class
                        ):
                            raise ValueError(f"invalid Fab Asset Registry asset identity in {package_name}")
                        tags_value = asset.get("tags")
                        if tags_value is None:
                            tags_value = {}
                        if not isinstance(tags_value, dict):
                            raise ValueError(f"invalid Fab Asset Registry tags in {package_name}")
                        _assert_safe_manifest_result(tags_value, f"Fab Asset Registry tags for {package_name}")
                        tags = json.dumps(tags_value, ensure_ascii=False, sort_keys=True)
                        connection.execute(
                            """INSERT INTO fab_asset_registry_assets
                               (file_id, package_name, package_path, object_path, asset_name, asset_class,
                                tags_json, metadata_fidelity)
                               VALUES (?, ?, ?, ?, ?, ?, ?, 'asset-registry')""",
                            (file["id"], asset_package, asset_path, object_path, asset_name, asset_class, tags),
                        )

            for product in connection.execute("SELECT * FROM products"):
                common = {
                    "product_id": product["product_id"], "artifact_id": product["artifact_id"],
                    "product_title": product["title"], "distribution_method": product["distribution_method"],
                    "engine_compatible": product["engine_compatible"],
                    "description": product["description"],
                    "compatibility_status": product["compatibility_status"],
                    "evidence_reference": product["evidence_reference"], "publisher_name": product["publisher_name"],
                    "source_product_id": product["source_product_id"], "listing_url": product["listing_url"],
                    "preview_references": product["preview_references_json"],
                    "reported_engine_versions": product["reported_engine_versions_json"],
                    "reported_platforms": product["reported_platforms_json"],
                    "asset_namespace": product["asset_namespace"], "thumbnail_url": product["thumbnail_url"],
                }
                _insert_search_row(
                    connection, **common, source_tier="listing", metadata_fidelity="listing",
                    type_hint="product", record_kind="fab-product", corpus_id="fab-owned", corpus_type="fab-owned",
                    source=product["source"], adoption_state=product["adoption_state"],
                    installed_state=product["installed_state"],
                )
                for file in connection.execute("SELECT * FROM files WHERE artifact_id = ?", (product["artifact_id"],)):
                    _insert_search_row(
                        connection, **common, path=file["path"], file_name=file["file_name"],
                        source_tier=file["source_tier"], metadata_fidelity=file["metadata_fidelity"],
                        installed_state=file["installed_state"], type_hint=file["type_hint"],
                        record_kind="fab-file", corpus_id=file["corpus"], corpus_type=file["corpus"],
                        source=file["source"], adoption_state=file["adoption_state"],
                        asset_registry_status=file["asset_registry_status"],
                    )

            fab_asset_rows = connection.execute(
                """SELECT p.*, f.path AS file_path, f.installed_state AS file_installed_state,
                          a.package_name, a.package_path, a.object_path, a.asset_name, a.asset_class,
                          a.metadata_fidelity AS asset_metadata_fidelity
                   FROM fab_asset_registry_assets a
                   JOIN files f ON f.id = a.file_id
                   JOIN products p ON p.artifact_id = f.artifact_id
                   ORDER BY p.title, f.path, a.object_path"""
            ).fetchall()
            for asset in fab_asset_rows:
                _insert_search_row(
                    connection, product_id=asset["product_id"], artifact_id=asset["artifact_id"],
                    product_title=asset["title"], search_title="", path=asset["file_path"], file_name=asset["asset_name"],
                    source_tier="asset-registry", metadata_fidelity=asset["asset_metadata_fidelity"],
                    installed_state=asset["file_installed_state"], distribution_method=asset["distribution_method"],
                    engine_compatible=asset["engine_compatible"], type_hint=asset["asset_class"],
                    record_kind="fab-asset-registry-asset", description="",
                    corpus_id="fab-vault-cache", corpus_type="fab-vault-cache", source="fab",
                    compatibility_status=asset["compatibility_status"], adoption_state=asset["adoption_state"],
                    asset_registry_status="enriched", object_path=asset["object_path"],
                    asset_class=asset["asset_class"], evidence_reference=asset["file_path"],
                    source_product_id=asset["source_product_id"],
                )

            local_rows = connection.execute(
                """SELECT lp.*, c.name AS corpus_name, c.corpus_type FROM local_packages lp
                   JOIN corpora c ON c.corpus_id = lp.corpus_id"""
            ).fetchall()
            for package in local_rows:
                _insert_search_row(
                    connection, product_title=package["corpus_name"], path=package["relative_path"],
                    file_name=package["file_name"], source_tier="local-filesystem", metadata_fidelity="filesystem",
                    installed_state="local", distribution_method="LOCAL", type_hint=_type_hint(package["relative_path"])[0],
                    record_kind="local-package", corpus_id=package["corpus_id"], corpus_type=package["corpus_type"],
                    source=package["source"], compatibility_status=package["compatibility_status"],
                    adoption_state=package["adoption_state"], asset_registry_status=package["asset_registry_status"],
                    object_path=package["package_name"], evidence_reference=package["filesystem_path"],
                )
                registry_assets = connection.execute(
                    "SELECT * FROM asset_registry_assets WHERE local_package_id = ?", (package["id"],)
                ).fetchall()
                for asset in registry_assets:
                    _insert_search_row(
                        connection, product_title=package["corpus_name"], path=asset["package_name"],
                        file_name=asset["asset_name"], source_tier="asset-registry",
                        metadata_fidelity="asset-registry", installed_state="local", distribution_method="LOCAL",
                        type_hint=asset["asset_class"], record_kind="asset-registry-asset",
                        corpus_id=package["corpus_id"], corpus_type=package["corpus_type"], source=package["source"],
                        compatibility_status=package["compatibility_status"], adoption_state=package["adoption_state"],
                        asset_registry_status="enriched", object_path=asset["object_path"],
                        asset_class=asset["asset_class"], evidence_reference=package["filesystem_path"],
                    )

            fab_registry_package_count = sum(len(packages) for packages in fab_registry_results.values())
            fab_registry_enriched_count = sum(
                bool(result["success"])
                for packages in fab_registry_results.values()
                for result in packages.values()
            )
            local_registry_asset_count = connection.execute("SELECT COUNT(*) FROM asset_registry_assets").fetchone()[0]
            fab_registry_asset_count = connection.execute("SELECT COUNT(*) FROM fab_asset_registry_assets").fetchone()[0]
            summary = {
                "products": connection.execute("SELECT COUNT(*) FROM products").fetchone()[0],
                "manifest_attempts": connection.execute("SELECT COUNT(*) FROM manifest_attempts").fetchone()[0],
                "manifest_succeeded": connection.execute("SELECT COUNT(*) FROM manifest_attempts WHERE status = 'succeeded'").fetchone()[0],
                "manifest_failed": connection.execute("SELECT COUNT(*) FROM manifest_attempts WHERE status = 'failed'").fetchone()[0],
                "manifest_not_attempted": connection.execute("SELECT COUNT(*) FROM manifest_attempts WHERE status = 'not_attempted'").fetchone()[0],
                "vault_products": connection.execute("SELECT COUNT(*) FROM products WHERE installed_state = 'expanded-cache'").fetchone()[0],
                "local_uasset": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'vault-cache' AND extension = '.uasset'").fetchone()[0],
                "local_umap": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'vault-cache' AND extension = '.umap'").fetchone()[0],
                "manifest_files": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'manifest'").fetchone()[0],
                "corpora": connection.execute("SELECT COUNT(*) FROM corpora").fetchone()[0],
                "local_filesystem_packages": connection.execute("SELECT COUNT(*) FROM local_packages").fetchone()[0],
                "local_project_uasset": connection.execute("SELECT COUNT(*) FROM local_packages WHERE extension = '.uasset'").fetchone()[0],
                "local_project_umap": connection.execute("SELECT COUNT(*) FROM local_packages WHERE extension = '.umap'").fetchone()[0],
                "asset_registry_enriched_packages": connection.execute("SELECT COUNT(*) FROM local_packages WHERE asset_registry_status = 'enriched'").fetchone()[0],
                "local_asset_registry_assets": local_registry_asset_count,
                "fab_asset_registry_scanned_products": len(fab_registry_results),
                "fab_asset_registry_unscanned_products": connection.execute("SELECT COUNT(*) FROM products").fetchone()[0] - len(fab_registry_results),
                "fab_asset_registry_expected_packages": fab_registry_package_count,
                "fab_asset_registry_enriched_packages": fab_registry_enriched_count,
                "fab_asset_registry_failed_packages": fab_registry_package_count - fab_registry_enriched_count,
                "fab_asset_registry_assets": fab_registry_asset_count,
                "fab_asset_registry_unscanned_packages": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'vault-cache' AND asset_registry_status = 'unavailable-unmounted-vault'").fetchone()[0],
                "asset_registry_assets": local_registry_asset_count + fab_registry_asset_count,
                "local_asset_registry_unavailable_or_failed": connection.execute("SELECT COUNT(*) FROM local_packages WHERE asset_registry_status != 'enriched'").fetchone()[0],
                "vault_asset_registry_unavailable_unmounted": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'vault-cache' AND asset_registry_status = 'unavailable-unmounted-vault'").fetchone()[0],
                "asset_registry_unavailable_or_failed_total": connection.execute("SELECT COUNT(*) FROM local_packages WHERE asset_registry_status != 'enriched'").fetchone()[0]
                    + connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'vault-cache' AND asset_registry_status != 'enriched'").fetchone()[0],
                "tier_listing_rows": connection.execute("SELECT COUNT(*) FROM products").fetchone()[0],
                "tier_manifest_rows": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'manifest'").fetchone()[0],
                "tier_vault_filesystem_rows": connection.execute("SELECT COUNT(*) FROM files WHERE source_tier = 'vault-cache'").fetchone()[0],
                "tier_local_filesystem_rows": connection.execute("SELECT COUNT(*) FROM local_packages").fetchone()[0],
                "tier_asset_registry_rows": local_registry_asset_count + fab_registry_asset_count,
            }
            connection.execute("INSERT INTO metadata VALUES ('summary_json', ?)", (json.dumps(summary, sort_keys=True),))
            connection.commit()
        connection.close()
        os.replace(temporary, database_path)
        return summary
    except Exception:
        if connection is not None:
            connection.close()
        temporary.unlink(missing_ok=True)
        raise


def _fts_query(text: str) -> str:
    tokens = re.findall(r"[\w]+", text, flags=re.UNICODE)
    terms = []
    for token in tokens:
        lowered = token.casefold()
        singular = ""
        if lowered.endswith("ies") and len(lowered) > 4:
            singular = lowered[:-3] + "y"
        elif lowered.endswith(("ches", "shes", "sses", "xes", "zes")) and len(lowered) > 4:
            singular = lowered[:-2]
        elif lowered.endswith("s") and not lowered.endswith("ss") and len(lowered) > 3:
            singular = lowered[:-1]
        if singular and singular != lowered:
            terms.append(f'("{token}"* OR "{singular}"*)')
        else:
            terms.append(f'"{token}"*')
    return " AND ".join(terms)


def search_catalog(
    database_path: Path,
    query: str = "",
    *,
    product: str = "",
    path: str = "",
    installed: str = "",
    distribution: str = "",
    compatible: bool | None = None,
    fidelity: str = "",
    asset_class: str = "",
    source: str = "",
    corpus: str = "",
    adoption_state: str = "",
    registry_status: str = "",
    include_engine: bool = False,
    limit: int = 50,
) -> list[dict[str, Any]]:
    clauses: list[str] = []
    parameters: list[Any] = []
    match = _fts_query(query)
    if match:
        clauses.append("catalog_fts MATCH ?")
        parameters.append(match)
    if product:
        clauses.append("product_title LIKE ? ESCAPE '\\'")
        parameters.append(f"%{product}%")
    if path:
        clauses.append("path LIKE ? ESCAPE '\\'")
        parameters.append(f"%{path}%")
    if installed:
        clauses.append("installed_state = ?")
        parameters.append(installed)
    if distribution:
        clauses.append("distribution_method = ? COLLATE NOCASE")
        parameters.append(distribution)
    if compatible is not None:
        clauses.append("engine_compatible = ?")
        parameters.append(str(int(compatible)))
    if fidelity:
        clauses.append("metadata_fidelity = ?")
        parameters.append(fidelity)
    if asset_class:
        if asset_class.startswith("/"):
            clauses.append("asset_class = ? COLLATE NOCASE")
            parameters.append(asset_class)
        else:
            clauses.append("(asset_class = ? COLLATE NOCASE OR asset_class LIKE ? COLLATE NOCASE)")
            parameters.extend((asset_class, f"%.{asset_class}"))
    if source:
        clauses.append("source = ? COLLATE NOCASE")
        parameters.append(source)
    if corpus:
        clauses.append("corpus_type = ? COLLATE NOCASE")
        parameters.append(corpus)
    if adoption_state:
        clauses.append("adoption_state = ? COLLATE NOCASE")
        parameters.append(adoption_state)
    if registry_status:
        clauses.append("asset_registry_status = ? COLLATE NOCASE")
        parameters.append(registry_status)
    if not include_engine:
        clauses.append("corpus_type NOT LIKE 'engine-%'")
    where = " WHERE " + " AND ".join(clauses) if clauses else ""
    parameters.append(max(1, min(limit, 1000)))
    sql = f"""SELECT product_id, artifact_id, product_title, path, file_name, source_tier,
                     metadata_fidelity, installed_state, distribution_method, engine_compatible, type_hint,
                     record_kind, corpus_id, corpus_type, source, compatibility_status, adoption_state,
                     asset_registry_status, object_path, asset_class, evidence_reference, publisher_name,
                     source_product_id, listing_url, preview_references, reported_engine_versions,
                     reported_platforms, asset_namespace, thumbnail_url
              FROM catalog_fts{where} ORDER BY product_title, path LIMIT ?"""
    connection = sqlite3.connect(f"file:{database_path.as_posix()}?mode=ro", uri=True)
    try:
        connection.row_factory = sqlite3.Row
        rows = [dict(row) for row in connection.execute(sql, parameters)]
        for row in rows:
            row["engine_compatible"] = (
                bool(int(row["engine_compatible"])) if row["engine_compatible"] else None
            )
        return rows
    finally:
        connection.close()


def verify_catalog(database_path: Path) -> dict[str, Any]:
    prohibited_columns: list[str] = []
    connection = sqlite3.connect(f"file:{database_path.as_posix()}?mode=ro", uri=True)
    try:
        tables = [row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type IN ('table', 'view')")]
        for table in tables:
            if not re.fullmatch(r"[A-Za-z0-9_]+", table):
                continue
            for column in connection.execute(f"PRAGMA table_info({table})"):
                if str(column[1]).casefold() in PROHIBITED_KEYS:
                    prohibited_columns.append(f"{table}.{column[1]}")
        summary_row = connection.execute("SELECT value FROM metadata WHERE key = 'summary_json'").fetchone()
    finally:
        connection.close()
    database_bytes = database_path.read_bytes().lower()
    markers = [marker for marker in (b"bearer ", b"x-amz-credential", b"signature=", b"epic_account_id", b"manifest_url") if marker in database_bytes]
    return {
        "safe": not prohibited_columns and not markers,
        "prohibited_columns": prohibited_columns,
        "prohibited_markers": [marker.decode("ascii") for marker in markers],
        "summary": json.loads(summary_row[0]) if summary_row else {},
    }


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)

    sync = subcommands.add_parser("sync", help="atomically rebuild the SQLite/FTS catalog")
    sync.add_argument("--snapshot", required=True, type=Path)
    sync.add_argument("--vault-cache", required=True, type=Path)
    sync.add_argument("--manifest-results", type=Path)
    sync.add_argument("--database", required=True, type=Path)
    sync.add_argument("--engine-version", default="5.8")
    sync.add_argument("--scan-root", action="append", default=[], type=Path)
    sync.add_argument("--current-project", type=Path)
    sync.add_argument("--registry-results", type=Path)
    sync.add_argument("--fab-registry-results", type=Path)
    sync.add_argument("--include-engine", action="store_true")
    sync.add_argument("--engine-root", type=Path)

    search = subcommands.add_parser("search", help="search product and file metadata")
    search.add_argument("query", nargs="?", default="")
    search.add_argument("--database", required=True, type=Path)
    search.add_argument("--product", default="")
    search.add_argument("--path", default="")
    search.add_argument("--installed", choices=("expanded-cache", "not-cached", "local"), default="")
    search.add_argument("--distribution", default="")
    search.add_argument("--compatible", choices=("true", "false"))
    search.add_argument("--fidelity", choices=("listing", "manifest", "local-file", "filesystem", "asset-registry"), default="")
    search.add_argument("--asset-class", default="")
    search.add_argument("--source", default="")
    search.add_argument("--corpus", default="")
    search.add_argument("--adoption-state", default="")
    search.add_argument("--registry-status", default="")
    search.add_argument("--include-engine", action="store_true")
    search.add_argument("--limit", type=int, default=50)

    verify = subcommands.add_parser("verify", help="inspect reconciliation and prohibited persistence")
    verify.add_argument("--database", required=True, type=Path)
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.command == "sync":
        result = sync_catalog(
            args.snapshot,
            args.vault_cache,
            args.manifest_results,
            args.database,
            args.engine_version,
            scan_roots=args.scan_root,
            current_project=args.current_project,
            registry_results_path=args.registry_results,
            fab_registry_results_path=args.fab_registry_results,
            include_engine=args.include_engine,
            engine_root=args.engine_root,
        )
    elif args.command == "search":
        compatible = None if args.compatible is None else args.compatible == "true"
        result = search_catalog(
            args.database,
            args.query,
            product=args.product,
            path=args.path,
            installed=args.installed,
            distribution=args.distribution,
            compatible=compatible,
            fidelity=args.fidelity,
            asset_class=args.asset_class,
            source=args.source,
            corpus=args.corpus,
            adoption_state=args.adoption_state,
            registry_status=args.registry_status,
            include_engine=args.include_engine,
            limit=args.limit,
        )
    else:
        result = verify_catalog(args.database)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if not isinstance(result, dict) or result.get("safe", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
