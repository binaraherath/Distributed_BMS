import type { Contactors } from '../api/types'
import s from './viz.module.css'

/**
 * Single-line diagram of the high-voltage path: Main− on the negative rail, Main+
 * with the precharge branch on the positive rail. A closed contactor draws its
 * rail in copper; an open one leaves it in the neutral line colour.
 */
export function ContactorDiagram({ k }: { k: Contactors }) {
  const posLive = k.mainPos || k.precharge
  return (
    <figure className={s.contactors}>
      <svg viewBox="0 0 320 128" role="img" aria-label={describe(k)} className={s.contactorSvg}>
        {/* pack */}
        <g className={s.hvBox}>
          <rect x="6" y="20" width="44" height="88" rx="6" />
        </g>
        <text x="28" y="60" className={s.hvText} textAnchor="middle">PACK</text>
        <text x="28" y="76" className={s.hvSub} textAnchor="middle">16S</text>
        <text x="56" y="38" className={s.hvSub}>+</text>
        <text x="56" y="102" className={s.hvSub}>−</text>

        {/* positive rail: pack -> main+ -> load, with precharge branch above */}
        <path d="M50 44 H104" className={posLive ? s.railOn : s.rail} />
        <Switch x={104} y={44} closed={k.mainPos} />
        <path d="M144 44 H262" className={k.mainPos ? s.railOn : s.rail} />
        <path d="M84 44 V18 H104" className={k.precharge ? s.railOn : s.rail} />
        <Switch x={104} y={18} closed={k.precharge} />
        <path d="M144 18 H156" className={k.precharge ? s.railOn : s.rail} />
        <path d="M156 18 l4 -5 l6 10 l6 -10 l6 10 l6 -10 l4 5" className={k.precharge ? s.railOn : s.rail} />
        <path d="M188 18 H200 V44" className={k.precharge ? s.railOn : s.rail} />

        {/* negative rail */}
        <path d="M50 92 H104" className={k.mainNeg ? s.railOn : s.rail} />
        <Switch x={104} y={92} closed={k.mainNeg} />
        <path d="M144 92 H262" className={k.mainNeg ? s.railOn : s.rail} />

        {/* load */}
        <g className={s.hvBox}>
          <rect x="262" y="30" width="52" height="76" rx="6" />
        </g>
        <text x="288" y="72" className={s.hvText} textAnchor="middle">LOAD</text>

        <text x="124" y="64" className={s.hvLabel} textAnchor="middle">Main +</text>
        <text x="124" y="118" className={s.hvLabel} textAnchor="middle">Main −</text>
        <text x="216" y="12" className={s.hvLabel} textAnchor="middle">Precharge</text>
      </svg>
      <figcaption className={s.contactorLegend}>
        <Chip on={k.mainNeg} label="Main −" />
        <Chip on={k.precharge} label="Precharge" />
        <Chip on={k.mainPos} label="Main +" />
        <Chip on={k.chargeEnable} label="Charge" />
      </figcaption>
    </figure>
  )
}

function Switch({ x, y, closed }: { x: number; y: number; closed: boolean }) {
  return (
    <g className={closed ? s.swOn : s.sw}>
      <circle cx={x + 4} cy={y} r="3" />
      <circle cx={x + 36} cy={y} r="3" />
      {closed
        ? <path d={`M${x + 7} ${y} H${x + 33}`} />
        : <path d={`M${x + 7} ${y} L${x + 31} ${y - 12}`} />}
    </g>
  )
}

function Chip({ on, label }: { on: boolean; label: string }) {
  return (
    <span className={on ? s.chipOn : s.chip}>
      <span className={s.chipDot} aria-hidden />
      {label} <span className={s.chipState}>{on ? 'closed' : 'open'}</span>
    </span>
  )
}

function describe(k: Contactors) {
  const st = (b: boolean) => (b ? 'closed' : 'open')
  return `Contactors: main negative ${st(k.mainNeg)}, precharge ${st(k.precharge)}, main positive ${st(k.mainPos)}, charge enable ${st(k.chargeEnable)}`
}
