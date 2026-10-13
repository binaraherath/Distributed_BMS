/**
 * @file    secrets.example.h
 * @brief   Copy this file to secrets.h and fill in your own values.
 *          secrets.h is git-ignored so passwords never reach the repository.
 */
#pragma once

#define WIFI_SSID           "your-wifi-name"
#define WIFI_PASSWORD       "your-wifi-password"

/* Local Mosquitto (e.g. 192.168.1.10:1883) or a cloud broker.
 * For a TLS broker (e.g. HiveMQ Cloud on 8883) set MQTT_USE_TLS to 1 and
 * paste the broker root CA below (empty CA = encrypted but not verified). */
#define MQTT_HOST           "192.168.1.10"
#define MQTT_PORT           1883
#define MQTT_USER           ""
#define MQTT_PASSWORD       ""
#define MQTT_USE_TLS        0
#define MQTT_ROOT_CA        ""

/* Remote commands must carry this token: {"cmd":"stop","token":"..."}.
 * Leave empty only on a private test network. */
#define GW_CMD_TOKEN        "change-me"

/* ArduinoOTA password for wireless firmware updates */
#define OTA_PASSWORD        "change-me"
