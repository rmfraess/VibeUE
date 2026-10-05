# Fab asset catalog (Phase 1)

This read-only workflow indexes owned Fab product provenance, BuildPatch manifest file lists, already-expanded Vault Cache files, local Unreal project/plugin packages, and on-disk Asset Registry evidence. Fab cache scans use disposable UE projects with a read-only Content link; they do not modify WOT or import content into the production project. Discovery does not approve adoption, start an installer, or download payload chunks or previews.

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
3. Export the current project's already-mounted, on-disk Asset Registry evidence through `direct.execute_python_code` and the Unreal MCP Adapter:

   ```python
   import sys
   sys.path.insert(0, r"D:\Dev\Personal\Wheel of Time\WOT\Plugins\VibeUE\scripts")
   import export_asset_registry
   export_asset_registry.export_current_project(
       r"D:\Dev\Personal\Wheel of Time\WOT",
       r"D:\Dev\Personal\Asset Catalog\Unified Unreal Catalog\asset-registry-results.jsonl",
   )
   ```

   This calls `InspectAssetRegistryPackage` without loading packages and records package/object paths, classes, tags, and on-disk dependency/referencer evidence. Packages outside the running project are not mounted; they receive a classified unavailable status during synchronization.
4. Rebuild the SQLite/FTS database from the read-only inputs:

   ```text
   python scripts/fab_asset_catalog.py sync --snapshot "D:/Dev/Personal/Asset Catalog/Fab/fab-library-2026-09-24.json" --vault-cache "C:/ProgramData/Epic/EpicGamesLauncher/VaultCache" --manifest-results "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/manifest-results.jsonl" --registry-results "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/asset-registry-results.jsonl" --fab-registry-results "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/fab-asset-registry-results-2026-09-24.jsonl" --scan-root "D:/Dev/Personal" --current-project "D:/Dev/Personal/Wheel of Time/WOT/WOT.uproject" --engine-root "C:/Program Files/Epic Games/UE_5.8" --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3" --engine-version 5.8
   ```

The synchronizer writes a temporary database and atomically replaces the destination only after validation. It excludes Fab `ENGINE` rows, requires distinct content product/artifact IDs, records one manifest status per artifact, maps expanded cache folders by exact artifact ID, and discovers `.uproject`/`.uplugin` corpora without opening them. `--fab-registry-results` reconciles every reported package against the exact expanded-cache artifact and preserves per-package failures. Installed Engine content/plugins are indexed only when sync is explicitly passed `--include-engine`; normal search hides every `engine-*` corpus unless search is explicitly passed `--include-engine`.

## Search and verify

```text
python scripts/fab_asset_catalog.py search oak --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search rock --fidelity local-file --installed expanded-cache --distribution ASSET_PACK --compatible true --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search tree --product "English Oak" --path Content --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search trees --fidelity asset-registry --source fab --asset-class StaticMesh --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search walls --fidelity asset-registry --source fab --asset-class StaticMesh --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search Blueprint --corpus tooling-test-project --fidelity filesystem --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py search QuarryRoad --fidelity asset-registry --registry-status enriched --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
python scripts/fab_asset_catalog.py verify --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"
```

Search output identifies the record kind, source/corpus, product/artifact identity, publisher, evidence/listing references, metadata fidelity, installed state, distribution method, compatibility status, adoption state, and Asset Registry status. `--asset-class` accepts an exact class path or short class name; plural role queries such as `trees` and `walls` also match singular names. Asset Registry searches match individual asset name/path/class, not the duplicated source-pack title; use `--product` to scope an asset query to a source pack. Other filters include `--source`, `--corpus`, `--adoption-state`, and `--registry-status`.

## Provenance and fidelity

- `listing`: owned-library product metadata. Description, publisher/source identity, listing and preview references, project/artifact versions, and reported engine/platform compatibility are preserved; no pack file evidence is implied.
- `manifest`: filename, relative package/file path, and byte size parsed from the authenticated BuildPatch manifest only.
- `local-file`: `.uasset`/`.umap` found under an already-expanded Vault Cache artifact directory.
- `filesystem`: `.uasset`/`.umap` found beneath a discovered local `.uproject` or `.uplugin` Content directory.
- `asset-registry`: authoritative on-disk registry package/object path and class. WOT project results come from the running WOT Editor; Fab cache results come from isolated UE 5.8.3 scans. The Fab pilot did not export searchable tags, dependencies, referencers, or previews.
- `type_hint` / `type_inference=heuristic`: filename-extension inference only. It is not Asset Registry class data and does not prove that an asset loads or renders.

Fab compatibility is reported metadata, not runtime validation. An incompatible product can still have its first owned artifact indexed and remains marked `engine_compatible=false`. Discovered vendor and external corpora begin with `adoption_state=source`; WOT's existing package is `existing-project`. Neither state grants new adoption approval.

Asset Registry status is explicit per package. `enriched` means on-disk registry evidence was returned without loading the package. `unavailable-not-current-project` means the package belongs to another discovered project/test corpus; `unavailable-unmounted-vault` means a Vault Cache package was not scanned; `failed:<code>` preserves an attempted failure.

## Current Fab Asset Registry coverage

The 2026-09-24 snapshot has 140 owned entries: nine `ENGINE`/`Legacy` Unreal Engine listings are excluded, leaving 131 content listings (121 asset packs, five code plugins, five complete projects). Seven already-expanded Fab products were scanned in disposable UE 5.8.3 projects (CL 58210709). The active schema-3 catalog contains 2,589 individual Asset Registry records from 2,589 of 2,590 cached package files; one `Science Fiction Hideout KIT` package under `__ExternalActors__` returned `no-asset-registry-record`. No assets were loaded, and the cache source inventories remained unchanged. The other 124 content listings remain unscanned: 94 have manifests whose per-file sizes sum to 99,748,736,410 bytes (~92.9 GiB expanded-file-size sum, not a download-size estimate), while 30 manifest attempts were not made and have no recorded reason. No additional products were downloaded or staged. Active-catalog searches with `--fidelity asset-registry --source fab --asset-class StaticMesh` returned 34 tree-related name/path matches across five products (including branch/twig components) and 27 wall matches across three. These are not visual classifications; packages with opaque names can be missed. No individual-asset previews were generated.

Example: `python scripts/fab_asset_catalog.py search trees --fidelity asset-registry --source fab --asset-class StaticMesh --database "D:/Dev/Personal/Asset Catalog/Unified Unreal Catalog/catalog.sqlite3"`. Fab asset rows keep product title and provider identity as result provenance, not searchable text; a title-only token such as `Runestone` returns no Asset Registry asset match.

## Safety and unsupported-contract risk

The Fab listing and artifact endpoints are unofficial and may change. Inspection fails closed on malformed responses/manifests. The new operation performs one artifact-metadata request and one BuildPatch-manifest request, does not create an installer, and reports `payload_chunk_requests=0`.

Signed manifest/CDN locations and EOS/account data remain transient in memory. Manifest export rejects every URL and auth/identity field; the catalog permits only public snapshot listing/preview references and rejects credential/signature markers. Endpoint response bodies are not included in errors. Do not add token, cookie, account, entitlement, signed URL, CDN query, or credential fields to generated output. Vault Cache, other projects, and vendor payloads are read-only.
