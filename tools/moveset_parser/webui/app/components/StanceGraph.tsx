/**
 * Stance-transition graph with 8 selectable layouts.
 *
 * Data comes from lib/moves.buildStanceGraph (the movelist — a move's
 * `condition` is the stance it's done from, a " ~ Stance" name suffix is
 * the stance it enters). This component offers 8 ways to look at it:
 *
 *   layered-lr / layered-tb  hierarchical (dagre)
 *   circular                 all stances on one ring
 *   radial                   Neutral hub at the centre
 *   force                    force-directed simulation
 *   grid                     uniform grid
 *   matrix                   adjacency matrix (zero edge crossings)
 *   arc                      arc diagram on a linear axis
 *
 * The node-positioned layouts share one SVG renderer (SvgGraph) wrapped
 * in ZoomableSvg, which restores pan / wheel-zoom / zoom controls. Custom
 * SVG (not ReactFlow) so opposing edges (X→Z and Z→X) bow to opposite
 * sides and never overlap.
 */
import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { useNavigate } from "@tanstack/react-router";
import dagre from "@dagrejs/dagre";
import type { CharData } from "../data/types";
import {
  buildStanceGraph, NEUTRAL_STANCE,
  type StanceGraph, type StanceNode, type StanceTransition,
} from "../lib/moves";

type Nav = ReturnType<typeof useNavigate>;
type XY = { x: number; y: number };
type Box = { x: number; y: number; w: number; h: number };
type LayoutId =
  | "layered-lr" | "layered-tb" | "circular" | "radial"
  | "force" | "grid" | "matrix" | "arc";

interface AggEdge { from: string; to: string; count: number }

// Node box size, in layout/SVG units.
const NW = 162;
const NH = 50;
const ZOOM_MIN = 0.25;
const ZOOM_MAX = 8;

const LAYOUTS: { id: LayoutId; label: string; hint: string }[] = [
  { id: "layered-lr", label: "Layered →", hint: "Hierarchical, left-to-right (dagre layered)." },
  { id: "layered-tb", label: "Layered ↓", hint: "Hierarchical, top-to-bottom (dagre layered)." },
  { id: "circular", label: "Circular", hint: "Every stance on one ring." },
  { id: "radial", label: "Radial hub", hint: "Neutral at the centre, stances around it." },
  { id: "force", label: "Force", hint: "Force-directed — linked stances pull together, all repel." },
  { id: "grid", label: "Grid", hint: "Uniform grid — predictable, compact." },
  { id: "matrix", label: "Matrix", hint: "Adjacency matrix: from-row × to-column. Zero edge crossings — best for dense graphs." },
  { id: "arc", label: "Arc", hint: "Arc diagram: stances on an axis, transitions as arcs (forward above, backward below)." },
];

function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v));
}

// --------------------------------------------------------------------------
// Edge aggregation — one edge per (from,to) pair, carrying the move count.
// --------------------------------------------------------------------------

function aggregateEdges(transitions: StanceTransition[]): AggEdge[] {
  const m = new Map<string, AggEdge>();
  for (const t of transitions) {
    const k = `${t.from} ${t.to}`;
    const e = m.get(k);
    if (e) e.count++;
    else m.set(k, { from: t.from, to: t.to, count: 1 });
  }
  return [...m.values()];
}

// --------------------------------------------------------------------------
// Layout algorithms — each returns Map<stanceName, centre XY>.
// --------------------------------------------------------------------------

function dagrePositions(
  stances: StanceNode[], edges: AggEdge[], dir: "LR" | "TB",
): Map<string, XY> {
  const g = new dagre.graphlib.Graph();
  g.setGraph({ rankdir: dir, nodesep: 58, ranksep: 150, marginx: 20, marginy: 20 });
  g.setDefaultEdgeLabel(() => ({}));
  for (const s of stances) g.setNode(s.name, { width: NW, height: NH });
  for (const e of edges) if (e.from !== e.to) g.setEdge(e.from, e.to);
  dagre.layout(g);
  const m = new Map<string, XY>();
  for (const s of stances) {
    const n = g.node(s.name);
    m.set(s.name, { x: n?.x ?? 0, y: n?.y ?? 0 });
  }
  return m;
}

