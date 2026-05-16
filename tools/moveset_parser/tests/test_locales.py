"""Tests for the localized movelist extraction from Game.archive."""
from __future__ import annotations

from pathlib import Path

import pytest

ARCHIVE_PATH = Path(
    r"C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump"
    r"\SoulcaliburVI\Content\Localization\Game\Steam\en\Game.archive"
)


pytestmark = pytest.mark.skipif(
    not ARCHIVE_PATH.exists(),
    reason=f"Game.archive not available at {ARCHIVE_PATH}",
)


def test_archive_loads():
    from locales import load_game_archive
    archive = load_game_archive(ARCHIVE_PATH)
    # Sanity: at least 20k localized strings
    assert len(archive) > 20000
    # Spot-check: Mitsurugi's "Heaven Cannon"
    assert archive.get("ID_CMD_001_0160_name") == "Heaven Cannon"


def test_mitsurugi_movelist_index():
    from locales import get_archive, build_movelist_index
    archive = get_archive(ARCHIVE_PATH)
    idx = build_movelist_index(archive, "001")
    # ~135 unique MoveListID entries for Mitsurugi
    assert len(idx) > 120
    # Spot-checks against known SC6 community notation
    assert idx[16].name == "Heaven Cannon"
    assert idx[16].command == "3B"
    assert idx[19].name == "Leap of the Loach"
    assert idx[19].command == "1B.B"
    assert idx[60].name == "Bell Breaker"
    assert idx[60].command == "(6)K"
    assert idx[1].name == "Prime Moon Shadow Rush"
    assert idx[1].command == "A.A.A"
    # Move 144's raw `_command` ships with a stray trailing space —
    # the canonical form must be stripped (regression pin).
    assert idx[144].command == "(2)|(8)B ~ 2B"
    for e in idx.values():
        assert e.command == e.command.strip(), f"untrimmed: {e.command!r}"
        assert e.name == e.name.strip(), f"untrimmed name: {e.name!r}"


def test_command_markup_conversion():
    from locales import to_canonical_input
    assert to_canonical_input("{cmd_3}{cmd_B}") == "3B"
    assert to_canonical_input("{cmd_A}.{cmd_A}.{cmd_A}") == "A.A.A"
    # Press-then-hold same direction collapses to (X) — readable
    # "press and hold" shorthand for charge motions.
    assert to_canonical_input("{cmd_6}{cmd_6h}{cmd_K}") == "(6)K"
    assert to_canonical_input("{cmd_1}{cmd_1h}") == "(1)"
    assert to_canonical_input("{cmd_A}{cmd_Ah}") == "(A)"
    # Plain held (no preceding press) stays bracketed.
    assert to_canonical_input("{cmd_Ah}") == "[A]"
    assert to_canonical_input("{cmd_3h}{cmd_B}") == "[3]B"
    # Mixed press X, hold Y — no collapse (different keys).
    assert to_canonical_input("{cmd_6}{cmd_3h}") == "6[3]"
    # Alternatives — in-game "or" becomes `|`.
    src = "{cmd_1}{cmd_1h}or{cmd_4}{cmd_4h}or{cmd_7}{cmd_7h}{cmd_B}+{cmd_K}"
    assert to_canonical_input(src) == "(1)|(4)|(7)B+K"
    # English "or" with spaces around it stays as-is — only adjacent-
    # to-input "or" is treated as an alternatives separator.
    assert to_canonical_input("Hold left or right {cmd_B}") == "Hold left or right B"
    assert to_canonical_input("{cmd_A}+{cmd_B}+{cmd_K}") == "A+B+K"
    assert to_canonical_input("During Mist {cmd_B}.{cmd_B}") == "During Mist B.B"
    # Slide-cancel — collapses to base button (SC6 community implicit)
    assert to_canonical_input("{cmd_Bs}{cmd_6}") == "B6"
    assert to_canonical_input("{cmd_Ks}{cmd_B}") == "KB"
    # Neutral
    assert to_canonical_input("{cmd_N}") == "N"
    # Empty / unknown
    assert to_canonical_input("") == ""


def test_clean_markup_strips_effect_glyphs():
    """clean_markup should strip {effect_X} and {font_X} tokens that
    leak into note fields. Regression: notes used to render literal
    '{effect_BA}' text."""
    from locales import clean_markup
    s = "Use {font_blue}{effect_BA} Break Attack{font_def} with {cmd_G}"
    out = clean_markup(s)
    assert "{" not in out, f"residual markup: {out!r}"
    assert "Break Attack" in out
    assert "G" in out


