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

type SortKey = "order" | "from" | "input" | "name" | "damage" | "startup" | "onBlock" | "onHit" | "class";
type SortDir = "asc" | "desc";

function pickPrimaryCell(move: MovelistMove, cells: Cell[]): {
  cellIdx: number;
  cell: Cell | null;
  navSlot: number;
} {
  let cellIdx = -1;
  const cs = move.commandSets ?? [];

  for (const cset of cs) {
    const ci = cset.cellIdx;
    if (ci >= 0 && ci < cells.length && cells[ci]?.role === "Attack") {
      cellIdx = ci;
      break;
    }
  }

  const navSlot = cs[0]?.slotIdx ?? -1;
  const cell = cellIdx >= 0 && cellIdx < cells.length ? cells[cellIdx] : null;
  return { cellIdx, cell, navSlot };
}

function MovesTab() {
  const char = Route.useLoaderData();
  const navigate = useNavigate();
  const movelist = char.movelist;
  const cells = useMemo(() => char.khd?.cells ?? [], [char.khd]);

  const [search, setSearch] = useState("");
  const [classFilter, setClassFilter] = useState<string | null>(null);
  const [categoryFilter, setCategoryFilter] = useState<number | null>(null);
  const [conditionFilter, setConditionFilter] = useState<string | null>(null);
  const [sortKey, setSortKey] = useState<SortKey>("order");
  const [sortDir, setSortDir] = useState<SortDir>("asc");

  type EnrichedMove = MovelistMove & {
    cell: Cell | null;       // cell to show for khd-only columns
    cellIdx: number;
    navSlot: number;         // slot the row navigates to
    classKey: string;        // first hit class, for filter/sort
  };

  const enriched = useMemo<EnrichedMove[]>(() => {
    if (!movelist) return [];
    return movelist.moves.map((m) => {
      const { cellIdx, cell, navSlot } = pickPrimaryCell(m, cells);
      const classKey = m.hitClasses[0] ?? cell?.class ?? "";
      return { ...m, cell, cellIdx, navSlot, classKey };
    });
  }, [movelist, cells]);

  const classChoices = useMemo(() => {
    const s = new Set<string>();
    for (const m of enriched) if (m.classKey) s.add(m.classKey);
    return Array.from(s).sort();
  }, [enriched]);

  // Distinct "From" / condition prefixes with counts, for the filter chip strip.
  // "(none)" is the implicit no-condition bucket â€” most moves fall into it.
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
      if (classFilter && m.classKey !== classFilter) return false;
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
      const fa = a.cell;
      const fb = b.cell;
      switch (sortKey) {
        case "order": cmp = a.order - b.order; break;
        case "from": cmp = a.condition.localeCompare(b.condition); break;
        case "input": cmp = a.input.localeCompare(b.input); break;
        case "name": cmp = a.name.localeCompare(b.name); break;
        case "class": cmp = a.classKey.localeCompare(b.classKey); break;
        case "damage":
          cmp = (fa?.damage ?? -1) - (fb?.damage ?? -1); break;
        case "startup":
          cmp = (fa?.activeStart ?? 9999) - (fb?.activeStart ?? 9999); break;
        case "onBlock":
          cmp = (fa?.onBlock ?? -9999) - (fb?.onBlock ?? -9999); break;
        case "onHit":
          cmp = (fa?.onHitStanding ?? -9999) - (fb?.onHitStanding ?? -9999); break;
      }
      return sortDir === "asc" ? cmp : -cmp;
    });
    return arr;
  }, [filtered, sortKey, sortDir]);

  const withParsedCell = useMemo(
    () => enriched.filter((m) => m.cell?.role === "Attack").length,
    [enriched],
  );

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
              title={`${cat.name} â€” ${cat.itemOrders.length} moves. "Main Attacks" re-lists picks from the type-specific tabs.`}
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
        {" â€¢ "}
        <span title="Stats come from parsed khd cells only (no community frame-data join).
        {withParsedCell} moves currently resolve to an Attack-role cell."}>
          {withParsedCell} with parsed-cell stats
        </span>
      </p>

      <table className="moves-table">
        <thead>
          <tr>
            <th
              className={sortClass("from")}
              onClick={() => setSort("from")}
              title="From / stance / state prerequisite for the move â€” e.g. 'During Mist'"
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
              title="Attack class from DA_MoveListTable (authoritative), or parsed attack-cell class if missing"
            >
              Class
            </th>
            <th
              className={sortClass("damage")}
              onClick={() => setSort("damage")}
              title="Base damage from parsed khd cell"
            >
              Dmg
            </th>
            <th
              className={sortClass("startup")}
              onClick={() => setSort("startup")}
              title="Startup / impact frame from parsed khd cell"
            >
              Startup
            </th>
            <th
              className={sortClass("onBlock")}
              onClick={() => setSort("onBlock")}
              title="Block stun from parsed khd cell (raw stun frames)"
            >
              On Block
            </th>
            <th
              className={sortClass("onHit")}
              onClick={() => setSort("onHit")}
              title="Hit stun from parsed khd cell (raw stun frames)"
            >
              On Hit
            </th>
            <th title="Not parsed from khd by this exporter">On CH</th>
            <th title="Number of active hit frames from the matched khd cell (activeEnd âˆ’ activeStart + 1)">
              Active
            </th>
            <th title="Stand-stance reach band of the matched khd cell, in engine units. âˆž = sentinel âˆ’127 (no range gate).">
              Range
            </th>
          </tr>
        </thead>
        <tbody>
          {sorted.map((m) => {
            const c = m.cell;
            const range = !c ? "â€”"
              : (c.rangeStandMin === -127 && c.rangeStandMax === -127)
                ? "âˆž"
                : `${c.rangeStandMin}..${c.rangeStandMax}`;
            const hasAttack = c?.role === "Attack";
            return (
              <tr
                key={m.order}
                onClick={() => {
                  if (m.navSlot >= 0) {
                    navigate({
                      to: "/c/$cid/moves/$slot",
                      params: { cid: char.cid, slot: String(m.navSlot) },
                      search: { move: m.moveId, order: m.order },
                    });
                  } else if (m.cellIdx >= 0) {
                    navigate({
                      to: "/c/$cid/cells/$idx",
                      params: { cid: char.cid, idx: String(m.cellIdx) },
                    });
                  }
                }}
              >
                <td className="col-from">
                  {m.condition || <span style={{ opacity: 0.35 }}>â€”</span>}
                </td>
                <td className="col-input mono">
                  {m.input || <span className="muted">â€”</span>}
                  {m.hasInputAlternatives && m.inputVariants.length > 0 && (
                    <span
                      className="variant-mark"
                      title={`Bandai's data groups several inputs under this one entry. ${m.inputVariants.length} candidate alternate cell(s) found via dispatcher-sibling lookup. open the move to see them.`}
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
                    // Authoritative DA_MoveListTable class.
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
                          title="Throw-style input (+G)."
                        >
                          Throw
                        </span>
                      )}
                    </>
                  ) : m.isMovementOnly ? (
                    <span
                      className="badge badge-movement"
                      title="Movement / stance entry â€” pure-direction input, no hit cell"
                    >
                      Movement
                    </span>
                  ) : m.isThrowInput ? (
                    <span
                      className="badge badge-throw"
                      title="Throw-style input (+G)."
                    >
                      Throw
                    </span>
                  ) : (
                    <span className="muted">{c?.role ?? "â€”"}</span>
                  )}
                </td>
                <td className="num mono">{hasAttack ? c!.damage : "â€”"}</td>
                <td className="num">{hasAttack ? `i${c!.activeStart}` : "â€”"}</td>
                <td className="num">{hasAttack ? c!.onBlock : "â€”"}</td>
                <td className="num">{hasAttack ? c!.onHitStanding : "â€”"}</td>
                <td className="num">â€”</td>
                <td className="num muted">{hasAttack ? `${c!.activeFrames}f` : "â€”"}</td>
                <td className="num mono muted" style={{ fontSize: 12 }}>{range}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </>
  );
}
