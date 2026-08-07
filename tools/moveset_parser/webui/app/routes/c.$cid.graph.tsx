import { createFileRoute, useNavigate } from "@tanstack/react-router";
import { useMemo, useState, useEffect } from "react";
import {
  ReactFlow,
  Background,
  Controls,
  type Node,
  type Edge,
  type NodeMouseHandler,
  Position,
  MarkerType,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import dagre from "@dagrejs/dagre";
import type { CharData, SlotEdge } from "../data/types";
import { StanceGraphView } from "../components/StanceGraph";

export const Route = createFileRoute("/c/$cid/graph")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error("Failed to load chara");
    return (await res.json()) as CharData;
  },
  component: GraphTab,
});

type ViewMode = "stance-graph" | "stances" | "moves";

const NODE_W = 200;
const NODE_H = 60;

function GraphTab() {
  const char = Route.useLoaderData();
  const navigate = useNavigate();
  const khd = char.khd;

  const [mode, setMode] = useState<ViewMode>("stance-graph");
  const [selectedRoot, setSelectedRoot] = useState<number | null>(null);
  const [depth, setDepth] = useState(2);

  // On first load, default the selected root to the richest stance.
  useEffect(() => {
    if (selectedRoot === null && khd?.stanceRoots.length) {
      setSelectedRoot(khd.stanceRoots[0].slot);
    }
  }, [khd, selectedRoot]);

  // The stance graph is built purely from the movelist, so it works even
  // when a character has no KHD (e.g. Inferno). Only the slot map / move
  // tree need KHD — those are gated inside the render below.

  return (
    <>
      <div className="filters">
        <span className="filter-label">View:</span>
        <button
          className={`chip ${mode === "stance-graph" ? "active" : ""}`}
          onClick={() => setMode("stance-graph")}
          title="Named-stance transition graph built from the movelist — in stance X, input Y shifts you to stance Z"
        >
          Stance graph
        </button>
        <button
          className={`chip ${mode === "stances" ? "active" : ""}`}
          onClick={() => setMode("stances")}
          title="Low-level slot map: stance-root slots and their bytecode transitions"
        >
          Slot map
        </button>
        <button
          className={`chip ${mode === "moves" ? "active" : ""}`}
          onClick={() => setMode("moves")}
          title="Move tree from one stance, depth-limited"
        >
          Move tree
        </button>

        {mode === "moves" && khd && (
          <>
            <span className="filter-label" style={{ marginLeft: "1em" }}>
              Stance:
            </span>
            {khd.stanceRoots.slice(0, 8).map((r) => (
              <button
                key={r.slot}
                className={`chip ${selectedRoot === r.slot ? "active" : ""}`}
                onClick={() => setSelectedRoot(r.slot)}
                title={`Anim ${r.anim} · ${r.distinctInputs} distinct inputs`}
              >
                slot {r.slot}
              </button>
            ))}
            <span className="filter-label" style={{ marginLeft: "1em" }}>
              Depth:
            </span>
            {[1, 2, 3, 4].map((n) => (
              <button
                key={n}
                className={`chip ${depth === n ? "active" : ""}`}
                onClick={() => setDepth(n)}
              >
                {n}
              </button>
            ))}
          </>
        )}
      </div>

      {mode === "stance-graph" ? (
        <StanceGraphView char={char} navigate={navigate} />
      ) : !khd ? (
        <div className="notice" style={{ marginTop: 12 }}>
          No KHD data for this character — the slot map and move tree need
          it. The <strong>Stance graph</strong> works without it.
        </div>
      ) : (
        <>
          <p className="muted" style={{ fontSize: 13, marginTop: 8 }}>
            {mode === "stances" ? (
              <>
                Low-level <strong>slot map</strong>: each node is a
                stance-root slot the engine bytecode branches from — useful
                for reverse-engineering. For the player-facing named-stance
                view use <strong>Stance graph</strong>. Click a node to open
                its slot detail.
              </>
            ) : (
              <>
                Move tree rooted at <code className="mono">slot {selectedRoot}</code>{" "}
                (anim{" "}
                <code className="mono">
                  {khd.stanceRoots.find((r) => r.slot === selectedRoot)?.anim ?? "?"}
                </code>
                ), expanded to depth {depth}. Following only user-input edges
                (buttons / directions / commands). Click any node to inspect it.
              </>
            )}
          </p>

          <div
            style={{
              width: "100%",
              height: "70vh",
              background: "#14171d",
              border: "1px solid #262a33",
              borderRadius: 8,
            }}
          >
            {mode === "stances" ? (
              <StanceMap khd={khd} cid={char.cid} navigate={navigate} />
            ) : (
              selectedRoot !== null && (
                <MoveTree
                  khd={khd}
                  cid={char.cid}
                  rootSlot={selectedRoot}
                  depth={depth}
                  navigate={navigate}
                />
              )
            )}
          </div>
        </>
      )}
    </>
  );
}

