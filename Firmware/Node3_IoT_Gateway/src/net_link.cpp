/**
 * @file    net_link.cpp
 * @brief   Wi-Fi / NTP / MQTT / OTA (see net_link.h).
 */
#include "net_link.h"
#include "gateway_config.h"
#include "can_link.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found - building with the placeholders in secrets.example.h"
#include "secrets.example.h"
#endif

#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
static WiFiClientSecure s_net;
#else
static WiFiClient s_net;
#endif

static PubSubClient s_mqtt(s_net);
static char     s_client_id[40];
static char     s_t_telemetry[64], s_t_event[64], s_t_status[64], s_t_cmd[64], s_t_ack[64];
static uint32_t s_last_wifi_try;
static uint32_t s_last_mqtt_try;
static bool     s_services_started;

/* ==========================================================================
 * Remote commands
 * ========================================================================== */
static void publish_ack(const char *cmd, bool ok, const char *reason)
{
    JsonDocument ack;
    char buf[192];
    ack["cmd"]    = cmd;
    ack["result"] = ok ? "sent" : "rejected";
    if (!ok) ack["reason"] = reason;
    ack["ts"]     = net_epoch_ms();
    serializeJson(ack, buf, sizeof(buf));
    s_mqtt.publish(s_t_ack, buf);
}

static void on_message(char *topic, byte *payload, unsigned int len)
{
    (void)topic;
    JsonDocument doc;
    const char *reason = "";
    bool ok = false;

    /* const input makes ArduinoJson copy the strings: PubSubClient reuses
     * its buffer for the acknowledgement we publish below. */
    if (deserializeJson(doc, (const char *)payload, len)) {
        publish_ack("", false, "invalid JSON");
        return;
    }
    const char *cmd = doc["cmd"] | "";

    if (strlen(GW_CMD_TOKEN) > 0 && strcmp(doc["token"] | "", GW_CMD_TOKEN) != 0) {
        reason = "bad token";
    } else if (!strcmp(cmd, "clear_faults")) {
        ok = can_link_send_command(GW_CMD_CLEAR_FAULTS, 0);
    } else if (!strcmp(cmd, "balancing")) {
        if (doc["enable"].is<bool>()) ok = can_link_send_command(GW_CMD_SET_BALANCING, doc["enable"].as<bool>() ? 1 : 0);
        else reason = "\"enable\" must be true or false";
    } else if (!strcmp(cmd, "stop")) {
        ok = can_link_send_command(GW_CMD_REQUEST_STOP, 0);
    } else if (!strcmp(cmd, "fan")) {
        if (doc["duty"].is<const char *>() && !strcmp(doc["duty"], "auto")) {
            ok = can_link_send_command(GW_CMD_FAN_OVERRIDE, 0xFF);
        } else if (doc["duty"].is<int>() && doc["duty"].as<int>() >= 0 && doc["duty"].as<int>() <= 100) {
            ok = can_link_send_command(GW_CMD_FAN_OVERRIDE, (uint8_t)doc["duty"].as<int>());
        } else {
            reason = "\"duty\" must be 0..100 or \"auto\"";
        }
    } else {
        reason = "unknown command";
    }
    if (!ok && reason[0] == '\0') reason = "CAN queue full";

    Serial.printf("[cmd] %s -> %s %s\n", cmd, ok ? "sent" : "rejected", reason);
    publish_ack(cmd, ok, reason);
}

/* ==========================================================================
 * Connection handling
 * ========================================================================== */
static void start_services(void)
{
    /* NTP (UTC). Sri Lanka local time is UTC+05:30 - convert on the dashboard. */
    configTime(0, 0, GW_NTP_SERVER_1, GW_NTP_SERVER_2);

    ArduinoOTA.setHostname(GW_DEVICE_ID);
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    s_services_started = true;
    Serial.printf("[net] Wi-Fi up %s, RSSI %d dBm, OTA as %s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), GW_DEVICE_ID);
}

static void connect_mqtt(void)
{
    Serial.printf("[mqtt] connecting to %s:%d ...\n", MQTT_HOST, MQTT_PORT);
    bool ok = s_mqtt.connect(s_client_id,
                             strlen(MQTT_USER) ? MQTT_USER : nullptr,
                             strlen(MQTT_PASSWORD) ? MQTT_PASSWORD : nullptr,
                             s_t_status, 1, true, "offline");
    if (!ok) {
        Serial.printf("[mqtt] failed, state %d\n", s_mqtt.state());
        return;
    }
    s_mqtt.publish(s_t_status, "online", true);
    s_mqtt.subscribe(s_t_cmd, 1);
    Serial.printf("[mqtt] connected, publishing to %s\n", s_t_telemetry);
}

void net_begin(void)
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s_client_id, sizeof(s_client_id), "%s-%02X%02X%02X", GW_DEVICE_ID, mac[3], mac[4], mac[5]);
    snprintf(s_t_telemetry, sizeof(s_t_telemetry), "%s/%s/telemetry", GW_TOPIC_ROOT, GW_DEVICE_ID);
    snprintf(s_t_event,     sizeof(s_t_event),     "%s/%s/event",     GW_TOPIC_ROOT, GW_DEVICE_ID);
    snprintf(s_t_status,    sizeof(s_t_status),    "%s/%s/status",    GW_TOPIC_ROOT, GW_DEVICE_ID);
    snprintf(s_t_cmd,       sizeof(s_t_cmd),       "%s/%s/cmd",       GW_TOPIC_ROOT, GW_DEVICE_ID);
    snprintf(s_t_ack,       sizeof(s_t_ack),       "%s/%s/cmd/ack",   GW_TOPIC_ROOT, GW_DEVICE_ID);

#if MQTT_USE_TLS
    if (strlen(MQTT_ROOT_CA) > 0) s_net.setCACert(MQTT_ROOT_CA);
    else s_net.setInsecure();
#endif
    s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    s_mqtt.setCallback(on_message);
    s_mqtt.setBufferSize(GW_MQTT_BUFFER_BYTES);
    s_mqtt.setKeepAlive(15);
    s_mqtt.setSocketTimeout(3);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(GW_DEVICE_ID);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_last_wifi_try = millis();
    Serial.printf("[net] joining Wi-Fi \"%s\"\n", WIFI_SSID);
}

void net_loop(void)
{
    uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        if (now - s_last_wifi_try >= GW_WIFI_RETRY_MS) {
            s_last_wifi_try = now;
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        return;
    }

    if (!s_services_started) start_services();
    ArduinoOTA.handle();

    if (!s_mqtt.connected()) {
        if (now - s_last_mqtt_try >= GW_MQTT_RETRY_MS || s_last_mqtt_try == 0) {
            s_last_mqtt_try = now;
            connect_mqtt();
        }
    } else {
        s_mqtt.loop();
    }
}

bool   net_wifi_ok(void) { return WiFi.status() == WL_CONNECTED; }
bool   net_mqtt_ok(void) { return net_wifi_ok() && s_mqtt.connected(); }
int8_t net_rssi(void)    { return net_wifi_ok() ? (int8_t)WiFi.RSSI() : 0; }
bool   net_time_ok(void) { return time(nullptr) > 1700000000; }

uint64_t net_epoch_ms(void)
{
    struct timeval tv;
    if (!net_time_ok()) return 0;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

bool net_publish_telemetry(const char *json, size_t len)
{
    return net_mqtt_ok() && s_mqtt.publish(s_t_telemetry, (const uint8_t *)json, len, false);
}

bool net_publish_event(const char *json)
{
    return net_mqtt_ok() && s_mqtt.publish(s_t_event, json);
}
