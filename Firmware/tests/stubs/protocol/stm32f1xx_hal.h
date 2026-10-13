#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { HAL_OK = 0, HAL_ERROR } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int id; } GPIO_TypeDef;
#define GPIOB ((GPIO_TypeDef *)0)
#define GPIO_PIN_6 0x40u
#define GPIO_PIN_7 0x80u
typedef struct { uint32_t ESR; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; } CAN_HandleTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, TransmitGlobalTime; } CAN_TxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, Timestamp, FilterMatchIndex; } CAN_RxHeaderTypeDef;
typedef struct { uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow, FilterFIFOAssignment, FilterBank, FilterMode, FilterScale, FilterActivation, SlaveStartFilterBank; } CAN_FilterTypeDef;
typedef struct { int d; } I2C_HandleTypeDef; typedef struct { int d; } SPI_HandleTypeDef;
typedef struct { int d; } UART_HandleTypeDef; typedef struct { int d; } TIM_HandleTypeDef;
typedef struct { void *Instance; } IWDG_HandleTypeDef;
#define CAN_ID_STD 0u
#define CAN_RTR_DATA 0u
#define CAN_RX_FIFO0 0u
#define CAN_FILTERMODE_IDLIST 1u
#define CAN_FILTERMODE_IDMASK 0u
#define CAN_FILTERSCALE_16BIT 0u
#define CAN_FILTER_FIFO0 0u
#define CAN_FILTER_ENABLE 1u
#define CAN_IT_RX_FIFO0_MSG_PENDING 1u
#define CAN_IT_TX_MAILBOX_EMPTY 2u
#define CAN_IT_ERROR_WARNING 4u
#define CAN_IT_ERROR_PASSIVE 8u
#define CAN_IT_BUSOFF 16u
#define CAN_IT_ERROR 32u
#define CAN_ESR_BOFF 4u
#define CAN_ESR_EPVF 2u
#define ENABLE 1u
#define DISABLE 0u
uint32_t HAL_GetTick(void);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *hdr, uint8_t *d, uint32_t *mb);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hdr, uint8_t *d);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t it);
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *h);
static inline uint32_t __get_PRIMASK(void) { return 0; }
static inline void __disable_irq(void) {}
static inline void __set_PRIMASK(uint32_t p) { (void)p; }
#ifdef __cplusplus
}
#endif
