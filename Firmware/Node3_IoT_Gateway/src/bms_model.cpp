/**
 * @file    bms_model.cpp
 * @brief   CAN frame decoding and JSON serialisation (see bms_model.h).
 */
#include "bms_model.h"
#include "gateway_config.h"
#include <ArduinoJson.h>
#include <math.h>

/* ==========================================================================
 * Names (index = bit number in bms_can_protocol.h)
 * ========================================================================== */
static const char *const k_bms_fault[] = {
    "BQ_COMM", "BQ_CONFIG", "CELL_OV", "CELL_UV", "OVER_TEMP", "UNDER_TEMP",
    "OC_CHARGE", "OC_DISCHARGE", "SHORT_CIRCUIT", "IMBALANCE", "CELL_SENSE",
    "NTC_SENSE", "STACK_MISMATCH", "BQ_PERMANENT_FAIL"
};
static const char *const k_bms_warn[] = {
    "CELL_HIGH", "CELL_LOW", "TEMP_HIGH", "TEMP_LOW", "IMBALANCE", "CURRENT_HIGH",
    "CTRL_LOST", "BALANCING", "ALERT_PIN", "CAN_ERROR", "WDG_RESET", "SOC_LOW",
    "BQ_ALERT", "I2C_RETRY"
};
static const char *const k_ctrl_fault[] = {
    "BMS_TIMEOUT", "BMS_FAULT", "BMS_DATA", "PRECHARGE", "ESTOP", "LIMIT_VIOLATION", "CAN_BUSOFF"
};
static const char *const k_ctrl_warn[] = {
    "FAN_FAIL", "GW_OFFLINE", "CHARGE_BLOCKED", "DERATED", "WDG_RESET", "REMOTE_STOP", "FAN_OVERRIDE"
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

const char *bms_state_name(uint8_t s)
{
    static const char *const n[] = { "INIT", "IDLE", "CHARGING", "DISCHARGING", "FAULT" };
    return s < COUNT(n) ? n[s] : "UNKNOWN";
}
const char *ctrl_state_name(uint8_t s)
{
    static const char *const n[] = { "INIT", "STANDBY", "PRECHARGE", "RUN", "STOPPING", "FAULT" };
    return s < COUNT(n) ? n[s] : "UNKNOWN";
}
const char *bms_fault_name(uint8_t b)    { return b < COUNT(k_bms_fault)  ? k_bms_fault[b]  : "RESERVED"; }
const char *bms_warning_name(uint8_t b)  { return b < COUNT(k_bms_warn)   ? k_bms_warn[b]   : "RESERVED"; }
const char *ctrl_fault_name(uint8_t b)   { return b < COUNT(k_ctrl_fault) ? k_ctrl_fault[b] : "RESERVED"; }
const char *ctrl_warning_name(uint8_t b) { return b < COUNT(k_ctrl_warn)  ? k_ctrl_warn[b]  : "RESERVED"; }

bool model_bms_online(const BmsSnapshot &s, uint32_t now_ms)
{
    return s.bms_last_ms != 0 && (now_ms - s.bms_last_ms) < GW_NODE_STALE_MS;
}
bool model_ctrl_online(const BmsSnapshot &s, uint32_t now_ms)
{
    return s.ctrl_last_ms != 0 && (now_ms - s.ctrl_last_ms) < GW_NODE_STALE_MS;
}

/* ==========================================================================
 * Decoding
 * ========================================================================== */
bool model_apply_frame(BmsSnapshot &s, uint16_t id, const uint8_t *d, uint32_t now_ms)
{
    switch (id) {
    case CAN_ID_BMS_STATUS:
        if (!can_e2e_check(id, d)) return false;
        s.bms_state    = d[0];
        s.bms_faults   = can_get_u24(d, 1);
        s.bms_warnings = can_get_u16(d, 4);
        s.bms_last_ms  = now_ms;
        break;
    case CAN_ID_BMS_PACK:
        if (!can_e2e_check(id, d)) return false;
        s.pack_cv    = can_get_u16(d, 0);
        s.current_ca = can_get_i16(d, 2);
        s.soc_x10    = can_get_u16(d, 4);
        break;
    case CAN_ID_BMS_LIMITS:
        if (!can_e2e_check(id, d)) return false;
        s.chg_limit_da = can_get_u16(d, 0);
        s.dsg_limit_da = can_get_u16(d, 2);
        s.limit_flags  = d[4];
        s.tmax_c       = (int8_t)d[5];
        break;
    case CAN_ID_BMS_CELL_STATS:
        s.cell_min_mv = can_get_u16(d, 0);
        s.cell_max_mv = can_get_u16(d, 2);
        s.cell_avg_mv = can_get_u16(d, 4);
        s.cell_min_no = d[6];
        s.cell_max_no = d[7];
        break;
    case CAN_ID_BMS_CELLS_1_4:
    case CAN_ID_BMS_CELLS_5_8:
    case CAN_ID_BMS_CELLS_9_12:
    case CAN_ID_BMS_CELLS_13_16: {
        uint8_t base = (uint8_t)((id - CAN_ID_BMS_CELLS_1_4) * 4u);
        for (uint8_t i = 0; i < 4u; i++) s.cell_mv[base + i] = can_get_u16(d, (uint8_t)(2u * i));
        break;
    }
    case CAN_ID_BMS_TEMPS:
        for (uint8_t i = 0; i < BMS_NTC_COUNT; i++) s.ntc_dc[i] = can_get_i16(d, (uint8_t)(2u * i));
        break;
    case CAN_ID_BMS_BALANCE:
        s.bal_mask     = can_get_u16(d, 0);
        s.bal_delta_mv = can_get_u16(d, 2);
        s.bal_flags    = d[4];
        s.bal_count    = d[5];
        s.bal_minutes  = can_get_u16(d, 6);
        break;
    case CAN_ID_BMS_ENERGY:
        s.soh_x10        = can_get_u16(d, 0);
        s.remaining_cah  = can_get_u16(d, 2);
        s.usable_cah     = can_get_u16(d, 4);
        s.discharged_dah = can_get_u16(d, 6);
        break;
    case CAN_ID_BMS_INFO:
        s.bms_uptime_s    = can_get_u32(d, 0);
        s.bms_fw_major    = d[4];
        s.bms_fw_minor    = d[5];
        s.bms_reset_cause = d[6];
        s.bq_die_c        = (int8_t)d[7];
        break;
    case CAN_ID_BMS_BQ_STATUS:
        s.bq_ssa = d[0]; s.bq_ssb = d[1]; s.bq_ssc = d[2];
        s.bq_battery_status = can_get_u16(d, 3);
        s.bq_alarm          = can_get_u16(d, 5);
        s.bq_i2c_errors     = d[7];
        break;
    case CAN_ID_CTRL_STATUS:
        if (!can_e2e_check(id, d)) return false;
        s.ctrl_state    = d[0];
        s.ctrl_outputs  = d[1];
        s.ctrl_fan_duty = d[2];
        s.ctrl_inputs   = d[3];
        s.ctrl_faults   = d[4];
        s.ctrl_warnings = d[5];
        s.ctrl_last_ms  = now_ms;
        break;
    case CAN_ID_CTRL_INFO:
        s.ctrl_uptime_s = can_get_u32(d, 0);
        s.ctrl_fw_major = d[4];
        s.ctrl_fw_minor = d[5];
        s.fan_rpm       = can_get_u16(d, 6);
        break;
    default:
        break;      /* our own frames and unknown IDs */
    }
    return true;
}

/* ==========================================================================
 * JSON
 * ========================================================================== */
static double r(double v, double scale) { return round(v * scale) / scale; }

template <typename F>
static void add_names(JsonArray arr, uint32_t bits, uint8_t nbits, F name)
{
    for (uint8_t b = 0; b < nbits; b++) if (bits & (1UL << b)) arr.add(name(b));
}

size_t model_to_json(const BmsSnapshot &s, char *out, size_t cap)
{
    JsonDocument doc;

    doc["device"] = GW_DEVICE_ID;
    doc["ts"]     = s.ts_ms;
    doc["seq"]    = s.seq;
    if (s.replay) doc["replay"] = true;

    /* ---- Node 1 ---- */
    JsonObject bms = doc["bms"].to<JsonObject>();
    bms["online"]        = s.bms_online;
    bms["state"]         = bms_state_name(s.bms_state);
    double v = s.pack_cv / 100.0, i = s.current_ca / 100.0;
    bms["pack_v"]        = r(v, 100);
    bms["current_a"]     = r(i, 100);
    bms["power_w"]       = r(v * i, 10);
    bms["soc"]           = r(s.soc_x10 / 10.0, 10);
    bms["soh"]           = r(s.soh_x10 / 10.0, 10);
    bms["remaining_ah"]  = r(s.remaining_cah / 100.0, 100);
    bms["capacity_ah"]   = r(s.usable_cah / 100.0, 100);
    bms["discharged_ah"] = r(s.discharged_dah / 10.0, 10);

    JsonArray cells = bms["cells_mv"].to<JsonArray>();
    for (uint8_t c = 0; c < BMS_CELL_COUNT; c++) cells.add(s.cell_mv[c]);
    bms["cell_min_mv"]   = s.cell_min_mv;
    bms["cell_max_mv"]   = s.cell_max_mv;
    bms["cell_avg_mv"]   = s.cell_avg_mv;
    bms["cell_delta_mv"] = (int)s.cell_max_mv - (int)s.cell_min_mv;
    bms["cell_min_no"]   = s.cell_min_no;
    bms["cell_max_no"]   = s.cell_max_no;

    JsonArray temps = bms["temps_c"].to<JsonArray>();
    for (uint8_t n = 0; n < BMS_NTC_COUNT; n++) temps.add(r(s.ntc_dc[n] / 10.0, 10));
    bms["temp_max_c"] = s.tmax_c;
    bms["bq_die_c"]   = s.bq_die_c;

    JsonObject lim = bms["limits"].to<JsonObject>();
    lim["charge_a"]          = r(s.chg_limit_da / 10.0, 10);
    lim["discharge_a"]       = r(s.dsg_limit_da / 10.0, 10);
    lim["charge_allowed"]    = (s.limit_flags & BMS_LIM_CHARGE_ALLOWED) != 0;
    lim["discharge_allowed"] = (s.limit_flags & BMS_LIM_DISCHARGE_ALLOWED) != 0;
    lim["full"]              = (s.limit_flags & BMS_LIM_PACK_FULL) != 0;
    lim["empty"]             = (s.limit_flags & BMS_LIM_PACK_EMPTY) != 0;

    JsonObject bal = bms["balancing"].to<JsonObject>();
    bal["enabled"] = (s.bal_flags & BMS_BAL_FLAG_ENABLED) != 0;
    bal["active"]  = s.bal_mask != 0;
    bal["mask"]    = s.bal_mask;
    JsonArray bc   = bal["cells"].to<JsonArray>();
    for (uint8_t c = 0; c < BMS_CELL_COUNT; c++) if (s.bal_mask & (1u << c)) bc.add(c + 1);
    bal["minutes"] = s.bal_minutes;

    bms["fault_bits"]   = s.bms_faults;
    add_names(bms["faults"].to<JsonArray>(), s.bms_faults, 24, bms_fault_name);
    bms["warning_bits"] = s.bms_warnings;
    add_names(bms["warnings"].to<JsonArray>(), s.bms_warnings, 16, bms_warning_name);

    JsonObject bq = bms["bq76952"].to<JsonObject>();
    bq["safety_a"]       = s.bq_ssa;
    bq["safety_b"]       = s.bq_ssb;
    bq["safety_c"]       = s.bq_ssc;
    bq["battery_status"] = s.bq_battery_status;
    bq["i2c_errors"]     = s.bq_i2c_errors;

    bms["uptime_s"] = s.bms_uptime_s;
    char fw[12];
    snprintf(fw, sizeof(fw), "%u.%u", s.bms_fw_major, s.bms_fw_minor);
    bms["fw"] = fw;

    /* ---- Node 2 ---- */
    JsonObject ctrl = doc["ctrl"].to<JsonObject>();
    ctrl["online"] = s.ctrl_online;
    ctrl["state"]  = ctrl_state_name(s.ctrl_state);
    JsonObject k = ctrl["contactors"].to<JsonObject>();
    k["main_neg"]  = (s.ctrl_outputs & CTRL_OUT_MAIN_NEG) != 0;
    k["precharge"] = (s.ctrl_outputs & CTRL_OUT_PRECHARGE) != 0;
    k["main_pos"]  = (s.ctrl_outputs & CTRL_OUT_MAIN_POS) != 0;
    k["charge_en"] = (s.ctrl_outputs & CTRL_OUT_CHARGE_EN) != 0;
    JsonObject fan = ctrl["fan"].to<JsonObject>();
    fan["duty"] = s.ctrl_fan_duty;
    fan["rpm"]  = s.fan_rpm;
    ctrl["key_on"]   = (s.ctrl_inputs & CTRL_IN_KEY_ON) != 0;
    ctrl["estop_ok"] = (s.ctrl_inputs & CTRL_IN_ESTOP_OK) != 0;
    ctrl["fault_bits"] = s.ctrl_faults;
    add_names(ctrl["faults"].to<JsonArray>(), s.ctrl_faults, 8, ctrl_fault_name);
    ctrl["warning_bits"] = s.ctrl_warnings;
    add_names(ctrl["warnings"].to<JsonArray>(), s.ctrl_warnings, 8, ctrl_warning_name);
    ctrl["uptime_s"] = s.ctrl_uptime_s;
    snprintf(fw, sizeof(fw), "%u.%u", s.ctrl_fw_major, s.ctrl_fw_minor);
    ctrl["fw"] = fw;

    /* ---- Gateway ---- */
    JsonObject gw = doc["gateway"].to<JsonObject>();
    gw["fw"]         = GW_FW_VERSION;
    gw["rssi_dbm"]   = s.wifi_rssi;
    gw["uptime_s"]   = s.gw_uptime_s;
    gw["can_rx"]     = s.can_rx_frames;
    gw["can_e2e_errors"] = s.can_e2e_errors;
    gw["can_bus_off"]    = s.can_bus_off;
    gw["buffered"]   = s.buffered;

    if (measureJson(doc) + 1 > cap) return 0;
    return serializeJson(doc, out, cap);
}
