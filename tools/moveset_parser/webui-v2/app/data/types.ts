export type CharaKind = "base" | "dlc" | "boss" | "shared" | "unknown";

export interface CharSummary {
  cid: string;
  name: string;
  kind: CharaKind;
  uncertain?: boolean;
  files: Record<string, boolean>;
  attackCount?: number;
  slotCount?: number;
  topDamage?: number;
  classDistribution?: Record<string, number>;
  error?: string;
}

export interface Roster {
  chars: CharSummary[];
}

export type CellRole = "Attack" | "Header" | "NonDamaging" | "Sentinel";

export interface Cell {
  idx: number;
  role: CellRole;
  class: string;
  damage: number;
  activeStartCoordinate: number;
  activeEndCoordinate: number;
  activeFrames: number;
  blockStunFrames: number;
  baseHitStunFrames: number;
  attackFlagsDecoded?: string;
  isBreakAttack?: boolean;
  isGiImmune?: boolean;
  isGuardBypass?: boolean;
}

export interface Slot {
  idx: number;
  animationIndex: number;
  totalFrames: number;
  attackCellRefs: number[];
  throwCellRefs: number[];
}

export interface KhdPayload {
  attackCount: number;
  slotCount: number;
  totalCells: number;
  cells: Cell[];
  slots: Slot[];
}

export interface MovelistCommandSet {
  commandSetIndex: number;
  lane: "primary-fighter" | "paired-opponent" | string;
  mainIndex: number;
  introIndex: number;
  cellIdx: number;
  slotIdx: number;
  resolution: string;
}

export interface MovelistInputVariant {
  cellIdx: number;
  slotIdx: number;
  hint: string;
  kind: string;
}

export interface MovelistMove {
  moveId: number;
  category: number;
  categoryMemberships?: number[];
  listingOrders?: number[];
  order: number;
  name: string;
  condition: string;
  input: string;
  fullCommand: string;
  inputMarkup: string;
  note: string;
  isRevengeAttack: boolean;
  isMovementOnly: boolean;
  hasInputAlternatives: boolean;
  inputVariants: MovelistInputVariant[];
  isThrowInput: boolean;
  attributeTag: string;
  hitClasses: string[];
  effectTags: { code: string; label: string }[];
  mainTip: string;
  lethalHitCondition: string;
  groupIds?: string[];
  commandSets: MovelistCommandSet[];
  nativeLink: NativeLink;
  metrics: PlayerMoveFamilyMetrics;
  evidence: PlayerMoveMetricEvidence;
}

export interface MovelistCategory {
  index: number;
  name: string;
  itemOrders: number[];
}

export interface MovelistGroup {
  id: string;
  kind: "duplicate-move-id" | "input-family" | string;
  reason: string;
  rootOrder: number;
  orders: number[];
  moveIds: number[];
  condition: string;
  baseInput: string;
  displayName: string;
}

export type SourceConfidence = "game-authored" | "native-confirmed" | "native-inferred" | "unknown";
export type NativeLinkStatus = "confirmed" | "heuristic" | "ambiguous" | "unresolved";
export type MetricSource = "game-movelist-table" | "khd-attack-cell" | "khd-static-timeline" | "unknown";
export type MetricEvidenceStatus = "game-authored" | "native-confirmed" | "native-inferred" | "unknown";
export type PlayerMoveFamilyRowSource = "game-movelist-table";

export interface NativeLink {
  status: NativeLinkStatus;
  resolutions: string[];
  definitions: Array<{
    lane: string;
    mainDefinitionId: number;
    fallbackDefinitionId: number;
  }>;
  slots: number[];
  cells: number[];
  attackSlots?: number[];
  attackCells?: number[];
  combatContextStatus?: "resolved" | "unresolved";
  startupTimingStatus?: "resolved" | "unresolved";
  frameEndpointStatus?: "resolved" | "unresolved";
  frameEndpointStatuses?: Partial<
    Record<"block" | "hit" | "counterHit", "resolved" | "unresolved">
  >;
  hitSequenceStatus?: "resolved" | "unresolved";
}

export interface MetricEvidence {
  source: MetricSource;
  status: MetricEvidenceStatus;
}

export interface PlayerMoveFamilyMetrics {
  startup: number | null;
  damage: number[];
  block: string | number | null;
  hit: string | number | null;
  counterHit: string | number | null;
  guardBurst: number | null;
  hitLevels: string[];
}

export type PlayerMoveMetricEvidence = Record<keyof PlayerMoveFamilyMetrics, MetricEvidence>;

