import { Search, Tag } from "@digdir/designsystemet-react";
import { Link } from "@tanstack/react-router";
import { useMemo } from "react";
import type { AppSearch, LookupIndex } from "../data/types";
import { prefetchFamilyDetail } from "../data/api";
import { CommandText } from "../components/CommandText";
import { ConfidenceTag, FrameTag } from "../components/StatusTags";
import { displayFrame } from "../lib/frames";
import { rankLookupFamily } from "../lib/search";

export function LookupPage({
  index,
  search,
  onQueryChange,
}: {
  index: LookupIndex;
  search: AppSearch;
  onQueryChange: (query: string) => void;
}) {
  const query = search.q ?? "";

  const results = useMemo(() => {
    return index.families
      .map((family) => ({ family, score: rankLookupFamily(family, query) }))
      .filter((result) => !query.trim() || result.score > 0)
      .sort((a, b) => b.score - a.score || a.family.charName.localeCompare(b.family.charName))
      .slice(0, 120);
  }, [index.families, query]);

  return (
    <div className="page-stack">
      <section className="lookup-toolbar">
        <div>
          <h1>Quick lookup</h1>
          <p className="muted">
            Search command, move name, stance, source, hit level, or property text.
          </p>
        </div>
        <Search className="lookup-search">
          <Search.Input
            aria-label="Search all moves"
            placeholder="AA, 3B, Wind Sault, lethal, unsafe..."
            value={query}
            onChange={(event) => onQueryChange(event.currentTarget.value)}
          />
          {query ? <Search.Clear aria-label="Clear search" onClick={() => onQueryChange("")} /> : null}
        </Search>
      </section>

      <div className="lookup-results">
        <div className="section-heading">
          <h2>{query ? "Matches" : "Sample families"}</h2>
          <Tag variant="outline">{results.length} shown</Tag>
        </div>
        {results.map(({ family }) => {
          const stats = family.metrics;
          return (
            <Link
              key={`${family.cid}-${family.familyId}`}
              className="lookup-result"
              to="/c/$cid/families/$familyId"
              params={{ cid: family.cid, familyId: family.familyId }}
              search={search}
              onFocus={() => { void prefetchFamilyDetail(family.cid, family.familyId).catch(() => {}); }}
              onMouseEnter={() => { void prefetchFamilyDetail(family.cid, family.familyId).catch(() => {}); }}
            >
              <span className="lookup-char">{family.charName}</span>
              <span className="lookup-command"><CommandText value={family.rootCommand} /></span>
              <span className="lookup-name">{family.rootName}</span>
              <span className="lookup-metrics">
                <span>i{stats.startup ?? "-"}</span>
                <span>{stats.damage ?? "-"} dmg</span>
                <FrameTag value={displayFrame(stats.block)} />
              </span>
              <ConfidenceTag value={family.confidence} />
            </Link>
          );
        })}
        {!results.length ? <p className="empty-state">No matches found.</p> : null}
      </div>
    </div>
  );
}
