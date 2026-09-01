/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Distributed BMS - battery monitoring node
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bms_config.h"
#include "bq76952.h"
#include "bms_app.h"
#include "bms_can.h"
#if (BMS_USE_OLED == 1)
#include "ssd1306.h"
#endif
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

IWDG_HandleTypeDef hiwdg;

/* USER CODE BEGIN PV */
static volatile bool s_bq_alert = false;   /* set by the BQ ALERT EXTI */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */
static void DebugPrint(const char *s);
static void StartupSelfTest(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void DebugPrint(const char *s)
{
#if (BMS_USE_UART_DEBUG == 1)
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
#else
    (void)s;
#endif
}

/**
 * Bring the BQ76952 up and prove it is configured the way we think it is.
 * Any failure here latches a fault -- the node keeps running and keeps
 * reporting, but the safety output stays low.
 */
static void StartupSelfTest(void)
{
    char msg[80];
    BQ_Status_t st;

    BQ76952_Attach(&hi2c1);

    st = BQ76952_Probe();
    if (st != BQ_OK) {
        snprintf(msg, sizeof(msg), "BQ probe FAILED: %s\r\n",
                 BQ76952_StatusName(st));
        DebugPrint(msg);
        g_bms.faults |= BMS_FAULT_BQ_COMMS;
        return;
    }
    DebugPrint("BQ76952 detected (0x7695)\r\n");

    st = BQ76952_Configure();
    if (st != BQ_OK) {
        snprintf(msg, sizeof(msg), "BQ config FAILED: %s\r\n",
                 BQ76952_StatusName(st));
        DebugPrint(msg);
        g_bms.faults |= BMS_FAULT_BQ_CONFIG;
        return;
    }
    DebugPrint("BQ76952 configured and verified\r\n");
}

/* CAN RX interrupt. Reading the message clears the pending flag, so this
 * callback must actually drain the FIFO or the ISR will re-fire forever. */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h)
{
    (void)h;
    BMS_CAN_Poll();
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *h)
{
    if (READ_BIT(h->Instance->ESR, CAN_ESR_BOFF) != 0u) {
        g_bms.faults |= BMS_FAULT_CAN_BUS_OFF;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == BQ_ALERT_Pin) s_bq_alert = true;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  uint32_t t_meas = 0, t_fast = 0, t_slow = 0, t_hb = 0, t_bal = 0, t_oled = 0;
  uint32_t last_faults = 0;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */
  DebugPrint("\r\n=== 16S BMS monitoring node ===\r\n");

  BMS_App_Init();

  if (BMS_CAN_Init(&hcan) != HAL_OK) {
      DebugPrint("CAN init FAILED\r\n");
      g_bms.faults |= BMS_FAULT_CAN_BUS_OFF;
  } else {
      DebugPrint("CAN 500 kbit/s up\r\n");
  }

#if (BMS_USE_OLED == 1)
  SSD1306_Init(&hspi1);
#endif

  StartupSelfTest();

  /* Watchdog last, so a slow BQ configuration cannot trip it. */
  MX_IWDG_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* --- 100 ms: measure and evaluate ---------------------------------- */
    if ((now - t_meas) >= BMS_MEAS_PERIOD_MS) {
        t_meas = now;
        BMS_App_MeasureAndEvaluate();

        if (s_bq_alert) {
            s_bq_alert = false;
            /* The next scheduled read already picks up the new safety status;
             * the flag is here so you can see the pin working during bring-up. */
        }
    }

    /* --- 100 ms: fast CAN group ---------------------------------------- */
    if ((now - t_fast) >= BMS_CAN_FAST_PERIOD_MS) {
        t_fast = now;
        BMS_CAN_SendFast();
    }

    /* --- immediate fault frame on any change --------------------------- */
    if (g_bms.faults != last_faults) {
        last_faults = g_bms.faults;
        BMS_CAN_SendFaultsNow();
    }

    /* --- 500 ms: slow CAN group ---------------------------------------- */
    if ((now - t_slow) >= BMS_CAN_SLOW_PERIOD_MS) {
        t_slow = now;
        BMS_CAN_SendSlow();
    }

    /* --- 1 s: heartbeat, housekeeping, LED ----------------------------- */
    if ((now - t_hb) >= BMS_CAN_HEARTBEAT_MS) {
        t_hb = now;
        BMS_App_Tick1s();
        BMS_CAN_SendHeartbeat();
        HAL_GPIO_TogglePin(HB_LED_GPIO_Port, HB_LED_Pin);
    }

    /* --- 1 s: balancing ------------------------------------------------- */
    if ((now - t_bal) >= BMS_BALANCE_PERIOD_MS) {
        t_bal = now;
        BMS_App_UpdateBalancing();
    }

    /* --- 250 ms: display ------------------------------------------------ */
#if (BMS_USE_OLED == 1)
    if ((now - t_oled) >= BMS_OLED_PERIOD_MS) {
        t_oled = now;
        SSD1306_RenderBms();
    }
#else
    (void)t_oled;
#endif

    /* --- backstop RX poll ---------------------------------------------- */
    BMS_CAN_Poll();

    /* --- watchdog -------------------------------------------------------
     * Only refresh while measurements are fresh. If the BQ read loop stalls,
     * the watchdog resets the MCU, BMS_OK drops to its pull-down and the
     * system controller opens the contactors. That is the point of it.      */
#if (BMS_USE_IWDG == 1)
    if ((HAL_GetTick() - g_bms.last_good_meas_ms) < BMS_BQ_COMMS_TIMEOUT_MS ||
        (g_bms.faults & BMS_FAULT_BQ_COMMS)) {
        HAL_IWDG_Refresh(&hiwdg);
    }
#endif

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  *        HSE 8 MHz crystal -> PLL x9 -> SYSCLK 72 MHz
  *        AHB 72 MHz, APB2 72 MHz, APB1 36 MHz (CAN clock source)
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /* 36 MHz max for APB1 */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN Initialization Function - 500 kbit/s
  * @retval None
  */
static void MX_CAN_Init(void)
{
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 4;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_4TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = ENABLE;          /* recover automatically */
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;  /* retry on arbitration loss */
  hcan.Init.ReceiveFifoLocked = DISABLE;  /* newest message wins */
  hcan.Init.TransmitFifoPriority = ENABLE;/* FIFO order, not ID order */
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function - 100 kHz standard mode
  * @retval None
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function - SSD1306 OLED, 9 MHz, mode 0
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  /* 72/8 = 9 MHz */
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief IWDG Initialization Function - about 500 ms timeout
  *        LSI is nominally 40 kHz. Prescaler 32 -> 1.25 kHz, reload 625.
  *        LSI tolerance is wide (30-60 kHz), so the real timeout is roughly
  *        330-660 ms. That is fine for a 500 ms budget; do not tighten it.
  * @retval None
  */
static void MX_IWDG_Init(void)
{
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Reload = 625;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* Free PB3/PB4/PA15 from JTAG so SWD still works but the pins are usable.
   * Keep SWD enabled -- you will want the debugger. */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
  /* USER CODE END MX_GPIO_Init_1 */

  /* Safe reset states BEFORE configuring as outputs. BMS_OK must never
   * glitch high during startup. */
  HAL_GPIO_WritePin(BMS_OK_GPIO_Port, BMS_OK_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(FAULT_LED_GPIO_Port, FAULT_LED_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(HB_LED_GPIO_Port, HB_LED_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET);

  /* Heartbeat LED (PC13, active low on the Blue Pill) */
  GPIO_InitStruct.Pin = HB_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HB_LED_GPIO_Port, &GPIO_InitStruct);

  /* BMS_OK interlock + fault LED */
  GPIO_InitStruct.Pin = BMS_OK_Pin | FAULT_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* OLED control lines */
  GPIO_InitStruct.Pin = OLED_CS_Pin | OLED_DC_Pin | OLED_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* BQ76952 ALERT, active low, external pull-up on the PCB */
  GPIO_InitStruct.Pin = BQ_ALERT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BQ_ALERT_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(BQ_ALERT_EXTI_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(BQ_ALERT_EXTI_IRQn);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Reached only for clock or peripheral init failures, which mean the MCU
   * cannot be trusted at all. Drop BMS_OK and spin -- the watchdog is not
   * running yet at this point, so this is a deliberate hard stop with the
   * interlock open. */
  HAL_GPIO_WritePin(BMS_OK_GPIO_Port, BMS_OK_Pin, GPIO_PIN_RESET);
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
