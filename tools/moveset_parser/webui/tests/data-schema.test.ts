/** Verifies the exported character JSON conforms to the TS types
 * the UI loaders cast it to. If the Python export changes shape,
 * this catches it before runtime. */
import { describe, it, expect } from "vitest";
import { readFileSync, existsSync } from "node:fs";
import { resolve } from "node:path";
import type { CharData } from "../app/data/types";

const DATA = resolve(__dirname, "../public/data");

function loadChar(cid: string): CharData | null {
  const p = resolve(DATA, "chars", `${cid}.json`);
  if (!existsSync(p)) return null;
  return JSON.parse(readFileSync(p, "utf-8")) as CharData;
}

describe("Exported JSON schema", () => {
  it.skipIf(!existsSync(resolve(DATA, "chars/001.json")))(
    "Mitsurugi has the expected fields",
    () => {
      const d = loadChar("001")!;
      expect(d.cid).toBe("001");
      expect(d.name).toBe("Mitsurugi");
      expect(d.khd).toBeDefined();
      const khd = d.khd!;
      expect(Array.isArray(khd.cells)).toBe(true);
      expect(Array.isArray(khd.slots)).toBe(true);
      expect(Array.isArray(khd.slotEdges)).toBe(true);
      expect(Array.isArray(khd.stanceRoots)).toBe(true);
      expect(Array.isArray(khd.flatMoves)).toBe(true);
    },
  );

  it.skipIf(!existsSync(resolve(DATA, "chars/001.json")))(
    "no synthetic '?A'/'?B'/'?X' inputs leaked into JSON",
    () => {
      const khd = loadChar("001")!.khd!;
      for (const m of khd.flatMoves) {
        for (const inp of m.inputs) {
          expect(inp.startsWith("?")).toBe(false);
        }
      }
    },
  );

  it.skipIf(!existsSync(resolve(DATA, "chars/001.json")))(
    "current Mitsurugi slot 401 is preserved without a fabricated input",
    () => {
      const khd = loadChar("001")!.khd!;
      const m = khd.flatMoves.find((m) => m.slot === 401);
      expect(m).toBeDefined();
      // The checked-in older KHD uses slot 401 for animation 367 and reaches
      // it through a K,B orphan chain. The current production KHD reuses the
      // numeric slot for animation 159; its only incoming edge is the frame
      // transition from orphan slot 400, so no player input is proven.
      expect(m!.anim).toBe(159);
      expect(m!.inputs).toEqual([]);
      expect(m!.kinds).toEqual(["unknown"]);
    },
  );

  it.skipIf(!existsSync(resolve(DATA, "chars/00b.json")))(
    "Astaroth move count above floor (regression)",
    () => {
      // Was 11 in one bad version; should be 200+ now.
      const khd = loadChar("00b")!.khd!;
      expect(khd.flatMoves.length).toBeGreaterThan(200);
    },
  );

  it.skipIf(!existsSync(resolve(DATA, "chars/001.json")))(
    "no dead moveTrees field leaks in JSON",
    () => {
      const khd = loadChar("001")!.khd! as unknown as Record<string, unknown>;
      expect("moveTrees" in khd).toBe(false);
    },
  );
});
