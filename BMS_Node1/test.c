/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : BMS Node 1 Firmware - BQ76952 I2C to CAN Bridge
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private typedef -----------------------------------------------------------*/
typedef struct {
    uint16_t cell_voltages[16]; // Millivolts (mV)
    int16_t  pack_current;      // Milliamps (mA)
    uint16_t temperature_k;     // 0.1 Kelvin
} BMS_Data_t;

/* Private define ------------------------------------------------------------*/
// BQ76952 default I2C 7-bit address (0x08) shifted left 1 bit for STM32 HAL
#define BQ76952_I2C_ADDR      (0x08 << 1)

// Direct Command Register Addresses
#define CMD_CELL1_VOLTAGE     0x14 // Cell 1 start register (2 bytes each up to Cell 16)
#define CMD_PACK_CURRENT      0x3A // Signed 16-bit current
#define CMD_INTERNAL_TEMP     0x6E // Internal IC Temperature

// Base CAN IDs for Node 1 telemetry
#define CAN_ID_CELLS_1_4      0x101
#define CAN_ID_CELLS_5_8      0x102
#define CAN_ID_CELLS_9_12     0x103
#define CAN_ID_CELLS_13_16    0x104
#define CAN_ID_SYSTEM_STATUS  0x105

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
CAN_HandleTypeDef hcan;

BMS_Data_t bms;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_CAN_Init(void);

uint16_t BQ76952_ReadRegister16(uint8_t reg_addr);
void BQ76952_ReadAllTelemetry(void);
void CAN_Send_Frame(uint32_t std_id, uint8_t *data, uint8_t len);
void CAN_Broadcast_BMS_Data(void);

/**
  * @brief  The application entry point.
  */
int main(void)
{
  /* MCU Configuration */
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_CAN_Init();

  // Start the CAN bus hardware module
  if (HAL_CAN_Start(&hcan) != HAL_OK) {
      // Hardware error indicator
      Error_Handler();
  }

  // 1. Verify BQ76952 is alive on I2C bus before starting
  while (HAL_I2C_IsDeviceReady(&hi2c1, BQ76952_I2C_ADDR, 3, 100) != HAL_OK) {
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // Flash onboard LED quickly if waiting
      HAL_Delay(200);
  }

  /* Infinite Loop */
  while (1)
  {
      // Toggle LED to show active heartbeat loop
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

      // 2. Read all sensor data from BQ76952 via I2C
      BQ76952_ReadAllTelemetry();

      // 3. Packetize and transmit the data onto the CAN network
      CAN_Broadcast_BMS_Data();

      // Transmit update cycle at 10 Hz (every 100ms)
      HAL_Delay(100);
  }
}

/**
  * @brief Read a 16-bit Direct Command register from BQ76952
  */
uint16_t BQ76952_ReadRegister16(uint8_t reg_addr) {
    uint8_t rx_buf[2] = {0};
    
    // Request 2 bytes starting at target register
    if (HAL_I2C_Mem_Read(&hi2c1, BQ76952_I2C_ADDR, reg_addr, I2C_MEMADD_SIZE_8BIT, rx_buf, 2, 50) == HAL_OK) {
        // BQ76952 uses Little-Endian format (Byte 0 = LSB, Byte 1 = MSB)
        return (uint16_t)(rx_buf[0] | (rx_buf[1] << 8));
    }
    return 0; // Return 0 on communication error
}

/**
  * @brief Polls all relevant BQ76952 registers
  */
void BQ76952_ReadAllTelemetry(void) {
    // Read 16 Cell Voltages (Registers 0x14 through 0x32, stepping by 2 bytes)
    for (int i = 0; i < 16; i++) {
        uint8_t reg = CMD_CELL1_VOLTAGE + (i * 2);
        bms.cell_voltages[i] = BQ76952_ReadRegister16(reg);
    }

    // Read Pack Current
    bms.pack_current = (int16_t)BQ76952_ReadRegister16(CMD_PACK_CURRENT);

    // Read Temperature
    bms.temperature_k = BQ76952_ReadRegister16(CMD_INTERNAL_TEMP);
}

