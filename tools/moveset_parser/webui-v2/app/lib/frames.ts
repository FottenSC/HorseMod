import type { PlayerMoveFamilyRow } from "../data/types";

export type FrameTone = "empty" | "danger" | "warning" | "neutral" | "success" | "special";

export function parseFrameValue(value: string | number | null | undefined): number | null {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (value === null || value === undefined || value === "") return null;
  const match = String(value).match(/[+-]?\d+/);
  return match ? Number(match[0]) : null;
}

export function displayFrame(value: string | number | null | undefined): string {
  if (value === null || value === undefined || value === "") return "-";
  if (typeof value === "number") return value > 0 ? `+${value}` : String(value);
  return String(value);
}

export function frameTone(value: string | number | null | undefined): FrameTone {
  const text = String(value ?? "").trim().toUpperCase();
  if (!text) return "empty";
  if (/\b(KND|LNC|STN|GI|RE|LH)\b/.test(text)) return "special";
  const parsed = parseFrameValue(value);
  if (parsed === null) return "neutral";
  if (parsed <= -14) return "danger";
  if (parsed <= -10) return "warning";
  if (parsed > 0) return "success";
  return "neutral";
}

export function damageTotal(values: number[] | null | undefined): number | null {
  if (!values || values.length === 0) return null;
  return values.reduce((sum, value) => sum + value, 0);
}

export function displayDamage(values: number[] | null | undefined): string {
  const total = damageTotal(values);
  if (total === null) return "-";
  if (values && values.length > 1) return `${total} (${values.join("+")})`;
  return String(total);
}

export function bestStartup(rows: PlayerMoveFamilyRow[]): number | null {
  const starts = rows
    .map((row) => row.metrics.startup)
    .filter((value): value is number => typeof value === "number" && Number.isFinite(value));
  return starts.length ? Math.min(...starts) : null;
}

export function worstBlock(rows: PlayerMoveFamilyRow[]): string | number | null {
  const values = rows
    .map((row) => row.metrics.block)
    .map((value) => ({ raw: value, parsed: parseFrameValue(value) }))
    .filter((value): value is { raw: string | number; parsed: number } => value.parsed !== null);
  if (!values.length) return null;
  values.sort((a, b) => a.parsed - b.parsed);
  return values[0].raw;
}

export function bestHit(rows: PlayerMoveFamilyRow[]): string | number | null {
  const values = rows
    .map((row) => row.metrics.hit)
    .map((value) => ({ raw: value, parsed: parseFrameValue(value) }))
    .filter((value): value is { raw: string | number; parsed: number } => value.parsed !== null);
  if (!values.length) return null;
  values.sort((a, b) => b.parsed - a.parsed);
  return values[0].raw;
}

export function rowLooksLikeLauncher(row: PlayerMoveFamilyRow): boolean {
  const blob = `${row.metrics.hit ?? ""} ${row.metrics.counterHit ?? ""} ${row.notes ?? ""}`.toUpperCase();
  return /\b(LNC|KND|STN|LAUNCH|KNOCK)/.test(blob);
}
