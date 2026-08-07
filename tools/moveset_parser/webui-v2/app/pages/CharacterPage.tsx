import { Alert, Select, SelectOption, Table, Tag } from "@digdir/designsystemet-react";
import { Link } from "@tanstack/react-router";
import { useMemo, useState } from "react";
import { CharacterTabs, type CharacterTab } from "../components/CharacterTabs";
import { CommandText } from "../components/CommandText";
import { FamilyRowsTable } from "../components/FamilyRowsTable";
import { FamilyTable } from "../components/FamilyTable";
import { ConfidenceTag, FrameTag, HitLevelTags, SourceTag, TimelineTag } from "../components/StatusTags";
import { prefetchFamilyDetail } from "../data/api";
import type { AppSearch, PlayerCharPayload, RawMovelistPayload } from "../data/types";
import { displayDamage, displayFrame } from "../lib/frames";
import { buildFamilyViewModels, ensurePlayerFamilies, familyStats, flattenFamilyRows, type FamilyViewModel } from "../lib/families";
import { filterFamilyViews } from "../lib/search";

export interface CharacterPageProps {
  char: PlayerCharPayload;
  search: AppSearch;
  activeTab: CharacterTab;
  onTabChange: (tab: CharacterTab) => void;
  familyId?: string;
}

function CharacterHeader({ char, activeTab, onTabChange }: CharacterPageProps) {
  const { families, summary } = ensurePlayerFamilies(char);
  return (
    <header className="character-header">
      <div>
        <p className="eyebrow mono">{char.cid}</p>
        <h1>{char.name}</h1>
        <div className="character-meta">
          <Tag variant="outline">{char.kind}</Tag>
          <Tag variant="outline">{summary?.playerFamilies ?? families.length} families</Tag>
          <Tag variant="outline">{summary?.officialRows ?? 0} unique moves</Tag>
        </div>
      </div>
      <CharacterTabs active={activeTab} onChange={onTabChange} />
    </header>
  );
}

function MetricPanel({ label, value, hint }: { label: string; value: string | number; hint?: string }) {
  return (
    <div className="metric-panel">
      <span className="metric-label">{label}</span>
      <strong>{value}</strong>
      {hint ? <span className="muted">{hint}</span> : null}
    </div>
  );
}

export function CharacterDashboardPage(props: CharacterPageProps) {
  const { char, search } = props;
  const { families, summary } = ensurePlayerFamilies(char);
  const rows = flattenFamilyRows(families);
  const viewModels = useMemo(
    () => buildFamilyViewModels(families, char.dashboard),
    [families, char.dashboard],
  );
  const viewsById = useMemo(
    () => new Map(viewModels.map((view) => [view.family.id, view])),
    [viewModels],
  );
  const pickViews = (ids: string[]) => ids.map((id) => viewsById.get(id)).filter((view): view is FamilyViewModel => Boolean(view));

  const fastest = pickViews(char.dashboard?.fastestFamilyIds ?? []);
  const unsafe = pickViews(char.dashboard?.unsafeFamilyIds ?? []);
  const plus = pickViews(char.dashboard?.plusFamilyIds ?? []);
  const launchers = pickViews(char.dashboard?.launcherFamilyIds ?? []);

  return (
    <div className="page-stack">
      <CharacterHeader {...props} />
      {!families.length ? (
        <Alert data-color="warning">No movelist data was exported for this character.</Alert>
      ) : null}
      <section className="metric-grid">
        <MetricPanel label="Families" value={summary?.playerFamilies ?? families.length} hint={`${rows.length} exact rows`} />
        <MetricPanel label="MoveVM linked" value={summary?.nativeLinkedRows ?? 0} hint={`${summary?.nativeUnlinkedRows ?? 0} ambiguous or unresolved`} />
        <MetricPanel label="Startup coverage" value={summary?.metricCoverage.startup ?? 0} hint="static native evidence" />
        <MetricPanel label="Native attacks" value={char.nativeSummary?.attackCount ?? "-"} hint={`${char.nativeSummary?.slotCount ?? "-"} slots`} />
      </section>

      <section className="dashboard-grid">
        <DashboardFamilyList title="Fastest tools" familyViews={fastest} cid={char.cid} search={search} metric="startup" />
        <DashboardFamilyList title="Unsafe on block" familyViews={unsafe} cid={char.cid} search={search} metric="block" />
        <DashboardFamilyList title="Plus on block" familyViews={plus} cid={char.cid} search={search} metric="block" />
        <DashboardFamilyList title="Confirmed launch / knockdown" familyViews={launchers} cid={char.cid} search={search} metric="hit" />
      </section>
    </div>
  );
}