function circularPositions(names: string[]): Map<string, XY> {
  const m = new Map<string, XY>();
  const n = Math.max(1, names.length);
  const r = Math.max(190, n * 56);
  names.forEach((nm, i) => {
    const a = (2 * Math.PI * i) / n - Math.PI / 2;
    m.set(nm, { x: r * Math.cos(a), y: r * Math.sin(a) });
  });
  return m;
}

function radialPositions(names: string[]): Map<string, XY> {
  const m = new Map<string, XY>();
  const others = names.filter((n) => n !== NEUTRAL_STANCE);
  if (names.includes(NEUTRAL_STANCE)) m.set(NEUTRAL_STANCE, { x: 0, y: 0 });
  const r = Math.max(210, others.length * 62);
  others.forEach((nm, i) => {
    const a = (2 * Math.PI * i) / Math.max(1, others.length) - Math.PI / 2;
    m.set(nm, { x: r * Math.cos(a), y: r * Math.sin(a) });
  });
  return m;
}

function gridPositions(names: string[]): Map<string, XY> {
  const m = new Map<string, XY>();
  const cols = Math.max(1, Math.ceil(Math.sqrt(names.length)));
  names.forEach((nm, i) => {
    m.set(nm, {
      x: (i % cols) * (NW + 64),
      y: Math.floor(i / cols) * (NH + 92),
    });
  });
  return m;
}

/** Deterministic force-directed layout (Fruchterman-Reingold-ish).
 * Seeded from a circle, no randomness — same input → same picture. */
function forcePositions(names: string[], edges: AggEdge[]): Map<string, XY> {
  const n = names.length;
  const pos = new Map<string, XY>();
  names.forEach((nm, i) => {
    const a = (2 * Math.PI * i) / Math.max(1, n);
    pos.set(nm, { x: 250 * Math.cos(a), y: 250 * Math.sin(a) });
  });
  if (n < 2) return pos;
  const ITER = 320;
  for (let it = 0; it < ITER; it++) {
    const disp = new Map<string, XY>(names.map((nm) => [nm, { x: 0, y: 0 }]));
    // all-pairs repulsion
    for (let i = 0; i < n; i++) {
      for (let j = i + 1; j < n; j++) {
        const a = pos.get(names[i])!;
        const b = pos.get(names[j])!;
        let dx = a.x - b.x;
        let dy = a.y - b.y;
        let d2 = dx * dx + dy * dy;
        if (d2 < 1) { d2 = 1; dx = 0.5; dy = 0.5; }
        const d = Math.sqrt(d2);
        const f = 150000 / d2;
        const fx = (f * dx) / d;
        const fy = (f * dy) / d;
        const di = disp.get(names[i])!;
        const dj = disp.get(names[j])!;
        di.x += fx; di.y += fy;
        dj.x -= fx; dj.y -= fy;
      }
    }
    // edge springs
    for (const e of edges) {
      if (e.from === e.to) continue;
      const a = pos.get(e.from);
      const b = pos.get(e.to);
      if (!a || !b) continue;
      const dx = b.x - a.x;
      const dy = b.y - a.y;
      const d = Math.sqrt(dx * dx + dy * dy) || 0.1;
      const f = (d - 270) * 0.06;
      const fx = (f * dx) / d;
      const fy = (f * dy) / d;
      disp.get(e.from)!.x += fx; disp.get(e.from)!.y += fy;
      disp.get(e.to)!.x -= fx; disp.get(e.to)!.y -= fy;
    }
    const cool = 1 - it / ITER;
    for (const nm of names) {
      const d = disp.get(nm)!;
      const p = pos.get(nm)!;
      const dl = Math.sqrt(d.x * d.x + d.y * d.y) || 0.1;
      const step = Math.min(dl, 28 * cool + 2);
      p.x += (d.x / dl) * step;
      p.y += (d.y / dl) * step;
    }
  }
  return pos;
}

// --------------------------------------------------------------------------
// ZoomableSvg — pan (drag the backdrop) + wheel-zoom + zoom controls.
// Restores the interaction the ReactFlow canvas had.
// --------------------------------------------------------------------------

