import { Alert, Search, Tag } from "@digdir/designsystemet-react";
import { useEffect, useMemo, useState } from "react";
import type { AppSearch, CharData, Roster } from "../data/types";
import { loadAllPlayableChars } from "../data/api";
import { CommandText } from "../components/CommandText";
import { ConfidenceTag, FrameTag } from "../components/StatusTags";
import { displayFrame } from "../lib/frames";
import { ensurePlayerFamilies, familyStats } from "../lib/families";
import { rankFamily } from "../lib/search";

export function LookupPage({
  roster,
  search,
  onQueryChange,
}: {
  roster: Roster;
  search: AppSearch;
  onQueryChange: (query: string) => void;
}) {
  const [chars, setChars] = useState<CharData[]>([]);
  const [loading, setLoading] = useState(true);
  const query = search.q ?? "";

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    loadAllPlayableChars(roster)
      .then((loaded) => {
        if (!cancelled) setChars(loaded);
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [roster]);

  const results = useMemo(() => {
    const enriched = chars.map((char) => ({
      char,
      families: ensurePlayerFamilies(char).families,
    }));
    return enriched
      .flatMap(({ char, families }) =>
        families.map((family) => ({ char, family, score: rankFamily(family, query) })),
      )
      .filter((result) => !query.trim() || result.score > 0)
      .sort((a, b) => b.score - a.score || a.char.name.localeCompare(b.char.name))
      .slice(0, 120);
  }, [chars, query]);

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

      {loading ? <Alert data-color="info">Loading character data...</Alert> : null}

      <div className="lookup-results">
        <div className="section-heading">
          <h2>{query ? "Matches" : "Sample families"}</h2>
          <Tag variant="outline">{results.length} shown</Tag>
        </div>
        {results.map(({ char, family }) => {
          const stats = familyStats(family);
          return (
            <a
              key={`${char.cid}-${family.id}`}
              className="lookup-result"
              href={`/c/${char.cid}/families/${encodeURIComponent(family.id)}${window.location.search}`}
            >
              <span className="lookup-char">{char.name}</span>
              <span className="lookup-command"><CommandText value={family.rootCommand} /></span>
              <span className="lookup-name">{family.rootName}</span>
              <span className="lookup-metrics">
                <span>i{stats.startup ?? "-"}</span>
                <span>{stats.damage ?? "-"} dmg</span>
                <FrameTag value={displayFrame(stats.block)} />
              </span>
              <ConfidenceTag value={family.confidence} />
            </a>
          );
        })}
        {!loading && !results.length ? <p className="empty-state">No matches found.</p> : null}
      </div>
    </div>
  );
}
