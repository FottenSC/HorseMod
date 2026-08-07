import { createFileRoute } from "@tanstack/react-router";
import { useMemo, useState } from "react";
import { EffectTagBadges, RevengeAttackBadge } from "../components/AttackClassBadge";
import type { CharData, MovelistMove } from "../data/types";

export const Route = createFileRoute("/c/$cid/moves")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: MovesTab,
});

type SortKey = "order" | "stance" | "command" | "damage" | "impact" | "onBlock" | "onHit" | "class";
type SortDir = "asc" | "desc";
type PresetFilter = "punishable" | "fast" | "plus" | "launch" | "gi" | "revenge" | null;

function parseFrameValue(value: string | number | null | undefined): number | null {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (!value) return null;
  const match = String(value).match(/[+-]?\d+/);
  return match ? Number(match[0]) : null;
}

function displayFrame(value: string | number | null | undefined): string {
  if (value === null || value === undefined || value === "") return "-";
  if (typeof value === "number") return value > 0 ? `+${value}` : String(value);
  return String(value);
}

function frameClass(value: string | number | null | undefined): string {
  const text = String(value ?? "").toUpperCase();
  if (text === "KND" || text === "LNC" || text === "STN") return "frame-pill special";
  const n = parseFrameValue(value);
  if (n === null) return "frame-pill empty";
  if (n < 0) return "frame-pill neg-bg";
  if (n > 0) return "frame-pill pos-bg";
  return "frame-pill zero-bg";
}

function getImpact(move: MovelistMove): number | null {
  return move.metrics.startup;
}

function getDamage(move: MovelistMove): number | null {
  return move.metrics.damage.length > 0
    ? move.metrics.damage.reduce((sum, n) => sum + n, 0)
    : null;
}

function getBlock(move: MovelistMove): string | number | null {
  return move.evidence.block.status !== "unknown" ? move.metrics.block : null;
}

function getHit(move: MovelistMove): string | number | null {
  return move.evidence.hit.status !== "unknown" ? move.metrics.hit : null;
}

function getCounterHit(move: MovelistMove): string | number | null {
  return move.evidence.counterHit.status === "native-confirmed" ? move.metrics.counterHit : null;
}

function getGuardBurst(move: MovelistMove): string | number | null {
  return move.evidence.guardBurst.status === "native-confirmed" ? move.metrics.guardBurst : null;
}

const DIR_SYMBOLS: Record<string, string> = {
  "1": "↙",
  "2": "↓",
  "3": "↘",
  "4": "←",
  "6": "→",
  "7": "↖",
  "8": "↑",
  "9": "↗",
};

function CommandChips({ input }: { input: string }) {
  if (!input) return <span className="muted">-</span>;
  return (
    <span className="command-chips" aria-label={input}>
      {Array.from(input).map((ch, idx) => {
        if (DIR_SYMBOLS[ch]) return <span key={idx} className="cmd-dir">{DIR_SYMBOLS[ch]}</span>;
        if (/[ABKG]/.test(ch)) return <span key={idx} className="cmd-button">{ch}</span>;
        if (ch === "+") return <span key={idx} className="cmd-plus">+</span>;
        if (ch === ".") return <span key={idx} className="cmd-dot">.</span>;
        return <span key={idx}>{ch}</span>;
      })}
    </span>
  );
}

function HitLevelDots({ move }: { move: MovelistMove }) {
  const levels = move.hitClasses;
  if (levels.length === 0) return <span className="muted">-</span>;
  return (
    <span className="hit-levels" title={levels.join(" / ")}>
      {levels.slice(0, 4).map((level, idx) => {
        const short = level.includes("High") ? "H"
          : level.includes("Low") ? "L"
            : level.includes("Mid") ? "M"
              : level.includes("Throw") ? "T"
                : level.slice(0, 1).toUpperCase();
        const cls = short === "H" ? "high" : short === "L" ? "low" : short === "T" ? "throw" : "mid";
        return <span key={`${level}-${idx}`} className={`level-dot ${cls}`}>{short}</span>;
      })}
      {levels.length > 4 && <span className="level-more">+{levels.length - 4}</span>}
    </span>
  );
}

