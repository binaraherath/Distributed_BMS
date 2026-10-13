/**
 * @file    main.cpp
 * @brief   Distributed BMS - Node 3 IoT Gateway (ESP32 DevKit V1 + ISO1042)
 *
 *  CAN (all frames) --> live snapshot --> JSON every second --> MQTT
 *  - While MQTT is down, snapshots go into a RAM ring buffer and are
 *    replayed (with their original time stamps) after reconnecting.
 *  - Fault and state changes of Node 1 / Node 2 are also sent as events.
 *  - Remote commands arrive on <root>/cmd and are forwarded to Node 2.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include "gateway_config.h"
#include "bms_model.h"
#include "can_link.h"
#include "net_link.h"

static BmsSnapshot s_ring[GW_BUFFER_SNAPSHOTS];
static uint16_t    s_ring_head, s_ring_count;
static char        s_json[GW_MQTT_BUFFER_BYTES];
static uint32_t    s_seq;
static uint32_t    t_publish, t_log;

static bool        s_have_prev;
static uint8_t     s_prev_bms_state, s_prev_ctrl_state, s_prev_ctrl_faults;
static uint32_t    s_prev_bms_faults;

/* ==========================================================================
 * Store and forward
 * ========================================================================== */
static void ring_push(const BmsSnapshot &s)
{
    s_ring[s_ring_head] = s;
    s_ring_head = (uint16_t)((s_ring_head + 1u) % GW_BUFFER_SNAPSHOTS);
    if (s_ring_count < GW_BUFFER_SNAPSHOTS) s_ring_count++;      /* else oldest is overwritten */
}

static bool ring_pop_oldest(BmsSnapshot &s)
{
    if (s_ring_count == 0) return false;
    uint16_t tail = (uint16_t)((s_ring_head + GW_BUFFER_SNAPSHOTS - s_ring_count) % GW_BUFFER_SNAPSHOTS);
    s = s_ring[tail];
    s_ring_count--;
    return true;
}

static bool publish_snapshot(const BmsSnapshot &s)
{
    size_t n = model_to_json(s, s_json, sizeof(s_json));
    return n > 0 && net_publish_telemetry(s_json, n);
}

/* ==========================================================================
 * Events
 * ========================================================================== */
template <typename F>
static void names(JsonArray a, uint32_t bits, uint8_t nbits, F fn)
{
    for (uint8_t b = 0; b < nbits; b++) if (bits & (1UL << b)) a.add(fn(b));
}

static void send_event(const char *node, const char *type, const char *from, const char *to,
                       uint32_t set_bits, uint32_t clr_bits, bool is_bms)
{
    JsonDocument ev;
    char buf[384];
    ev["device"] = GW_DEVICE_ID;
    ev["ts"]     = net_epoch_ms();
    ev["node"]   = node;
    ev["type"]   = type;
    if (from) { ev["from"] = from; ev["to"] = to; }
    if (set_bits || clr_bits) {
        if (is_bms) {
            names(ev["set"].to<JsonArray>(),     set_bits, 24, bms_fault_name);
            names(ev["cleared"].to<JsonArray>(), clr_bits, 24, bms_fault_name);
        } else {
            names(ev["set"].to<JsonArray>(),     set_bits, 8, ctrl_fault_name);
            names(ev["cleared"].to<JsonArray>(), clr_bits, 8, ctrl_fault_name);
        }
    }
    serializeJson(ev, buf, sizeof(buf));
    Serial.printf("[event] %s\n", buf);
    net_publish_event(buf);
}

static void detect_events(const BmsSnapshot &s)
{
    if (!s_have_prev) {
        s_have_prev = true;
    } else {
        if (s.bms_faults != s_prev_bms_faults)
            send_event("bms", "fault", nullptr, nullptr, s.bms_faults & ~s_prev_bms_faults,
                       s_prev_bms_faults & ~s.bms_faults, true);
        if (s.bms_state != s_prev_bms_state)
            send_event("bms", "state", bms_state_name(s_prev_bms_state), bms_state_name(s.bms_state), 0, 0, true);
        if (s.ctrl_faults != s_prev_ctrl_faults)
            send_event("ctrl", "fault", nullptr, nullptr, s.ctrl_faults & ~s_prev_ctrl_faults,
                       s_prev_ctrl_faults & ~s.ctrl_faults, false);
        if (s.ctrl_state != s_prev_ctrl_state)
            send_event("ctrl", "state", ctrl_state_name(s_prev_ctrl_state), ctrl_state_name(s.ctrl_state), 0, 0, false);
    }
    s_prev_bms_faults  = s.bms_faults;
    s_prev_bms_state   = s.bms_state;
    s_prev_ctrl_faults = s.ctrl_faults;
    s_prev_ctrl_state  = s.ctrl_state;
}

