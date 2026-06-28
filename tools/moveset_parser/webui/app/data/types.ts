// Shape of the JSON payloads produced by `export_webui_data.py`.

export type CharaKind = "base" | "dlc" | "boss" | "shared" | "unknown";

export interface CharSummary {
  cid: string;
  name: string;
  kind: CharaKind;
  uncertain?: boolean;
  files: Record<"khd" | "mot" | "dtp" | "atkhit" | "bodyhit" | "yararehit", boolean>;
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

export type AttackClass =
  | "Mid" | "High" | "Low" | "Unblockable" | "Throw" | "Inactive" | string;

export interface Cell {
  idx: number;
  role: CellRole;
  class: AttackClass;
  moveType: string;
  animKind: string;
  damage: number;
  activeStart: number;
  activeEnd: number;
  activeFrames: number;
  onBlock: number;
  onHitStanding: number;
  onHitStandingAir: number;
  onHitCrouchNormal: number;
  onHitCrouchAir: number;
  reactionIdStanding: number;
  reactionIdAir: number;
  throwEscapeId: number;
  rangeStandMin: number;
  rangeStandMax: number;
  rangeCrouchMin: number;
  rangeCrouchMax: number;
  reachExtraGate: number;
  attackFlags: number;
  attackFlagsDecoded: string;
  // Orthogonal modifiers on top of `class`. Sourced from the engine
  // ELuxBattleAttackFlags enum (Ghidra-verified).
  // `isBreakAttack`: still blockable, but the block reaction is the
  //   stagger variant (engine reaction 3 + chara+0x88=1).
  // `isGiImmune`: cell has bit 0x200 set - defender cannot Guard Impact
  //   this attack. Most true Unblockables also set this; Break Attacks
  //   set this AND have block bits.
  // `isGuardBypass`: cell has BlockBypass_GuardBreak (bit 0x004) -
  //   bypasses block when defender is in guard-broken substate
  //   (engine reaction 7 path).
  isBreakAttack: boolean;
  isGiImmune: boolean;
  isGuardBypass: boolean;
  extraStateFlags: number;
  stunRecoil: number;
  inputCond: number;
  hitboxGroup: number;
  passthroughA: number;
  passthroughC: number;
  slotMask: string; // u64 as decimal string
}

export interface SlotBytecodeSummary {
  offset: number;
  instructionCount: number;
  lengthBytes: number;
  truncated: boolean;
  callconds: Record<string, number>;
  facingEffects: MoveEffectEvent[];
}

export interface Slot {
  idx: number;
  animationIndex: number;
  animLength: number;
  totalFrames: number;
  playbackSpeed60ths: number;
  playbackSpeed: number;
  hitWindowStart: number;
  cellVariants: number[];
  attackCellRefs: number[];
  throwCellRefs: number[];
  bytecodeOffset: number;
  bytecode: SlotBytecodeSummary | null;
}

export interface ThrowCell {
  idx: number;
  damage: number;
  aux: number;
  scaling: number;
}

export interface EventRecord {
  idx: number;
  offset: number;
  packedMoveId: number;
  resolvedSlot: number | null;
  eventKind: number;
  eventKindName: string;
  field08: number;
  shapeFlags: number;
  offsetX: number;
  offsetY: number;
  offsetZ: number;
  field1C: number;
  field20: number;
  field24: number;
  radiusScale: number;
  field2C: number;
  // Compatibility aliases retained by the exporter.
  key: number;
  typeTag: number;
  typeName: string;
}

export interface HitRecord {
  idx: number;
  tag: number;
  tagName: "Sphere" | "Area" | "FixArea" | "?";
  slot: number;
  flags: number;
  x: number;
  y: number;
  z: number;
  radius: number;
  idLink: number;
}

export interface HitFile {
  recordCount: number;
  records: HitRecord[];
}

export interface SlotEdge {
  src: number;
  dst: number;
  bank: number;
  rawId: number;
  input: string;
  // "buttons" / "direction" / "command" - user-initiated;
  // "auto" / "frame" / "stance" / "from-move" / "range" - state-driven;
  // "other" / "indirect" - unknown / load-var.
  kind: string;
  subOp: number | null;
  args: (number | null)[];
  indirect: boolean;
  callcond: number;
  pc: number;
}

export interface StanceRoot {
  slot: number;
  anim: number;
  label: string;
  distinctInputs: number;
  totalOutgoing: number;
  incoming: number;
}

// A flat "this is one user-facing move" record: a cell-bearing slot
// reached by some input path from a stance root.
export interface FlatMove {
  slot: number;                  // the cell-bearing slot
  anim: number;
  cell: number;                  // its primary attack cell (always >= 0)
  inputs: string[];              // input strings, oldest-first
  kinds: string[];               // kind per input
  slots: number[];               // intermediate slots visited
  rootSlot: number;              // stance root we started from
  rootAnim: number;
}

export interface KhdPayload {
  magic: string;
  moveCount: number;
  movelistId: number;
  sectionOffsets: number[];
  attackBlockOffset: number;
  throwBlockOffset: number;
  eventRecordTableOffset: number;
  eventRecordCount: number;
  parsedEventRecordCount: number;
  eventRecordPrefixBytes: number;
  miscBlockOffset: number;
  firstCancelOffset: number;
  totalCells: number;
  throwCount: number;
  attackCount: number;
  headerCount: number;
  sentinelCount: number;
  nonDamagingCount: number;
  cells: Cell[];
  throws: ThrowCell[];
  eventRecords: EventRecord[];
  slotCount: number;
  slots: Slot[];
  cellToSlots: Record<string, [number, number][]>; // cellIdx -> [slotIdx, variant][]
  throwToSlots: Record<string, [number, number][]>; // throwIdx -> [slotIdx, variant][]
  slotEdges: SlotEdge[];
  stanceRoots: StanceRoot[];
  flatMoves: FlatMove[];
}

export interface CharData {
  cid: string;
  name: string;
  kind: CharaKind;
  uncertain?: boolean;
  files: Record<string, boolean>;
  khd?: KhdPayload;
  khdError?: string;
  atkhit?: HitFile;
  bodyhit?: HitFile;
  yararehit?: HitFile;
  mot?: { count: number; fileSize: number; emptySections: number };
  dtp?: { count: number; fileSize: number; emptySections: number };
  movelist?: Movelist;
}

// In-game canonical movelist - sourced from UE4 DataAsset
// (DA_MovePlayData_<cid>.uexp) + Game.archive localization.
export interface MovelistCommandSet {
  mainIndex: number;       // raw DA_MovePlayData field - indexes the
                           // movelist-DEMO command player, NOT a cell
  // Raw CommandSet.IntroIndex - the move's LEAD-IN cell: 8-Way Run
  // direction, While-crouching/rising state, or stance entry. Shared
  // across every move with the same lead-in; it is NOT one of the
  // move's hits (a 1-hit move still has one). The UI does not read it
  // - the lead-in is already conveyed by `condition` + `command`.
  introIndex: number;
  // LEGACY MainIndex resolution - UNRELIABLE (the cell it picks is sometimes wrong).
  // Kept only as a navigation fallback. Prefer commandSet candidates
  // with real Attack-role cells from this payload when rendering stats.
  cellIdx: number;         // resolved attack-cell index (-1 if none)
  slotIdx: number;         // resolved slot index (for navigation, -1 if none)
  resolution:
    | "cell-direct"
    | "slot-overrides-direct"
    | "cell-direct-invalid-startup"
    | "cell"
    | "slot"
    | "slot-no-cell"
    | "movement-only"
    | "none";
  tracking: MoveTrackingSummary;
}

export interface MoveEffectEvent {
  opcode: number | null;
  opcodeHex: string | null;
  kind: string;
  args: (number | null)[];
  callcond: number;
  pc: number;
  isFacingRelated: boolean;
  targetWeight: number | null;
  rampSelector: number | null;
}

export interface MoveTrackingSummary {
  hasFacingCommit: boolean;
  hasRetrackRamp: boolean;
  maxTargetWeight: number | null;
  events: MoveEffectEvent[];
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

// One direction-modified alternate cell found by the exporter's
// dispatcher-sibling lookup. Heuristic only - see
// `_find_dispatcher_variants` in export_webui_data.py. Predicate hints
// like "(back+forward)" are NOT exact-match labels for "4B+K vs 6B+K";
// the engine's direction predicates are too loose to pin a variant to
// a single input. Treat them as "possible alternate stats for this
// move", not as ground-truth frame data.
export interface MovelistInputVariant {
  cellIdx: number;
  slotIdx: number;
  hint: string;            // bytecode predicate label, e.g. "(back+forward)"
  kind: string;            // predicate kind, e.g. "direction"
}

// (DA_MovePlayData.MainIndex addresses the demo command-player, not the
export interface MovelistMove {
  moveId: number;          // MoveListID from DA_MovePlayData
  category: number;        // index into Movelist.categories
  order: number;           // sequential order in the in-game listing
  name: string;            // localized display name (e.g. "Heaven Cannon")
  // Split for separate UI columns. "During Mist B.B.B.B" becomes
  // condition="During Mist", input="B.B.B.B". `fullCommand` preserves
  // the combined string for users who want the raw form.
  condition: string;
  input: string;           // canonical button portion (e.g. "3B", "66K", "A+B+K")
  fullCommand: string;     // condition + input joined ("During Mist B.B.B.B")
  inputMarkup: string;     // raw {cmd_X} markup from the game's archive
  note: string;            // usage hint (often empty)
  // Derived from localized NoteTextID text containing "Revenge attack".
  // There is no observed DA_MoveListTable EffectTag for this; `RE` means
  // Reversal Edge, not Revenge.
  isRevengeAttack: boolean;
  // True when the input is direction-only (no A/B/K/G) - sidesteps,
  // backsteps, stance entries, dashes. The exporter clears the
  // commandSets' cellIdx for these so we don't pretend the slot's
  // borrowed attack-cell is the move's hit. UI should render without
  // damage / startup / class columns.
  isMovementOnly: boolean;
  // True when the localization presents this entry with `|`-separated
  // alternative inputs (e.g. "B+K|6B+K|4B+K"). Bandai groups these
  // under one MoveListItem, but the engine dispatcher routes each
  // direction to a different cell with different stats - the
  // `inputVariants` array carries those alternate cells when found.
  hasInputAlternatives: boolean;
  inputVariants: MovelistInputVariant[];
  tracking: MoveTrackingSummary;
  communityFrame: CommunityFrameData | null;
  // True when the canonical input contains `+G` (e.g. A+G, 6A+G,
  // 46A+G). The resolved cell is typically the STRIKE-PHASE / whiff
  // cell, not the throw cinematic damage cell - the actual throw cell
  // is reached via engine-mediated yarareId state transitions that
  // aren't visible in static bytecode edges. UI should surface a
  // "Throw" hint so users don't read the strike-phase stats as the
  // throw's actual damage.
  isThrowInput: boolean;
  // ---- DA_MoveListTable metadata (authoritative; the in-game movelist
  // UI's own per-move data) -------------------------------------------
  // `attributeTag`: raw per-hit class string, e.g. "M.M.M" or "H.M".
  // `hitClasses`: parsed list - ["Mid","Mid","Mid"]. Length = hit count.
  // `effectTags`: property icons the in-game movelist shows. Each is
  //   { code, label } - codes: LH (Lethal Hit), BA (Break Attack),
  //   GI (Guard Impact), UA (Unblockable Attack), RE (Reversal Edge),
  //   TH (Throw), SC (Soul Charge), SS (Stance Shift),
  //   SGF/SGH/SGQ (Soul Gauge Full/Half/Quarter requirement).
  //   This is more reliable than deriving class from cell flags.
  // `mainTip`: a gameplay hint string ("Combo starter", "Knocks down
  //   opponent", ...) - empty for most moves.
  // `lethalHitCondition`: the Lethal Hit trigger text, e.g. "Triggers
  //   upon impact counter" - only set for LH moves.
  attributeTag: string;
  hitClasses: string[];
  effectTags: { code: string; label: string }[];
  mainTip: string;
  lethalHitCondition: string;
  groupIds: string[];
  commandSets: MovelistCommandSet[];
}

export interface MovelistCategory {
  index: number;
  name: string;           // localized tab name, e.g. "Horizontal Attacks"
  itemOrders: number[];   // indices into Movelist.moves
}

export interface MovelistGroup {
  id: string;
  kind: "duplicate-move-id" | "input-family";
  reason: string;
  rootOrder: number;
  orders: number[];
  moveIds: number[];
  condition: string;
  baseInput: string;
  displayName: string;
}

export interface Movelist {
  ryuuhaType: number;
  categories: MovelistCategory[];
  moves: MovelistMove[];
  moveGroups: MovelistGroup[];
}
