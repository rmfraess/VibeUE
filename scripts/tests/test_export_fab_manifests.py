# Copyright Buckley Builds LLC 2026 All Rights Reserved.

import json
import sys
import tempfile
import types
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

import export_fab_manifests as export


def _listing(listing_id, install_version="UE_5.7", versions=("UE_5.6", "UE_5.7")):
    options = [{"assetFormat": "unreal-engine", "engineVersion": install_version}] if install_version else []
    return {"listingId": listing_id, "title": listing_id, "engineVersions": list(versions), "installOptions": options}


class ExportFabManifestsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.listings = self.root / "library.json"
        self.output = self.root / "manifests.jsonl"
        self.calls = []

        def inspect(listing_id, engine_version, max_files):
            self.calls.append((listing_id, engine_version, max_files))
            if listing_id == "signed":
                return json.dumps({"success": True, "artifact_id": "A", "files": [{"path": "https://cdn/x?signature=1"}]})
            if listing_id == "refused":
                return json.dumps({"success": False, "error_code": "MANIFEST_RESOLUTION_FAILED", "error": "not owned"})
            return json.dumps({"success": True, "listing_id": listing_id, "product_id": "item-" + listing_id,
                               "artifact_id": "artifact-" + listing_id, "total_files": 1,
                               "files": [{"path": "Content/SM_Oak.uasset", "size_bytes": 3}]})

        fake = types.ModuleType("unreal")
        fake.FabManifestService = types.SimpleNamespace(inspect_owned_manifest=inspect)
        sys.modules["unreal"] = fake

    def tearDown(self):
        sys.modules.pop("unreal", None)
        self.temp.cleanup()

    def _rows(self):
        return [json.loads(line) for line in self.output.read_text(encoding="utf-8").splitlines()]

    def test_every_listing_gets_one_classified_row(self):
        listings = [_listing("oak"), _listing("signed"), _listing("refused"), _listing("plain", "", ())]
        self.listings.write_text(json.dumps({"results": listings}), encoding="utf-8")

        first = export.export_batch(str(self.listings), str(self.output), 0, 2)
        second = export.export_batch(str(self.listings), str(self.output), 2, 2)

        self.assertEqual((first["succeeded_in_batch"], first["failed_in_batch"]), (1, 1))
        self.assertEqual((second["succeeded_in_batch"], second["failed_in_batch"]), (0, 2))
        rows = {row["listing_id"]: row for row in self._rows()}
        self.assertEqual(rows["oak"]["artifact_id"], "artifact-oak")
        # A signed location in a result is replaced by a classified failure, never written.
        self.assertEqual(rows["signed"]["error_code"], "INSPECTION_FAILED")
        self.assertNotIn("https://", self.output.read_text(encoding="utf-8"))
        self.assertEqual(rows["refused"]["error_code"], "MANIFEST_RESOLUTION_FAILED")
        self.assertEqual(rows["refused"]["artifact_id"], "unresolved:refused")
        self.assertEqual(rows["plain"]["error_code"], "NO_UNREAL_FORMAT")
        self.assertNotIn("plain", [call[0] for call in self.calls])
        self.assertEqual(
            export.validate_export(str(self.listings), str(self.output)),
            {"expected": 4, "exported": 4, "succeeded": 1, "failed": 3},
        )

    def test_engine_version_prefers_install_option_then_newest_offered(self):
        self.assertEqual(export._engine_version(_listing("a", "UE_5.6")), "UE_5.6")
        self.assertEqual(export._engine_version(_listing("b", "", ("UE_5.9", "UE_5.10", "UE_4.27"))), "UE_5.10")
        self.assertEqual(export._engine_version(_listing("c", "", ())), "")

    def test_rejects_incomplete_exports_and_repeated_listings(self):
        self.listings.write_text(json.dumps([_listing("oak"), _listing("elm")]), encoding="utf-8")
        export.export_batch(str(self.listings), str(self.output), 0, 1)
        with self.assertRaises(ValueError):
            export.validate_export(str(self.listings), str(self.output))
        export.export_batch(str(self.listings), str(self.output), 1, 1)
        with self.assertRaises(ValueError):
            export.export_batch(str(self.listings), str(self.output), 1, 1)


if __name__ == "__main__":
    unittest.main()
