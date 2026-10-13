/**
 ******************************************************************************
 * @file    bq76952_regs.h
 * @brief   BQ76952 command, subcommand and data-memory map.
 *          Checked against TI SLUUBY2B (Technical Reference Manual, May 2022).
 ******************************************************************************
 */
#ifndef BQ76952_REGS_H
#define BQ76952_REGS_H

/* ---------------------------------------------------------------------------
 * Direct commands (TRM Table 12-1)
 * ------------------------------------------------------------------------- */
#define BQ_CMD_CONTROL_STATUS       0x00u
#define BQ_CMD_SAFETY_ALERT_A       0x02u   /* 0x02..0x07 read as one block  */
#define BQ_CMD_SAFETY_STATUS_A      0x03u
#define BQ_CMD_SAFETY_ALERT_B       0x04u
#define BQ_CMD_SAFETY_STATUS_B      0x05u
#define BQ_CMD_SAFETY_ALERT_C       0x06u
#define BQ_CMD_SAFETY_STATUS_C      0x07u
#define BQ_CMD_PF_STATUS_A          0x0Bu
#define BQ_CMD_BATTERY_STATUS       0x12u
#define BQ_CMD_CELL1_VOLTAGE        0x14u   /* I2 mV, cells 1..16 to 0x32    */
#define BQ_CMD_STACK_VOLTAGE        0x34u   /* I2 userV                      */
#define BQ_CMD_PACK_VOLTAGE         0x36u   /* I2 userV                      */
#define BQ_CMD_LD_VOLTAGE           0x38u   /* I2 userV                      */
#define BQ_CMD_CC2_CURRENT          0x3Au   /* I2 userA                      */
#define BQ_CMD_SUBCMD_LOW           0x3Eu
#define BQ_CMD_SUBCMD_HIGH          0x3Fu
#define BQ_CMD_TRANSFER_BUFFER      0x40u   /* 32 bytes, 0x40..0x5F          */
#define BQ_CMD_BUFFER_CHECKSUM      0x60u
#define BQ_CMD_BUFFER_LENGTH        0x61u
#define BQ_CMD_ALARM_STATUS         0x62u   /* latched, write 1 to clear     */
#define BQ_CMD_ALARM_RAW_STATUS     0x64u
#define BQ_CMD_ALARM_ENABLE         0x66u
#define BQ_CMD_INT_TEMPERATURE      0x68u   /* I2 0.1 K, 0x68..0x7B block    */
#define BQ_CMD_TS1_TEMPERATURE      0x70u
#define BQ_CMD_TS2_TEMPERATURE      0x72u
#define BQ_CMD_TS3_TEMPERATURE      0x74u
#define BQ_CMD_HDQ_TEMPERATURE      0x76u
#define BQ_CMD_DCHG_TEMPERATURE     0x78u
#define BQ_CMD_DDSG_TEMPERATURE     0x7Au
#define BQ_CMD_FET_STATUS           0x7Fu

/* Block read spans used by the driver */
#define BQ_BLOCK_MEAS_START         BQ_CMD_CELL1_VOLTAGE          /* 0x14 */
#define BQ_BLOCK_MEAS_LEN           40u     /* 16 cells + stack + pack + LD + CC2 */
#define BQ_BLOCK_TEMP_START         BQ_CMD_INT_TEMPERATURE        /* 0x68 */
#define BQ_BLOCK_TEMP_LEN           20u     /* 0x68..0x7B                    */
#define BQ_BLOCK_SAFETY_START       BQ_CMD_SAFETY_ALERT_A         /* 0x02 */
#define BQ_BLOCK_SAFETY_LEN         6u

/* ---------------------------------------------------------------------------
 * Subcommands (TRM Table 12-22 / 12-23)
 * ------------------------------------------------------------------------- */
