/**
  ******************************************************************************
  * @file    bq76952_regs.h
  * @brief   BQ76952 direct commands, subcommands and data-memory addresses.
  ******************************************************************************
  */
#ifndef BQ76952_REGS_H
#define BQ76952_REGS_H

/* ---------------------------------------------------------------------------
 * DIRECT COMMANDS  - write 1 command byte, read 2 data bytes
 * ------------------------------------------------------------------------- */
#define BQ_CMD_CONTROL_STATUS       0x00u   /* [V] */
#define BQ_CMD_SAFETY_ALERT_A       0x02u   /* [V] */
#define BQ_CMD_SAFETY_STATUS_A      0x03u   /* [V] */
#define BQ_CMD_SAFETY_ALERT_B       0x04u   /* [V] */
#define BQ_CMD_SAFETY_STATUS_B      0x05u   /* [V] */
#define BQ_CMD_SAFETY_ALERT_C       0x06u   /* [V] */
#define BQ_CMD_SAFETY_STATUS_C      0x07u   /* [V] */
#define BQ_CMD_PF_ALERT_A           0x0Au   /* [?] */
#define BQ_CMD_PF_STATUS_A          0x0Bu   /* [?] */
#define BQ_CMD_PF_ALERT_B           0x0Cu   /* [?] */
#define BQ_CMD_PF_STATUS_B          0x0Du   /* [?] */
#define BQ_CMD_BATTERY_STATUS       0x12u   /* [V] */

#define BQ_CMD_CELL1_VOLTAGE        0x14u   /* [V] */
#define BQ_CMD_CELL_VOLTAGE(n)      (BQ_CMD_CELL1_VOLTAGE + 2u * ((n) - 1u))

#define BQ_CMD_STACK_VOLTAGE        0x34u   /* [V] userV units */
#define BQ_CMD_PACK_PIN_VOLTAGE     0x36u   /* [V] userV units */
#define BQ_CMD_LD_PIN_VOLTAGE       0x38u   /* [?] userV units */
#define BQ_CMD_CC2_CURRENT          0x3Au   /* [V] signed, userA units */

#define BQ_CMD_SUBCMD_LOW           0x3Eu   /* [V] subcommand address low  */
#define BQ_CMD_SUBCMD_HIGH          0x3Fu   /* [V] subcommand address high */
#define BQ_CMD_TRANSFER_BUFFER      0x40u   /* [V] 32-byte buffer 0x40-0x5F */
#define BQ_CMD_BUFFER_CHECKSUM      0x60u   /* [V] */
#define BQ_CMD_BUFFER_LENGTH        0x61u   /* [V] */

#define BQ_CMD_ALARM_STATUS         0x62u   /* [V] */
#define BQ_CMD_ALARM_RAW_STATUS     0x64u   /* [V] */
#define BQ_CMD_ALARM_ENABLE         0x66u   /* [V] */
#define BQ_CMD_INT_TEMPERATURE      0x68u   /* [V] */
#define BQ_CMD_CFETOFF_TEMPERATURE  0x6Au   /* [?] */
#define BQ_CMD_DFETOFF_TEMPERATURE  0x6Cu   /* [V] */
#define BQ_CMD_ALERT_TEMPERATURE    0x6Eu   /* [?] */
#define BQ_CMD_TS1_TEMPERATURE      0x70u   /* [V] */
#define BQ_CMD_TS2_TEMPERATURE      0x72u   /* [V] */
#define BQ_CMD_TS3_TEMPERATURE      0x74u   /* [V] */
#define BQ_CMD_HDQ_TEMPERATURE      0x76u   /* [?] */
#define BQ_CMD_DCHG_TEMPERATURE     0x78u   /* [?] */
#define BQ_CMD_DDSG_TEMPERATURE     0x7Au   /* [?] */
#define BQ_CMD_FET_STATUS           0x7Fu   /* [V] */

