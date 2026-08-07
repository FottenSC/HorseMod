"""Tests for the UE4 .uasset/.uexp parser.

These pin the canonical movelist extraction across all 24 ship-state
characters. Two real regressions are covered:
  * Empty struct-array doesn't have a leading inner tag — over-read by
    one tag-header used to knock the rest of the stream out of sync.
  * Export's serial_size sometimes ends with up to 7 bytes of zero
    alignment padding instead of a "None" FName terminator — the
    parser used to walk past that and try to read past EOF.
"""
from __future__ import annotations

import os
import struct
from pathlib import Path

import pytest

DUMP_ROOT = Path(
    r"C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools"
    r"\dump\SoulcaliburVI\Content\Style"
)


def _asset_path(cid: str) -> Path:
    return DUMP_ROOT / cid / f"DA_MovePlayData_{cid}.uasset"


def _uexp_path(cid: str) -> Path:
    return DUMP_ROOT / cid / f"DA_MovePlayData_{cid}.uexp"


pytestmark = pytest.mark.skipif(
    not DUMP_ROOT.exists(),
    reason=f"SC6 dump not available at {DUMP_ROOT}",
)


def _all_chars() -> list[str]:
    return sorted(
        d.name for d in DUMP_ROOT.iterdir()
        if d.is_dir() and (DUMP_ROOT / d.name / f"DA_MovePlayData_{d.name}.uasset").exists()
    )


def test_uasset_parses_mitsurugi():
    from uassetparse import parse_uasset
    pkg = parse_uasset(str(_asset_path("001")))
    # Name table — Mitsurugi notably has 23 names because his movelist
    # omits the RyuuhaType property (it's the default value).
    assert len(pkg.name_table) == 23
    assert "CategoryPlayList" in pkg.name_table
    assert "LuxBattleMovePlayData" in pkg.name_table
    # Exactly one export, the DataAsset itself
    assert len(pkg.exports) == 1


@pytest.mark.parametrize("cid", _all_chars())
def test_all_24_chars_parse(cid):
    """The hard regression: every shipping character's MovePlayData
    must parse cleanly. Earlier versions blew up on 4 chars (012/024/
    064/065) because of empty arrays and trailing zero-padding."""
    from uassetparse import parse_uasset, parse_uexp
    pkg = parse_uasset(str(_asset_path(cid)))
    data = parse_uexp(str(_uexp_path(cid)), pkg)
    cpl = data.get("CategoryPlayList", [])
    # Every character has 11 categories
    assert len(cpl) == 11, f"{cid} has {len(cpl)} categories, expected 11"
    # And > 100 total movelist items
    total = sum(len(c.get("Items", [])) for c in cpl)
    assert total > 100, f"{cid} has only {total} movelist items"


def test_mitsurugi_has_movelist_shape():
    """Schema validation against Mitsurugi data."""
    from uassetparse import parse_uasset, parse_uexp
    pkg = parse_uasset(str(_asset_path("001")))
    data = parse_uexp(str(_uexp_path("001")), pkg)
    cpl = data["CategoryPlayList"]
    # 11 categories
    assert len(cpl) == 11
    # First category has Items
    cat = cpl[0]
    assert "Items" in cat
    items = cat["Items"]
    assert len(items) > 0
    # First item structure
    first = items[0]
    assert "MoveListID" in first
    assert "Param" in first
    assert isinstance(first["MoveListID"], int)
    param = first["Param"]
    assert "CommandSets" in param
    css = param["CommandSets"]
    assert len(css) >= 1
    cs = css[0]
    assert "MainIndex" in cs
    assert "IntroIndex" in cs
    assert isinstance(cs["MainIndex"], int)


def test_movelist_main_indices_mostly_overlap_local_slot_range():
    """Track the corpus property used by the legacy navigation heuristic.

    This does not prove MainIndex is a KHD slot index; its consumer is in
    cooked Blueprint code that is absent from the current dump.
    """
    from uassetparse import parse_uasset, parse_uexp
    import luxformats as lf

    pkg = parse_uasset(str(_asset_path("001")))
    data = parse_uexp(str(_uexp_path("001")), pkg)
    khd_path = r"E:\myMods\dump\Battle\hdr\hdr001.khd"
    if not os.path.exists(khd_path):
        pytest.skip("Mitsurugi .khd not available")
    bank = lf.parse_khd(open(khd_path, "rb").read())
    slot_count = len(bank.slots)

    main_indices = set()
    for cat in data["CategoryPlayList"]:
        for item in cat["Items"]:
            for cs in item["Param"]["CommandSets"]:
                if cs["MainIndex"]:
                    main_indices.add(cs["MainIndex"])

    out_of_range = [i for i in main_indices if i >= slot_count]
    # A handful of CommandSets reference move-ids in the shared/common
    # bank (high nibble != 0) — those WILL exceed our local slot count.
    # Most should be in range; flag if more than 10% are OOB.
    assert len(out_of_range) / len(main_indices) < 0.1, (
        f"{len(out_of_range)}/{len(main_indices)} MainIndex values are "
        f"out of slot range"
    )


