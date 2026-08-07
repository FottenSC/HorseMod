import { afterEach, describe, expect, it, vi } from "vitest";
import {
  clearDataCache,
  loadChar,
  loadLookupIndex,
  loadPlayerChar,
  loadRawMovelist,
  loadRoster,
  prefetchFamilyDetail,
} from "../app/data/api";

afterEach(() => {
  clearDataCache();
  vi.restoreAllMocks();
});

function jsonResponse(value: unknown) {
  return Promise.resolve({
    ok: true,
    json: () => Promise.resolve(value),
    status: 200,
    statusText: "OK",
  } as Response);
}

describe("data api cache", () => {
  it("deduplicates repeated roster, legacy, and v2 shard loads", async () => {
    const fetchMock = vi.fn((path: string) => {
      if (path === "/data/roster.json") return jsonResponse({ chars: [] });
      if (path === "/data/v2/lookup-index.json") return jsonResponse({ schemaVersion: 2, chars: [], families: [] });
      if (path === "/data/v2/chars/003/player.json") return jsonResponse({ cid: "003", name: "Taki", kind: "base", files: {}, nativeSummary: {}, playerMoveFamilies: [], playerMoveSummary: {}, dashboard: { statsByFamily: {}, fastestFamilyIds: [], unsafeFamilyIds: [], plusFamilyIds: [], launcherFamilyIds: [] } });
      if (path === "/data/v2/chars/003/raw-movelist.json") return jsonResponse({ cid: "003", name: "Taki", kind: "base", categories: [], moveGroups: [], rows: [] });
      return jsonResponse({ cid: "003", name: "Taki", kind: "base", files: {} });
    });
    vi.stubGlobal("fetch", fetchMock);

    await Promise.all([loadRoster(), loadRoster()]);
    await Promise.all([loadChar("003"), loadChar("003")]);
    await Promise.all([loadLookupIndex(), loadLookupIndex()]);
    await Promise.all([loadPlayerChar("003"), loadPlayerChar("003")]);
    await Promise.all([loadRawMovelist("003"), loadRawMovelist("003")]);

    expect(fetchMock).toHaveBeenCalledTimes(5);
    expect(fetchMock).toHaveBeenCalledWith("/data/roster.json");
    expect(fetchMock).toHaveBeenCalledWith("/data/chars/003.json");
    expect(fetchMock).toHaveBeenCalledWith("/data/v2/lookup-index.json");
    expect(fetchMock).toHaveBeenCalledWith("/data/v2/chars/003/player.json");
    expect(fetchMock).toHaveBeenCalledWith("/data/v2/chars/003/raw-movelist.json");
  });

  it("loads global lookup from the v2 index without touching full character JSON", async () => {
    const fetchMock = vi.fn((path: string) => {
      if (path.includes("/data/chars/")) {
        throw new Error(`unexpected full character fetch: ${path}`);
      }
      return jsonResponse({ schemaVersion: 2, chars: [], families: [] });
    });
    vi.stubGlobal("fetch", fetchMock);

    await loadLookupIndex();
    await loadLookupIndex();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock).toHaveBeenCalledWith("/data/v2/lookup-index.json");
  });

  it("family detail prefetch is a thin player payload cache hit", async () => {
    const fetchMock = vi.fn(() => jsonResponse({
      cid: "003",
      name: "Taki",
      kind: "base",
      files: {},
      nativeSummary: {},
      playerMoveFamilies: [],
      playerMoveSummary: {},
      dashboard: { statsByFamily: {}, fastestFamilyIds: [], unsafeFamilyIds: [], plusFamilyIds: [], launcherFamilyIds: [] },
    }));
    vi.stubGlobal("fetch", fetchMock);

    await Promise.all([
      loadPlayerChar("003"),
      prefetchFamilyDetail("003", "family-aa"),
    ]);

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock).toHaveBeenCalledWith("/data/v2/chars/003/player.json");
  });
});
