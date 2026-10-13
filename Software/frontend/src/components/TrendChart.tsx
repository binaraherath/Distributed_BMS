import { Area, AreaChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts'
import { when } from '../lib/format'
import { niceTicks } from '../lib/ticks'
import s from './viz.module.css'

export interface TrendPoint {
  t: number
  v: number
  simulated?: boolean
}

interface Props {
  data: TrendPoint[]
  unit: string
  format: (v: number) => string
  syncId?: string
  height?: number
  /** Fixed y-domain (e.g. 0..100 for SoC); otherwise padded around the data. */
  domain?: [number, number]
  /** Smallest visible y-span, so a flat signal is not stretched into noise. */
  minSpan?: number
  spanMs: number
}

function dataTicks(data: TrendPoint[], minSpan: number): number[] {
  if (!data.length) return [0, 1]
  let lo = Math.min(...data.map((d) => d.v))
  let hi = Math.max(...data.map((d) => d.v))
  if (hi - lo < minSpan) {
    const mid = (hi + lo) / 2
    lo = mid - minSpan / 2
    hi = mid + minSpan / 2
  }
  return niceTicks(lo, hi)
}

/** One measure over time: 2px line, 10 % wash, hairline grid, crosshair tooltip. */
export function TrendChart({ data, unit, format, syncId, height = 180, domain, minSpan = 1, spanMs }: Props) {
  const yTicks = domain ? niceTicks(domain[0], domain[1]) : dataTicks(data, minSpan)
  const yDomain: [number, number] = [yTicks[0], yTicks[yTicks.length - 1]]
  const tick = spanMs > 36 * 3600_000 ? when.dayTime : when.time

  return (
    <div className={s.trend} style={{ height }}>
      <ResponsiveContainer width="100%" height="100%">
        <AreaChart data={data} syncId={syncId} margin={{ top: 8, right: 8, bottom: 0, left: 0 }}>
          <CartesianGrid vertical={false} stroke="var(--grid)" strokeWidth={1} />
          <XAxis
            dataKey="t"
            type="number"
            scale="time"
            domain={['dataMin', 'dataMax']}
            tickFormatter={tick}
            tick={{ fill: 'var(--muted)', fontSize: 11 }}
            tickLine={false}
            axisLine={{ stroke: 'var(--border-strong)' }}
            minTickGap={48}
          />
          <YAxis
            domain={yDomain}
            ticks={yTicks}
            interval={0}
            allowDataOverflow
            tickFormatter={(v: number) => format(v)}
            tick={{ fill: 'var(--muted)', fontSize: 11 }}
            tickLine={false}
            axisLine={false}
            width={48}
          />
          <Tooltip
            cursor={{ stroke: 'var(--border-strong)', strokeWidth: 1 }}
            isAnimationActive={false}
            content={({ active, payload }) => {
              const p = payload?.[0]?.payload as TrendPoint | undefined
              if (!active || !p) return null
              return (
                <div className={s.tooltip} style={{ position: 'static' }}>
                  <div className={s.ttValue}>{format(p.v)} {unit}</div>
                  <div className={s.ttRow}><span className={s.ttKey} />{when.full(new Date(p.t).toISOString())}</div>
                  {p.simulated && <div className={s.ttRow}>Simulated sample</div>}
                </div>
              )
            }}
          />
          <Area
            type="monotone"
            dataKey="v"
            stroke="var(--accent)"
            strokeWidth={2}
            strokeLinejoin="round"
            strokeLinecap="round"
            fill="var(--accent)"
            fillOpacity={0.1}
            dot={false}
            activeDot={{ r: 4, fill: 'var(--accent)', stroke: 'var(--surface)', strokeWidth: 2 }}
            isAnimationActive={false}
          />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  )
}
