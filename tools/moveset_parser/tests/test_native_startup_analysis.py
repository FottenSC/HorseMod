from pathlib import Path
from types import SimpleNamespace

import pytest

from luxformats import parse_khd
from native_startup_analysis import analyze_player_startup


HDR_ROOT = Path(__file__).parents[3] / "dump" / "Battle" / "hdr"


def _bank(cid: str):
    path = HDR_ROOT / f"hdr{cid}.khd"
    if not path.exists():
        pytest.skip(f"checked-in {path.name} is unavailable")
    return parse_khd(path.read_bytes())


def test_astaroth_bear_fang_uses_timed_contact_variant_and_one_based_impact():
    evidence = analyze_player_startup(_bank("012"), 341, 110)

    assert evidence is not None
    assert evidence.route_cell == 110
    assert evidence.effective_cell == 111
    assert evidence.effective_variant == 1
    assert evidence.master_window_start == 15
    assert evidence.selection_coordinate == 15
    assert evidence.impact_coordinate == 15
    assert evidence.player_impact_frame == 16


def test_direct_contact_cell_converts_zero_based_coordinate_to_impact_frame():
    evidence = analyze_player_startup(_bank("012"), 347, 122)

    assert evidence is not None
    assert evidence.effective_cell == 122
    assert evidence.master_window_start == 13
    assert evidence.player_impact_frame == 14


def test_dynamic_variant_without_local_timing_proof_fails_closed():
    # hdr011 slot 386 has a special conditional variant whose nearby timing
    # operand is the 0x7FFF sentinel.  It must not become an ordinary i2 move.
    assert analyze_player_startup(_bank("011"), 386, 137) is None


def test_variant_selection_later_than_cell_window_controls_impact():
    evidence = analyze_player_startup(_bank("022"), 400, 248)

    assert evidence is not None
    assert evidence.master_window_start == 17
    assert evidence.selection_coordinate == 18
    assert evidence.player_impact_frame == 19


def test_distinct_timed_variant_candidates_fail_closed():
    def instruction(
        mnemonic: str,
        *,
        push: bool = False,
        value: int | None = None,
        function: int | None = None,
        argc: int | None = None,
    ):
        return SimpleNamespace(
            mnemonic=mnemonic,
            push_flag=push,
            imm_u16=value,
            opcode=0x0B if value is not None else 0x25,
            imm_b0=function,
            imm_b1=argc,
        )

    def cell(index: int, *, ordinary: bool):
        return SimpleNamespace(
            cell_role="Attack",
            wI16BaseDamage=20 + index,
            has_valid_active_window=True,
            wU16AttackFlags=1 if ordinary else 0,
            wI16MasterWindowStart=10 + index,
            wI16MasterWindowEnd=20 + index,
        )

    script = SimpleNamespace(instructions=[
        instruction("SET_ACC_U16", push=True, value=11),
        instruction("CALLCOND", push=True, function=0x25, argc=1),
        instruction("SET_ACC_U16", push=True, value=1),
        instruction("CALLCOND", function=0x26, argc=1),
        instruction("SET_ACC_U16", push=True, value=12),
        instruction("CALLCOND", push=True, function=0x25, argc=1),
        instruction("SET_ACC_U16", push=True, value=2),
        instruction("CALLCOND", function=0x26, argc=1),
    ])
    slot = SimpleNamespace(
        bytecode=script,
        nCellBoneIndexPerVariant=[0, 1, 2],
    )
    bank = SimpleNamespace(
        slots=[slot],
        sections=[SimpleNamespace(entries=[
            cell(0, ordinary=False),
            cell(1, ordinary=True),
            cell(2, ordinary=True),
        ])],
    )

    assert analyze_player_startup(bank, 0, 0) is None


def test_preimpact_variant_replaces_already_contact_capable_route_cell():
    def instruction(
        mnemonic: str,
        *,
        push: bool = False,
        value: int | None = None,
        function: int | None = None,
        argc: int | None = None,
    ):
        return SimpleNamespace(
            mnemonic=mnemonic,
            push_flag=push,
            imm_u16=value,
            opcode=0x0B if value is not None else 0x25,
            imm_b0=function,
            imm_b1=argc,
        )

    cells = [
        SimpleNamespace(
            cell_role="Attack", wI16BaseDamage=20,
            has_valid_active_window=True, wU16AttackFlags=1,
            wI16MasterWindowStart=19, wI16MasterWindowEnd=20,
        ),
        SimpleNamespace(
            cell_role="Attack", wI16BaseDamage=24,
            has_valid_active_window=True, wU16AttackFlags=1,
            wI16MasterWindowStart=15, wI16MasterWindowEnd=20,
        ),
    ]
    script = SimpleNamespace(instructions=[
        instruction("SET_ACC_U16", push=True, value=15),
        instruction("CALLCOND", push=True, function=0x25, argc=1),
        instruction("SET_ACC_U16", push=True, value=1),
        instruction("CALLCOND", function=0x26, argc=1),
    ])
    bank = SimpleNamespace(
        slots=[SimpleNamespace(
            bytecode=script,
            nCellBoneIndexPerVariant=[0, 1],
        )],
        sections=[SimpleNamespace(entries=cells)],
    )

    evidence = analyze_player_startup(bank, 0, 0)

    assert evidence is not None
    assert evidence.effective_cell == 1
    assert evidence.player_impact_frame == 16


def test_postimpact_variant_does_not_replace_first_contact_cell():
    cells = [
        SimpleNamespace(
            cell_role="Attack", wI16BaseDamage=20,
            has_valid_active_window=True, wU16AttackFlags=1,
            wI16MasterWindowStart=10, wI16MasterWindowEnd=12,
        ),
        SimpleNamespace(
            cell_role="Attack", wI16BaseDamage=24,
            has_valid_active_window=True, wU16AttackFlags=1,
            wI16MasterWindowStart=15, wI16MasterWindowEnd=18,
        ),
    ]
    instructions = [
        SimpleNamespace(mnemonic="SET_ACC_U16", push_flag=True, imm_u16=15,
                        opcode=0x0B, imm_b0=None, imm_b1=None),
        SimpleNamespace(mnemonic="CALLCOND", push_flag=True, imm_u16=None,
                        opcode=0x25, imm_b0=0x25, imm_b1=1),
        SimpleNamespace(mnemonic="SET_ACC_U16", push_flag=True, imm_u16=1,
                        opcode=0x0B, imm_b0=None, imm_b1=None),
        SimpleNamespace(mnemonic="CALLCOND", push_flag=False, imm_u16=None,
                        opcode=0x25, imm_b0=0x26, imm_b1=1),
    ]
    bank = SimpleNamespace(
        slots=[SimpleNamespace(
            bytecode=SimpleNamespace(instructions=instructions),
            nCellBoneIndexPerVariant=[0, 1],
        )],
        sections=[SimpleNamespace(entries=cells)],
    )

    evidence = analyze_player_startup(bank, 0, 0)

    assert evidence is not None
    assert evidence.effective_cell == 0
    assert evidence.player_impact_frame == 11
