import { createFileRoute, Link, useNavigate } from "@tanstack/react-router";
import { useMemo } from "react";
import { AttackClassBadge, AttackModifierBadges, EffectTagBadges, RevengeAttackBadge } from "../components/AttackClassBadge";
import type { CharData, SlotEdge, FlatMove } from "../data/types";
import { findInputVariations } from "../lib/moves";

export const Route = createFileRoute("/c/$cid/moves_/$slot")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  // Optional search params disambiguate when multiple movelist entries
  // share the same slotIdx (~1091 collisions across the roster). The
  // Moves tab passes both `move` (= MoveListID) and `order` (= sequential
  // index into movelist.moves) so we can land on the exact entry that
  // was clicked.
  validateSearch: (s: Record<string, unknown>) => ({
    move: typeof s.move === "number" ? s.move
      : typeof s.move === "string" && /^\d+$/.test(s.move) ? Number(s.move)
      : undefined,
    order: typeof s.order === "number" ? s.order
      : typeof s.order === "string" && /^\d+$/.test(s.order) ? Number(s.order)
      : undefined,
  }),
  component: MoveSlotDetail,
});

function MoveSlotDetail() {
  const char = Route.useLoaderData();
  const { cid, slot: slotParam } = Route.useParams();
  const navigate = useNavigate();
  const slotIdx = parseInt(slotParam, 10);
  const khd = char.khd;
  const slot = khd?.slots[slotIdx];

  const outgoing = useMemo(
    () => (khd?.slotEdges ?? []).filter((e) => e.src === slotIdx),
    [khd, slotIdx],
  );
  const incoming = useMemo(
    () => (khd?.slotEdges ?? []).filter((e) => e.dst === slotIdx && e.bank === 0),
    [khd, slotIdx],
  );

  const search = Route.useSearch();
  const flatMove: FlatMove | undefined = useMemo(
    () => khd?.flatMoves.find((m) => m.slot === slotIdx),
    [khd, slotIdx],
  );
  // Pick the canonical movelist entry. Priority:
  //   1. Exact match via `order` (the row's index in movelist.moves) -
  //      the most unambiguous identifier (each row is unique).
  //   2. Match via `move` (= MoveListID) intersected with the slot.
  //   3. Fallback: first movelist entry whose CommandSets reference
  //      this slotIdx. May pick the wrong one in collision cases.
  const movelistEntry = useMemo(() => {
    const ml = char.movelist;
    if (!ml) return undefined;
    if (search.order !== undefined) {
      const exact = ml.moves[search.order];
      if (exact && exact.commandSets.some((cs) => cs.slotIdx === slotIdx)) {
        return exact;
      }
    }
    if (search.move !== undefined) {
      const byMoveId = ml.moves.find(
        (m) => m.moveId === search.move
          && m.commandSets.some((cs) => cs.slotIdx === slotIdx),
      );
      if (byMoveId) return byMoveId;
    }
    return ml.moves.find((m) =>
      m.commandSets.some((cs) => cs.slotIdx === slotIdx),
    );
  }, [char.movelist, slotIdx, search.order, search.move]);

  // Movelist "string" variations of this move (e.g. Astaroth's
  // "Poseidon Tide" 214A and "Poseidon Tide Rush" 214A.A.A.A.A.A are
  // separate MoveListIDs the game never cross-references). See
  // findInputVariations.
  const variations = useMemo(
    () => (char.movelist && movelistEntry
      ? findInputVariations(char.movelist.moves, movelistEntry)
      : []),
    [char.movelist, movelistEntry],
  );

  if (!khd) {
    return <div className="notice">No KHD data for this character.</div>;
  }
  if (!slot) {
    return (
      <div className="notice">
        <strong>No slot at index {slotParam}.</strong>{" "}
        <Link to="/c/$cid/moves" params={{ cid }}>{"<-"} back to all moves</Link>
      </div>
    );
  }

  const cellVariantRows = slot.cellVariants
    .map((cellIdx, variant) => ({
      variant,
      cellIdx,
      cell: cellIdx >= 0 && cellIdx < khd.cells.length ? khd.cells[cellIdx] : null,
    }));
  const validCellRows = cellVariantRows.filter((r) => r.cell !== null);
  // Pick the cell to feature. Priority:
  //  1. The old MainIndex resolution (`commandSets.cellIdx`) when it
  //     targets this slot.
  //  2. The slot's first valid variant when there's no movelist entry.
  let primaryCellIdx = -1;
  if (primaryCellIdx < 0) {
    const movelistCS = movelistEntry?.commandSets.find((cs) => cs.slotIdx === slotIdx);
    const mci = movelistCS?.cellIdx ?? -1;
    if (mci >= 0 && mci < khd.cells.length) {
      primaryCellIdx = mci;
    } else if (!movelistEntry) {
      primaryCellIdx = validCellRows[0]?.cellIdx ?? -1;
    }
  }
  const primaryCell = (primaryCellIdx >= 0 && primaryCellIdx < khd.cells.length)
    ? khd.cells[primaryCellIdx]
    : null;
  const hasKnownInput = flatMove && flatMove.inputs.length > 0
    && flatMove.kinds[0] !== "unknown";
  // 0xFFFF anim = sentinel "trampoline" slot used as an engine input-
  // dispatch entry point. It's a valid path origin for BFS but not a
  // navigable stance, so we hide the "from stance slot N" link.
  const rootIsRealStance = flatMove && flatMove.rootSlot >= 0
    && flatMove.rootAnim !== 0xFFFF;

  return (
    <>
      <p>
        <Link to="/c/$cid/moves" params={{ cid }}>{"<-"} all moves</Link>
      </p>

      <section className="move-hero">
        <div className="move-hero-head">
          <div className="move-hero-title">
            <h1 className="move-hero-name">
              {movelistEntry
                ? movelistEntry.name || `Move ${movelistEntry.moveId}`
                : hasKnownInput
                  ? <InputChips inputs={flatMove!.inputs} kinds={flatMove!.kinds} />
                  : <span className="muted" style={{ fontStyle: "italic" }}>input unknown</span>}
            </h1>
            {movelistEntry?.input && (
              <div className="move-hero-input mono">
                {movelistEntry.condition && (
                  <span className="move-hero-condition">{movelistEntry.condition}</span>
                )}
                <span className="move-hero-buttons">{movelistEntry.input}</span>
              </div>
            )}
          </div>
          <div className="move-hero-badges">
            {/* DA_MoveListTable EffectTag pills - the game's own
                movelist property icons (Lethal Hit, Break Attack,
                Unblockable, etc.). Authoritative; shown first. */}
            {movelistEntry && movelistEntry.effectTags.length > 0 && (
              <EffectTagBadges tags={movelistEntry.effectTags} />
            )}
            {movelistEntry?.isRevengeAttack && <RevengeAttackBadge />}
            {primaryCell?.role === "Attack" && (
              <>
                <AttackClassBadge value={primaryCell.class} />
                <AttackModifierBadges cell={primaryCell} />
              </>
            )}
            {/* Bit 0x10 of wU16InputCond promotes the move to MoveType=4
                ("Wire/Special-FX"). In practice this fires on Critical
                Edges, Soul Charges, command throws, Lethal Hits, and
                stance specials - the "fancy" moves a player typically
                wants to identify at a glance. */}
            {primaryCell && (primaryCell.inputCond & 0x10) !== 0 && (
              <span
                className="chip-small kind-buttons"
                title="Engine treats this as MoveType=4 (Wire) and routes its hit-spark through the special-VFX table - typical for CE / Soul Charge / command throws / Lethal Hits / stance specials"
              >
                Special
              </span>
            )}
          </div>
        </div>

        {movelistEntry?.note && (
          <p className="move-hero-note">{movelistEntry.note}</p>
        )}

        {/* DA_MoveListTable metadata: per-hit class sequence, gameplay
            tip, and the Lethal Hit trigger condition. Authoritative
            in-game movelist data. */}
        {movelistEntry && (movelistEntry.hitClasses.length > 0
          || movelistEntry.mainTip || movelistEntry.lethalHitCondition) && (
          <div className="move-hero-meta">
            {movelistEntry.hitClasses.length > 0 && (
              <div className="move-meta-row">
                <span className="move-meta-label">Hits</span>
                <span className="move-meta-value">
                  {movelistEntry.hitClasses.join(" - ")}
                  {movelistEntry.hitClasses.length > 1 && (
                    <span className="muted">
                      {" "}({movelistEntry.hitClasses.length}-hit)
                    </span>
                  )}
                </span>
              </div>
            )}
            {movelistEntry.mainTip && (
              <div className="move-meta-row">
                <span className="move-meta-label">Tip</span>
                <span className="move-meta-value">{movelistEntry.mainTip}</span>
              </div>
            )}
            {movelistEntry.lethalHitCondition && (
              <div className="move-meta-row">
                <span className="move-meta-label move-meta-lh">Lethal Hit</span>
                <span className="move-meta-value">{movelistEntry.lethalHitCondition}</span>
              </div>
            )}
          </div>
        )}

        {primaryCell?.role === "Attack" ? (
          <div className="move-summary-stats">
            <div className="stat-block">
              <div className="stat-label" title="Base damage authored on the cell -- runtime charge/SC multipliers not captured">Damage</div>
              <div className="stat-value">{primaryCell.damage}</div>
            </div>
            <div className="stat-block">
              <div className="stat-label" title="cell.wI16MasterWindowStart -- first frame the cell is active">Startup</div>
              <div className="stat-value">i{primaryCell.activeStart}</div>
            </div>
            <div className="stat-block">
              <div className="stat-label" title="Block stun from parsed khd cell">On Block</div>
              <div className="stat-value">{primaryCell.onBlock}</div>
            </div>
            <div className="stat-block">
              <div className="stat-label" title="Standing hit stun from parsed khd cell">On Hit</div>
              <div className="stat-value">{primaryCell.onHitStanding}</div>
            </div>
            <div className="stat-block">
              <div className="stat-label" title="Counter-hit result from optional community frame-data. KHD exports do not currently derive this field.">On CH</div>
              <div className="stat-value">{movelistEntry?.communityFrame?.onCounterHit || "-"}</div>
            </div>
            <div className="stat-block">
              <div className="stat-label" title="activeEnd -- activeStart + 1">Active</div>
              <div className="stat-value">{primaryCell.activeFrames}<span className="stat-unit">f</span></div>
            </div>
            <div className="stat-block">
              <div className="stat-label" title="Animation length in frames (slot.flAnimLength_30)">Anim len</div>
              <div className="stat-value">{Math.round(slot.animLength)}<span className="stat-unit">f</span></div>
            </div>
          </div>
        ) : primaryCell ? (
          <div className="muted" style={{ marginTop: 8 }}>
            {primaryCell.role === "Header" && "Header cell - fallback variant, frame data not meaningful."}
            {primaryCell.role === "NonDamaging" && "Non-damaging cell (stance entry / GI / parry)."}
            {primaryCell.role === "Sentinel" && "Sentinel cell (cleared / inactive)."}
          </div>
        ) : movelistEntry?.isMovementOnly ? (
          <div className="muted" style={{ marginTop: 8 }}>
            Movement / stance entry - pure-direction input with no hit cell.
            The animation slot is shared with sibling attack moves; their
            hit cells don't apply here.
          </div>
        ) : (
          <div className="muted" style={{ marginTop: 8 }}>
            No attack cell on this slot - it's a stance, transition, or movement state.
          </div>
        )}
      </section>

      {variations.length > 0 && (
        <section className="detail-card variation-card">
          <h2 className="card-title">Variations</h2>
          <p className="card-sub">
            Same input, extended. Bandai lists each as its own movelist
            entry - pressing further inputs branches the move and can
            change its damage / on-hit behaviour (e.g. a press-once
            version that shifts to an attack throw vs a press-again rush).
          </p>
          <ul className="variation-list">
            {variations.map(({ move: v, relation }) => {
              const vslot = v.commandSets[0]?.slotIdx ?? -1;
              const inner = (
                <>
                  <span className="mono variation-input">{v.input}</span>
                  <span className="variation-name">{v.name || `Move ${v.moveId}`}</span>
                  <span className={`badge ${relation === "base" ? "badge-other" : "badge-mid"}`}>
                    {relation === "base" ? "base move" : "follow-up"}
                  </span>
                  {v.effectTags.length > 0 && <EffectTagBadges tags={v.effectTags} />}
                </>
              );
              return (
                <li key={v.order}>
                  {vslot >= 0 ? (
                    <Link
                      to="/c/$cid/moves/$slot"
                      params={{ cid, slot: String(vslot) }}
                      search={{ move: v.moveId, order: v.order }}
                      className="variation-row"
                    >
                      {inner}
                    </Link>
                  ) : (
                    <span className="variation-row">{inner}</span>
                  )}
                </li>
              );
            })}
          </ul>
        </section>
      )}

      {primaryCell?.role === "Attack" && (
        <div className="detail-cards">
          <section className="detail-card">
            <h2 className="card-title">Hit reactions</h2>
            <p className="card-sub">Raw stun frames, by defender stance. Not frame advantage.</p>
            <table className="kv-table">
              <tbody>
                <tr>
                  <td>Standing hit</td>
                  <td className="num"><strong>{primaryCell.onHitStanding}</strong>f</td>
                </tr>
                <tr>
                  <td>Standing - air</td>
                  <td className="num">{primaryCell.onHitStandingAir}f</td>
                </tr>
                <tr>
                  <td>Crouching hit</td>
                  <td className="num">{primaryCell.onHitCrouchNormal}f</td>
                </tr>
                <tr>
                  <td>Crouching - air</td>
                  <td className="num">{primaryCell.onHitCrouchAir}f</td>
                </tr>
                <tr className="kv-divider">
                  <td>On block</td>
                  <td className="num">{primaryCell.onBlock}f</td>
                </tr>
              </tbody>
            </table>
          </section>

          <section className="detail-card">
            <h2 className="card-title">Range &amp; hitbox</h2>
            <p className="card-sub">Engine units. inf = sentinel -127 (no range gate).</p>
            <table className="kv-table">
              <tbody>
                <tr>
                  <td>Standing</td>
                  <td className="num mono">
                    {primaryCell.rangeStandMin === -127 && primaryCell.rangeStandMax === -127
                      ? "inf"
                      : `${primaryCell.rangeStandMin} .. ${primaryCell.rangeStandMax}`}
                  </td>
                </tr>
                <tr>
                  <td>Crouching</td>
                  <td className="num mono">
                    {primaryCell.rangeCrouchMin === -127 && primaryCell.rangeCrouchMax === -127
                      ? "inf"
                      : `${primaryCell.rangeCrouchMin} .. ${primaryCell.rangeCrouchMax}`}
                  </td>
                </tr>
                <tr className="kv-divider">
                  <td>Reach gate</td>
                  <td className="num mono">{primaryCell.reachExtraGate}</td>
                </tr>
                <tr>
                  <td>Hitbox group</td>
                  <td className="num mono">0x{primaryCell.hitboxGroup.toString(16).padStart(4, "0")}</td>
                </tr>
              </tbody>
            </table>
          </section>

          <section className="detail-card">
            <h2 className="card-title">Engine fields</h2>
            <p className="card-sub">Less-common cell fields - useful for modders.</p>
            <table className="kv-table">
              <tbody>
                <tr>
                  <td>Move type</td>
                  <td className="mono">{primaryCell.moveType}</td>
                </tr>
                <tr>
                  <td>Anim kind</td>
                  <td className="mono">{primaryCell.animKind}</td>
                </tr>
                <tr>
                  <td>Reaction id</td>
                  <td className="num mono">{primaryCell.reactionIdStanding}</td>
                </tr>
                <tr>
                  <td>Reaction id (air)</td>
                  <td className="num mono">{primaryCell.reactionIdAir}</td>
                </tr>
                <tr>
                  <td>Throw-escape id</td>
                  <td className="num mono">{primaryCell.throwEscapeId}</td>
                </tr>
                <tr className="kv-divider">
                  <td>Attack flags</td>
                  <td className="mono" style={{ fontSize: 11 }}>
                    0x{primaryCell.attackFlags.toString(16).padStart(4, "0")}
                  </td>
                </tr>
                <tr>
                  <td colSpan={2} className="muted mono" style={{ fontSize: 11, paddingTop: 0 }}>
                    {primaryCell.attackFlagsDecoded}
                  </td>
                </tr>
              </tbody>
            </table>
          </section>
        </div>
      )}

      {movelistEntry?.hasInputAlternatives && movelistEntry.inputVariants.length > 0 && (
        <section className="detail-section">
          <h2>Direction variants</h2>
          <p className="muted" style={{ fontSize: 12, lineHeight: 1.5 }}>
            Bandai's data groups <code className="mono">{movelistEntry.input}</code>{" "}
            under one entry, but the stance dispatcher routes each
            direction-modified input to a different cell with different
            stats. These are the candidate variants found from the parent
            dispatcher's direction-tagged sibling edges. Heuristic - the
            predicate hints don't exactly match individual inputs, treat
            them as <em>possible alternate stats</em>.
          </p>
          <table className="moves-table">
            <thead>
              <tr>
                <th>Predicate</th>
                <th>Class</th>
                <th>Dmg</th>
                <th>Startup</th>
                <th>Active</th>
                <th>Hit stun</th>
                <th>Blk stun</th>
                <th>Cell - Slot</th>
              </tr>
            </thead>
            <tbody>
              {movelistEntry.inputVariants.map((v) => {
                const c = khd.cells[v.cellIdx];
                if (!c) return null;
                return (
                  <tr
                    key={`${v.slotIdx}-${v.cellIdx}`}
                    onClick={() => navigate({
                      to: "/c/$cid/moves/$slot",
                      params: { cid, slot: String(v.slotIdx) },
                      search: { move: undefined, order: undefined },
                    })}
                  >
                    <td className="mono" style={{ fontSize: 12 }}>{v.hint}</td>
                    <td>
                      <AttackClassBadge value={c.class} />
                      <AttackModifierBadges cell={c} />
                    </td>
                    <td className="num">{c.damage}</td>
                    <td className="num">i{c.activeStart}</td>
                    <td className="num muted">{c.activeFrames}f</td>
                    <td className="num">{c.onHitStanding}f</td>
                    <td className="num">{c.onBlock}f</td>
                    <td className="num mono muted" style={{ fontSize: 11 }}>
                      #{v.cellIdx} - {v.slotIdx}
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </section>
      )}

      {validCellRows.length > 1 && (
        <section className="detail-section">
          <h2>Hit variants</h2>
          <p className="muted" style={{ fontSize: 13 }}>
            This slot has multiple animation variants with different attack data.
          </p>
          <table className="moves-table">
            <thead>
              <tr>
                <th>Variant</th>
                <th>Class</th>
                <th>Damage</th>
                <th>Startup</th>
                <th>On-hit</th>
                <th>On-block</th>
              </tr>
            </thead>
            <tbody>
              {validCellRows.map(({ variant, cellIdx, cell }) => (
                <tr
                  key={variant}
                  onClick={() => navigate({
                    to: "/c/$cid/cells/$idx",
                    params: { cid, idx: String(cellIdx) },
                  })}
                >
                  <td className="num mono muted">v{variant}</td>
                  <td>
                    {cell!.role === "Attack" ? (
                      <>
                        <AttackClassBadge value={cell!.class} />
                        <AttackModifierBadges cell={cell!} />
                      </>
                    ) : (
                      <span className="muted">{cell!.role}</span>
                    )}
                  </td>
                  <td className="num">{cell!.damage}</td>
                  <td className="num">{cell!.role === "Attack" ? `i${cell!.activeStart}` : "-"}</td>
                  <td className="num">{cell!.onHitStanding}f</td>
                  <td className="num">{cell!.onBlock}f</td>
                </tr>
              ))}
            </tbody>
          </table>
        </section>
      )}

      <details className="engine-details">
        <summary>
          Engine details {outgoing.length > 0 && <> - {outgoing.length} outgoing</>}
          {incoming.length > 0 && <> - {incoming.length} incoming</>}
        </summary>
        <div className="engine-fields">
          <p className="muted" style={{ fontSize: 13 }}>
            slot <code className="mono">{slot.idx}</code>{" "}
            - anim <code className="mono">{slot.animationIndex}</code>
            {" "}- length <code className="mono">{slot.animLength.toFixed(1)}f</code>
            {primaryCell && (
              <>
                {" "}-{" "}
                <Link to="/c/$cid/cells/$idx" params={{ cid, idx: String(primaryCellIdx) }}>
                  full frame data for cell #{primaryCellIdx} {"->"}
                </Link>
              </>
            )}
          </p>

          {outgoing.length > 0 && (
            <>
              <h3 style={{ margin: "1em 0 0.4em", fontSize: 13, color: "#98a0a8" }}>
                Outgoing transitions
              </h3>
              <EdgeTable edges={outgoing} char={char} cid={cid} direction="out" navigate={navigate} />
            </>
          )}
          {incoming.length > 0 && (
            <>
              <h3 style={{ margin: "1em 0 0.4em", fontSize: 13, color: "#98a0a8" }}>
                Incoming transitions
              </h3>
              <EdgeTable edges={incoming} char={char} cid={cid} direction="in" navigate={navigate} />
            </>
          )}
        </div>
      </details>

      <div style={{ marginTop: 24, display: "flex", gap: 12, fontSize: 13 }}>
        {slotIdx > 0 && (
          <Link to="/c/$cid/moves/$slot" params={{ cid, slot: String(slotIdx - 1) }} search={{ move: undefined, order: undefined }}>
            {"<-"} prev slot
          </Link>
        )}
        {slotIdx + 1 < khd.slotCount && (
          <Link to="/c/$cid/moves/$slot" params={{ cid, slot: String(slotIdx + 1) }} search={{ move: undefined, order: undefined }}>
            next slot {"->"}
          </Link>
        )}
      </div>
    </>
  );
}

function InputChips({ inputs, kinds }: { inputs: string[]; kinds: string[] }) {
  return (
    <span style={{ display: "inline-flex", gap: 4, alignItems: "center", flexWrap: "wrap" }}>
      {inputs.map((inp, i) => (
        <span key={i} style={{ display: "inline-flex", alignItems: "center", gap: 4 }}>
          {i > 0 && <span className="muted">&gt;</span>}
          <span className={`chip-small kind-${kinds[i] ?? "other"}`}>{inp}</span>
        </span>
      ))}
    </span>
  );
}

function EdgeTable({
  edges, char, cid, direction, navigate,
}: {
  edges: SlotEdge[];
  char: CharData;
  cid: string;
  direction: "in" | "out";
  navigate: ReturnType<typeof useNavigate>;
}) {
  const otherCol = direction === "out" ? "dst" : "src";
  return (
    <table className="moves-table">
      <thead>
        <tr>
          <th>Input</th>
          <th>{direction === "out" ? "-> Slot" : "<- Slot"}</th>
          <th>Cell</th>
        </tr>
      </thead>
      <tbody>
        {edges.map((e, i) => {
          const otherSlotIdx = e[otherCol as "src" | "dst"];
          const isCrossBank = e.bank !== 0;
          const otherSlot = isCrossBank ? null : char.khd?.slots[otherSlotIdx];
          const primaryCellIdx =
            otherSlot?.cellVariants.find((c) => c >= 0 && c < (char.khd?.cells.length ?? 0)) ?? -1;
          const primaryCell =
            primaryCellIdx >= 0 ? char.khd?.cells[primaryCellIdx] : null;
          return (
            <tr
              key={i}
              onClick={isCrossBank ? undefined : () => navigate({
                to: "/c/$cid/moves/$slot",
                params: { cid, slot: String(otherSlotIdx) },
                search: { move: undefined, order: undefined },
              })}
              style={isCrossBank ? { cursor: "default", opacity: 0.7 } : undefined}
            >
              <td>
                <span className={`chip-small kind-${e.kind}`}>{e.input}</span>
              </td>
              <td className="num mono">
                {otherSlotIdx}
                {isCrossBank && (
                  <span className="muted" style={{ fontSize: 10 }}> (bank {e.bank})</span>
                )}
              </td>
              <td>
                {primaryCell ? (
                  <span className="mono">
                    #{primaryCellIdx}{" "}
                    <span className="muted">
                      ({primaryCell.class}, {primaryCell.damage}dmg)
                    </span>
                  </span>
                ) : (
                  <span className="muted">-</span>
                )}
              </td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}





