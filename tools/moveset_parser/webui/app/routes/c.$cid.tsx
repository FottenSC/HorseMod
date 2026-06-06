import { createFileRoute, Link, Outlet, useMatches } from "@tanstack/react-router";
import type { CharData } from "../data/types";

export const Route = createFileRoute("/c/$cid")({
  loader: async ({ params }) => {
    const res = await fetch(`/data/chars/${params.cid}.json`);
    if (!res.ok) throw new Error(`Failed to load chara ${params.cid}`);
    return (await res.json()) as CharData;
  },
  component: CharLayout,
});

function CharLayout() {
  const char = Route.useLoaderData();
  const matches = useMatches();
  const currentPath = matches[matches.length - 1]?.pathname ?? "";

  // Active-tab match: section is the path segment immediately after /c/<cid>/.
  // Covers both /c/$cid/moves and /c/$cid/moves/$slot for the "moves" tab.
  const sectionMatch = currentPath.match(/\/c\/[^/]+\/([^/]+)/);
  const currentSection = sectionMatch?.[1] ?? "";
  const tabActive = (section: string) => currentSection === section;

  return (
    <>
      <div className="char-page-header">
        <div className="char-page-title">
          <h1>
            {char.name}
            {char.uncertain ? (
              <span className="muted" title="Identity not confirmed">?</span>
            ) : null}
          </h1>
          <div className="char-page-subtitle">
            <code className="mono">{char.cid}</code> · {char.kind}
            {char.movelist ? (
              <>
                {" · "}
                <strong>{char.movelist.moves.length}</strong> moves
              </>
            ) : null}
          </div>
        </div>
        <nav className="char-tabs">
          <Link
            to="/c/$cid/moves"
            params={{ cid: char.cid }}
            className={`char-tab ${tabActive("moves") ? "active" : ""}`}
            title="User-facing moves grouped by stance — derived from slot bytecode"
          >
            Moves
          </Link>
          <Link
            to="/c/$cid/graph"
            params={{ cid: char.cid }}
            className={`char-tab ${tabActive("graph") ? "active" : ""}`}
            title="Visual map of stances and move chains"
          >
            Graph
          </Link>
          <Link
            to="/c/$cid/cells"
            params={{ cid: char.cid }}
            className={`char-tab ${tabActive("cells") ? "active" : ""}`}
            title="Attack cells — raw per-hit engine data (damage, hitstun, range)"
          >
            Attack cells
          </Link>
          <Link
            to="/c/$cid/hitboxes"
            params={{ cid: char.cid }}
            className={`char-tab ${tabActive("hitboxes") ? "active" : ""}`}
          >
            Hitboxes
          </Link>
          <Link
            to="/c/$cid/internals"
            params={{ cid: char.cid }}
            className={`char-tab ${tabActive("internals") ? "active" : ""}`}
          >
            Internals
          </Link>
        </nav>
      </div>
      <Outlet />
    </>
  );
}
