import { describe, expect, it } from 'vitest'
import { duration, fmt, humanize } from '../lib/format'
import { balancingText, currentDirection } from '../lib/status'
import { liveTelemetry } from './fixtures'
import { niceTicks } from '../lib/ticks'

describe('formatting', () => {
  it('never shows a negative zero current', () => {
    expect(fmt.signed2(-0.001)).toBe('0.00')
    expect(fmt.signed2(1.234)).toBe('+1.23')
    expect(fmt.signed2(-12.5)).toBe('-12.50')
  })

  it('turns firmware codes into words', () => {
    expect(humanize('OVER_TEMP')).toBe('Over temp')
  })

  it('formats uptime compactly', () => {
    expect(duration(59)).toBe('59s')
    expect(duration(3725)).toBe('1h 2m')
    expect(duration(90061)).toBe('1d 1h')
  })
})

describe('status text', () => {
  it('reports "Not balancing" when no bleed resistor is switched', () => {
    const m = liveTelemetry().snapshot.modules[0]
    expect(balancingText(m).text).toBe('Not balancing · disabled')
    expect(balancingText({ ...m, balancing: { ...m.balancing, enabled: true } }).text).toBe('Not balancing')
    expect(balancingText({ ...m, balancing: { enabled: true, active: true, mask: 6, cells: [2, 3], minutes: 4 } }).text)
      .toBe('Balancing cells 2, 3')
  })

  it('calls a zero current "At rest"', () => {
    expect(currentDirection(0)).toBe('At rest')
    expect(currentDirection(-3)).toBe('Discharging')
  })
})

describe('axis ticks', () => {
  it('uses round steps that enclose the data', () => {
    expect(niceTicks(3.676, 3.803)).toEqual([3.65, 3.7, 3.75, 3.8, 3.85])
    expect(niceTicks(0, 100)).toEqual([0, 25, 50, 75, 100])
    expect(niceTicks(-24.1, -2.4)).toEqual([-30, -20, -10, 0])
  })
})
