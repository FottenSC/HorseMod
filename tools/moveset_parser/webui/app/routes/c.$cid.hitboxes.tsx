import { createFileRoute } from "@tanstack/react-router";
import type { CharData } from "../data/types";

export const Route = createFileRoute("/c/$cid/hitboxes")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: HitboxesTab,
});

function HitboxesTab() {
  const char = Route.useLoaderData();

  const tables = [
    { key: "atkhit" as const, label: "Attack hitboxes" },
    { key: "bodyhit" as const, label: "Body / pushbox" },
    { key: "yararehit" as const, label: "Reaction (yarare) hitboxes" },
  ];

  return (
    <>
      {tables.map(({ key, label }) => {
        const data = char[key];
        if (!data) return null;
        const tagCounts = data.records.reduce<Record<string, number>>((acc, r) => {
          acc[r.tagName] = (acc[r.tagName] ?? 0) + 1;
          return acc;
        }, {});
        return (
          <section key={key} style={{ marginBottom: "2em" }}>
            <h2>{label}</h2>
            <p className="muted" style={{ fontSize: 13 }}>
              {data.recordCount} records ·{" "}
              {Object.entries(tagCounts).map(([t, c]) => `${t}=${c}`).join(", ")}
            </p>
            <table className="moves-table">
              <thead>
                <tr>
                  <th>#</th>
                  <th>Type</th>
                  <th>Bone slot</th>
                  <th>X</th>
                  <th>Y</th>
                  <th>Z</th>
                  <th>Radius</th>
                  <th>Impulse</th>
                  <th>UE4 bone</th>
                </tr>
              </thead>
              <tbody>
                {data.records.map((r) => (
                  <tr key={r.idx}>
                    <td className="num muted mono">{r.idx}</td>
                    <td>
                      <span className={`badge ${r.tagName === "Sphere" ? "badge-mid" : r.tagName === "Area" ? "badge-high" : "badge-low"}`}>
                        {r.tagName}
                      </span>
                    </td>
                    <td className="num">{r.slot}</td>
                    <td className="num mono">{r.tag === 0 ? r.x.toFixed(3) : "—"}</td>
                    <td className="num mono">{r.tag === 0 ? r.y.toFixed(3) : "—"}</td>
                    <td className="num mono">{r.tag === 0 ? r.z.toFixed(3) : "—"}</td>
                    <td className="num mono">{r.tag === 0 ? r.radius.toFixed(3) : "—"}</td>
                    <td className="num mono muted">{r.tag === 0 ? r.contactImpulseScale.toFixed(3) : "—"}</td>
                    <td className="num mono muted">{r.tag === 0 ? r.boneIndexUe4 : "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </section>
        );
      })}
    </>
  );
}
