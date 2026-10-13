import { Link } from 'react-router'
import { ArrowRight, BellOff, PlugZap } from 'lucide-react'
import { useAlarms } from '../api/queries'
import type { ModuleSnapshot } from '../api/types'
import { usePack } from '../app/pack'
import { CellChart } from '../components/CellChart'
import { ContactorDiagram } from '../components/ContactorDiagram'
import { NodeTopology } from '../components/NodeTopology'
import { SocGauge } from '../components/SocGauge'
import { Empty, Panel, StatusBadge } from '../components/ui'
import { useShell } from '../layout/AppShell'
import { ago, dcToC, fmt, humanize } from '../lib/format'
import {
  TEMP_FAULT_C, balancingText, bmsTone, ctrlTone, currentDirection, stateLabel, tempColor,
} from '../lib/status'
import s from './pages.module.css'

export function OverviewPage() {
  const { live } = useShell()
  const { pack } = usePack()
  const snap = live.data?.snapshot

  if (!pack || !snap) {
    return (
      <Panel>
        <Empty icon={<PlugZap size={18} />} title={live.loading ? 'Connecting…' : 'Waiting for the gateway'}>
          The dashboard fills in as soon as the ESP32 gateway publishes its first message.
        </Empty>
      </Panel>
    )
  }

  const offline = !live.gatewayOnline
  const m = snap.modules[0]
  const ctrl = snap.controller
  const p = snap.pack
  const bmsSource = offline ? 'OFFLINE' : m.source
  const ctrlSource = offline ? 'OFFLINE' : ctrl.source
  const v = p.voltageMv / 1000
  const i = p.currentMa / 1000
  const power = v * i
  const temps = m.tempsDc.map(dcToC)
  const tmax = Math.max(...temps)

  return (
    <div className={s.grid}>
      <Panel title="State of charge" source={bmsSource} className={s.s4}>
        <SocGauge soc={p.socPct} remainingAh={p.remainingAh} capacityAh={p.capacityAh} />
        <div className={s.rows} style={{ marginTop: 12 }}>
          <div className={s.row}>
            <span className={s.rowLabel}>State of health</span>
            <span className={s.rowValue}>{fmt.d1(p.sohPct)} %</span>
          </div>
          <div className={s.row}>
            <span className={s.rowLabel}>Discharged this cycle</span>
            <span className={s.rowValue}>{fmt.d1(p.dischargedAh)} Ah</span>
          </div>
        </div>
      </Panel>

      <Panel title="Pack" hint={`${pack.seriesCells} cells in series`} source={bmsSource} className={s.s8}>
        <div className={s.headline}>
          <span className={s.headlineValue}>{fmt.d2(v)}</span>
          <span className={s.headlineUnit}>V</span>
          <span className={s.headlineNote}>{currentDirection(i)}</span>
        </div>
        <div className={s.tiles}>
          <Tile label="Current" value={fmt.signed2(i)} unit="A"
            sub={Math.abs(i) < 0.05 ? 'No current flowing' : i > 0 ? 'Into the pack' : 'Out of the pack'} />
          <Tile label="Power" value={fmt.int(Math.abs(power) < 0.5 ? 0 : power)} unit="W" sub={currentDirection(i)} />
          <Tile label="Cell spread" value={fmt.int(m.cellMaxMv - m.cellMinMv)} unit="mV"
            sub={`${fmt.d3(m.cellMinMv / 1000)} – ${fmt.d3(m.cellMaxMv / 1000)} V`} />
          <Tile label="Highest temperature" value={fmt.d1(tmax)} unit="°C" sub={`Fault limit ${TEMP_FAULT_C} °C`} />
          <Tile label="Charge limit" value={fmt.d1(p.limits.chargeA)} unit="A"
            sub={p.limits.chargeAllowed ? 'Charging allowed' : 'Charging blocked'} />
          <Tile label="Discharge limit" value={fmt.d1(p.limits.dischargeA)} unit="A"
            sub={p.limits.dischargeAllowed ? 'Discharging allowed' : 'Discharging blocked'} />
        </div>
      </Panel>

      <Panel title="Power path" hint="High-voltage contactors" source={ctrlSource} className={s.s7}>
        <ContactorDiagram k={ctrl.contactors} />
      </Panel>

      <Panel title="System status" className={s.s5}>
        <StatusRows m={m} ctrlState={ctrl.state} fanDuty={ctrl.fanDutyPct} fanRpm={ctrl.fanRpm}
          keyOn={ctrl.keyOn} estopOk={ctrl.estopOk} />
      </Panel>

      <Panel title="Nodes" hint={`Last message ${ago(live.lastMessageAt)}`} className={s.s12}>
        <NodeTopology live={live.data} gatewayOnline={live.gatewayOnline} />
      </Panel>

      <Panel
        title="Cells"
        hint={`avg ${fmt.d3(m.cellAvgMv / 1000)} V · deviation from average`}
        source={bmsSource}
        className={s.s8}
        actions={<Link to="/cells" className={s.link}>Details <ArrowRight size={13} /></Link>}
      >
        <CellChart cellsMv={m.cellsMv} firstCellNo={m.firstCellNo} balancing={m.balancing.cells} height={190} compact />
      </Panel>

      <Panel title="Temperatures" source={bmsSource} className={s.s4}>
        <Temperatures temps={temps} dieC={m.dieTempC} />
      </Panel>

      <ActiveAlarms packId={pack.id} />
    </div>
  )
}

