/**
 * @file    net_link.h
 * @brief   Wi-Fi, NTP time, MQTT and OTA for the gateway.
 *
 *  Topics (root = GW_TOPIC_ROOT/GW_DEVICE_ID, e.g. dbms/dbms-01):
 *    <root>/telemetry   JSON snapshot every GW_PUBLISH_INTERVAL_MS
 *    <root>/event       fault / state changes
 *    <root>/status      "online" / "offline" (retained, last will)
 *    <root>/cmd         remote commands in   (JSON, needs GW_CMD_TOKEN)
 *    <root>/cmd/ack     command result out
 */
#pragma once

#include <Arduino.h>

void     net_begin(void);
void     net_loop(void);            /* call every loop(): reconnects, OTA */

bool     net_wifi_ok(void);
bool     net_mqtt_ok(void);
bool     net_time_ok(void);
int8_t   net_rssi(void);
uint64_t net_epoch_ms(void);        /* UTC, 0 until NTP has synced */

bool     net_publish_telemetry(const char *json, size_t len);
bool     net_publish_event(const char *json);
