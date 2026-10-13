#include <string.h>
#include "stm32f1xx_hal.h"
#include "bus.h"
BusFrame g_bus[256]; int g_bus_n; BusFrame g_rx[256]; int g_rx_n, g_rx_i;
uint32_t g_tick = 1000;
uint32_t HAL_GetTick(void) { return g_tick; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) { (void)h; return 3; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *hdr, uint8_t *d, uint32_t *mb)
{ (void)h; (void)mb; g_bus[g_bus_n].id = (uint16_t)hdr->StdId; memcpy(g_bus[g_bus_n].d, d, 8); g_bus_n++; return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t f) { (void)h; (void)f; return (uint32_t)(g_rx_n - g_rx_i); }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t f, CAN_RxHeaderTypeDef *hdr, uint8_t *d)
{ (void)h; (void)f; memset(hdr, 0, sizeof(*hdr)); hdr->StdId = g_rx[g_rx_i].id; hdr->DLC = 8; memcpy(d, g_rx[g_rx_i].d, 8); g_rx_i++; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f) { (void)h; (void)f; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t i) { (void)h; (void)i; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *h) { (void)h; return HAL_OK; }
