import type { ButtonHTMLAttributes, ComponentProps, ReactNode } from 'react'
import * as Dialog from '@radix-ui/react-dialog'
import * as RadixSwitch from '@radix-ui/react-switch'
import * as ToggleGroup from '@radix-ui/react-toggle-group'
import { AlertTriangle, CheckCircle2, CircleDashed, Info, OctagonAlert } from 'lucide-react'
import type { DataSource } from '../api/types'
import s from './ui.module.css'

const cx = (...c: (string | false | null | undefined)[]) => c.filter(Boolean).join(' ')

// ---------------------------------------------------------------- panel

interface PanelProps {
  title?: ReactNode
  hint?: ReactNode
  actions?: ReactNode
  source?: DataSource
  flush?: boolean
  className?: string
  children: ReactNode
  'aria-label'?: string
}

export function Panel({ title, hint, actions, source, flush, className, children, ...rest }: PanelProps) {
  return (
    <section className={cx(s.panel, className)} aria-label={rest['aria-label']}>
      {(title || actions || source) && (
        <header className={s.panelHead}>
          {title && <h2 className={s.panelTitle}>{title}</h2>}
          {hint && <span className={s.panelHint}>{hint}</span>}
          <div className={s.panelActions}>
            {source && <SourceTag source={source} />}
            {actions}
          </div>
        </header>
      )}
      <div className={cx(s.panelBody, flush && s.flush)}>{children}</div>
    </section>
  )
}

// ---------------------------------------------------------------- status

export type Tone = 'good' | 'warn' | 'crit' | 'neutral' | 'accent'

const toneIcon = {
  good: CheckCircle2,
  warn: AlertTriangle,
  crit: OctagonAlert,
  neutral: CircleDashed,
  accent: Info,
} as const

/** A state with meaning: always icon + words, never colour alone. */
export function StatusBadge({ tone, children, icon = true }: { tone: Tone; children: ReactNode; icon?: boolean }) {
  const Icon = toneIcon[tone]
  return (
    <span className={cx(s.badge, s[tone])}>
      {icon && <Icon size={14} strokeWidth={2} aria-hidden />}
      {children}
    </span>
  )
}

const sourceLabel: Record<DataSource, string> = { LIVE: 'Live', SIMULATED: 'Simulated', OFFLINE: 'Offline' }
const sourceTitle: Record<DataSource, string> = {
  LIVE: 'Measured by the node and received over CAN',
  SIMULATED: 'The node is not reporting; the gateway fills in modelled values',
  OFFLINE: 'No data from this node',
}

/** Where a panel's numbers come from. */
export function SourceTag({ source, label }: { source: DataSource; label?: string }) {
  return (
    <span className={cx(s.source, s[`src${source}`])} title={sourceTitle[source]} data-source={source}>
      <span className={s.dot} aria-hidden />
      {label ?? sourceLabel[source]}
    </span>
  )
}

// ---------------------------------------------------------------- controls

type Variant = 'default' | 'primary' | 'ghost' | 'danger'

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: Variant
  size?: 'md' | 'sm'
  iconOnly?: boolean
}

export function Button({ variant = 'default', size = 'md', iconOnly, className, type = 'button', ...rest }: ButtonProps) {
  return (
    <button
      type={type}
      className={cx(s.btn, variant !== 'default' && s[variant], size === 'sm' && s.sm, iconOnly && s.icon, className)}
      {...rest}
    />
  )
}

interface SegmentedProps<T extends string> {
  value: T
  onChange: (v: T) => void
  options: { value: T; label: ReactNode }[]
  label: string
}

export function Segmented<T extends string>({ value, onChange, options, label }: SegmentedProps<T>) {
  return (
    <ToggleGroup.Root
      type="single"
      value={value}
      onValueChange={(v) => v && onChange(v as T)}
      className={s.segmented}
      aria-label={label}
    >
      {options.map((o) => (
        <ToggleGroup.Item key={o.value} value={o.value} className={s.segment}>
          {o.label}
        </ToggleGroup.Item>
      ))}
    </ToggleGroup.Root>
  )
}

export function Switch(props: ComponentProps<typeof RadixSwitch.Root>) {
  return (
    <RadixSwitch.Root {...props} className={s.switch}>
      <RadixSwitch.Thumb className={s.thumb} />
    </RadixSwitch.Root>
  )
}

export function Field({ label, error, children }: { label: string; error?: string; children: ReactNode }) {
  return (
    <label className={s.field}>
      <span className={s.label}>{label}</span>
      {children}
      {error && <span className={s.fieldError}>{error}</span>}
    </label>
  )
}

export function Input(props: ComponentProps<'input'>) {
  return <input {...props} className={cx(s.input, props.className)} />
}

export function Select(props: ComponentProps<'select'>) {
  return <select {...props} className={cx(s.input, props.className)} />
}

// ---------------------------------------------------------------- dialogs

interface ConfirmProps {
  open: boolean
  onOpenChange: (open: boolean) => void
  title: string
  children: ReactNode
  confirmLabel: string
  danger?: boolean
  busy?: boolean
  onConfirm: () => void
}

export function ConfirmDialog({ open, onOpenChange, title, children, confirmLabel, danger, busy, onConfirm }: ConfirmProps) {
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className={s.overlay} />
        <Dialog.Content className={s.dialog}>
          <Dialog.Title className={s.dialogTitle}>{title}</Dialog.Title>
          <Dialog.Description asChild>
            <div className={s.dialogText}>{children}</div>
          </Dialog.Description>
          <div className={s.dialogActions}>
            <Dialog.Close asChild>
              <Button variant="ghost">Cancel</Button>
            </Dialog.Close>
            <Button variant={danger ? 'danger' : 'primary'} onClick={onConfirm} disabled={busy}>
              {confirmLabel}
            </Button>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

export function FormDialog({ open, onOpenChange, title, children }: {
  open: boolean
  onOpenChange: (open: boolean) => void
  title: string
  children: ReactNode
}) {
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className={s.overlay} />
        <Dialog.Content className={s.dialog} aria-describedby={undefined}>
          <Dialog.Title className={s.dialogTitle}>{title}</Dialog.Title>
          {children}
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

export function DialogActions({ children }: { children: ReactNode }) {
  return <div className={s.dialogActions}>{children}</div>
}

// ---------------------------------------------------------------- misc

export function Empty({ icon, title, children }: { icon?: ReactNode; title: string; children?: ReactNode }) {
  return (
    <div className={s.empty}>
      {icon && <div className={s.emptyIcon}>{icon}</div>}
      <div className={s.emptyTitle}>{title}</div>
      {children && <div>{children}</div>}
    </div>
  )
}

export function Notice({ icon, children }: { icon?: ReactNode; children: ReactNode }) {
  return (
    <div className={s.notice} role="note">
      {icon ?? <Info size={16} aria-hidden />}
      <div>{children}</div>
    </div>
  )
}

export function KeyValues({ children }: { children: ReactNode }) {
  return <dl className={s.kv}>{children}</dl>
}

export function KeyValue({ label, children }: { label: ReactNode; children: ReactNode }) {
  return (
    <div style={{ display: 'contents' }}>
      <dt>{label}</dt>
      <dd>{children}</dd>
    </div>
  )
}
