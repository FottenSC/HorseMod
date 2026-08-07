import type { PlayerMoveFamily } from "../app/data/types";

export function makeFamily(p: Partial<PlayerMoveFamily> & { id: string; command: string; name: string }): PlayerMoveFamily {
  const rowId = `${p.id}-row`;
  return {
    id: p.id,
    cid: p.cid ?? "003",
    kind: p.kind ?? "test",
    rootCommand: p.rootCommand ?? p.command,
    rootName: p.rootName ?? p.name,
    context: p.context ?? "Neutral",
    confidence: p.confidence ?? "game-authored",
    relations: p.relations ?? [],
    rows: p.rows ?? [
      {
        id: rowId,
        displayCommand: p.command,
        displayName: p.name,
        context: p.context ?? "Neutral",
        isThrowInput: false,
        source: "game-movelist-table",
        confidence: "game-authored",
        parserMoveOrders: [1],
        nativeLink: {
          status: "confirmed",
          resolutions: ["movevm-main-definition-confirmed"],
          definitions: [{ lane: "primary-fighter", mainDefinitionId: 10, fallbackDefinitionId: 0 }],
          slots: [],
          cells: [],
        },
        metrics: {
          startup: 10,
          damage: [8, 12],
          block: -8,
          hit: 2,
          counterHit: "KND",
          guardBurst: null,
          hitLevels: ["High"],
        },
        evidence: {
          startup: { source: "khd-attack-cell", status: "native-inferred" },
          damage: { source: "khd-attack-cell", status: "native-inferred" },
          block: { source: "unknown", status: "unknown" },
          hit: { source: "unknown", status: "unknown" },
          counterHit: { source: "unknown", status: "unknown" },
          guardBurst: { source: "unknown", status: "unknown" },
          hitLevels: { source: "game-movelist-table", status: "game-authored" },
        },
        notes: "test row",
      },
    ],
    edges: p.edges ?? [],
  };
}
