import { createRootRoute, Link, Outlet } from "@tanstack/react-router";

export const Route = createRootRoute({
  component: RootComponent,
});

function RootComponent() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>
          <Link to="/">SC6 Moveset Browser</Link>
        </h1>
        <div className="breadcrumbs muted">
          Soulcalibur VI &middot; parsed from <code>dump/Battle</code>
        </div>
      </header>
      <main className="app-main">
        <Outlet />
      </main>
    </div>
  );
}
