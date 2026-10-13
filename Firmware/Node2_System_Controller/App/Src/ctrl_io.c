/**
 ******************************************************************************
 * @file    ctrl_io.c
 * @brief   Node 2 hardware I/O (see header).
 ******************************************************************************
 */
#include "ctrl_io.h"
#include "ctrl_config.h"
#include "main.h"

#if (CTRL_RELAY_ACTIVE_HIGH == 1)
#define RELAY_ON    GPIO_PIN_SET
#define RELAY_OFF   GPIO_PIN_RESET
#else
#define RELAY_ON    GPIO_PIN_RESET
#define RELAY_OFF   GPIO_PIN_SET
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool          active_low;
    bool          state;          /* debounced, true = active       */
    uint8_t       count;
} Input_t;

static TIM_HandleTypeDef *s_tim;
static volatile uint32_t  s_tach_pulses;
static uint16_t           s_rpm;

/* KEY_SW: switch to GND closes = RUN request.
 * ESTOP_LOOP: NC loop to GND. Low = healthy, so "active" means loop OK. */
static Input_t s_key   = { KEY_SW_GPIO_Port,     KEY_SW_Pin,     true, false, 0 };
static Input_t s_estop = { ESTOP_LOOP_GPIO_Port, ESTOP_LOOP_Pin, true, false, 0 };

static void relay(GPIO_TypeDef *port, uint16_t pin, bool on)
{
    HAL_GPIO_WritePin(port, pin, on ? RELAY_ON : RELAY_OFF);
}

void Io_Init(TIM_HandleTypeDef *htim_fan)
{
    s_tim = htim_fan;
    Io_SetOutputs(0);
    Io_SetFanDuty(0);
    (void)HAL_TIM_PWM_Start(s_tim, TIM_CHANNEL_1);
}

void Io_SetOutputs(uint8_t o)
{
    relay(RLY_MAIN_NEG_GPIO_Port,  RLY_MAIN_NEG_Pin,  (o & CTRL_OUT_MAIN_NEG)  != 0u);
    relay(RLY_PRECHARGE_GPIO_Port, RLY_PRECHARGE_Pin, (o & CTRL_OUT_PRECHARGE) != 0u);
    relay(RLY_MAIN_POS_GPIO_Port,  RLY_MAIN_POS_Pin,  (o & CTRL_OUT_MAIN_POS)  != 0u);
    relay(RLY_CHARGE_EN_GPIO_Port, RLY_CHARGE_EN_Pin, (o & CTRL_OUT_CHARGE_EN) != 0u);
}

static void debounce(Input_t *in)
{
    bool level  = HAL_GPIO_ReadPin(in->port, in->pin) == GPIO_PIN_SET;
    bool active = in->active_low ? !level : level;

    if (active == in->state) { in->count = 0; return; }
    if (++in->count >= CTRL_INPUT_DEBOUNCE_TICKS) { in->state = active; in->count = 0; }
}

void Io_Sample(void)
{
    debounce(&s_key);
    debounce(&s_estop);
}

bool Io_KeyOn(void)   { return s_key.state; }
bool Io_EstopOk(void) { return s_estop.state; }

void Io_SetFanDuty(uint8_t percent)
{
    uint32_t ccr;
    if (percent > 100u) percent = 100u;
    ccr = (CTRL_FAN_PWM_PERIOD * percent) / 100u;
#if (CTRL_FAN_PWM_INVERT == 1)
    ccr = CTRL_FAN_PWM_PERIOD - ccr;
#endif
    if (s_tim != NULL) __HAL_TIM_SET_COMPARE(s_tim, TIM_CHANNEL_1, ccr);
}

void Io_OnTachPulse(void)
{
    s_tach_pulses++;
}

void Io_Tick1s(void)
{
    uint32_t p;
    __disable_irq();
    p = s_tach_pulses;
    s_tach_pulses = 0;
    __enable_irq();
    s_rpm = (uint16_t)((p * 60u) / CTRL_FAN_PULSES_PER_REV);
}

uint16_t Io_FanRpm(void)
{
    return s_rpm;
}

void Io_SetLeds(bool status_on, bool fault_on)
{
    /* Blue Pill PC13 LED is active low */
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, status_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#if (CTRL_LED_FAULT_ACTIVE_HIGH == 1)
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, fault_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, fault_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}
