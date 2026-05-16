import { createFileRoute, useNavigate } from "@tanstack/react-router";
import { useMemo, useState } from "react";
import { AttackClassBadge, AttackModifierBadges, EffectTagBadges, MoveClassBadge } from "../components/AttackClassBadge";
import type { CharData, Cell, MovelistMove } from "../data/types";

export const Route = createFileRoute("/c/$cid/moves")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: MovesTab,
});

type SortKey = "order" | "from" | "input" | "name" | "damage" | "startup" | "active" | "class" | "onBlock" | "onHit";
type SortDir = "asc" | "desc";

function MovesTab() {
  const char = Route.useLoaderData();
  const navigate = useNavigate();
  const movelist = char.movelist;
  const cells = useMemo(
    () => char.khd?.cells ?? [],
    [char.khd],
  );
  const slots = useMemo(
    () => char.khd?.slots ?? [],
    [char.khd],
  );

  const [search, setSearch] = useState("");
  const [classFilter, setClassFilter] = useState<string | null>(null);
  const [categoryFilter, setCategoryFilter] = useState<number | null>(null);
  const [conditionFilter, setConditionFilter] = useState<string | null>(null);
  const [sortKey, setSortKey] = useState<SortKey>("order");
  const [sortDir, setSortDir] = useState<SortDir>("asc");

  // Resolve attack data per movelist entry. The Python side pre-resolved
  // `mainIndex` to (cellIdx, slotIdx) using a hybrid heuristic — see
  // `export_webui_data._resolve_main_index` for why this can't be done
  // with a single lookup table.
  type EnrichedMove = MovelistMove & {
    primaryCell: Cell | null;
    primaryCellIdx: number;
    primarySlot: number;
  };
  const enriched = useMemo<EnrichedMove[]>(() => {
    if (!movelist) return [];
    return movelist.moves.map((m) => {
      const cs = m.commandSets[0];
      const cellIdx = cs?.cellIdx ?? -1;
      const slotIdx = cs?.slotIdx ?? -1;
      const primaryCell = (cellIdx >= 0 && cellIdx < cells.length) ? cells[cellIdx] : null;
      return { ...m, primaryCell, primaryCellIdx: cellIdx, primarySlot: slotIdx };
    });
  }, [movelist, cells]);

  const classChoices = useMemo(() => {
    const s = new Set<string>();
    for (const m of enriched) {
      if (m.primaryCell?.role === "Attack") s.add(m.primaryCell.class);
    }
    return Array.from(s).sort();
  }, [enriched]);

  // Distinct "From" / condition prefixes with their counts, for the filter
  // chip strip. "(none)" is the implicit no-condition bucket — most moves
  // fall into it.
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
      if (categoryFilter !== null && m.category !== categoryFilter) return false;
      if (classFilter && m.primaryCell?.class !== classFilter) return false;
      if (conditionFilter !== null) {
        if (conditionFilter === "(none)") {
          if (m.condition) return false;
        } else if (m.condition !== conditionFilter) {
          return false;
        }
      }
      if (q) {
        const blob = `${m.name} ${m.condition} ${m.input} ${m.note}`.toLowerCase();
        if (!blob.includes(q)) return false;
      }
      return true;
    });
  }, [enriched, search, classFilter, categoryFilter, conditionFilter]);

  const sorted = useMemo(() => {
    const arr = [...filtered];
    arr.sort((a, b) => {
      let cmp = 0;
      const ca = a.primaryCell;
      const cb = b.primaryCell;
      switch (sortKey) {
        case "order":   cmp = a.order - b.order; break;
        case "from":    cmp = a.condition.localeCompare(b.condition); break;
        case "input":   cmp = a.input.localeCompare(b.input); break;
        case "name":    cmp = a.name.localeCompare(b.name); break;
        case "damage":  cmp = (ca?.damage ?? 0) - (cb?.damage ?? 0); break;
        case "startup": cmp = (ca?.activeStart ?? 0) - (cb?.activeStart ?? 0); break;
        case "active":  cmp = (ca?.activeFrames ?? 0) - (cb?.activeFrames ?? 0); break;
        case "class":   cmp = (ca?.class ?? "").localeCompare(cb?.class ?? ""); break;
        case "onBlock": cmp = (ca?.onBlock ?? 0) - (cb?.onBlock ?? 0); break;
        case "onHit":   cmp = (ca?.onHitStanding ?? 0) - (cb?.onHitStanding ?? 0); break;
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
      <div className="filters">
        <input
          type="search"
          placeholder="Search by name or input (e.g. 'Heaven', '236A')"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          style={{
            flex: "1 1 240px",
            minWidth: 240,
            padding: "6px 10px",
            background: "#181b22",
            border: "1px solid #2a2e38",
            color: "#e6e8eb",
            borderRadius: 6,
            fontFamily: "inherit",
            fontSize: 13,
          }}
        />
      </div>

      <div className="filters" style={{ marginTop: 6 }}>
        <span className="filter-label">Category:</span>
        <button
          className={`chip ${categoryFilter === null ? "active" : ""}`}
          onClick={() => setCategoryFilter(null)}
        >
          all
        </button>
        {movelist.categories.map((cat) =>
          cat.itemOrders.length > 0 ? (
            <button
              key={cat.index}
              className={`chip ${categoryFilter === cat.index ? "active" : ""}`}
              onClick={() => setCategoryFilter(categoryFilter === cat.index ? null : cat.index)}
              title={`${cat.name} — ${cat.itemOrders.length} moves. "Main Attacks" re-lists picks from the type-specific tabs.`}
            >
              {cat.name}
              <span className="muted" style={{ marginLeft: 4, fontSize: 11 }}>
                ({cat.itemOrders.length})
              </span>
            </button>
          ) : null,
        )}
      </div>

      <div className="filters" style={{ marginTop: 4 }}>
        <span className="filter-label">Class:</span>
        <button
          className={`chip ${classFilter === null ? "active" : ""}`}
          onClick={() => setClassFilter(null)}
        >
          all
        </button>
        {classChoices.map((c) => (
          <button
            key={c}
            className={`chip ${classFilter === c ? "active" : ""}`}
            onClick={() => setClassFilter(classFilter === c ? null : c)}
          >
            {c}
          </button>
        ))}
      </div>

      {conditionChoices.length > 1 && (
        <div className="filters" style={{ marginTop: 4 }}>
          <span className="filter-label">From:</span>
          <button
            className={`chip ${conditionFilter === null ? "active" : ""}`}
            onClick={() => setConditionFilter(null)}
          >
            all
          </button>
          {conditionChoices.slice(0, 16).map((c) => (
            <button
              key={c.condition}
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

      <p className="muted" style={{ fontSize: 13, marginTop: 8 }}>
        {sorted.length} {sorted.length === 1 ? "move" : "moves"}
        {sorted.length !== enriched.length && <> of {enriched.length}</>}
      </p>

      <table className="moves-table">
        <thead>
          <tr>
            <th
              className={sortClass("from")}
              onClick={() => setSort("from")}
              title="Stance / state prerequisite for the move — e.g. 'During Mist'"
            >
              From
            </th>
            <th
              className={sortClass("input")}
              onClick={() => setSort("input")}
              title="Canonical input notation. [X] = hold; (X) = press+hold same direction; X|Y = alternatives"
            >
              Input
            </th>
            <th
              className={sortClass("name")}
              onClick={() => setSort("name")}
            >
              Name
            </th>
            <th
              className={sortClass("class")}
              onClick={() => setSort("class")}
              title="Attack class from DA_MoveListTable (the game's own per-hit AttributeTag — authoritative). 'X ×N' = first-hit class of an N-hit move. Falls back to the cell-flag-derived class only when no movelist-table data exists. Pills: BA = Break Attack, GI⊘ = GI-immune, GB = Guard Bypass."
            >
              Class
            </th>
            <th
              className={sortClass("damage")}
              onClick={() => setSort("damage")}
              title="Base damage from the cell. Runtime charge/SC multipliers are not captured."
            >
              Dmg
            </th>
            <th
              className={sortClass("startup")}
              onClick={() => setSort("startup")}
              title="Frame the active window starts on (cell.wI16MasterWindowStart)"
            >
              Startup
            </th>
            <th
              className={sortClass("active")}
              onClick={() => setSort("active")}
              title="Number of active hit frames (activeEnd − activeStart + 1)"
            >
              Active
            </th>
            <th
              className={sortClass("onHit")}
              onClick={() => setSort("onHit")}
              title="Hit-stun frames on standing hit (cell.wI16HitstunStandingNormal). Raw stun, not frame advantage."
            >
              Hit stun
            </th>
            <th
              className={sortClass("onBlock")}
              onClick={() => setSort("onBlock")}
              title="Block-stun frames (cell.wI16BlockstunFrames). Raw stun, not frame advantage."
            >
              Blk stun
            </th>
            <th title="Stand-stance reach band, in engine units. ∞ = sentinel −127 (no range gate).">
              Range
            </th>
          </tr>
        </thead>
        <tbody>
          {sorted.map((m) => {
            const c = m.primaryCell;
            const range = !c ? "—"
              : (c.rangeStandMin === -127 && c.rangeStandMax === -127)
                ? "∞"
                : `${c.rangeStandMin}..${c.rangeStandMax}`;
            return (
              <tr
                key={m.order}
                onClick={() => {
                  // Prefer navigating to a slot if we know one; otherwise
                  // fall through to the cell detail page so the user
                  // still lands somewhere useful. The `move` search param
                  // disambiguates slot collisions — multiple movelist
                  // entries can share a slotIdx (e.g. Mitsurugi slot 257
                  // = both Quivering Strike B+G AND Geyser Cannon A+B);
                  // passing moveId lets the detail page pick the right
                  // movelist entry.
                  if (m.primarySlot >= 0) {
                    navigate({
                      to: "/c/$cid/moves/$slot",
                      params: { cid: char.cid, slot: String(m.primarySlot) },
                      search: { move: m.moveId, order: m.order },
                    });
                  } else if (m.primaryCellIdx >= 0) {
                    navigate({
                      to: "/c/$cid/cells/$idx",
                      params: { cid: char.cid, idx: String(m.primaryCellIdx) },
                    });
                  }
                }}
              >
                <td className="col-from">
                  {m.condition || <span style={{ opacity: 0.35 }}>—</span>}
                </td>
                <td className="col-input mono">
                  {m.input || <span className="muted">—</span>}
                  {m.hasInputAlternatives && m.inputVariants.length > 0 && (
                    <span
                      className="variant-mark"
                      title={`Bandai's data groups several inputs under this one entry. ${m.inputVariants.length} candidate alternate cell(s) found via dispatcher-sibling lookup — open the move to see them.`}
                    >
                      +{m.inputVariants.length}
                    </span>
                  )}
                </td>
                <td className="col-name">
                  <div className="move-name">
                    {m.name}
                    {m.effectTags.length > 0 && (
                      <span style={{ marginLeft: 6 }}>
                        <EffectTagBadges tags={m.effectTags} />
                      </span>
                    )}
                  </div>
                  {m.note && <div className="move-note">{m.note}</div>}
                  {m.lethalHitCondition && (
                    <div className="move-lh" title="Lethal Hit trigger condition">
                      LH: {m.lethalHitCondition}
                    </div>
                  )}
                </td>
                <td className="col-class">
                  {m.hitClasses.length > 0 ? (
                    // Authoritative DA_MoveListTable class. The
                    // cell-flag-derived class (AttackClassBadge) is only
                    // ~57% accurate vs this — see MoveClassBadge note.
                    <>
                      <MoveClassBadge hitClasses={m.hitClasses} />
                      {c?.role === "Attack" && <AttackModifierBadges cell={c} />}
                    </>
                  ) : c?.role === "Attack" ? (
                    <>
                      <AttackClassBadge value={c.class} />
                      <AttackModifierBadges cell={c} />
                      {m.isThrowInput && (
                        <span
                          className="badge badge-throw-hint"
                          style={{ marginLeft: 4 }}
                          title="Throw-style input (+G). The cell shown is the strike-phase / whiff cell — the actual throw cinematic damage lives downstream of an engine-mediated state transition we can't statically resolve."
                        >
                          Throw
                        </span>
                      )}
                    </>
                  ) : m.isMovementOnly ? (
                    <span
                      className="badge badge-movement"
                      title="Movement / stance entry — pure-direction input, no hit cell"
                    >
                      Movement
                    </span>
                  ) : m.isThrowInput ? (
                    <span
                      className="badge badge-throw"
                      title="Throw-style input (+G) with no resolved cell — the throw cinematic damage isn't visible in static data."
                    >
                      Throw
                    </span>
                  ) : (
                    <span className="muted">{c?.role ?? "—"}</span>
                  )}
                </td>
                <td className="num">{c?.damage ?? "—"}</td>
                <td className="num">{c?.role === "Attack" ? `i${c.activeStart}` : "—"}</td>
                <td className="num muted">{c?.role === "Attack" ? `${c.activeFrames}f` : "—"}</td>
                <td className="num">{c?.onHitStanding ?? "—"}</td>
                <td className="num">{c?.onBlock ?? "—"}</td>
                <td className="num mono muted" style={{ fontSize: 12 }}>{range}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </>
  );
}
