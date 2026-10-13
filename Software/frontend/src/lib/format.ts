const nf = (min: number, max = min) =>
  new Intl.NumberFormat('en-GB', { minimumFractionDigits: min, maximumFractionDigits: max })

const f0 = nf(0)
const f1 = nf(1)
const f2 = nf(2)
const f3 = nf(3)

export const fmt = {
  int: (v: number) => f0.format(v),
  d1: (v: number) => f1.format(v),
  d2: (v: number) => f2.format(v),
  d3: (v: number) => f3.format(v),
  /** -0.00 reads as a fault on a display; show plain zero. */
  signed2: (v: number) => (Math.abs(v) < 0.005 ? f2.format(0) : (v > 0 ? '+' : '') + f2.format(v)),
}

export const mvToV = (mv: number) => mv / 1000
export const dcToC = (dc: number) => dc / 10

export function duration(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) return '—'
  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  if (d > 0) return `${d}d ${h}h`
  if (h > 0) return `${h}h ${m}m`
  if (m > 0) return `${m}m ${Math.floor(seconds % 60)}s`
  return `${Math.floor(seconds)}s`
}

export function ago(iso: string | null | undefined, now = Date.now()): string {
  if (!iso) return 'never'
  const s = Math.max(0, (now - Date.parse(iso)) / 1000)
  if (s < 2) return 'just now'
  if (s < 60) return `${Math.floor(s)} s ago`
  if (s < 3600) return `${Math.floor(s / 60)} min ago`
  if (s < 86400) return `${Math.floor(s / 3600)} h ago`
  return `${Math.floor(s / 86400)} d ago`
}

const dateTime = new Intl.DateTimeFormat('en-GB', {
  day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
})
const timeOnly = new Intl.DateTimeFormat('en-GB', { hour: '2-digit', minute: '2-digit', hour12: false })
const dayTime = new Intl.DateTimeFormat('en-GB', { day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit', hour12: false })

export const when = {
  full: (iso: string) => dateTime.format(new Date(iso)),
  time: (ms: number) => timeOnly.format(new Date(ms)),
  dayTime: (ms: number) => dayTime.format(new Date(ms)),
}

/** "OVER_TEMP" -> "Over temp" */
export function humanize(code: string): string {
  const s = code.toLowerCase().replace(/_/g, ' ')
  return s.charAt(0).toUpperCase() + s.slice(1)
}

export const capitalize = (s: string) => s.charAt(0).toUpperCase() + s.slice(1)

export function resolutionText(r: string) {
  return r === 'RAW' ? 'every stored sample (10 s)' : r === 'ONE_MINUTE' ? '1-minute averages' : '15-minute averages'
}
