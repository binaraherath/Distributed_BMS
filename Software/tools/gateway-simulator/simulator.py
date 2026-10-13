#!/usr/bin/env python3
"""
Gateway simulator: publishes the same MQTT topics and JSON as the Node 3 gateway
firmware (Firmware/Node3_IoT_Gateway/src/bms_model.cpp, net_link.cpp), so the
backend and dashboard can be developed and demonstrated without the hardware.

Every node is tagged "source": "simulated", so the dashboard labels it. Use
--untagged to imitate the official firmware (no tag -> treated as live data;
handy for testing alarms, which are only raised from live data).

Profiles
  rest   pack at rest, contactors open, 0 A  (what the exhibition table really shows);
         like a real resting pack, at most one reading moves by one step every 5 s
  cycle  slow discharge / rest / charge cycle (for history charts during development)

Interactive keys when run in a terminal:  f = toggle OVER_TEMP fault,  w = toggle
CELL_HIGH warning,  q = quit.
"""
import argparse
import json
import math
import os
import random
import sys
import threading
import time

import paho.mqtt.client as mqtt

CELLS = 16
NTCS = 4
RATED_AH = 66.2
SOH_PCT = 35.0
USABLE_AH = RATED_AH * SOH_PCT / 100.0

# Same OCV table as Firmware/Node1_Monitoring_Unit/App/Src/bms_soc.c
OCV = [(3200, 0), (3450, 5), (3550, 10), (3620, 20), (3680, 30), (3730, 40), (3780, 50),
       (3840, 60), (3900, 70), (3960, 80), (4030, 90), (4100, 100)]

BMS_FAULTS = ["BQ_COMM", "BQ_CONFIG", "CELL_OV", "CELL_UV", "OVER_TEMP", "UNDER_TEMP",
              "OC_CHARGE", "OC_DISCHARGE", "SHORT_CIRCUIT", "IMBALANCE", "CELL_SENSE",
              "NTC_SENSE", "STACK_MISMATCH", "BQ_PERMANENT_FAIL"]
BMS_WARNINGS = ["CELL_HIGH", "CELL_LOW", "TEMP_HIGH", "TEMP_LOW", "IMBALANCE", "CURRENT_HIGH",
                "CTRL_LOST", "BALANCING", "ALERT_PIN", "CAN_ERROR", "WDG_RESET", "SOC_LOW",
                "BQ_ALERT", "I2C_RETRY"]
REST_STEP_S = 5.0  # rest profile: at most one displayed reading changes per step
# Rest profile uses the same fixed pack as the Node 1 demo OLED and the exhibition
# gateway fallback (Exhibition/*/demo_model.c*): 16 x 3.895 V + offsets = 62.322 V (62.32 V shown).
REST_CELL_MV = 3895
REST_CELL_OFFSET_MV = [2, -1, 4, 0, -3, 1, 5, -2, 0, 3, -4, 1, -5, 2, -1, 0]

CTRL_WARNINGS = ["FAN_FAIL", "GW_OFFLINE", "CHARGE_BLOCKED", "DERATED", "WDG_RESET",
                 "REMOTE_STOP", "FAN_OVERRIDE"]
OVER_TEMP_BIT = BMS_FAULTS.index("OVER_TEMP")
CELL_HIGH_BIT = BMS_WARNINGS.index("CELL_HIGH")


def ocv_mv(soc):
    soc = max(0.0, min(100.0, soc))
    for (v0, s0), (v1, s1) in zip(OCV, OCV[1:]):
        if soc <= s1:
            return v0 + (soc - s0) / (s1 - s0) * (v1 - v0)
    return OCV[-1][0]


def ocv_soc(mv):
    """Inverse of ocv_mv: SoC (%) of a rested cell at mv."""
    for (v0, s0), (v1, s1) in zip(OCV, OCV[1:]):
        if mv <= v1:
            return s0 + (max(mv, v0) - v0) / (v1 - v0) * (s1 - s0)
    return 100.0


def names(bits, table):
    return [table[b] if b < len(table) else "RESERVED" for b in range(24) if bits & (1 << b)]