function DashboardFamilyList({
  title,
  familyViews,
  cid,
  search,
  metric,
}: {
  title: string;
  familyViews: FamilyViewModel[];
  cid: string;
  search: AppSearch;
  metric: "startup" | "block" | "hit";
}) {
  return (
    <section className="dashboard-list">
      <div className="section-heading">
        <h2>{title}</h2>
        <Tag variant="outline">{familyViews.length}</Tag>
      </div>
      {familyViews.map(({ family, stats }) => {
        const value = metric === "startup"
          ? stats.startup ?? "-"
          : metric === "block"
            ? displayFrame(stats.block)
            : displayFrame(stats.hit);
        return (
          <Link
            key={family.id}
            className="compact-family-link"
            to="/c/$cid/families/$familyId"
            params={{ cid, familyId: family.id }}
            search={search}
            onFocus={() => { void prefetchFamilyDetail(cid, family.id).catch(() => {}); }}
            onMouseEnter={() => { void prefetchFamilyDetail(cid, family.id).catch(() => {}); }}
          >
            <CommandText value={family.rootCommand} />
            <span>{family.rootName}</span>
            <strong>{value}</strong>
          </Link>
        );
      })}
      {!familyViews.length ? <p className="muted">No proven native values.</p> : null}
    </section>
  );
}

export function CharacterFamiliesPage(props: CharacterPageProps) {
  const { char } = props;
  const { families, summary } = ensurePlayerFamilies(char);
  const [query, setQuery] = useState("");
  const [linkStatus, setLinkStatus] = useState("all");
  const [confidence, setConfidence] = useState("all");
  const viewModels = useMemo(
    () => buildFamilyViewModels(families, char.dashboard),
    [families, char.dashboard],
  );

  const filtered = useMemo(() => {
    return filterFamilyViews(viewModels, query).filter(({ family }) => {
      if (confidence !== "all" && family.confidence !== confidence) return false;
      if (linkStatus !== "all" && !family.rows.some((row) => row.nativeLink.status === linkStatus)) return false;
      return true;
    });
  }, [viewModels, query, linkStatus, confidence]);

  const linkChoices = Object.keys(summary?.linkStatusCounts ?? {});
  const confidenceChoices = Object.keys(summary?.groupingConfidenceCounts ?? {});

  return (
    <div className="page-stack">
      <CharacterHeader {...props} />
      <section className="family-browser-toolbar" data-size="sm">
        <div>
          <h1>Move families</h1>
          <p className="muted">Grouped the way players usually talk about strings, holds, stance branches, and directional alternatives.</p>
        </div>
        <input
          className="ds-input-like"
          type="search"
          value={query}
          onChange={(event) => setQuery(event.currentTarget.value)}
          placeholder="Search command, name, stance, level, evidence..."
          aria-label="Search this character"
        />
        <label>
          <span>Native link</span>
          <Select width="auto" value={linkStatus} onChange={(event) => setLinkStatus(event.currentTarget.value)}>
            <SelectOption value="all">All</SelectOption>
            {linkChoices.map((value) => <SelectOption key={value} value={value}>{value}</SelectOption>)}
          </Select>
        </label>
        <label>
          <span>Confidence</span>
          <Select width="auto" value={confidence} onChange={(event) => setConfidence(event.currentTarget.value)}>
            <SelectOption value="all">All</SelectOption>
            {confidenceChoices.map((value) => <SelectOption key={value} value={value}>{value}</SelectOption>)}
          </Select>
        </label>
      </section>
      <div className="section-heading">
        <h2>{filtered.length} families</h2>
        <Tag variant="outline">{families.length} total</Tag>
      </div>
      <FamilyTable
        familyViews={filtered}
        familyLink={(family) => (
          <Link
            className="text-link"
            to="/c/$cid/families/$familyId"
            params={{ cid: char.cid, familyId: family.id }}
            search={props.search}
            onFocus={() => { void prefetchFamilyDetail(char.cid, family.id).catch(() => {}); }}
            onMouseEnter={() => { void prefetchFamilyDetail(char.cid, family.id).catch(() => {}); }}
          >
            Open family evidence
          </Link>
        )}
      />
    </div>
  );
}

