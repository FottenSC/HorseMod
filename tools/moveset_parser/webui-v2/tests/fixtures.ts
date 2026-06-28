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
    confidence: p.confidence ?? "mixed-supported",
    relations: p.relations ?? [],
    rows: p.rows ?? [
      {
        id: rowId,
        displayCommand: p.command,
        displayName: p.name,
        context: p.context ?? "Neutral",
        source: "mixed",
        confidence: "mixed-supported",
        parserMoveOrders: [1],
        nativeSlots: [10],
        nativeCells: [20],
        metrics: {
          startup: 10,
          damage: [8, 12],
          block: -8,
          hit: 2,
          counterHit: "KND",
          hitLevels: ["High"],
        },
        notes: "test row",
        timelineStatus: "partial",
      },
    ],
    edges: p.edges ?? [],
  };
}