function ZoomableSvg({ viewBox, children }: { viewBox: Box; children: ReactNode }) {
  const svgRef = useRef<SVGSVGElement>(null);
  const [t, setT] = useState({ x: 0, y: 0, k: 1 });
  const drag = useRef<{ px: number; py: number; ox: number; oy: number } | null>(null);
  const [panning, setPanning] = useState(false);

  // Reset the transform whenever the layout (and thus viewBox) changes.
  useEffect(() => { setT({ x: 0, y: 0, k: 1 }); }, [viewBox.x, viewBox.y, viewBox.w, viewBox.h]);

  // Wheel-zoom toward the cursor. A native non-passive listener so
  // preventDefault() actually stops the page from scrolling.
  useEffect(() => {
    const svg = svgRef.current;
    if (!svg) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const m = svg.getScreenCTM();
      if (!m) return;
      const p = svg.createSVGPoint();
      p.x = e.clientX; p.y = e.clientY;
      const v = p.matrixTransform(m.inverse());
      const factor = e.deltaY < 0 ? 1.18 : 1 / 1.18;
      setT((cur) => {
        const k = clamp(cur.k * factor, ZOOM_MIN, ZOOM_MAX);
        return {
          x: v.x - ((v.x - cur.x) / cur.k) * k,
          y: v.y - ((v.y - cur.y) / cur.k) * k,
          k,
        };
      });
    };
    svg.addEventListener("wheel", onWheel, { passive: false });
    return () => svg.removeEventListener("wheel", onWheel);
  }, []);

  const onMouseDown = (e: React.MouseEvent) => {
    const el = e.target as Element;
    // Pan only from the backdrop — clicks on nodes stay node clicks.
    if (e.button !== 0 || (!el.classList.contains("zsvg-bg") && el.tagName !== "svg")) {
      return;
    }
    e.preventDefault();
    drag.current = { px: e.clientX, py: e.clientY, ox: t.x, oy: t.y };
    setPanning(true);
  };
  const onMouseMove = (e: React.MouseEvent) => {
    const d = drag.current;
    const svg = svgRef.current;
    if (!d || !svg) return;
    const m = svg.getScreenCTM();
    if (!m) return;
    setT((cur) => ({
      ...cur,
      x: d.ox + (e.clientX - d.px) / m.a,
      y: d.oy + (e.clientY - d.py) / m.d,
    }));
  };
  const endPan = () => { drag.current = null; setPanning(false); };

  const zoomCentre = (factor: number) => {
    const cx = viewBox.x + viewBox.w / 2;
    const cy = viewBox.y + viewBox.h / 2;
    setT((cur) => {
      const k = clamp(cur.k * factor, ZOOM_MIN, ZOOM_MAX);
      return {
        x: cx - ((cx - cur.x) / cur.k) * k,
        y: cy - ((cy - cur.y) / cur.k) * k,
        k,
      };
    });
  };

  return (
    <div className="zsvg">
      <svg
        ref={svgRef}
        width="100%" height="100%"
        viewBox={`${viewBox.x} ${viewBox.y} ${viewBox.w} ${viewBox.h}`}
        preserveAspectRatio="xMidYMid meet"
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={endPan}
        onMouseLeave={endPan}
      >
        <defs>
          <pattern id="zsvg-dots" width="24" height="24" patternUnits="userSpaceOnUse">
            <circle cx="1.5" cy="1.5" r="1.2" fill="#262a33" />
          </pattern>
          <marker id="sg-arr" markerWidth="9" markerHeight="9" refX="7" refY="3"
            orient="auto-start-reverse">
            <path d="M0,0 L7,3 L0,6 Z" fill="#5a7a9c" />
          </marker>
          <marker id="sg-arr-hot" markerWidth="9" markerHeight="9" refX="7" refY="3"
            orient="auto-start-reverse">
            <path d="M0,0 L7,3 L0,6 Z" fill="#5cc77a" />
          </marker>
        </defs>
        <rect
          className="zsvg-bg"
          x={viewBox.x} y={viewBox.y} width={viewBox.w} height={viewBox.h}
          fill="url(#zsvg-dots)"
          style={{ cursor: panning ? "grabbing" : "grab" }}
        />
        <g transform={`translate(${t.x} ${t.y}) scale(${t.k})`}>
          {children}
        </g>
      </svg>
      <div className="zsvg-controls">
        <button onClick={() => zoomCentre(1.3)} title="Zoom in">+</button>
        <button onClick={() => zoomCentre(1 / 1.3)} title="Zoom out">−</button>
        <button onClick={() => setT({ x: 0, y: 0, k: 1 })} title="Reset view">⊡</button>
      </div>
    </div>
  );
}

