import { describe, expect, it } from "vitest";
import type { PlayerMoveFamilyRow } from "../app/data/types";
import { damageTotal, displayDamage, displayFrame, frameTone, parseFrameValue, rowLooksLikeLauncher } from "../app/lib/frames";

describe("frame helpers", () => {
  it("parses only pure signed frame values", () => {
    expect(parseFrameValue("-14")).toBe(-14);
    expect(parseFrameValue("+6")).toBe(6);
    expect(parseFrameValue("KND -2")).toBeNull();
    expect(parseFrameValue("KND")).toBeNull();
  });

  it("maps safety values to useful tones", () => {
    expect(frameTone(-16)).toBe("danger");
    expect(frameTone(-12)).toBe("warning");
    expect(frameTone(0)).toBe("neutral");
    expect(frameTone("+4")).toBe("success");
    expect(frameTone("KND")).toBe("special");
  });

  it("formats frame and damage values without hiding multi-hit totals", () => {
    expect(displayFrame(4)).toBe("+4");
    expect(displayFrame(null)).toBe("-");
    expect(damageTotal([8, 12, 20])).toBe(40);
    expect(displayDamage([8, 12])).toBe("20 (8+12)");
  });

  it("classifies launch outcomes only from confirmed native metric evidence", () => {
    const row = {
      metrics: { hit: "LNC", counterHit: null },
      evidence: {
        hit: { source: "khd-static-timeline", status: "native-confirmed" },
        counterHit: { source: "unknown", status: "unknown" },
      },
      notes: "ordinary localized note",
    } as unknown as PlayerMoveFamilyRow;
    expect(rowLooksLikeLauncher(row)).toBe(true);

    row.evidence.hit.status = "native-inferred";
    expect(rowLooksLikeLauncher(row)).toBe(false);

    row.metrics.hit = null;
    row.notes = "Launches and knocks down the opponent";
    row.evidence.hit.status = "native-confirmed";
    expect(rowLooksLikeLauncher(row)).toBe(false);
  });
});
