import { useMemo, useState } from 'react'
import { PlugZap } from 'lucide-react'
import { useCellHistory } from '../api/queries'
import { usePack } from '../app/pack'
import { CellChart } from '../components/CellChart'
import { TrendChart } from '../components/TrendChart'
import { Empty, Panel, StatusBadge } from '../components/ui'
import { useShell } from '../layout/AppShell'
import { dcToC, fmt, resolutionText } from '../lib/format'
import { balancingText } from '../lib/status'
import { Temperatures } from './OverviewPage'
import s from './pages.module.css'

const DAY = 24 * 3600_000

export function CellsPage() {
  const { live } = useShell()
  const { pack } = usePack()
  const m = live.data?.snapshot.modules[0]
  const [picked, setPicked] = useState<number | null>(null)

  if (!pack || !m) {
    return <Panel><Empty icon={<PlugZap size={18} />} title="Waiting for the gateway" /></Panel>
  }

  const source = live.gatewayOnline ? m.source : 'OFFLINE'
  const cells = m.cellsMv
  const avg = cells.reduce((a, b) => a + b, 0) / cells.length
  const selected = picked ?? m.cellMinNo
  const bal = balancingText(m)

  return (
    <div className={s.grid}>
      <Panel title="Cell voltages" hint="Bars show each cell's deviation from the pack average" source={source} className={s.s12}>
        <div className={s.tiles} style={{ borderTop: 0, borderBottom: '1px solid var(--border)', margin: '0 -20px 16px' }}>
          <Stat label="Average" value={fmt.d3(avg / 1000)} unit="V" />
          <Stat label={`Lowest · cell ${m.cellMinNo}`} value={fmt.d3(m.cellMinMv / 1000)} unit="V" />
          <Stat label={`Highest · cell ${m.cellMaxNo}`} value={fmt.d3(m.cellMaxMv / 1000)} unit="V" />
        </div>
        <CellChart
          cellsMv={cells}
          firstCellNo={m.firstCellNo}
          balancing={m.balancing.cells}
          height={280}
          selected={selected}
          onSelect={setPicked}
        />
        <div style={{ display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap', margin: '16px 0' }}>
          <StatusBadge tone={bal.tone}>{bal.text}</StatusBadge>
          <span className={s.meta}>
            Spread {fmt.int(m.cellMaxMv - m.cellMinMv)} mV · warning above 80 mV · select a cell for its history
          </span>
        </div>
        <div className={s.cellGrid}>
          {cells.map((mv, i) => {
            const no = m.firstCellNo + i
            const d = mv - avg
            return (
              <button key={no} className={s.cellTile} aria-pressed={selected === no} onClick={() => setPicked(no)}>
                <span className={s.cellNo}>
                  Cell {no}
                  {no === m.cellMinNo && <span className={s.tag}>min</span>}
                  {no === m.cellMaxNo && <span className={s.tag}>max</span>}
                </span>
                <span className={s.cellV}>{fmt.d3(mv / 1000)}</span>
                <span className={s.cellDev}>{d >= 0 ? '+' : ''}{fmt.d1(d)} mV</span>
              </button>
            )
          })}
        </div>
      </Panel>

      <CellHistoryPanel packId={pack.id} cellNo={selected} />

      <Panel title="Temperatures" source={source} className={s.s4}>
        <Temperatures temps={m.tempsDc.map(dcToC)} dieC={m.dieTempC} />
      </Panel>
    </div>
  )
}

function Stat({ label, value, unit }: { label: string; value: string; unit: string }) {
  return (
    <div className={s.tile}>
      <span className={s.tileLabel}>{label}</span>
      <span className={s.tileValue}>{value}<span className={s.tileUnit}>{unit}</span></span>
    </div>
  )
}

function CellHistoryPanel({ packId, cellNo }: { packId: number; cellNo: number }) {
  // round to the minute so the query key is stable between renders
  const [range] = useState(() => {
    const to = Math.ceil(Date.now() / 60_000) * 60_000
    return { from: new Date(to - DAY).toISOString(), to: new Date(to).toISOString() }
  })
  const { data, isFetching } = useCellHistory(packId, cellNo, range.from, range.to)
  const points = useMemo(
    () => (data?.points ?? []).map((p) => ({ t: Date.parse(p.t), v: p.voltageMv / 1000 })),
    [data],
  )

  return (
    <Panel title={`Cell ${cellNo} · last 24 hours`} hint={data ? resolutionText(data.resolution) : undefined} className={s.s8}>
      <div style={{ opacity: isFetching ? 0.6 : 1, transition: 'opacity 150ms' }}>
        {points.length < 2 ? (
          <Empty title="Not enough stored samples yet">Samples are stored every 10 seconds.</Empty>
        ) : (
          <TrendChart data={points} unit="V" format={fmt.d3} minSpan={0.02} spanMs={DAY} height={220} />
        )}
      </div>
    </Panel>
  )
}
