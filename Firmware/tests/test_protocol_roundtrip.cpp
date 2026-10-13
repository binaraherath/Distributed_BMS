/* Cross-node protocol test: the real Node 1 encoder, Node 2 decoder/encoder
 * and gateway decoder/JSON serialiser exchange frames on a simulated bus. */
#include <cstdio>
#include <cstring>
#include <cmath>
#include "bms_model.h"
#include <ArduinoJson.h>
extern "C" {
#include "bus.h"
#include "bms_app.h"
#include "bms_can.h"
#include "ctrl_app.h"
#include "ctrl_can.h"
extern int n1_clear_calls, n1_hb_calls, n1_bal_calls; extern bool n1_bal_last;
extern int n2_stop_calls, n2_clear_calls, n2_bal_calls, n2_fan_calls; extern int16_t n2_fan_last; extern bool n2_bal_last;
void Ctrl_RxCb(CAN_HandleTypeDef *h);                 /* Node 2's renamed RX callback */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h);   /* Node 1's */
}
static int fails, passes;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } else passes++; } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-6)

static CAN_TypeDef regs1, regs2;
static CAN_HandleTypeDef can1 = { &regs1 }, can2 = { &regs2 };

static void deliver_to_node2(int from, int to) { g_rx_n = 0; g_rx_i = 0; for (int k = from; k < to; k++) g_rx[g_rx_n++] = g_bus[k]; Ctrl_RxCb(&can2); CtrlCan_Process(); }
static void deliver_to_node1(int from, int to) { g_rx_n = 0; g_rx_i = 0; for (int k = from; k < to; k++) g_rx[g_rx_n++] = g_bus[k]; HAL_CAN_RxFifo0MsgPendingCallback(&can1); BmsCan_Process(); }

