import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { ConfidenceTag } from "../app/components/StatusTags";
import { FamilyTable } from "../app/components/FamilyTable";
import { makeFamily } from "./fixtures";
import { buildFamilyViewModels, familyStats } from "../app/lib/families";

describe("ConfidenceTag", () => {
  it("maps confidence values to player-readable labels", () => {
    render(<ConfidenceTag value="game-authored" />);
    expect(screen.getByText("game authored")).toBeInTheDocument();
  });
});

describe("native evidence rankings", () => {
  it("excludes unproven advantage and reaction text", () => {
    const family = makeFamily({ id: "unproven", command: "3B", name: "Description Launcher" });
    const stats = familyStats(family);
    expect(stats.unsafeCount).toBe(0);
    expect(stats.plusCount).toBe(0);
    expect(stats.launcherCount).toBe(0);
    expect(stats.block).toBeNull();
    expect(stats.hit).toBeNull();
  });

  it("displays inferred advantage without admitting it to confirmed-only rankings", () => {
    const family = makeFamily({
      id: "inferred-advantage",
      command: "B.A",
      name: "Bear Tamer",
    });
    const row = family.rows[0];
    row.metrics.block = -8;
    row.metrics.hit = 2;
    row.evidence.block = { source: "khd-static-timeline", status: "native-inferred" };
    row.evidence.hit = { source: "khd-static-timeline", status: "native-inferred" };

    const stats = familyStats(family);
    expect(stats.block).toBe(-8);
    expect(stats.hit).toBe(2);
    expect(stats.unsafeCount).toBe(0);
    expect(stats.plusCount).toBe(0);

    const dashboard = {
      statsByFamily: {
        [family.id]: { ...stats, block: null, hit: null },
      },
      fastestFamilyIds: [],
      unsafeFamilyIds: [],
      plusFamilyIds: [],
      launcherFamilyIds: [],
    };
    expect(buildFamilyViewModels([family], dashboard)[0].stats.block).toBe(-8);
    expect(buildFamilyViewModels([family], dashboard)[0].stats.hit).toBe(2);
  });

  it("does not rank confirmed throw-break recovery as strike punishability", () => {
    const family = makeFamily({
      id: "throw-break",
      command: "A+G",
      name: "Throw",
    });
    const row = family.rows[0];
    row.isThrowInput = true;
    row.metrics.block = -12;
    row.evidence.block = { source: "khd-static-timeline", status: "native-confirmed" };

    const stats = familyStats(family);
    expect(stats.block).toBe(-12);
    expect(stats.unsafeCount).toBe(0);
    expect(stats.plusCount).toBe(0);
  });
});

describe("FamilyTable", () => {
  it("shows family summaries and expands exact child rows", async () => {
    const user = userEvent.setup();
    const family = makeFamily({
      id: "family-aa",
      command: "A.A",
      name: "Double Slice",
      relations: ["prefix"],
    });

    render(
      <FamilyTable
        families={[family]}
        familyLink={() => <span className="text-link">Open family evidence</span>}
      />,
    );

    expect(screen.getAllByText("Double Slice").length).toBeGreaterThan(0);
    expect(screen.getByText("1 rows")).toBeInTheDocument();
    expect(screen.queryByText("test row")).not.toBeInTheDocument();

    await user.click(screen.getAllByText("Double Slice")[0]);

    expect(screen.getByText("Open family evidence")).toBeInTheDocument();
    expect(screen.getByText("test row")).toBeInTheDocument();
  });
});
