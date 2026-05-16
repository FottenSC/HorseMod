/**
 * Pure functions extracted from the Moves tab so they can be unit-tested
 * without rendering a React tree.
 */
import type { Cell, FlatMove, MovelistMove } from "../data/types";

/** Dedup cell-bearing slots that share the same (anim, cell) pair —
 * collapses facing-side mirrors. Multi-hit moves with distinct cells
 * stay visible. When two slots share a key, prefers the one with a
 * BFS-derived input (kinds[0] !== "unknown") over a fallback. */
export function dedupMoves(moves: FlatMove[]): FlatMove[] {
  const seen = new Map<string, FlatMove>();
  for (const m of moves) {
    const key = `${m.anim}:${m.cell}`;
    const cur = seen.get(key);
    const curIsFallback = cur?.kinds[0] === "unknown";
    const mIsFallback = m.kinds[0] === "unknown";
    if (
      !cur ||
      (curIsFallback && !mIsFallback) ||
      ((curIsFallback === mIsFallback) && m.slot < cur.slot)
    ) {
      seen.set(key, m);
    }
  }
  return Array.from(seen.values());
}

export interface MovesFilter {
  classFilter: string | null;
  knownOnly: boolean;
  minDamage: number;
}

export function filterMoves(
  moves: FlatMove[],
  cells: Cell[],
  filter: MovesFilter,
): FlatMove[] {
  return moves.filter((m) => {
    const cell = cells[m.cell];
    if (filter.knownOnly && m.kinds[0] === "unknown") return false;
    if (filter.classFilter) {
      if (!cell) return false;
      if (cell.class !== filter.classFilter) return false;
    }
    if (filter.minDamage > 0 && (cell?.damage ?? 0) < filter.minDamage) return false;
    return true;
  });
}

export type MoveSortKey =
  | "name" | "damage" | "startup" | "class" | "onBlock" | "onHit";

export function sortMoves(
  moves: FlatMove[],
  cells: Cell[],
  key: MoveSortKey,
  dir: "asc" | "desc",
): FlatMove[] {
  const arr = [...moves];
  arr.sort((a, b) => {
    const ca = cells[a.cell];
    const cb = cells[b.cell];
    let cmp = 0;
    switch (key) {
      case "name":
        cmp = (a.kinds[0] === "unknown" ? 1 : 0) - (b.kinds[0] === "unknown" ? 1 : 0);
        if (cmp === 0) cmp = a.inputs.length - b.inputs.length;
        if (cmp === 0) cmp = a.inputs.join(" ").localeCompare(b.inputs.join(" "));
        if (cmp === 0) cmp = a.slot - b.slot;
        break;
      case "damage":
        cmp = (ca?.damage ?? 0) - (cb?.damage ?? 0);
        break;
      case "startup":
        cmp = (ca?.activeStart ?? 0) - (cb?.activeStart ?? 0);
        break;
      case "class":
        cmp = (ca?.class ?? "").localeCompare(cb?.class ?? "");
        break;
      case "onBlock":
        cmp = (ca?.onBlock ?? 0) - (cb?.onBlock ?? 0);
        break;
      case "onHit":
        cmp = (ca?.onHitStanding ?? 0) - (cb?.onHitStanding ?? 0);
        break;
    }
    return dir === "asc" ? cmp : -cmp;
  });
  return arr;
}

/** Sentinel anim = 0xFFFF means the parent stance is an engine
 * trampoline (no animation, used only as an input-dispatch entry).
 * Don't show "from stance" navigation for those — clicking would
 * land the user on a sparse junk page. */
export function rootIsRealStance(m: FlatMove | undefined): boolean {
  return !!m && m.rootSlot >= 0 && m.rootAnim !== 0xFFFF;
}

// ---- Stance transition graph -------------------------------------------
// SC6's movelist encodes stances directly, so we don't need the bytecode
// slot-fan-out heuristic to model them. A move's `condition` ("During
// Mist") is the stance it is performed FROM; a " ~ Stance" suffix in the
// move NAME is the stance it transitions INTO (the game's own "shift
// into stance" notation — combo `~` only ever appears in the input
// string, never the name). buildStanceGraph turns the flat movelist into
// a named-stance graph: node per stance, edge per stance-shifting move.

export interface StanceNode {
  name: string;        // display name; "Neutral" is the stanceless root
  movesFrom: number;   // moves performable while in this stance
  movesInto: number;   // moves that transition into this stance
}

export interface StanceTransition {
  from: string;        // source stance ("Neutral" when not stance-bound)
  to: string;          // destination stance
  input: string;       // how the player triggers it (full command)
  moveName: string;
  moveId: number;
  order: number;       // row index into movelist.moves (for deep-linking)
  slot: number;        // primary slot index, or -1
}

export interface StanceGraph {
  stances: StanceNode[];
  transitions: StanceTransition[];
}

