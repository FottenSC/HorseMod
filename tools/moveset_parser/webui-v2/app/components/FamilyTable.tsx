import { Details, Tag } from "@digdir/designsystemet-react";
import type { PlayerMoveFamily } from "../data/types";
import { displayFrame } from "../lib/frames";
import { familyStats } from "../lib/families";
import { CommandText } from "./CommandText";
import { FamilyRowsTable } from "./FamilyRowsTable";
import { ConfidenceTag, FrameTag } from "./StatusTags";

export interface FamilyTableProps {
  families: PlayerMoveFamily[];
  familyUrl?: (family: PlayerMoveFamily) => string;
  initiallyOpenFirst?: boolean;
}

export function FamilyTable({ families, familyUrl, initiallyOpenFirst = false }: FamilyTableProps) {
  if (!families.length) {
    return <p className="empty-state">No move families match the current filters.</p>;
  }

  return (
    <div className="family-list" data-size="sm">
      {families.map((family, idx) => {
        const stats = familyStats(family);
        return (
          <Details
            key={family.id}
            className="family-disclosure"
            defaultOpen={initiallyOpenFirst && idx === 0}
          >
            <Details.Summary>
              <span className="family-summary-grid">
                <span className="family-main">
                  <CommandText value={family.rootCommand} />
                  <span className="family-title">
                    {family.rootName || "Unnamed move"}
                    {family.context && family.context !== "Neutral" ? (
                      <span className="context-line">{family.context}</span>
                    ) : null}
                  </span>
                </span>
                <span className="metric-cell"><span className="metric-label">i</span>{stats.startup ?? "-"}</span>
                <span className="metric-cell"><span className="metric-label">dmg</span>{stats.damage ?? "-"}</span>
                <span className="metric-cell"><span className="metric-label">blk</span><FrameTag value={displayFrame(stats.block)} /></span>
                <span className="metric-cell"><span className="metric-label">hit</span><FrameTag value={displayFrame(stats.hit)} /></span>
                <span className="tag-stack">
                  <Tag variant="outline">{stats.rowCount} rows</Tag>
                  <ConfidenceTag value={family.confidence} />
                </span>
              </span>
            </Details.Summary>
            <Details.Content>
              <div className="family-expanded">
                <div className="family-actions">
                  {family.relations.length ? (
                    <span className="relation-list">
                      {family.relations.map((relation) => (
                        <Tag key={relation} variant="outline">{relation}</Tag>
                      ))}
                    </span>
                  ) : (
                    <span className="muted">No explicit relation edges yet.</span>
                  )}
                  {familyUrl ? (
                    <a className="text-link" href={familyUrl(family)}>Open family evidence</a>
                  ) : null}
                </div>
                <FamilyRowsTable rows={family.rows} compact />
              </div>
            </Details.Content>
          </Details>
        );
      })}
    </div>
  );
}
