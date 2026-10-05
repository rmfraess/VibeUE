# Copyright Buckley Builds LLC 2026 All Rights Reserved.

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

import fab_asset_catalog as catalog


class FabAssetCatalogTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.snapshot = self.root / "snapshot.json"
        self.vault = self.root / "VaultCache"
        self.manifests = self.root / "manifests.jsonl"
        self.fab_registry = self.root / "fab-registry.jsonl"
        self.database = self.root / "catalog.sqlite3"
        products = [
            {
                "id": "product-oak",
                "title": "English Oak",
                "description": "A mature deciduous tree source pack.",
                "distribution_method": "ASSET_PACK",
                "listing_type": "3D",
                "source": "fab",
                "seller": "Tree Publisher",
                "url": "https://www.fab.com/listings/product-oak",
                "images": [{"url": "https://media.fab.com/oak.jpg", "type": "Featured"}],
                "compatible": True,
                "project_versions": [{"artifact_id": "OakArtifactV1", "engine_versions": ["UE_5.8"], "target_platforms": ["Windows"]}],
            },
            {
                "id": "product-engine",
                "title": "Unreal Engine",
                "distribution_method": "ENGINE",
                "listing_type": "Legacy",
                "source": "fab",
                "compatible": True,
                "project_versions": [{"artifact_id": "EngineArtifactV1", "engine_versions": ["UE_5.8"]}],
            },
        ]
        self.snapshot.write_text(json.dumps({"products": products}), encoding="utf-8")
        local = self.vault / "OakArtifactV1" / "data" / "Content" / "Trees"
        local.mkdir(parents=True)
        (local / "SM_English_Oak.uasset").write_bytes(b"asset")
        (local / "Oak_Grove.umap").write_bytes(b"map")
        self.manifests.write_text(
            json.dumps(
                {
                    "success": True,
                    "product_id": "product-oak",
                    "artifact_id": "OakArtifactV1",
                    "metadata_fidelity": "manifest",
                    "files": [
                        {"path": "Content/Trees/SM_English_Oak.uasset", "size_bytes": 123, "type_hint": "unreal-package", "type_inference": "heuristic"},
                        {"path": "Content/Rocks/SM_Rock.uasset", "size_bytes": 456, "type_hint": "unreal-package", "type_inference": "heuristic"},
                    ],
                }
            )
            + "\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temp.cleanup()

    def sync(self):
        return catalog.sync_catalog(self.snapshot, self.vault, self.manifests, self.database, "5.8")

    def fab_registry_row(self, path, asset_name, asset_class, *, success=True, error_code="", tags=None):
        package_name = "/Game/" + Path(path).relative_to("Content").with_suffix("").as_posix()
        return {
            "artifact_id": "OakArtifactV1",
            "product_id": "product-oak",
            "product_title": "English Oak",
            "expected_package_name": package_name,
            "package_name": package_name if success else "",
            "path": path,
            "success": success,
            "error_code": error_code,
            "assets": [
                {
                    "package_name": package_name,
                    "package_path": package_name.rpartition("/")[0],
                    "object_path": f"{package_name}.{asset_name}",
                    "asset_name": asset_name,
                    "asset_class": asset_class,
                    "tags": tags or {},
                }
            ] if success else [],
        }

    def test_sync_excludes_engine_rows_and_maps_expanded_cache(self):
        summary = self.sync()
        self.assertEqual(summary["products"], 1)
        self.assertEqual(summary["vault_products"], 1)
        self.assertEqual(summary["local_uasset"], 1)
        self.assertEqual(summary["local_umap"], 1)
        connection = sqlite3.connect(self.database)
        try:
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM products").fetchone()[0], 1)
            self.assertEqual(connection.execute("SELECT title FROM products").fetchone()[0], "English Oak")
            provenance = connection.execute(
                "SELECT description, publisher_name, listing_url, reported_platforms_json, adoption_state FROM products"
            ).fetchone()
            self.assertEqual(provenance[0], "A mature deciduous tree source pack.")
            self.assertEqual(provenance[1], "Tree Publisher")
            self.assertEqual(provenance[2], "https://www.fab.com/listings/product-oak")
            self.assertEqual(json.loads(provenance[3]), ["Windows"])
            self.assertEqual(provenance[4], "source")
        finally:
            connection.close()

    def test_manifest_attempts_are_explicit_and_search_filters_work(self):
        self.sync()
        rows = catalog.search_catalog(self.database, "rock", fidelity="manifest", limit=10)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["product_title"], "English Oak")
        self.assertEqual(rows[0]["metadata_fidelity"], "manifest")
        self.assertEqual(rows[0]["installed_state"], "expanded-cache")
        self.assertIs(rows[0]["engine_compatible"], True)
        product_rows = catalog.search_catalog(self.database, "Tree Publisher", fidelity="listing", limit=10)
        self.assertEqual(len(product_rows), 1)
        self.assertEqual(product_rows[0]["source_product_id"], "product-oak")
        self.assertEqual(product_rows[0]["listing_url"], "https://www.fab.com/listings/product-oak")
        self.assertEqual(
            len(
                catalog.search_catalog(
                    self.database,
                    installed="expanded-cache",
                    distribution="ASSET_PACK",
                    compatible=True,
                    limit=10,
                )
            ),
            5,
        )
        connection = sqlite3.connect(self.database)
        try:
            status = connection.execute(
                "SELECT status FROM manifest_attempts WHERE artifact_id = ?", ("OakArtifactV1",)
            ).fetchone()[0]
        finally:
            connection.close()
        self.assertEqual(status, "succeeded")

    def test_fab_registry_assets_search_by_plural_role_and_class(self):
        wall_path = self.vault / "OakArtifactV1" / "data" / "Content" / "Structures" / "SM_Wall.uasset"
        wall_path.parent.mkdir(parents=True)
        wall_path.write_bytes(b"wall")
        foliage_path = self.vault / "OakArtifactV1" / "data" / "Content" / "Trees" / "FT_Foliage.uasset"
        foliage_path.write_bytes(b"foliage")
        crate_path = self.vault / "OakArtifactV1" / "data" / "Content" / "Props" / "SM_Crate.uasset"
        crate_path.parent.mkdir(parents=True)
        crate_path.write_bytes(b"crate")
        records = [
            self.fab_registry_row("Content/Trees/SM_English_Oak.uasset", "SM_English_Oak", "/Script/Engine.StaticMesh"),
            self.fab_registry_row("Content/Trees/Oak_Grove.umap", "Oak_Grove", "/Script/Engine.World"),
            self.fab_registry_row("Content/Trees/FT_Foliage.uasset", "FT_Foliage", "/Script/Foliage.FoliageType_InstancedStaticMesh"),
            self.fab_registry_row("Content/Structures/SM_Wall.uasset", "SM_Wall", "/Script/Engine.StaticMesh"),
            self.fab_registry_row("Content/Props/SM_Crate.uasset", "SM_Crate", "/Script/Engine.StaticMesh"),
        ]
        self.fab_registry.write_text("\n".join(json.dumps(row) for row in records) + "\n", encoding="utf-8")
        summary = catalog.sync_catalog(
            self.snapshot, self.vault, self.manifests, self.database, "5.8",
            fab_registry_results_path=self.fab_registry,
        )
        self.assertEqual(summary["fab_asset_registry_scanned_products"], 1)
        self.assertEqual(summary["fab_asset_registry_unscanned_products"], 0)
        self.assertEqual(summary["fab_asset_registry_expected_packages"], 5)
        self.assertEqual(summary["fab_asset_registry_enriched_packages"], 5)
        self.assertEqual(summary["fab_asset_registry_assets"], 5)
        walls = catalog.search_catalog(
            self.database, "walls", fidelity="asset-registry", source="fab", asset_class="StaticMesh", limit=10,
        )
        self.assertEqual(len(walls), 1)
        self.assertEqual(walls[0]["record_kind"], "fab-asset-registry-asset")
        self.assertEqual(walls[0]["file_name"], "SM_Wall")
        self.assertEqual(walls[0]["source_product_id"], "product-oak")
        trees = catalog.search_catalog(
            self.database, "trees", fidelity="asset-registry", source="fab", asset_class="StaticMesh", limit=10,
        )
        self.assertEqual(len(trees), 1)
        self.assertEqual(trees[0]["file_name"], "SM_English_Oak")
        oaks = catalog.search_catalog(
            self.database, "oak", fidelity="asset-registry", source="fab", asset_class="StaticMesh", limit=10,
        )
        self.assertEqual(len(oaks), 1)
        self.assertEqual(oaks[0]["file_name"], "SM_English_Oak")
        self.assertEqual(oaks[0]["product_title"], "English Oak")

    def test_fab_registry_failure_is_preserved_and_counted(self):
        records = [
            self.fab_registry_row("Content/Trees/SM_English_Oak.uasset", "SM_English_Oak", "/Script/Engine.StaticMesh"),
            self.fab_registry_row(
                "Content/Trees/Oak_Grove.umap", "", "", success=False, error_code="no-asset-registry-record",
            ),
        ]
        self.fab_registry.write_text("\n".join(json.dumps(row) for row in records) + "\n", encoding="utf-8")
        summary = catalog.sync_catalog(
            self.snapshot, self.vault, self.manifests, self.database, "5.8",
            fab_registry_results_path=self.fab_registry,
        )
        self.assertEqual(summary["fab_asset_registry_expected_packages"], 2)
        self.assertEqual(summary["fab_asset_registry_enriched_packages"], 1)
        self.assertEqual(summary["fab_asset_registry_failed_packages"], 1)
        connection = sqlite3.connect(self.database)
        try:
            status = connection.execute(
                "SELECT asset_registry_status FROM files WHERE artifact_id = ? AND path = ? AND source_tier = 'vault-cache'",
                ("OakArtifactV1", "Content/Trees/Oak_Grove.umap"),
            ).fetchone()[0]
        finally:
            connection.close()
        self.assertEqual(status, "failed:no-asset-registry-record")

    def test_fab_registry_rejects_identity_mismatch_and_unsafe_tags(self):
        records = [
            self.fab_registry_row("Content/Trees/SM_English_Oak.uasset", "SM_English_Oak", "/Script/Engine.StaticMesh"),
            self.fab_registry_row("Content/Trees/Oak_Grove.umap", "Oak_Grove", "/Script/Engine.World"),
        ]
        records[1]["product_id"] = "wrong-product"
        self.fab_registry.write_text("\n".join(json.dumps(row) for row in records) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "product identity mismatch"):
            catalog.sync_catalog(
                self.snapshot, self.vault, self.manifests, self.database, "5.8",
                fab_registry_results_path=self.fab_registry,
            )
        self.assertFalse(self.database.exists())

        records[1]["product_id"] = "product-oak"
        records[0]["assets"][0]["tags"] = {"download_url": "https://example.invalid/signed"}
        self.fab_registry.write_text("\n".join(json.dumps(row) for row in records) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "prohibited"):
            catalog.sync_catalog(
                self.snapshot, self.vault, self.manifests, self.database, "5.8",
                fab_registry_results_path=self.fab_registry,
            )
        self.assertFalse(self.database.exists())

        records[0]["assets"][0]["tags"] = {}
        records[0]["assets"][0]["object_path"] = "/Game/Trees/Other"
        self.fab_registry.write_text("\n".join(json.dumps(row) for row in records) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "invalid Fab Asset Registry asset identity"):
            catalog.sync_catalog(
                self.snapshot, self.vault, self.manifests, self.database, "5.8",
                fab_registry_results_path=self.fab_registry,
            )
        self.assertFalse(self.database.exists())

        records[0].pop("assets")
        self.fab_registry.write_text("\n".join(json.dumps(row) for row in records) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "invalid Fab Asset Registry result"):
            catalog.sync_catalog(
                self.snapshot, self.vault, self.manifests, self.database, "5.8",
                fab_registry_results_path=self.fab_registry,
            )
        self.assertFalse(self.database.exists())

    def test_missing_manifest_result_is_classified_not_silently_omitted(self):
        self.manifests.write_text("", encoding="utf-8")
        summary = self.sync()
        self.assertEqual(summary["manifest_attempts"], 1)
        self.assertEqual(summary["manifest_not_attempted"], 1)

    def test_rejects_manifest_export_containing_prohibited_secret_fields(self):
        self.manifests.write_text(
            json.dumps(
                {
                    "success": True,
                    "product_id": "product-oak",
                    "artifact_id": "OakArtifactV1",
                    "manifest_url": "https://cdn.invalid/manifest?X-Amz-Credential=secret",
                    "files": [],
                }
            )
            + "\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(ValueError, "prohibited"):
            self.sync()
        self.assertFalse(self.database.exists())

    def test_rejects_manifest_result_with_mismatched_product_identity(self):
        result = json.loads(self.manifests.read_text(encoding="utf-8"))
        result["product_id"] = "wrong-product"
        self.manifests.write_text(json.dumps(result) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identity mismatch"):
            self.sync()
        self.assertFalse(self.database.exists())

    def test_discovers_local_corpora_and_keeps_registry_authority_separate(self):
        projects = self.root / "Projects"
        wot = projects / "WOT"
        test_project = projects / "Tool" / "Test"
        (wot / "Content" / "Maps").mkdir(parents=True)
        (test_project / "Content").mkdir(parents=True)
        current_project = wot / "WOT.uproject"
        current_project.write_text(json.dumps({"EngineAssociation": "5.8"}), encoding="utf-8")
        (test_project / "Test.uproject").write_text(json.dumps({"EngineAssociation": "5.8"}), encoding="utf-8")
        wot_map = wot / "Content" / "Maps" / "QuarryRoad.umap"
        wot_map.write_bytes(b"map")
        (test_project / "Content" / "Fixture.uasset").write_bytes(b"asset")
        engine_root = self.root / "UE_5.8"
        engine_content = engine_root / "Engine" / "Content"
        engine_plugin = engine_root / "Engine" / "Plugins" / "FakePlugin"
        engine_content.mkdir(parents=True)
        (engine_plugin / "Content").mkdir(parents=True)
        (engine_content / "EngineAsset.uasset").write_bytes(b"engine")
        (engine_plugin / "FakePlugin.uplugin").write_text(json.dumps({"EngineVersion": "5.8"}), encoding="utf-8")
        (engine_plugin / "Content" / "PluginAsset.uasset").write_bytes(b"plugin")
        registry = self.root / "registry.jsonl"
        registry.write_text(
            json.dumps(
                {
                    "success": True,
                    "expected_package_name": "/Game/Maps/QuarryRoad",
                    "package_name": "/Game/Maps/QuarryRoad",
                    "dependencies": ["/Script/Engine"],
                    "referencers": [],
                    "assets": [
                        {
                            "package_name": "/Game/Maps/QuarryRoad",
                            "package_path": "/Game/Maps",
                            "object_path": "/Game/Maps/QuarryRoad.QuarryRoad",
                            "asset_name": "QuarryRoad",
                            "asset_class": "/Script/Engine.World",
                            "tags": {"MapTag": "Value"},
                        }
                    ],
                }
            )
            + "\n",
            encoding="utf-8",
        )
        summary = catalog.sync_catalog(
            self.snapshot,
            self.vault,
            self.manifests,
            self.database,
            "5.8",
            scan_roots=[projects],
            current_project=current_project,
            registry_results_path=registry,
            include_engine=True,
            engine_root=engine_root,
        )
        self.assertEqual(summary["local_filesystem_packages"], 4)
        self.assertEqual(summary["asset_registry_enriched_packages"], 1)
        self.assertEqual(summary["asset_registry_assets"], 1)
        self.assertEqual(summary["local_asset_registry_unavailable_or_failed"], 3)
        connection = sqlite3.connect(self.database)
        try:
            statuses = dict(connection.execute("SELECT package_name, asset_registry_status FROM local_packages"))
            self.assertEqual(statuses["/Game/Maps/QuarryRoad"], "enriched")
            self.assertEqual(statuses["/Game/Fixture"], "unavailable-not-current-project")
            tags = connection.execute("SELECT tags_json FROM asset_registry_assets").fetchone()[0]
            self.assertEqual(json.loads(tags), {"MapTag": "Value"})
        finally:
            connection.close()
        rows = catalog.search_catalog(self.database, "QuarryRoad", limit=10)
        self.assertEqual({row["metadata_fidelity"] for row in rows}, {"filesystem", "asset-registry"})
        self.assertEqual(catalog.search_catalog(self.database, "EngineAsset", limit=10), [])
        self.assertEqual(len(catalog.search_catalog(self.database, "EngineAsset", include_engine=True, limit=10)), 1)


if __name__ == "__main__":
    unittest.main()
