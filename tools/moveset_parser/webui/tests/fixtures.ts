import type { Cell, FlatMove, MovelistMove } from "../app/data/types";

/** Build a minimal Cell stub for tests. */
export function makeCell(p: Partial<Cell> & { idx: number }): Cell {
  return {
    idx: p.idx,
    role: p.role ?? "Attack",
    class: p.class ?? "Mid",
    moveType: p.moveType ?? "Strike",
    animKind: p.animKind ?? "Neutral",
    damage: p.damage ?? 10,
    activeStartCoordinate: p.activeStartCoordinate ?? 14,
    activeEndCoordinate: p.activeEndCoordinate ?? 16,
    activeFrames: p.activeFrames ?? 3,
    blockStunFrames: p.blockStunFrames ?? 10,
    baseHitStunFrames: p.baseHitStunFrames ?? 14,
    specialHitStunFrames: p.specialHitStunFrames ?? 14,
    alternatePostureBaseHitStunFrames: p.alternatePostureBaseHitStunFrames ?? 14,
    alternatePostureSpecialHitStunFrames: p.alternatePostureSpecialHitStunFrames ?? 14,
    reactionIdBaseContact: p.reactionIdBaseContact ?? 0,
    reactionIdSpecialContact: p.reactionIdSpecialContact ?? 0,
    throwEscapeId: p.throwEscapeId ?? 0,
    rangeStandMin: p.rangeStandMin ?? -50,
    rangeStandMax: p.rangeStandMax ?? 50,
    rangeCrouchMin: p.rangeCrouchMin ?? -50,
    rangeCrouchMax: p.rangeCrouchMax ?? 50,
    reachExtraGate: p.reachExtraGate ?? 0,
    attackFlags: p.attackFlags ?? 0,
    attackFlagsDecoded: p.attackFlagsDecoded ?? "",
    extraStateFlags: p.extraStateFlags ?? 0,
    stunRecoil: p.stunRecoil ?? 0,
    inputCond: p.inputCond ?? 0,
    hitboxGroup: p.hitboxGroup ?? 0,
    passthroughA: p.passthroughA ?? 0,
    passthroughC: p.passthroughC ?? 0,
    slotMask: p.slotMask ?? "0",
  };
}

/** Build a minimal FlatMove stub. */
export function makeMove(p: Partial<FlatMove> & { slot: number; cell: number }): FlatMove {
  return {
    slot: p.slot,
    anim: p.anim ?? 100,
    cell: p.cell,
    inputs: p.inputs ?? [],
    kinds: p.kinds ?? ["unknown"],
    slots: p.slots ?? [p.slot],
    rootSlot: p.rootSlot ?? -1,
    rootAnim: p.rootAnim ?? -1,
  };
}

/** Build a minimal MovelistMove stub. */
export function makeMovelistMove(
  p: Partial<MovelistMove> & { moveId: number; order: number },
): MovelistMove {
  return {
    moveId: p.moveId,
    category: p.category ?? 0,
    order: p.order,
    name: p.name ?? `Move ${p.moveId}`,
    condition: p.condition ?? "",
    input: p.input ?? "",
    fullCommand: p.fullCommand ?? p.input ?? "",
    inputMarkup: p.inputMarkup ?? "",
    note: p.note ?? "",
    isRevengeAttack: p.isRevengeAttack ?? false,
    isMovementOnly: p.isMovementOnly ?? false,
    hasInputAlternatives: p.hasInputAlternatives ?? false,
    inputVariants: p.inputVariants ?? [],
    isThrowInput: p.isThrowInput ?? false,
    attributeTag: p.attributeTag ?? "",
    hitClasses: p.hitClasses ?? [],
    effectTags: p.effectTags ?? [],
    mainTip: p.mainTip ?? "",
    lethalHitCondition: p.lethalHitCondition ?? "",
    groupIds: p.groupIds ?? [],
    commandSets: p.commandSets ?? [],
  };
}
