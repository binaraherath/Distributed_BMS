import { useState, type FormEvent } from 'react'
import { UserPlus } from 'lucide-react'
import { ApiError, errorMessage } from '../api/client'
import { useCreateUser, useUpdatePack, useUpdateUser, useUsers } from '../api/queries'
import type { Pack, Role } from '../api/types'
import { usePack } from '../app/pack'
import { useTheme, type ThemeChoice } from '../app/theme'
import {
  Button, DialogActions, Field, FormDialog, Input, KeyValue, KeyValues, Notice, Panel, Segmented, Select, StatusBadge, Switch,
} from '../components/ui'
import { useShell } from '../layout/AppShell'
import { ago } from '../lib/format'
import s from './pages.module.css'

export function SettingsPage() {
  const { isAdmin, live, me } = useShell()
  const { pack } = usePack()
  const { choice, setChoice } = useTheme()
  const snap = live.data?.snapshot

  return (
    <div className={s.grid}>
      <div className={`${s.stack} ${s.s7}`}>
        {pack && <PackForm key={pack.id} pack={pack} editable={isAdmin} />}
        {isAdmin && <Users myName={me.username} />}
      </div>

      <div className={`${s.stack} ${s.s5}`}>
        <Panel title="Appearance">
          <div className={s.controlRow}>
            <div className={s.controlText}>
              <span className={s.controlTitle}>Theme</span>
              <span className={s.controlHelp}>System follows this device's light or dark setting.</span>
            </div>
            <Segmented<ThemeChoice>
              label="Theme"
              value={choice}
              onChange={setChoice}
              options={[{ value: 'system', label: 'System' }, { value: 'light', label: 'Light' }, { value: 'dark', label: 'Dark' }]}
            />
          </div>
        </Panel>

        <Panel title="Firmware">
          <KeyValues>
            <KeyValue label="Master (Node 1)"><span className="mono">{snap?.modules[0]?.firmware || '—'}</span></KeyValue>
            <KeyValue label="Controller (Node 2)"><span className="mono">{snap?.controller.firmware || '—'}</span></KeyValue>
            <KeyValue label="Gateway (Node 3)"><span className="mono">{snap?.gateway.firmware || '—'}</span></KeyValue>
            <KeyValue label="Device ID"><span className="mono">{pack?.deviceId ?? '—'}</span></KeyValue>
          </KeyValues>
        </Panel>

        <Panel title="Data">
          <KeyValues>
            <KeyValue label="Live view">every second, from memory</KeyValue>
            <KeyValue label="Stored sample">every 10 seconds</KeyValue>
            <KeyValue label="Kept for">90 days</KeyValue>
            <KeyValue label="API reference">
              <a href="/api/docs" target="_blank" rel="noreferrer" className={s.link}>OpenAPI / Swagger</a>
            </KeyValue>
          </KeyValues>
        </Panel>
      </div>
    </div>
  )
}

function PackForm({ pack, editable }: { pack: Pack; editable: boolean }) {
  const update = useUpdatePack(pack.id)
  const [name, setName] = useState(pack.name)
  const [location, setLocation] = useState(pack.location ?? '')
  const [capacity, setCapacity] = useState(pack.ratedCapacityAh?.toString() ?? '')

  const fieldErrors = update.error instanceof ApiError ? update.error.problem.errors ?? {} : {}
  const submit = (e: FormEvent) => {
    e.preventDefault()
    update.mutate({ name, location, ratedCapacityAh: capacity === '' ? undefined : Number(capacity) })
  }

  return (
    <Panel title="Battery pack" hint={`Registered ${ago(pack.createdAt)}`}>
      <form className={s.form} onSubmit={submit}>
        <div className={s.formRow}>
          <Field label="Name" error={fieldErrors.name}>
            <Input value={name} onChange={(e) => setName(e.target.value)} disabled={!editable} maxLength={100} required />
          </Field>
          <Field label="Location" error={fieldErrors.location}>
            <Input value={location} onChange={(e) => setLocation(e.target.value)} disabled={!editable} maxLength={120} />
          </Field>
        </div>
        <div className={s.formRow}>
          <Field label="Rated capacity (Ah, when new)" error={fieldErrors.ratedCapacityAh}>
            <Input value={capacity} onChange={(e) => setCapacity(e.target.value)} disabled={!editable}
              inputMode="decimal" type="number" step="0.1" min="0.1" />
          </Field>
          <Field label="Chemistry">
            <Input value={`${pack.chemistry} · ${pack.seriesCells}S`} disabled />
          </Field>
        </div>
        {editable && (
          <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
            <Button type="submit" variant="primary" disabled={update.isPending}>Save changes</Button>
            {update.isSuccess && <span className={s.meta}>Saved</span>}
            {update.error && !Object.keys(fieldErrors).length && <span className={s.meta}>{errorMessage(update.error)}</span>}
          </div>
        )}
      </form>
    </Panel>
  )
}

