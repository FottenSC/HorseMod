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
  activeStart: number;
  activeEnd: number;
  activeFrames: number;
  onBlock: number;
  onHitStanding: number;
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

export interface CommunityFrameData {
  source: "community";
  startup: number | null;
  damage: number[];
  onBlock: string;
  onHit: string;
  onCounterHit: string;
  guardBurst: number | null;
  notes: string;
}

export interface MovelistCommandSet {
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
  communityFrame: CommunityFrameData | null;
  isThrowInput: boolean;
  attributeTag: string;
  hitClasses: string[];
  effectTags: { code: string; label: string }[];
  mainTip: string;
  lethalHitCondition: string;
  groupIds?: string[];
  commandSets: MovelistCommandSet[];
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

export type SourceConfidence =
  | "runtime-validated"
  | "community-confirmed"
  | "native-confirmed"
  | "mixed-supported"
  | "native-inferred"
  | "weak"
  | "conflict"
  | "unknown";

export type TimelineStatus =
  | "resolved"
  | "partial"
  | "native-cell-only"
  | "unresolved";

export type PlayerMoveFamilyRowSource =
  | "community"
  | "movelist"
  | "mixed"
  | "native-inferred";

export interface PlayerMoveFamilyMetrics {
  startup: number | null;
  damage: number[];
  block: string | number | null;
  hit: string | number | null;
  counterHit: string | null;
  hitLevels: string[];
}

export interface PlayerMoveFamilyRow {
  id: string;
  displayCommand: string;
  displayName: string;
  context?: string;
  source: PlayerMoveFamilyRowSource;
  confidence: SourceConfidence;
  parserMoveOrders: number[];
  nativeSlots: number[];
  nativeCells: number[];
  metrics: PlayerMoveFamilyMetrics;
  notes?: string;
  guardBurst?: number | null;
  timelineStatus: TimelineStatus;
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
  rawMoveRows: number;
  playerFamilies: number;
  playerRows: number;
  communityRows: number;
  communityCoveredParserRows: number;
  parserFallbackFamilies: number;
  sourceCounts: Record<string, number>;
  confidenceCounts: Record<string, number>;
  timelineStatusCounts: Record<string, number>;
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
