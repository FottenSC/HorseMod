"""Localized movelist strings from SC6's UE4 Game.archive.

Game.archive is the source-text JSON (UTF-16 + BOM) that UE4's
localization pipeline produces alongside Game.locres. It holds the
canonical (key -> translation) mapping for every shipped string.

For SC6 movelist text, keys follow a strict naming convention:

    ID_CMD_<cid>_<NNNN>_{name|command|note|yomi}

where:
    cid    — character ID (e.g. '001' for Mitsurugi)
    NNNN   — `MoveListID * 10`, zero-padded to 4 digits
    name   — display name (e.g. 'Heaven Cannon')
    command— canonical input notation in {cmd_X} markup
    note   — usage hint shown in the in-game movelist
    yomi   — Japanese reading (empty in EN)

The DA_MovePlayData_<cid>.uexp gives us MoveListID values (1..146 for
Mitsurugi). Multiplying by 10 and zero-padding to 4 digits gives the
archive key. The MoveListID-by-10 convention is verified across all 24
shipping characters.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# {cmd_X} markup -> canonical SC6 input notation
# ---------------------------------------------------------------------------
#
# The game's movelist uses {cmd_<token>} markup that the UI replaces with
# button glyphs. For text output we collapse them to canonical SC6
# community notation:
#
#     {cmd_A}/{cmd_B}/{cmd_K}/{cmd_G}    → A / B / K / G
#     {cmd_1}..{cmd_9}                    → numpad directions
#     {cmd_Xh}  (held)                    → [X]   e.g. [A], [3]
#     {cmd_Xs}  (slide — buttons rolled)  → X     (slide is implicit in SC6
#                                                   notation; tokens
#                                                   collapse to the base)
#     {cmd_N}   (neutral, no direction)   → N
#     {cmd_def} (stylistic — leave alone) → def
#     {cmd_button_UDLR}                   → ↕↔    (the directional stick /
#                                                   d-pad as a whole — "any
#                                                   direction"; appears in
#                                                   grapple-break / movement
#                                                   notes, never commands)
#
# Other markup tokens that may appear in notes / tutorials are stripped
# by `clean_markup`:
#     {effect_BA} = Break Attack glyph
#     {effect_GI} = Guard Impact glyph
#     {font_blue}…{font_def} = colour markup

# Composite {cmd_X} tokens that are NOT a single button/direction glyph.
# The generic _CMD_TOKEN regex below is alphanumeric-only, so it cannot
# even match these (the embedded underscore breaks it) — they must be
# substituted explicitly and BEFORE the generic pass. `↕↔` (up-down +
# left-right arrows) reads as "any direction" and stays symbol-consistent
# with the numpad notation used for the rest of the input string.
_SPECIAL_CMD = {
    "{cmd_button_UDLR}": "↕↔",
}
_SPECIAL_CMD_RE = re.compile("|".join(re.escape(k) for k in _SPECIAL_CMD))

_CMD_TOKEN = re.compile(r"\{cmd_([A-Za-z0-9]+)\}")
_OTHER_MARKUP = re.compile(r"\{(?!cmd_)[A-Za-z_][A-Za-z0-9_]*\}")
# Final catch-all: any {…} markup token that survived both the cmd and
# the other-markup passes. Used only by `clean_markup` (notes/tutorials)
# so raw markup never reaches the UI; `to_canonical_input` deliberately
# stays strict so a genuinely unhandled command token surfaces as a bug.
_LEFTOVER_MARKUP = re.compile(r"\{[^}]*\}")
# Press-then-hold same direction/button — `1[1]` collapses to `(1)`. The
# `X[X]` motion is canonical SC6 charge-attack input (tap a direction,
# then keep holding to charge); `(X)` is a more compact "press and hold"
# shorthand than spelling out the press + the held form separately. The
# standalone bracketed form `[X]` is reserved for "just hold X" without
# an initial press.
_PRESS_HOLD = re.compile(r"([A-Z0-9])\[\1\]")
# Alternatives separator — the game's source uses literal "or" between
# alternate inputs (e.g. "1[1]or4[4]or7[7]B+K" charges any backward
# direction); replace with `|` for compactness. Requires "or" to be
# adjacent to input tokens on both sides so English "or" inside condition
# phrases (with spaces around it) stays intact.
_ALT_OR = re.compile(r"([A-Z\]\)0-9])\s*or\s*([A-Z\[\(0-9])")


def _convert_cmd(token: str) -> str:
    """Convert a single {cmd_X} marker into canonical text."""
    if token == "N":
        return "N"                     # neutral
    if len(token) > 1 and token.endswith("h"):
        return f"[{token[:-1]}]"       # held — bracketed
    if len(token) > 1 and token.endswith("s"):
        # Slide-cancel — SC6 community notation treats this as implicit
        # (the rolling motion is implied by listing the buttons), so
        # `{cmd_As}{cmd_B}` becomes `AB` not `As B`. Collapse to the base.
        return token[:-1]
    return token


def to_canonical_input(markup: str) -> str:
    """Convert SC6 movelist {cmd_X} markup into canonical fighting-game
    input notation. Examples:

        '{cmd_3}{cmd_B}'              -> '3B'
        '{cmd_A}.{cmd_A}.{cmd_A}'     -> 'A.A.A'
        '{cmd_6}{cmd_6h}{cmd_K}'      -> '(6)K'     (press+hold collapses)
        '{cmd_A}+{cmd_B}+{cmd_K}'     -> 'A+B+K'
        '{cmd_Bs}{cmd_6}'             -> 'B6'       (slide collapses)
        '{cmd_N}'                     -> 'N'
        'During Mist {cmd_B}.{cmd_B}' -> 'During Mist B.B'
        '{cmd_1}{cmd_1h}or{cmd_4}{cmd_4h}{cmd_B}+{cmd_K}'
                                      -> '(1)|(4)B+K' (alternatives via |)
    """
    if not markup:
        return ""
    # Composite tokens (e.g. {cmd_button_UDLR}) first — they have an
    # underscore the generic _CMD_TOKEN pass cannot match.
    s = _SPECIAL_CMD_RE.sub(lambda m: _SPECIAL_CMD[m.group(0)], markup)
    s = _CMD_TOKEN.sub(lambda m: _convert_cmd(m.group(1)), s)
    s = _PRESS_HOLD.sub(r"(\1)", s)
    s = _ALT_OR.sub(r"\1|\2", s)
    return s


def clean_markup(text: str) -> str:
    """Run `to_canonical_input` THEN strip any remaining UE4-style
    `{tag}` markup tokens that aren't `cmd_*`. Notes and tutorials use
    `{effect_BA}`, `{font_blue}…{font_def}` etc. that look like garbage
    in a plain-text view."""
    if not text:
        return ""
    s = _OTHER_MARKUP.sub("", to_canonical_input(text))
    # Defensive: drop any markup token that slipped past both passes so
    # the UI never shows literal `{…}`. The token sweep over Game.archive
    # found none today — this guards future / unseen tokens only.
    s = _LEFTOVER_MARKUP.sub("", s)
    # Tidy whitespace: stripping a `{effect_BA}`/`{font_*}` token leaves a
    # dangling space, and many notes ship with a trailing ASCII or
    # ideographic (　) space. Collapse runs and strip both ends.
    return re.sub(r"\s+", " ", s).strip()


# ---------------------------------------------------------------------------
# Condition (stance/state) prefix vs button-input split
# ---------------------------------------------------------------------------
#
# SC6 movelist commands often have a "condition" prefix:
#
#     "During Mist B.B.B.B"            -> condition="During Mist", input="B.B.B.B"
#     "While crouching K"              -> condition="While crouching", input="K"
#     "While rising K"                 -> condition="While rising", input="K"
#     "While soul charged 1K.B.B"      -> condition="While soul charged", input="1K.B.B"
#     "After reversal edge hits A.A.A" -> condition="After reversal edge hits", input="A.A.A"
#     "Facing away 2B"                 -> condition="Facing away", input="2B"
#     "During jump K"                  -> condition="During jump", input="K"
#     "After running A+G"              -> condition="After running", input="A+G"
#     "While Jolly during Possession 4A" -> condition="While Jolly during Possession", input="4A"
#
# Conditions that do NOT take a button input (the whole thing IS the input):
#
#     "Left side throw"   -> condition="", input="Left side throw"
#     "Right side throw"  -> condition="", input="Right side throw"
#     "Back throw"        -> condition="", input="Back throw"
#
# The split makes the UI sort + filter the condition independently of the
# raw button sequence.

_CONDITION_KEYWORDS = (
    "During ", "While ", "After ", "Facing ", "8-way ", "Jumping ",
    "When hit", "With Red", "With White",
)
# An "input character" is anything a button/direction sequence can start
# with: A/B/K/G, digit, [ (held), ( (group). The negative lookahead is
# crucial — without it, the regex would match the "B" in "During Behind"
# or the "A" in "Avenger" and split the condition string in the wrong
# place. A real input token is followed by another input char, a
# direction-joiner (+ . ~), a closing bracket, whitespace, or end-of-string,
# never by a lowercase letter (which would mean we're inside a word).
_INPUT_CHAR_RE = re.compile(r"[ABKG0-9\[\(](?![a-z])")


def split_condition_and_input(markup_text: str) -> tuple[str, str]:
    """Split an SC6 movelist command string into a (condition, input)
    pair. Returns ('', text) if there's no detectable condition prefix.

    Input is the cleaned canonical text (post `to_canonical_input`), NOT
    the raw `{cmd_X}` markup — callers should pre-process the markup.
    """
    s = (markup_text or "").strip()
    if not s:
        return "", ""
    # Quick reject: doesn't start with a condition keyword
    if not any(s.startswith(k) for k in _CONDITION_KEYWORDS):
        return "", s
    # Find the FIRST whitespace immediately before an input character.
    # The whole prefix may include multiple words ("While soul charged
    # after reversal edge hits"), but the condition always ENDS with a
    # space followed by a button/direction token.
    for m in _INPUT_CHAR_RE.finditer(s):
        pos = m.start()
        if pos == 0:
            continue
        # The split is valid only if the char IMMEDIATELY before is a
        # space. Otherwise we're inside a word (e.g. "Jolly").
        if s[pos - 1] != " ":
            continue
        condition = s[: pos - 1].strip()
        button_input = s[pos:].strip()
        return condition, button_input
    # No input character found after the condition — the whole string is
    # condition-only (rare, e.g. a stance-entry move described purely as
    # "Left side throw" without a button).
    return "", s


# ---------------------------------------------------------------------------
# Movelist category names
# ---------------------------------------------------------------------------
#
# DA_MovePlayData_<cid>'s CategoryPlayList always has exactly 11 entries
# (verified across all 24 shipping characters). The categories are
# POSITIONAL — index N maps to the localized name below, sourced from
# Game.archive keys `ID_SYS_CMD_CATE_0000`..`_0010`. The in-game movelist
# UI uses these as the tab labels.
#
# Category 0 ("Main Attacks") is a curated highlight list that re-lists
# moves from the type-specific categories — this is why ~689 movelist
# entries appear twice (once in cat 0, once in their real type category).
MOVELIST_CATEGORY_NAMES: tuple[str, ...] = (
    "Main Attacks",            # 0
    "Reversal Edge Attacks",   # 1
    "Gauge Attacks",           # 2
    "Horizontal Attacks",      # 3
    "Vertical Attacks",        # 4
    "Kicks",                   # 5
    "Dual Button Attacks",     # 6
    "8-Way Run Moves",         # 7
    "Throws",                  # 8
    "Special Moves",           # 9
    "Lethal Hit Attacks",      # 10
)


def movelist_category_name(index: int) -> str:
    """Return the localized movelist-category name for a CategoryPlayList
    index, or a fallback ``"Category N"`` for out-of-range indices."""
    if 0 <= index < len(MOVELIST_CATEGORY_NAMES):
        return MOVELIST_CATEGORY_NAMES[index]
    return f"Category {index}"


# ---------------------------------------------------------------------------
# Game.archive loader + indexer
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class MoveListEntry:
    """One localized movelist entry."""
    name: str
    command_markup: str           # raw {cmd_X} markup as shipped
    command: str                  # canonicalized (e.g. '3B', '66K', 'B+G')
    note: str                     # usage hint (empty if none)
    yomi: str                     # Japanese reading (empty in EN)


def load_game_archive(path: Path | str) -> dict[str, str]:
    """Load Game.archive and return a flat `(Source.Text -> Translation.Text)`
    dict. Source.Text holds the namespace-qualified key, Translation.Text
    holds the localized value."""
    raw = Path(path).read_bytes()
    # Strip UTF-16 LE BOM if present
    if raw.startswith(b"\xff\xfe"):
        text = raw[2:].decode("utf-16-le")
    elif raw.startswith(b"\xfe\xff"):
        text = raw[2:].decode("utf-16-be")
    else:
        text = raw.decode("utf-8")
    j = json.loads(text)

    out: dict[str, str] = {}

    def walk(node: dict) -> None:
        for c in node.get("Children", []):
            src = c.get("Source", {}).get("Text", "")
            tr = c.get("Translation", {}).get("Text", "")
            if src:
                out[src] = tr
        for sub in node.get("Subnamespaces", []):
            walk(sub)

    walk(j)
    return out


def build_movelist_index(
    archive_map: dict[str, str],
    cid: str,
) -> dict[int, MoveListEntry]:
    """Extract movelist entries for one character. Returns a dict keyed
    by `MoveListID` (1..N from DA_MovePlayData), pointing to the four
    name/command/note/yomi fields."""
    out: dict[int, MoveListEntry] = {}
    prefix = f"ID_CMD_{cid}_"
    suffixes = ("_name", "_command", "_note", "_yomi")

    # Gather all unique MoveListID values that have entries
    ids: set[int] = set()
    for key in archive_map:
        if not key.startswith(prefix):
            continue
        for suf in suffixes:
            if key.endswith(suf):
                middle = key[len(prefix):-len(suf)]
                if middle.isdigit():
                    # Archive key is MoveListID*10; the DA_MovePlayData
                    # value is MoveListID itself.
                    archive_id = int(middle)
                    if archive_id % 10 == 0:
                        ids.add(archive_id // 10)
                break

    for move_id in ids:
        archive_pad = f"{move_id * 10:04d}"
        name = archive_map.get(f"{prefix}{archive_pad}_name", "")
        cmd = archive_map.get(f"{prefix}{archive_pad}_command", "")
        note = archive_map.get(f"{prefix}{archive_pad}_note", "")
        yomi = archive_map.get(f"{prefix}{archive_pad}_yomi", "")
        if not (name or cmd):
            continue
        # A handful of `_command` / `_name` values ship with stray
        # trailing whitespace (`ID_CMD_060_1940` is literally just a
        # space). `to_canonical_input` deliberately doesn't strip — it's
        # a pure converter — so tidy the canonical form here, at the
        # display-entry boundary, before it reaches the UI.
        out[move_id] = MoveListEntry(
            name=name.strip(),
            command_markup=cmd,
            command=to_canonical_input(cmd).strip(),
            # Notes also use {cmd_X} markup AND {effect_X}/{font_X} markup
            # — clean_markup handles both. So "Use {font_blue}{effect_BA}
            # Break Attack{font_def} with {cmd_G}" becomes
            # "Use Break Attack with G".
            note=clean_markup(note),
            yomi=yomi,
        )
    return out


# Convenience: load once + cache the archive map per session
_ARCHIVE_CACHE: dict[str, dict[str, str]] = {}


def get_archive(path: Path | str) -> dict[str, str]:
    """Cached archive load — call as many times as you like."""
    key = str(path)
    if key not in _ARCHIVE_CACHE:
        _ARCHIVE_CACHE[key] = load_game_archive(path)
    return _ARCHIVE_CACHE[key]
