/** Three modules on one bus: the distributed BMS in a glyph. */
export function BrandMark({ size = 28 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 28 28" aria-hidden focusable="false">
      <rect x="0.5" y="0.5" width="27" height="27" rx="7" fill="var(--accent)" />
      <g fill="none" stroke="var(--accent-ink)" strokeWidth="1.6" strokeLinecap="round">
        <path d="M7 21.5h14" />
        <path d="M9 21.5v-3M14 21.5v-3M19 21.5v-3" />
      </g>
      <g fill="var(--accent-ink)">
        <rect x="6.5" y="7" width="5" height="10" rx="1.4" />
        <rect x="11.5" y="5.5" width="5" height="11.5" rx="1.4" opacity="0.85" />
        <rect x="16.5" y="8.5" width="5" height="8.5" rx="1.4" opacity="0.7" />
      </g>
    </svg>
  )
}
