import "@digdir/designsystemet-css";
import "@digdir/designsystemet-css/theme.css";
import React from "react";
import ReactDOM from "react-dom/client";
import { RouterProvider } from "@tanstack/react-router";
import { router } from "./router";
import "./styles.css";

const rootEl = document.getElementById("root");
if (!rootEl) throw new Error("#root not found");

ReactDOM.createRoot(rootEl).render(
  <React.StrictMode>
    <RouterProvider router={router} />
  </React.StrictMode>,
);