export interface PlayerMoveFamilyRow {
  id: string;
  displayCommand: string;
  displayName: string;
  context?: string;
  isThrowInput: boolean;
  source: PlayerMoveFamilyRowSource;
  confidence: SourceConfidence;
  parserMoveOrders: number[];
  nativeLink: NativeLink;
  metrics: PlayerMoveFamilyMetrics;
  evidence: PlayerMoveMetricEvidence;
  notes?: string;
}

export interface PlayerMoveFamilyEdge {
  id: string;
  parentRowId: string;
  childRowId: string;
  relation: string;
  confidence: SourceConfidence;
  reasons: string[];
  source?: string;
}

export interface PlayerMoveFamily {
  id: string;
  cid: string;
  kind?: string;
  rootCommand: string;
  rootName: string;
  context: string;
  confidence: SourceConfidence;
  relations: string[];
  rows: PlayerMoveFamilyRow[];
  edges: PlayerMoveFamilyEdge[];
}

export interface PlayerMoveSummary {
  officialRows: number;
  playerFamilies: number;
  playerRows: number;
  nativeLinkedRows: number;
  nativeUnlinkedRows: number;
  linkStatusCounts: Record<string, number>;
  groupingConfidenceCounts: Record<string, number>;
  metricCoverage: Record<string, number>;
}

export interface PlayerFamilyStats {
  startup: number | null;
  damage: number | null;
  block: string | number | null;
  hit: string | number | null;
  rowCount: number;
  unsafeCount: number;
  plusCount: number;
  launcherCount: number;
}

export interface PlayerDashboard {
  statsByFamily: Record<string, PlayerFamilyStats>;
  fastestFamilyIds: string[];
  unsafeFamilyIds: string[];
  plusFamilyIds: string[];
  launcherFamilyIds: string[];
}

export interface NativeSummary {
  moveCount?: number;
  movelistId?: number;
  totalCells?: number;
  throwCount?: number;
  attackCount?: number;
  headerCount?: number;
  sentinelCount?: number;
  nonDamagingCount?: number;
  slotCount?: number;
  eventRecordCount?: number;
  parsedEventRecordCount?: number;
}

export interface PlayerCharPayload {
  schemaVersion: 2;
  cid: string;
  name: string;
  kind: CharaKind;
  uncertain?: boolean;
  files: Record<string, boolean>;
  nativeSummary: NativeSummary;
  playerMoveFamilies: PlayerMoveFamily[];
  playerMoveSummary: PlayerMoveSummary;
  dashboard: PlayerDashboard;
}

export interface LookupFamilySummary {
  cid: string;
  charName: string;
  kind: CharaKind;
  familyId: string;
  rootCommand: string;
  rootName: string;
  context: string;
  confidence: SourceConfidence;
  relations: string[];
  rowCount: number;
  metrics: PlayerFamilyStats;
  linkStatusCounts: Record<string, number>;
  commandKeys: string[];
  searchText: string;
}

export interface LookupIndex {
  schemaVersion: number;
  chars: {
    cid: string;
    name: string;
    kind: CharaKind;
    uncertain?: boolean;
  }[];
  families: LookupFamilySummary[];
}

export interface RawMovelistRow {
  order: number;
  moveId: number;
  category: number;
  categoryMemberships?: number[];
  listingOrders?: number[];
  name: string;
  condition: string;
  input: string;
  fullCommand: string;
  note: string;
  isMovementOnly: boolean;
  isThrowInput: boolean;
  hasInputAlternatives: boolean;
  hitClasses: string[];
  effectTags: { code: string; label: string }[];
  mainTip: string;
  lethalHitCondition: string;
  groupIds: string[];
  metrics: PlayerMoveFamilyMetrics;
  evidence: PlayerMoveMetricEvidence;
  nativeLink: NativeLink;
}

export interface RawMovelistPayload {
  schemaVersion: 2;
  cid: string;
  name: string;
  kind: CharaKind;
  categories: MovelistCategory[];
  moveGroups: MovelistGroup[];
  rows: RawMovelistRow[];
}

export interface Movelist {
  ryuuhaType: number;
  categories: MovelistCategory[];
  moves: MovelistMove[];
  moveGroups: MovelistGroup[];
  playerMoveFamilies?: PlayerMoveFamily[];
  playerMoveSummary?: PlayerMoveSummary;
}

export interface CharData {
  schemaVersion?: 2;
  cid: string;
  name: string;
  kind: CharaKind;
  uncertain?: boolean;
  files: Record<string, boolean>;
  khd?: KhdPayload;
  khdError?: string;
  movelist?: Movelist;
}

export interface AppSearch {
  me?: string;
  vs?: string;
  q?: string;
}
