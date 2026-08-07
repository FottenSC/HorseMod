import { describe, it, expect } from "vitest";
import {
  dedupMoves, filterMoves, sortMoves, rootIsRealStance,
  findInputVariations, buildStanceGraph,
} from "../app/lib/moves";
import { makeCell, makeMove, makeMovelistMove } from "./fixtures";

describe("dedupMoves", () => {
  it("collapses same (anim, cell) pair", () => {
    const moves = [
      makeMove({ slot: 100, anim: 50, cell: 5 }),
      makeMove({ slot: 200, anim: 50, cell: 5 }),  // mirror — same anim+cell
    ];
    const out = dedupMoves(moves);
    expect(out).toHaveLength(1);
    expect(out[0].slot).toBe(100);   // lower slot wins
  });

  it("preserves multi-hit moves (same anim, different cells)", () => {
    const moves = [
      makeMove({ slot: 100, anim: 50, cell: 5 }),
      makeMove({ slot: 101, anim: 50, cell: 6 }),
      makeMove({ slot: 102, anim: 50, cell: 7 }),
    ];
    expect(dedupMoves(moves)).toHaveLength(3);
  });

  it("prefers BFS-derived over fallback when same key", () => {
    const moves = [
      // fallback (lower slot but unknown)
      makeMove({ slot: 100, anim: 50, cell: 5, inputs: [], kinds: ["unknown"] }),
      // BFS-derived (higher slot, known input)
      makeMove({ slot: 200, anim: 50, cell: 5, inputs: ["6A"], kinds: ["buttons"] }),
    ];
    const out = dedupMoves(moves);
    expect(out).toHaveLength(1);
    expect(out[0].slot).toBe(200);
    expect(out[0].kinds[0]).toBe("buttons");
  });

  it("does not collapse different (anim) even if cell matches", () => {
    const moves = [
      makeMove({ slot: 100, anim: 50, cell: 5 }),
      makeMove({ slot: 101, anim: 51, cell: 5 }),
    ];
    expect(dedupMoves(moves)).toHaveLength(2);
  });
});

describe("filterMoves", () => {
  const cells = [
    makeCell({ idx: 0, class: "Mid", damage: 20 }),
    makeCell({ idx: 1, class: "High", damage: 10 }),
    makeCell({ idx: 2, class: "Low", damage: 30 }),
  ];

  it("class filter only keeps matching cells", () => {
    const moves = [
      makeMove({ slot: 1, cell: 0 }),  // Mid
      makeMove({ slot: 2, cell: 1 }),  // High
      makeMove({ slot: 3, cell: 2 }),  // Low
    ];
    expect(filterMoves(moves, cells, {
      classFilter: "Mid", knownOnly: false, minDamage: 0,
    })).toHaveLength(1);
  });

  it("class filter rejects cell-missing rows (no leakage)", () => {
    // Regression: previously, cell-less rows leaked through every class filter.
    const moves = [makeMove({ slot: 1, cell: 99 })];  // cell index OOB
    expect(filterMoves(moves, cells, {
      classFilter: "Mid", knownOnly: false, minDamage: 0,
    })).toHaveLength(0);
  });

  it("knownOnly hides fallback moves", () => {
    const moves = [
      makeMove({ slot: 1, cell: 0, inputs: ["6A"], kinds: ["buttons"] }),
      makeMove({ slot: 2, cell: 1, inputs: [], kinds: ["unknown"] }),
    ];
    expect(filterMoves(moves, cells, {
      classFilter: null, knownOnly: true, minDamage: 0,
    })).toHaveLength(1);
  });

  it("minDamage filters by cell damage", () => {
    const moves = [
      makeMove({ slot: 1, cell: 0 }),  // 20
      makeMove({ slot: 2, cell: 1 }),  // 10
      makeMove({ slot: 3, cell: 2 }),  // 30
    ];
    const out = filterMoves(moves, cells, {
      classFilter: null, knownOnly: false, minDamage: 20,
    });
    expect(out.map((m) => m.slot).sort()).toEqual([1, 3]);
  });
});

