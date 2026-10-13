import { useEffect, useState } from 'react'
import { NavLink, Outlet, useLocation, useMatches, useNavigate, useOutletContext } from 'react-router'
import * as Dialog from '@radix-ui/react-dialog'
import * as Menu from '@radix-ui/react-dropdown-menu'
import {
  Activity, Bell, Check, FileCode2, Gauge, LogOut, Monitor, Moon, MoreHorizontal, Rows3, Settings2,
  SlidersHorizontal, Sun,
} from 'lucide-react'
import { useAlarms, useLogout } from '../api/queries'
import type { Me } from '../api/types'
import { useLive, type Live } from '../app/live'
import { usePack } from '../app/pack'
import { useTheme, type ThemeChoice } from '../app/theme'
import { SourceTag } from '../components/ui'
import { ago } from '../lib/format'
import { BrandMark } from './BrandMark'
import s from './AppShell.module.css'

export interface ShellContext {
  me: Me
  live: Live
  isAdmin: boolean
}

export function useShell() {
  return useOutletContext<ShellContext>()
}

const NAV = [
  { to: '/', label: 'Overview', icon: Gauge, end: true },
  { to: '/cells', label: 'Cells', icon: Rows3 },
  { to: '/history', label: 'History', icon: Activity },
  { to: '/alarms', label: 'Alarms', icon: Bell },
  { to: '/control', label: 'Control', icon: SlidersHorizontal, admin: true },
  { to: '/settings', label: 'Settings', icon: Settings2 },
]

export function AppShell({ me }: { me: Me }) {
  const { pack, packs, select } = usePack()
  const live = useLive(pack?.id)
  const isAdmin = me.role === 'ADMIN'
  const activeAlarms = useAlarms(pack?.id, { status: 'ACTIVE', severity: 'ALL', page: 0, size: 1 }).data?.totalElements ?? 0
  const matches = useMatches()
  const title = (matches.at(-1)?.handle as { title?: string } | undefined)?.title ?? ''
  const nav = NAV.filter((n) => !n.admin || isAdmin)

  useEffect(() => {
    document.title = title ? `${title} · Distributed BMS` : 'Distributed BMS'
  }, [title])

  return (
    <div className={s.shell}>
      <aside className={s.sidebar}>
        <NavLink to="/" className={s.brand} aria-label="Distributed BMS home">
          <BrandMark />
          <div>
            <div className={s.brandName}>Distributed BMS</div>
            <div className={s.brandSub}>Battery operations</div>
          </div>
        </NavLink>

        <div className={`eyebrow ${s.navLabel}`}>Monitor</div>
        <nav className={s.nav} aria-label="Main">
          {nav.map(({ to, label, icon: Icon, end }) => (
            <NavLink key={to} to={to} end={end} className={s.navItem}>
              <Icon size={17} strokeWidth={1.8} aria-hidden />
              {label}
              {to === '/alarms' && activeAlarms > 0 && (
                <span className={s.count} aria-label={`${activeAlarms} active`}>{activeAlarms}</span>
              )}
            </NavLink>
          ))}
        </nav>

        <div className={s.sideFoot}>
          {packs.length > 1 && (
            <select
              className={s.packSelect}
              value={pack?.id}
              onChange={(e) => select(Number(e.target.value))}
              aria-label="Battery pack"
            >
              {packs.map((p) => (
                <option key={p.id} value={p.id}>{p.name}</option>
              ))}
            </select>
          )}
          {pack && (
            <div className={s.device}>
              <span className="mono">{pack.deviceId}</span>
              <span>{pack.seriesCells}S · {pack.chemistry.split(' ')[0]}</span>
            </div>
          )}
        </div>
      </aside>

      <div className={s.main}>
        <header className={s.topbar}>
          <span className={s.mobileBrand}><BrandMark size={26} /></span>
          <div className={s.titleBlock}>
            <h1 className={s.pageTitle}>{title}</h1>
            {pack && <span className={s.packName}>{pack.name}{pack.location ? ` · ${pack.location}` : ''}</span>}
          </div>
          <div className={s.topActions}>
            <LiveStatus live={live} />
            <ThemeMenu />
            <UserMenu me={me} />
          </div>
        </header>

        <main className={s.content} id="main">
          <Outlet context={{ me, live, isAdmin } satisfies ShellContext} />
        </main>
      </div>

      <MobileTabs isAdmin={isAdmin} activeAlarms={activeAlarms} />
    </div>
  )
}

