import { useLayoutEffect, useRef, useState } from 'react'
import { fmt } from '../lib/format'
import s from './viz.module.css'

interface Props {
  cellsMv: number[]
  firstCellNo?: number
  balancing?: number[]
  height?: number
  selected?: number | null
  onSelect?: (cellNo: number) => void
  compact?: boolean
}

function useWidth<T extends HTMLElement>() {
  const ref = useRef<T>(null)
  const [w, setW] = useState(0)
  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    const ro = new ResizeObserver(([e]) => setW(Math.floor(e.contentRect.width)))
    ro.observe(el)
    setW(Math.floor(el.getBoundingClientRect().width))
    return () => ro.disconnect()
  }, [])
  return [ref, w] as const
}

/** Nice symmetric limit: never tighter than ±10 mV, so sensor noise is not drawn as a problem. */
function niceLimit(maxAbs: number) {
  const steps = [10, 20, 25, 50, 100, 200, 250, 500, 1000]
  return steps.find((v) => v >= maxAbs) ?? Math.ceil(maxAbs / 500) * 500
}

/**
 * Each cell's deviation from the pack average, in mV. Bars grow from the zero line
 * (the average), so a 3.7 V cell and a 3.71 V cell are told apart honestly without a
 * truncated voltage axis. The lowest and highest cells are labelled directly.
 */
export function CellChart({ cellsMv, firstCellNo = 1, balancing = [], height = 220, selected, onSelect, compact }: Props) {
  const [ref, width] = useWidth<HTMLDivElement>()
  const [hover, setHover] = useState<number | null>(null)

  const n = cellsMv.length
  const avg = cellsMv.reduce((a, b) => a + b, 0) / Math.max(1, n)
  const dev = cellsMv.map((v) => v - avg)
  const lim = niceLimit(Math.max(...dev.map(Math.abs), 1))
  const minIdx = cellsMv.indexOf(Math.min(...cellsMv))
  const maxIdx = cellsMv.indexOf(Math.max(...cellsMv))

  const padL = compact ? 34 : 44
  const padR = 8
  const padT = 18
  const padB = compact ? 24 : 30
  const plotW = Math.max(0, width - padL - padR)
  const plotH = height - padT - padB
  const band = n ? plotW / n : 0
  const barW = Math.max(4, Math.min(24, band - 2))
  const y0 = padT + plotH / 2
  const scale = plotH / 2 / lim
  const ticks = [-lim, -lim / 2, 0, lim / 2, lim]

  return (
    <div ref={ref} className={s.cellChart} style={{ height }}>
      {width > 0 && (
        <svg width={width} height={height} role="img"
          aria-label={`Cell voltages: average ${fmt.d3(avg / 1000)} V, lowest cell ${firstCellNo + minIdx}, highest cell ${firstCellNo + maxIdx}`}>
          {ticks.map((t) => {
            const y = y0 - t * scale
            return (
              <g key={t}>
                <line x1={padL} x2={width - padR} y1={y} y2={y} className={t === 0 ? s.axisZero : s.grid} />
                <text x={padL - 8} y={y} className={s.tick} textAnchor="end" dominantBaseline="middle">
                  {t > 0 ? '+' : ''}{fmt.int(t)}
                </text>
              </g>
            )
          })}
          {!compact && (
            <text x={padL - 8} y={padT - 8} className={s.axisTitle} textAnchor="end">mV</text>
          )}

          {dev.map((d, i) => {
            const cellNo = firstCellNo + i
            const x = padL + i * band + (band - barW) / 2
            const h = Math.max(1, Math.abs(d) * scale)
            const up = d >= 0
            const emphasised = selected == null ? true : selected === cellNo
            return (
              <g key={i}>
                <path
                  d={barPath(x, y0, barW, h, up)}
                  className={s.bar}
                  style={{ opacity: hover === i ? 0.82 : emphasised ? 1 : 0.35 }}
                />
                {balancing.includes(i + 1) && (
                  <circle cx={x + barW / 2} cy={up ? y0 + 7 : y0 - 7} r={2.5} className={s.balDot} />
                )}
                {!compact && (i === minIdx || i === maxIdx) && (
                  <text
                    x={x + barW / 2}
                    y={up ? y0 - h - 6 : y0 + h + 12}
                    className={s.barLabel}
                    textAnchor="middle"
                  >
                    {fmt.d3(cellsMv[i] / 1000)}
                  </text>
                )}
                <text x={x + barW / 2} y={height - (compact ? 6 : 10)} className={s.tick} textAnchor="middle">
                  {compact && n > 12 && i % 2 === 1 ? '' : cellNo}
                </text>
                {/* hit target: the whole column, wider than the bar */}
                <rect
                  x={padL + i * band}
                  y={padT}
                  width={band}
                  height={plotH}
                  fill="transparent"
                  tabIndex={onSelect ? 0 : -1}
                  role={onSelect ? 'button' : undefined}
                  aria-label={`Cell ${cellNo}: ${fmt.d3(cellsMv[i] / 1000)} volts, ${d >= 0 ? '+' : ''}${fmt.d1(d)} millivolts from average`}
                  onPointerEnter={() => setHover(i)}
                  onPointerLeave={() => setHover(null)}
                  onFocus={() => setHover(i)}
                  onBlur={() => setHover(null)}
                  onClick={() => onSelect?.(cellNo)}
                  onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && onSelect?.(cellNo)}
                  style={{ cursor: onSelect ? 'pointer' : 'default', outline: 'none' }}
                />
              </g>
            )
          })}
        </svg>
      )}
      {hover !== null && width > 0 && (
        <div
          className={s.tooltip}
          style={{
            left: Math.min(width - 150, Math.max(0, padL + hover * band + band / 2 - 75)),
            top: 0,
          }}
        >
          <div className={s.ttValue}>{fmt.d3(cellsMv[hover] / 1000)} V</div>
          <div className={s.ttRow}>
            Cell {firstCellNo + hover} · {dev[hover] >= 0 ? '+' : ''}{fmt.d1(dev[hover])} mV vs avg
          </div>
          {balancing.includes(hover + 1) && <div className={s.ttRow}>Balancing</div>}
        </div>
      )}
    </div>
  )
}

/** Rounded (4px) at the data end, square at the baseline. */
function barPath(x: number, y0: number, w: number, h: number, up: boolean) {
  const r = Math.min(4, w / 2, h)
  if (up) {
    const top = y0 - h
    return `M${x},${y0} V${top + r} Q${x},${top} ${x + r},${top} H${x + w - r} Q${x + w},${top} ${x + w},${top + r} V${y0} Z`
  }
  const bot = y0 + h
  return `M${x},${y0} V${bot - r} Q${x},${bot} ${x + r},${bot} H${x + w - r} Q${x + w},${bot} ${x + w},${bot - r} V${y0} Z`
}
