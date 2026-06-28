import { Button, Card, Tag } from "@digdir/designsystemet-react";
import { Link } from "@tanstack/react-router";
import type { AppSearch, CharaKind, Roster } from "../data/types";
import { prefetchPlayerChar } from "../data/api";

const KIND_ORDER: CharaKind[] = ["base", "dlc", "boss", "shared", "unknown"];

const KIND_LABEL: Record<CharaKind, string> = {
  base: "Base roster",
  dlc: "DLC",
  boss: "Bosses",
  shared: "Shared data",
  unknown: "Unknown",
};

export function RosterPage({ roster, search }: { roster: Roster; search: AppSearch }) {
  const groups = new Map<CharaKind, typeof roster.chars>();
  for (const char of roster.chars) {
    const kind = char.kind ?? "unknown";
    groups.set(kind, [...(groups.get(kind) ?? []), char]);
  }

  return (
    <div className="page-stack">
      <section className="intro-band">
        <div>
          <h1>SC6 move lookup</h1>
          <p>
            Browse the player-facing move families first, then expand to exact parser rows and
            evidence when the details matter.
          </p>
        </div>
        <Button asChild>
          <Link to="/lookup" search={search}>Quick lookup</Link>
        </Button>
      </section>

      {KIND_ORDER.map((kind) => {
        const chars = groups.get(kind)?.filter((char) => char.files?.khd || char.attackCount);
        if (!chars?.length) return null;
        return (
          <section key={kind} className="roster-section">
            <div className="section-heading">
              <h2>{KIND_LABEL[kind]}</h2>
              <Tag variant="outline">{chars.length} styles</Tag>
            </div>
            <div className="roster-grid">
              {chars.map((char) => {
                const prefetch = () => {
                  void prefetchPlayerChar(char.cid).catch(() => {});
                };
                return (
                  <Card key={char.cid} className="roster-card" asChild>
                    <Link
                      to="/c/$cid"
                      params={{ cid: char.cid }}
                      search={search}
                      onFocus={prefetch}
                      onMouseEnter={prefetch}
                    >
                      <strong>{char.name}</strong>
                      <span className="muted mono">{char.cid}</span>
                      <span className="roster-card-meta">
                        {char.attackCount ?? "-"} attacks
                        <span>{char.slotCount ?? "-"} slots</span>
                      </span>
                    </Link>
                  </Card>
                );
              })}
            </div>
          </section>
        );
      })}
    </div>
  );
}
