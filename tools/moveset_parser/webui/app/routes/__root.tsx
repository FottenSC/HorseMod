import { createRootRoute, Link, Outlet } from "@tanstack/react-router";

export const Route = createRootRoute({
  component: RootComponent,
});

function RootComponent() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>
          <Link to="/">SC6 Frame Data</Link>
        </h1>
        <div className="breadcrumbs muted">
          Soulcalibur VI move browser
        </div>
      </header>
      <main className="app-main">
        <Outlet />
      </main>
    </div>
  );
}
