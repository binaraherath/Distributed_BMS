/**
 * @file    bms_model.h
 * @brief   Decoded picture of the whole pack, built from CAN frames, and its
 *          JSON form for MQTT. Frame layouts: common/bms_can_protocol.h
 */
#pragma once

#include <Arduino.h>
#include "bms_can_protocol.h"

struct BmsSnapshot {
    uint64_t ts_ms;                 /* UTC epoch ms, 0 if time not synced  */
    uint32_t seq;
    bool     bms_online;            /* set when the snapshot is captured   */
    bool     ctrl_online;

    /* ---- Node 1: Master Monitoring Unit ---- */
    uint32_t bms_last_ms;           /* millis() of last valid BMS_STATUS   */
    uint8_t  bms_state;
    uint32_t bms_faults;
    uint16_t bms_warnings;
    uint16_t pack_cv;               /* 10 mV                               */
    int16_t  current_ca;            /* 10 mA                               */
    uint16_t soc_x10;
    uint16_t chg_limit_da, dsg_limit_da;   /* 0.1 A                       */
    uint8_t  limit_flags;
    int8_t   tmax_c;
    uint16_t cell_mv[BMS_CELL_COUNT];
    uint16_t cell_min_mv, cell_max_mv, cell_avg_mv;
    uint8_t  cell_min_no, cell_max_no;
    int16_t  ntc_dc[BMS_NTC_COUNT];
    uint16_t bal_mask, bal_delta_mv, bal_minutes;
    uint8_t  bal_flags, bal_count;
    uint16_t soh_x10, remaining_cah, usable_cah, discharged_dah;
    uint32_t bms_uptime_s;
    uint8_t  bms_fw_major, bms_fw_minor, bms_reset_cause;
    int8_t   bq_die_c;
    uint8_t  bq_ssa, bq_ssb, bq_ssc, bq_i2c_errors;
    uint16_t bq_battery_status, bq_alarm;

    /* ---- Node 2: System Controller ---- */
    uint32_t ctrl_last_ms;
    uint8_t  ctrl_state, ctrl_outputs, ctrl_fan_duty, ctrl_inputs, ctrl_faults, ctrl_warnings;
    uint32_t ctrl_uptime_s;
    uint8_t  ctrl_fw_major, ctrl_fw_minor;
    uint16_t fan_rpm;

    /* ---- Gateway / bus health ---- */
    uint32_t can_rx_frames;
    uint32_t can_e2e_errors;
    uint32_t can_bus_off;
    int8_t   wifi_rssi;
    uint32_t gw_uptime_s;
    uint16_t buffered;              /* snapshots waiting to be sent        */
    bool     replay;                /* this one comes from the buffer      */
};

/** Apply one received frame. Returns false if an E2E check failed. */
bool model_apply_frame(BmsSnapshot &s, uint16_t id, const uint8_t *d, uint32_t now_ms);

/** Serialise a snapshot to JSON. Returns the length (0 if it did not fit). */
size_t model_to_json(const BmsSnapshot &s, char *out, size_t cap);

bool model_bms_online(const BmsSnapshot &s, uint32_t now_ms);
bool model_ctrl_online(const BmsSnapshot &s, uint32_t now_ms);

const char *bms_state_name(uint8_t s);
const char *ctrl_state_name(uint8_t s);
const char *bms_fault_name(uint8_t bit);       /* bit 0..23 */
const char *bms_warning_name(uint8_t bit);     /* bit 0..15 */
const char *ctrl_fault_name(uint8_t bit);      /* bit 0..7  */
const char *ctrl_warning_name(uint8_t bit);    /* bit 0..7  */
