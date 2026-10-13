/**
 * @file    gateway_config.h
 * @brief   Node 3 (IoT Gateway) settings that are not secret.
 *          Wi-Fi / MQTT credentials live in secrets.h (not in git).
 */
#pragma once

#define GW_FW_VERSION               "1.0.0"
#define GW_FW_MAJOR                 1
#define GW_FW_MINOR                 0

/* Identity: topics are <GW_TOPIC_ROOT>/<GW_DEVICE_ID>/... */
#define GW_DEVICE_ID                "dbms-01"
#define GW_TOPIC_ROOT               "dbms"

/* Hardware: ESP32 DevKit V1 + custom ISO1042 isolated CAN module */
#define GW_CAN_TX_PIN               21
#define GW_CAN_RX_PIN               22
#define GW_LED_PIN                  2       /* on-board blue LED       */

/* Telemetry */
#define GW_PUBLISH_INTERVAL_MS      1000    /* one JSON snapshot / s   */
#define GW_NODE_STALE_MS            1000    /* node shown offline after */
#define GW_BUFFER_SNAPSHOTS         300     /* 5 min kept while offline */
#define GW_REPLAY_PER_LOOP          5       /* buffered msgs sent / loop */
#define GW_MQTT_BUFFER_BYTES        2048
#define GW_MQTT_RETRY_MS            5000
#define GW_WIFI_RETRY_MS            10000

/* Time (stored as UTC epoch ms; Sri Lanka is UTC+05:30 for display) */
#define GW_NTP_SERVER_1             "pool.ntp.org"
#define GW_NTP_SERVER_2             "time.google.com"
