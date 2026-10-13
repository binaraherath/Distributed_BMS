/**
 * @file    can_link.cpp
 * @brief   TWAI driver task (see can_link.h).
 */
#include "can_link.h"
#include "gateway_config.h"
#include "driver/twai.h"

struct PendingCmd { uint8_t cmd; uint8_t arg; };

static BmsSnapshot     s_live;
static portMUX_TYPE    s_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t   s_cmd_queue;
static volatile uint8_t s_gw_flags;
static volatile int8_t  s_gw_rssi;
static uint8_t         s_hb_counter;
static uint8_t         s_cmd_counter;

static bool transmit(uint16_t id, const uint8_t *d)
{
    twai_message_t m = {};
    m.identifier = id;
    m.data_length_code = 8;
    memcpy(m.data, d, 8);
    return twai_transmit(&m, pdMS_TO_TICKS(5)) == ESP_OK;
}

static void send_heartbeat(void)
{
    uint8_t d[8] = {0};
    d[0] = s_gw_flags;
    d[1] = (uint8_t)s_gw_rssi;
    can_put_u32(d, 2, (uint32_t)(millis() / 1000u));
    can_e2e_protect(CAN_ID_GW_HEARTBEAT, d, s_hb_counter++);
    transmit(CAN_ID_GW_HEARTBEAT, d);
}

static void can_task(void *)
{
    uint32_t last_hb = 0;

    for (;;) {
        twai_message_t m;
        if (twai_receive(&m, pdMS_TO_TICKS(10)) == ESP_OK) {
            if (!m.extd && !m.rtr && m.data_length_code == 8) {
                uint32_t now = millis();
                portENTER_CRITICAL(&s_mux);
                s_live.can_rx_frames++;
                if (!model_apply_frame(s_live, (uint16_t)m.identifier, m.data, now)) s_live.can_e2e_errors++;
                portEXIT_CRITICAL(&s_mux);
            }
        }

        uint32_t alerts = 0;
        if (twai_read_alerts(&alerts, 0) == ESP_OK) {
            if (alerts & TWAI_ALERT_BUS_OFF) {
                portENTER_CRITICAL(&s_mux);
                s_live.can_bus_off++;
                portEXIT_CRITICAL(&s_mux);
                twai_initiate_recovery();
            }
            if (alerts & TWAI_ALERT_BUS_RECOVERED) twai_start();
        }

        PendingCmd c;
        while (xQueueReceive(s_cmd_queue, &c, 0) == pdTRUE) {
            uint8_t d[8] = {0};
            d[0] = c.cmd;
            d[1] = c.arg;
            can_e2e_protect(CAN_ID_GW_CMD, d, ++s_cmd_counter);
            transmit(CAN_ID_GW_CMD, d);
        }

        if (millis() - last_hb >= CAN_PERIOD_INFO_MS) {
            last_hb = millis();
            send_heartbeat();
        }
    }
}

bool can_link_begin(void)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)GW_CAN_TX_PIN,
                                                          (gpio_num_t)GW_CAN_RX_PIN,
                                                          TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();   /* gateway sees everything */

    g.rx_queue_len   = 64;
    g.tx_queue_len   = 8;
    g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                       TWAI_ALERT_ERR_PASS | TWAI_ALERT_RX_QUEUE_FULL;

    memset(&s_live, 0, sizeof(s_live));
    s_cmd_queue = xQueueCreate(8, sizeof(PendingCmd));

    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    if (twai_start() != ESP_OK) return false;

    /* Core 1 with loop(); higher priority so decoding always wins */
    xTaskCreatePinnedToCore(can_task, "can", 4096, nullptr, 5, nullptr, 1);
    return true;
}

void can_link_snapshot(BmsSnapshot &out)
{
    portENTER_CRITICAL(&s_mux);
    out = s_live;
    portEXIT_CRITICAL(&s_mux);
}

bool can_link_send_command(uint8_t cmd, uint8_t arg)
{
    PendingCmd c = { cmd, arg };
    return s_cmd_queue != nullptr && xQueueSend(s_cmd_queue, &c, 0) == pdTRUE;
}

void can_link_set_gateway_status(uint8_t flags, int8_t rssi_dbm)
{
    s_gw_flags = flags;
    s_gw_rssi  = rssi_dbm;
}
