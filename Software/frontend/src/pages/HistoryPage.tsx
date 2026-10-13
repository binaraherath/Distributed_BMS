import { useEffect, useMemo, useState } from 'react'
import { Download, LineChart } from 'lucide-react'
import { exportUrl, useHistory } from '../api/queries'
import type { HistoryPoint } from '../api/types'
import { usePack } from '../app/pack'
import { TrendChart, type TrendPoint } from '../components/TrendChart'
import { Empty, Notice, Panel, Segmented } from '../components/ui'
import { errorMessage } from '../api/client'
import { fmt, resolutionText } from '../lib/format'
import s from './pages.module.css'
import ui from '../components/ui.module.css'

type Range = '1h' | '6h' | '24h' | '7d' | '30d'
const RANGE_MS: Record<Range, number> = {
  '1h': 3600_000, '6h': 6 * 3600_000, '24h': 24 * 3600_000, '7d': 7 * 24 * 3600_000, '30d': 30 * 24 * 3600_000,
}

function rangeWindow(range: Range) {
  const to = Math.ceil(Date.now() / 60_000) * 60_000
  return { from: new Date(to - RANGE_MS[range]).toISOString(), to: new Date(to).toISOString() }
}

const series = (pts: HistoryPoint[], pick: (p: HistoryPoint) => number): TrendPoint[] =>
  pts.map((p) => ({ t: Date.parse(p.t), v: pick(p), simulated: p.simulated }))

export function HistoryPage() {
  const { pack } = usePack()
  const [range, setRange] = useState<Range>('1h')
  const [win, setWin] = useState(() => rangeWindow('1h'))

  const pickRange = (r: Range) => {
    setRange(r)
    setWin(rangeWindow(r))
  }

  useEffect(() => {
    const t = setInterval(() => setWin(rangeWindow(range)), 60_000)
    return () => clearInterval(t)
  }, [range])

  const { data, isFetching, error } = useHistory(pack?.id, win.from, win.to)
  const pts = useMemo(() => data?.points ?? [], [data])
  const simulatedShare = pts.length ? pts.filter((p) => p.simulated).length / pts.length : 0
  const span = RANGE_MS[range]

  const charts = useMemo(() => ({
    v: series(pts, (p) => p.voltageV),
    i: series(pts, (p) => p.currentA),
    soc: series(pts, (p) => p.socPct),
    t: series(pts, (p) => p.tempMaxC),
    d: series(pts, (p) => p.cellDeltaMv),
  }), [pts])

  return (
    <>
      <div className={s.toolbar}>
        <Segmented<Range>
          label="Time range"
          value={range}
          onChange={pickRange}
          options={[
            { value: '1h', label: '1 hour' },
            { value: '6h', label: '6 hours' },
            { value: '24h', label: '24 hours' },
            { value: '7d', label: '7 days' },
            { value: '30d', label: '30 days' },
          ]}
        />
        {data && (
          <span className={s.meta}>
            {fmt.int(pts.length)} points · {resolutionText(data.resolution)}
          </span>
        )}
        <div className={s.toolbarEnd}>
          {pack && (
            <a className={`${ui.btn} ${ui.sm}`} href={exportUrl(pack.id, win.from, win.to)} download>
              <Download size={14} /> Export CSV
            </a>
          )}
        </div>
      </div>

      {error && <div style={{ marginBottom: 16 }}><Notice>{errorMessage(error)}</Notice></div>}
      {simulatedShare > 0 && (
        <div style={{ marginBottom: 16 }}>
          <Notice>
            {fmt.int(simulatedShare * 100)} % of the samples in this range came from the gateway's fallback model
            while a node was not reporting. The tooltip marks them as simulated.
          </Notice>
        </div>
      )}

      {pts.length < 2 ? (
        <Panel>
          <Empty icon={<LineChart size={18} />} title="No stored samples in this range">
            The server stores one sample every 10 seconds while the gateway is online.
          </Empty>
        </Panel>
      ) : (
        <div className={s.grid} style={{ opacity: isFetching ? 0.6 : 1, transition: 'opacity 150ms' }}>
          <ChartPanel title="Pack voltage" unit="V" last={last(charts.v)} format={fmt.d2} className={s.s6}>
            <TrendChart data={charts.v} unit="V" format={fmt.d1} minSpan={1} spanMs={span} syncId="h" />
          </ChartPanel>
          <ChartPanel title="Current" unit="A" last={last(charts.i)} format={fmt.signed2} className={s.s6}>
            <TrendChart data={charts.i} unit="A" format={fmt.d1} minSpan={2} spanMs={span} syncId="h" />
          </ChartPanel>
          <ChartPanel title="State of charge" unit="%" last={last(charts.soc)} format={fmt.d1} className={s.s4}>
            <TrendChart data={charts.soc} unit="%" format={fmt.int} domain={[0, 100]} spanMs={span} syncId="h" />
          </ChartPanel>
          <ChartPanel title="Highest temperature" unit="°C" last={last(charts.t)} format={fmt.d1} className={s.s4}>
            <TrendChart data={charts.t} unit="°C" format={fmt.int} minSpan={4} spanMs={span} syncId="h" />
          </ChartPanel>
          <ChartPanel title="Cell spread" unit="mV" last={last(charts.d)} format={fmt.int} className={s.s4}>
            <TrendChart data={charts.d} unit="mV" format={fmt.int} minSpan={10} spanMs={span} syncId="h" />
          </ChartPanel>
        </div>
      )}
    </>
  )
}

const last = (d: TrendPoint[]) => d.at(-1)?.v

function ChartPanel({ title, unit, last, format, className, children }: {
  title: string
  unit: string
  last: number | undefined
  format: (v: number) => string
  className: string
  children: React.ReactNode
}) {
  return (
    <Panel
      title={title}
      className={className}
      actions={last !== undefined && <span className="tnum" style={{ fontWeight: 600 }}>{format(last)} <span style={{ color: 'var(--muted)', fontWeight: 400 }}>{unit}</span></span>}
    >
      {children}
    </Panel>
  )
}
