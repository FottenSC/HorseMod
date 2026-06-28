import type { CharData, PlayerMoveFamily } from "../data/types";
import { familySearchText, familyStats, normalizeCommand } from "./families";

export interface FamilySearchResult {
  char: CharData;
  family: PlayerMoveFamily;
  score: number;
}

function exactCommandScore(family: PlayerMoveFamily, normalizedQuery: string): number {
  if (!normalizedQuery) return 0;
  const commands = [family.rootCommand, ...family.rows.map((row) => row.displayCommand)]
    .map(normalizeCommand)
    .filter(Boolean);
  if (commands.some((command) => command === normalizedQuery)) return 1000;
  if (commands.some((command) => command.startsWith(normalizedQuery))) return 600;
  if (commands.some((command) => command.includes(normalizedQuery))) return 300;
  return 0;
}

export function rankFamily(family: PlayerMoveFamily, query: string): number {
  const q = query.trim().toLowerCase();
  if (!q) return 1;
  const normalizedQuery = normalizeCommand(q);
  let score = exactCommandScore(family, normalizedQuery);
  const text = familySearchText(family);
  const terms = q.split(/\s+/).filter(Boolean);
  for (const term of terms) {
    if (text.includes(term)) score += 100;
  }
  if (family.rootName.toLowerCase().startsWith(q)) score += 250;
  if (family.context.toLowerCase().includes(q)) score += 75;
  return score;
}

export function filterFamilies(families: PlayerMoveFamily[], query: string): PlayerMoveFamily[] {
  const q = query.trim();
  if (!q) return [...families];
  const scored = families
    .map((family) => ({ family, score: rankFamily(family, q) }))
    .filter((item) => item.score > 0);
  scored.sort((a, b) => {
    if (b.score !== a.score) return b.score - a.score;
    const aStats = familyStats(a.family);
    const bStats = familyStats(b.family);
    return (aStats.startup ?? 9999) - (bStats.startup ?? 9999)
      || a.family.rootCommand.localeCompare(b.family.rootCommand);
  });
  return scored.map((item) => item.family);
}

export function searchAcrossChars(chars: CharData[], query: string): FamilySearchResult[] {
  const results: FamilySearchResult[] = [];
  for (const char of chars) {
    const families = char.movelist?.playerMoveFamilies ?? [];
    for (const family of families) {
      const score = rankFamily(family, query);
      if (!query.trim() || score > 0) results.push({ char, family, score });
    }
  }
  results.sort((a, b) => {
    if (b.score !== a.score) return b.score - a.score;
    return a.char.name.localeCompare(b.char.name)
      || a.family.rootCommand.localeCompare(b.family.rootCommand);
  });
  return results;
}
