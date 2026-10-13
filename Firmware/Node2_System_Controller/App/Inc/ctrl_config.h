/**
 ******************************************************************************
 * @file    ctrl_config.h
 * @brief   Node 2 (System Controller) - every tunable parameter.
 *
 *  Board : STM32F103C8T6 "Blue Pill" + ISO1042 isolated CAN module
 *  Pins  : see README "Node 2 pin map" and Node2_System_Controller.ioc
 ******************************************************************************
 */
#ifndef CTRL_CONFIG_H
#define CTRL_CONFIG_H

#include "bms_can_protocol.h"

#define CTRL_FW_VERSION_MAJOR       1u
#define CTRL_FW_VERSION_MINOR       0u

/* ==========================================================================
 * 1. OUTPUT / INPUT POLARITY
 * ========================================================================== */
/* 1: GPIO high closes the relay (NPN / ULN2803 coil driver, 10 k pull-down
 *    on the driver input keeps it open during reset).
 * 0: active-low opto relay modules. ALSO set PB12..PB15 "GPIO output level"
 *    to High in Node2_System_Controller.ioc and regenerate, otherwise
 *    MX_GPIO_Init() closes every relay for a moment at power-up.          */
#define CTRL_RELAY_ACTIVE_HIGH      1
#define CTRL_FAN_PWM_INVERT         0       /* 1 if the fan driver inverts */
#define CTRL_LED_FAULT_ACTIVE_HIGH  1

/* KEY_SW (PB10): switch to GND = RUN request (internal pull-up).
 * ESTOP_LOOP (PB11): normally-closed loop to GND. Open loop or broken wire
 * reads high and stops the pack (fail-safe).                              */

/* ==========================================================================
 * 2. TIMING
 * ========================================================================== */
#define CTRL_TICK_MS                10u
#define CTRL_INPUT_DEBOUNCE_TICKS   3u      /* 30 ms                          */
#define CTRL_STARTUP_WAIT_MS        5000u   /* Node 1 boots from the same bus */
#define CTRL_RELAY_STEP_MS          100u    /* gap between contactor actions  */

/* ==========================================================================
 * 3. PRECHARGE (closed-loop on the pack current measured by Node 1)
 *    Main- closes, then the precharge relay. The DC-link capacitors charge
 *    through the precharge resistor, so the pack current decays to ~0.
 *    When it is below DONE (and at least MIN_MS has passed) Main+ closes.
 * ========================================================================== */
#define CTRL_PRECHARGE_MIN_MS       300u
#define CTRL_PRECHARGE_TIMEOUT_MS   5000u
#define CTRL_PRECHARGE_DONE_MA      1000L
#define CTRL_PRECHARGE_MAX_MA       20000L  /* above this = short on the load */

/* ==========================================================================
 * 4. RUN / STOP
 * ========================================================================== */
#define CTRL_STOP_CURRENT_MA        2000L   /* open Main+ below this ...      */
#define CTRL_STOP_TIMEOUT_MS        2000u   /* ... or after this long anyway  */
#define CTRL_CHARGE_ENABLE_DELAY_MS 2000u   /* BMS must allow charge this long */
#define CTRL_LIMIT_MARGIN_PCT       20      /* current above limit + margin   */
#define CTRL_LIMIT_MARGIN_MIN_MA    5000L
#define CTRL_LIMIT_VIOLATION_MS     3000u   /* ... for this long -> trip       */

/* ==========================================================================
 * 5. BUS SUPERVISION
 * ========================================================================== */
#define CTRL_BMS_TIMEOUT_MS         CAN_TIMEOUT_BMS_MS
#define CTRL_GW_TIMEOUT_MS          CAN_TIMEOUT_GW_MS
#define CTRL_E2E_MAX_ERRORS         5u      /* consecutive bad / frozen frames */

/* ==========================================================================
 * 6. COOLING FAN (TIM3_CH1 on PA6, 25 kHz, 4-wire PC fan or MOSFET)
 * ========================================================================== */
#define CTRL_FAN_PWM_PERIOD         2880u   /* TIM3 ARR + 1 at 72 MHz          */
#define CTRL_FAN_START_C            30      /* fan on above ...                */
#define CTRL_FAN_STOP_C             27      /* ... and off below (hysteresis)  */
#define CTRL_FAN_FULL_C             45      /* 100 % at and above              */
#define CTRL_FAN_MIN_DUTY           30      /* % at CTRL_FAN_START_C           */
#define CTRL_FAN_BALANCING_DUTY     40      /* bleeder resistors heat the box  */
#define CTRL_FAN_HAS_TACH           1       /* 0 for a 2-wire fan              */
#define CTRL_FAN_PULSES_PER_REV     2u
#define CTRL_FAN_FAIL_RPM           300u
#define CTRL_FAN_SPINUP_MS          5000u
#define CTRL_FAN_OVERRIDE_MAX_MS    (10UL * 60UL * 1000UL)

/* ==========================================================================
 * 7. FEATURE SWITCHES
 * ========================================================================== */
#define CTRL_USE_IWDG               1
#define CTRL_USE_CONSOLE            1

#endif /* CTRL_CONFIG_H */