/* ---------------------------------------------------------------------------
 * SUBCOMMANDS  - write 2 bytes to 0x3E 
 * ------------------------------------------------------------------------- */
#define BQ_SUB_DEVICE_NUMBER        0x0001u /* [V] returns 0x7695 */
#define BQ_SUB_FW_VERSION           0x0002u /* [V] */
#define BQ_SUB_HW_VERSION           0x0003u /* [?] */
#define BQ_SUB_DASTATUS5            0x0075u /* [V] */
#define BQ_SUB_RESET                0x0012u /* [?] full device reset */
#define BQ_SUB_FET_ENABLE           0x0022u /* [V] toggles host FET control */
#define BQ_SUB_PF_ENABLE            0x0024u /* [V] */
#define BQ_SUB_PF_CLEAR             0x0029u /* [V] */
#define BQ_SUB_MANUFACTURING_STATUS 0x0057u /* [V] */
#define BQ_SUB_CB_ACTIVE_CELLS      0x0083u /* [V] 16-bit balancing bitmap */
#define BQ_SUB_CB_SET_LVL           0x0084u /* [?] */
#define BQ_SUB_SET_CFGUPDATE        0x0090u /* [V] enter CONFIG_UPDATE */
#define BQ_SUB_EXIT_CFGUPDATE       0x0092u /* [V] exit CONFIG_UPDATE  */
#define BQ_SUB_DSG_PDSG_OFF         0x0093u /* [V] */
#define BQ_SUB_CHG_PCHG_OFF         0x0094u /* [V] */
#define BQ_SUB_ALL_FETS_OFF         0x0095u /* [V] */
#define BQ_SUB_ALL_FETS_ON          0x0096u /* [V] */
#define BQ_SUB_SLEEP_ENABLE         0x0099u /* [?] */
#define BQ_SUB_SLEEP_DISABLE        0x009Au /* [?] */

/* ---------------------------------------------------------------------------
 * DATA MEMORY (RAM) ADDRESSES - written only in CONFIG_UPDATE mode
 * ------------------------------------------------------------------------- */
/* Calibration:Current */
#define BQ_REG_CC_GAIN              0x91A8u /* [V] F32 */
#define BQ_REG_CAPACITY_GAIN        0x91ACu /* [V] F32 */
#define BQ_REG_CC_OFFSET_SAMPLES    0x91C6u /* [V] U16 */
#define BQ_REG_BOARD_OFFSET         0x91C8u /* [V] I16 */

/* Settings:Protection / Configuration */
#define BQ_REG_ENABLED_PROT_A       0x9261u /* [?] */
#define BQ_REG_ENABLED_PROT_B       0x9262u /* [?] */
#define BQ_REG_ENABLED_PROT_C       0x9263u /* [?] */
#define BQ_REG_CUV_THRESHOLD        0x9275u /* [?] U8, 50.6 mV/LSB */
#define BQ_REG_CUV_DELAY            0x9276u /* [?] U16 */
#define BQ_REG_COV_THRESHOLD        0x9278u /* [?] U8, 50.6 mV/LSB */
#define BQ_REG_COV_DELAY            0x9279u /* [?] U16 */
#define BQ_REG_OCC_THRESHOLD        0x9280u /* [?] U8, 2 mV/LSB across shunt */
#define BQ_REG_OCC_DELAY            0x9281u /* [?] U8 */
#define BQ_REG_OCD1_THRESHOLD       0x9282u /* [?] U8, 2 mV/LSB */
#define BQ_REG_OCD1_DELAY           0x9283u /* [?] U8 */
#define BQ_REG_OCD2_THRESHOLD       0x9284u /* [?] U8, 2 mV/LSB */
#define BQ_REG_OCD2_DELAY           0x9285u /* [?] U8 */
#define BQ_REG_SCD_THRESHOLD        0x9286u /* [?] U8, enumerated code */
#define BQ_REG_SCD_DELAY            0x9287u /* [?] U8 */
#define BQ_REG_OTC_THRESHOLD        0x929Au /* [?] I8, degC */
#define BQ_REG_OTC_DELAY            0x929Bu /* [?] U8 */
#define BQ_REG_OTD_THRESHOLD        0x929Du /* [?] I8, degC */
#define BQ_REG_OTD_DELAY            0x929Eu /* [?] U8 */
#define BQ_REG_UTC_THRESHOLD        0x92A6u /* [?] I8, degC */
#define BQ_REG_UTC_DELAY            0x92A7u /* [?] U8 */
#define BQ_REG_UTD_THRESHOLD        0x92A9u /* [?] I8, degC */
#define BQ_REG_UTD_DELAY            0x92AAu /* [?] U8 */