#define BQ_SUB_DEVICE_NUMBER        0x0001u /* returns 0x7695                */
#define BQ_SUB_FW_VERSION           0x0002u
#define BQ_SUB_RESET                0x0012u
#define BQ_SUB_DASTATUS6            0x0076u /* accumulated charge + time     */
#define BQ_SUB_RESET_PASSQ          0x0082u
#define BQ_SUB_CB_ACTIVE_CELLS      0x0083u
#define BQ_SUB_CBSTATUS1            0x0085u
#define BQ_SUB_SET_CFGUPDATE        0x0090u
#define BQ_SUB_EXIT_CFGUPDATE       0x0092u
#define BQ_SUB_SLEEP_DISABLE        0x009Au
#define BQ_SUB_OCDL_RECOVER         0x009Bu
#define BQ_SUB_SCDL_RECOVER         0x009Cu

#define BQ_DEVICE_NUMBER            0x7695u

/* ---------------------------------------------------------------------------
 * Data memory (TRM Table 13-37). Written only inside CONFIG_UPDATE.
 * ------------------------------------------------------------------------- */
#define BQ_DM_CC_GAIN               0x91A8u /* F4                            */
#define BQ_DM_CAPACITY_GAIN         0x91ACu /* F4                            */
#define BQ_DM_POWER_CONFIG          0x9234u /* H2, default 0x2982            */
#define BQ_DM_REG12_CONFIG          0x9236u /* H1                            */
#define BQ_DM_CFETOFF_PIN_CONFIG    0x92FAu /* H1                            */
#define BQ_DM_DFETOFF_PIN_CONFIG    0x92FBu
#define BQ_DM_ALERT_PIN_CONFIG      0x92FCu
#define BQ_DM_TS1_CONFIG            0x92FDu
#define BQ_DM_TS2_CONFIG            0x92FEu
#define BQ_DM_TS3_CONFIG            0x92FFu
#define BQ_DM_HDQ_PIN_CONFIG        0x9300u
#define BQ_DM_DCHG_PIN_CONFIG       0x9301u
#define BQ_DM_DDSG_PIN_CONFIG       0x9302u
#define BQ_DM_DA_CONFIGURATION      0x9303u /* H1, default 0x05              */
#define BQ_DM_VCELL_MODE            0x9304u /* H2                            */
#define BQ_DM_ENABLED_PROT_A        0x9261u
#define BQ_DM_ENABLED_PROT_B        0x9262u
#define BQ_DM_ENABLED_PROT_C        0x9263u
#define BQ_DM_DEFAULT_ALARM_MASK    0x926Du /* H2, default 0xF800            */
#define BQ_DM_BALANCING_CONFIG      0x9335u
#define BQ_DM_BAL_MIN_CELL_TEMP     0x9336u /* I1 degC                       */
#define BQ_DM_BAL_MAX_CELL_TEMP     0x9337u
#define BQ_DM_CUV_THRESHOLD         0x9275u /* U1, 50.6 mV                   */
#define BQ_DM_COV_THRESHOLD         0x9278u /* U1, 50.6 mV                   */
#define BQ_DM_OCC_THRESHOLD         0x9280u /* U1, 2 mV across shunt         */
#define BQ_DM_OCC_DELAY             0x9281u /* U1, 3.3 ms                    */
#define BQ_DM_OCD1_THRESHOLD        0x9282u
#define BQ_DM_OCD1_DELAY            0x9283u
#define BQ_DM_OCD2_THRESHOLD        0x9284u
#define BQ_DM_OCD2_DELAY            0x9285u
#define BQ_DM_SCD_THRESHOLD         0x9286u /* U1 enum                       */
#define BQ_DM_OTC_THRESHOLD         0x929Au /* I1 degC                       */
#define BQ_DM_OTD_THRESHOLD         0x929Du
#define BQ_DM_UTC_THRESHOLD         0x92A6u
#define BQ_DM_UTD_THRESHOLD         0x92A9u

/* ---------------------------------------------------------------------------
 * Field values
 * ------------------------------------------------------------------------- */
/* Power Config: default 0x2982 with bit 8 (SLEEP) cleared -> always NORMAL */
#define BQ_POWER_CONFIG_NO_SLEEP    0x2882u

/* Pin config (TRM 6.4, Tables 6-5 / 6-6) */
#define BQ_PIN_UNUSED               0x00u
/* Thermistor: PIN_FXN = 3, 18 k pull-up, 18K model, cell temperature */
#define BQ_PIN_THERMISTOR_CELL      0x07u
/* ALERT: PIN_FXN = 2, OPT5 = active-low, OPT1 = 0 -> tri-state when high.
 * i.e. open-drain, pulled up to 3V3 by R25 (10 k) on the PCB. */
