import type {
  Cell,
  CharData,
  MovelistMove,
  PlayerMoveFamily,
  PlayerMoveFamilyMetrics,
  PlayerMoveFamilyRow,
  PlayerMoveSummary,
  SourceConfidence,
  TimelineStatus,
} from "../data/types";
import { bestHit, bestStartup, damageTotal, parseFrameValue, rowLooksLikeLauncher, worstBlock } from "./frames";

export interface FamilyStats {
  startup: number | null;
  damage: number | null;
  block: string | number | null;
  hit: string | number | null;
  rowCount: number;
  unsafeCount: number;
  plusCount: number;
  launcherCount: number;
}

export function normalizeCommand(input: string): string {
  return input
    .toUpperCase()
    .replace(/\s+/g, "")
    .replace(/[,>.]/g, "")
    .replace(/[()]/g, "")
    .replace(/-/g, "");
}

function primaryCell(move: MovelistMove, cells: Cell[]): Cell | null {
  for (const commandSet of move.commandSets ?? []) {
    const cell = cells[commandSet.cellIdx];
    if (cell?.role === "Attack") return cell;
  }
  return null;
}

function nativeSlots(move: MovelistMove): number[] {
  return [...new Set((move.commandSets ?? []).map((set) => set.slotIdx).filter((idx) => idx >= 0))];
}

function nativeCells(move: MovelistMove): number[] {
  return [...new Set((move.commandSets ?? []).map((set) => set.cellIdx).filter((idx) => idx >= 0))];
}

function metricsFromMove(move: MovelistMove, cells: Cell[]): PlayerMoveFamilyMetrics {
  const cell = primaryCell(move, cells);
  return {
    startup: move.communityFrame?.startup ?? cell?.activeStart ?? null,
    damage: move.communityFrame?.damage?.length
      ? move.communityFrame.damage
      : cell?.role === "Attack"
        ? [cell.damage]
        : [],
    block: move.communityFrame?.onBlock || (cell?.onBlock ?? null),
    hit: move.communityFrame?.onHit || (cell?.onHitStanding ?? null),
    counterHit: move.communityFrame?.onCounterHit || null,
    hitLevels: move.hitClasses?.length ? move.hitClasses : cell?.class ? [cell.class] : [],
  };
}

function rowFromMove(char: CharData, move: MovelistMove): PlayerMoveFamilyRow {
  const metrics = metricsFromMove(move, char.khd?.cells ?? []);
  const hasCommunity = Boolean(move.communityFrame);
  return {
    id: `${char.cid}-fallback-row-${move.order}`,
    displayCommand: move.fullCommand || move.input || move.condition || `row ${move.order}`,
    displayName: move.name || `Move ${move.moveId}`,
    context: move.condition || "Neutral",
    source: hasCommunity ? "mixed" : "movelist",
    confidence: hasCommunity ? "mixed-supported" : "native-inferred",
    parserMoveOrders: [move.order],
    nativeSlots: nativeSlots(move),
    nativeCells: nativeCells(move),
    metrics,
    notes: [move.note, move.mainTip, move.lethalHitCondition].filter(Boolean).join(" "),
    guardBurst: move.communityFrame?.guardBurst ?? null,
    timelineStatus: hasCommunity ? "partial" : "native-cell-only",
  };
}

function fallbackSummary(char: CharData, families: PlayerMoveFamily[]): PlayerMoveSummary {
  const sourceCounts: Record<string, number> = {};
  const confidenceCounts: Record<string, number> = {};
  const timelineStatusCounts: Record<string, number> = {};
  let playerRows = 0;
  for (const family of families) {
    confidenceCounts[family.confidence] = (confidenceCounts[family.confidence] ?? 0) + 1;
    playerRows += family.rows.length;
    for (const row of family.rows) {
      sourceCounts[row.source] = (sourceCounts[row.source] ?? 0) + 1;
      timelineStatusCounts[row.timelineStatus] = (timelineStatusCounts[row.timelineStatus] ?? 0) + 1;
    }
  }
  return {
    rawMoveRows: char.movelist?.moves.length ?? 0,
    playerFamilies: families.length,
    playerRows,
    communityRows: 0,
    communityCoveredParserRows: 0,
    parserFallbackFamilies: families.length,
    sourceCounts,
    confidenceCounts,
    timelineStatusCounts,
  };
}

export function ensurePlayerFamilies(char: CharData): {
  families: PlayerMoveFamily[];
  summary: PlayerMoveSummary | null;
} {
  const existing = char.movelist?.playerMoveFamilies;
  if (existing?.length) {
    return { families: existing, summary: char.movelist?.playerMoveSummary ?? null };
  }

  const moves = char.movelist?.moves ?? [];
  const families = moves.map((move): PlayerMoveFamily => {
    const row = rowFromMove(char, move);
    return {
      id: `${char.cid}-fallback-family-${move.order}`,
      cid: char.cid,
      kind: "client-fallback",
      rootCommand: row.displayCommand,
      rootName: row.displayName,
      context: row.context ?? "Neutral",
      confidence: row.confidence,
      relations: [],
      rows: [row],
      edges: [],
    };
  });
  return { families, summary: fallbackSummary(char, families) };
}

export function flattenFamilyRows(families: PlayerMoveFamily[]): PlayerMoveFamilyRow[] {
  return families.flatMap((family) => family.rows);
}

export function familyStats(family: PlayerMoveFamily): FamilyStats {
  const rows = family.rows;
  const damages = rows
    .map((row) => damageTotal(row.metrics.damage))
    .filter((value): value is number => value !== null);
  const unsafeCount = rows.filter((row) => {
    const block = parseFrameValue(row.metrics.block);
    return block !== null && block <= -10;
  }).length;
  const plusCount = rows.filter((row) => {
    const block = parseFrameValue(row.metrics.block);
    return block !== null && block > 0;
  }).length;
  return {
    startup: bestStartup(rows),
    damage: damages.length ? Math.max(...damages) : null,
    block: worstBlock(rows),
    hit: bestHit(rows),
    rowCount: rows.length,
    unsafeCount,
    plusCount,
    launcherCount: rows.filter(rowLooksLikeLauncher).length,
  };
}

export function familySearchText(family: PlayerMoveFamily): string {
  return [
    family.rootCommand,
    family.rootName,
    family.context,
    family.confidence,
    family.relations.join(" "),
    ...family.rows.flatMap((row) => [
      row.displayCommand,
      row.displayName,
      row.context ?? "",
      row.source,
      row.confidence,
      row.timelineStatus,
      row.metrics.hitLevels.join(" "),
      row.notes ?? "",
    ]),
  ]
    .join(" ")
    .toLowerCase();
}

export function confidenceRank(confidence: SourceConfidence): number {
  switch (confidence) {
    case "runtime-validated": return 7;
    case "community-confirmed": return 6;
    case "native-confirmed": return 5;
    case "mixed-supported": return 4;
    case "native-inferred": return 3;
    case "weak": return 2;
    case "conflict": return 1;
    default: return 0;
  }
}

export function timelineRank(status: TimelineStatus): number {
  switch (status) {
    case "resolved": return 3;
    case "partial": return 2;
    case "native-cell-only": return 1;
    default: return 0;
  }
}