export const NEUTRAL_STANCE = "Neutral";

/** Lowercased/trimmed key for case-insensitive stance matching. */
function stanceKey(s: string): string {
  return s.trim().toLowerCase();
}

/** Build the stance-transition graph for one character's movelist.
 * "When in stance X, input Y shifts you to stance Z" = a transition
 * edge X --Y--> Z. */
export function buildStanceGraph(moves: MovelistMove[]): StanceGraph {
  // Pass 1 — the stance vocabulary is the set of " ~ X" name suffixes.
  const displayByKey = new Map<string, string>();
  for (const m of moves) {
    const i = m.name.lastIndexOf(" ~ ");
    if (i < 0) continue;
    const dest = m.name.slice(i + 3).trim();
    if (dest) displayByKey.set(stanceKey(dest), dest);
  }

  // condition -> from-stance. "During <KnownStance>…" => that stance
  // (longest-prefix match folds "Mantis Crawl with feet toward
  // opponent" into "Mantis Crawl"); anything else => Neutral, so the
  // graph stays anchored on real stances.
  const fromStance = (condition: string): string => {
    const c = condition.trim();
    if (!/^during /i.test(c)) return NEUTRAL_STANCE;
    const stem = stanceKey(c.slice(7));
    if (displayByKey.has(stem)) return displayByKey.get(stem)!;
    let best = "";
    for (const k of displayByKey.keys()) {
      if ((stem === k || stem.startsWith(k + " ")) && k.length > best.length) {
        best = k;
      }
    }
    return best ? displayByKey.get(best)! : NEUTRAL_STANCE;
  };

  // move name -> destination stance: a " ~ X" suffix, or the whole name
  // when it IS a stance (dedicated stance-entry moves like "Mist").
  const toStance = (name: string): string | null => {
    const i = name.lastIndexOf(" ~ ");
    if (i >= 0) {
      const d = displayByKey.get(stanceKey(name.slice(i + 3)));
      if (d) return d;
    }
    return displayByKey.get(stanceKey(name)) ?? null;
  };

  const nodes = new Map<string, StanceNode>();
  const node = (name: string): StanceNode => {
    let n = nodes.get(name);
    if (!n) { n = { name, movesFrom: 0, movesInto: 0 }; nodes.set(name, n); }
    return n;
  };
  node(NEUTRAL_STANCE);
  for (const disp of displayByKey.values()) node(disp);

  const transitions: StanceTransition[] = [];
  for (const m of moves) {
    const from = fromStance(m.condition);
    node(from).movesFrom++;
    const to = toStance(m.name);
    if (to && to !== from) {
      node(to).movesInto++;
      transitions.push({
        from, to,
        input: m.fullCommand || m.input || m.condition,
        moveName: m.name,
        moveId: m.moveId,
        order: m.order,
        slot: m.commandSets[0]?.slotIdx ?? -1,
      });
    }
  }
  // Drop a lone Neutral node for characters with no stances at all.
  const stances = [...nodes.values()].filter(
    (n) => n.name !== NEUTRAL_STANCE || transitions.length > 0,
  );
  return { stances, transitions };
}

export interface MoveVariation {
  move: MovelistMove;
  /** "follow-up" = `move` extends `current`; "base" = the reverse. */
  relation: "follow-up" | "base";
}

/** Find a move's "string" variations: other movelist entries whose
 * canonical input extends this one's at a `.`/`~` boundary, or vice
 * versa. SC6 lists e.g. Astaroth's "Poseidon Tide" (214A) and
 * "Poseidon Tide Rush" (214A.A.A.A.A.A) as two separate MoveListIDs
 * with no cross-reference; this re-links them so the press-once vs
 * press-again branch (which changes damage / on-hit behaviour) is
 * visible from either move's page.
 *
 * The boundary check is load-bearing: "214A" is an ancestor of
 * "214A.A" but NOT of "214AB" — a continuation always starts a fresh
 * `.`/`~`-separated token. Only entries sharing the move's
 * stance/condition prefix are paired (a grounded string and its
 * While-crouching namesake are different moves). */
export function findInputVariations(
  moves: MovelistMove[],
  current: MovelistMove,
): MoveVariation[] {
  if (!current.input) return [];
  const isExt = (long: string, short: string) =>
    long.length > short.length && long.startsWith(short)
    && (long[short.length] === "." || long[short.length] === "~");
  const out: MoveVariation[] = [];
  for (const m of moves) {
    if (m.order === current.order || m.condition !== current.condition || !m.input) {
      continue;
    }
    if (isExt(m.input, current.input)) out.push({ move: m, relation: "follow-up" });
    else if (isExt(current.input, m.input)) out.push({ move: m, relation: "base" });
  }
  out.sort((a, b) => a.move.input.length - b.move.input.length);
  return out;
}
