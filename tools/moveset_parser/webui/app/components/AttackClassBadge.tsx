import type { AttackClass, Cell, CellRole } from "../data/types";

const CLASS_TO_BADGE: Record<string, string> = {
  Mid: "badge-mid",
  High: "badge-high",
  Low: "badge-low",
  Unblockable: "badge-unblockable",
  Throw: "badge-throw",
  Inactive: "badge-inactive",
};

export function AttackClassBadge({ value }: { value: AttackClass }) {
  const cls = CLASS_TO_BADGE[value] ?? "badge-other";
  return <span className={`badge ${cls}`}>{value}</span>;
}

// Authoritative move-class badge sourced from DA_MoveListTable's
// `AttributeTag` (per-hit class sequence). Use this in preference to the
// cell-flag-derived `AttackClassBadge` for movelist moves: the cell's
// wU16AttackFlags low bits do NOT encode attack height — verified
// against the DataTable, the bits are ~98%/79% base-rate regardless of
// whether the move is High/Mid/Low, so the cell-derived class is only
// ~57% accurate. The DataTable AttributeTag is the game's own data.
//
// `hitClasses` is the full sequence (e.g. ["Low","Mid"]); the badge
// shows the FIRST hit's class (how the move must initially be blocked)
// with a "×N" suffix when the move is multi-hit.
const SHORT_CLASS: Record<string, string> = {
  High: "badge-high", Mid: "badge-mid", Low: "badge-low",
  "Special Mid": "badge-mid", "Special Low": "badge-low",
};

export function MoveClassBadge({ hitClasses }: { hitClasses: string[] }) {
  if (!hitClasses || hitClasses.length === 0) return null;
  const first = hitClasses[0];
  const cls = SHORT_CLASS[first] ?? "badge-other";
  const multi = hitClasses.length > 1;
  return (
    <span
      className={`badge ${cls}`}
      title={multi
        ? `Per-hit class sequence (DA_MoveListTable): ${hitClasses.join(" · ")}`
        : `Attack class (DA_MoveListTable AttributeTag)`}
    >
      {first}{multi ? ` ×${hitClasses.length}` : ""}
    </span>
  );
}

// Compact modifier-flag pills (BA / GIimm / GB) that go next to the
// class badge. Each is sourced from a verified bit in ELuxBattleAttackFlags
// (cell+0x32). They are orthogonal to the class — e.g. a Mid attack can
// also be a Break Attack, and an Unblockable usually has GIimm set.
export function AttackModifierBadges({ cell }: { cell: Pick<Cell, "isBreakAttack" | "isGiImmune" | "isGuardBypass" | "class"> }) {
  // Don't double-up: if the class is already "Unblockable", GIimm is
  // implied by convention — skip the pill to reduce noise.
  const showGiImm = cell.isGiImmune && cell.class !== "Unblockable" && !cell.isBreakAttack;
  return (
    <>
      {cell.isBreakAttack && (
        <span
          className="badge badge-ba"
          title="Break Attack — still blockable, but block reaction staggers. Cannot be Guard Impacted."
          style={{ marginLeft: 4 }}
        >
          BA
        </span>
      )}
      {showGiImm && (
        <span
          className="badge badge-giimm"
          title="GI-immune — defender cannot Guard Impact this attack."
          style={{ marginLeft: 4 }}
        >
          GI⊘
        </span>
      )}
      {cell.isGuardBypass && (
        <span
          className="badge badge-gbypass"
          title="Guard Bypass — bypasses block when the defender is in guard-broken substate."
          style={{ marginLeft: 4 }}
        >
          GB
        </span>
      )}
    </>
  );
}

// Move-property pills sourced from DA_MoveListTable's `EffectTag` — the
// AUTHORITATIVE move-property set the in-game movelist shows as icons.
// Unlike AttackModifierBadges (derived from cell flags) these come
// straight from the game's own movelist table.
const EFFECT_TAG_CSS: Record<string, string> = {
  LH:  "badge-eff-lh",   // Lethal Hit
  BA:  "badge-eff-ba",   // Break Attack
  GI:  "badge-eff-gi",   // Guard Impact
  UA:  "badge-eff-ua",   // Unblockable Attack
  RE:  "badge-eff-re",   // Reversal Edge
  TH:  "badge-eff-th",   // Throw
  SC:  "badge-eff-sc",   // Soul Charge
  SS:  "badge-eff-ss",   // Stance Shift
  SGF: "badge-eff-sg",   // Soul Gauge: Full
  SGH: "badge-eff-sg",   // Soul Gauge: Half
  SGQ: "badge-eff-sg",   // Soul Gauge: Quarter
};

export function EffectTagBadges(
  { tags }: { tags: { code: string; label: string }[] },
) {
  if (!tags || tags.length === 0) return null;
  return (
    <>
      {tags.map((t) => (
        <span
          key={t.code}
          className={`badge badge-eff ${EFFECT_TAG_CSS[t.code] ?? "badge-other"}`}
          title={`${t.label} (DA_MoveListTable EffectTag "${t.code}")`}
        >
          {t.code}
        </span>
      ))}
    </>
  );
}

export function RevengeAttackBadge() {
  return (
    <span
      className="badge badge-eff badge-eff-rv"
      title="Revenge attack - derived from localized movelist note text, not DA_MoveListTable EffectTag"
    >
      RV
    </span>
  );
}

export function CellRoleBadge({ value }: { value: CellRole }) {
  const map: Record<CellRole, string> = {
    Attack: "badge-mid",
    Header: "badge-header",
    NonDamaging: "badge-nondamaging",
    Sentinel: "badge-sentinel",
  };
  return <span className={`badge ${map[value] ?? "badge-other"}`}>{value}</span>;
}
