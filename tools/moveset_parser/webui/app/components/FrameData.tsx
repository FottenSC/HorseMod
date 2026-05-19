import type { Cell } from "../data/types";

/**
 * Compact frame-data notation for a cell. Examples:
 *   i11 Ã¢Â· Mid Ã¢Â· 12 dmg Ã¢Â· +31 on hit Ã¢Â· +21 on block Ã¢Â· close
 *
 * The "+N" frame values are RAW STUN frames from the cell (defender side).
 * True frame advantage = stun - attacker_recovery; we can't compute that
 * without the bytecode trace, so we mark them as "stun frames" and let the
 * power user understand the difference.
 */
export function FrameDataLine({ cell }: { cell: Cell }) {
  const startup = cell.activeStart;
  return (
    <span className="framedata">
      i{startup}
      <span className="muted"> Ã¢Â· </span>
      <span>{cell.damage} dmg</span>
      <span className="muted"> Ã¢Â· </span>
      <span>active <strong>{cell.activeFrames}f</strong></span>
      <span className="muted"> Ã¢Â· </span>
      <span>hit <strong>{cell.onHitStanding}f</strong></span>
      <span className="muted"> Ã¢Â· </span>
      <span>blk <strong>{cell.onBlock}f</strong></span>
    </span>
  );
}

/**
 * Big-headline number --- used in the move-detail page. Renders e.g.
 *   12  (right-aligned)
 * With a "neg"/"pos" colour for advantage-style values.
 */
export function StatValue({ value, sign = false, suffix = "" }: {
  value: number; sign?: boolean; suffix?: string;
}) {
  let cls = "";
  if (sign) cls = value < 0 ? "neg" : value > 0 ? "pos" : "";
  const display = sign && value > 0 ? `+${value}` : `${value}`;
  return <span className={cls}>{display}{suffix}</span>;
}
