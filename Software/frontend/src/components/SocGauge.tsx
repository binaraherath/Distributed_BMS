import { fmt } from '../lib/format'
import s from './viz.module.css'

interface Props {
  soc: number
  remainingAh: number
  capacityAh: number
}

const START = 135 // degrees, clockwise from 3 o'clock
const SWEEP = 270

function polar(cx: number, cy: number, r: number, deg: number) {
  const a = (deg * Math.PI) / 180
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)]
}

function arc(cx: number, cy: number, r: number, from: number, to: number) {
  const [x0, y0] = polar(cx, cy, r, from)
  const [x1, y1] = polar(cx, cy, r, to)
  const large = to - from > 180 ? 1 : 0
  return `M ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1}`
}

/**
 * State of charge as a meter: one ratio against a limit. The track is a light step
 * of the same hue; the fill turns amber below 15 % and brick below 5 %.
 */
export function SocGauge({ soc, remainingAh, capacityAh }: Props) {
  const pct = Math.max(0, Math.min(100, soc))
  const tone = pct < 5 ? 'var(--crit)' : pct < 15 ? 'var(--warn)' : 'var(--accent)'
  const size = 200
  const c = size / 2
  const r = 84
  const end = START + (SWEEP * pct) / 100

  return (
    <div className={s.gauge}>
      <svg viewBox={`0 0 ${size} ${size}`} className={s.gaugeSvg} role="img"
        aria-label={`State of charge ${fmt.d1(pct)} percent`}>
        <path d={arc(c, c, r, START, START + SWEEP)} className={s.gaugeTrack} />
        {pct > 0.2 && <path d={arc(c, c, r, START, end)} className={s.gaugeFill} style={{ stroke: tone }} />}
        {[0, 25, 50, 75, 100].map((t) => {
          const deg = START + (SWEEP * t) / 100
          const [x0, y0] = polar(c, c, r - 14, deg)
          const [x1, y1] = polar(c, c, r - 9, deg)
          return <line key={t} x1={x0} y1={y0} x2={x1} y2={y1} className={s.gaugeTick} />
        })}
      </svg>
      <div className={s.gaugeCenter}>
        <div className={s.gaugeValue}>
          {fmt.d1(pct)}
          <span className={s.gaugeUnit}>%</span>
        </div>
        <div className={s.gaugeLabel}>State of charge</div>
        <div className={s.gaugeSub}>
          <span className="tnum">{fmt.d1(remainingAh)}</span> of <span className="tnum">{fmt.d1(capacityAh)}</span> Ah
        </div>
      </div>
    </div>
  )
}
