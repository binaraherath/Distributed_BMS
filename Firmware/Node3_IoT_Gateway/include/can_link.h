/**
 * @file    can_link.h
 * @brief   ESP32 TWAI (CAN) link. A dedicated FreeRTOS task receives every
 *          frame, decodes it into the shared snapshot, recovers from bus-off
 *          and sends the gateway heartbeat and queued commands. Wi-Fi or
 *          MQTT reconnects in loop() can therefore never stall CAN reception.
 */
#pragma once

#include <Arduino.h>
#include "bms_model.h"

bool can_link_begin(void);

/** Thread-safe copy of the live snapshot. */
void can_link_snapshot(BmsSnapshot &out);

/** Queue a GW_CMD frame for Node 2 (thread-safe). */
bool can_link_send_command(uint8_t cmd, uint8_t arg);

/** Status bits for the heartbeat frame (GW_FLAG_*) and RSSI. */
void can_link_set_gateway_status(uint8_t flags, int8_t rssi_dbm);