#define BQ_PIN_ALERT_OD_ACTIVE_LOW  0x82u

/* DA Configuration: USER_VOLTS_CV = 1 (10 mV), USER_AMPS = 2 (10 mA) */
#define BQ_DA_CONFIG_CV_CA          0x06u
#define BQ_USER_V_MV                10
#define BQ_USER_A_MA                10

/* Enabled Protections */
#define BQ_PROT_A_CUV               (1u << 2)
#define BQ_PROT_A_COV               (1u << 3)
#define BQ_PROT_A_OCC               (1u << 4)
#define BQ_PROT_A_OCD1              (1u << 5)
#define BQ_PROT_A_OCD2              (1u << 6)
#define BQ_PROT_A_SCD               (1u << 7)
#define BQ_PROT_B_UTC               (1u << 0)
#define BQ_PROT_B_UTD               (1u << 1)
#define BQ_PROT_B_UTINT             (1u << 2)
#define BQ_PROT_B_OTC               (1u << 4)
#define BQ_PROT_B_OTD               (1u << 5)
#define BQ_PROT_B_OTINT             (1u << 6)
#define BQ_PROT_B_OTF               (1u << 7)

/* Safety Status A / B / C (same layout as Safety Alert A / B / C) */
#define BQ_SSA_CUV                  (1u << 2)
#define BQ_SSA_COV                  (1u << 3)
#define BQ_SSA_OCC                  (1u << 4)
#define BQ_SSA_OCD1                 (1u << 5)
#define BQ_SSA_OCD2                 (1u << 6)
#define BQ_SSA_SCD                  (1u << 7)
#define BQ_SSB_UTC                  (1u << 0)
#define BQ_SSB_UTD                  (1u << 1)
#define BQ_SSB_UTINT                (1u << 2)
#define BQ_SSB_OTC                  (1u << 4)
#define BQ_SSB_OTD                  (1u << 5)
#define BQ_SSB_OTINT                (1u << 6)
#define BQ_SSB_OTF                  (1u << 7)
#define BQ_SSC_HWDF                 (1u << 1)
#define BQ_SSC_PTO                  (1u << 2)
#define BQ_SSC_COVL                 (1u << 4)
#define BQ_SSC_OCDL                 (1u << 5)
#define BQ_SSC_SCDL                 (1u << 6)
#define BQ_SSC_OCD3                 (1u << 7)

/* Battery Status (TRM Table 12-17) */
#define BQ_BAT_CFGUPDATE            (1u << 0)
#define BQ_BAT_PCHG_MODE            (1u << 1)
#define BQ_BAT_SLEEP_EN             (1u << 2)
#define BQ_BAT_POR                  (1u << 3)   /* RAM config lost -> redo   */
#define BQ_BAT_WD                   (1u << 4)
#define BQ_BAT_SS                   (1u << 11)
#define BQ_BAT_PF                   (1u << 12)
#define BQ_BAT_SLEEP                (1u << 15)

/* Alarm Status / Enable (TRM Table 12-18) */
#define BQ_ALARM_WAKE               (1u << 0)
#define BQ_ALARM_ADSCAN             (1u << 1)
#define BQ_ALARM_CB                 (1u << 2)
#define BQ_ALARM_FULLSCAN           (1u << 7)
#define BQ_ALARM_INITCOMP           (1u << 9)
#define BQ_ALARM_MSK_PFALERT        (1u << 11)
#define BQ_ALARM_MSK_SFALERT        (1u << 12)
#define BQ_ALARM_PF                 (1u << 13)
#define BQ_ALARM_SSA                (1u << 14)
#define BQ_ALARM_SSBC               (1u << 15)
#define BQ_ALARM_SAFETY_MASK        0xF800u     /* SSBC|SSA|PF|MSK_SF|MSK_PF */

/* FET Status */
#define BQ_FET_ALRT_PIN             (1u << 6)

#endif /* BQ76952_REGS_H */
