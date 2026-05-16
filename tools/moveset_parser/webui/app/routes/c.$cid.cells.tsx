import { createFileRoute, useNavigate } from "@tanstack/react-router";
import { useMemo, useState } from "react";
import { AttackClassBadge, AttackModifierBadges } from "../components/AttackClassBadge";
import type { CharData, Cell } from "../data/types";

export const Route = createFileRoute("/c/$cid/cells")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: MovesTab,
});

type SortKey = "idx" | "class" | "damage" | "activeStart" | "onBlock" | "onHitStanding" | "rangeStandMax";
type SortDir = "asc" | "desc";

function MovesTab() {
  const char = Route.useLoaderData();
  const navigate = useNavigate();
  // Memo the cells array so the `?? []` fallback doesn't create a fresh
  // identity each render (which would bust downstream filter/sort memos).
  const cells = useMemo(
    () => char.khd?.cells ?? [],
    [char.khd],
  );

  // Default: show only Attack-role cells (real moves).
  const [showHeaders, setShowHeaders] = useState(false);
  const [classFilter, setClassFilter] = useState<string | null>(null);
  const [minDamage, setMinDamage] = useState(0);
  const [sortKey, setSortKey] = useState<SortKey>("idx");
  const [sortDir, setSortDir] = useState<SortDir>("asc");

  // Class options actually present in this char
  const presentClasses = useMemo(() => {
    const s = new Set<string>();
    for (const c of cells) if (c.role === "Attack") s.add(c.class);
    return Array.from(s).sort();
  }, [cells]);

  const filtered = useMemo(() => {
    return cells.filter((c) => {
      if (!showHeaders && c.role !== "Attack") return false;
      if (classFilter && c.class !== classFilter) return false;
      if (minDamage > 0 && c.damage < minDamage) return false;
      return true;
    });
  }, [cells, showHeaders, classFilter, minDamage]);

  const sorted = useMemo(() => {
    const arr = [...filtered];
    arr.sort((a, b) => {
      const av = a[sortKey];
      const bv = b[sortKey];
      const cmp = typeof av === "string" && typeof bv === "string"
        ? av.localeCompare(bv)
        : (av as number) - (bv as number);
      return sortDir === "asc" ? cmp : -cmp;
    });
    return arr;
  }, [filtered, sortKey, sortDir]);

  const setSort = (k: SortKey) => {
    if (sortKey === k) {
      setSortDir(sortDir === "asc" ? "desc" : "asc");
    } else {
      setSortKey(k);
      setSortDir(k === "damage" || k === "onHitStanding" ? "desc" : "asc");
    }
  };

  const sortClass = (k: SortKey) =>
    sortKey === k ? (sortDir === "asc" ? "sort-asc" : "sort-desc") : "";

  if (!char.khd) {
    return <div className="notice">This character has no KHD data.</div>;
  }

  return (
    <>
      <div className="filters">
        <span className="filter-label">Class:</span>
        <button
          className={`chip ${classFilter === null ? "active" : ""}`}
          onClick={() => setClassFilter(null)}
        >
          All
        </button>
        {presentClasses.map((cls) => (
          <button
            key={cls}
            className={`chip ${classFilter === cls ? "active" : ""}`}
            onClick={() => setClassFilter(classFilter === cls ? null : cls)}
          >
            {cls}
          </button>
        ))}
        <span className="filter-label" style={{ marginLeft: "1em" }}>Min dmg:</span>
        {[0, 10, 20, 30, 50].map((n) => (
          <button
            key={n}
            className={`chip ${minDamage === n ? "active" : ""}`}
            onClick={() => setMinDamage(n)}
          >
            {n === 0 ? "any" : `≥${n}`}
          </button>
        ))}
        <span className="filter-label" style={{ marginLeft: "1em" }}>Role:</span>
        <button
          className={`chip ${!showHeaders ? "active" : ""}`}
          onClick={() => setShowHeaders(false)}
        >
          Attacks only
        </button>
        <button
          className={`chip ${showHeaders ? "active" : ""}`}
          onClick={() => setShowHeaders(true)}
        >
          Include headers/sentinels
        </button>
      </div>

      <div className="notice" style={{ marginBottom: "0.8em" }}>
        These are <strong>attack cells</strong> — engine records describing
        what happens when each hit lands (damage, hitstun, range, reaction).
        For the <em>input commands</em> that trigger them, see the{" "}
        <strong>Moves</strong> tab.
      </div>

      <p className="muted" style={{ fontSize: 13 }}>
        Showing <strong>{sorted.length}</strong> of {cells.length} cells.
        Click a row for full details.
      </p>

      <table className="moves-table">
        <thead>
          <tr>
            <th className={sortClass("idx")} onClick={() => setSort("idx")}>#</th>
            <th className={sortClass("class")} onClick={() => setSort("class")}>Class</th>
            <th className={sortClass("damage")} onClick={() => setSort("damage")}>Damage</th>
            <th className={sortClass("activeStart")} onClick={() => setSort("activeStart")}>Startup</th>
            <th>Active</th>
            <th className={sortClass("onHitStanding")} onClick={() => setSort("onHitStanding")}>On-hit</th>
            <th className={sortClass("onBlock")} onClick={() => setSort("onBlock")}>On-block</th>
            <th className={sortClass("rangeStandMax")} onClick={() => setSort("rangeStandMax")}>Range</th>
            <th>Used by</th>
          </tr>
        </thead>
        <tbody>
          {sorted.map((c) => (
            <MoveRow
              key={c.idx}
              cell={c}
              cid={char.cid}
              usedBy={char.khd!.cellToSlots[String(c.idx)]?.length ?? 0}
              navigate={navigate}
            />
          ))}
        </tbody>
      </table>
    </>
  );
}

function MoveRow({
  cell, cid, usedBy, navigate,
}: {
  cell: Cell; cid: string; usedBy: number;
  navigate: ReturnType<typeof useNavigate>;
}) {
  const ranges = [cell.rangeStandMin, cell.rangeStandMax];
  const rangeText = ranges[0] === -127 && ranges[1] === -127
    ? "∞"
    : `${ranges[0]}..${ranges[1]}`;
  return (
    <tr
      onClick={() => navigate({
        to: "/c/$cid/cells/$idx",
        params: { cid, idx: String(cell.idx) },
      })}
    >
      <td className="num muted mono">{cell.idx}</td>
      <td>
        {cell.role === "Attack" ? (
          <>
            <AttackClassBadge value={cell.class} />
            <AttackModifierBadges cell={cell} />
          </>
        ) : (
          <span className="badge badge-header">{cell.role}</span>
        )}
      </td>
      <td className="num">{cell.damage}</td>
      <td className="num">{cell.role === "Attack" ? `i${cell.activeStart}` : ""}</td>
      <td className="num muted">{cell.activeFrames}f</td>
      <td className="num">{cell.onHitStanding}</td>
      <td className="num">{cell.onBlock}</td>
      <td className="num mono">{rangeText}</td>
      <td className="num muted">{usedBy}</td>
    </tr>
  );
}