// --------------------------------------------------------------------------
// SVG node-graph renderer — shared by the 6 node-positioned layouts.
// --------------------------------------------------------------------------

/** Point on the boundary of a (2*hw × 2*hh) box centred at `c`, along the
 * ray towards `toward`. Keeps edges/arrowheads off the node face. */
function clipToBox(c: XY, toward: XY, hw: number, hh: number): XY {
  const dx = toward.x - c.x;
  const dy = toward.y - c.y;
  if (dx === 0 && dy === 0) return c;
  const sx = dx !== 0 ? hw / Math.abs(dx) : Infinity;
  const sy = dy !== 0 ? hh / Math.abs(dy) : Infinity;
  const s = Math.min(sx, sy);
  return { x: c.x + dx * s, y: c.y + dy * s };
}

function SvgGraph({
  graph, edges, positions, sel, onSelect,
}: {
  graph: StanceGraph;
  edges: AggEdge[];
  positions: Map<string, XY>;
  sel: string | null;
  onSelect: (s: string | null) => void;
}) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const p of positions.values()) {
    minX = Math.min(minX, p.x); minY = Math.min(minY, p.y);
    maxX = Math.max(maxX, p.x); maxY = Math.max(maxY, p.y);
  }
  if (!isFinite(minX)) { minX = minY = maxX = maxY = 0; }
  const pad = 80;
  const vb: Box = {
    x: minX - NW / 2 - pad,
    y: minY - NH / 2 - pad,
    w: maxX - minX + NW + pad * 2,
    h: maxY - minY + NH + pad * 2,
  };

  return (
    <ZoomableSvg viewBox={vb}>
      {edges.map((e, i) => {
        const hot = sel !== null && e.from === sel;
        const dim = sel !== null && !hot;
        const color = hot ? "#5cc77a" : dim ? "#36404f" : "#5a7a9c";
        const lw = e.count >= 100 ? 28 : e.count >= 10 ? 22 : 16;

        // Self-loop (a move that re-enters its own stance): a small loop
        // bulging out of the node's top edge.
        if (e.from === e.to) {
          const p = positions.get(e.from);
          if (!p) return null;
          const ty = p.y - NH / 2;
          return (
            <g key={i} opacity={dim ? 0.5 : 1}>
              <path
                d={`M ${p.x - 20} ${ty} C ${p.x - 52} ${ty - 76} ${p.x + 52} ${ty - 76} ${p.x + 20} ${ty}`}
                fill="none" stroke={color} strokeWidth={hot ? 2.6 : 1.6}
                markerEnd={`url(#${hot ? "sg-arr-hot" : "sg-arr"})`}
              />
              {!dim && (
                <g transform={`translate(${p.x} ${ty - 58})`}>
                  <rect x={-lw / 2} y={-8} width={lw} height={16} rx={3}
                    fill="#14171d" opacity={0.92} />
                  <text textAnchor="middle" dominantBaseline="central"
                    fontSize={11} fill="#cdd2d8">{e.count}</text>
                </g>
              )}
            </g>
          );
        }

        const a = positions.get(e.from);
        const b = positions.get(e.to);
        if (!a || !b) return null;
        // Bow the curve perpendicular to the a→b vector. The opposite
        // edge (b→a) computes the perpendicular from b→a, so the two
        // bow to opposite sides and never overlap.
        const dx = b.x - a.x, dy = b.y - a.y;
        const d = Math.sqrt(dx * dx + dy * dy) || 1;
        const px = -dy / d, py = dx / d;
        const bow = Math.min(72, d * 0.2);
        const ctrl = { x: (a.x + b.x) / 2 + px * bow, y: (a.y + b.y) / 2 + py * bow };
        const s = clipToBox(a, ctrl, NW / 2 + 3, NH / 2 + 3);
        const t = clipToBox(b, ctrl, NW / 2 + 10, NH / 2 + 10);
        return (
          <g key={i} opacity={dim ? 0.5 : 1}>
            <path
              d={`M ${s.x} ${s.y} Q ${ctrl.x} ${ctrl.y} ${t.x} ${t.y}`}
              fill="none" stroke={color} strokeWidth={hot ? 2.6 : 1.6}
              markerEnd={`url(#${hot ? "sg-arr-hot" : "sg-arr"})`}
            />
            {!dim && (
              <g transform={`translate(${ctrl.x} ${ctrl.y})`}>
                <rect x={-lw / 2} y={-8} width={lw} height={16} rx={3}
                  fill="#14171d" opacity={0.92} />
                <text textAnchor="middle" dominantBaseline="central"
                  fontSize={11} fill="#cdd2d8">{e.count}</text>
              </g>
            )}
          </g>
        );
      })}

      {graph.stances.map((st) => {
        const p = positions.get(st.name);
        if (!p) return null;
        const isNeutral = st.name === NEUTRAL_STANCE;
        const selected = st.name === sel;
        return (
          <g
            key={st.name}
            transform={`translate(${p.x - NW / 2} ${p.y - NH / 2})`}
            onClick={() => onSelect(selected ? null : st.name)}
            style={{ cursor: "pointer" }}
          >
            <rect
              width={NW} height={NH} rx={10}
              fill={selected ? "#26344a" : isNeutral ? "#1b2a3a" : "#181b22"}
              stroke={selected ? "#6fa8e0" : isNeutral ? "#4f93ff" : "#3a4a5c"}
              strokeWidth={selected ? 2.4 : 1.4}
            />
            <text x={NW / 2} y={NH / 2 - 5} textAnchor="middle"
              fontSize={13} fontWeight={600} fill="#e6e8eb"
              style={{ pointerEvents: "none" }}>{st.name}</text>
            <text x={NW / 2} y={NH / 2 + 12} textAnchor="middle"
              fontSize={10} fill="#98a0a8"
              style={{ pointerEvents: "none" }}>{st.movesFrom} moves</text>
          </g>
        );
      })}
    </ZoomableSvg>
  );
}