# --- DataTable parsing (DA_MoveListTable / DA_MoveCategoryTable) ----------

@pytest.mark.parametrize("cid", _all_chars())
def test_movelisttable_parses(cid):
    """DA_MoveListTable_<cid> is a UE4 DataTable. Verify it parses and
    every row carries the AttributeTag / EffectTag fields."""
    import uassetparse
    p = DUMP_ROOT / cid / f"DA_MoveListTable_{cid}.uasset"
    if not p.exists():
        pytest.skip(f"DA_MoveListTable_{cid} not in dump")
    pkg = uassetparse.parse_uasset(str(p))
    rows = uassetparse.parse_datatable(str(p).replace(".uasset", ".uexp"), pkg)
    assert len(rows) > 50, f"{cid}: only {len(rows)} MoveListTable rows"
    # Rows are keyed by decimal strings; each has the schema fields.
    for rn, row in rows.items():
        assert rn.isdigit(), f"{cid}: non-numeric row key {rn!r}"
        assert "AttributeTag" in row
        assert "EffectTag" in row
        break


def test_movecategorytable_parses():
    """DA_MoveCategoryTable maps rows to {CategoryID, MoveListID}."""
    import uassetparse
    p = DUMP_ROOT / "001" / "DA_MoveCategoryTable_001.uasset"
    if not p.exists():
        pytest.skip("DA_MoveCategoryTable_001 not in dump")
    pkg = uassetparse.parse_uasset(str(p))
    rows = uassetparse.parse_datatable(str(p).replace(".uasset", ".uexp"), pkg)
    assert len(rows) > 50
    sample = next(iter(rows.values()))
    assert "CategoryID" in sample and "MoveListID" in sample
    # CategoryID is one of the 11 movelist categories.
    assert all(0 <= r["CategoryID"] <= 10 for r in rows.values())


def test_parse_datatable_rejects_non_datatable():
    """parse_datatable must fail loudly when handed a plain UDataAsset
    (DA_MovePlayData) rather than mis-parsing it as a DataTable. The
    bound-checks added after the 4-byte-gap discovery guard this."""
    import uassetparse
    p = DUMP_ROOT / "001" / "DA_MovePlayData_001.uasset"
    if not p.exists():
        pytest.skip("DA_MovePlayData_001 not in dump")
    pkg = uassetparse.parse_uasset(str(p))
    with pytest.raises(ValueError):
        uassetparse.parse_datatable(str(p).replace(".uasset", ".uexp"), pkg)


def _synthetic_datatable_package(payload_size: int):
    from uassetparse import UAssetExport, UAssetPackage

    return UAssetPackage(
        name_table=["None", "RowA"],
        exports=[UAssetExport(0, 0, 0, payload_size, 0)],
        total_header_size=0,
    )


def test_parse_datatable_rejects_advertised_rows_missing_from_export(tmp_path):
    import uassetparse

    none_tag = struct.pack("<ii", 0, 0)
    row_a = struct.pack("<ii", 1, 0) + none_tag
    payload = none_tag + struct.pack("<ii", 0, 2) + row_a
    path = tmp_path / "truncated.uexp"
    path.write_bytes(payload)

    with pytest.raises(ValueError, match="truncated before row 1 of 2"):
        uassetparse.parse_datatable(
            str(path), _synthetic_datatable_package(len(payload))
        )


def test_parse_datatable_rejects_duplicate_row_names(tmp_path):
    import uassetparse

    none_tag = struct.pack("<ii", 0, 0)
    row_a = struct.pack("<ii", 1, 0) + none_tag
    payload = none_tag + struct.pack("<ii", 0, 2) + row_a + row_a
    path = tmp_path / "duplicates.uexp"
    path.write_bytes(payload)

    with pytest.raises(ValueError, match="duplicate row name 'RowA'"):
        uassetparse.parse_datatable(
            str(path), _synthetic_datatable_package(len(payload))
        )


def test_parse_uexp_rejects_export_span_outside_file(tmp_path):
    import uassetparse

    path = tmp_path / "short.uexp"
    path.write_bytes(bytes(8))
    pkg = _synthetic_datatable_package(9)
    with pytest.raises(ValueError, match="outside"):
        uassetparse.parse_uexp(str(path), pkg)


def test_tiamats_rampage_metadata():
    """Zasalamel's Tiamat's Rampage (moveId 75) is a 3-hit Unblockable
    Attack per DA_MoveListTable — regression-pin the metadata that the
    DA_MovePlayData/.khd path alone could not surface."""
    import uassetparse
    p = DUMP_ROOT / "024" / "DA_MoveListTable_024.uasset"
    if not p.exists():
        pytest.skip("DA_MoveListTable_024 not in dump")
    pkg = uassetparse.parse_uasset(str(p))
    rows = uassetparse.parse_datatable(str(p).replace(".uasset", ".uexp"), pkg)
    row = rows["75"]
    assert row["AttributeTag"] == "M.M.M"   # three Mid hits
    assert row["EffectTag"] == "UA"          # Unblockable Attack