export function CharacterFamilyDetailPage(props: CharacterPageProps) {
  const { char, familyId } = props;
  const { families } = ensurePlayerFamilies(char);
  const family = families.find((item) => item.id === familyId);

  if (!family) {
    return (
      <div className="page-stack">
        <CharacterHeader {...props} />
        <Alert data-color="warning">Family not found.</Alert>
      </div>
    );
  }

  // The detail header is descriptive, not a ranking.  Show the row-derived
  // value (including tagged native inference) instead of the dashboard's
  // confirmed-only aggregate.
  const stats = familyStats(family);

  return (
    <div className="page-stack">
      <CharacterHeader {...props} activeTab="families" />
      <section className="family-detail-header">
        <div>
          <CommandText value={family.rootCommand} />
          <h1>{family.rootName}</h1>
          <p className="muted">{family.context || "Neutral"}</p>
        </div>
        <div className="family-detail-metrics">
          <MetricPanel label="Startup" value={stats.startup ?? "-"} />
          <MetricPanel label="Damage" value={stats.damage ?? "-"} />
          <MetricPanel label="Block" value={displayFrame(stats.block)} />
          <MetricPanel label="Hit" value={displayFrame(stats.hit)} />
        </div>
      </section>

      <section>
        <div className="section-heading">
          <h2>Exact rows</h2>
          <ConfidenceTag value={family.confidence} />
        </div>
        <FamilyRowsTable rows={family.rows} />
      </section>

      <section className="evidence-layout">
        <div>
          <h2>Relation tree</h2>
          {family.edges.length ? (
            <div className="edge-list">
              {family.edges.map((edge) => {
                const parent = family.rows.find((row) => row.id === edge.parentRowId);
                const child = family.rows.find((row) => row.id === edge.childRowId);
                return (
                  <div key={edge.id} className="edge-row">
                    <CommandText value={parent?.displayCommand ?? edge.parentRowId} subtle />
                    <Tag variant="outline">{edge.relation}</Tag>
                    <CommandText value={child?.displayCommand ?? edge.childRowId} subtle />
                  </div>
                );
              })}
            </div>
          ) : (
            <p className="muted">No explicit edges were exported for this family.</p>
          )}
        </div>
        <div>
          <h2>Native evidence</h2>
          <div className="evidence-stack">
            {family.rows.map((row) => (
              <div key={row.id} className="evidence-row">
                <div>
                  <CommandText value={row.displayCommand} />
                  <strong>{row.displayName}</strong>
                </div>
                <div className="tag-stack">
                  <SourceTag value={row.source} />
                  <ConfidenceTag value={row.confidence} />
                  <TimelineTag value={row.nativeLink.status} />
                </div>
                <dl className="evidence-dl">
                  <dt>Parser orders</dt>
                  <dd>{row.parserMoveOrders.length ? row.parserMoveOrders.join(", ") : "-"}</dd>
                  <dt>MoveVM definitions</dt>
                  <dd>{row.nativeLink.definitions.length
                    ? row.nativeLink.definitions.map((definition) => (
                      `${definition.lane}: main ${definition.mainDefinitionId || "-"}, fallback ${definition.fallbackDefinitionId || "-"}`
                    )).join("; ")
                    : "-"}</dd>
                  <dt>Route KHD slots</dt>
                  <dd>{row.nativeLink.slots.length ? row.nativeLink.slots.join(" → ") : "-"}</dd>
                  <dt>Route KHD cells</dt>
                  <dd>{row.nativeLink.cells.length ? row.nativeLink.cells.join(" → ") : "-"}</dd>
                  <dt>Effective attack slots</dt>
                  <dd>{(row.nativeLink.attackSlots ?? row.nativeLink.slots).length
                    ? (row.nativeLink.attackSlots ?? row.nativeLink.slots).join(" → ")
                    : "-"}</dd>
                  <dt>Effective attack cells</dt>
                  <dd>{(row.nativeLink.attackCells ?? row.nativeLink.cells).length
                    ? (row.nativeLink.attackCells ?? row.nativeLink.cells).join(" → ")
                    : "-"}</dd>
                  <dt>Resolution</dt>
                  <dd>{row.nativeLink.resolutions.length ? row.nativeLink.resolutions.join(", ") : "-"}</dd>
                  <dt>Hit levels</dt>
                  <dd><HitLevelTags levels={row.metrics.hitLevels} /></dd>
                </dl>
              </div>
            ))}
          </div>
        </div>
      </section>
    </div>
  );
}