// --------------------------------------------------------------------------
// Arc diagram — stances on a horizontal axis, transitions as arcs.
// --------------------------------------------------------------------------

function ArcDiagram({
  graph, edges, sel, onSelect,
}: {
  graph: StanceGraph;
  edges: AggEdge[];
  sel: string | null;
  onSelect: (s: string | null) => void;
}) {
  const names = orderedStanceNames(graph);
  const idx = new Map(names.map((n, i) => [n, i]));
  const gap = 188;
  const cw = 132, ch = 32;            // chip size
  const W = (names.length - 1) * gap;

  let maxH = 60;
  for (const e of edges) {
    const fi = idx.get(e.from), ti = idx.get(e.to);
    if (fi === undefined || ti === undefined || fi === ti) continue;
    maxH = Math.max(maxH, Math.min(280, 46 + Math.abs(ti - fi) * gap * 0.42));
  }
  const vb: Box = {
    x: -cw / 2 - 30,
    y: -(maxH + 46),
    w: W + cw + 60,
    h: maxH * 2 + 120,
  };

  return (
    <ZoomableSvg viewBox={vb}>
      <line x1={-cw / 2 - 10} y1={0} x2={W + cw / 2 + 10} y2={0}
        stroke="#2a2f3a" strokeWidth={1} />

      {edges.map((e, i) => {
        const fi = idx.get(e.from), ti = idx.get(e.to);
        if (fi === undefined || ti === undefined) return null;
        const hot = sel !== null && e.from === sel;
        const dim = sel !== null && !hot;
        const color = hot ? "#5cc77a" : dim ? "#36404f" : "#5a7a9c";
        // Self-loop: a small loop above the chip.
        if (fi === ti) {
          const x = fi * gap;
          return (
            <path
              key={i}
              d={`M ${x - 16} ${-ch / 2} C ${x - 40} ${-ch / 2 - 54} ${x + 40} ${-ch / 2 - 54} ${x + 16} ${-ch / 2}`}
              fill="none" stroke={color} strokeWidth={hot ? 2.4 : 1.5}
              opacity={dim ? 0.5 : 1}
              markerEnd={`url(#${hot ? "sg-arr-hot" : "sg-arr"})`}
            />
          );
        }
        const x1 = fi * gap, x2 = ti * gap;
        const forward = ti > fi;
        const h = Math.min(280, 46 + Math.abs(x2 - x1) * 0.42) * (forward ? -1 : 1);
        const y0 = forward ? -ch / 2 : ch / 2;
        return (
          <path
            key={i}
            d={`M ${x1} ${y0} Q ${(x1 + x2) / 2} ${h} ${x2} ${y0}`}
            fill="none" stroke={color} strokeWidth={hot ? 2.6 : 1.5}
            opacity={dim ? 0.5 : 1}
            markerEnd={`url(#${hot ? "sg-arr-hot" : "sg-arr"})`}
          />
        );
      })}

      {names.map((nm, i) => {
        const isNeutral = nm === NEUTRAL_STANCE;
        const selected = nm === sel;
        return (
          <g key={nm} transform={`translate(${i * gap - cw / 2} ${-ch / 2})`}
            onClick={() => onSelect(selected ? null : nm)} style={{ cursor: "pointer" }}>
            <rect width={cw} height={ch} rx={8}
              fill={selected ? "#26344a" : isNeutral ? "#1b2a3a" : "#181b22"}
              stroke={selected ? "#6fa8e0" : isNeutral ? "#4f93ff" : "#3a4a5c"}
              strokeWidth={selected ? 2.2 : 1.3} />
            <text x={cw / 2} y={ch / 2 + 1} textAnchor="middle"
              dominantBaseline="central" fontSize={12} fontWeight={600}
              fill="#e6e8eb" style={{ pointerEvents: "none" }}>{nm}</text>
          </g>
        );
      })}
    </ZoomableSvg>
  );
}