def test_clean_markup_handles_button_udlr_and_whitespace():
    """`{cmd_button_UDLR}` is the directional-pad token — it has an
    embedded underscore the generic {cmd_X} regex cannot match, so it
    used to leak into 118 grapple-break / movement notes as literal
    text. It now renders as `↕↔`. Also pin the whitespace tidy-up:
    stripping a trailing `{effect_BA}` and the ideographic spaces some
    notes ship with must not leave dangling whitespace."""
    from locales import clean_markup, to_canonical_input
    # The directional-pad token resolves, in both note contexts.
    assert clean_markup("Can move using {cmd_button_UDLR}") == "Can move using ↕↔"
    out = clean_markup(
        "{cmd_button_UDLR} (back) + attack button to grapple break")
    assert out == "↕↔ (back) + attack button to grapple break"
    assert "{" not in out
    # to_canonical_input handles it too (notes route through it).
    assert to_canonical_input("{cmd_button_UDLR}") == "↕↔"
    # Trailing {effect_BA} must not leave a dangling space.
    assert clean_markup("Unable to grapple break when held {effect_BA}") == (
        "Unable to grapple break when held")
    # Trailing ASCII and ideographic (　) spaces are stripped.
    assert clean_markup("Returns to crouching & facing away ") == (
        "Returns to crouching & facing away")
    assert clean_markup("Guard impact vs. high attacks　") == (
        "Guard impact vs. high attacks")
    # Defensive catch-all: an unknown future {…} token is dropped, never
    # shown raw.
    assert clean_markup("text {cmd_some_future_token} more") == "text more"


def test_split_condition_and_input():
    """The condition/input split powers the 'From' column. Regression
    cases: 'During Behind Lower A' used to split at 'During' because
    the 'B' in 'Behind' was matched as a button — fixed by requiring
    the button character not be followed by a lowercase letter."""
    from locales import split_condition_and_input as sp
    # Basic stance + input
    assert sp("During Mist B.B.B.B") == ("During Mist", "B.B.B.B")
    assert sp("During Right Outer A.K") == ("During Right Outer", "A.K")
    # The Behind/Avenger trap — stance names that START with input chars
    assert sp("During Behind Lower A") == ("During Behind Lower", "A")
    assert sp("During Behind Lower while soul charged K.K") == (
        "During Behind Lower while soul charged", "K.K")
    # While-conditions
    assert sp("While crouching K") == ("While crouching", "K")
    assert sp("While rising 3B") == ("While rising", "3B")
    assert sp("While soul charged 1K.B.B") == ("While soul charged", "1K.B.B")
    # After reversal edge / facing
    assert sp("After reversal edge hits A.A.A") == ("After reversal edge hits", "A.A.A")
    assert sp("Facing away 2B") == ("Facing away", "2B")
    # No condition — pure button/direction inputs pass through
    assert sp("3B") == ("", "3B")
    assert sp("A+B+K") == ("", "A+B+K")
    assert sp("(6)K") == ("", "(6)K")
    assert sp("(1)|(4)|(7)B+K") == ("", "(1)|(4)|(7)B+K")
    # Positional throws — no button input, whole thing is the input
    assert sp("Left side throw") == ("", "Left side throw")
    assert sp("Right side throw") == ("", "Right side throw")
    assert sp("Back throw") == ("", "Back throw")
    # Empty
    assert sp("") == ("", "")


def test_all_chars_have_movelist():
    """Every shipped character should have at least 100 movelist entries
    in the archive — pin the floor as a regression guard. Includes the 5
    DLC cids (009/017/022/028/061) added to the dump 2026-05-16."""
    from locales import get_archive, build_movelist_index
    archive = get_archive(ARCHIVE_PATH)
    # Iterate the shipping characters by cid
    cids = ["001", "002", "003", "004", "005", "006", "007", "009", "00b",
            "00c", "00d", "00f", "011", "012", "013", "014", "015", "016",
            "017", "022", "023", "024", "028", "030", "060", "061", "062",
            "064", "065"]
    for cid in cids:
        idx = build_movelist_index(archive, cid)
        assert len(idx) >= 100, f"chara {cid} has only {len(idx)} movelist entries"


def test_notes_have_markup_converted():
    """Notes (the 'Cancel with G' hints) also use {cmd_X} markup and
    must be canonicalized for display."""
    from locales import get_archive, build_movelist_index
    archive = get_archive(ARCHIVE_PATH)
    idx = build_movelist_index(archive, "001")
    # "Prime Moon Shadow Rush" has note "Cancel 1st hit with G" (with {cmd_G})
    entry = idx[1]
    assert entry.note  # has a note
    assert "{cmd_" not in entry.note, f"unconverted markup in note: {entry.note!r}"


def test_movelist_category_names():
    """The 11 movelist categories are positional — index N always maps
    to the same localized tab name. Pin the mapping as a regression
    guard so a reorder is caught."""
    from locales import movelist_category_name, MOVELIST_CATEGORY_NAMES
    assert len(MOVELIST_CATEGORY_NAMES) == 11
    assert movelist_category_name(0) == "Main Attacks"
    assert movelist_category_name(3) == "Horizontal Attacks"
    assert movelist_category_name(8) == "Throws"
    assert movelist_category_name(10) == "Lethal Hit Attacks"
    # Out-of-range falls back gracefully
    assert movelist_category_name(99) == "Category 99"
    assert movelist_category_name(-1) == "Category -1"
