import { Button } from "@digdir/designsystemet-react";
import {
  Link,
  Outlet,
  createRootRoute,
  createRoute,
  createRouter,
  useNavigate,
} from "@tanstack/react-router";
import type { AppSearch, CharData, Roster } from "./data/types";
import { loadChar, loadRoster } from "./data/api";
import { MatchupSelector } from "./components/MatchupSelector";
import { CharacterDashboardPage, CharacterFamiliesPage, CharacterFamilyDetailPage, CharacterRawPage } from "./pages/CharacterPage";
import { LookupPage } from "./pages/LookupPage";
import { RosterPage } from "./pages/RosterPage";

function optionalSearchString(value: unknown): string | undefined {
  return typeof value === "string" && value.trim() ? value : undefined;
}

function validateSearch(search: Record<string, unknown>): AppSearch {
  return {
    me: optionalSearchString(search.me),
    vs: optionalSearchString(search.vs),
    q: optionalSearchString(search.q),
  };
}

function persistMatchup(search: AppSearch) {
  try {
    const current = JSON.parse(localStorage.getItem("sc6-webui-v2-matchup") ?? "{}") as AppSearch;
    localStorage.setItem("sc6-webui-v2-matchup", JSON.stringify({ ...current, ...search }));
  } catch {
    // Local storage is just a convenience cache; URL params remain canonical.
  }
}

function readStoredMatchup(): AppSearch {
  try {
    return JSON.parse(localStorage.getItem("sc6-webui-v2-matchup") ?? "{}") as AppSearch;
  } catch {
    return {};
  }
}

function navigateLoose(navigate: ReturnType<typeof useNavigate>, options: unknown) {
  void (navigate as unknown as (opts: unknown) => Promise<void>)(options);
}

function AppShell() {
  const roster = rootRoute.useLoaderData();
  const search = rootRoute.useSearch();
  const navigate = useNavigate();
  const stored = readStoredMatchup();
  const effectiveSearch = {
    ...search,
    me: search.me ?? stored.me,
    vs: search.vs ?? stored.vs,
  };

  function updateSearch(patch: Partial<AppSearch>) {
    const next = { ...effectiveSearch, ...patch };
    persistMatchup(next);
    navigateLoose(navigate, { search: next, replace: true });
  }

  return (
    <div className="app-shell" data-color-scheme="auto" data-size="md">
      <header className="app-header">
        <Link to="/" search={effectiveSearch} className="brand-link">
          SC6 Move Lookup
        </Link>
        <nav className="app-nav" aria-label="Primary navigation">
          <Button variant="tertiary" asChild>
            <Link to="/" search={effectiveSearch}>Roster</Link>
          </Button>
          <Button variant="tertiary" asChild>
            <Link to="/lookup" search={effectiveSearch}>Lookup</Link>
          </Button>
        </nav>
        <MatchupSelector roster={roster} search={effectiveSearch} onChange={updateSearch} />
      </header>
      <main className="app-main">
        <Outlet />
      </main>
    </div>
  );
}

function ErrorView({ error }: { error: Error }) {
  return (
    <div className="app-shell" data-color-scheme="auto" data-size="md">
      <main className="app-main">
        <section className="error-panel">
          <h1>Could not load this view</h1>
          <p>{error.message}</p>
          <Button asChild><Link to="/">Back to roster</Link></Button>
        </section>
      </main>
    </div>
  );
}

export const rootRoute = createRootRoute({
  validateSearch,
  loader: loadRoster,
  component: AppShell,
  errorComponent: ({ error }) => <ErrorView error={error instanceof Error ? error : new Error(String(error))} />,
});

function useCharacterNav(char: CharData, search: AppSearch) {
  const navigate = useNavigate();
  return (tab: "dashboard" | "families" | "raw") => {
    const to = tab === "dashboard"
      ? "/c/$cid"
      : tab === "families"
        ? "/c/$cid/families"
        : "/c/$cid/raw";
    void navigate({ to, params: { cid: char.cid }, search });
  };
}

const indexRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/",
  component: () => {
    const roster = rootRoute.useLoaderData();
    const search = rootRoute.useSearch();
    return <RosterPage roster={roster} search={search} />;
  },
});

const lookupRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/lookup",
  component: () => {
    const roster = rootRoute.useLoaderData();
    const search = rootRoute.useSearch();
    const navigate = useNavigate();
    return (
      <LookupPage
        roster={roster}
        search={search}
        onQueryChange={(q) => navigateLoose(navigate, { to: "/lookup", search: { ...search, q: q || undefined }, replace: true })}
      />
    );
  },
});

const characterRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/c/$cid",
  loader: ({ params }) => loadChar(params.cid),
  component: () => {
    const char = characterRoute.useLoaderData();
    const search = rootRoute.useSearch();
    return (
      <CharacterDashboardPage
        char={char}
        search={search}
        activeTab="dashboard"
        onTabChange={useCharacterNav(char, search)}
      />
    );
  },
});

const characterFamiliesRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/c/$cid/families",
  loader: ({ params }) => loadChar(params.cid),
  component: () => {
    const char = characterFamiliesRoute.useLoaderData();
    const search = rootRoute.useSearch();
    return (
      <CharacterFamiliesPage
        char={char}
        search={search}
        activeTab="families"
        onTabChange={useCharacterNav(char, search)}
      />
    );
  },
});

const characterFamilyDetailRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/c/$cid/families/$familyId",
  loader: ({ params }) => loadChar(params.cid),
  component: () => {
    const char = characterFamilyDetailRoute.useLoaderData();
    const params = characterFamilyDetailRoute.useParams();
    const search = rootRoute.useSearch();
    return (
      <CharacterFamilyDetailPage
        char={char}
        search={search}
        activeTab="families"
        onTabChange={useCharacterNav(char, search)}
        familyId={params.familyId}
      />
    );
  },
});

const characterRawRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: "/c/$cid/raw",
  loader: ({ params }) => loadChar(params.cid),
  component: () => {
    const char = characterRawRoute.useLoaderData();
    const search = rootRoute.useSearch();
    return (
      <CharacterRawPage
        char={char}
        search={search}
        activeTab="raw"
        onTabChange={useCharacterNav(char, search)}
      />
    );
  },
});

const routeTree = rootRoute.addChildren([
  indexRoute,
  lookupRoute,
  characterRoute,
  characterFamiliesRoute,
  characterFamilyDetailRoute,
  characterRawRoute,
]);

export const router = createRouter({
  routeTree,
  defaultPreload: "intent",
  defaultStaleTime: 60_000,
});

declare module "@tanstack/react-router" {
  interface Register {
    router: typeof router;
  }
}

export type AppRouter = typeof router;