int main()
{
    /* ---------------- Node 1 encodes a realistic snapshot ---------------- */
    const uint16_t cells[16] = {3712,3715,3709,3711,3745,3720,3718,3716,3714,3719,3721,3713,3717,3722,3710,3716};
    memset(&g_bms, 0, sizeof g_bms);
    g_bms.state = BMS_STATE_DISCHARGING; g_bms.faults = BMS_FLT_CELL_OV | BMS_FLT_OVER_TEMP;
    g_bms.warnings = BMS_WRN_BALANCING | BMS_WRN_TEMP_HIGH;
    g_bms.m.stack_mv = 59843; g_bms.m.current_ma = -12345; g_bms.soc_pct = 76.5f;
    memcpy(g_bms.m.cell_mv, cells, sizeof cells);
    g_bms.m.cell_min_mv = 3709; g_bms.m.cell_max_mv = 3745; g_bms.m.cell_avg_mv = 3717;
    g_bms.m.cell_min_idx = 2; g_bms.m.cell_max_idx = 4; g_bms.m.cell_delta_mv = 36;
    g_bms.m.ntc_dc[0] = 315; g_bms.m.ntc_dc[1] = -52; g_bms.m.ntc_dc[2] = 300; g_bms.m.ntc_dc[3] = 285;
    g_bms.m.temp_max_dc = 315; g_bms.m.die_dc = 352;
    g_bms.chg_limit_ma = 6500; g_bms.dsg_limit_ma = 40000;
    g_bms.limit_flags = BMS_LIM_CHARGE_ALLOWED | BMS_LIM_DISCHARGE_ALLOWED | BMS_LIM_BALANCING_ACTIVE;
    g_bms.bal_mask = 0x0010; g_bms.bal_enabled = true; g_bms.bal_conditions_ok = true; g_bms.bal_seconds = 754;
    g_bms.soh_pct = 35.0f; g_bms.remaining_ah = 17.73f; g_bms.usable_ah = 23.17f; g_bms.discharged_ah = 5.4f;
    g_bms.uptime_s = 5025; g_bms.reset_cause = 0x05; g_bms.bq.status_a = 0x08; g_bms.bq.battery_status = 0x0800;

    CHECK(BmsCan_Init(&can1) == HAL_OK);
    g_bus_n = 0;
    BmsCan_SendFast(&g_bms); BmsCan_SendSlow(&g_bms); BmsCan_SendInfo(&g_bms);
    CHECK(g_bus_n == 13);
    int n1_end = g_bus_n;

    /* ---------------- Gateway decodes and serialises ---------------- */
    BmsSnapshot s; memset(&s, 0, sizeof s);
    bool all_ok = true;
    for (int k = 0; k < n1_end; k++) all_ok &= model_apply_frame(s, g_bus[k].id, g_bus[k].d, 5000);
    CHECK(all_ok);
    s.bms_online = model_bms_online(s, 5100); CHECK(s.bms_online);
    s.ts_ms = 1758650000123ULL; s.seq = 42;
    static char json[2048];
    size_t n = model_to_json(s, json, sizeof json);
    CHECK(n > 0 && n < sizeof json);
    printf("telemetry JSON: %zu bytes\n", n);
    JsonDocument doc; CHECK(!deserializeJson(doc, json));
    JsonObject b = doc["bms"];
    CHECK(doc["ts"].as<uint64_t>() == 1758650000123ULL);
    CHECK(!strcmp(b["state"], "DISCHARGING"));
    CHECK(NEAR(b["pack_v"].as<double>(), 59.84));
    CHECK(NEAR(b["current_a"].as<double>(), -12.34));
    CHECK(NEAR(b["soc"].as<double>(), 76.5));
    CHECK(b["cells_mv"].size() == 16 && b["cells_mv"][4] == 3745 && b["cells_mv"][15] == 3716);
    CHECK(b["cell_min_no"] == 3 && b["cell_max_no"] == 5 && b["cell_delta_mv"] == 36);
    CHECK(NEAR(b["temps_c"][0].as<double>(), 31.5) && NEAR(b["temps_c"][1].as<double>(), -5.2));
    CHECK(b["temp_max_c"] == 31 && b["bq_die_c"] == 35);
    CHECK(NEAR(b["limits"]["charge_a"].as<double>(), 6.5) && NEAR(b["limits"]["discharge_a"].as<double>(), 40.0));
    CHECK(b["limits"]["charge_allowed"] == true);
    CHECK(b["balancing"]["active"] == true && b["balancing"]["cells"][0] == 5 && b["balancing"]["minutes"] == 12);
    CHECK(b["faults"].size() == 2 && !strcmp(b["faults"][0], "CELL_OV") && !strcmp(b["faults"][1], "OVER_TEMP"));
    CHECK(b["warnings"].size() == 2);
    CHECK(NEAR(b["soh"].as<double>(), 35.0) && NEAR(b["remaining_ah"].as<double>(), 17.73) &&
          NEAR(b["capacity_ah"].as<double>(), 23.17) && NEAR(b["discharged_ah"].as<double>(), 5.4));
    CHECK(b["uptime_s"] == 5025 && !strcmp(b["fw"], "1.0") && b["bq76952"]["i2c_errors"] == 7);

    /* ---------------- Node 2 decodes the same frames ---------------- */
    memset(&g_ctrl, 0, sizeof g_ctrl);
    CHECK(CtrlCan_Init(&can2) == HAL_OK);
    deliver_to_node2(0, n1_end);
    BmsView_t *v = &g_ctrl.bms;
    CHECK(v->online && v->state == BMS_STATE_DISCHARGING && v->faults == (BMS_FLT_CELL_OV | BMS_FLT_OVER_TEMP));
    CHECK(v->pack_mv == 59840 && v->current_ma == -12340 && v->soc_x10 == 765);
    CHECK(v->chg_limit_ma == 6500 && v->dsg_limit_ma == 40000 && v->tmax_c == 31);
    CHECK(v->e2e_errors == 0 && v->pack_rx_count == 1);

    /* replayed (frozen counter) frames are rejected, fresh ones accepted */
    deliver_to_node2(0, n1_end);
    CHECK(v->e2e_errors >= 1 && v->pack_rx_count == 1);
    int m0 = g_bus_n; BmsCan_SendFast(&g_bms); deliver_to_node2(m0, g_bus_n);
    CHECK(v->e2e_errors == 0 && v->pack_rx_count == 2);

    /* corrupted CRC is rejected by both receivers */
    int m1 = g_bus_n; BmsCan_SendFast(&g_bms);
    for (int k = m1; k < g_bus_n; k++) if (g_bus[k].id == CAN_ID_BMS_PACK) g_bus[k].d[0] ^= 0x01;
    for (int k = m1; k < g_bus_n; k++) if (g_bus[k].id == CAN_ID_BMS_PACK) CHECK(!model_apply_frame(s, g_bus[k].id, g_bus[k].d, 6000));
    deliver_to_node2(m1, g_bus_n);
    CHECK(v->pack_rx_count == 2 && v->e2e_errors == 1);

    /* ---------------- Node 2 encodes, Node 1 + gateway decode ---------------- */
    g_ctrl.state = CTRL_STATE_RUN; g_ctrl.outputs = CTRL_OUT_MAIN_NEG | CTRL_OUT_MAIN_POS | CTRL_OUT_CHARGE_EN | CTRL_OUT_FAN;
    g_ctrl.fan_duty = 40; g_ctrl.inputs = CTRL_IN_KEY_ON | CTRL_IN_ESTOP_OK | CTRL_IN_BMS_ONLINE;
    g_ctrl.warnings = CTRL_WRN_REMOTE_STOP; g_ctrl.uptime_s = 321; g_ctrl.fan_rpm = 1500;
    int c0 = g_bus_n; CtrlCan_SendStatus(&g_ctrl); CtrlCan_SendInfo(&g_ctrl);
    for (int k = c0; k < g_bus_n; k++) CHECK(model_apply_frame(s, g_bus[k].id, g_bus[k].d, 7000));
    s.ctrl_online = model_ctrl_online(s, 7100);
    n = model_to_json(s, json, sizeof json); JsonDocument d2; deserializeJson(d2, json);
    JsonObject c = d2["ctrl"];
    CHECK(c["online"] == true && !strcmp(c["state"], "RUN"));
    CHECK(c["contactors"]["main_neg"] == true && c["contactors"]["precharge"] == false &&
          c["contactors"]["main_pos"] == true && c["contactors"]["charge_en"] == true);
    CHECK(c["fan"]["duty"] == 40 && c["fan"]["rpm"] == 1500 && c["key_on"] == true && c["estop_ok"] == true);
    CHECK(c["warnings"].size() == 1 && !strcmp(c["warnings"][0], "REMOTE_STOP") && c["uptime_s"] == 321);
    deliver_to_node1(c0, g_bus_n);
    CHECK(n1_hb_calls == 1);

    /* ---------------- Node 2 -> Node 1 commands ---------------- */
    int k0 = g_bus_n; CtrlCan_SendBmsCommand(CTRL_CMD_CLEAR_FAULTS, 0);
    deliver_to_node1(k0, g_bus_n); CHECK(n1_clear_calls == 1);
    deliver_to_node1(k0, g_bus_n); CHECK(n1_clear_calls == 1);           /* duplicate ignored */
    k0 = g_bus_n; CtrlCan_SendBmsCommand(CTRL_CMD_SET_BALANCING, 0);
    deliver_to_node1(k0, g_bus_n); CHECK(n1_bal_calls == 1 && n1_bal_last == false);

    /* ---------------- Gateway -> Node 2 commands (as can_link.cpp builds them) ---------------- */
    uint8_t cmd[8] = { GW_CMD_REQUEST_STOP, 0 }; can_e2e_protect(CAN_ID_GW_CMD, cmd, 1);
    g_bus[g_bus_n].id = CAN_ID_GW_CMD; memcpy(g_bus[g_bus_n].d, cmd, 8); g_bus_n++;
    deliver_to_node2(g_bus_n - 1, g_bus_n); CHECK(n2_stop_calls == 1);
    deliver_to_node2(g_bus_n - 1, g_bus_n); CHECK(n2_stop_calls == 1);  /* duplicate ignored */
    uint8_t fan[8] = { GW_CMD_FAN_OVERRIDE, 0xFF }; can_e2e_protect(CAN_ID_GW_CMD, fan, 2);
    g_bus[g_bus_n].id = CAN_ID_GW_CMD; memcpy(g_bus[g_bus_n].d, fan, 8); g_bus_n++;
    deliver_to_node2(g_bus_n - 1, g_bus_n); CHECK(n2_fan_calls == 1 && n2_fan_last == -1);
    uint8_t bad[8] = { GW_CMD_CLEAR_FAULTS, 0 }; can_e2e_protect(CAN_ID_GW_CMD, bad, 3); bad[7] ^= 0x55;
    g_bus[g_bus_n].id = CAN_ID_GW_CMD; memcpy(g_bus[g_bus_n].d, bad, 8); g_bus_n++;
    deliver_to_node2(g_bus_n - 1, g_bus_n); CHECK(n2_clear_calls == 0);  /* bad CRC ignored */

    /* ---------------- bus load estimate from the real schedule ---------------- */
    /* per second: 10 x fast(8 frames) + 2 x slow(2) + 1 x info(3) from Node 1,
     * 10 x status + 1 info from Node 2, 1 heartbeat from Node 3 */
    int frames_per_s = 10 * 8 + 2 * 2 + 3 + 10 + 1 + 1;
    double bits = frames_per_s * 135.0;   /* 8-byte std frame incl. worst-case stuffing */
    printf("bus load: %d frames/s, %.1f kbit/s = %.1f %% of 500 kbit/s\n", frames_per_s, bits / 1000.0, bits / 5000.0);

    printf("%d passed, %d failed\n", passes, fails);
    puts("--- sample telemetry JSON ---"); puts(json);
    return fails != 0;
}
