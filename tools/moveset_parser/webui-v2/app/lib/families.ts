import type {
  CharData,
  MovelistMove,
  PlayerCharPayload,
  PlayerDashboard,
  PlayerMoveFamily,
  PlayerMoveFamilyRow,
  PlayerMoveSummary,
  SourceConfidence,
  NativeLinkStatus,
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

export interface FamilyViewModel {
  family: PlayerMoveFamily;
  stats: FamilyStats;
  searchText: string;
  commandKeys: string[];
}

export function normalizeCommand(input: string): string {
  return input
    .toUpperCase()
    .replace(/\s+/g, "")
    .replace(/[,>.]/g, "")
    .replace(/[()]/g, "")
    .replace(/-/g, "");
}

function rowFromMove(char: CharData, move: MovelistMove): PlayerMoveFamilyRow {
  return {
    id: `${char.cid}-fallback-row-${move.order}`,
    displayCommand: move.fullCommand || move.input || move.condition || `row ${move.order}`,
    displayName: move.name || `Move ${move.moveId}`,
    context: move.condition || "Neutral",
    isThrowInput: move.isThrowInput,
    source: "game-movelist-table",
    confidence: "game-authored",
    parserMoveOrders: [move.order],
    nativeLink: move.nativeLink,
    metrics: move.metrics,
    evidence: move.evidence,
    notes: [move.note, move.mainTip, move.lethalHitCondition].filter(Boolean).join(" "),
  };
}

function fallbackSummary(char: CharData, families: PlayerMoveFamily[]): PlayerMoveSummary {
  const linkStatusCounts: Record<string, number> = {};
  const groupingConfidenceCounts: Record<string, number> = {};
  const metricCoverage: Record<string, number> = {};
  let playerRows = 0;
  for (const family of families) {
    groupingConfidenceCounts[family.confidence] = (groupingConfidenceCounts[family.confidence] ?? 0) + 1;
    playerRows += family.rows.length;
    for (const row of family.rows) {
      linkStatusCounts[row.nativeLink.status] = (linkStatusCounts[row.nativeLink.status] ?? 0) + 1;
      for (const [metric, value] of Object.entries(row.metrics)) {
        if (value !== null && value !== "" && (!Array.isArray(value) || value.length)) {
          metricCoverage[metric] = (metricCoverage[metric] ?? 0) + 1;
        }
      }
    }
  }
  const nativeLinkedRows = (linkStatusCounts.confirmed ?? 0) + (linkStatusCounts.heuristic ?? 0);
  return {
    officialRows: char.movelist?.moves.length ?? 0,
    playerFamilies: families.length,
    playerRows,
    nativeLinkedRows,
    nativeUnlinkedRows: playerRows - nativeLinkedRows,
    linkStatusCounts,
    groupingConfidenceCounts,
    metricCoverage,
  };
}

function isPlayerPayload(char: CharData | PlayerCharPayload): char is PlayerCharPayload {
  return "playerMoveFamilies" in char;
}

export function ensurePlayerFamilies(char: CharData | PlayerCharPayload): {
  families: PlayerMoveFamily[];
  summary: PlayerMoveSummary | null;
} {
  if (isPlayerPayload(char)) {
    return {
      families: char.playerMoveFamilies ?? [],
      summary: char.playerMoveSummary ?? null,
    };
  }

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
    if (row.isThrowInput) return false;
    if (row.evidence.block.status !== "native-confirmed") return false;
    const block = parseFrameValue(row.metrics.block);
    return block !== null && block <= -10;
  }).length;
  const plusCount = rows.filter((row) => {
    if (row.isThrowInput) return false;
    if (row.evidence.block.status !== "native-confirmed") return false;
    const block = parseFrameValue(row.metrics.block);
    return block !== null && block > 0;
  }).length;
  return {
    startup: bestStartup(rows),
    damage: damages.length ? Math.max(...damages) : null,
    // Family summaries may display statically inferred values as long as the
    // row keeps its provenance tag.  Rankings above remain confirmed-only.
    block: worstBlock(rows.filter((row) => row.evidence.block.status !== "unknown")),
    hit: bestHit(rows.filter((row) => row.evidence.hit.status !== "unknown")),
    rowCount: rows.length,
    unsafeCount,
    plusCount,
    launcherCount: rows.filter((row) => (
      row.evidence.hit.status === "native-confirmed"
      || row.evidence.counterHit.status === "native-confirmed"
    ) && rowLooksLikeLauncher(row)).length,
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
      row.nativeLink.status,
      row.metrics.hitLevels.join(" "),
      row.notes ?? "",
    ]),
  ]
    .join(" ")
    .toLowerCase();
}

export function familyCommandKeys(family: PlayerMoveFamily): string[] {
  return [...new Set([family.rootCommand, ...family.rows.map((row) => row.displayCommand)]
    .map(normalizeCommand)
    .filter(Boolean))];
}

export function buildFamilyViewModels(
  families: PlayerMoveFamily[],
  _dashboard?: PlayerDashboard,
): FamilyViewModel[] {
  return families.map((family) => ({
    family,
    // Dashboard ids remain the trust gate for rankings.  Visible family
    // summaries come from the rows so inferred values are not hidden by the
    // confirmed-only dashboard aggregate.
    stats: familyStats(family),
    searchText: familySearchText(family),
    commandKeys: familyCommandKeys(family),
  }));
}

export function confidenceRank(confidence: SourceConfidence): number {
  switch (confidence) {
    case "game-authored": return 4;
    case "native-confirmed": return 3;
    case "native-inferred": return 3;
    default: return 0;
  }
}

export function timelineRank(status: NativeLinkStatus): number {
  switch (status) {
    case "confirmed": return 3;
    case "heuristic": return 2;
    case "ambiguous": return 1;
    default: return 0;
  }
}
