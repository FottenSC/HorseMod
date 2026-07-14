from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ExportContractTests(unittest.TestCase):
    def test_name_transformer_manifest_value_has_no_object_identity_suffix(self):
        source = (ROOT / "tools" / "StructuredExporter.java").read_text(encoding="utf-8")
        self.assertIn('field("name_transformer", nameTransformerDescriptor(options))', source)
        self.assertIn("return transformer.getClass().getName();", source)
        self.assertNotIn("String.valueOf(options.getNameTransformer())", source)

    def test_msbuild_disk_fallback_checks_existing_amd64_host_first(self):
        source = (ROOT / "tools" / "refresh_ghidra_calibur.ps1").read_text(encoding="utf-8")
        self.assertIn('"*\\*\\MSBuild\\Current\\Bin\\amd64\\MSBuild.exe"', source)
        self.assertIn("Get-ChildItem -Path (Join-Path $root $pattern) -File", source)
        self.assertIn("if ($result) { return $result.FullName }", source)

    def test_pe_export_enumeration_preserves_ordinals_and_fails_closed(self):
        source = (ROOT / "tools" / "StructuredExporter.java").read_text(encoding="utf-8")
        self.assertIn("List<PeExport> exports = readPeExports", source)
        self.assertIn('numberField("ordinal", export.ordinal)', source)
        self.assertIn("Cannot enumerate PE exports", source)
        self.assertIn("if (export.rva == previousRva) continue;", source)
        self.assertIn("boolean leftNamed = !left.name.isEmpty();", source)
        self.assertNotIn("getExternalEntryPointIterator", source)

    def test_reuse_requires_the_same_ghidra_modification_audit(self):
        source = (ROOT / "tools" / "refresh_ghidra_calibur.ps1").read_text(encoding="utf-8")
        self.assertIn('"compiler_spec", "modification_number",', source)


if __name__ == "__main__":
    unittest.main()
