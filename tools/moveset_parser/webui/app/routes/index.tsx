import { createFileRoute } from "@tanstack/react-router";
import { CharCard } from "../components/CharCard";
import type { Roster } from "../data/types";

export const Route = createFileRoute("/")({
  loader: async () => {
    const res = await fetch("/data/roster.json");
    if (!res.ok) throw new Error("Failed to load roster");
    return (await res.json()) as Roster;
  },
  component: RosterPage,
});

function RosterPage() {
  const data = Route.useLoaderData();

  // Group by kind: base first, then DLC, then shared/boss/unknown
  const order = ["base", "dlc", "boss", "shared", "unknown"] as const;
  const groups: Record<string, typeof data.chars> = {};
  for (const c of data.chars) {
    (groups[c.kind] ??= []).push(c);
  }

  const KIND_LABEL: Record<string, string> = {
    base: "Base roster",
    dlc: "DLC",
    boss: "Bosses / hidden",
    shared: "Shared / common files",
    unknown: "Unmapped",
  };

  return (
    <>
      <h1>SC6 Characters</h1>
      <p className="muted">
        {data.chars.length} character slots from{" "}
        <code className="mono">dump/Battle</code>. Click any character to see
        their moves.
      </p>

      {order.map((kind) =>
        groups[kind]?.length ? (
          <section key={kind} style={{ marginTop: "1.5em" }}>
            <h2>{KIND_LABEL[kind]}</h2>
            <div className="roster-grid">
              {groups[kind].map((c) => (
                <CharCard key={c.cid} char={c} />
              ))}
            </div>
          </section>
        ) : null
      )}
    </>
  );
}