/**
  * @brief Helper function to transmit an 8-byte payload on CAN bus
  */
void CAN_Send_Frame(uint32_t std_id, uint8_t *data, uint8_t len) {
    CAN_TxHeaderTypeDef tx_header;
    uint32_t tx_mailbox;

    tx_header.StdId = std_id;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.IDE = CAN_ID_STD;
    tx_header.DLC = len;
    tx_header.TransmitGlobalTime = DISABLE;

    HAL_CAN_AddTxMessage(&hcan, &tx_header, data, &tx_mailbox);
}

/**
  * @brief Packs 16 cell voltages + system telemetry into 5 standard CAN frames
  */
void CAN_Broadcast_BMS_Data(void) {
    uint8_t frame_buf[8];

    // Frame 1: Cells 1 to 4 (CAN ID: 0x101)
    for (int i = 0; i < 4; i++) {
        frame_buf[i * 2]     = (uint8_t)(bms.cell_voltages[i] & 0xFF);        // LSB
        frame_buf[i * 2 + 1] = (uint8_t)((bms.cell_voltages[i] >> 8) & 0xFF); // MSB
    }
    CAN_Send_Frame(CAN_ID_CELLS_1_4, frame_buf, 8);

    // Frame 2: Cells 5 to 8 (CAN ID: 0x102)
    for (int i = 0; i < 4; i++) {
        frame_buf[i * 2]     = (uint8_t)(bms.cell_voltages[i + 4] & 0xFF);
        frame_buf[i * 2 + 1] = (uint8_t)((bms.cell_voltages[i + 4] >> 8) & 0xFF);
    }
    CAN_Send_Frame(CAN_ID_CELLS_5_8, frame_buf, 8);

    // Frame 3: Cells 9 to 12 (CAN ID: 0x103)
    for (int i = 0; i < 4; i++) {
        frame_buf[i * 2]     = (uint8_t)(bms.cell_voltages[i + 8] & 0xFF);
        frame_buf[i * 2 + 1] = (uint8_t)((bms.cell_voltages[i + 8] >> 8) & 0xFF);
    }
    CAN_Send_Frame(CAN_ID_CELLS_9_12, frame_buf, 8);

    // Frame 4: Cells 13 to 16 (CAN ID: 0x104)
    for (int i = 0; i < 4; i++) {
        frame_buf[i * 2]     = (uint8_t)(bms.cell_voltages[i + 12] & 0xFF);
        frame_buf[i * 2 + 1] = (uint8_t)((bms.cell_voltages[i + 12] >> 8) & 0xFF);
    }
    CAN_Send_Frame(CAN_ID_CELLS_13_16, frame_buf, 8);

    // Frame 5: Current & Temperature System Data (CAN ID: 0x105)
    frame_buf[0] = (uint8_t)(bms.pack_current & 0xFF);
    frame_buf[1] = (uint8_t)((bms.pack_current >> 8) & 0xFF);
    frame_buf[2] = (uint8_t)(bms.temperature_k & 0xFF);
    frame_buf[3] = (uint8_t)((bms.temperature_k >> 8) & 0xFF);
    frame_buf[4] = 0x00; // Reserved
    frame_buf[5] = 0x00; // Reserved
    frame_buf[6] = 0x00; // Reserved
    frame_buf[7] = 0x00; // Reserved
    CAN_Send_Frame(CAN_ID_SYSTEM_STATUS, frame_buf, 8);
}

/* Peripheral Initialization Functions (Generated by STM32CubeMX) ------------*/
static void MX_I2C1_Init(void) {
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000; // Standard 100 kHz I2C
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  HAL_I2C_Init(&hi2c1);
}

static void MX_CAN_Init(void) {
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 18;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_2TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_1TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  HAL_CAN_Init(&hcan);
}

static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* Configure Onboard LED Pin PC13 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void SystemClock_Config(void) {
  // Clock config generated by CubeMX
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {}
}