class Pack:
    """A small, believable model of the 16S pack and the controller."""

    def __init__(self, profile, ntc_offset_c=None):
        self.profile = profile
        self.ntc_offset_c = ntc_offset_c or [0.0] * NTCS  # per-sensor reading error
        self.soc = 58.0
        self.current_a = 0.0
        self.discharged_ah = 0.0
        # Aged cells differ a little in capacity and internal resistance.
        self.cell_offset_mv = [random.uniform(-5, 5) for _ in range(CELLS)]
        self.cell_r_mohm = [random.uniform(1.4, 2.0) for _ in range(CELLS)]
        self.ambient_c = 29.5
        self.temps_c = [self.ambient_c + random.uniform(-0.4, 0.6) for _ in range(NTCS)]
        if profile == "rest":   # fixed pack, identical to the Node 1 OLED
            self.soc = ocv_soc(REST_CELL_MV)
            self.cell_offset_mv = [float(x) for x in REST_CELL_OFFSET_MV]
            self.temps_c = [self.ambient_c] * NTCS
        self.balancing_enabled = False
        self.fan_override = None
        self.ctrl_state = "STANDBY"
        self.stop_until = 0.0
        self.bms_faults = 0
        self.bms_warnings = 0
        self.fan_on = False
        self.shown_cells = None   # rest profile: readings held between steps
        self.shown_temps = None
        self.next_rest_step = 0.0
        self.started = time.monotonic()

    def target_current(self, t):
        if self.profile == "rest":
            return 0.0
        # 40-minute cycle: 15 min discharge, 5 min rest, 15 min charge, 5 min rest
        phase = (t % 2400) / 60.0
        if phase < 15:
            return -18.0 - 4.0 * math.sin(t / 37.0)
        if phase < 20:
            return 0.0
        if phase < 35:
            return 9.5
        return 0.0

    def step(self, dt):
        t = time.monotonic() - self.started
        if self.ctrl_state in ("STOPPING", "FAULT") or self.bms_faults:
            target = 0.0
        else:
            target = self.target_current(t)
        self.current_a += (target - self.current_a) * min(1.0, dt / 4.0)
        if abs(self.current_a) < 0.06:  # same dead-band as the firmware
            self.current_a = 0.0
        self.soc = max(1.0, min(99.0, self.soc + self.current_a * dt / 3600.0 / USABLE_AH * 100.0))
        if self.current_a < 0:
            self.discharged_ah += -self.current_a * dt / 3600.0

        heat = abs(self.current_a) * 0.035
        hot = 18.0 if self.bms_faults & (1 << OVER_TEMP_BIT) else 0.0
        for n in range(NTCS):
            goal = self.ambient_c + heat * (1.0 + 0.15 * n) + hot + 0.15 * math.sin(t / 300.0 + n)
            self.temps_c[n] += (goal - self.temps_c[n]) * min(1.0, dt / 90.0)

        if self.ctrl_state == "STOPPING" and time.monotonic() > self.stop_until:
            self.ctrl_state = "STANDBY"
        if self.profile == "rest":
            self.rest_step()

    def model_cells_mv(self):
        base = ocv_mv(self.soc)
        return [base + self.cell_offset_mv[c] + self.current_a * self.cell_r_mohm[c] for c in range(CELLS)]

    def rest_step(self):
        """A resting pack barely moves: instead of fresh noise every second, nudge one
        cell (1 mV) or one NTC (0.1 C) every REST_STEP_S, staying close to the model."""
        now = time.monotonic()
        if self.shown_cells is None:
            self.shown_cells = [int(round(v)) for v in self.model_cells_mv()]
            self.shown_temps = [round(x, 1) for x in self.sensed_temps()]
            self.next_rest_step = now + REST_STEP_S
            return
        sensed = self.sensed_temps()
        if now < self.next_rest_step:
            return
        self.next_rest_step = now + REST_STEP_S
        if random.random() < 0.7:
            # One cell moves 1 mV, but the steps balance out (sum stays -1..+1 mV), so the
            # pack voltage shown to 10 mV never moves and matches the Node 1 OLED.
            jit = [s - int(round(g)) for s, g in zip(self.shown_cells, self.model_cells_mv())]
            total = sum(jit)
            step = -1 if total > 0 else 1 if total < 0 else random.choice((-1, 1))
            cands = [c for c in range(CELLS) if abs(jit[c] + step) <= 2]
            if cands:
                self.shown_cells[random.choice(cands)] += step
        else:
            n = random.randrange(NTCS)
            gap = sensed[n] - self.shown_temps[n]
            step = math.copysign(0.1, gap) if abs(gap) >= 0.1 else random.choice((-0.1, 0.1))
            if abs(self.shown_temps[n] + step - sensed[n]) > 0.3:
                step = -step
            self.shown_temps[n] = round(self.shown_temps[n] + step, 1)

    def cells_mv(self):
        if self.shown_cells is not None:
            return list(self.shown_cells)
        noise = lambda: random.uniform(-0.8, 0.8)
        return [int(round(v + noise())) for v in self.model_cells_mv()]

    def sensed_temps(self):
        return [t + e for t, e in zip(self.temps_c, self.ntc_offset_c)]

    def temps(self):
        if self.shown_temps is not None:
            return list(self.shown_temps)
        return [round(x, 1) for x in self.sensed_temps()]

    def fan_duty(self):
        if self.fan_override is not None:
            return self.fan_override
        tmax = max(self.temps())  # same curve as Node 2 ctrl_app.c: on at 30 C, off below 27 C
        if tmax >= 30:
            self.fan_on = True
        elif tmax < 27:
            self.fan_on = False
        if not self.fan_on:
            return 0
        if tmax >= 45:
            return 100
        return int(30 + max(0.0, tmax - 30) / (45 - 30) * 70)

    def snapshot(self, device, seq, tagged, gw_started):
        cells = self.cells_mv()
        temps = self.temps()
        cmin, cmax = min(cells), max(cells)
        v = sum(cells) / 1000.0
        i = self.current_a
        state = "FAULT" if self.bms_faults else ("CHARGING" if i > 0.5 else "DISCHARGING" if i < -0.5 else "IDLE")
        running = self.profile == "cycle" and self.ctrl_state not in ("STOPPING", "FAULT") and not self.bms_faults
        ctrl_state = self.ctrl_state if not running else "RUN"
        closed = ctrl_state == "RUN"
        uptime = int(time.monotonic() - self.started)
        bms = {
            "online": True, "state": state,
            "pack_v": round(v, 2), "current_a": round(i, 2), "power_w": round(v * i, 1),
            "soc": round(self.soc, 1), "soh": SOH_PCT,
            "remaining_ah": round(USABLE_AH * self.soc / 100.0, 2), "capacity_ah": round(USABLE_AH, 2),
            "discharged_ah": round(self.discharged_ah, 1),
            "cells_mv": cells, "cell_min_mv": cmin, "cell_max_mv": cmax,
            "cell_avg_mv": int(round(sum(cells) / CELLS)), "cell_delta_mv": cmax - cmin,
            "cell_min_no": cells.index(cmin) + 1, "cell_max_no": cells.index(cmax) + 1,
            "temps_c": temps, "temp_max_c": int(max(temps)),
            "bq_die_c": int(self.ambient_c + 1.5),
            "limits": {"charge_a": 12.0, "discharge_a": 40.0, "charge_allowed": not self.bms_faults,
                       "discharge_allowed": not self.bms_faults, "full": False, "empty": False},
            "balancing": {"enabled": self.balancing_enabled, "active": False, "mask": 0, "cells": [], "minutes": 0},
            "fault_bits": self.bms_faults, "faults": names(self.bms_faults, BMS_FAULTS),
            "warning_bits": self.bms_warnings, "warnings": names(self.bms_warnings, BMS_WARNINGS),
            "bq76952": {"safety_a": 0, "safety_b": 0, "safety_c": 0, "battery_status": 0, "i2c_errors": 0},
            "uptime_s": uptime, "fw": "1.0",
        }
        fan = self.fan_duty()
        ctrl_warn = (1 << CTRL_WARNINGS.index("FAN_OVERRIDE")) if self.fan_override is not None else 0
        ctrl = {
            "online": True, "state": ctrl_state,
            "contactors": {"main_neg": closed, "precharge": False, "main_pos": closed, "charge_en": closed and i > 0},
            "fan": {"duty": fan, "rpm": fan * 18},
            "key_on": self.profile == "cycle", "estop_ok": True,
            "fault_bits": 0, "faults": [], "warning_bits": ctrl_warn, "warnings": names(ctrl_warn, CTRL_WARNINGS),
            "uptime_s": uptime, "fw": "1.0",
        }
        if tagged:
            bms["source"] = "simulated"
            ctrl["source"] = "simulated"
        return {
            "device": device, "ts": int(time.time() * 1000), "seq": seq,
            "bms": bms, "ctrl": ctrl,
            "gateway": {"fw": "sim-1.0" if tagged else "1.0.0", "rssi_dbm": random.randint(-62, -55),
                        "uptime_s": int(time.monotonic() - gw_started), "can_rx": seq * 98,
                        "can_e2e_errors": 0, "can_bus_off": 0, "buffered": 0},
        }

    def handle(self, cmd):
        """Apply a command; returns (ok, reason) like the firmware's on_message()."""
        name = cmd.get("cmd", "")
        if name == "clear_faults":
            self.bms_faults = 0
            return True, ""
        if name == "balancing":
            if not isinstance(cmd.get("enable"), bool):
                return False, '"enable" must be true or false'
            self.balancing_enabled = cmd["enable"]
            return True, ""
        if name == "stop":
            self.ctrl_state = "STOPPING"
            self.stop_until = time.monotonic() + 3
            return True, ""
        if name == "fan":
            duty = cmd.get("duty")
            if duty == "auto":
                self.fan_override = None
                return True, ""
            if isinstance(duty, int) and not isinstance(duty, bool) and 0 <= duty <= 100:
                self.fan_override = duty
                return True, ""
            return False, '"duty" must be 0..100 or "auto"'
        return False, "unknown command"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default=os.getenv("MQTT_HOST", "localhost"))
    p.add_argument("--port", type=int, default=int(os.getenv("MQTT_PORT", "1883")))
    p.add_argument("--username", default=os.getenv("MQTT_USERNAME", "gateway"))
    p.add_argument("--password", default=os.getenv("MQTT_PASSWORD", "gateway"))
    p.add_argument("--device", default=os.getenv("DEVICE_ID", "sim-01"))
    p.add_argument("--root", default=os.getenv("TOPIC_ROOT", "dbms"))
    p.add_argument("--token", default=os.getenv("GATEWAY_COMMAND_TOKEN", "change-me"))
    p.add_argument("--profile", choices=["rest", "cycle"], default=os.getenv("SIM_PROFILE", "cycle"))
    p.add_argument("--untagged", action="store_true", default=os.getenv("SIM_UNTAGGED") == "1",
                   help="omit the source tag (imitates the official firmware)")
    p.add_argument("--ntc-offsets", default=os.getenv("SIM_NTC_OFFSETS", ""),
                   help="per-NTC reading error in C, comma separated, e.g. -0.1,-7.8,-3.2,-17.1")
    p.add_argument("--fault-cycle", type=int, default=int(os.getenv("SIM_FAULT_CYCLE_S", "0")),
                   help="raise OVER_TEMP for 30 s every N seconds (0 = never)")
    args = p.parse_args()

    base = f"{args.root}/{args.device}"
    offsets = [float(x) for x in args.ntc_offsets.split(",") if x.strip()] if args.ntc_offsets else None
    if offsets is not None and len(offsets) != NTCS:
        p.error(f"--ntc-offsets needs {NTCS} values")
    pack = Pack(args.profile, offsets)
    lock = threading.Lock()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"gw-{args.device}")
    client.username_pw_set(args.username, args.password)
    client.will_set(f"{base}/status", "offline", qos=1, retain=True)

    def on_connect(c, _userdata, _flags, reason, _props):
        if reason.is_failure:
            print(f"[sim] connect refused: {reason}", flush=True)
            return
        c.publish(f"{base}/status", "online", qos=1, retain=True)
        c.subscribe(f"{base}/cmd", qos=1)
        print(f"[sim] connected to {args.host}:{args.port} as {args.device} ({args.profile})", flush=True)

    def on_message(c, _userdata, msg):
        try:
            cmd = json.loads(msg.payload)
        except ValueError:
            cmd, ok, reason = {}, False, "invalid JSON"
        else:
            if args.token and cmd.get("token") != args.token:
                ok, reason = False, "bad token"
            else:
                with lock:
                    ok, reason = pack.handle(cmd)
        ack = {"cmd": cmd.get("cmd", ""), "result": "sent" if ok else "rejected", "ts": int(time.time() * 1000)}
        if not ok:
            ack["reason"] = reason
        if "id" in cmd:
            ack["id"] = cmd["id"]
        c.publish(f"{base}/cmd/ack", json.dumps(ack), qos=1)
        print(f"[sim] cmd {ack['cmd']} -> {ack['result']} {reason if not ok else ''}", flush=True)

    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(1, 10)
    while True:
        try:
            client.connect(args.host, args.port, keepalive=30)
            break
        except OSError as ex:
            print(f"[sim] broker {args.host}:{args.port} not reachable ({ex}), retrying", flush=True)
            time.sleep(3)
    client.loop_start()

    if sys.stdin.isatty():
        def keys():
            for line in sys.stdin:
                k = line.strip().lower()
                with lock:
                    if k == "f":
                        pack.bms_faults ^= 1 << OVER_TEMP_BIT
                    elif k == "w":
                        pack.bms_warnings ^= 1 << CELL_HIGH_BIT
                    elif k == "q":
                        os._exit(0)
                print(f"[sim] faults=0x{pack.bms_faults:x} warnings=0x{pack.bms_warnings:x}", flush=True)
        threading.Thread(target=keys, daemon=True).start()
        print("[sim] keys: f = OVER_TEMP fault, w = CELL_HIGH warning, q = quit (then Enter)", flush=True)

    seq, gw_started, last = 0, time.monotonic(), time.monotonic()
    while True:
        time.sleep(1.0 - (time.monotonic() - last) % 1.0)
        now = time.monotonic()
        dt, last = now - last, now
        with lock:
            if args.fault_cycle > 0:
                in_fault = (now - gw_started) % args.fault_cycle < 30 and now - gw_started > 30
                pack.bms_faults = (pack.bms_faults | (1 << OVER_TEMP_BIT)) if in_fault \
                    else (pack.bms_faults & ~(1 << OVER_TEMP_BIT))
            pack.step(dt)
            seq += 1
            payload = pack.snapshot(args.device, seq, not args.untagged, gw_started)
        client.publish(f"{base}/telemetry", json.dumps(payload, separators=(",", ":")), qos=0)


if __name__ == "__main__":
    main()