export function CharacterRawPage(props: CharacterPageProps & { raw: RawMovelistPayload }) {
  const { char } = props;
  const moves = props.raw.rows;
  return (
    <div className="page-stack">
      <CharacterHeader {...props} />
      <section>
        <div className="section-heading">
          <h1>Raw movelist rows</h1>
          <Tag variant="outline">{moves.length} rows</Tag>
        </div>
        <div className="table-scroll" data-size="sm">
          <Table className="raw-table">
            <Table.Head>
              <Table.Row>
                <Table.HeaderCell>Order</Table.HeaderCell>
                <Table.HeaderCell>Move ID</Table.HeaderCell>
                <Table.HeaderCell>Command</Table.HeaderCell>
                <Table.HeaderCell>Name</Table.HeaderCell>
                <Table.HeaderCell>Level</Table.HeaderCell>
                <Table.HeaderCell>i</Table.HeaderCell>
                <Table.HeaderCell>Damage</Table.HeaderCell>
                <Table.HeaderCell>Block</Table.HeaderCell>
                <Table.HeaderCell>Hit</Table.HeaderCell>
                <Table.HeaderCell>Native refs</Table.HeaderCell>
              </Table.Row>
            </Table.Head>
            <Table.Body>
              {moves.map((move) => {
                const metrics = move.metrics;
                return (
                  <Table.Row key={move.order}>
                    <Table.Cell className="numeric">{move.order}</Table.Cell>
                    <Table.Cell className="numeric">{move.moveId}</Table.Cell>
                    <Table.Cell><CommandText value={move.fullCommand || move.input} /></Table.Cell>
                    <Table.Cell>{move.name || "-"}</Table.Cell>
                    <Table.Cell><HitLevelTags levels={metrics.hitLevels} /></Table.Cell>
                    <Table.Cell className="numeric">{metrics.startup ?? "-"}</Table.Cell>
                    <Table.Cell className="numeric">{displayDamage(metrics.damage)}</Table.Cell>
                    <Table.Cell><FrameTag value={displayFrame(metrics.block)} /></Table.Cell>
                    <Table.Cell><FrameTag value={displayFrame(metrics.hit)} /></Table.Cell>
                    <Table.Cell className="raw-refs">
                      {move.nativeLink.definitions.length
                        ? `vm:${move.nativeLink.definitions.map((definition) => definition.mainDefinitionId || definition.fallbackDefinitionId).filter(Boolean).slice(0, 4).join(",") || "-"}`
                        : "vm:-"}
                      <br />
                      {(move.nativeLink.attackCells ?? move.nativeLink.cells).length
                        ? `attack:${(move.nativeLink.attackCells ?? move.nativeLink.cells).slice(0, 4).join(",")}`
                        : "attack:-"}
                    </Table.Cell>
                  </Table.Row>
                );
              })}
            </Table.Body>
          </Table>
        </div>
      </section>
    </div>
  );
}
