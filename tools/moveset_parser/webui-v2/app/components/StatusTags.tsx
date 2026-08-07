import { Tag } from "@digdir/designsystemet-react";
import type { MetricEvidence, NativeLinkStatus, PlayerMoveFamilyRowSource, SourceConfidence } from "../data/types";
import { frameTone, type FrameTone } from "../lib/frames";

type TagColor = "info" | "success" | "warning" | "danger" | "neutral";

const CONFIDENCE_LABELS: Record<SourceConfidence, string> = {
  "game-authored": "game authored",
  "native-confirmed": "native",
  "native-inferred": "native inferred",
  unknown: "unknown",
};

const CONFIDENCE_COLORS: Record<SourceConfidence, TagColor> = {
  "game-authored": "success",
  "native-confirmed": "info",
  "native-inferred": "warning",
  unknown: "neutral",
};

const SOURCE_LABELS: Record<PlayerMoveFamilyRowSource, string> = {
  "game-movelist-table": "official movelist",
};

const TIMELINE_LABELS: Record<NativeLinkStatus, string> = {
  confirmed: "confirmed link",
  heuristic: "heuristic link",
  ambiguous: "ambiguous link",
  unresolved: "unresolved",
};

const TIMELINE_COLORS: Record<NativeLinkStatus, TagColor> = {
  confirmed: "success",
  heuristic: "warning",
  ambiguous: "danger",
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
  return (
    <Tag data-color="success" variant="outline">
      {SOURCE_LABELS[value]}
    </Tag>
  );
}

export function TimelineTag({ value }: { value: NativeLinkStatus }) {
  return (
    <Tag data-color={tagColor(TIMELINE_COLORS[value])} variant="outline">
      {TIMELINE_LABELS[value]}
    </Tag>
  );
}

export function MetricEvidenceTag({ value, metric }: { value: MetricEvidence; metric?: string }) {
  const evidenceLabel = value.status === "unknown" ? "unknown" : `${value.status}: ${value.source}`;
  const label = metric ? `${metric}: ${evidenceLabel}` : evidenceLabel;
  const color: TagColor = value.status === "game-authored" ? "success"
    : value.status === "native-confirmed" ? "info"
      : value.status === "native-inferred" ? "warning"
        : "neutral";
  return <Tag data-color={tagColor(color)} variant="outline">{label}</Tag>;
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
