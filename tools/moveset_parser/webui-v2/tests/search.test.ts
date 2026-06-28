import { describe, expect, it } from "vitest";
import { filterFamilies, rankFamily, rankLookupFamily } from "../app/lib/search";
import type { LookupFamilySummary } from "../app/data/types";
import { makeFamily } from "./fixtures";

describe("family search", () => {
  it("ranks exact command matches above prefix or text matches", () => {
    const exact = makeFamily({ id: "aa", command: "A.A", name: "Double Slice" });
    const prefix = makeFamily({ id: "aaa", command: "A.A.B", name: "Long String" });
    const text = makeFamily({ id: "note", command: "3B", name: "AA Crusher" });

    expect(rankFamily(exact, "AA")).toBeGreaterThan(rankFamily(prefix, "AA"));
    expect(rankFamily(prefix, "AA")).toBeGreaterThan(rankFamily(text, "AA"));
  });

  it("filters by stance and move names as well as command text", () => {
    const neutral = makeFamily({ id: "neutral", command: "3B", name: "Launcher" });
    const stance = makeFamily({ id: "pos", command: "B", name: "Stance Hit", context: "During Possession" });

    expect(filterFamilies([neutral, stance], "possession")).toEqual([stance]);
    expect(filterFamilies([neutral, stance], "launcher")).toEqual([neutral]);
  });

  it("preserves exported order when there is no query", () => {
    const slow = makeFamily({ id: "slow", command: "A", name: "Listed First" });
    const fast = makeFamily({ id: "fast", command: "2A", name: "Listed Second" });
    fast.rows[0].metrics.startup = 6;
    slow.rows[0].metrics.startup = 24;

    expect(filterFamilies([slow, fast], "")).toEqual([slow, fast]);
  });

  it("ranks exact command matches from lookup-index summaries", () => {
    const exact: LookupFamilySummary = {
      cid: "003",
      charName: "Taki",
      kind: "base",
      familyId: "aa",
      rootCommand: "A.A",
      rootName: "Double Slice",
      context: "Neutral",
      confidence: "mixed-supported",
      relations: [],
      rowCount: 1,
      metrics: { startup: 10, damage: 16, block: -8, hit: 2, rowCount: 1, unsafeCount: 0, plusCount: 0, launcherCount: 0 },
      sourceCounts: { mixed: 1 },
      timelineStatusCounts: { partial: 1 },
      commandKeys: ["AA"],
      searchText: "taki double slice aa high",
    };
    const text = { ...exact, familyId: "note", rootCommand: "3B", commandKeys: ["3B"], searchText: "taki aa crusher" };

    expect(rankLookupFamily(exact, "AA")).toBeGreaterThan(rankLookupFamily(text, "AA"));
  });
});