function Tile({ label, value, unit, sub }: { label: string; value: string; unit: string; sub?: string }) {
  return (
    <div className={s.tile}>
      <span className={s.tileLabel}>{label}</span>
      <span className={s.tileValue}>{value}<span className={s.tileUnit}>{unit}</span></span>
      {sub && <span className={s.tileSub}>{sub}</span>}
    </div>
  )
}

function StatusRows({ m, ctrlState, fanDuty, fanRpm, keyOn, estopOk }: {
  m: ModuleSnapshot
  ctrlState: string
  fanDuty: number
  fanRpm: number
  keyOn: boolean
  estopOk: boolean
}) {
  const bal = balancingText(m)
  return (
    <div className={s.rows}>
      <Row label="Battery monitor"><StatusBadge tone={bmsTone(m.state)}>{stateLabel(m.state)}</StatusBadge></Row>
      <Row label="System controller"><StatusBadge tone={ctrlTone(ctrlState)}>{stateLabel(ctrlState)}</StatusBadge></Row>
      <Row label="Cell balancing"><StatusBadge tone={bal.tone}>{bal.text}</StatusBadge></Row>
      <Row label="Cooling fan">
        {fanDuty === 0 ? <span className={s.muted}>Off</span> : <span>{fanDuty} % · {fmt.int(fanRpm)} rpm</span>}
      </Row>
      <Row label="Key switch">{keyOn ? 'On' : <span className={s.muted}>Off</span>}</Row>
      <Row label="Emergency stop">
        {estopOk
          ? <StatusBadge tone="good">Released</StatusBadge>
          : <StatusBadge tone="crit">Pressed</StatusBadge>}
      </Row>
      {(m.faults.length > 0 || m.warnings.length > 0) && (
        <Row label="Active flags">
          {m.faults.map((f) => <StatusBadge key={f} tone="crit">{humanize(f)}</StatusBadge>)}
          {m.warnings.map((w) => <StatusBadge key={w} tone="warn">{humanize(w)}</StatusBadge>)}
        </Row>
      )}
    </div>
  )
}

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className={s.row}>
      <span className={s.rowLabel}>{label}</span>
      <span className={s.rowValue}>{children}</span>
    </div>
  )
}

export function Temperatures({ temps, dieC }: { temps: number[]; dieC: number }) {
  const rows: [string, number][] = [...temps.map((t, i) => [`NTC ${i + 1}`, t] as [string, number]), ['BQ die', dieC]]
  return (
    <div className={s.meters}>
      {rows.map(([name, c]) => (
        <div key={name} className={s.meter}>
          <span className={s.meterName}>{name}</span>
          <span className={s.meterTrack} aria-hidden>
            <span className={s.meterFill}
              style={{ width: `${Math.max(2, Math.min(100, (c / TEMP_FAULT_C) * 100))}%`, background: tempColor(c), display: 'block' }} />
          </span>
          <span className={s.meterValue}>{fmt.d1(c)} °C</span>
        </div>
      ))}
      <div className={s.meta}>Scale 0 – {TEMP_FAULT_C} °C (over-temperature fault)</div>
    </div>
  )
}

function ActiveAlarms({ packId }: { packId: number }) {
  const { data } = useAlarms(packId, { status: 'ACTIVE', severity: 'ALL', page: 0, size: 5 })
  const alarms = data?.content ?? []
  return (
    <Panel
      title="Active alarms"
      className={s.s12}
      actions={<Link to="/alarms" className={s.link}>All alarms <ArrowRight size={13} /></Link>}
    >
      {alarms.length === 0 ? (
        <Empty icon={<BellOff size={18} />} title="No active alarms">Every fault flag reported by the nodes is clear.</Empty>
      ) : (
        <ul className={s.alarmList}>
          {alarms.map((a) => (
            <li key={a.id} className={s.alarmItem}>
              <StatusBadge tone={a.severity === 'CRITICAL' ? 'crit' : 'warn'}>{humanize(a.severity)}</StatusBadge>
              <div className={s.alarmText}>
                <div className={s.alarmCode}>{humanize(a.code)}</div>
                <div className={s.alarmDesc}>{a.description}</div>
              </div>
              <span className={s.meta}>{ago(a.raisedAt)}</span>
            </li>
          ))}
        </ul>
      )}
    </Panel>
  )
}
