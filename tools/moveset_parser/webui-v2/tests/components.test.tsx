import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { ConfidenceTag } from "../app/components/StatusTags";
import { FamilyTable } from "../app/components/FamilyTable";
import { makeFamily } from "./fixtures";

describe("ConfidenceTag", () => {
  it("maps confidence values to player-readable labels", () => {
    render(<ConfidenceTag value="community-confirmed" />);
    expect(screen.getByText("community")).toBeInTheDocument();
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
