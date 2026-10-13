import { useState } from 'react'
import { Navigate } from 'react-router'
import { History, WifiOff } from 'lucide-react'
import { errorMessage } from '../api/client'
import { useCommands, useIssueCommand } from '../api/queries'
import type { CommandStatus, CommandType } from '../api/types'
import { usePack } from '../app/pack'
import { Button, ConfirmDialog, Empty, Notice, Panel, StatusBadge, Switch, type Tone } from '../components/ui'
import { useShell } from '../layout/AppShell'
import { ago, capitalize } from '../lib/format'
import s from './pages.module.css'

const statusTone: Record<CommandStatus, Tone> = {
  PENDING: 'neutral',
  PUBLISHED: 'accent',
  ACCEPTED: 'good',
  REJECTED: 'crit',
  TIMED_OUT: 'warn',
  FAILED: 'crit',
}

const statusText: Record<CommandStatus, string> = {
  PENDING: 'Pending',
  PUBLISHED: 'Sent, waiting',
  ACCEPTED: 'Accepted',
  REJECTED: 'Rejected',
  TIMED_OUT: 'No answer',
  FAILED: 'Not delivered',
}

const commandText: Record<CommandType, string> = {
  CLEAR_FAULTS: 'Clear latched faults',
  SET_BALANCING: 'Cell balancing',
  FAN_OVERRIDE: 'Fan override',
  STOP: 'Stop (open contactors)',
}

function argumentText(type: CommandType, arg: number | null) {
  if (type === 'SET_BALANCING') return arg === 1 ? 'Allow' : 'Block'
  if (type === 'FAN_OVERRIDE') return arg === null ? 'Automatic' : `${arg} %`
  return '—'
}

export function ControlPage() {
  const { isAdmin, live } = useShell()
  const { pack } = usePack()
  const issue = useIssueCommand(pack?.id ?? 0)
  const [page, setPage] = useState(0)
  const commands = useCommands(pack?.id, page)
  const [fan, setFan] = useState(50)
  const [confirmStop, setConfirmStop] = useState(false)

  if (!isAdmin) return <Navigate to="/" replace />
  if (!pack) return null

  const m = live.data?.snapshot.modules[0]
  const ctrl = live.data?.snapshot.controller
  const offline = !live.gatewayOnline
  const send = (type: CommandType, argument: number | null = null) => issue.mutate({ type, argument })
  const busy = issue.isPending || offline
  const log = commands.data

  return (
    <div className={s.grid}>
      <div className={`${s.stack} ${s.s7}`}>
        {offline && (
          <Notice icon={<WifiOff size={16} />}>
            The gateway is offline, so commands cannot reach the pack. They are enabled again as soon as it reconnects.
          </Notice>
        )}
        {issue.error && <Notice>{errorMessage(issue.error)}</Notice>}

        <Panel title="Remote commands" hint="Relayed by the gateway over CAN; every command is logged">
          <div className={s.rows}>
            <div className={s.row} style={{ padding: '12px 0' }}>
              <div className={s.controlText}>
                <span className={s.controlTitle}>Cell balancing</span>
                <span className={s.controlHelp}>
                  Allows the master to switch the external bleed resistors. Leave off when the bleeder board is not fitted.
                </span>
              </div>
              <Switch
                aria-label="Allow cell balancing"
                checked={m?.balancing.enabled ?? false}
                disabled={busy}
                onCheckedChange={(on) => send('SET_BALANCING', on ? 1 : 0)}
              />
            </div>

            <div className={s.row} style={{ padding: '12px 0', flexWrap: 'wrap' }}>
              <div className={s.controlText}>
                <span className={s.controlTitle}>Cooling fan</span>
                <span className={s.controlHelp}>
                  Now {ctrl ? `${ctrl.fanDutyPct} %` : '—'}. Override the temperature curve, or hand control back.
                </span>
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
                <input
                  type="range" min={0} max={100} step={5} value={fan}
                  onChange={(e) => setFan(Number(e.target.value))}
                  className={s.slider} aria-label="Fan duty"
                />
                <span className="tnum" style={{ width: 40 }}>{fan} %</span>
                <Button size="sm" disabled={busy} onClick={() => send('FAN_OVERRIDE', fan)}>Apply</Button>
                <Button size="sm" variant="ghost" disabled={busy} onClick={() => send('FAN_OVERRIDE', null)}>Auto</Button>
              </div>
            </div>

            <div className={s.row} style={{ padding: '12px 0' }}>
              <div className={s.controlText}>
                <span className={s.controlTitle}>Clear latched faults</span>
                <span className={s.controlHelp}>Only clears faults whose cause has gone; active conditions latch again.</span>
              </div>
              <Button size="sm" disabled={busy} onClick={() => send('CLEAR_FAULTS')}>Clear faults</Button>
            </div>

            <div className={s.row} style={{ padding: '12px 0' }}>
              <div className={s.controlText}>
                <span className={s.controlTitle}>Stop</span>
                <span className={s.controlHelp}>
                  Opens all contactors. Restarting needs the key switch on the pack; it cannot be done from here.
                </span>
              </div>
              <Button size="sm" variant="danger" disabled={busy} onClick={() => setConfirmStop(true)}>Stop pack</Button>
            </div>
          </div>
        </Panel>
      </div>

      <Panel title="Command log" className={s.s5} flush>
        {!log || log.content.length === 0 ? (
          <Empty icon={<History size={18} />} title="No commands sent yet" />
        ) : (
          <>
            <ul className={s.alarmList} style={{ padding: '0 20px' }}>
              {log.content.map((c) => (
                <li key={c.id} className={s.alarmItem} style={{ gridTemplateColumns: '1fr auto' }}>
                  <div className={s.alarmText}>
                    <div className={s.alarmCode}>
                      {commandText[c.type]}
                      {argumentText(c.type, c.argument) !== '—' && (
                        <span className={s.muted} style={{ fontWeight: 400 }}> · {argumentText(c.type, c.argument)}</span>
                      )}
                    </div>
                    <div className={s.alarmDesc}>
                      {c.issuedBy} · {ago(c.issuedAt)}
                      {c.resultMessage && ` · ${capitalize(c.resultMessage)}`}
                    </div>
                  </div>
                  <StatusBadge tone={statusTone[c.status]}>{statusText[c.status]}</StatusBadge>
                </li>
              ))}
            </ul>
            {log.totalPages > 1 && (
              <div className={s.pager}>
                <span>Page {log.page + 1} of {log.totalPages}</span>
                <span style={{ display: 'flex', gap: 8 }}>
                  <Button size="sm" disabled={page === 0} onClick={() => setPage((p) => p - 1)}>Newer</Button>
                  <Button size="sm" disabled={page + 1 >= log.totalPages} onClick={() => setPage((p) => p + 1)}>Older</Button>
                </span>
              </div>
            )}
          </>
        )}
      </Panel>

      <ConfirmDialog
        open={confirmStop}
        onOpenChange={setConfirmStop}
        title="Stop the pack?"
        confirmLabel="Open contactors"
        danger
        busy={issue.isPending}
        onConfirm={() => { send('STOP'); setConfirmStop(false) }}
      >
        The controller opens Main+, Main− and precharge. Any connected load loses power immediately. To run
        again, someone has to turn the key switch off and on at the pack.
      </ConfirmDialog>
    </div>
  )
}
