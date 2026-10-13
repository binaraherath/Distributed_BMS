import { useState, type FormEvent } from 'react'
import { Navigate, useLocation, useNavigate } from 'react-router'
import { errorMessage } from '../api/client'
import { useLogin, useMe } from '../api/queries'
import { Button, Field, Input } from '../components/ui'
import { BrandMark } from '../layout/BrandMark'
import s from './LoginPage.module.css'

export function LoginPage() {
  const me = useMe()
  const login = useLogin()
  const navigate = useNavigate()
  const location = useLocation()
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const next = (location.state as { from?: string } | null)?.from ?? '/'

  if (me.data) return <Navigate to={next} replace />

  const submit = (e: FormEvent) => {
    e.preventDefault()
    login.mutate({ username: username.trim(), password }, { onSuccess: () => navigate(next, { replace: true }) })
  }

  return (
    <div className={s.page}>
      <section className={s.story} aria-hidden>
        <div className={s.storyBrand}>
          <BrandMark size={32} />
          <span>Distributed BMS</span>
        </div>
        <div className={s.storyBody}>
          <p className="eyebrow">Second-life battery monitoring</p>
          <h2 className={s.storyTitle}>Every cell, every node, accounted for.</h2>
          <p className={s.storyText}>
            A 16-series pack of reused Nissan Leaf modules, watched by a master monitor, a system controller and
            an IoT gateway on one CAN bus.
          </p>
        </div>
        <Topology />
      </section>

      <main className={s.formSide}>
        <form className={s.form} onSubmit={submit}>
          <div className={s.mobileBrand}><BrandMark size={36} /></div>
          <div>
            <h1 className={s.title}>Sign in</h1>
            <p className={s.subtitle}>Use the account the administrator gave you.</p>
          </div>
          <Field label="Username">
            <Input value={username} onChange={(e) => setUsername(e.target.value)}
              autoComplete="username" autoCapitalize="none" spellCheck={false} required autoFocus />
          </Field>
          <Field label="Password">
            <Input type="password" value={password} onChange={(e) => setPassword(e.target.value)}
              autoComplete="current-password" required />
          </Field>
          {login.error && <p className={s.error} role="alert">{errorMessage(login.error)}</p>}
          <Button type="submit" variant="primary" disabled={login.isPending} className={s.submit}>
            {login.isPending ? 'Signing in…' : 'Sign in'}
          </Button>
        </form>
        <p className={s.foot}>University of Moratuwa · Department of Computer Science &amp; Engineering</p>
      </main>
    </div>
  )
}

/** The system in one line drawing: three nodes on a bus, a link up to the server. */
function Topology() {
  return (
    <svg viewBox="0 0 420 150" className={s.topo}>
      <g fill="none" strokeWidth="1.5" strokeLinecap="round">
        <path d="M30 110 H390" className={s.topoBus} />
        {[70, 180, 290].map((x) => <path key={x} d={`M${x} 110 V82`} className={s.topoBus} />)}
        <path d="M290 54 V36 H360" className={s.topoLink} strokeDasharray="2 5" />
      </g>
      {[
        [70, 'MASTER'],
        [180, 'CONTROLLER'],
        [290, 'GATEWAY'],
      ].map(([x, label]) => (
        <g key={label as string}>
          <rect x={(x as number) - 44} y="54" width="88" height="28" rx="6" className={s.topoNode} />
          <text x={x as number} y="72" textAnchor="middle" className={s.topoText}>{label}</text>
        </g>
      ))}
      <rect x="360" y="20" width="44" height="32" rx="6" className={s.topoServer} />
      <text x="382" y="40" textAnchor="middle" className={s.topoText}>SRV</text>
      <text x="30" y="132" className={s.topoCaption}>CAN 500 KBIT/S</text>
      <text x="300" y="28" className={s.topoCaption}>MQTT</text>
    </svg>
  )
}
