import { createFileRoute, Link } from "@tanstack/react-router";
import { AttackClassBadge, AttackModifierBadges, CellRoleBadge } from "../components/AttackClassBadge";
import { StatValue } from "../components/FrameData";
import type { CharData } from "../data/types";

export const Route = createFileRoute("/c/$cid/cells_/$idx")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: MoveDetail,
});

function MoveDetail() {
  const char = Route.useLoaderData();
  const { cid, idx } = Route.useParams();
  const cellIdx = parseInt(idx, 10);
  const cell = char.khd?.cells[cellIdx];

  if (!cell) {
    return <div className="notice">No cell at index {idx}.</div>;
  }

  const usedBy = char.khd?.cellToSlots[String(cellIdx)] ?? [];
  const rangeStand = formatRange(cell.rangeStandMin, cell.rangeStandMax);
  const rangeCrouch = formatRange(cell.rangeCrouchMin, cell.rangeCrouchMax);

  return (
    <>
      <p>
        <Link to="/c/$cid/cells" params={{ cid }}>← all cells</Link>
        {"  ·  "}
        <Link to="/c/$cid/moves" params={{ cid }}>moves</Link>
        {cellIdx > 0 ? (
          <>
            {"  ·  "}
            <Link to="/c/$cid/cells/$idx" params={{ cid, idx: String(cellIdx - 1) }}>← prev</Link>
          </>
        ) : null}
        {cellIdx + 1 < (char.khd?.cells.length ?? 0) ? (
          <>
            {"  ·  "}
            <Link to="/c/$cid/cells/$idx" params={{ cid, idx: String(cellIdx + 1) }}>next →</Link>
          </>
        ) : null}
      </p>

      {cell.role !== "Attack" && (
        <div className="info">
          <strong>Cell role: {cell.role}.</strong>{" "}
          {cell.role === "Header" &&
            "This cell has damage = 0 and an anomalous flag pattern. It's a fallback for slot variants that don't map to a real attack — the attack-specific fields below don't carry their normal meaning here."}
          {cell.role === "NonDamaging" &&
            "Damage = 0 but the cell has sensible flags. Used for stance entries / transitions / GI / parry — frame data may still be meaningful, but damage and hitstun aren't."}
          {cell.role === "Sentinel" &&
            "Cleared / inactive slot (wU16HitboxGroupBitfield = 0xFFFF). The other fields are uninitialised."}
        </div>
      )}

      <section className="move-hero">
        <h1 className="move-hero-name">
          {char.name} <span className="muted">cell {cell.idx}</span>
        </h1>
        <div style={{ display: "flex", gap: 10, alignItems: "center", marginTop: 4 }}>
          {cell.role === "Attack" ? (
            <>
              <AttackClassBadge value={cell.class} />
              <AttackModifierBadges cell={cell} />
            </>
          ) : (
            <CellRoleBadge value={cell.role} />
          )}
          <span className="muted">·</span>
          <span className="muted">move type: {cell.moveType}</span>
          {cell.animKind !== "Neutral" && (
            <>
              <span className="muted">·</span>
              <span className="muted">anim: {cell.animKind}</span>
            </>
          )}
        </div>

        <div className="move-summary-stats">
          <div className="stat-block">
            <div className="stat-label">Damage</div>
            <div className="stat-value">
              <StatValue value={cell.damage} />
            </div>
          </div>
          <div className="stat-block">
            <div className="stat-label">Cell window</div>
            <div className="stat-value">
              {cell.role === "Attack"
                ? <span className="mono">coord {cell.activeStartCoordinate}..{cell.activeEndCoordinate}</span>
                : "—"}
            </div>
          </div>
          <div className="stat-block">
            <div className="stat-label">Active</div>
            <div className="stat-value">{cell.activeFrames}f</div>
          </div>
          <div className="stat-block">
            <div className="stat-label">Hit stun</div>
            <div className="stat-value"><StatValue value={cell.baseHitStunFrames} suffix="f" /></div>
          </div>
          <div className="stat-block">
            <div className="stat-label">Block stun</div>
            <div className="stat-value"><StatValue value={cell.blockStunFrames} suffix="f" /></div>
          </div>
          <div className="stat-block">
            <div className="stat-label">Range (stand)</div>
            <div className="stat-value bigword mono">{rangeStand}</div>
          </div>
        </div>
      </section>

      <section className="detail-section">
        <div className="detail-grid">
          <div className="detail-card">
            <h3>Hit reactions</h3>
            <table className="engine-fields-tight">
              <tbody>
                <tr><td>Normal contact</td>
                    <td><strong>{cell.baseHitStunFrames}f stun</strong>, reaction <span className="mono">{cell.reactionIdBaseContact}</span></td></tr>
                <tr><td>Special contact</td>
                    <td>{cell.specialHitStunFrames}f stun, reaction <span className="mono">{cell.reactionIdSpecialContact}</span></td></tr>
                <tr><td>Alternate posture, normal</td>
                    <td>{cell.alternatePostureBaseHitStunFrames}f stun</td></tr>
                <tr><td>Alternate posture, special</td>
                    <td>{cell.alternatePostureSpecialHitStunFrames}f stun</td></tr>
                <tr><td>Throw escape</td>
                    <td>row <span className="mono">{cell.throwEscapeId}</span></td></tr>
              </tbody>
            </table>
          </div>

          <div className="detail-card">
            <h3>Range &amp; positioning</h3>
            <table className="engine-fields-tight">
              <tbody>
                <tr><td>Standing range</td><td>{rangeStand}</td></tr>
                <tr><td>Crouching range</td><td>{rangeCrouch}</td></tr>
                <tr><td>Reach gate</td><td>{cell.reachExtraGate}</td></tr>
                <tr><td>Hitbox group</td><td className="mono">0x{cell.hitboxGroup.toString(16).toUpperCase().padStart(4, "0")}</td></tr>
              </tbody>
            </table>
          </div>
        </div>
      </section>

      {usedBy.length > 0 && (
        <section className="detail-section">
          <h2>Move-state slots that reference this cell ({usedBy.length})</h2>
          <p className="muted" style={{ fontSize: 13 }}>
            Each slot's <code className="mono">nCellBoneIndexPerVariant</code> is a 6-entry
            list mapping animation variants to attack cells. Variant N means this cell is
            active when the slot is playing motion-variant N (0 = default).
          </p>
          <div style={{ display: "flex", gap: 6, flexWrap: "wrap", marginTop: 8 }}>
            {usedBy.slice(0, 50).map(([slot, variant]) => (
              <Link
                key={`${slot}-${variant}`}
                to="/c/$cid/moves/$slot"
                params={{ cid, slot: String(slot) }}
                search={{ move: undefined, order: undefined }}
                className="chip"
                style={{ fontFamily: "ui-monospace, monospace" }}
              >
                slot {slot} <span className="muted">v{variant}</span>
              </Link>
            ))}
            {usedBy.length > 50 && (
              <span className="muted" style={{ alignSelf: "center" }}>
                +{usedBy.length - 50} more
              </span>
            )}
          </div>
        </section>
      )}

      <details className="engine-details">
        <summary>Show engine fields (raw struct values)</summary>
        <div className="engine-fields">
          <table>
            <tbody>
              <tr><td>wU16AttackFlags</td>
                  <td>0x{cell.attackFlags.toString(16).toUpperCase().padStart(4, "0")}</td>
                  <td className="muted">{cell.attackFlagsDecoded}</td></tr>
              <tr><td>wU16ExtraStateFlags</td>
                  <td>0x{cell.extraStateFlags.toString(16).toUpperCase().padStart(4, "0")}</td>
                  <td className="muted">always 0x0000 in shipped data</td></tr>
              <tr><td>wI16BaseDamage</td><td>{cell.damage}</td><td className="muted">+0x3A</td></tr>
              <tr><td>wI16MasterWindowStart..End</td><td>{cell.activeStartCoordinate}..{cell.activeEndCoordinate}</td><td className="muted">native coordinates, +0x36/+0x38</td></tr>
              <tr><td>wI16BlockstunFrames</td><td>{cell.blockStunFrames}</td><td className="muted">+0x44</td></tr>
              <tr><td>wI16HitstunBaseContact</td><td>{cell.baseHitStunFrames}</td><td className="muted">+0x46</td></tr>
              <tr><td>wI16HitstunSpecialContact</td><td>{cell.specialHitStunFrames}</td><td className="muted">+0x48 (includes mode-11 CH)</td></tr>
              <tr><td>wI16HitstunAlternatePostureBaseContact</td><td>{cell.alternatePostureBaseHitStunFrames}</td><td className="muted">+0x4C</td></tr>
              <tr><td>wI16HitstunAlternatePostureSpecialContact</td><td>{cell.alternatePostureSpecialHitStunFrames}</td><td className="muted">+0x4E</td></tr>
              <tr><td>wI16ReactionIdBaseContact</td><td>{cell.reactionIdBaseContact}</td><td className="muted">+0x50 → chara+0x43DD8</td></tr>
              <tr><td>wI16ReactionIdSpecialContact</td><td>{cell.reactionIdSpecialContact}</td><td className="muted">+0x52</td></tr>
              <tr><td>wI16ThrowReactionRowId</td><td>{cell.throwEscapeId}</td><td className="muted">+0x54 (classifier-7 throw reaction)</td></tr>
              <tr><td>wU16HitboxGroupBitfield</td><td>0x{cell.hitboxGroup.toString(16).toUpperCase().padStart(4, "0")}</td><td className="muted">+0x5E (0xFFFF = sentinel)</td></tr>
              <tr><td>wU16PassthroughTagA</td><td>0x{cell.passthroughA.toString(16).toUpperCase().padStart(4, "0")}</td><td className="muted">+0x5A</td></tr>
              <tr><td>wU16PassthroughTagC</td><td>0x{cell.passthroughC.toString(16).toUpperCase().padStart(4, "0")}</td><td className="muted">+0x60</td></tr>
              <tr><td>cI8RangeStandMin..Max</td><td>{rangeStand}</td><td className="muted">+0x62..63 (-127=∞)</td></tr>
              <tr><td>cI8RangeCrouchMin..Max</td><td>{rangeCrouch}</td><td className="muted">+0x64..65</td></tr>
              <tr><td>nI16ReachExtraGate</td><td>{cell.reachExtraGate}</td><td className="muted">+0x66</td></tr>
              <tr><td>wU16InputCond</td><td>0x{cell.inputCond.toString(16).toUpperCase().padStart(4, "0")}</td><td className="muted">+0x34 (attack-class enum: 0x10=Wire/Special-FX; NOT button bits)</td></tr>
              <tr><td>wI16StunRecoil</td><td>{cell.stunRecoil}</td><td className="muted">+0x3C</td></tr>
              <tr><td>u64SlotMask</td><td className="mono">{toHexU64(cell.slotMask)}</td><td className="muted">+0x00</td></tr>
            </tbody>
          </table>
        </div>
      </details>
    </>
  );
}

function formatRange(min: number, max: number): string {
  if (min === -127 && max === -127) return "∞";
  return `${min}..${max}`;
}

function toHexU64(s: string): string {
  // server sends as decimal string; convert to hex for compactness
  try {
    return "0x" + BigInt(s).toString(16).toUpperCase().padStart(16, "0");
  } catch {
    return s;
  }
}
