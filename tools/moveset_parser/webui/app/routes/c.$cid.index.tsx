import { createFileRoute, Navigate } from "@tanstack/react-router";

// Default tab is "moves" — redirect /c/<cid> → /c/<cid>/moves
export const Route = createFileRoute("/c/$cid/")({
  component: Index,
});

function Index() {
  const { cid } = Route.useParams();
  return <Navigate to="/c/$cid/moves" params={{ cid }} replace />;
}