// --------------------------------------------------------------------------
// Adjacency matrix — from-row × to-column. No edges, so no crossings.
// --------------------------------------------------------------------------

function MatrixView({
  graph, edges, sel, onSelect,
}: {
  graph: StanceGraph;
  edges: AggEdge[];
  sel: string | null;
  onSelect: (s: string | null) => void;
}) {
  const names = orderedStanceNames(graph);
  const count = new Map<string, number>();
  for (const e of edges) count.set(`${e.from} ${e.to}`, e.count);
  const max = Math.max(1, ...edges.map((e) => e.count));

  return (
    <div className="stance-matrix-wrap">
      <table className="stance-matrix">
        <thead>
          <tr>
            <th className="mx-corner">from \ to</th>
            {names.map((n) => <th key={n}>{n}</th>)}
          </tr>
        </thead>
        <tbody>
          {names.map((from) => (
            <tr key={from}>
              <th
                className={`mx-row ${sel === from ? "mx-sel" : ""}`}
                onClick={() => onSelect(sel === from ? null : from)}
              >
                {from}
              </th>
              {names.map((to) => {
                const c = count.get(`${from} ${to}`) ?? 0;
                if (from === to) {
                  // Diagonal = self-loop count, blue-tinted to set it
                  // apart from the green cross-stance cells.
                  return (
                    <td
                      key={to}
                      className={c ? "mx-cell" : "mx-diag"}
                      style={c ? { background: `rgba(111,168,224,${0.16 + 0.62 * (c / max)})` } : undefined}
                      onClick={c ? () => onSelect(from) : undefined}
                      title={c ? `${from} self-loop: ${c} move${c === 1 ? "" : "s"}` : undefined}
                    >
                      {c || ""}
                    </td>
                  );
                }
                return (
                  <td
                    key={to}
                    className={c ? "mx-cell" : ""}
                    style={c ? { background: `rgba(92,199,122,${0.12 + 0.72 * (c / max)})` } : undefined}
                    onClick={c ? () => onSelect(from) : undefined}
                    title={c ? `${from} → ${to}: ${c} move${c === 1 ? "" : "s"}` : undefined}
                  >
                    {c || ""}
                  </td>
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

// --------------------------------------------------------------------------
// Transition table (shared across all 8 layouts).
// --------------------------------------------------------------------------

function StanceTransitionTable({
  rows, cid, navigate,
}: {
  rows: StanceTransition[];
  cid: string;
  navigate: Nav;
}) {
  if (rows.length === 0) {
    return (
      <p className="muted" style={{ fontSize: 13, marginTop: 14 }}>
        No stance transitions to show.
      </p>
    );
  }
  const sorted = [...rows].sort(
    (a, b) =>
      a.from.localeCompare(b.from) ||
      a.to.localeCompare(b.to) ||
      a.input.localeCompare(b.input),
  );
  return (
    <table className="stance-tx-table">
      <thead>
        <tr>
          <th>In stance</th><th>Press</th><th>Shifts to</th><th>Move</th>
        </tr>
      </thead>
      <tbody>
        {sorted.map((t) => (
          <tr
            key={t.order}
            className={t.slot >= 0 ? "tx-clickable" : undefined}
            onClick={
              t.slot >= 0
                ? () =>
                    navigate({
                      to: "/c/$cid/moves/$slot",
                      params: { cid, slot: String(t.slot) },
                      search: { move: t.moveId, order: t.order },
                    })
                : undefined
            }
          >
            <td>{t.from}</td>
            <td className="mono tx-input">{t.input}</td>
            <td className="tx-to"><strong>{t.to}</strong></td>
            <td className="muted">{t.moveName}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

/** Neutral first, then alphabetical — a stable order for matrix/arc. */
function orderedStanceNames(graph: StanceGraph): string[] {
  return graph.stances
    .map((s) => s.name)
    .sort((a, b) =>
      (a === NEUTRAL_STANCE ? -1 : 0) - (b === NEUTRAL_STANCE ? -1 : 0) ||
      a.localeCompare(b));
}

// --------------------------------------------------------------------------
// Main view.
// --------------------------------------------------------------------------

export function StanceGraphView({
  char, navigate,
}: {
  char: CharData;
  navigate: Nav;
}) {
  const [layout, setLayout] = useState<LayoutId>("radial");
  const [sel, setSel] = useState<string | null>(null);

  const graph = useMemo<StanceGraph>(
    () =>
      char.movelist
        ? buildStanceGraph(char.movelist.moves)
        : { stances: [], transitions: [] },
    [char.movelist],
  );
  const edges = useMemo(() => aggregateEdges(graph.transitions), [graph]);

  const positions = useMemo(() => {
    const names = graph.stances.map((s) => s.name);
    switch (layout) {
      case "layered-lr": return dagrePositions(graph.stances, edges, "LR");
      case "layered-tb": return dagrePositions(graph.stances, edges, "TB");
      case "circular": return circularPositions(names);
      case "radial": return radialPositions(names);
      case "force": return forcePositions(names, edges);
      case "grid": return gridPositions(names);
      default: return new Map<string, XY>();   // matrix / arc render differently
    }
  }, [graph, edges, layout]);

  const shown = useMemo(
    () => (sel === null
      ? graph.transitions
      : graph.transitions.filter((t) => t.from === sel)),
    [graph, sel],
  );

  if (!char.movelist || graph.stances.length === 0) {
    return (
      <p className="muted" style={{ padding: 16, fontSize: 13 }}>
        {char.movelist
          ? "This character has no stances — every move is performed from neutral."
          : "No movelist data for this character."}
      </p>
    );
  }

  const hint = LAYOUTS.find((l) => l.id === layout)!.hint;

  return (
    <>
      <div className="filters" style={{ marginTop: 4 }}>
        <span className="filter-label">Layout:</span>
        {LAYOUTS.map((l) => (
          <button
            key={l.id}
            className={`chip ${layout === l.id ? "active" : ""}`}
            onClick={() => setLayout(l.id)}
            title={l.hint}
          >
            {l.label}
          </button>
        ))}
      </div>

      <p className="muted" style={{ fontSize: 13, marginTop: 8 }}>
        {hint} An edge <strong>X → Z</strong> means a move done in stance X
        shifts you into stance Z — a loop back to X keeps the stance, an
        edge to <strong>Neutral</strong> exits it. Click a stance to focus
        its transitions; scroll to zoom, drag the backdrop to pan.
        {sel && (
          <>
            {" "}Showing <strong>{sel}</strong>{" "}
            <button className="chip" style={{ marginLeft: 4 }}
              onClick={() => setSel(null)}>clear</button>
          </>
        )}
      </p>

      <div className="stance-canvas">
        {layout === "matrix" ? (
          <MatrixView graph={graph} edges={edges} sel={sel} onSelect={setSel} />
        ) : layout === "arc" ? (
          <ArcDiagram graph={graph} edges={edges} sel={sel} onSelect={setSel} />
        ) : (
          <SvgGraph
            graph={graph} edges={edges} positions={positions}
            sel={sel} onSelect={setSel}
          />
        )}
      </div>

      <StanceTransitionTable rows={shown} cid={char.cid} navigate={navigate} />
    </>
  );
}