// --------------------------------------------------------------------------
// Layout helper
// --------------------------------------------------------------------------

function layoutGraph(
  nodes: Node[],
  edges: Edge[],
  direction: "LR" | "TB" = "LR",
): { nodes: Node[]; edges: Edge[] } {
  const g = new dagre.graphlib.Graph();
  g.setGraph({
    rankdir: direction,
    nodesep: 30,
    ranksep: 80,
    marginx: 32,
    marginy: 32,
  });
  g.setDefaultEdgeLabel(() => ({}));

  nodes.forEach((n) => g.setNode(n.id, { width: NODE_W, height: NODE_H }));
  edges.forEach((e) => g.setEdge(e.source, e.target));

  dagre.layout(g);

  const laidOut: Node[] = nodes.map((n) => {
    const pos = g.node(n.id);
    return {
      ...n,
      position: { x: pos.x - NODE_W / 2, y: pos.y - NODE_H / 2 },
      sourcePosition: direction === "LR" ? Position.Right : Position.Bottom,
      targetPosition: direction === "LR" ? Position.Left : Position.Top,
    };
  });

  return { nodes: laidOut, edges };
}

// --------------------------------------------------------------------------
// Edge styling per predicate kind
// --------------------------------------------------------------------------

const KIND_COLORS: Record<string, string> = {
  buttons: "#4f93ff",
  direction: "#5cc77a",
  command: "#c977c2",
  auto: "#c9a877",
  frame: "#c9a877",
  stance: "#7a8cc9",
  "from-move": "#7a8cc9",
  always: "#7a7a7a",
  other: "#7a7a7a",
  unknown: "#555",
  indirect: "#a08077",
  range: "#7a7a7a",
};

function edgeFromSlot(
  e: SlotEdge,
  idx: number,
): Edge {
  const color = KIND_COLORS[e.kind] ?? "#7a7a7a";
  return {
    id: `e${idx}-${e.src}-${e.dst}-${e.pc}`,
    source: String(e.src),
    target: String(e.dst),
    label: e.input,
    labelStyle: {
      fill: "#e6e8eb",
      fontSize: 11,
      fontFamily: "ui-monospace, monospace",
    },
    labelBgStyle: { fill: "#14171d", fillOpacity: 0.85 },
    labelBgPadding: [3, 5],
    labelBgBorderRadius: 3,
    style: { stroke: color, strokeWidth: 1.4 },
    markerEnd: { type: MarkerType.ArrowClosed, color, width: 14, height: 14 },
    animated: e.kind === "buttons" || e.kind === "direction" || e.kind === "command",
  };
}

// --------------------------------------------------------------------------
// Custom node renderer (slot summary)
// --------------------------------------------------------------------------

function buildSlotNode(
  slotIdx: number,
  khd: CharData["khd"],
  highlight: "root" | "leaf" | "default" = "default",
): Node {
  const slot = khd!.slots[slotIdx];
  const cellIdx = slot?.cellVariants.find(
    (c) => c >= 0 && c < (khd?.cells.length ?? 0),
  );
  const cell = cellIdx !== undefined ? khd!.cells[cellIdx] : undefined;

  let title: string;
  let subtitle: string;
  if (cell?.role === "Attack") {
    title = `${cell.class} · ${cell.damage}dmg · window coord ${cell.activeStartCoordinate}`;
    subtitle = `slot ${slotIdx} · anim ${slot?.animationIndex ?? "?"}`;
  } else if (cell) {
    title = cell.role;
    subtitle = `slot ${slotIdx} · anim ${slot?.animationIndex ?? "?"}`;
  } else {
    title = slot ? `anim ${slot.animationIndex}` : `slot ${slotIdx}`;
    subtitle = `slot ${slotIdx}` + (slot ? "" : " (?)");
  }

  const bg =
    highlight === "root"
      ? "#1b2a3a"
      : cell?.role === "Attack"
        ? "#181b22"
        : "#161922";
  const border =
    highlight === "root"
      ? "#4f93ff"
      : cell?.role === "Attack"
        ? "#3a4a5c"
        : "#2a2e38";

  return {
    id: String(slotIdx),
    data: {
      label: (
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            padding: "2px 6px",
            lineHeight: 1.25,
          }}
        >
          <div style={{ fontSize: 12, fontWeight: 600, color: "#e6e8eb" }}>
            {title}
          </div>
          <div
            style={{
              fontSize: 10,
              color: "#98a0a8",
              fontFamily: "ui-monospace, monospace",
            }}
          >
            {subtitle}
          </div>
        </div>
      ),
    },
    position: { x: 0, y: 0 },
    style: {
      background: bg,
      border: `1px solid ${border}`,
      borderRadius: 6,
      width: NODE_W,
      height: NODE_H,
      padding: 0,
    },
  };
}

// --------------------------------------------------------------------------
// Stance map (high level)
// --------------------------------------------------------------------------

