import { Cpu, Radio, Server, Zap } from 'lucide-react'
import type { DataSource, LiveTelemetry } from '../api/types'
import { duration } from '../lib/format'
import { SourceTag } from './ui'
import s from './viz.module.css'

interface NodeView {
  key: string
  name: string
  role: string
  icon: typeof Cpu
  source: DataSource
  facts: [string, string][]
}

/**
 * The three nodes on the CAN bus and the link to this server. Each node says
 * whether its numbers are measured (Live), modelled by the gateway (Simulated),
 * or missing (Offline).
 */
export function NodeTopology({ live, gatewayOnline }: { live: LiveTelemetry | null; gatewayOnline: boolean }) {
  const snap = live?.snapshot
  const master = snap?.modules[0]
  const ctrl = snap?.controller
  const gw = snap?.gateway
  const off = !gatewayOnline || !snap

  const nodes: NodeView[] = [
    {
      key: 'master',
      name: 'Master',
      role: 'Node 1 · BQ76952 monitor',
      icon: Zap,
      source: off || !master ? 'OFFLINE' : master.source,
      facts: master ? [['State', master.state], ['Uptime', duration(master.uptimeS)], ['Firmware', master.firmware || '—']] : [],
    },
    {
      key: 'ctrl',
      name: 'Controller',
      role: 'Node 2 · contactors & fan',
      icon: Cpu,
      source: off || !ctrl ? 'OFFLINE' : ctrl.source,
      facts: ctrl ? [['State', ctrl.state], ['Uptime', duration(ctrl.uptimeS)], ['Firmware', ctrl.firmware || '—']] : [],
    },
    {
      key: 'gw',
      name: 'Gateway',
      role: 'Node 3 · ESP32, CAN → MQTT',
      icon: Radio,
      source: gatewayOnline && gw ? 'LIVE' : 'OFFLINE',
      facts: gw ? [['Wi-Fi', `${gw.rssiDbm} dBm`], ['CAN frames', gw.canRxFrames.toLocaleString('en-GB')], ['Buffered', String(gw.buffered)]] : [],
    },
    {
      key: 'srv',
      name: 'Server',
      role: 'MQTT · PostgreSQL · API',
      icon: Server,
      source: 'LIVE',
      facts: [['Stream', '1 Hz'], ['Stored every', '10 s'], ['Retention', '90 days']],
    },
  ]

  return (
    <div className={s.topology}>
      <div className={s.bus} aria-hidden><span className={s.busLabel}>CAN bus · 500 kbit/s</span></div>
      <div className={s.link} aria-hidden><span className={s.busLabel}>MQTT over Wi-Fi</span></div>
      <ol className={s.nodes}>
        {nodes.map((n) => (
          <li key={n.key} className={s.node} data-source={n.source}>
            <div className={s.nodeHead}>
              <span className={s.nodeIcon}><n.icon size={16} strokeWidth={1.8} aria-hidden /></span>
              <div className={s.nodeName}>
                <div>{n.name}</div>
                <div className={s.nodeRole}>{n.role}</div>
              </div>
            </div>
            <SourceTag source={n.source} />
            {n.facts.length > 0 && (
              <dl className={s.nodeFacts}>
                {n.facts.map(([k, v]) => (
                  <div key={k}>
                    <dt>{k}</dt>
                    <dd>{v}</dd>
                  </div>
                ))}
              </dl>
            )}
          </li>
        ))}
      </ol>
    </div>
  )
}