describe("sortMoves", () => {
  const cells = [
    makeCell({ idx: 0, damage: 10, activeStartCoordinate: 14, blockStunFrames: 10, baseHitStunFrames: 20 }),
    makeCell({ idx: 1, damage: 30, activeStartCoordinate: 18, blockStunFrames: 2, baseHitStunFrames: 12 }),
    makeCell({ idx: 2, damage: 20, activeStartCoordinate: 11, blockStunFrames: 16, baseHitStunFrames: 18 }),
  ];

  it("sorts by damage desc", () => {
    const moves = [
      makeMove({ slot: 1, cell: 0 }),
      makeMove({ slot: 2, cell: 1 }),
      makeMove({ slot: 3, cell: 2 }),
    ];
    const out = sortMoves(moves, cells, "damage", "desc");
    expect(out.map((m) => m.cell)).toEqual([1, 2, 0]);
  });

  it("sorts by startup asc", () => {
    const moves = [
      makeMove({ slot: 1, cell: 0 }),
      makeMove({ slot: 2, cell: 1 }),
      makeMove({ slot: 3, cell: 2 }),
    ];
    const out = sortMoves(moves, cells, "startup", "asc");
    expect(out.map((m) => m.cell)).toEqual([2, 0, 1]);
  });

  it("'name' sort puts known-input first", () => {
    const moves = [
      makeMove({ slot: 1, cell: 0, kinds: ["unknown"] }),
      makeMove({ slot: 2, cell: 1, inputs: ["6A"], kinds: ["buttons"] }),
      makeMove({ slot: 3, cell: 2, kinds: ["unknown"] }),
    ];
    const out = sortMoves(moves, cells, "name", "asc");
    expect(out[0].slot).toBe(2);  // known wins
  });
});

describe("rootIsRealStance", () => {
  it("true for normal stance", () => {
    expect(rootIsRealStance(makeMove({
      slot: 1, cell: 0, rootSlot: 406, rootAnim: 186,
    }))).toBe(true);
  });

  it("false for sentinel root (anim 0xFFFF)", () => {
    expect(rootIsRealStance(makeMove({
      slot: 1, cell: 0, rootSlot: 2892, rootAnim: 0xFFFF,
    }))).toBe(false);
  });

  it("false for unrooted (rootSlot = -1)", () => {
    expect(rootIsRealStance(makeMove({
      slot: 1, cell: 0, rootSlot: -1, rootAnim: -1,
    }))).toBe(false);
  });

  it("false for undefined", () => {
    expect(rootIsRealStance(undefined)).toBe(false);
  });
});

describe("findInputVariations", () => {
  // Astaroth's real movelist: 214A "Poseidon Tide" and its extension
  // 214A.A.A.A.A.A "Poseidon Tide Rush" are separate MoveListIDs.
  const tide = makeMovelistMove({ moveId: 18, order: 62, name: "Poseidon Tide", input: "214A" });
  const rush = makeMovelistMove({ moveId: 19, order: 63, name: "Poseidon Tide Rush", input: "214A.A.A.A.A.A" });
  const unrelated = makeMovelistMove({ moveId: 20, order: 64, name: "Command Kicks", input: "BK.K" });

  it("links a follow-up string from the base move's side", () => {
    const out = findInputVariations([tide, rush, unrelated], tide);
    expect(out).toHaveLength(1);
    expect(out[0].move.moveId).toBe(19);
    expect(out[0].relation).toBe("follow-up");
  });

  it("links the base move from the follow-up's side", () => {
    const out = findInputVariations([tide, rush, unrelated], rush);
    expect(out).toHaveLength(1);
    expect(out[0].move.moveId).toBe(18);
    expect(out[0].relation).toBe("base");
  });

  it("requires a boundary — '214A' is NOT an ancestor of '214AB'", () => {
    const glued = makeMovelistMove({ moveId: 21, order: 65, input: "214AB" });
    expect(findInputVariations([tide, glued], tide)).toHaveLength(0);
  });

  it("treats `~` (slide) as a token boundary", () => {
    const slide = makeMovelistMove({ moveId: 22, order: 66, input: "214A~B" });
    const out = findInputVariations([tide, slide], tide);
    expect(out.map((v) => v.move.moveId)).toEqual([22]);
  });

  it("does not pair moves with different stance/condition prefixes", () => {
    const grounded = makeMovelistMove({ moveId: 1, order: 1, input: "A.A", condition: "" });
    const crouch = makeMovelistMove({ moveId: 2, order: 2, input: "A.A.A", condition: "While crouching" });
    expect(findInputVariations([grounded, crouch], grounded)).toHaveLength(0);
  });

  it("excludes the move itself and sorts shortest-input first", () => {
    const a = makeMovelistMove({ moveId: 1, order: 1, input: "A" });
    const aa = makeMovelistMove({ moveId: 2, order: 2, input: "A.A" });
    const aaa = makeMovelistMove({ moveId: 3, order: 3, input: "A.A.A" });
    const out = findInputVariations([aaa, a, aa], aa);
    expect(out.map((v) => v.move.input)).toEqual(["A", "A.A.A"]);
  });

  it("returns nothing for an empty input", () => {
    const blank = makeMovelistMove({ moveId: 9, order: 9, input: "" });
    expect(findInputVariations([tide, rush], blank)).toHaveLength(0);
  });
});