function FramePill({ value }: { value: string | number | null | undefined }) {
  const display = displayFrame(value);
  if (display === "-") return <span className="muted">-</span>;
  return <span className={frameClass(value)}>{display}</span>;
}

function MovesTab() {
  const char = Route.useLoaderData();
  const movelist = char.movelist;

  const [search, setSearch] = useState("");
  const [presetFilter, setPresetFilter] = useState<PresetFilter>(null);
  const [classFilter, setClassFilter] = useState<string | null>(null);
  const [conditionFilter, setConditionFilter] = useState<string | null>(null);
  const [sortKey, setSortKey] = useState<SortKey>("order");
  const [sortDir, setSortDir] = useState<SortDir>("asc");

  type EnrichedMove = MovelistMove & {
    classKey: string;        // first hit class, for filter/sort
    impact: number | null;
    damageTotal: number | null;
    blockValue: string | number | null;
    hitValue: string | number | null;
    counterHitValue: string | number | null;
    guardBurstValue: string | number | null;
  };

  const enriched = useMemo<EnrichedMove[]>(() => {
    if (!movelist) return [];
    return movelist.moves.map((m) => {
      const classKey = m.hitClasses[0] ?? "";
      return {
        ...m,
        classKey,
        impact: getImpact(m),
        damageTotal: getDamage(m),
        blockValue: getBlock(m),
        hitValue: getHit(m),
        counterHitValue: getCounterHit(m),
        guardBurstValue: getGuardBurst(m),
      };
    });
  }, [movelist]);

  const classChoices = useMemo(() => {
    const s = new Set<string>();
    for (const m of enriched) if (m.classKey) s.add(m.classKey);
    return Array.from(s).sort();
  }, [enriched]);

  // Distinct "From" / condition prefixes with counts, for the filter chip strip.
  // "(none)" is the implicit no-condition bucket - most moves fall into it.
  const conditionChoices = useMemo(() => {
    const counts = new Map<string, number>();
    for (const m of enriched) {
      const c = m.condition || "(none)";
      counts.set(c, (counts.get(c) ?? 0) + 1);
    }
    return Array.from(counts.entries())
      .map(([condition, count]) => ({ condition, count }))
      .sort((a, b) => b.count - a.count);
  }, [enriched]);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return enriched.filter((m) => {
      if (classFilter && m.classKey !== classFilter) return false;
      if (presetFilter === "punishable" && (
        m.isThrowInput
        ||
        m.evidence.block.status !== "native-confirmed"
        || (parseFrameValue(m.blockValue) ?? 999) > -10
      )) return false;
      if (presetFilter === "fast" && ((m.impact ?? 999) > 10)) return false;
      if (presetFilter === "plus" && (
        m.isThrowInput
        ||
        m.evidence.block.status !== "native-confirmed"
        || (parseFrameValue(m.blockValue) ?? -999) <= 0
      )) return false;
      if (presetFilter === "launch" && !(
        (m.evidence.hit.status === "native-confirmed" && /LNC|KND/i.test(String(m.hitValue ?? "")))
        || (m.evidence.counterHit.status === "native-confirmed" && /LNC|KND/i.test(String(m.counterHitValue ?? "")))
      )) return false;
      if (presetFilter === "gi" && !m.effectTags.some((tag) => tag.code === "GI")) return false;
      if (presetFilter === "revenge" && !m.isRevengeAttack) return false;
      if (conditionFilter !== null) {
        if (conditionFilter === "(none)") {
          if (m.condition) return false;
        } else if (m.condition !== conditionFilter) {
          return false;
        }
      }
      if (q) {
        const blob = `${m.name} ${m.condition} ${m.input} ${m.fullCommand} ${m.note} ${m.mainTip}`.toLowerCase();
        if (!blob.includes(q)) return false;
      }
      return true;
    });
  }, [enriched, search, presetFilter, classFilter, conditionFilter]);

  const sorted = useMemo(() => {
    const arr = [...filtered];
    arr.sort((a, b) => {
      let cmp = 0;
      switch (sortKey) {
        case "order": cmp = a.order - b.order; break;
        case "stance": cmp = a.condition.localeCompare(b.condition); break;
        case "command": cmp = a.input.localeCompare(b.input); break;
        case "class": cmp = a.classKey.localeCompare(b.classKey); break;
        case "damage":
          cmp = (a.damageTotal ?? -1) - (b.damageTotal ?? -1); break;
        case "impact":
          cmp = (a.impact ?? 9999) - (b.impact ?? 9999); break;
        case "onBlock":
          cmp = (
            a.evidence.block.status === "native-confirmed" ? parseFrameValue(a.blockValue) ?? -9999 : -9999
          ) - (
            b.evidence.block.status === "native-confirmed" ? parseFrameValue(b.blockValue) ?? -9999 : -9999
          ); break;
        case "onHit":
          cmp = (
            a.evidence.hit.status === "native-confirmed" ? parseFrameValue(a.hitValue) ?? -9999 : -9999
          ) - (
            b.evidence.hit.status === "native-confirmed" ? parseFrameValue(b.hitValue) ?? -9999 : -9999
          ); break;
      }
      return sortDir === "asc" ? cmp : -cmp;
    });
    return arr;
  }, [filtered, sortKey, sortDir]);

  const setSort = (k: SortKey) => {
    if (sortKey === k) setSortDir(sortDir === "asc" ? "desc" : "asc");
    else {
      setSortKey(k);
      setSortDir(k === "damage" || k === "onHit" ? "desc" : "asc");
    }
  };
  const sortClass = (k: SortKey) =>
    sortKey === k ? (sortDir === "asc" ? "sort-asc" : "sort-desc") : "";

  if (!char.khd) {
    return <div className="notice">No KHD data for this character.</div>;
  }
  if (!movelist) {
    return (
      <div className="notice">
        Canonical movelist data not available for this character. (Requires
        the UE4 DataAsset + localization dump at the standard SC6 paths.)
      </div>
    );
  }

  return (
    <>
      <section className="moves-toolbar" aria-label="Move list controls">
        <input
          type="search"
          placeholder="Quick search"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          className="moves-search"
        />
        <div className="moves-toolbar-meta">
          <span>{sorted.length} moves</span>
          {sorted.length !== enriched.length && <span>{enriched.length} total</span>}
        </div>
      </section>

      <div className="preset-filters" aria-label="Quick filters">
        {([
          ["punishable", "Punishable"],
          ["fast", "i10+"],
          ["plus", "Plus on block"],
          ["launch", "Launch on hit"],
          ["gi", "is a GI"],
          ["revenge", "Revenge"],
        ] as const).map(([value, label]) => (
          <button
            key={value}
            type="button"
            className={`preset-chip ${presetFilter === value ? "active" : ""}`}
            onClick={() => setPresetFilter(presetFilter === value ? null : value)}
          >
            {label}
          </button>
        ))}
      </div>

      <details className="advanced-move-filters">
        <summary>Advanced filters</summary>
        <div className="filters advanced-filter-row">
          <span className="filter-label">Class:</span>
          <button
            type="button"
            className={`chip ${classFilter === null ? "active" : ""}`}
            onClick={() => setClassFilter(null)}
          >
            all
          </button>
          {classChoices.map((c) => (
            <button
              key={c}
              type="button"
              className={`chip ${classFilter === c ? "active" : ""}`}
              onClick={() => setClassFilter(classFilter === c ? null : c)}
            >
              {c}
            </button>
          ))}
        </div>

        {conditionChoices.length > 1 && (
          <div className="filters advanced-filter-row">
            <span className="filter-label">Stance:</span>
            <button
              type="button"
              className={`chip ${conditionFilter === null ? "active" : ""}`}
              onClick={() => setConditionFilter(null)}
            >
              all
            </button>
            {conditionChoices.slice(0, 16).map((c) => (
              <button
                key={c.condition}
                type="button"
                className={`chip ${conditionFilter === c.condition ? "active" : ""}`}
                onClick={() => setConditionFilter(conditionFilter === c.condition ? null : c.condition)}
                title={`${c.count} moves`}
              >
                {c.condition === "(none)" ? "neutral" : c.condition}
                <span className="muted" style={{ marginLeft: 4, fontSize: 11 }}>
                  ({c.count})
                </span>
              </button>
            ))}
          </div>
        )}
      </details>

      <div className="moves-table-wrap">
      <table className="moves-table frame-table">
        <thead>
          <tr>
            <th className="col-details">MoveVM</th>
            <th
              className={sortClass("stance")}
              onClick={() => setSort("stance")}
            >
              Stance
            </th>
            <th
              className={sortClass("command")}
              onClick={() => setSort("command")}
            >
              Command
            </th>
            <th
              className={sortClass("class")}
              onClick={() => setSort("class")}
            >
              Hit Level
            </th>
            <th
              className={sortClass("impact")}
              onClick={() => setSort("impact")}
            >
              Impact
            </th>
            <th
              className={sortClass("damage")}
              onClick={() => setSort("damage")}
            >
              Damage
            </th>
            <th
              className={sortClass("onBlock")}
              onClick={() => setSort("onBlock")}
            >
              Block
            </th>
            <th
              className={sortClass("onHit")}
              onClick={() => setSort("onHit")}
            >
              Hit
            </th>
            <th>CH</th>
            <th>GB</th>
            <th>Properties</th>
            <th>Notes</th>
          </tr>
        </thead>
        <tbody>
          {sorted.map((m) => {
            const notes = [m.note, m.lethalHitCondition ? `LH: ${m.lethalHitCondition}` : "", m.mainTip]
              .filter(Boolean)
              .join(" · ");
            return (
              <tr key={m.order}>
                <td className="col-details">
                  {m.nativeLink.definitions
                    .map((definition) => definition.mainDefinitionId || definition.fallbackDefinitionId)
                    .filter(Boolean)
                    .join(", ") || <span className="muted">-</span>}
                </td>
                <td className="col-stance">
                  {m.condition || <span style={{ opacity: 0.35 }}>-</span>}
                </td>
                <td className="col-command">
                  <CommandChips input={m.input} />
                  {m.hasInputAlternatives && m.inputVariants.length > 0 && (
                    <span
                      className="variant-mark"
                      title={`${m.inputVariants.length} alternate input route(s). Open details to compare variants.`}
                    >
                      +{m.inputVariants.length}
                    </span>
                  )}
                </td>
                <td className="col-level">
                  <HitLevelDots move={m} />
                </td>
                <td className="num mono">{m.impact ?? "-"}</td>
                <td className="num mono">{m.damageTotal ?? "-"}</td>
                <td className="num"><FramePill value={m.blockValue} /></td>
                <td className="num"><FramePill value={m.hitValue} /></td>
                <td className="num"><FramePill value={m.counterHitValue} /></td>
                <td className="num"><FramePill value={m.guardBurstValue} /></td>
                <td className="col-properties">
                  {m.effectTags.length > 0 ? <EffectTagBadges tags={m.effectTags} /> : null}
                  {m.isRevengeAttack && <RevengeAttackBadge />}
                  {m.isThrowInput && <span className="badge badge-eff badge-eff-th">TH</span>}
                  {m.effectTags.length === 0 && !m.isThrowInput && !m.isRevengeAttack && <span className="muted">-</span>}
                </td>
                <td className="col-notes" title={m.name || undefined}>
                  {notes || <span className="muted">-</span>}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
      </div>
    </>
  );
}
