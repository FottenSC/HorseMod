import { describe, expect, it } from "vitest";
import { damageTotal, displayDamage, displayFrame, frameTone, parseFrameValue } from "../app/lib/frames";

describe("frame helpers", () => {
  it("parses signed frame values from community notation", () => {
    expect(parseFrameValue("-14")).toBe(-14);
    expect(parseFrameValue("+6")).toBe(6);
    expect(parseFrameValue("KND -2")).toBe(-2);
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
});
