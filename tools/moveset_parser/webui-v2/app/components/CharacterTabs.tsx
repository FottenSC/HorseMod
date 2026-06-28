import { Tabs } from "@digdir/designsystemet-react";

export type CharacterTab = "dashboard" | "families" | "raw";

export function CharacterTabs({
  active,
  onChange,
}: {
  active: CharacterTab;
  onChange: (tab: CharacterTab) => void;
}) {
  return (
    <Tabs value={active} onChange={(value) => onChange(value as CharacterTab)} className="character-tabs">
      <Tabs.List>
        <Tabs.Tab value="dashboard">Dashboard</Tabs.Tab>
        <Tabs.Tab value="families">Families</Tabs.Tab>
        <Tabs.Tab value="raw">Raw rows</Tabs.Tab>
      </Tabs.List>
    </Tabs>
  );
}
