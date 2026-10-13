import type { DataSource, LiveTelemetry } from '../api/types'

/** A snapshot shaped exactly like GET /telemetry/latest. */
export function liveTelemetry(master: DataSource = 'LIVE', controller: DataSource = 'LIVE'): LiveTelemetry {
  const now = new Date().toISOString()
  return {
    packId: 1,
    gatewayOnline: true,
    lastMessageAt: now,
    serverTime: now,
    snapshot: {
      deviceId: 'dbms-01',
      recordedAt: now,
      receivedAt: now,
      timeFromGateway: false,
      seq: 42,
      replay: false,
      pack: {
        voltageMv: 59380, currentMa: 0, socPct: 52.4, sohPct: 35, remainingAh: 12.1, capacityAh: 23.2, dischargedAh: 0,
        limits: { chargeA: 12, dischargeA: 40, chargeAllowed: true, dischargeAllowed: true, full: false, empty: false },
      },
      modules: [{
        nodeId: 1, source: master, state: 'IDLE', firstCellNo: 1,
        cellsMv: [3712, 3710, 3709, 3713, 3711, 3708, 3712, 3714, 3710, 3709, 3711, 3712, 3710, 3713, 3709, 3711],
        cellMinMv: 3708, cellMaxMv: 3714, cellAvgMv: 3711, cellMinNo: 6, cellMaxNo: 8,
        tempsDc: [298, 301, 296, 300], dieTempC: 33,
        balancing: { enabled: false, active: false, mask: 0, cells: [], minutes: 0 },
        faultBits: 0, faults: [], warningBits: 0, warnings: [], uptimeS: 3600, firmware: '1.0',
      }],
      controller: {
        source: controller, state: 'STANDBY',
        contactors: { mainNeg: false, precharge: false, mainPos: false, chargeEnable: false },
        fanDutyPct: 0, fanRpm: 0, keyOn: false, estopOk: true,
        faultBits: 0, faults: [], warningBits: 0, warnings: [], uptimeS: 3600, firmware: '1.0',
      },
      gateway: { firmware: '1.0-exh', rssiDbm: -58, uptimeS: 3600, canRxFrames: 50400, canE2eErrors: 0, canBusOff: 0, buffered: 0 },
    },
  }
}
