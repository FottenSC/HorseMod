export function CommandText({ value, subtle = false }: { value: string; subtle?: boolean }) {
  if (!value) return <span className="muted">-</span>;
  return (
    <code className={`command-text ${subtle ? "command-text-subtle" : ""}`} title={value}>
      {value}
    </code>
  );
}
