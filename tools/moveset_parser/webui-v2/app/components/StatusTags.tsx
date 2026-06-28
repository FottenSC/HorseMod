import { Tag } from "@digdir/designsystemet-react";
import type { PlayerMoveFamilyRowSource, SourceConfidence, TimelineStatus } from "../data/types";
import { frameTone, type FrameTone } from "../lib/frames";

type TagColor = "info" | "success" | "warning" | "danger" | "neutral";

const CONFIDENCE_LABELS: Record<SourceConfidence, string> = {
  "runtime-validated": "runtime",
  "community-confirmed": "community",
  "native-confirmed": "native",
  "mixed-supported": "mixed",
  "native-inferred": "native inferred",
  weak: "weak",
  conflict: "conflict",
  unknown: "unknown",
};

const CONFIDENCE_COLORS: Record<SourceConfidence, TagColor> = {
  "runtime-validated": "success",
  "community-confirmed": "success",
  "native-confirmed": "info",
  "mixed-supported": "info",
  "native-inferred": "warning",
  weak: "warning",
  conflict: "danger",
  unknown: "neutral",
};

const SOURCE_LABELS: Record<PlayerMoveFamilyRowSource, string> = {
  community: "community",
  movelist: "movelist",
  mixed: "mixed",
  "native-inferred": "native inferred",
};

const TIMELINE_LABELS: Record<TimelineStatus, string> = {
  resolved: "resolved",
  partial: "partial",
  "native-cell-only": "native cell",
  unresolved: "unresolved",
};

const TIMELINE_COLORS: Record<TimelineStatus, TagColor> = {
  resolved: "success",
  partial: "info",
  "native-cell-only": "warning",
  unresolved: "neutral",
};

function tagColor(color: TagColor): string | undefined {
  return color === "neutral" ? undefined : color;
}

export function ConfidenceTag({ value }: { value: SourceConfidence }) {
  return (
    <Tag data-color={tagColor(CONFIDENCE_COLORS[value])} variant="outline">
      {CONFIDENCE_LABELS[value]}
    </Tag>
  );
}

export function SourceTag({ value }: { value: PlayerMoveFamilyRowSource }) {
  const color = value === "community" || value === "mixed" ? "success" : value === "movelist" ? "info" : "warning";
  return (
    <Tag data-color={color} variant="outline">
      {SOURCE_LABELS[value]}
    </Tag>
  );
}

export function TimelineTag({ value }: { value: TimelineStatus }) {
  return (
    <Tag data-color={tagColor(TIMELINE_COLORS[value])} variant="outline">
      {TIMELINE_LABELS[value]}
    </Tag>
  );
}

export function FrameTag({ value }: { value: string | number | null | undefined }) {
  const tone = frameTone(value);
  const color: Record<FrameTone, TagColor> = {
    empty: "neutral",
    danger: "danger",
    warning: "warning",
    neutral: "neutral",
    success: "success",
    special: "info",
  };
  return (
    <span className={`frame-tag frame-tag-${tone}`} data-color={tagColor(color[tone])}>
      {value === null || value === undefined || value === "" ? "-" : String(value)}
    </span>
  );
}

export function HitLevelTags({ levels }: { levels: string[] }) {
  if (!levels.length) return <span className="muted">-</span>;
  return (
    <span className="hit-level-tags" aria-label={levels.join(", ")}>
      {levels.slice(0, 4).map((level, idx) => {
        const key = level.toLowerCase().replace(/[^a-z]+/g, "-") || "other";
        const short = level.includes("High") ? "H"
          : level.includes("Low") ? "L"
            : level.includes("Throw") ? "T"
              : level.includes("Unblock") ? "U"
                : level.includes("Special") ? "S"
                  : "M";
        return (
          <span key={`${level}-${idx}`} className={`hit-level hit-level-${key}`} title={level}>
            {short}
          </span>
        );
      })}
      {levels.length > 4 ? <span className="hit-level-more">+{levels.length - 4}</span> : null}
    </span>
  );
}