/** One line that tells the room whether the numbers are live. */
function LiveStatus({ live }: { live: Live }) {
  const [, tick] = useState(0)
  useEffect(() => {
    const t = setInterval(() => tick((n) => n + 1), 1000)
    return () => clearInterval(t)
  }, [])

  const snap = live.data?.snapshot
  let source: 'LIVE' | 'SIMULATED' | 'OFFLINE' = 'OFFLINE'
  let label: string | undefined
  if (live.gatewayOnline && snap) {
    const nodes = [...snap.modules.map((m) => m.source), snap.controller.source]
    const simulated = nodes.filter((n) => n === 'SIMULATED').length
    source = simulated > 0 ? 'SIMULATED' : 'LIVE'
    if (simulated > 0 && simulated < nodes.length) label = 'Partly simulated'
  }
  const text = live.stream === 'reconnecting'
    ? 'Reconnecting…'
    : live.lastMessageAt ? `Updated ${ago(live.lastMessageAt)}` : 'Waiting for data'

  return (
    <div className={s.liveStatus} aria-live="polite">
      <span className={s.liveText}>{text}</span>
      <SourceTag source={source} label={label} />
    </div>
  )
}

const themeOptions: { value: ThemeChoice; label: string; icon: typeof Sun }[] = [
  { value: 'system', label: 'System', icon: Monitor },
  { value: 'light', label: 'Light', icon: Sun },
  { value: 'dark', label: 'Dark', icon: Moon },
]

function ThemeMenu() {
  const { choice, resolved, setChoice } = useTheme()
  const Icon = resolved === 'dark' ? Moon : Sun
  return (
    <Menu.Root>
      <Menu.Trigger asChild>
        <button className={`${s.avatar}`} aria-label="Theme" style={{ borderRadius: 8 }}>
          <Icon size={16} strokeWidth={1.8} />
        </button>
      </Menu.Trigger>
      <Menu.Portal>
        <Menu.Content className={s.menu} align="end" sideOffset={8}>
          <div className={`eyebrow ${s.menuLabel}`}>Appearance</div>
          {themeOptions.map(({ value, label, icon: I }) => (
            <Menu.Item key={value} className={s.menuItem} onSelect={() => setChoice(value)}>
              <I size={15} strokeWidth={1.8} />
              {label}
              {choice === value && <Check size={15} className={s.menuCheck} />}
            </Menu.Item>
          ))}
        </Menu.Content>
      </Menu.Portal>
    </Menu.Root>
  )
}

function initials(name: string) {
  return name.split(/\s+/).map((p) => p[0]).join('').slice(0, 2).toUpperCase()
}

function UserMenu({ me }: { me: Me }) {
  const logout = useLogout()
  const navigate = useNavigate()
  return (
    <Menu.Root>
      <Menu.Trigger asChild>
        <button className={s.avatar} aria-label={`Account: ${me.displayName}`}>{initials(me.displayName)}</button>
      </Menu.Trigger>
      <Menu.Portal>
        <Menu.Content className={s.menu} align="end" sideOffset={8}>
          <div className={s.who}>
            <span className={s.whoName}>{me.displayName}</span>
            <span className="eyebrow">{me.role === 'ADMIN' ? 'Administrator' : 'Viewer'} · {me.username}</span>
          </div>
          <Menu.Separator className={s.menuSep} />
          <Menu.Item className={s.menuItem} asChild>
            <a href="/api/docs" target="_blank" rel="noreferrer"><FileCode2 size={15} />API documentation</a>
          </Menu.Item>
          <Menu.Item
            className={s.menuItem}
            onSelect={() => logout.mutate(undefined, { onSettled: () => navigate('/login', { replace: true }) })}
          >
            <LogOut size={15} />
            Sign out
          </Menu.Item>
        </Menu.Content>
      </Menu.Portal>
    </Menu.Root>
  )
}

function MobileTabs({ isAdmin, activeAlarms }: { isAdmin: boolean; activeAlarms: number }) {
  const [open, setOpen] = useState(false)
  const location = useLocation()
  const close = () => setOpen(false)
  const moreActive = ['/control', '/settings'].some((p) => location.pathname.startsWith(p))

  return (
    <nav className={s.tabbar} aria-label="Main">
      {NAV.slice(0, 4).map(({ to, label, icon: Icon, end }) => (
        <NavLink key={to} to={to} end={end} className={s.tab}>
          <Icon size={20} strokeWidth={1.8} aria-hidden />
          {label}
          {to === '/alarms' && activeAlarms > 0 && <span className={s.tabCount}>{activeAlarms}</span>}
        </NavLink>
      ))}
      <Dialog.Root open={open} onOpenChange={setOpen}>
        <Dialog.Trigger className={s.tab} aria-current={moreActive ? 'page' : undefined}>
          <MoreHorizontal size={20} strokeWidth={1.8} aria-hidden />
          More
        </Dialog.Trigger>
        <Dialog.Portal>
          <Dialog.Overlay className={s.overlay} />
          <Dialog.Content className={s.sheet} aria-describedby={undefined}>
            <div className={s.sheetGrip} />
            <Dialog.Title className="visually-hidden">More</Dialog.Title>
            {isAdmin && (
              <NavLink to="/control" className={s.menuItem} onClick={close}><SlidersHorizontal size={17} />Control</NavLink>
            )}
            <NavLink to="/settings" className={s.menuItem} onClick={close}><Settings2 size={17} />Settings</NavLink>
            <a href="/api/docs" className={s.menuItem} target="_blank" rel="noreferrer"><FileCode2 size={17} />API documentation</a>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </nav>
  )
}