/* ==========================================================================
 * LED and serial summary
 * ========================================================================== */
static void update_led(uint32_t now)
{
    bool on;
    if (!net_wifi_ok())      on = (now / 100u) & 1u;     /* fast blink: no Wi-Fi  */
    else if (!net_mqtt_ok()) on = (now / 500u) & 1u;     /* slow blink: no broker */
    else                     on = true;                  /* steady: all good      */
    digitalWrite(GW_LED_PIN, on ? HIGH : LOW);
}

static void log_summary(const BmsSnapshot &s)
{
    Serial.printf("[gw] WiFi %s %d dBm | MQTT %s | BMS %s %s %.2fV %.2fA SoC %.1f%% | CTRL %s %s | "
                  "rx %lu e2e %lu busoff %lu | buffered %u\n",
                  net_wifi_ok() ? "ok" : "--", net_rssi(), net_mqtt_ok() ? "ok" : "--",
                  s.bms_online ? "online" : "OFFLINE", bms_state_name(s.bms_state),
                  s.pack_cv / 100.0, s.current_ca / 100.0, s.soc_x10 / 10.0,
                  s.ctrl_online ? "online" : "OFFLINE", ctrl_state_name(s.ctrl_state),
                  (unsigned long)s.can_rx_frames, (unsigned long)s.can_e2e_errors,
                  (unsigned long)s.can_bus_off, s_ring_count);
}

/* ==========================================================================
 * Arduino entry points
 * ========================================================================== */
void setup()
{
    Serial.begin(115200);
    pinMode(GW_LED_PIN, OUTPUT);
    delay(200);
    Serial.printf("\n=== Distributed BMS | Node 3 IoT Gateway | fw %s | %s ===\n", GW_FW_VERSION, GW_DEVICE_ID);

    if (can_link_begin()) Serial.println("[can] TWAI 500 kbit/s running (TX GPIO21, RX GPIO22)");
    else                  Serial.println("[can] TWAI start FAILED - check wiring / pins");

    net_begin();
}

void loop()
{
    uint32_t now = millis();

    net_loop();
    can_link_set_gateway_status((uint8_t)((net_wifi_ok() ? GW_FLAG_WIFI_OK : 0) |
                                          (net_mqtt_ok() ? GW_FLAG_MQTT_OK : 0) |
                                          (net_time_ok() ? GW_FLAG_TIME_OK : 0)),
                                net_rssi());

    if (now - t_publish >= GW_PUBLISH_INTERVAL_MS) {
        t_publish = now;
        BmsSnapshot s;
        can_link_snapshot(s);
        s.ts_ms       = net_epoch_ms();
        s.seq         = ++s_seq;
        s.bms_online  = model_bms_online(s, now);
        s.ctrl_online = model_ctrl_online(s, now);
        s.wifi_rssi   = net_rssi();
        s.gw_uptime_s = now / 1000u;
        s.buffered    = s_ring_count;
        s.replay      = false;

        detect_events(s);
        if (!publish_snapshot(s)) ring_push(s);

        if (now - t_log >= 5000u) { t_log = now; log_summary(s); }
    }

    /* Replay buffered snapshots a few at a time once the broker is back */
    for (uint8_t k = 0; k < GW_REPLAY_PER_LOOP && net_mqtt_ok() && s_ring_count > 0; k++) {
        BmsSnapshot old;
        if (!ring_pop_oldest(old)) break;
        old.replay = true;
        if (!publish_snapshot(old)) { s_ring_count++; break; }   /* slot untouched: undo the pop */
    }

    update_led(now);
    delay(2);
}