function StanceMap({
  khd,
  cid,
  navigate,
}: {
  khd: CharData["khd"];
  cid: string;
  navigate: ReturnType<typeof useNavigate>;
}) {
  const { nodes, edges } = useMemo(() => {
    if (!khd) return { nodes: [] as Node[], edges: [] as Edge[] };
    const rootSet = new Set(khd.stanceRoots.map((r) => r.slot));
    const nodeList: Node[] = khd.stanceRoots.map((r) =>
      buildSlotNode(r.slot, khd, "root"),
    );
    const edgeList: Edge[] = [];
    let idx = 0;
    for (const e of khd.slotEdges) {
      if (e.bank !== 0) continue;
      if (!rootSet.has(e.src) || !rootSet.has(e.dst)) continue;
      if (e.src === e.dst) continue;
      edgeList.push(edgeFromSlot(e, idx++));
    }
    return layoutGraph(nodeList, edgeList, "LR");
  }, [khd]);

  const onNodeClick: NodeMouseHandler = (_, node) => {
    navigate({ to: "/c/$cid/moves/$slot", params: { cid, slot: node.id }, search: { move: undefined, order: undefined } });
  };

  if (nodes.length === 0) {
    return (
      <p className="muted" style={{ padding: 16, fontSize: 13 }}>
        No stance roots identified. Try the Move tree view from any slot.
      </p>
    );
  }

  return (
    <ReactFlow
      nodes={nodes}
      edges={edges}
      onNodeClick={onNodeClick}
      fitView
      fitViewOptions={{ padding: 0.15 }}
      proOptions={{ hideAttribution: true }}
      colorMode="dark"
    >
      <Background gap={20} size={1} color="#262a33" />
      <Controls position="bottom-right" />
    </ReactFlow>
  );
}

// --------------------------------------------------------------------------
// Move tree (BFS from one root, user-input edges only)
// --------------------------------------------------------------------------

const USER_INPUT_KINDS = new Set(["buttons", "direction", "command"]);

function MoveTree({
  khd,
  cid,
  rootSlot,
  depth,
  navigate,
}: {
  khd: CharData["khd"];
  cid: string;
  rootSlot: number;
  depth: number;
  navigate: ReturnType<typeof useNavigate>;
}) {
  const { nodes, edges } = useMemo(() => {
    if (!khd) return { nodes: [] as Node[], edges: [] as Edge[] };

    const edgesBySrc = new Map<number, SlotEdge[]>();
    for (const e of khd.slotEdges) {
      if (e.bank !== 0) continue;
      if (!USER_INPUT_KINDS.has(e.kind)) continue;
      const arr = edgesBySrc.get(e.src) ?? [];
      arr.push(e);
      edgesBySrc.set(e.src, arr);
    }

    // BFS by (slot, depth). We keep depth-keyed visits so the SAME slot
    // can appear if reached via two different paths at different depths,
    // but we cap total nodes so the layout stays sane.
    const visited = new Set<number>();
    const queue: { slot: number; d: number }[] = [{ slot: rootSlot, d: 0 }];
    visited.add(rootSlot);
    const nodeList: Node[] = [buildSlotNode(rootSlot, khd, "root")];
    const edgeList: Edge[] = [];
    let edgeIdx = 0;
    const MAX_NODES = 120;

    while (queue.length && nodeList.length < MAX_NODES) {
      const { slot, d } = queue.shift()!;
      if (d >= depth) continue;
      const out = edgesBySrc.get(slot) ?? [];
      for (const e of out) {
        if (e.dst === slot) continue;
        if (!visited.has(e.dst)) {
          visited.add(e.dst);
          nodeList.push(buildSlotNode(e.dst, khd, "default"));
          queue.push({ slot: e.dst, d: d + 1 });
        }
        edgeList.push(edgeFromSlot(e, edgeIdx++));
        if (nodeList.length >= MAX_NODES) break;
      }
    }

    return layoutGraph(nodeList, edgeList, "LR");
  }, [khd, rootSlot, depth]);

  const onNodeClick: NodeMouseHandler = (_, node) => {
    navigate({ to: "/c/$cid/moves/$slot", params: { cid, slot: node.id }, search: { move: undefined, order: undefined } });
  };

  if (nodes.length <= 1) {
    return (
      <p className="muted" style={{ padding: 16, fontSize: 13 }}>
        No user-input transitions from this stance. Try a different root or
        switch to Stance map.
      </p>
    );
  }

  return (
    <ReactFlow
      nodes={nodes}
      edges={edges}
      onNodeClick={onNodeClick}
      fitView
      fitViewOptions={{ padding: 0.1 }}
      proOptions={{ hideAttribution: true }}
      colorMode="dark"
    >
      <Background gap={20} size={1} color="#262a33" />
      <Controls position="bottom-right" />
    </ReactFlow>
  );
}