/* Settings:Configuration - pin function assignment */
#define BQ_REG_CFETOFF_PIN_CONFIG   0x92FAu /* [V] */
#define BQ_REG_DFETOFF_PIN_CONFIG   0x92FBu /* [V] */
#define BQ_REG_ALERT_PIN_CONFIG     0x92FCu /* [V] */
#define BQ_REG_TS1_CONFIG           0x92FDu /* [V] */
#define BQ_REG_TS2_CONFIG           0x92FEu /* [V] */
#define BQ_REG_TS3_CONFIG           0x92FFu /* [V] */
#define BQ_REG_HDQ_PIN_CONFIG       0x9300u /* [V] */
#define BQ_REG_DCHG_PIN_CONFIG      0x9301u /* [V] */
#define BQ_REG_DDSG_PIN_CONFIG      0x9302u /* [V] */
#define BQ_REG_DA_CONFIGURATION     0x9303u /* [V] */
#define BQ_REG_VCELL_MODE           0x9304u /* [V] 0x0000 = all 16 cells */
#define BQ_REG_CC3_SAMPLES          0x9305u /* [?] */

/* Settings:Cell Balancing */
#define BQ_REG_BALANCING_CONFIG     0x9335u /* [?] */
#define BQ_REG_MFG_STATUS_INIT      0x0057u /* subcommand */

/* Pin config value for "use as thermistor input". */
#define BQ_PIN_CFG_THERMISTOR       0x07u
#define BQ_PIN_CFG_UNUSED           0x00u
/* ALERT pin as active-low interrupt output. */
#define BQ_PIN_CFG_ALERT_IRQ        0x2Au

/* ---------------------------------------------------------------------------
 * SAFETY STATUS BIT MASKS
 * ------------------------------------------------------------------------- */
/* Safety Status A (0x03) */
#define BQ_SSA_CUV                  (1u << 2)   /* cell undervoltage      */
#define BQ_SSA_COV                  (1u << 3)   /* cell overvoltage       */
#define BQ_SSA_OCC                  (1u << 4)   /* overcurrent in charge  */
#define BQ_SSA_OCD1                 (1u << 5)
#define BQ_SSA_OCD2                 (1u << 6)
#define BQ_SSA_SCD                  (1u << 7)   /* short circuit          */

/* Safety Status B (0x05) */
#define BQ_SSB_UTC                  (1u << 0)
#define BQ_SSB_UTD                  (1u << 1)
#define BQ_SSB_UTINT                (1u << 2)
#define BQ_SSB_OTC                  (1u << 4)
#define BQ_SSB_OTD                  (1u << 5)
#define BQ_SSB_OTINT                (1u << 6)
#define BQ_SSB_OTF                  (1u << 7)

/* Battery Status (0x12) */
#define BQ_BATSTAT_CFGUPDATE        (1u << 0)
#define BQ_BATSTAT_POR              (1u << 1)
#define BQ_BATSTAT_SLEEP            (1u << 2)
#define BQ_BATSTAT_FULLSCAN         (1u << 7)

#endif /* BQ76952_REGS_H */
