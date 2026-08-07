import { Table } from "@digdir/designsystemet-react";
import type { PlayerMoveFamilyRow } from "../data/types";
import { displayDamage, displayFrame } from "../lib/frames";
import { CommandText } from "./CommandText";
import { ConfidenceTag, FrameTag, HitLevelTags, MetricEvidenceTag, SourceTag, TimelineTag } from "./StatusTags";

export interface FamilyRowsTableProps {
  rows: PlayerMoveFamilyRow[];
  compact?: boolean;
}

export function FamilyRowsTable({ rows, compact = false }: FamilyRowsTableProps) {
  return (
    <div className="table-scroll" data-size={compact ? "sm" : undefined}>
      <Table className="move-table">
        <Table.Head>
          <Table.Row>
            <Table.HeaderCell>Command</Table.HeaderCell>
            <Table.HeaderCell>Name</Table.HeaderCell>
            <Table.HeaderCell>Level</Table.HeaderCell>
            <Table.HeaderCell>i</Table.HeaderCell>
            <Table.HeaderCell>Damage</Table.HeaderCell>
            <Table.HeaderCell>Block</Table.HeaderCell>
            <Table.HeaderCell>Hit</Table.HeaderCell>
            <Table.HeaderCell>CH</Table.HeaderCell>
            <Table.HeaderCell>Source</Table.HeaderCell>
            <Table.HeaderCell>Evidence</Table.HeaderCell>
          </Table.Row>
        </Table.Head>
        <Table.Body>
          {rows.map((row) => (
            <Table.Row key={row.id}>
              <Table.Cell className="command-cell">
                <CommandText value={row.displayCommand} />
                {row.context && row.context !== "Neutral" ? (
                  <span className="context-line">{row.context}</span>
                ) : null}
              </Table.Cell>
              <Table.Cell className="name-cell">
                <strong>{row.displayName || "-"}</strong>
                {row.notes ? <span className="notes-line">{row.notes}</span> : null}
              </Table.Cell>
              <Table.Cell><HitLevelTags levels={row.metrics.hitLevels} /></Table.Cell>
              <Table.Cell className="numeric">{row.metrics.startup ?? "-"}</Table.Cell>
              <Table.Cell className="numeric">{displayDamage(row.metrics.damage)}</Table.Cell>
              <Table.Cell><FrameTag value={displayFrame(row.metrics.block)} /></Table.Cell>
              <Table.Cell><FrameTag value={displayFrame(row.metrics.hit)} /></Table.Cell>
              <Table.Cell><FrameTag value={displayFrame(row.metrics.counterHit)} /></Table.Cell>
              <Table.Cell><SourceTag value={row.source} /></Table.Cell>
              <Table.Cell className="tag-stack">
                <ConfidenceTag value={row.confidence} />
                <TimelineTag value={row.nativeLink.status} />
                <MetricEvidenceTag metric="startup" value={row.evidence.startup} />
                <MetricEvidenceTag metric="damage" value={row.evidence.damage} />
                <MetricEvidenceTag metric="block" value={row.evidence.block} />
                <MetricEvidenceTag metric="hit" value={row.evidence.hit} />
                <MetricEvidenceTag metric="counter hit" value={row.evidence.counterHit} />
              </Table.Cell>
            </Table.Row>
          ))}
        </Table.Body>
      </Table>
    </div>
  );
}