function Users({ myName }: { myName: string }) {
  const users = useUsers(true)
  const update = useUpdateUser()
  const [adding, setAdding] = useState(false)

  return (
    <Panel
      title="Users"
      hint="Administrators can send commands; viewers can only look"
      flush
      actions={<Button size="sm" onClick={() => setAdding(true)}><UserPlus size={14} /> Add user</Button>}
    >
      {update.error && <div style={{ padding: '0 20px 12px' }}><Notice>{errorMessage(update.error)}</Notice></div>}
      <div className={s.tableWrap}>
        <table className={s.table}>
          <thead>
            <tr><th>User</th><th>Role</th><th>Status</th><th className={s.num}>Enabled</th></tr>
          </thead>
          <tbody>
            {users.data?.content.map((u) => (
              <tr key={u.id}>
                <td>
                  <div className={s.strong}>{u.displayName}</div>
                  <div className={`${s.muted} mono`} style={{ fontSize: 12 }}>{u.username}</div>
                </td>
                <td>
                  <Select
                    style={{ height: 32, width: 130 }}
                    value={u.role}
                    disabled={u.username === myName || update.isPending}
                    onChange={(e) => update.mutate({ id: u.id, role: e.target.value as Role })}
                    aria-label={`Role of ${u.username}`}
                  >
                    <option value="ADMIN">Administrator</option>
                    <option value="VIEWER">Viewer</option>
                  </Select>
                </td>
                <td>
                  {u.enabled ? <StatusBadge tone="good">Active</StatusBadge> : <StatusBadge tone="neutral">Disabled</StatusBadge>}
                </td>
                <td className={s.num}>
                  <Switch
                    checked={u.enabled}
                    disabled={u.username === myName || update.isPending}
                    onCheckedChange={(on) => update.mutate({ id: u.id, enabled: on })}
                    aria-label={`${u.enabled ? 'Disable' : 'Enable'} ${u.username}`}
                  />
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <AddUserDialog open={adding} onOpenChange={setAdding} />
    </Panel>
  )
}

function AddUserDialog({ open, onOpenChange }: { open: boolean; onOpenChange: (o: boolean) => void }) {
  const create = useCreateUser()
  const [form, setForm] = useState({ username: '', displayName: '', password: '', role: 'VIEWER' as Role })
  const errors = create.error instanceof ApiError ? create.error.problem.errors ?? {} : {}

  const submit = (e: FormEvent) => {
    e.preventDefault()
    create.mutate(form, {
      onSuccess: () => {
        onOpenChange(false)
        setForm({ username: '', displayName: '', password: '', role: 'VIEWER' })
        create.reset()
      },
    })
  }

  return (
    <FormDialog open={open} onOpenChange={(o) => { onOpenChange(o); if (!o) create.reset() }} title="Add user">
      <form className={s.form} onSubmit={submit}>
        <Field label="Username" error={errors.username}>
          <Input value={form.username} autoComplete="off" required
            onChange={(e) => setForm({ ...form, username: e.target.value.toLowerCase() })} />
        </Field>
        <Field label="Display name" error={errors.displayName}>
          <Input value={form.displayName} required
            onChange={(e) => setForm({ ...form, displayName: e.target.value })} />
        </Field>
        <Field label="Password (at least 8 characters)" error={errors.password}>
          <Input type="password" value={form.password} autoComplete="new-password" required minLength={8}
            onChange={(e) => setForm({ ...form, password: e.target.value })} />
        </Field>
        <Field label="Role">
          <Select value={form.role} onChange={(e) => setForm({ ...form, role: e.target.value as Role })}>
            <option value="VIEWER">Viewer</option>
            <option value="ADMIN">Administrator</option>
          </Select>
        </Field>
        {create.error && !Object.keys(errors).length && <Notice>{errorMessage(create.error)}</Notice>}
        <DialogActions>
          <Button variant="ghost" onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button type="submit" variant="primary" disabled={create.isPending}>Create user</Button>
        </DialogActions>
      </form>
    </FormDialog>
  )
}
