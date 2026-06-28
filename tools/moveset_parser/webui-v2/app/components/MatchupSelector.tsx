import { Select, SelectOption } from "@digdir/designsystemet-react";
import type { AppSearch, Roster } from "../data/types";

export interface MatchupSelectorProps {
  roster: Roster;
  search: AppSearch;
  onChange: (patch: Partial<AppSearch>) => void;
}

export function MatchupSelector({ roster, search, onChange }: MatchupSelectorProps) {
  const playable = roster.chars.filter((char) => char.files?.khd || char.attackCount);
  return (
    <div className="matchup-selector" data-size="sm" aria-label="Matchup selector">
      <label>
        <span>I play</span>
        <Select
          width="auto"
          aria-label="I play"
          value={search.me ?? ""}
          onChange={(event) => onChange({ me: event.currentTarget.value || undefined })}
        >
          <SelectOption value="">Any</SelectOption>
          {playable.map((char) => (
            <SelectOption key={char.cid} value={char.cid}>
              {char.name}
            </SelectOption>
          ))}
        </Select>
      </label>
      <label>
        <span>Against</span>
        <Select
          width="auto"
          aria-label="Against"
          value={search.vs ?? ""}
          onChange={(event) => onChange({ vs: event.currentTarget.value || undefined })}
        >
          <SelectOption value="">Any</SelectOption>
          {playable.map((char) => (
            <SelectOption key={char.cid} value={char.cid}>
              {char.name}
            </SelectOption>
          ))}
        </Select>
      </label>
    </div>
  );
}
