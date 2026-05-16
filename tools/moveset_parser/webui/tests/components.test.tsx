import { describe, it, expect } from "vitest";
import { render } from "@testing-library/react";
import { AttackClassBadge, AttackModifierBadges, CellRoleBadge, EffectTagBadges, MoveClassBadge } from "../app/components/AttackClassBadge";

describe("AttackClassBadge", () => {
  it("renders known classes with the matching css class", () => {
    const { container } = render(<AttackClassBadge value="Mid" />);
    expect(container.firstChild).toHaveClass("badge-mid");
    expect(container).toHaveTextContent("Mid");
  });

  it("falls back to badge-other for unknown class", () => {
    const { container } = render(<AttackClassBadge value={"Vortex" as never} />);
    expect(container.firstChild).toHaveClass("badge-other");
  });

  it.each(["High", "Low", "Unblockable", "Throw"] as const)(
    "renders %s",
    (cls) => {
      const { container } = render(<AttackClassBadge value={cls} />);
      expect(container).toHaveTextContent(cls);
    },
  );
});

describe("AttackModifierBadges", () => {
  it("renders BA pill for break attacks", () => {
    const { container } = render(
      <AttackModifierBadges cell={{ class: "Mid", isBreakAttack: true, isGiImmune: true, isGuardBypass: false }} />,
    );
    expect(container).toHaveTextContent("BA");
    // BA cell already implies GI immunity — don't duplicate the pill.
    expect(container).not.toHaveTextContent("GI");
  });

  it("renders GI⊘ for GI-immune non-BA non-Unblockable", () => {
    const { container } = render(
      <AttackModifierBadges cell={{ class: "Mid", isBreakAttack: false, isGiImmune: true, isGuardBypass: false }} />,
    );
    expect(container).toHaveTextContent("GI");
  });

  it("hides GI⊘ when class is Unblockable (implied)", () => {
    const { container } = render(
      <AttackModifierBadges cell={{ class: "Unblockable", isBreakAttack: false, isGiImmune: true, isGuardBypass: false }} />,
    );
    expect(container).not.toHaveTextContent("GI");
  });

  it("renders GB for guard-bypass cells", () => {
    const { container } = render(
      <AttackModifierBadges cell={{ class: "Mid", isBreakAttack: false, isGiImmune: false, isGuardBypass: true }} />,
    );
    expect(container).toHaveTextContent("GB");
  });

  it("renders nothing for a plain Mid cell", () => {
    const { container } = render(
      <AttackModifierBadges cell={{ class: "Mid", isBreakAttack: false, isGiImmune: false, isGuardBypass: false }} />,
    );
    expect(container.textContent).toBe("");
  });
});

describe("CellRoleBadge", () => {
  it("renders each role with the matching class", () => {
    const cases = [
      { role: "Header", expected: "badge-header" },
      { role: "NonDamaging", expected: "badge-nondamaging" },
      { role: "Sentinel", expected: "badge-sentinel" },
    ] as const;
    for (const { role, expected } of cases) {
      const { container } = render(<CellRoleBadge value={role} />);
      expect(container.firstChild).toHaveClass(expected);
    }
  });
});

describe("EffectTagBadges", () => {
  it("renders nothing for an empty tag list", () => {
    const { container } = render(<EffectTagBadges tags={[]} />);
    expect(container.firstChild).toBeNull();
  });

  it("renders a pill per tag with the family CSS class", () => {
    const { container } = render(
      <EffectTagBadges tags={[
        { code: "UA", label: "Unblockable Attack" },
        { code: "LH", label: "Lethal Hit" },
      ]} />,
    );
    expect(container).toHaveTextContent("UA");
    expect(container).toHaveTextContent("LH");
    expect(container.querySelector(".badge-eff-ua")).not.toBeNull();
    expect(container.querySelector(".badge-eff-lh")).not.toBeNull();
  });

  it("falls back to badge-other for an unknown code", () => {
    const { container } = render(
      <EffectTagBadges tags={[{ code: "ZZ", label: "Mystery" }]} />,
    );
    expect(container.querySelector(".badge-other")).not.toBeNull();
  });
});

describe("MoveClassBadge", () => {
  it("renders nothing for an empty hitClasses list", () => {
    const { container } = render(<MoveClassBadge hitClasses={[]} />);
    expect(container.firstChild).toBeNull();
  });

  it("shows the first hit's class for a single-hit move", () => {
    const { container } = render(<MoveClassBadge hitClasses={["Mid"]} />);
    expect(container).toHaveTextContent("Mid");
    expect(container.querySelector(".badge-mid")).not.toBeNull();
  });

  it("shows first class + ×N for a multi-hit move", () => {
    const { container } = render(
      <MoveClassBadge hitClasses={["Low", "Mid", "Mid"]} />,
    );
    expect(container).toHaveTextContent("Low ×3");
    expect(container.querySelector(".badge-low")).not.toBeNull();
  });
});
