import type { CharData, Roster } from "./types";

async function fetchJson<T>(path: string): Promise<T> {
  const res = await fetch(path);
  if (!res.ok) {
    throw new Error(`Failed to load ${path}: ${res.status} ${res.statusText}`);
  }
  return (await res.json()) as T;
}

export function loadRoster(): Promise<Roster> {
  return fetchJson<Roster>("/data/roster.json");
}

export function loadChar(cid: string): Promise<CharData> {
  return fetchJson<CharData>(`/data/chars/${cid}.json`);
}

export async function loadAllPlayableChars(roster: Roster): Promise<CharData[]> {
  const candidates = roster.chars.filter((char) => char.files?.khd || char.attackCount);
  const settled = await Promise.allSettled(candidates.map((char) => loadChar(char.cid)));
  return settled
    .filter((result): result is PromiseFulfilledResult<CharData> => result.status === "fulfilled")
    .map((result) => result.value)
    .filter((char) => Boolean(char.movelist?.moves?.length));
}
