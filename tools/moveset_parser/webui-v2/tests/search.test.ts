import { describe, expect, it } from "vitest";
import { filterFamilies, rankFamily } from "../app/lib/search";
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
});
