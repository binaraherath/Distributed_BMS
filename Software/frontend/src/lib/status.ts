import type { Tone } from '../components/ui'
import type { ModuleSnapshot } from '../api/types'
import { humanize } from './format'

export function bmsTone(state: string): Tone {
  switch (state) {
    case 'IDLE':
    case 'CHARGING':
    case 'DISCHARGING':
      return 'good'
    case 'FAULT':
      return 'crit'
    default:
      return 'neutral'
  }
}

export function ctrlTone(state: string): Tone {
  switch (state) {
    case 'STANDBY':
    case 'RUN':
      return 'good'
    case 'PRECHARGE':
      return 'accent'
    case 'STOPPING':
      return 'warn'
    case 'FAULT':
      return 'crit'
    default:
      return 'neutral'
  }
}

export const stateLabel = (state: string) => humanize(state)

/** What the balancing hardware is doing, in words. */
export function balancingText(m: ModuleSnapshot): { tone: Tone; text: string } {
  const b = m.balancing
  if (b.active && b.cells.length > 0) {
    return { tone: 'accent', text: `Balancing cell${b.cells.length > 1 ? 's' : ''} ${b.cells.join(', ')}` }
  }
  return { tone: 'neutral', text: b.enabled ? 'Not balancing' : 'Not balancing · disabled' }
}

export function currentDirection(currentA: number): string {
  if (currentA > 0.05) return 'Charging'
  if (currentA < -0.05) return 'Discharging'
  return 'At rest'
}

/** Same thresholds as the master firmware (bms_config.h): warning 45 °C, fault 60 °C. */
export const TEMP_WARN_C = 45
export const TEMP_FAULT_C = 60

export function tempColor(c: number): string {
  if (c >= TEMP_FAULT_C) return 'var(--crit)'
  if (c >= TEMP_WARN_C) return 'var(--warn)'
  return 'var(--accent)'
}
