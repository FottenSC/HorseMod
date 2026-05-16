import { Link } from "@tanstack/react-router";
import type { CharSummary } from "../data/types";

export function CharCard({ char }: { char: CharSummary }) {
  return (
    <Link
      to="/c/$cid/moves"
      params={{ cid: char.cid }}
      className="char-card"
    >
      <div className="char-card-header">
        <div className="char-card-name">
          {char.name}
          {char.uncertain ? <span className="muted" title="Identity not confirmed">?</span> : null}
        </div>
        <div className="char-card-cid">{char.cid}</div>
      </div>
      <div className={`char-card-kind ${char.kind}`}>{char.kind}</div>
      {char.attackCount !== undefined ? (
        <div className="char-card-stats">
          <div className="char-card-stat-label">attacks</div>
          <div className="char-card-stat-value">{char.attackCount}</div>
          <div className="char-card-stat-label">top dmg</div>
          <div className="char-card-stat-value">{char.topDamage}</div>
          <div className="char-card-stat-label">slots</div>
          <div className="char-card-stat-value">{char.slotCount}</div>
        </div>
      ) : (
        <div className="muted" style={{ fontSize: 12 }}>
          {char.kind === "shared" ? "Shared / common file only" :
            char.kind === "boss" ? "AI / placeholder only" :
            "No KHD data"}
        </div>
      )}
    </Link>
  );
}
