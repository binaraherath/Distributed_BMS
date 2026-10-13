// Mirrors the backend DTOs (Software/backend, package io.github.binaraherath.dbms).
// Times are ISO-8601 strings in UTC.

export type Role = 'ADMIN' | 'VIEWER'
export type DataSource = 'LIVE' | 'SIMULATED' | 'OFFLINE'

export interface Me {
  username: string
  displayName: string
  role: Role
}

export interface ModuleInfo {
  nodeId: number
  firstCellNo: number
  cellCount: number
}

export interface Pack {
  id: number
  deviceId: string
  name: string
  location: string | null
  chemistry: string
  seriesCells: number
  ratedCapacityAh: number | null
  gatewayOnline: boolean
  lastSeenAt: string | null
  createdAt: string
  modules: ModuleInfo[]
}

export interface PackUpdate {
  name?: string
  location?: string
  ratedCapacityAh?: number
}

// ---- live snapshot ----

export interface Limits {
  chargeA: number
  dischargeA: number
  chargeAllowed: boolean
  dischargeAllowed: boolean
  full: boolean
  empty: boolean
}

export interface PackMetrics {
  voltageMv: number
  currentMa: number
  socPct: number
  sohPct: number
  remainingAh: number
  capacityAh: number
  dischargedAh: number
  limits: Limits
}

export interface Balancing {
  enabled: boolean
  active: boolean
  mask: number
  cells: number[]
  minutes: number
}

export interface ModuleSnapshot {
  nodeId: number
  source: DataSource
  state: string
  firstCellNo: number
  cellsMv: number[]
  cellMinMv: number
  cellMaxMv: number
  cellAvgMv: number
  cellMinNo: number
  cellMaxNo: number
  tempsDc: number[]
  dieTempC: number
  balancing: Balancing
  faultBits: number
  faults: string[]
  warningBits: number
  warnings: string[]
  uptimeS: number
  firmware: string
}

export interface Contactors {
  mainNeg: boolean
  precharge: boolean
  mainPos: boolean
  chargeEnable: boolean
}

export interface ControllerSnapshot {
  source: DataSource
  state: string
  contactors: Contactors
  fanDutyPct: number
  fanRpm: number
  keyOn: boolean
  estopOk: boolean
  faultBits: number
  faults: string[]
  warningBits: number
  warnings: string[]
  uptimeS: number
  firmware: string
}

export interface GatewaySnapshot {
  firmware: string
  rssiDbm: number
  uptimeS: number
  canRxFrames: number
  canE2eErrors: number
  canBusOff: number
  buffered: number
}

export interface PackSnapshot {
  deviceId: string
  recordedAt: string
  receivedAt: string
  timeFromGateway: boolean
  seq: number
  replay: boolean
  pack: PackMetrics
  modules: ModuleSnapshot[]
  controller: ControllerSnapshot
  gateway: GatewaySnapshot
}

export interface LiveTelemetry {
  packId: number
  gatewayOnline: boolean
  lastMessageAt: string | null
  serverTime: string
  snapshot: PackSnapshot
}

export interface LiveHeartbeat {
  packId: number
  gatewayOnline: boolean
  lastMessageAt: string | null
  serverTime: string
}

// ---- history ----

export type Resolution = 'RAW' | 'ONE_MINUTE' | 'FIFTEEN_MINUTES'

export interface HistoryPoint {
  t: string
  voltageV: number
  currentA: number
  socPct: number
  tempMaxC: number
  cellDeltaMv: number
  cellMinMv: number
  cellMaxMv: number
  simulated: boolean
}

export interface History {
  packId: number
  from: string
  to: string
  resolution: Resolution
  bucketSeconds: number
  points: HistoryPoint[]
}

export interface CellHistory {
  packId: number
  cellNo: number
  resolution: Resolution
  points: { t: string; voltageMv: number }[]
}

// ---- alarms, commands, users ----

export interface Page<T> {
  content: T[]
  page: number
  size: number
  totalElements: number
  totalPages: number
}

export type Severity = 'CRITICAL' | 'WARNING'

export interface Alarm {
  id: number
  source: 'BMS' | 'CTRL'
  code: string
  description: string
  severity: Severity
  raisedAt: string
  clearedAt: string | null
  active: boolean
  acknowledgedAt: string | null
  acknowledgedBy: string | null
}

export type CommandType = 'CLEAR_FAULTS' | 'SET_BALANCING' | 'FAN_OVERRIDE' | 'STOP'
export type CommandStatus = 'PENDING' | 'PUBLISHED' | 'ACCEPTED' | 'REJECTED' | 'TIMED_OUT' | 'FAILED'

export interface Command {
  id: number
  packId: number
  type: CommandType
  argument: number | null
  status: CommandStatus
  issuedBy: string
  issuedAt: string
  completedAt: string | null
  resultMessage: string | null
}

export interface User {
  id: number
  username: string
  displayName: string
  role: Role
  enabled: boolean
  createdAt: string
}

/** RFC 9457 problem detail, as returned by the backend for every error. */
export interface Problem {
  type?: string
  title?: string
  status: number
  detail?: string
  errors?: Record<string, string>
}
