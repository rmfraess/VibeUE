# Fab asset catalog (Phase 1)

This read-only workflow indexes owned Fab product metadata, BuildPatch manifest file lists, and already-expanded Vault Cache files. Discovery does not approve adoption, import content, start an installer, or download payload chunks.

## Refresh

1. Build and relaunch the project with `BuildAndLaunchGame.ps1`.
2. Through the configured Unreal MCP Adapter, call `VibeUE.FabService.InspectOwnedManifest` for a single owned product, or use `direct.execute_python_code` to run bounded batches from `scripts/export_fab_manifests.py`:

   ```python
   import sys
   sys.path.insert(0, r"D:\Dev\Personal\Wheel of Time\WOT\Plugins\VibeUE\scripts")
   import export_fab_manifests
   export_fab_manifests.export_batch(
       r"D:\Dev\Personal\Asset Catalog\Fab\fab-library-2026-08-28.json",
       r"D:\Dev\Personal\Asset Catalog\Unified Unreal Catalog\manifest-results.jsonl",
       start=0,
       count=10,
       engine_version="5.8",
   )
   ```

   Continue at `start=10,20,...` and finish with `validate_export(...)`. Batch zero intentionally replaces the prior JSONL export; later batches append and reject duplicate artifacts.
3. Rebuild the SQLite/FTS database from the read-only inputs:

   ```text
   python scripts/fab_asset_catalog.py sync --snapshot "D:/Dev/Personal/Asset Catalog/Fab/fab-library-2026-08-28.json" --vault-cache "C:/ProgramData/Epic/EpicGamesLauncher/VaultCache" --manifest-results "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/manifest-results.jsonl" --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3" --engine-version 5.8
   ```

The synchronizer writes a temporary database and atomically replaces the destination only after validation. It excludes `ENGINE` rows from creative search, requires distinct content product/artifact IDs, records one manifest status per artifact, and maps expanded cache folders by exact artifact ID.

## Search and verify

```text
python scripts/fab_asset_catalog.py search oak --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search rock --fidelity local-file --installed expanded-cache --distribution ASSET_PACK --compatible true --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search tree --product "English Oak" --path Content --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py verify --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
```

Search output always identifies product, artifact, source tier, metadata fidelity, installed state, distribution method, and reported engine compatibility.

## Provenance and fidelity

- `listing`: owned-library product metadata; no pack file evidence.
- `manifest`: filename, relative package/file path, and byte size parsed from the authenticated BuildPatch manifest only.
- `local-file`: `.uasset`/`.umap` found under an already-expanded Vault Cache artifact directory.
- `type_hint` / `type_inference=heuristic`: filename-extension inference only. It is not Asset Registry class data and does not prove that an asset loads or renders.

Fab compatibility is reported metadata, not runtime validation. An incompatible product can still have its first owned artifact indexed and remains marked `engine_compatible=false`.

## Safety and unsupported-contract risk

The Fab listing and artifact endpoints are unofficial and may change. Inspection fails closed on malformed responses/manifests. The new operation performs one artifact-metadata request and one BuildPatch-manifest request, does not create an installer, and reports `payload_chunk_requests=0`.

Signed manifest/CDN locations and EOS/account data remain transient in memory. Export and database schemas reject URLs and auth/identity fields; endpoint response bodies are not included in errors. Do not add token, cookie, account, entitlement, signed URL, CDN query, or credential fields to generated output. Vault Cache and vendor payloads are read-only.
