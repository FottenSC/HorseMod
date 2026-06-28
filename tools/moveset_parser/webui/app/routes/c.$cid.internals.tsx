import { createFileRoute } from "@tanstack/react-router";
import type { CharData } from "../data/types";

export const Route = createFileRoute("/c/$cid/internals")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: InternalsTab,
});

function InternalsTab() {
  const char = Route.useLoaderData();
  const khd = char.khd;

  return (
    <>
      <p className="muted">
        Engine-level data. For day-to-day modding stick with the Moves tab —
        what's here is the raw parser output.
      </p>

      <section style={{ marginTop: "1em" }}>
        <h2>Files</h2>
        <ul>
          {Object.entries(char.files).map(([key, present]) => (
            <li key={key}>
              <code className="mono">{key}</code>:{" "}
              {present ? <span className="pos">present</span> : <span className="muted">missing</span>}
            </li>
          ))}
        </ul>
      </section>

      {khd && (
        <>
          <section style={{ marginTop: "1em" }}>
            <h2>KHD overview</h2>
            <table className="engine-fields-tight">
              <tbody>
                <tr><td>Section offsets</td>
                    <td className="mono">{khd.sectionOffsets.map(o => "0x" + o.toString(16).toUpperCase()).join(", ")}</td></tr>
                <tr><td>Total cells</td>     <td>{khd.totalCells}</td></tr>
                <tr><td>Attack cells</td>    <td>{khd.attackCount}</td></tr>
                <tr><td>Header cells</td>    <td>{khd.headerCount}</td></tr>
                <tr><td>Non-damaging</td>    <td>{khd.nonDamagingCount}</td></tr>
                <tr><td>Sentinels (cleared)</td><td>{khd.sentinelCount}</td></tr>
                <tr><td>Move slots</td>      <td>{khd.slotCount}</td></tr>
              </tbody>
            </table>
          </section>

          <section style={{ marginTop: "1em" }}>
            <h2>Move slots (first 200)</h2>
            <p className="muted" style={{ fontSize: 13 }}>
              Stack-VM bytecode summary per slot. CALLCOND columns count
              how many times each dispatch function was called by the slot's
              bytecode (a fingerprint of move-state behaviour).
            </p>
            <table className="moves-table">
              <thead>
                <tr>
                  <th>#</th>
                  <th>Anim</th>
                  <th>Frames</th>
                  <th>Speed</th>
                  <th>BC offset</th>
                  <th>BC size</th>
                  <th>Top callconds</th>
                  <th>Cell variants</th>
                </tr>
              </thead>
              <tbody>
                {khd.slots.slice(0, 200).map((s) => {
                  const cc = s.bytecode?.callconds ?? {};
                  const topCC = Object.entries(cc)
                    .sort((a, b) => b[1] - a[1])
                    .slice(0, 3)
                    .map(([k, v]) => `${k}×${v}`)
                    .join("  ");
                  return (
                    <tr key={s.idx}>
                      <td className="num muted mono">{s.idx}</td>
                      <td className="num">{s.animationIndex}</td>
                      <td className="num">{s.totalFrames}f</td>
                      <td className="num">{(s.playbackSpeed ?? 0).toFixed(3)}x</td>
                      <td className="num mono muted">0x{s.bytecodeOffset.toString(16).toUpperCase()}</td>
                      <td className="num">{s.bytecode?.instructionCount ?? 0} ops</td>
                      <td className="mono" style={{ fontSize: 12 }}>{topCC}</td>
                      <td className="mono muted" style={{ fontSize: 11 }}>
                        [{s.cellVariants.join(", ")}]
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
            {khd.slots.length > 200 && (
              <p className="muted">+{khd.slots.length - 200} more slots not shown</p>
            )}
          </section>
        </>
      )}

      {char.mot && (
        <section style={{ marginTop: "1em" }}>
          <h2>Motion (MOT)</h2>
          <p>{char.mot.count} motions, {char.mot.emptySections} empty, file size {char.mot.fileSize.toLocaleString()} bytes</p>
        </section>
      )}

      {char.dtp && (
        <section style={{ marginTop: "1em" }}>
          <h2>AI personality (DTP)</h2>
          <p>{char.dtp.count} sections, {char.dtp.emptySections} empty, file size {char.dtp.fileSize.toLocaleString()} bytes</p>
        </section>
      )}
    </>
  );
}
