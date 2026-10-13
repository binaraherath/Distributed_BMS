import { useState } from 'react'
import { BellOff, Check } from 'lucide-react'
import { errorMessage } from '../api/client'
import { useAcknowledgeAlarm, useAlarms, type AlarmFilter } from '../api/queries'
import { usePack } from '../app/pack'
import { Button, Empty, Notice, Panel, Segmented, StatusBadge } from '../components/ui'
import { useShell } from '../layout/AppShell'
import { ago, humanize, when } from '../lib/format'
import s from './pages.module.css'

const PAGE_SIZE = 15

export function AlarmsPage() {
  const { pack } = usePack()
  const { isAdmin } = useShell()
  const [status, setStatus] = useState<AlarmFilter['status']>('ALL')
  const [severity, setSeverity] = useState<AlarmFilter['severity']>('ALL')
  const [page, setPage] = useState(0)
  const { data, isFetching, error } = useAlarms(pack?.id, { status, severity, page, size: PAGE_SIZE })
  const ack = useAcknowledgeAlarm(pack?.id ?? 0)
  const alarms = data?.content ?? []

  return (
    <>
      <div className={s.toolbar}>
        <Segmented<AlarmFilter['status']>
          label="Status"
          value={status}
          onChange={(v) => { setStatus(v); setPage(0) }}
          options={[{ value: 'ALL', label: 'All' }, { value: 'ACTIVE', label: 'Active' }, { value: 'CLEARED', label: 'Cleared' }]}
        />
        <Segmented<AlarmFilter['severity']>
          label="Severity"
          value={severity}
          onChange={(v) => { setSeverity(v); setPage(0) }}
          options={[{ value: 'ALL', label: 'Any severity' }, { value: 'CRITICAL', label: 'Critical' }, { value: 'WARNING', label: 'Warning' }]}
        />
        {data && <span className={s.meta}>{data.totalElements} alarm{data.totalElements === 1 ? '' : 's'}</span>}
      </div>

      {(error || ack.error) && <div style={{ marginBottom: 16 }}><Notice>{errorMessage(error ?? ack.error)}</Notice></div>}

      <Panel flush>
        {alarms.length === 0 ? (
          <Empty icon={<BellOff size={18} />} title="No alarms match these filters">
            Alarms are raised from the fault flags the nodes report and close when the flag clears.
          </Empty>
        ) : (
          <>
            <div className={s.tableWrap} style={{ opacity: isFetching ? 0.7 : 1 }}>
              <table className={s.table}>
                <thead>
                  <tr>
                    <th>Severity</th>
                    <th>Alarm</th>
                    <th>Node</th>
                    <th>Raised</th>
                    <th>Cleared</th>
                    <th>Acknowledged</th>
                  </tr>
                </thead>
                <tbody>
                  {alarms.map((a) => (
                    <tr key={a.id}>
                      <td>
                        <StatusBadge tone={a.severity === 'CRITICAL' ? 'crit' : 'warn'}>{humanize(a.severity)}</StatusBadge>
                      </td>
                      <td>
                        <div className={s.strong}>{humanize(a.code)}</div>
                        <div className={s.muted} style={{ fontSize: 12 }}>{a.description}</div>
                      </td>
                      <td className={s.muted}>{a.source === 'BMS' ? 'Master' : 'Controller'}</td>
                      <td title={a.raisedAt}>{when.full(a.raisedAt)}</td>
                      <td>
                        {a.active
                          ? <StatusBadge tone="crit" icon={false}>Active</StatusBadge>
                          : <span title={a.clearedAt ?? ''}>{ago(a.clearedAt)}</span>}
                      </td>
                      <td>
                        {a.acknowledgedAt ? (
                          <span className={s.muted}><Check size={13} style={{ verticalAlign: -2 }} /> {a.acknowledgedBy}</span>
                        ) : isAdmin ? (
                          <Button size="sm" onClick={() => ack.mutate(a.id)} disabled={ack.isPending}>Acknowledge</Button>
                        ) : (
                          <span className={s.muted}>—</span>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            {data && data.totalPages > 1 && (
              <div className={s.pager}>
                <span>Page {data.page + 1} of {data.totalPages}</span>
                <span style={{ display: 'flex', gap: 8 }}>
                  <Button size="sm" disabled={page === 0} onClick={() => setPage((p) => p - 1)}>Previous</Button>
                  <Button size="sm" disabled={page + 1 >= data.totalPages} onClick={() => setPage((p) => p + 1)}>Next</Button>
                </span>
              </div>
            )}
          </>
        )}
      </Panel>
    </>
  )
}
