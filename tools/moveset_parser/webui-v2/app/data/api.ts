import type { CharData, LookupIndex, PlayerCharPayload, RawMovelistPayload, Roster } from "./types";

const jsonCache = new Map<string, Promise<unknown>>();
const playableCache = new Map<string, Promise<CharData[]>>();

async function fetchJson<T>(path: string): Promise<T> {
  const cached = jsonCache.get(path) as Promise<T> | undefined;
  if (cached) return cached;

  const promise = fetch(path)
    .then(async (res) => {
      if (!res.ok) {
        throw new Error(`Failed to load ${path}: ${res.status} ${res.statusText}`);
      }
      return (await res.json()) as T;
    })
    .catch((error) => {
      jsonCache.delete(path);
      throw error;
    });
  jsonCache.set(path, promise);
  return promise;
}

export function loadRoster(): Promise<Roster> {
  return fetchJson<Roster>("/data/roster.json");
}

export function loadChar(cid: string): Promise<CharData> {
  return fetchJson<CharData>(`/data/chars/${cid}.json`);
}

export function loadLookupIndex(): Promise<LookupIndex> {
  return fetchJson<LookupIndex>("/data/v2/lookup-index.json");
}

export function loadPlayerChar(cid: string): Promise<PlayerCharPayload> {
  return fetchJson<PlayerCharPayload>(`/data/v2/chars/${cid}/player.json`);
}

export function loadRawMovelist(cid: string): Promise<RawMovelistPayload> {
  return fetchJson<RawMovelistPayload>(`/data/v2/chars/${cid}/raw-movelist.json`);
}

export function prefetchPlayerChar(cid: string): Promise<PlayerCharPayload> {
  return loadPlayerChar(cid);
}

export function prefetchFamilyDetail(cid: string, _familyId: string): Promise<PlayerCharPayload> {
  return prefetchPlayerChar(cid);
}

export async function loadAllPlayableChars(roster: Roster): Promise<CharData[]> {
  const candidates = roster.chars.filter((char) => char.files?.khd || char.attackCount);
  const key = candidates.map((char) => char.cid).join(",");
  const cached = playableCache.get(key);
  if (cached) return cached;

  const promise = Promise.allSettled(candidates.map((char) => loadChar(char.cid))).then((settled) => settled
    .filter((result): result is PromiseFulfilledResult<CharData> => result.status === "fulfilled")
    .map((result) => result.value)
    .filter((char) => Boolean(char.movelist?.moves?.length)));
  playableCache.set(key, promise);
  return promise;
}

export function clearDataCache() {
  jsonCache.clear();
  playableCache.clear();
}
