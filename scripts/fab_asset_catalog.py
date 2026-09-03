#!/usr/bin/env python
# Copyright Buckley Builds LLC 2026 All Rights Reserved.

"""Build and query a local, provenance-preserving Fab asset catalog."""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
from pathlib import Path
from typing import Any, Iterable

SCHEMA_VERSION = 1
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
    installed_state TEXT NOT NULL DEFAULT 'not-cached'
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
    UNIQUE (artifact_id, path, source_tier)
);
CREATE VIRTUAL TABLE catalog_fts USING fts5(
    product_id UNINDEXED,
    artifact_id UNINDEXED,
    product_title,
    path,
    file_name,
    source_tier UNINDEXED,
    metadata_fidelity UNINDEXED,
    installed_state UNINDEXED,
    distribution_method UNINDEXED,
    engine_compatible UNINDEXED,
    type_hint UNINDEXED
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


def _insert_search_row(connection: sqlite3.Connection, product: sqlite3.Row | tuple[Any, ...], file_row: tuple[Any, ...] | None = None) -> None:
    product_id, artifact_id, title, distribution, compatible, installed_state = product
    if file_row is None:
        path = file_name = ""
        source_tier = metadata_fidelity = "listing"
        type_hint = "product"
    else:
        path, file_name, source_tier, metadata_fidelity, installed_state, type_hint = file_row
    connection.execute(
        "INSERT INTO catalog_fts VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            product_id,
            artifact_id,
            title,
            path,
            file_name,
            source_tier,
            metadata_fidelity,
            installed_state,
            distribution,
            str(int(bool(compatible))),
            type_hint,
        ),
    )


def sync_catalog(
    snapshot_path: Path,
    vault_cache: Path,
    manifest_results_path: Path | None,
    database_path: Path,
    engine_version: str,
) -> dict[str, int]:
    """Replace database_path atomically from read-only snapshot, manifest, and cache inputs."""
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

    database_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = database_path.with_name(database_path.name + ".tmp")
    temporary.unlink(missing_ok=True)
    connection: sqlite3.Connection | None = None
    try:
        with sqlite3.connect(temporary) as connection:
            connection.executescript(SCHEMA)
            connection.execute("INSERT INTO metadata VALUES ('schema_version', ?)", (str(SCHEMA_VERSION),))
            connection.execute("INSERT INTO metadata VALUES ('engine_version', ?)", (engine_version,))

            for artifact_id, product in selected.items():
                connection.execute(
                    "INSERT INTO products VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'not-cached')",
                    (
                        str(product["id"]),
                        artifact_id,
                        str(product.get("title") or ""),
                        str(product.get("distribution_method") or ""),
                        str(product.get("listing_type") or ""),
                        str(product.get("source") or ""),
                        engine_version,
                        int(bool(product.get("compatible"))),
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
                            metadata_fidelity, installed_state, type_hint, type_inference)
                           VALUES (?, ?, ?, ?, ?, ?, 'manifest', 'manifest', 'not-cached', ?, ?)""",
                        (
                            product_id,
                            artifact_id,
                            path,
                            Path(path).name,
                            Path(path).suffix.lower(),
                            item.get("size_bytes"),
                            str(item.get("type_hint") or type_hint),
                            str(item.get("type_inference") or inference),
                        ),
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
                            metadata_fidelity, installed_state, type_hint, type_inference)
                           VALUES (?, ?, ?, ?, ?, ?, 'vault-cache', 'local-file', 'expanded-cache', ?, ?)""",
                        (
                            product_id,
                            artifact_id,
                            path,
                            file_path.name,
                            file_path.suffix.lower(),
                            file_path.stat().st_size,
                            type_hint,
                            inference,
                        ),
                    )

            product_rows = connection.execute(
                "SELECT product_id, artifact_id, title, distribution_method, engine_compatible, installed_state FROM products"
            ).fetchall()
            for product_row in product_rows:
                _insert_search_row(connection, product_row)
                file_rows = connection.execute(
                    """SELECT path, file_name, source_tier, metadata_fidelity, installed_state, type_hint
                       FROM files WHERE artifact_id = ?""",
                    (product_row[1],),
                ).fetchall()
                for file_row in file_rows:
                    _insert_search_row(connection, product_row, file_row)

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
    return " AND ".join(f'"{token}"*' for token in tokens)


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
    where = " WHERE " + " AND ".join(clauses) if clauses else ""
    parameters.append(max(1, min(limit, 1000)))
    sql = f"""SELECT product_id, artifact_id, product_title, path, file_name, source_tier,
                     metadata_fidelity, installed_state, distribution_method, engine_compatible, type_hint
              FROM catalog_fts{where} ORDER BY product_title, path LIMIT ?"""
    connection = sqlite3.connect(f"file:{database_path.as_posix()}?mode=ro", uri=True)
    try:
        connection.row_factory = sqlite3.Row
        rows = [dict(row) for row in connection.execute(sql, parameters)]
        for row in rows:
            row["engine_compatible"] = bool(int(row["engine_compatible"]))
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

    search = subcommands.add_parser("search", help="search product and file metadata")
    search.add_argument("query", nargs="?", default="")
    search.add_argument("--database", required=True, type=Path)
    search.add_argument("--product", default="")
    search.add_argument("--path", default="")
    search.add_argument("--installed", choices=("expanded-cache", "not-cached"), default="")
    search.add_argument("--distribution", default="")
    search.add_argument("--compatible", choices=("true", "false"))
    search.add_argument("--fidelity", choices=("listing", "manifest", "local-file"), default="")
    search.add_argument("--limit", type=int, default=50)

    verify = subcommands.add_parser("verify", help="inspect reconciliation and prohibited persistence")
    verify.add_argument("--database", required=True, type=Path)
    return parser


def main(argv: Iterable[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.command == "sync":
        result = sync_catalog(args.snapshot, args.vault_cache, args.manifest_results, args.database, args.engine_version)
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
            limit=args.limit,
        )
    else:
        result = verify_catalog(args.database)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if not isinstance(result, dict) or result.get("safe", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