describe("buildStanceGraph", () => {
  it("links Neutral -> stance and stance -> stance", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "Furious Stab ~ Mist", input: "6B.B", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "Sun Flip ~ Relic", input: "1B.A+B", condition: "During Mist" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.stances.map((s) => s.name).sort()).toEqual(["Mist", "Neutral", "Relic"]);
    expect(g.transitions).toHaveLength(2);
    expect(g.transitions.find((t) => t.to === "Mist")).toMatchObject({ from: "Neutral", input: "6B.B" });
    expect(g.transitions.find((t) => t.to === "Relic")).toMatchObject({ from: "Mist" });
  });

  it("treats a move named exactly as a stance as an entry move", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "X ~ Mist", input: "6B", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "Mist", input: "B+K", condition: "During Relic" }),
      makeMovelistMove({ moveId: 3, order: 2, name: "Y ~ Relic", input: "1B", condition: "" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.transitions.find((t) => t.moveId === 2)).toMatchObject({ from: "Relic", to: "Mist" });
  });

  it("collapses a compound 'During X ...' condition to its base stance", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "A ~ Mantis Crawl", input: "B+K", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "B ~ Death Roll", input: "K",
        condition: "During Mantis Crawl with feet toward opponent" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.transitions.find((t) => t.moveId === 2)?.from).toBe("Mantis Crawl");
  });

  it("matches stance names case-insensitively", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "A ~ facing away", input: "4K", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "B ~ Mist", input: "K", condition: "During Facing Away" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.transitions.find((t) => t.moveId === 2)?.from).toBe("facing away");
  });

  it("keeps an X->X self-loop when a move re-enters its own stance", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "A ~ Mist", input: "B", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "B ~ Mist", input: "K", condition: "During Mist" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.transitions.find((t) => t.moveId === 2))
      .toMatchObject({ from: "Mist", to: "Mist" });
  });

  it("routes a no-shift stance move back to Neutral", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "A ~ Mist", input: "B", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "Mist Slash", input: "B", condition: "During Mist" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.transitions.find((t) => t.moveId === 2))
      .toMatchObject({ from: "Mist", to: "Neutral" });
  });

  it("does not make a Neutral->Neutral self-loop from a plain move", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "A ~ Mist", input: "B", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "Heaven Cannon", input: "3B", condition: "" }),
    ];
    const g = buildStanceGraph(moves);
    expect(g.transitions.map((t) => t.moveId)).toEqual([1]);
  });

  it("returns an empty graph for a stanceless movelist", () => {
    const g = buildStanceGraph([
      makeMovelistMove({ moveId: 1, order: 0, name: "Heaven Cannon", input: "3B", condition: "" }),
    ]);
    expect(g.stances).toHaveLength(0);
    expect(g.transitions).toHaveLength(0);
  });

  it("counts movesFrom / movesInto per stance", () => {
    const moves = [
      makeMovelistMove({ moveId: 1, order: 0, name: "A ~ Mist", input: "B", condition: "" }),
      makeMovelistMove({ moveId: 2, order: 1, name: "Mist Slash", input: "B", condition: "During Mist" }),
      makeMovelistMove({ moveId: 3, order: 2, name: "Mist Kick", input: "K", condition: "During Mist" }),
    ];
    const mist = buildStanceGraph(moves).stances.find((s) => s.name === "Mist")!;
    expect(mist.movesInto).toBe(1);
    expect(mist.movesFrom).toBe(2);
  });
});
