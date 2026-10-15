/*
* Copyright (c) 2020 - 2026 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

/***********************************************************************************************************************
 * Includes
 **********************************************************************************************************************/
#ifdef QE_TOUCH_CONFIGURATION
 #include "qe_touch_define.h"
#endif
#include "r_ctsu.h"
#include "r_ioport.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/** "CTSU" in ASCII, used to determine if device is open. */
#define CTSU_OPEN                              (0x43545355U)

/* Macro definitions for register setting */
#define CTSU_PON_OFF                           (0)    // CTSU hardware macro power off
#define CTSU_PON_ON                            (1)    // CTSU hardware macro power on
#define CTSU_CSW_OFF                           (0)    // Capacitance switch turned off
#define CTSU_CSW_ON                            (1)    // Capacitance switch turned on

#define CTSU_CR1_MODIFY_BIT                    (0xC8) // MD1, MD0, ATUNE1
#define CTSU_SOVF                              (0x20) // Overflow bit
#define CTSU_CORRECTION_AVERAGE                (32)
#define CTSU_SHIFT_AMOUNT                      (15)

#define CTSU_PCLKB_FREQ_MHZ                    (1000000)
#define CTSU_PCLKB_FREQ_RANGE1                 (32)
#define CTSU_PCLKB_FREQ_RANGE2                 (64)
#define CTSU_PCLKB_FREQ_RANGE3                 (128)
#define CTSU_WAIT_TIME                         (500)

/* Macro definitions for initial offset tuning */
#define CTSU_TUNING_MAX                        (0x03FF)
#define CTSU_TUNING_MIN                        (0x0000)
#define CTSU_TUNING_VALUE_SELF                 (15360)
#define CTSU_TUNING_VALUE_MUTUAL               (10240)

#define CTSU_CFG_DECIMAL_POINT                 (16)
#define CTSU_CFG_DECIMAL_POINT_MASK            (0x0000FFFF)

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #define CTSU_SST_RECOMMEND                    (0x1F) // The recommend value of SST
 #define CTSU_SST_RECOMMEND_CURRENT            (0x3F) // The recommend value of SST with current
 #define CTSU_SNUM_RECOMMEND                   (0x07) // The value of SNUM should be fixed
 #define CTSU_SNUM_MAX                         (0xFF) // The maximum value of SNUM
 #define CTSU_ICOMP0                           (0x80) // ICOMP0 bit
 #define CTSU_ICOMP1                           (0x40) // ICOMP1 bit
 #define CTSU_ICOMPRST                         (0x20) // ICOMPRST bit
 #define CTSU_CR0_MODIFY_BIT                   (0xC0) // TXVSEL
 #define CTSU_CR2_MODIFY_BIT                   (0x33) // POSEL, ATUNE2, MD2
 #define CTSU_SUADJ_MAX                        (0xFF) // The maximum value of SUADJx
 #define CTSU_SUADJ_SSCNT_ADJ                  (0x20) // The value of Adjusting SCADJx by SSCNT
 #define CTSU_MUTUAL_BUF_SIZE                  (CTSU_CFG_NUM_SUMULTI * 2)

/* Macro definitions for correction */
 #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
  #define CTSU_CORRECTION_STD_VAL              (19200) // 20UC standard value
  #define CTSU_CORRECTION_STD_UNIT             (1920)  // 2UC value
  #define CTSU_CORRECTION_STD_EXREG            (14400) // External registance standard value
  #define CTSU_CORRECTION_OFFSET_UNIT          (120)   // (7680 / 64)
 #else
  #define CTSU_CORRECTION_STD_VAL              (15360) // 20UC standard value * 0.8
  #define CTSU_CORRECTION_STD_UNIT             (1536)  // 2UC value * 0.8
  #define CTSU_CORRECTION_STD_EXREG            (11520) // External registance standard value
  #define CTSU_CORRECTION_OFFSET_UNIT          (96)    // (7680 / 64) * 0.8
 #endif
 #define CTSU_CORRECTION_SUMULTI               (0x20)  // SUMULTI step
 #define CTSU_CORRECTION_TRIMB_MAX             (0xFF)
 #define CTSU_CORRECTION_TRIMB_SIGN_BIT        (0x80)
 #define CTSU_CORRECTION_RTRIM_THRESHOLD1      (0xA0)
 #define CTSU_CORRECTION_RTRIM_THRESHOLD2      (0x50)
 #define CTSU_CORRECTION_TRIMB_THRESHOLD1      (0xC0)
 #define CTSU_CORRECTION_TRIMB_THRESHOLD2      (0x3F)
 #define CTSU_CORRECTION_BIT16                 (0x10000)
 #define CTSU_CORRECTION_BIT10                 (0x0400)
 #define CTSU_CORRECTION_BIT9                  (0x0200)
 #define CTSU_CORRECTION_BIT8                  (0x0100)
 #define CTSU_CORRECTION_BIT7                  (0x0080)
 #define CTSU_CORRECTION_BIT6_0                (0x007F)
 #define CTSU_CORRECTION_DIV_PRECISION         (12)

 #if (CTSU_CFG_NUM_CFC != 0)
  #define CTSU_CORRCFC_CENTER_POINT            ((CTSU_CORRCFC_POINT_NUM - 1) / 2) // number of center point
  #define CTSU_CORRCFC_TS_MAX                  (36)                               // Maximum number of TS terminal
  #define CTSU_CORRCFC_SHIFT8                  (8)                                // Definition of 8bit shift
 #endif

 #if (CTSU_CFG_CALIB_RTRIM_SUPPORT == 1)
  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_CALIB_REF                      ((6144000 * 10) / CTSU_CFG_VCC_MV) // 1.5V Reference value (4096 * 1500 * 10)
  #else
   #define CTSU_CALIB_REF                      ((4915200 * 10) / CTSU_CFG_VCC_MV) // 1.2V Reference value (4096 * 1200 * 10)
  #endif
  #define CTSU_CALIB_AVERAGE_TIME              (64)                               // ADC average time
  #define CTSU_CALIB_THRESHOLD                 ((0x1000 * 4) / CTSU_CFG_VCC_MV)   // RTRIM calib threshold
  #define CTSU_CALIB_CTSUSO                    (0x3C0)                            // 150uA offset
  #define CTSU_CALIB_ADSSTRL                   (0x3F)                             // Sampling time
 #endif

#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
 #define CTSU_TXVSEL                           (0x80) // TXVSEL bit
 #define CTSU_SST_RECOMMEND                    (0x10) // The value of SST should be fixed to 00010000b
 #define CTSU_SNUM_MAX                         (0x3F) // The maximum value of SNUM
 #define CTSU_SDPA_MAX                         (0x1F) // The maximum value of SDPA
 #define CTSU_PRRATIO_RECOMMEND                (3)    // Recommended setting value
 #define CTSU_PRMODE_62_PULSES                 (2)    // 62 pulses (recommended setting value)
 #define CTSU_SOFF_ON                          (0)    // High-pass noise reduction function turned on
 #define CTSU_SSMOD                            (0)    // The value of SSMOD should be fixed to 00b
 #define CTSU_SSCNT                            (3)    // The value of SSCNT should be fixed to 11b
 #define CTSU_RICOA_RECOMMEND                  (0x0F) // Recommended setting value
 #define CTSU_ICOG_100                         (0)    // ICOG = 100%
 #define CTSU_ICOG_66                          (1)    // ICOG = 66%
 #define CTSU_ICOG_50                          (2)    // ICOG = 50%
 #define CTSU_ICOG_40                          (3)    // ICOG = 40%
 #define CTSU_MUTUAL_BUF_SIZE                  (1)

/* Macro definitions for correction */
 #if (BSP_FEATURE_CTSU_CORRECTION_TYPE == 1)
  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_CORRECTION_1ST_STD_VAL         (40960UL)       // ICOG = 66%
   #define CTSU_CORRECTION_2ND_STD_VAL         (24824)         // ICOG = 40%, (x = 40960 * 40 / 66)
  #else
   #define CTSU_CORRECTION_1ST_STD_VAL         (32768UL)       // ICOG = 66%
   #define CTSU_CORRECTION_2ND_STD_VAL         (19859)         // ICOG = 40%, (x = 40960 * 40 / 66)
  #endif
  #define CTSU_WAFER_PARAMETER                 (0.96523525)
  #define CTSU_ICOG_RECOMMEND                  (CTSU_ICOG_66)  // Recommended setting value
 #endif
 #if (BSP_FEATURE_CTSU_CORRECTION_TYPE == 2)
  #define CTSU_CORRECTION_1ST_STD_VAL          (27306UL)       // ICOG = 66%, (x = 40960 * 66 / 100)
  #define CTSU_CORRECTION_2ND_STD_VAL          (16384)         // ICOG = 40%, (x = 40960 * 40 / 100)
  #define CTSU_WAFER_PARAMETER                 (1)
  #define CTSU_ICOG_RECOMMEND                  (CTSU_ICOG_100) // Recommended setting value
 #endif

 #define CTSU_CORRECTION_TRIMMING_OFFSET       (273)           // CCO trimming base offset
 #define CTSU_CORRECTION_CALCULATION_FACTOR    (9930)          // CCO correction calculation factor
 #define CTSU_CORRECTION_ROUNDING_OFFSET       (64)            // Fixed-point rounding (before /128)
#endif

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)

  #define CTSU_DIAG_DAC_1UC               (0x10)  // 0x10 for so dac value
  #define CTSU_DIAG_DAC_2UC               (0x20)  // 0x20 for so dac value
  #define CTSU_DIAG_DAC_4UC               (0x40)  // 0x40 for so dac value
  #define CTSU_DIAG_DAC_8UC               (0x80)  // 0x80 for so dac value
  #define CTSU_DIAG_DAC_16UC              (0x100) // 0x100 for so dac value

  #define CTSU_DIAG_DAC_SO_MAX            (0x3FF) // so dac max

  #define CTSU_DIAG_DAC_INIT_VALUE        (241)   // SO value of dac test
  #define CTSU_DIAG_DAC_TARGET_VALUE      (15360) // 6UC value dac test target
  #define CTSU_DIAG_DAC_START_VALUE       (0x100) // so value dac test tuning
  #define CTSU_DAC_TEST_ATUNE1            (0x08)  // ATUNE1 bit 1

  #if (BSP_FEATURE_CTSU_CORRECTION_TYPE == 1)
   #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
    #define CTSU_CFG_DIAG_CCO_HIGH_MAX    (54888)
    #define CTSU_CFG_DIAG_CCO_HIGH_MIN    (29062)
    #define CTSU_CFG_DIAG_CCO_LOW_MAX     (3269)
    #define CTSU_CFG_DIAG_CCO_LOW_MIN     (705)
    #define CTSU_CFG_DIAG_SSCG_MAX        (21813)
    #define CTSU_CFG_DIAG_SSCG_MIN        (11782)
    #define CTSU_CFG_DIAG_DAC1_MAX        (20422)
    #define CTSU_CFG_DIAG_DAC2_MAX        (21532)
    #define CTSU_CFG_DIAG_DAC3_MAX        (23015)
    #define CTSU_CFG_DIAG_DAC4_MAX        (25923)
    #define CTSU_CFG_DIAG_DAC5_MAX        (32016)
    #define CTSU_CFG_DIAG_DAC6_MAX        (45795)
    #define CTSU_CFG_DIAG_DAC1_MIN        (12492)
    #define CTSU_CFG_DIAG_DAC2_MIN        (14053)
    #define CTSU_CFG_DIAG_DAC3_MIN        (15322)
    #define CTSU_CFG_DIAG_DAC4_MIN        (18070)
    #define CTSU_CFG_DIAG_DAC5_MIN        (23262)
    #define CTSU_CFG_DIAG_DAC6_MIN        (33446)

   #else
    #define CTSU_CFG_DIAG_CCO_HIGH_MAX    (43910)
    #define CTSU_CFG_DIAG_CCO_HIGH_MIN    (23249)
    #define CTSU_CFG_DIAG_CCO_LOW_MAX     (2615)
    #define CTSU_CFG_DIAG_CCO_LOW_MIN     (564)
    #define CTSU_CFG_DIAG_SSCG_MAX        (21813)
    #define CTSU_CFG_DIAG_SSCG_MIN        (11782)
    #define CTSU_CFG_DIAG_DAC1_MAX        (16599)
    #define CTSU_CFG_DIAG_DAC2_MAX        (17226)
    #define CTSU_CFG_DIAG_DAC3_MAX        (18412)
    #define CTSU_CFG_DIAG_DAC4_MAX        (20738)
    #define CTSU_CFG_DIAG_DAC5_MAX        (25613)
    #define CTSU_CFG_DIAG_DAC6_MAX        (36636)
    #define CTSU_CFG_DIAG_DAC1_MIN        (9994)
    #define CTSU_CFG_DIAG_DAC2_MIN        (11242)
    #define CTSU_CFG_DIAG_DAC3_MIN        (12258)
    #define CTSU_CFG_DIAG_DAC4_MIN        (14456)
    #define CTSU_CFG_DIAG_DAC5_MIN        (18610)
    #define CTSU_CFG_DIAG_DAC6_MIN        (26757)
   #endif
  #endif
  #if (BSP_FEATURE_CTSU_CORRECTION_TYPE == 2)
   #define CTSU_CFG_DIAG_CCO_HIGH_MAX     (36873)
   #define CTSU_CFG_DIAG_CCO_HIGH_MIN     (24433)
   #define CTSU_CFG_DIAG_CCO_LOW_MAX      (1781)
   #define CTSU_CFG_DIAG_CCO_LOW_MIN      (105)
   #define CTSU_CFG_DIAG_SSCG_MAX         (17795)
   #define CTSU_CFG_DIAG_SSCG_MIN         (9610)
   #define CTSU_CFG_DIAG_DAC1_MAX         (20422)
   #define CTSU_CFG_DIAG_DAC2_MAX         (21532)
   #define CTSU_CFG_DIAG_DAC3_MAX         (23015)
   #define CTSU_CFG_DIAG_DAC4_MAX         (25923)
   #define CTSU_CFG_DIAG_DAC5_MAX         (32016)
   #define CTSU_CFG_DIAG_DAC6_MAX         (45795)
   #define CTSU_CFG_DIAG_DAC1_MIN         (12492)
   #define CTSU_CFG_DIAG_DAC2_MIN         (14053)
   #define CTSU_CFG_DIAG_DAC3_MIN         (15322)
   #define CTSU_CFG_DIAG_DAC4_MIN         (18070)
   #define CTSU_CFG_DIAG_DAC5_MIN         (23262)
   #define CTSU_CFG_DIAG_DAC6_MIN         (33446)
  #endif
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
  #define CTSU_DIAG_ADC_MEASUREMENT_ERROR                       (CTSU_DIAG_ADC_OFFSET_ERR_12B + \
                                                                 CTSU_DIAG_ADC_ABS_ACC_ERR_12B)
  #define CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_SELECT           (0x0)                               // Select low  unit of DAC lower current source for diagnosis
  #define CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_SELECT          (0x100)                             // Select high unit of DAC lower current source for diagnosis
  #define CTSU_DIAG_DAC_UPPER_CURRENT_LOW_UNIT_SELECT           (0x200)                             // Select low  unit of DAC upper current source for diagnosis
  #define CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT          (0x300)                             // Select high unit of DAC upper current source for diagnosis

  #define CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_DATA_MAX         (0x0FF)                             // Max value of low  unit of DAC lower current source for diagnosis
  #define CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_DATA_MAX        (0x003)                             // Max value of high unit of DAC lower current source for diagnosis

  #define CTSU_DIAG_SUCLK_LOWER_CURRENT_SELECT                  (0x80)                              // Select lower current source for SUCLK gain diagnosis
  #define CTSU_DIAG_SUCLK_UPPER_CURRENT_SELECT                  (0x0)                               // Select upper current source for SUCLK gain diagnosis

  #define CTSU_DIAG_CLOCK_RECOVERY_SST_RECOMMEND                (0x3F)                              // Recommended setting value of SST for clock recovery diagnosis

  #define CTSU_DIAG_CHACA_TSMAX                                 (32)                                // ts max chaca byte

  #define CTSU_DIAG_CCODAC_GAIN_1_UNIT                          (0x1F)                              // Turn on 1 unit of CCODAC current source
  #define CTSU_DIAG_CCODAC_GAIN_8_UNIT                          (0xFF)                              // Turn on 8 unit of CCODAC current source

  #define CTSU_DIAG_OUTPUT_VOLTAGE_TEST_NUM                     (8)                                 // Number of diagnostic tests for output voltage
  #define CTSU_DIAG_OVER_VOLTAGE_TEST_NUM                       (2)                                 // Number of diagnostic tests for over voltage
  #define CTSU_DIAG_OVER_CURRENT_TEST_NUM                       (2)                                 // Number of diagnostic tests for over current
  #define CTSU_DIAG_LOAD_RESISTANCE_TEST_NUM                    (5)                                 // Number of diagnostic tests for load resistance
  #define CTSU_DIAG_CURRENT_SOURCE_TEST_NUM                     (27)                                // Number of diagnostic tests for current source
  #define CTSU_DIAG_SENSCLK_TEST_NUM                            (12)                                // Number of diagnostic tests for sensclk
  #define CTSU_DIAG_SUCLK_TEST_NUM                              (24)                                // Number of diagnostic tests for suclk

  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_SUCLK_MAX_MHZ                                   (BSP_FEATURE_CTSU_SUCLK_MAX_MHZ)    // Maximum SUCLK frequency [MHz] (NM mode)
  #else
   #define CTSU_SUCLK_MAX_MHZ                                   (BSP_FEATURE_CTSU_SUCLK_LV_MAX_MHZ) // Maximum SUCLK frequency [MHz] (LV mode)
  #endif

  #if (CTSU_SUCLK_MAX_MHZ > 32)
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST_NUM                    (4)                                 // Number of diagnostic tests for clock recovery
  #else
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST_NUM                    (3)                                 // Number of diagnostic tests for clock recovery
  #endif

  #define CTSU_DIAG_CFC_TEST_NUM                                (5)                                 // Number of diagnostic tests for CFC

  #define CTSU_DIAG_AVERAGE_SHIFT_NUM                           (2)                                 // Number of shifts required to achieve a 4-time average
  #define CTSU_DIAG_AVERAGE_NUM                                 (4)                                 // Number of samples used for averaging in CTSU diagnosis
  #define CTSU_DIAG_PERCENT_BASE                                (100)                               // Reference value for percentage calculation

/* Over voltage diagnosis macro */
  #define CTSU_DIAG_OFFSET_CURRENT_80UA                         (0x0200)                            // SO[9:0] value for 80uA offset current

/* Over voltage and Over current diagnosis macro */
  #define CTSU_DIAG_OFFSET_CURRENT_0UA                          (0x0000)                            // SO[9:0] value for 0uA offset current

/* TSCAP output voltage diagnosis macro */
  #if (BSP_FEATURE_ADC_D_IS_AVAILABLE == 1)
   #define CTSU_ADC_VOLTAGE_MEASUREMENT_CHANNEL                 ADC_CHANNEL_TSCAP_VOLT
  #else
   #define CTSU_ADC_VOLTAGE_MEASUREMENT_CHANNEL                 ADC_CHANNEL_16
  #endif

  #define CTSU_DIAG_INTERNAL_VREF_TYP_MV                        (1480L) // Internal reference voltage theoretical value [mV]

  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_DIAG_TSCAP_VREF_TYP_MV                          (1500L) // TSCAP voltage theoretical value [mV] (NM mode)
  #else
   #define CTSU_DIAG_TSCAP_VREF_TYP_MV                          (1200L) // TSCAP voltage theoretical value [mV] (LV mode)
  #endif

/* Load resistance diagnosis macro */
  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_DIAG_LOAD_RESISTANCE_THEORETICAL_COUNT_VALUE    (19200L) // Theoretical count value of load resistance (NM mode)
   #define CTSU_DIAG_IDLE_CURRENT_THEORETICAL_COUNT_VALUE       (1920L)  // Theoretical count value of IDLE current (NM mode)
  #else
   #define CTSU_DIAG_LOAD_RESISTANCE_THEORETICAL_COUNT_VALUE    (15360L) // Theoretical count value of load resistance (LV mode)
   #define CTSU_DIAG_IDLE_CURRENT_THEORETICAL_COUNT_VALUE       (1536L)  // Theoretical count value of IDLE current (LV mode)
  #endif
  #define CTSU_DIAG_LOAD_RESISTANCE_VARIATION_SCALE_SHIFT       (9)

/* Current source diagnosis macro */
  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_DIAG_UPPER_CURRENT_SOURCE_DIFF_VALUE            (7680L)
   #define CTSU_DIAG_LOWER_CURRENT_SOURCE_DIFF_VALUE            (960L)
  #else
   #define CTSU_DIAG_UPPER_CURRENT_SOURCE_DIFF_VALUE            (6144L)
   #define CTSU_DIAG_LOWER_CURRENT_SOURCE_DIFF_VALUE            (768L)
  #endif

/* SENSCLK gain and SUCLK gain (DWA) macro */
  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)

/* Minimum threshold for differential value (NM mode) */
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_2_1_MIN              (1048)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_3_2_MIN              (1004)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_4_3_MIN              (964)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_5_4_MIN              (928)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_6_5_MIN              (890)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_7_6_MIN              (854)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_8_7_MIN              (812)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_9_8_MIN              (770)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_10_9_MIN             (714)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_11_10_MIN            (656)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_12_11_MIN            (598)

/* Maximum threshold for differential value (NM mode) */
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_2_1_MAX              (3218)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_3_2_MAX              (3018)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_4_3_MAX              (2922)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_5_4_MAX              (2788)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_6_5_MAX              (2696)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_7_6_MAX              (2600)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_8_7_MAX              (2530)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_9_8_MAX              (2436)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_10_9_MAX             (2350)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_11_10_MAX            (2262)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_12_11_MAX            (2180)

  #else

/* Minimum threshold for differential value (LV mode) */
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_2_1_MIN              (834)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_3_2_MIN              (815)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_4_3_MIN              (794)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_5_4_MIN              (772)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_6_5_MIN              (749)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_7_6_MIN              (724)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_8_7_MIN              (699)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_9_8_MIN              (671)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_10_9_MIN             (643)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_11_10_MIN            (613)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_12_11_MIN            (582)

/* Maximum threshold for differential value (LV mode)*/
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_2_1_MAX              (2600)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_3_2_MAX              (2494)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_4_3_MAX              (2400)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_5_4_MAX              (2318)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_6_5_MAX              (2244)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_7_6_MAX              (2178)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_8_7_MAX              (2118)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_9_8_MAX              (2062)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_10_9_MAX             (2010)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_11_10_MAX            (1958)
   #define CTSU_CFG_DIAG_CCO_GAIN_DIFF_12_11_MAX            (1905)
  #endif

/* SUCLK gain (NO_DWA) macro */
  #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
   #define CTSU_DIAG_NO_DWA_SUCLK_GAIN_MAX_VALUE            (3709L) // Maximum threshold without DWA
   #define CTSU_DIAG_NO_DWA_SUCLK_GAIN_MIN_VALUE            (763L)  // Minimum threshold without DWA
  #else
   #define CTSU_DIAG_NO_DWA_SUCLK_GAIN_MAX_VALUE            (3142L) // Maximum threshold without DWA
   #define CTSU_DIAG_NO_DWA_SUCLK_GAIN_MIN_VALUE            (547L)  // Minimum threshold without DWA
  #endif

/* SUCLK clock recovery macro */
  #define CTSU_DIAG_CLOCK_RECOVERY_FREQUENCY_32MHZ_SHIFT    (5)     // Number of shifts required to calculate the frequency ratio for 32 MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST1_FREQUENCY_MHZ      (16)    // Clock recovery TEST1 frequency: 16MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST2_FREQUENCY_MHZ      (24)    // Clock recovery TEST2 frequency: 24MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST1_FREQUENCY          (31)    // Clock recovery TEST1 with SUCLK frequency set to 16MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST2_FREQUENCY          (47)    // Clock recovery TEST2 with SUCLK frequency set to 24MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST1_MIN_VALUE          (4092)  // Minimum threshold for clock recovery TEST1 at 16MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST1_MAX_VALUE          (4100)  // Maximum threshold for clock recovery TEST1 at 16MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST2_MIN_VALUE          (6140)  // Minimum threshold for clock recovery TEST2 at 24MHz
  #define CTSU_DIAG_CLOCK_RECOVERY_TEST2_MAX_VALUE          (6148)  // Maximum threshold for clock recovery TEST2 at 24MHz

  #if (CTSU_SUCLK_MAX_MHZ >= 32)
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_FREQUENCY_MHZ     (32)    // Clock recovery TEST3 frequency: 32MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_FREQUENCY         (63)    // Clock recovery TEST3 with SUCLK frequency set to 32MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_MIN_VALUE         (8188)  // Minimum threshold for clock recovery TEST3 at 32MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_MAX_VALUE         (8196)  // Maximum threshold for clock recovery TEST3 at 32MHz

  #elif (CTSU_SUCLK_MAX_MHZ == 30)
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_FREQUENCY_MHZ     (30)    // Clock recovery TEST3 frequency: 30MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_FREQUENCY         (59)    // Clock recovery TEST3 with SUCLK frequency set to 30MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_MIN_VALUE         (7676)  // Minimum threshold for clock recovery TEST3 at 30MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST3_MAX_VALUE         (7684)  // Maximum threshold for clock recovery TEST3 at 30MHz
  #endif

  #if (CTSU_SUCLK_MAX_MHZ == 40)
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST4_FREQUENCY_MHZ     (40)    // Clock recovery TEST4 frequency: 40MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST4_FREQUENCY         (79)    // Clock recovery TEST4 with SUCLK frequency set to 40MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST4_MIN_VALUE         (10234) // Minimum threshold for clock recovery TEST4 at 40MHz
   #define CTSU_DIAG_CLOCK_RECOVERY_TEST4_MAX_VALUE         (10246) // Maximum threshold for clock recovery TEST4 at 40MHz
  #endif
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
  #define CTSU_TRANSFER_TUNING_CH_REG_MAX_NUM    (32)
 #endif
#endif

#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
 #define CTSU_SERIAL_TUNING_ELEMENT              (1) // To avoid reading outside the array in the case of mutual 1 x N configuration
#else
 #define CTSU_SERIAL_TUNING_ELEMENT              (0) // No additional buffer is required
#endif

#if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
 #define CTSU_AUTO_JUDGE_ELEMENT_NUM             (CTSU_CFG_NUM_AUTOJUDGE_SELF_ELEMENTS + \
                                                  CTSU_CFG_NUM_AUTOJUDGE_MUTUAL_ELEMENTS)
 #define CTSU_AUTO_FINAL_JUDGE_BIT               (4)          // Definition for bit-shifting the final judgement bit of the automatic judgement result register
 #define CTSU_AUTO_MINUS_BIT_MASK                (0x0000FFFF) // Mask to assign to the CTSUAJTHR register
#endif

#if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
 #define CTSU_AUTO_DECIMAL_POINT_NUM             (12)         // Number of bits after the decimal point of the sensor counter auto correction table register
 #define CTSU_AUTO_DECIMAL_BIT_MASK              (0xFFF)      // Decimal point setting for SCNTACCOEFF (b11-b0)
#endif
#if (CTSU_CFG_MULTIPLE_ELECTRODE_CONNECTION_ENABLE == 1)
 #define CTSU_MEC_BIT6_MASK                      (0x3F)
#endif

#if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
 #define CTSU_AUTO_CURRENT_MODE_20UA             (20)
 #define CTSU_AUTO_CURRENT_MODE_40UA             (40)
 #define CTSU_AUTO_CURRENT_MODE_80UA             (80)
 #define CTSU_AUTO_CURRENT_MODE_160UA            (160)
 #define CTSU_AUTO_REF_COEFFICIENT               (15)  // RefFullScaleCount / OffsetDAC Resolution (15360 / 1024)
 #if (CTSU_CFG_LOW_VOLTAGE_MODE == 0)
  #define CTSU_AUTO_CORRECTION_OFFSET_DAC_MAX    (160) // OffsetDAC maximum output current [uA]
 #else
  #define CTSU_AUTO_CORRECTION_OFFSET_DAC_MAX    (128) // 160 * 0.8
 #endif
 #define CTSU_AUTO_INT32_OVERFLOW_VALUE          (2147483648)
 #define CTSU_AUTO_INT16_OVERFLOW_VALUE          (32767)
 #define CTSU_AUTO_INT16_UNDERFLOW_VALUE         (-32768)
#endif

/***********************************************************************************************************************
 * Typedef definitions
 ***********************************************************************************************************************/
typedef struct st_ctsu_correction_calc
{
    ctsu_range_t range;
    uint16_t     snum;
    uint16_t     sdpa;
    uint16_t     cfc;
    ctsu_md_t    md;
} ctsu_correction_calc_t;

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
typedef struct st_ctsu_diag_save_reg
{
    uint8_t  ctsucr0;
    uint8_t  ctsucr1;
    uint8_t  ctsusdprs;
    uint8_t  ctsusst;
    uint8_t  ctsuchac0;
    uint8_t  ctsuchac1;
    uint8_t  ctsuchac2;
    uint8_t  ctsuchtrc0;
    uint8_t  ctsuchtrc1;
    uint8_t  ctsuchtrc2;
    uint8_t  ctsudclkc;
    uint16_t ctsuerrs;
} ctsu_diag_save_reg_t;
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
typedef struct st_ctsu_diag_save_reg
{
    uint32_t ctsucra;
    uint32_t ctsucrb;
    uint32_t ctsucalib;
    uint32_t ctsusuclka;
    uint32_t ctsuopt;
} ctsu_diag_save_reg_t;
 #endif
#endif

#if defined(__ARMCC_VERSION) || defined(__ICCARM__)
typedef void (BSP_CMSE_NONSECURE_CALL * ctsu_prv_ns_callback)(ctsu_callback_args_t * p_args);
#elif defined(__GNUC__)
typedef BSP_CMSE_NONSECURE_CALL void (*volatile ctsu_prv_ns_callback)(ctsu_callback_args_t * p_args);
#endif

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/
#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
static fsp_err_t ctsu_transfer_open(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_close(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_configure(ctsu_instance_ctrl_t * const p_instance_ctrl);

 #if (BSP_FEATURE_CTSU_VERSION == 2)
static fsp_err_t ctsu_transfer_normal(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_normal_ctsuwr(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_normal_ctsurd(ctsu_instance_ctrl_t * const p_instance_ctrl);

  #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
static fsp_err_t ctsu_transfer_autojudge(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_autojudge_ctsuwr(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_autojudge_ctsurd(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void      ctsu_transfer_autojudge_ctsuwr_value_set(transfer_info_t * p_info,
                                                          uint8_t           array_number,
                                                          uint16_t          set_transfer_count);
static void ctsu_transfer_autojudge_ctsuso_set(transfer_info_t * p_info,
                                               uint8_t           array_number,
                                               uint16_t          set_transfer_count);
static void ctsu_transfer_autojudge_ctsurd_value_set(transfer_info_t * p_info,
                                                     uint8_t           array_number,
                                                     uint16_t          set_transfer_count);
static void ctsu_transfer_autojudge_ctsuscnt_set(transfer_info_t * p_info,
                                                 uint8_t           array_number,
                                                 uint16_t          set_transfer_count);

  #endif
  #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
static void ctsu_transfer_mcact_set(transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count);

  #endif
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
static void ctsu_transer_count_element(uint32_t element_mask, uint16_t * num_element);

  #endif
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
static fsp_err_t ctsu_transfer_ctsu1(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_ctsu1_ctsuwr(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_transfer_ctsu1_ctsurd(ctsu_instance_ctrl_t * const p_instance_ctrl);

 #endif
static void ctsu_transfer_ctsuso_set(transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count);
static void ctsu_transfer_ctsuscnt_set(transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count);
static void ctsu_transfer_address_set(transfer_info_t * p_info,
                                      uint8_t           array_number,
                                      void            * p_set_source_addr,
                                      void            * p_set_dest_addr);

#endif
static void ctsu_initial_offset_tuning(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_moving_average(ctsu_data_t * p_average, uint16_t new_data, uint16_t average_num);
void        ctsu_write_isr(void);
void        ctsu_read_isr(void);
void        ctsu_end_isr(void);
void        ctsu_end_interrupt(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_correction_process(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_correction_measurement(ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * data);

static void ctsu_correction_exec(ctsu_instance_ctrl_t * const p_instance_ctrl);

#if ((CTSU_CFG_AUTO_CORRECTION_ENABLE == 0) || (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1))
static void ctsu_correction_calc(uint16_t * correction_data, uint16_t raw_data, ctsu_correction_calc_t * p_calc);

#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
static void ctsu_correction_ctsu1_exec(ctsu_instance_ctrl_t * const p_instance_ctrl);

#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
static void ctsu_correction_ctsu2_exec(ctsu_instance_ctrl_t * const p_instance_ctrl);

#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)
static void ctsu_correction_freq(uint16_t * p_corr, uint32_t * p_so_value, uint16_t * p_mfc);

 #endif
static uint8_t ctsu_correction_multi(uint16_t * p_pri_mfc,
                                     uint16_t * p_snd_mfc,
                                     uint16_t * p_pri_data,
                                     uint16_t * p_snd_data);

 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
static void      ctsu_correction_scan_start(void);
static fsp_err_t ctsu_correction_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * p_data);

  #if (CTSU_CFG_CALIB_RTRIM_SUPPORT == 1)
static fsp_err_t ctsu_correction_calib_rtrim(ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * p_data);

  #endif
 #endif
 #if (CTSU_CFG_NUM_CFC != 0)
static void ctsu_corrcfc_process(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_corrcfc_measurement(ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * data, uint8_t point_num);

 #endif

 #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
static void ctsu_auto_correction_register_set(ctsu_instance_ctrl_t * const p_instance_ctrl);

 #endif

 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
static void ctsu_auto_judge_threshold_calc(ctsu_instance_ctrl_t * const p_instance_ctrl);

 #endif

 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
static void    ctsu_multiclock_auto_correction_register_set(ctsu_instance_ctrl_t * const p_instance_ctrl);
static int16_t ctsu_multiclock_auto_correction_calc(ctsu_instance_ctrl_t * const p_instance_ctrl,
                                                    uint16_t                     element_id,
                                                    uint8_t                      freq);

 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
static void ctsu_diag_dac_initial_tuning(void);

static void ctsu_diag_ldo_over_voltage_scan_start(void);
static void ctsu_diag_oscillator_high_scan_start(void);
static void ctsu_diag_oscillator_low_scan_start(void);
static void ctsu_diag_sscg_scan_start(void);
static void ctsu_diag_dac_scan_start(ctsu_instance_ctrl_t * const p_instance_ctrl);

static void ctsu_diag_ldo_over_voltage_data_get(void);
static void ctsu_diag_oscillator_high_data_get(void);
static void ctsu_diag_oscillator_low_data_get(void);
static void ctsu_diag_sscg_data_get(void);
static void ctsu_diag_dac_data_get(void);

static fsp_err_t ctsu_diag_ldo_over_voltage_result(void);
static fsp_err_t ctsu_diag_oscillator_high_result(void);
static fsp_err_t ctsu_diag_oscillator_low_result(void);
static fsp_err_t ctsu_diag_sscg_result(void);
static fsp_err_t ctsu_diag_dac_result(void);

static void      ctsu_diag_scan_start1(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_diag_data_get1(void);

 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)

static void ctsu_diag_regi_store2(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_regi_restore2(void);

static void      ctsu_diag_output_voltage_scan_start(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_diag_adc_measure_average(adc_instance_t const * p_adc_instance,
                                               adc_channel_t          channel,
                                               uint16_t             * adc_measured_value);
static uint16_t ctsu_diag_calc_threshold_output_voltage(bool threshold_type, uint16_t internal_vref_value);

static void ctsu_diag_over_voltage_scan_start(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_over_voltage_data_get(void);

static void ctsu_diag_over_current_scan_start(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_over_current_data_get(void);

static void ctsu_diag_load_resistance_scan_start(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_load_resistance_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl);

static void ctsu_diag_current_source_scan_start(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_current_source_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl);

static void ctsu_diag_sensclk_gain_scan_start(void);
static void ctsu_diag_sensclk_gain_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_cco_gain_config_test(void);

static void ctsu_diag_suclk_gain_scan_start(void);
static void ctsu_diag_suclk_gain_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl);
static void ctsu_diag_suclk_gain_config_test(void);

static void ctsu_diag_clock_recovery_scan_start(void);
static void ctsu_diag_clock_recovery_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl);

  #if (CTSU_CFG_NUM_CFC != 0)
static void ctsu_diag_cfc_gain_scan_start(void);
static void ctsu_diag_cfc_gain_data_get(ctsu_instance_ctrl_t * const p_instance_ctrl);

  #endif

static void      ctsu_diag_scan_start2(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_diag_data_get2(ctsu_instance_ctrl_t * const p_instance_ctrl);
static fsp_err_t ctsu_diag_adc_open_check(void);

 #endif
#endif

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static uint16_t g_ctsu_element_index = 0;
static uint8_t  g_ctsu_element_complete_flag[CTSU_CFG_NUM_SELF_ELEMENTS + CTSU_CFG_NUM_MUTUAL_ELEMENTS];
#if (BSP_FEATURE_CTSU_VERSION == 2)
static uint8_t g_ctsu_frequency_complete_flag[CTSU_CFG_NUM_SELF_ELEMENTS + CTSU_CFG_NUM_MUTUAL_ELEMENTS];
#endif
static int32_t       g_ctsu_tuning_diff[CTSU_CFG_NUM_SELF_ELEMENTS + CTSU_CFG_NUM_MUTUAL_ELEMENTS];
static ctsu_ctsuwr_t g_ctsu_ctsuwr[(CTSU_CFG_NUM_SELF_ELEMENTS + CTSU_CFG_NUM_MUTUAL_ELEMENTS +
                                    CTSU_SERIAL_TUNING_ELEMENT) * CTSU_CFG_NUM_SUMULTI];
#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
static uint16_t        g_ctsu_self_element_index = 0;
static ctsu_self_buf_t g_ctsu_self_raw[CTSU_CFG_NUM_SELF_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static uint16_t        g_ctsu_self_corr[CTSU_CFG_NUM_SELF_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static uint16_t        g_ctsu_self_mfc[CTSU_CFG_NUM_SELF_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static ctsu_data_t     g_ctsu_self_data[CTSU_CFG_NUM_SELF_ELEMENTS * CTSU_MAJORITY_MODE_ELEMENTS];
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
static uint16_t          g_ctsu_mutual_element_index = 0;
static ctsu_mutual_buf_t g_ctsu_mutual_raw[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_MUTUAL_BUF_SIZE];
static uint16_t          g_ctsu_mutual_pri_corr[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static uint16_t          g_ctsu_mutual_snd_corr[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static uint16_t          g_ctsu_mutual_pri_mfc[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static uint16_t          g_ctsu_mutual_snd_mfc[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_CFG_NUM_SUMULTI];
static ctsu_data_t       g_ctsu_mutual_pri_data[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_MAJORITY_MODE_ELEMENTS];
static ctsu_data_t       g_ctsu_mutual_snd_data[CTSU_CFG_NUM_MUTUAL_ELEMENTS * CTSU_MAJORITY_MODE_ELEMENTS];
#endif
static ctsu_correction_info_t g_ctsu_correction_info;

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
static ctsu_diag_info_t     g_ctsu_diag_info;
static ctsu_diag_save_reg_t g_ctsu_diag_reg;
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_NUM_CFC != 0)
static ctsu_corrcfc_info_t g_ctsu_corrcfc_info;
 #endif

 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
static ctsu_diag_info_t     g_ctsu_diag_info;
static ctsu_diag_save_reg_t g_ctsu_diag_reg;
 #endif
 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
static uint32_t g_ctsu_temp_reg_ctsucra;
 #endif
 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
static uint32_t g_ctsu_ajthr[CTSU_AUTO_JUDGE_ELEMENT_NUM * CTSU_MAJORITY_MODE_ELEMENTS];
static uint32_t g_ctsu_ajmmar[CTSU_AUTO_JUDGE_ELEMENT_NUM * CTSU_MAJORITY_MODE_ELEMENTS];
static uint32_t g_ctsu_ajblact[CTSU_AUTO_JUDGE_ELEMENT_NUM * CTSU_MAJORITY_MODE_ELEMENTS];
static uint32_t g_ctsu_ajblar[CTSU_AUTO_JUDGE_ELEMENT_NUM * CTSU_MAJORITY_MODE_ELEMENTS];
static uint32_t g_ctsu_ajrr[CTSU_AUTO_JUDGE_ELEMENT_NUM * CTSU_MAJORITY_MODE_ELEMENTS];
 #endif
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
static uint32_t g_ctsu_mcact1[(CTSU_CFG_NUM_SELF_ELEMENTS + CTSU_CFG_NUM_MUTUAL_ELEMENTS) * CTSU_CFG_NUM_SUMULTI];
static uint32_t g_ctsu_mcact2[(CTSU_CFG_NUM_SELF_ELEMENTS + CTSU_CFG_NUM_MUTUAL_ELEMENTS) * CTSU_CFG_NUM_SUMULTI];
 #endif

 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
uint8_t g_ctsu_selected_freq_self[CTSU_CFG_NUM_SELF_ELEMENTS];
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
uint8_t g_ctsu_selected_freq_mutual[CTSU_CFG_NUM_MUTUAL_ELEMENTS];
 #endif
#endif

static ioport_instance_ctrl_t g_ctsu_tscap_ioport_ctrl;
static ioport_pin_cfg_t       g_ctsu_tscap_pin_cfg_data =
{
    .pin = (bsp_io_port_pin_t) CTSU_CFG_TSCAP_PORT,
};
static const ioport_cfg_t g_ctsu_tscap_pin_cfg =
{
    .number_of_pins = 1,
    .p_pin_cfg_data = &g_ctsu_tscap_pin_cfg_data,
};

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
static const uint16_t dac_oscil_table[6][2] =
{
    {CTSU_CFG_DIAG_DAC1_MAX, CTSU_CFG_DIAG_DAC1_MIN},
    {CTSU_CFG_DIAG_DAC2_MAX, CTSU_CFG_DIAG_DAC2_MIN},
    {CTSU_CFG_DIAG_DAC3_MAX, CTSU_CFG_DIAG_DAC3_MIN},
    {CTSU_CFG_DIAG_DAC4_MAX, CTSU_CFG_DIAG_DAC4_MIN},
    {CTSU_CFG_DIAG_DAC5_MAX, CTSU_CFG_DIAG_DAC5_MIN},
    {CTSU_CFG_DIAG_DAC6_MAX, CTSU_CFG_DIAG_DAC6_MIN},
};
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)

static const uint16_t cco_gain_threshold_diff_table[11][2] =
{
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_2_1_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_2_1_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_3_2_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_3_2_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_4_3_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_4_3_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_5_4_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_5_4_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_6_5_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_6_5_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_7_6_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_7_6_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_8_7_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_8_7_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_9_8_MIN,   CTSU_CFG_DIAG_CCO_GAIN_DIFF_9_8_MAX        },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_10_9_MIN,  CTSU_CFG_DIAG_CCO_GAIN_DIFF_10_9_MAX       },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_11_10_MIN, CTSU_CFG_DIAG_CCO_GAIN_DIFF_11_10_MAX      },
    {CTSU_CFG_DIAG_CCO_GAIN_DIFF_12_11_MIN, CTSU_CFG_DIAG_CCO_GAIN_DIFF_12_11_MAX      },
};

static const uint16_t clock_recovery_threshold_table[CTSU_DIAG_CLOCK_RECOVERY_TEST_NUM][2] =
{
    {CTSU_DIAG_CLOCK_RECOVERY_TEST1_MIN_VALUE, CTSU_DIAG_CLOCK_RECOVERY_TEST1_MAX_VALUE},
    {CTSU_DIAG_CLOCK_RECOVERY_TEST2_MIN_VALUE, CTSU_DIAG_CLOCK_RECOVERY_TEST2_MAX_VALUE},
    {CTSU_DIAG_CLOCK_RECOVERY_TEST3_MIN_VALUE, CTSU_DIAG_CLOCK_RECOVERY_TEST3_MAX_VALUE},
  #if (CTSU_DIAG_CLOCK_RECOVERY_TEST_NUM == 4)
    {CTSU_DIAG_CLOCK_RECOVERY_TEST4_MIN_VALUE, CTSU_DIAG_CLOCK_RECOVERY_TEST4_MAX_VALUE},
  #endif
};
 #endif
#endif

/***********************************************************************************************************************
 * Global variables
 **********************************************************************************************************************/
const ctsu_api_t g_ctsu_on_ctsu =
{
    .open                 = R_CTSU_Open,
    .scanStart            = R_CTSU_ScanStart,
    .dataGet              = R_CTSU_DataGet,
    .scanStop             = R_CTSU_ScanStop,
    .diagnosis            = R_CTSU_Diagnosis,
    .close                = R_CTSU_Close,
    .callbackSet          = R_CTSU_CallbackSet,
    .specificDataGet      = R_CTSU_SpecificDataGet,
    .dataInsert           = R_CTSU_DataInsert,
    .offsetTuning         = R_CTSU_OffsetTuning,
    .autoJudgementDataGet = R_CTSU_AutoJudgementDataGet
};

/*******************************************************************************************************************//**
 * @addtogroup CTSU
 * @{
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Functions
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * @brief Opens and configures the CTSU driver module. Implements @ref ctsu_api_t::open.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_Open
 *
 * @retval FSP_SUCCESS                     CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION               Null pointer, or one or more configuration options is invalid.
 * @retval FSP_ERR_ALREADY_OPEN            Module is already open.  This module can only be opened once.
 * @retval FSP_ERR_INVALID_ARGUMENT        Configuration parameter error.
 *
 * @note
 * - In the first Open, measurement for correction works, and it takes several tens of milliseconds.
 * - When the touch interface configuration is in diagnosis mode, execute the R_CTSU_Open() of the other touch interface
 * configuration first.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_Open (ctsu_ctrl_t * const p_ctrl, ctsu_cfg_t const * const p_cfg)
{
    ctsu_instance_ctrl_t     * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
    fsp_err_t                  err             = FSP_SUCCESS;
    uint16_t                   element_id;
    const ctsu_element_cfg_t * element_cfgs;
#if (BSP_FEATURE_CTSU_VERSION == 2)
    uint16_t i;
    uint32_t pclkb_mhz;
    uint16_t suadj[CTSU_CFG_NUM_SUMULTI];
#endif

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ASSERT(p_cfg);
#endif
    FSP_ERROR_RETURN(CTSU_OPEN != p_instance_ctrl->open, FSP_ERR_ALREADY_OPEN);

    /* Check element number */
#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    if (CTSU_MODE_SELF_MULTI_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_cfg->md))
    {
        FSP_ERROR_RETURN(CTSU_CFG_NUM_SELF_ELEMENTS >=
                         (uint8_t) (g_ctsu_self_element_index + p_cfg->num_rx),
                         FSP_ERR_INVALID_ARGUMENT);
    }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_cfg->md))
    {
        FSP_ERROR_RETURN(CTSU_CFG_NUM_MUTUAL_ELEMENTS >=
                         (uint8_t) (g_ctsu_mutual_element_index + (p_cfg->num_rx * p_cfg->num_tx)),
                         FSP_ERR_INVALID_ARGUMENT);
    }
#endif
    p_instance_ctrl->state = CTSU_STATE_INIT;

    /* Save configurations */
    p_instance_ctrl->p_ctsu_cfg = p_cfg;

    /* Initialize driver control structure (address setting) */
#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    if (CTSU_MODE_SELF_MULTI_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_cfg->md))
    {
        p_instance_ctrl->p_self_raw   = &g_ctsu_self_raw[g_ctsu_self_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_self_corr  = &g_ctsu_self_corr[g_ctsu_self_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_self_mfc   = &g_ctsu_self_mfc[g_ctsu_self_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_self_data  = &g_ctsu_self_data[g_ctsu_self_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
        p_instance_ctrl->num_elements = p_cfg->num_rx;
 #if (BSP_FEATURE_CTSU_VERSION == 2)
        p_instance_ctrl->p_selected_freq_self = &g_ctsu_selected_freq_self[g_ctsu_self_element_index];
 #endif
        g_ctsu_self_element_index        = (uint8_t) (g_ctsu_self_element_index + p_instance_ctrl->num_elements);
        p_instance_ctrl->self_elem_index = g_ctsu_self_element_index;

 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        p_instance_ctrl->p_mutual_raw      = &g_ctsu_mutual_raw[0];
        p_instance_ctrl->p_mutual_pri_corr = &g_ctsu_mutual_pri_corr[0];
        p_instance_ctrl->p_mutual_snd_corr = &g_ctsu_mutual_snd_corr[0];
        p_instance_ctrl->p_mutual_pri_mfc  = &g_ctsu_mutual_pri_mfc[0];
        p_instance_ctrl->p_mutual_snd_mfc  = &g_ctsu_mutual_snd_mfc[0];
        p_instance_ctrl->p_mutual_pri_data = &g_ctsu_mutual_pri_data[0];
        p_instance_ctrl->p_mutual_snd_data = &g_ctsu_mutual_snd_data[0];
 #endif
    }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_cfg->md))
    {
        p_instance_ctrl->p_mutual_raw      = &g_ctsu_mutual_raw[g_ctsu_mutual_element_index * CTSU_MUTUAL_BUF_SIZE];
        p_instance_ctrl->p_mutual_pri_corr =
            &g_ctsu_mutual_pri_corr[g_ctsu_mutual_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_mutual_snd_corr =
            &g_ctsu_mutual_snd_corr[g_ctsu_mutual_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_mutual_pri_mfc =
            &g_ctsu_mutual_pri_mfc[g_ctsu_mutual_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_mutual_snd_mfc =
            &g_ctsu_mutual_snd_mfc[g_ctsu_mutual_element_index * CTSU_CFG_NUM_SUMULTI];
        p_instance_ctrl->p_mutual_pri_data =
            &g_ctsu_mutual_pri_data[g_ctsu_mutual_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
        p_instance_ctrl->p_mutual_snd_data =
            &g_ctsu_mutual_snd_data[g_ctsu_mutual_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
        p_instance_ctrl->num_elements = (uint8_t) (p_cfg->num_rx * p_cfg->num_tx);
 #if (BSP_FEATURE_CTSU_VERSION == 2)
        p_instance_ctrl->p_selected_freq_mutual = &g_ctsu_selected_freq_mutual[g_ctsu_mutual_element_index];
 #endif
        g_ctsu_mutual_element_index        = (uint8_t) (g_ctsu_mutual_element_index + p_instance_ctrl->num_elements);
        p_instance_ctrl->mutual_elem_index = g_ctsu_mutual_element_index;

 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        p_instance_ctrl->p_self_raw  = &g_ctsu_self_raw[0];
        p_instance_ctrl->p_self_corr = &g_ctsu_self_corr[0];
        p_instance_ctrl->p_self_mfc  = &g_ctsu_self_mfc[0];
        p_instance_ctrl->p_self_data = &g_ctsu_self_data[0];
 #endif
    }
#endif

    p_instance_ctrl->p_element_complete_flag = &g_ctsu_element_complete_flag[g_ctsu_element_index];
#if (BSP_FEATURE_CTSU_VERSION == 2)
    p_instance_ctrl->p_frequency_complete_flag = &g_ctsu_frequency_complete_flag[g_ctsu_element_index];
#endif
    p_instance_ctrl->p_tuning_diff = &g_ctsu_tuning_diff[g_ctsu_element_index];
    p_instance_ctrl->p_ctsuwr      = &g_ctsu_ctsuwr[g_ctsu_element_index * CTSU_CFG_NUM_SUMULTI];
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)

    /* Secure DTC transfer area for automatic judgement, store start address and transfer count */
    p_instance_ctrl->p_ajthr   = &g_ctsu_ajthr[g_ctsu_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
    p_instance_ctrl->p_ajmmar  = &g_ctsu_ajmmar[g_ctsu_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
    p_instance_ctrl->p_ajblact = &g_ctsu_ajblact[g_ctsu_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
    p_instance_ctrl->p_ajblar  = &g_ctsu_ajblar[g_ctsu_element_index * CTSU_MAJORITY_MODE_ELEMENTS];
    p_instance_ctrl->p_ajrr    = &g_ctsu_ajrr[g_ctsu_element_index * CTSU_MAJORITY_MODE_ELEMENTS];

    /* Copy to ctrl variable for setting change for automatic judgement */
    p_instance_ctrl->ajmmat = p_cfg->ajmmat;
    p_instance_ctrl->ajbmat = p_cfg->ajbmat;
 #endif
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
    p_instance_ctrl->p_mcact1   = &g_ctsu_mcact1[g_ctsu_element_index * CTSU_CFG_NUM_SUMULTI];
    p_instance_ctrl->p_mcact2   = &g_ctsu_mcact2[g_ctsu_element_index * CTSU_CFG_NUM_SUMULTI];
    p_instance_ctrl->mcact_flag = 0;
 #endif
#endif
    g_ctsu_element_index             = (uint8_t) (g_ctsu_element_index + p_instance_ctrl->num_elements);
    p_instance_ctrl->ctsu_elem_index = g_ctsu_element_index;

    /* Set Value */
    p_instance_ctrl->cap                = p_cfg->cap;
    p_instance_ctrl->num_moving_average = p_cfg->num_moving_average;
    p_instance_ctrl->average            = 0;
    if (true == p_cfg->tunning_enable)
    {
        p_instance_ctrl->tuning = CTSU_TUNING_INCOMPLETE;
    }
    else
    {
        p_instance_ctrl->tuning = CTSU_TUNING_COMPLETE;
    }

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_NUM_CFC != 0)
    if (CTSU_MODE_MUTUAL_CFC_SCAN == p_cfg->md)
    {
        p_instance_ctrl->tuning = CTSU_TUNING_COMPLETE;
    }
 #endif

    if (CTSU_MODE_CURRENT_SCAN == p_cfg->md)
    {
        p_instance_ctrl->tuning = CTSU_TUNING_COMPLETE;
    }
#endif

#if (BSP_FEATURE_CTSU_VERSION == 1)
    p_instance_ctrl->ctsucr1 = (uint8_t) (p_cfg->atune1 << 3);
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
    p_instance_ctrl->ctsucr1 = (uint8_t) ((p_cfg->atune12 & 0x01) << 3);
#endif
    p_instance_ctrl->ctsucr1 |= (uint8_t) (p_cfg->md << 6);

    p_instance_ctrl->txvsel  = p_instance_ctrl->p_ctsu_cfg->txvsel;
    p_instance_ctrl->txvsel2 = p_instance_ctrl->p_ctsu_cfg->txvsel2;

    p_instance_ctrl->ctsuchac0 = p_instance_ctrl->p_ctsu_cfg->ctsuchac0;
    p_instance_ctrl->ctsuchac1 = p_instance_ctrl->p_ctsu_cfg->ctsuchac1;
    p_instance_ctrl->ctsuchac2 = p_instance_ctrl->p_ctsu_cfg->ctsuchac2;
    p_instance_ctrl->ctsuchac3 = p_instance_ctrl->p_ctsu_cfg->ctsuchac3;
    p_instance_ctrl->ctsuchac4 = p_instance_ctrl->p_ctsu_cfg->ctsuchac4;

    p_instance_ctrl->ctsuchtrc0 = p_instance_ctrl->p_ctsu_cfg->ctsuchtrc0;
    p_instance_ctrl->ctsuchtrc1 = p_instance_ctrl->p_ctsu_cfg->ctsuchtrc1;
    p_instance_ctrl->ctsuchtrc2 = p_instance_ctrl->p_ctsu_cfg->ctsuchtrc2;
    p_instance_ctrl->ctsuchtrc3 = p_instance_ctrl->p_ctsu_cfg->ctsuchtrc3;
    p_instance_ctrl->ctsuchtrc4 = p_instance_ctrl->p_ctsu_cfg->ctsuchtrc4;

    p_instance_ctrl->md = p_instance_ctrl->p_ctsu_cfg->md;

#if (BSP_FEATURE_CTSU_VERSION == 2)
    p_instance_ctrl->ctsucr2  = (uint8_t) (p_cfg->atune12 & 0x02);
    p_instance_ctrl->ctsucr2 |= (uint8_t) ((p_cfg->md & 0x04) >> 2);
    p_instance_ctrl->ctsucr2 |= (uint8_t) (p_cfg->posel << 4);

    if (CTSU_ATUNE12_80UA == p_cfg->atune12)
    {
        p_instance_ctrl->range = CTSU_RANGE_80UA;
    }
    else if (CTSU_ATUNE12_40UA == p_cfg->atune12)
    {
        p_instance_ctrl->range = CTSU_RANGE_40UA;
    }
    else if (CTSU_ATUNE12_20UA == p_cfg->atune12)
    {
        p_instance_ctrl->range = CTSU_RANGE_20UA;
    }
    else if (CTSU_ATUNE12_160UA == p_cfg->atune12)
    {
        p_instance_ctrl->range = CTSU_RANGE_160UA;
    }
    else
    {
    }

 #if (CTSU_CFG_MULTIPLE_ELECTRODE_CONNECTION_ENABLE == 1)

    /* Copy the variables used for the MEC function to the ctrl instance */
    p_instance_ctrl->tsod          = p_instance_ctrl->p_ctsu_cfg->tsod;
    p_instance_ctrl->mec_ts        = p_instance_ctrl->p_ctsu_cfg->mec_ts;
    p_instance_ctrl->mec_shield_ts = p_instance_ctrl->p_ctsu_cfg->mec_shield_ts;
 #endif
#endif
    for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
    {
        p_instance_ctrl->p_element_complete_flag[element_id] = 0;
#if (BSP_FEATURE_CTSU_VERSION == 2)
        p_instance_ctrl->p_frequency_complete_flag[element_id] = 0;
#endif
        p_instance_ctrl->p_tuning_diff[element_id] = 0;
        element_cfgs = (p_cfg->p_elements + element_id);
#if (BSP_FEATURE_CTSU_VERSION == 2)
        if (CTSU_MODE_CURRENT_SCAN == p_cfg->md)
        {
            /* Current scan does not run multiple frequency */
            p_instance_ctrl->p_ctsuwr[element_id].ctsuso =
                (uint32_t) (((uint32_t) element_cfgs->sdpa << (uint32_t) 24) |
                            ((uint32_t) element_cfgs->snum << (uint32_t) 10) | element_cfgs->so);
        }
        else
        {
            if (CTSU_MODE_MUTUAL_CFC_SCAN != p_cfg->md)
            {
                p_instance_ctrl->p_ctsuwr[element_id * CTSU_CFG_NUM_SUMULTI].ctsuso =
                    (uint32_t) (((uint32_t) element_cfgs->sdpa << (uint32_t) 24) |
                                ((uint32_t) element_cfgs->snum << (uint32_t) 10) | element_cfgs->so);
            }

 #if (CTSU_CFG_NUM_CFC != 0)
            else
            {
                /* CFC scan does not use CTSUSO */
                p_instance_ctrl->p_ctsuwr[element_id * CTSU_CFG_NUM_SUMULTI].ctsuso =
                    (uint32_t) (((uint32_t) element_cfgs->sdpa << (uint32_t) 24) |
                                ((uint32_t) element_cfgs->snum << (uint32_t) 10));
            }
 #endif

            for (i = 1; i < CTSU_CFG_NUM_SUMULTI; i++)
            {
                p_instance_ctrl->p_ctsuwr[(element_id * CTSU_CFG_NUM_SUMULTI) + i].ctsuso =
                    p_instance_ctrl->p_ctsuwr[element_id * CTSU_CFG_NUM_SUMULTI].ctsuso;
            }
        }
#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
        p_instance_ctrl->p_ctsuwr[element_id].ctsussc = (uint16_t) (element_cfgs->ssdiv << 8);
        p_instance_ctrl->p_ctsuwr[element_id].ctsuso0 = (uint16_t) ((element_cfgs->snum << 10) | element_cfgs->so);
        p_instance_ctrl->p_ctsuwr[element_id].ctsuso1 =
            (uint16_t) ((CTSU_ICOG_RECOMMEND << 13) | (element_cfgs->sdpa << 8) | CTSU_RICOA_RECOMMEND);
#endif
    }

    p_instance_ctrl->write_irq = p_cfg->write_irq;
    p_instance_ctrl->read_irq  = p_cfg->read_irq;
    p_instance_ctrl->end_irq   = p_cfg->end_irq;

    /* Enable interrupts for CTSUWR, CTSURD, CTSUFN */
    R_BSP_IrqCfgEnable(p_cfg->write_irq, CTSU_CFG_INT_PRIORITY_LEVEL, p_instance_ctrl);
    R_BSP_IrqCfgEnable(p_cfg->read_irq, CTSU_CFG_INT_PRIORITY_LEVEL, p_instance_ctrl);
    R_BSP_IrqCfgEnable(p_cfg->end_irq, CTSU_CFG_INT_PRIORITY_LEVEL, p_instance_ctrl);

    p_instance_ctrl->interrupt_reverse_flag = 0;

    if (p_instance_ctrl->num_elements == g_ctsu_element_index)
    {
        /* TSCAP discharge process */
        g_ctsu_tscap_pin_cfg_data.pin_cfg =
            ((uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);
        R_IOPORT_Open(&g_ctsu_tscap_ioport_ctrl, &g_ctsu_tscap_pin_cfg);
        R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MICROSECONDS);
        g_ctsu_tscap_pin_cfg_data.pin_cfg =
            ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN | (uint32_t) IOPORT_PERIPHERAL_CTSU);
        R_IOPORT_PinsCfg(&g_ctsu_tscap_ioport_ctrl, &g_ctsu_tscap_pin_cfg);
        R_IOPORT_Close(&g_ctsu_tscap_ioport_ctrl);

        /* Get CTSU out of stop state (supply power/clock) */
        R_BSP_MODULE_START(FSP_IP_CTSU, 0);

#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
        err = ctsu_transfer_open(p_instance_ctrl);
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
        R_CTSU->CTSUCRA_b.ATUNE0 = CTSU_CFG_LOW_VOLTAGE_MODE;
        R_CTSU->CTSUCRA_b.PUMPON = 1;

        pclkb_mhz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / CTSU_PCLKB_FREQ_MHZ;

        if (CTSU_PCLKB_FREQ_RANGE1 >= pclkb_mhz)
        {
            R_CTSU->CTSUCRA_b.CLK = 0;
            R_CTSU->CTSUCR3       = (uint8_t) (pclkb_mhz - 1);
        }
        else if ((CTSU_PCLKB_FREQ_RANGE1 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE2 >= pclkb_mhz))
        {
            R_CTSU->CTSUCRA_b.CLK = 1;
            R_CTSU->CTSUCR3       = (uint8_t) ((pclkb_mhz >> 1) - 1);
        }
        else if ((CTSU_PCLKB_FREQ_RANGE2 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE3 >= pclkb_mhz))
        {
            R_CTSU->CTSUCRA_b.CLK = 2;
            R_CTSU->CTSUCR3       = (uint8_t) ((pclkb_mhz >> 2) - 1);
        }
        else
        {
            R_CTSU->CTSUCRA_b.CLK = 3;
            R_CTSU->CTSUCR3       = (uint8_t) ((pclkb_mhz >> 3) - 1);
        }
#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)

        /* Set power on */
        R_CTSU->CTSUCR1 = ((CTSU_CFG_LOW_VOLTAGE_MODE << 2) | (CTSU_CSW_ON << 1) | CTSU_PON_ON);

        /* Synchronous Noise Reduction Setting */
        R_CTSU->CTSUSDPRS = ((CTSU_SOFF_ON << 6) | (CTSU_PRMODE_62_PULSES << 4) | CTSU_PRRATIO_RECOMMEND);

        /* High Pass Noise Reduction- ALWAYS 0x30 as per HW Manual */
        R_CTSU->CTSUDCLKC = ((CTSU_SSCNT << 4) | CTSU_SSMOD);

        /* Sensor Stabilization- ALWAYS 0x10 as per HW Manual */
        R_CTSU->CTSUSST = (CTSU_SST_RECOMMEND);

        if (CTSU_CORRECTION_INIT == g_ctsu_correction_info.status)
        {
            ctsu_correction_process(p_instance_ctrl);
        }

        /* Since CLK is rewritten by correction, set here. */
        R_CTSU->CTSUCR1 |= (uint8_t) (CTSU_CFG_PCLK_DIVISION << 4);
#endif
    }

#if (BSP_FEATURE_CTSU_VERSION == 2)
    if (CTSU_MODE_MUTUAL_CFC_SCAN != p_cfg->md)
    {
        /* Set power on */
        R_CTSU->CTSUCRA_b.CSW = CTSU_CSW_ON;
        R_CTSU->CTSUCRA_b.PON = CTSU_PON_ON;
        R_BSP_SoftwareDelay(30, BSP_DELAY_UNITS_MICROSECONDS);
    }

 #if (CTSU_CFG_NUM_CFC != 0)
    else
    {
        R_CTSU->CTSUCALIB_b.CFCRDMD = 1;
        R_CTSU->CTSUCRA_b.CFCON     = 1;
        R_BSP_SoftwareDelay(30, BSP_DELAY_UNITS_MICROSECONDS);
    }
 #endif

    /* High resolution pulse mode  */
    R_CTSU->CTSUCRA_b.SDPSEL   = 1;
    R_CTSU->CTSUCRA_b.PCSEL    = 1;
    R_CTSU->CTSUSST            = CTSU_SST_RECOMMEND;
    R_CTSU->CTSUCALIB_b.CCOCLK = 0;

    if (CTSU_MODE_MUTUAL_CFC_SCAN != p_cfg->md)
    {
 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
        if (CTSU_MODE_CORRECTION_SCAN == p_cfg->md)
        {
            g_ctsu_correction_info.scan_index = CTSU_CORRECTION_POINT_NUM;
        }
 #endif
        if (CTSU_CORRECTION_INIT == g_ctsu_correction_info.status)
        {
            ctsu_correction_process(p_instance_ctrl);
        }
    }

 #if (CTSU_CFG_NUM_CFC != 0)
    else
    {
        ctsu_corrcfc_process(p_instance_ctrl);
    }
    p_instance_ctrl->p_corrcfc_info = &g_ctsu_corrcfc_info;
 #endif

    R_CTSU->CTSUCRA_b.LOAD       = 1;
    R_CTSU->CTSUCRB_b.SSCNT      = 1;
    R_CTSU->CTSUCALIB_b.SUCARRY  = 0;
    R_CTSU->CTSUCALIB_b.CCOCALIB = 0;
    R_CTSU->CTSUCALIB_b.CCOCLK   = 1;
    R_CTSU->CTSUCALIB_b.TSOC     = 0;

    /* Read SUADJD byte */
    suadj[0] = R_CTSUTRIM->CTSUTRIMA_b.SUADJD;
 #if (CTSU_CFG_NUM_SUMULTI >= 2)

    /* Adjust multi freq */
    suadj[1] = (uint16_t) ((suadj[0] * (CTSU_CFG_SUMULTI1 + 1)) / (CTSU_CFG_SUMULTI0 + 1));
 #endif
 #if (CTSU_CFG_NUM_SUMULTI >= 3)
    suadj[2] = (uint16_t) ((suadj[0] * (CTSU_CFG_SUMULTI2 + 1)) / (CTSU_CFG_SUMULTI0 + 1));
 #endif

    /* Adjust SSCNT setting */
    suadj[0] = (uint16_t) (suadj[0] - (CTSU_SUADJ_SSCNT_ADJ * R_CTSU->CTSUCRB_b.SSCNT));
 #if (CTSU_CFG_NUM_SUMULTI >= 2)
    suadj[1] = (uint16_t) (suadj[1] - (CTSU_SUADJ_SSCNT_ADJ * R_CTSU->CTSUCRB_b.SSCNT));
 #endif
 #if (CTSU_CFG_NUM_SUMULTI >= 3)
    suadj[2] = (uint16_t) (suadj[2] - (CTSU_SUADJ_SSCNT_ADJ * R_CTSU->CTSUCRB_b.SSCNT));
 #endif

    /* Set CTSUSUCLK register */
    R_CTSU->CTSUCRA_b.SDPSEL = 0;
    R_CTSU->CTSUSUCLK0       = (uint16_t) (CTSU_CFG_SUMULTI0 << 8) | suadj[0];
 #if (CTSU_CFG_NUM_SUMULTI >= 2)
    R_CTSU->CTSUSUCLK1 = (uint16_t) (CTSU_CFG_SUMULTI1 << 8) | suadj[1];
 #endif
 #if (CTSU_CFG_NUM_SUMULTI >= 3)
    R_CTSU->CTSUSUCLK2 = (uint16_t) (CTSU_CFG_SUMULTI2 << 8) | suadj[2];
 #endif
    R_CTSU->CTSUCRA_b.SDPSEL = 1;

 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
    if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
    {
        /* Fixed parameter setting for automatic judgement */
        R_CTSU->CTSUAJCR_b.TLOT = p_instance_ctrl->p_ctsu_cfg->tlot;
        R_CTSU->CTSUAJCR_b.THOT = p_instance_ctrl->p_ctsu_cfg->thot;
        R_CTSU->CTSUAJCR_b.JC   = (uint8_t) (p_instance_ctrl->p_ctsu_cfg->jc & 0x3);

        p_instance_ctrl->blini_flag = 1;
    }
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        /* Initialize diagnosis information */
        p_instance_ctrl->p_diag_info    = &g_ctsu_diag_info;
        g_ctsu_diag_info.state          = CTSU_DIAG_INIT;
        g_ctsu_diag_info.ctsuwr.ctsussc = 0;
        g_ctsu_diag_info.ctsuwr.ctsuso0 = 0;
        g_ctsu_diag_info.ctsuwr.ctsuso1 = 0;
        g_ctsu_diag_info.loop_count     = 0;
        g_ctsu_diag_info.scanbuf.sen    = 0;
        g_ctsu_diag_info.scanbuf.ref    = 0;
        g_ctsu_diag_info.correct_data   = 0;
        g_ctsu_diag_info.icomp          = 0;
        g_ctsu_diag_info.cco_high       = 0;
        g_ctsu_diag_info.cco_low        = 0;
        g_ctsu_diag_info.sscg           = 0;
        g_ctsu_diag_info.dac_cnt[0]     = 0;
        g_ctsu_diag_info.dac_cnt[1]     = 0;
        g_ctsu_diag_info.dac_cnt[2]     = 0;
        g_ctsu_diag_info.dac_cnt[3]     = 0;
        g_ctsu_diag_info.dac_cnt[4]     = 0;
        g_ctsu_diag_info.dac_cnt[5]     = 0;
        g_ctsu_diag_info.so0_4uc_val    = 0;
        g_ctsu_diag_info.dac_init       = 0;
        g_ctsu_diag_info.tuning         = CTSU_TUNING_INCOMPLETE;
        g_ctsu_diag_info.tuning_diff    = 0;
    }
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        p_instance_ctrl->p_diag_info       = &g_ctsu_diag_info;
        g_ctsu_diag_info.state             = CTSU_DIAG_INIT;
        g_ctsu_diag_info.test_result       = FSP_SUCCESS;
        g_ctsu_diag_info.test_count        = 0;
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
        g_ctsu_diag_info.load_resistance   = 0;
        g_ctsu_diag_info.average_data_pre  = 0;
        g_ctsu_diag_info.onetime_exec_flag = 0;
    }
 #endif
#endif

    p_instance_ctrl->p_correction_info = &g_ctsu_correction_info;
    p_instance_ctrl->rd_index          = 0;
    p_instance_ctrl->wr_index          = 0;
    p_instance_ctrl->state             = CTSU_STATE_IDLE;

    p_instance_ctrl->p_callback        = p_cfg->p_callback;
    p_instance_ctrl->p_context         = p_cfg->p_context;
    p_instance_ctrl->p_callback_memory = NULL;

    p_instance_ctrl->serial_tuning_enable     = 0;
    p_instance_ctrl->serial_tuning_mutual_cnt = 0;

#if defined(CTSU_CFG_TARGET_VALUE_QE_SUPPORT)
    p_instance_ctrl->tuning_self_target_value   = p_cfg->tuning_self_target_value;
    p_instance_ctrl->tuning_mutual_target_value = p_cfg->tuning_mutual_target_value;
#else
    p_instance_ctrl->tuning_self_target_value   = CTSU_TUNING_VALUE_SELF;
    p_instance_ctrl->tuning_mutual_target_value = CTSU_TUNING_VALUE_MUTUAL;
#endif

    /* Mark driver as open */
    p_instance_ctrl->open = CTSU_OPEN;

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function should be called each time a periodic timer expires.
 * If initial offset tuning is enabled, The first several calls are used to tuning for the sensors.
 * Before starting the next scan, first get the data with R_CTSU_DataGet().
 * If a different control block scan should be run, check the scan is complete before executing.
 * Implements @ref ctsu_api_t::scanStart.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_ScanStart
 *
 * @retval FSP_SUCCESS                      CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION                Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN                 Module is not open.
 * @retval FSP_ERR_CTSU_SCANNING            Scanning this instance or other.
 * @retval FSP_ERR_CTSU_NOT_GET_DATA        The previous data has not been retrieved by DataGet.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_ScanStart (ctsu_ctrl_t * const p_ctrl)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
    uint8_t                temp;
#if (BSP_FEATURE_CTSU_VERSION == 2)
    uint8_t txvsel;
#endif

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    if (CTSU_CAP_SOFTWARE == p_instance_ctrl->cap)
    {
        /* Can be checked if the previous measurement was a software trigger */
        if (0 == (0x02 & R_CTSU->CTSUCR0))
        {
            FSP_ERROR_RETURN(0x01 != (R_CTSU->CTSUCR0 & 0x01), FSP_ERR_CTSU_SCANNING);
        }
    }

    FSP_ERROR_RETURN(CTSU_STATE_SCANNED != p_instance_ctrl->state, FSP_ERR_CTSU_NOT_GET_DATA);

    R_FSP_IsrContextSet(p_instance_ctrl->write_irq, p_instance_ctrl);
    R_FSP_IsrContextSet(p_instance_ctrl->read_irq, p_instance_ctrl);
    R_FSP_IsrContextSet(p_instance_ctrl->end_irq, p_instance_ctrl);

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
    ctsu_auto_correction_register_set(p_instance_ctrl);

    /* Sensor counter auto correction enabled */
    R_CTSU->CTSUOPT_b.CCOCFEN = 1;
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
    if (((1 == p_instance_ctrl->p_ctsu_cfg->ajfen) && (1 == p_instance_ctrl->p_ctsu_cfg->majirimd)) ||
        ((0 == p_instance_ctrl->p_ctsu_cfg->ajfen) && (0 == p_instance_ctrl->p_ctsu_cfg->majority_mode)))
    {
        if (0 == p_instance_ctrl->mcact_flag)
        {
            if (CTSU_TUNING_COMPLETE == p_instance_ctrl->tuning)
            {
                ctsu_multiclock_auto_correction_register_set(p_instance_ctrl);
                p_instance_ctrl->mcact_flag = 1;
                R_CTSU->CTSUOPT_b.MCACEFN   = 1;
                R_CTSU->CTSUOPT_b.MAJIRIMD  = (uint8_t) (p_instance_ctrl->p_ctsu_cfg->majirimd & 0x1);
            }
            else
            {
                R_CTSU->CTSUOPT_b.MAJIRIMD = 0;
                R_CTSU->CTSUOPT_b.MCACEFN  = 0;
            }
        }
        else
        {
            R_CTSU->CTSUOPT_b.MCACEFN  = 1;
            R_CTSU->CTSUOPT_b.MAJIRIMD = (uint8_t) (p_instance_ctrl->p_ctsu_cfg->majirimd & 0x1);
        }
    }
    else
    {
        R_CTSU->CTSUOPT_b.MAJIRIMD = 0;
        R_CTSU->CTSUOPT_b.MCACEFN  = 0;
    }
 #endif
#endif

#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
    err = ctsu_transfer_configure(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);
#endif

    if (CTSU_CAP_SOFTWARE == p_instance_ctrl->cap)
    {
        R_CTSU->CTSUCR0 &= (uint8_t) ~0x06; // specify Software trigger usage
    }
    else
    {
        R_CTSU->CTSUCR0 |= 0x06;            // specify external trigger usage and enable power saving
        R_CTSU->CTSUCR0 &= (uint8_t) ~0x01; // To write CTSUCR1
    }

    /* Write CTSU Control Register 1 and save mode */
#if (BSP_FEATURE_CTSU_VERSION == 2)
    if (CTSU_MODE_CORRECTION_SCAN != p_instance_ctrl->md)
    {
        txvsel =
            (uint8_t) ((p_instance_ctrl->txvsel2 << 6) | (p_instance_ctrl->txvsel << 7));
        temp            = (uint8_t) (R_CTSU->CTSUCR0 & ~(CTSU_CR0_MODIFY_BIT));
        R_CTSU->CTSUCR0 = (uint8_t) (temp | (txvsel & CTSU_CR0_MODIFY_BIT));                   // TXVSEL
        temp            = (uint8_t) (R_CTSU->CTSUCR1 & ~(CTSU_CR1_MODIFY_BIT));
        R_CTSU->CTSUCR1 = (uint8_t) (temp | (p_instance_ctrl->ctsucr1 & CTSU_CR1_MODIFY_BIT)); // MD1, MD0, ATUNE1
        temp            = (uint8_t) (R_CTSU->CTSUCR2 & ~(CTSU_CR2_MODIFY_BIT));
        R_CTSU->CTSUCR2 = (uint8_t) (temp | (p_instance_ctrl->ctsucr2 & CTSU_CR2_MODIFY_BIT)); // POSEL, ATUNE2, MD2
        if (CTSU_MODE_CURRENT_SCAN == p_instance_ctrl->md)
        {
            R_CTSU->CTSUSST          = CTSU_SST_RECOMMEND_CURRENT;
            R_CTSU->CTSUCRA_b.DCMODE = 1;
            R_CTSU->CTSUCRA_b.DCBACK = 1;
            R_CTSU->CTSUMCH_b.MCA0   = 1;
            R_CTSU->CTSUMCH_b.MCA1   = 0;
            R_CTSU->CTSUMCH_b.MCA2   = 0;
            R_CTSU->CTSUMCH_b.MCA3   = 0;
        }
        else
        {
            R_CTSU->CTSUSST          = CTSU_SST_RECOMMEND;
            R_CTSU->CTSUCRA_b.DCMODE = 0;
            R_CTSU->CTSUCRA_b.DCBACK = 0;
            R_CTSU->CTSUMCH_b.MCA0   = 1;
 #if (CTSU_CFG_NUM_SUMULTI >= 2)
            R_CTSU->CTSUMCH_b.MCA1 = 1;
 #endif
 #if (CTSU_CFG_NUM_SUMULTI >= 3)
            R_CTSU->CTSUMCH_b.MCA2 = 1;
 #endif
        }

        /* Write Channel setting */
        R_CTSU->CTSUCHAC0  = p_instance_ctrl->ctsuchac0;
        R_CTSU->CTSUCHAC1  = p_instance_ctrl->ctsuchac1;
        R_CTSU->CTSUCHAC2  = p_instance_ctrl->ctsuchac2;
        R_CTSU->CTSUCHAC3  = p_instance_ctrl->ctsuchac3;
        R_CTSU->CTSUCHAC4  = p_instance_ctrl->ctsuchac4;
        R_CTSU->CTSUCHTRC0 = p_instance_ctrl->ctsuchtrc0;
        R_CTSU->CTSUCHTRC1 = p_instance_ctrl->ctsuchtrc1;
        R_CTSU->CTSUCHTRC2 = p_instance_ctrl->ctsuchtrc2;
        R_CTSU->CTSUCHTRC3 = p_instance_ctrl->ctsuchtrc3;
        R_CTSU->CTSUCHTRC4 = p_instance_ctrl->ctsuchtrc4;
    }

 #if (CTSU_CFG_NUM_CFC != 0)
    if (CTSU_MODE_MUTUAL_CFC_SCAN == p_instance_ctrl->md)
    {
        R_CTSU->CTSUSR_b.CFCRDCH = 0x00;
    }
 #endif

 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        ctsu_diag_scan_start2(p_instance_ctrl);
    }
 #endif
 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
    if (CTSU_MODE_CORRECTION_SCAN == p_instance_ctrl->md)
    {
        R_CTSU->CTSUSST = CTSU_SST_RECOMMEND;
        ctsu_correction_scan_start();
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)

        /* Disable automatic correction during temperature correction */
        R_CTSU->CTSUOPT_b.CCOCFEN = 0;
  #endif
    }
 #endif

 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
    if (CTSU_TUNING_COMPLETE == p_instance_ctrl->tuning)
    {
        /* Enable / disable switching of automatic judgement */
        R_CTSU->CTSUOPT_b.MTUCFEN = (uint8_t) (p_instance_ctrl->p_ctsu_cfg->mtucfen & 0x1);
        R_CTSU->CTSUOPT_b.AJFEN   = (uint8_t) (p_instance_ctrl->p_ctsu_cfg->ajfen & 0x1);

        if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
        {
            /* Setting the number of moving averages and the number of baseline averages for automatic judgement */
            R_CTSU->CTSUAJCR_b.AJMMAT = (uint8_t) (p_instance_ctrl->ajmmat & 0xF);
            R_CTSU->CTSUAJCR_b.AJBMAT = (uint8_t) (p_instance_ctrl->ajbmat & 0xF);
            R_CTSU->CTSUAJCR_b.JC     = (uint8_t) (p_instance_ctrl->p_ctsu_cfg->jc & 0x3);
            R_CTSU->CTSUAJRR_b.TJR0   = 0;
            R_CTSU->CTSUAJRR_b.TJR1   = 0;
            R_CTSU->CTSUAJRR_b.TJR2   = 0;
        }
    }
    else
    {
        /* If tuning incomplete, auto judgement register set OFF */
        R_CTSU->CTSUOPT_b.MTUCFEN = 0;
        R_CTSU->CTSUOPT_b.AJFEN   = 0;
    }

    if (1 == p_instance_ctrl->blini_flag)
    {
        /* Set blini to 1 at the first judgement of automatic judgement */
        R_CTSU->CTSUAJCR_b.BLINI = 1;
    }
 #endif

 #if (CTSU_CFG_MULTIPLE_ELECTRODE_CONNECTION_ENABLE == 1)

    /* Parameter setting for multiple electrode connection */
    R_CTSU->CTSUCALIB_b.TSOD   = (uint8_t) (p_instance_ctrl->tsod & 0x01);
    R_CTSU->CTSUCALIB_b.IOCSEL = 0;
    if (1 == p_instance_ctrl->tsod)
    {
        /* When using MEC, MD0 bit is set to single scan mode. */
        R_CTSU->CTSUCRA_b.MD0  = 0;
        R_CTSU->CTSUMCH_b.MCH0 = (uint8_t) (p_instance_ctrl->mec_ts & CTSU_MEC_BIT6_MASK);

        /* Set MCH1 when using both MEC and Active Shield.*/
        if ((0 != p_instance_ctrl->ctsuchtrc0) ||
            (0 != p_instance_ctrl->ctsuchtrc1) ||
            (0 != p_instance_ctrl->ctsuchtrc2) ||
            (0 != p_instance_ctrl->ctsuchtrc3) ||
            (0 != p_instance_ctrl->ctsuchtrc4))
        {
            R_CTSU->CTSUMCH_b.MCH1 = (uint8_t) (p_instance_ctrl->mec_shield_ts & CTSU_MEC_BIT6_MASK);
        }
    }
 #endif
#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN != p_instance_ctrl->md)
    {
        temp            = (uint8_t) (R_CTSU->CTSUCR1 & ~(CTSU_CR1_MODIFY_BIT));
        R_CTSU->CTSUCR1 = (uint8_t) (temp | (p_instance_ctrl->ctsucr1 & CTSU_CR1_MODIFY_BIT)); // MD1, MD0, ATUNE1

 #if BSP_FEATURE_CTSU_HAS_TXVSEL
        R_CTSU->CTSUCR0 =
            (uint8_t) ((R_CTSU->CTSUCR0 & ~(CTSU_TXVSEL)) | (p_instance_ctrl->txvsel & CTSU_TXVSEL));
 #endif

        /* Write Channel setting */
        R_CTSU->CTSUCHAC[0] = p_instance_ctrl->ctsuchac0;
        R_CTSU->CTSUCHAC[1] = p_instance_ctrl->ctsuchac1;
 #if (BSP_FEATURE_CTSU_CTSUCHAC_REGISTER_COUNT >= 3)
        R_CTSU->CTSUCHAC[2] = p_instance_ctrl->ctsuchac2;
 #endif
 #if (BSP_FEATURE_CTSU_CTSUCHAC_REGISTER_COUNT >= 4)
        R_CTSU->CTSUCHAC[3] = p_instance_ctrl->ctsuchac3;
 #endif
 #if (BSP_FEATURE_CTSU_CTSUCHAC_REGISTER_COUNT >= 5)
        R_CTSU->CTSUCHAC[4] = p_instance_ctrl->ctsuchac4;
 #endif

 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        R_CTSU->CTSUCHTRC[0] = p_instance_ctrl->ctsuchtrc0;
        R_CTSU->CTSUCHTRC[1] = p_instance_ctrl->ctsuchtrc1;
  #if (BSP_FEATURE_CTSU_CTSUCHTRC_REGISTER_COUNT >= 3)
        R_CTSU->CTSUCHTRC[2] = p_instance_ctrl->ctsuchtrc2;
  #endif
  #if (BSP_FEATURE_CTSU_CTSUCHTRC_REGISTER_COUNT >= 4)
        R_CTSU->CTSUCHTRC[3] = p_instance_ctrl->ctsuchtrc3;
  #endif
  #if (BSP_FEATURE_CTSU_CTSUCHTRC_REGISTER_COUNT >= 5)
        R_CTSU->CTSUCHTRC[4] = p_instance_ctrl->ctsuchtrc4;
  #endif
 #endif
    }

 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        ctsu_diag_scan_start1(p_instance_ctrl);
    }
 #endif
#endif

    p_instance_ctrl->state = CTSU_STATE_SCANNING;

    /* Set CTSU_STRT bit to start scan */
    R_CTSU->CTSUCR0 |= 0x01;           ///< CTSU_STRT

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function gets the sensor values as scanned by the CTSU.
 * If initial offset tuning is enabled, The first several calls are used to tuning for the sensors.
 * This function reads all previously corrected measured values into the specified buffer(p_data).
 * CTSU1: The value passed through sensor CCO correction and moving average.
 * CTSU2 JMM: The value passed through sensor CCO correction and moving average.
 * CTSU2 VMM: Sensor passed through sensor CCO correction, frequency correction and moving average.
 * The required buffer size varies depending on the measurement mode. Prepare the number of TS for the self-capacitance
 * measurement and current measurement modes, and twice the number of matrixes for the mutual-capacitance measurement
 * mode. In the case of CTSU2 JMM, data of 3 frequencies is stored, so prepare 3 times more.
 * In diagnosis mode, if data collection has not been completed, the function returns FSP_ERR_CTSU_DIAG_NOT_YET.
 * If data collection is completed, it will return FSP_SUCCESS, so please call R_CTSU_Diagnosis() to check
 * the diagnosis result. In addition, if an error occurs in ADC measurement during the output‑voltage diagnosis,
 * the function returns FSP_ERR_ABORTED.
 * Implements @ref ctsu_api_t::dataGet.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_DataGet
 *
 * @retval FSP_SUCCESS                           CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION                     Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN                      Module is not open.
 * @retval FSP_ERR_CTSU_SCANNING                 Scanning this instance.
 * @retval FSP_ERR_CTSU_INCOMPLETE_TUNING        Incomplete initial offset tuning.
 * @retval FSP_ERR_CTSU_DIAG_NOT_YET             Diagnosis of data collected no yet.
 * @retval FSP_ERR_INVALID_MODE                  The mode of automatic judgement on is invalid.
 * @retval FSP_ERR_ABORTED                       Operate error of Diagnosis ADC data collection, since ADC use other
 * @note In diagnosis mode, calling R_CTSU_DataGet() executes internal diagnostic processing,
 * and a dummy value is returned in p_data.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_DataGet (ctsu_ctrl_t * const p_ctrl, uint16_t * p_data)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
    uint16_t               element_id;
    uint16_t               majority_mode_id;

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ASSERT(p_data);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    FSP_ERROR_RETURN(CTSU_STATE_SCANNING != p_instance_ctrl->state, FSP_ERR_CTSU_SCANNING);
    FSP_ERROR_RETURN(0 == p_instance_ctrl->p_ctsu_cfg->ajfen, FSP_ERR_INVALID_MODE);

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
    if (CTSU_MODE_CORRECTION_SCAN == p_instance_ctrl->md)
    {
        err = ctsu_correction_data_get(p_instance_ctrl, p_data);
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)

        /* Enable automatic correction after temperature correction is completed */
        R_CTSU->CTSUOPT_b.CCOCFEN = 1;
  #endif
        p_instance_ctrl->state = CTSU_STATE_IDLE;

        return err;
    }
 #endif

 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        err = ctsu_diag_data_get2(p_instance_ctrl);
        p_instance_ctrl->state = CTSU_STATE_IDLE;
        *p_data                = CTSU_COUNT_MAX;

        return err;
    }
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        err = ctsu_diag_data_get1();
        p_instance_ctrl->state = CTSU_STATE_IDLE;
        if (FSP_ERR_CTSU_DIAG_NOT_YET == err)
        {
            err = FSP_ERR_CTSU_DIAG_NOT_YET;
        }
        else
        {
            err = FSP_SUCCESS;
        }

        return err;
    }
 #endif
#endif

    if (CTSU_STATE_SCANNED == p_instance_ctrl->state)
    {
        if (CTSU_TUNING_COMPLETE == p_instance_ctrl->tuning)
        {
            if (p_instance_ctrl->average == p_instance_ctrl->num_moving_average)
            {
                /* Do nothing */
            }
            else if (p_instance_ctrl->average < p_instance_ctrl->num_moving_average)
            {
                (p_instance_ctrl->average)++;
            }
            else
            {
                p_instance_ctrl->average = p_instance_ctrl->num_moving_average;
            }
        }

        ctsu_correction_exec(p_instance_ctrl);

        if (CTSU_TUNING_INCOMPLETE == p_instance_ctrl->tuning)
        {
            if ((CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md) ||
                (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md))
            {
                ctsu_initial_offset_tuning(p_instance_ctrl);
            }
        }

        p_instance_ctrl->state = CTSU_STATE_IDLE;
    }

    FSP_ERROR_RETURN(0 < p_instance_ctrl->average, FSP_ERR_CTSU_INCOMPLETE_TUNING);

#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    if (CTSU_MODE_SELF_MULTI_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
        {
            for (majority_mode_id = 0; majority_mode_id < CTSU_MAJORITY_MODE_ELEMENTS; majority_mode_id++)
            {
                *p_data =
                    (p_instance_ctrl->p_self_data + element_id * CTSU_MAJORITY_MODE_ELEMENTS +
                     majority_mode_id)->int_data;
                p_data++;
            }
        }
    }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        if (true == p_instance_ctrl->serial_tuning_enable)
        {
            /* Serial Tuning Phase1 */
            if ((0 == p_instance_ctrl->ctsuchtrc0) && (0 == p_instance_ctrl->ctsuchtrc1) &&
                (0 == p_instance_ctrl->ctsuchtrc2) && (0 == p_instance_ctrl->ctsuchtrc3) &&
                (0 == p_instance_ctrl->ctsuchtrc4))
            {
                return FSP_SUCCESS;
            }
        }

        for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
        {
            for (majority_mode_id = 0; majority_mode_id < CTSU_MAJORITY_MODE_ELEMENTS; majority_mode_id++)
            {
                *p_data =
                    (p_instance_ctrl->p_mutual_pri_data + element_id * CTSU_MAJORITY_MODE_ELEMENTS +
                     majority_mode_id)->int_data;
                p_data++;
                *p_data =
                    (p_instance_ctrl->p_mutual_snd_data + element_id * CTSU_MAJORITY_MODE_ELEMENTS +
                     majority_mode_id)->int_data;
                p_data++;
            }
        }
    }
#endif

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function gets the result of automatic judgement button. Call after the scan is completed.
 * The result is 64-bit bitmaps and is stored in order of TS number for specified ctsu control.
 * After the initial judgement, the baseline initialization bit is set and the automatic judgement threshold is set.
 * This function is called only for automatic judgement.
 * Implements @ref ctsu_api_t::autoJudgementDataGet.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_AutoJudgementDataGet
 *
 * @retval FSP_SUCCESS                  CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION            Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN             Module is not open.
 * @retval FSP_ERR_CTSU_SCANNING        Scanning this instance.
 * @retval FSP_ERR_INVALID_MODE         The mode of automatic judgement off is invalid.
 * @note This function is only supported by CTSU2SLa.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_AutoJudgementDataGet (ctsu_ctrl_t * const p_ctrl, uint64_t * p_button_status)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
    uint16_t element_id;
 #endif
#endif

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ASSERT(p_button_status);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    FSP_ERROR_RETURN(CTSU_STATE_SCANNING != p_instance_ctrl->state, FSP_ERR_CTSU_SCANNING);
    FSP_ERROR_RETURN(1 == p_instance_ctrl->p_ctsu_cfg->ajfen, FSP_ERR_INVALID_MODE);

#if  ((BSP_FEATURE_CTSU_VERSION == 2) && (CTSU_CFG_AUTO_JUDGE_ENABLE == 1))
    if (1 == p_instance_ctrl->blini_flag)
    {
        p_instance_ctrl->blini_flag = 0;

        /* After the initial judgement of automatic judgement, set the baseline initialization bit to 0. */
        R_CTSU->CTSUAJCR_b.BLINI = 0;

        ctsu_auto_judge_threshold_calc(p_instance_ctrl);
    }

    if (CTSU_STATE_SCANNED == p_instance_ctrl->state)
    {
        for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
        {
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
            if (1 == p_instance_ctrl->p_ctsu_cfg->majirimd)
            {
                /* Reflect the final result bit of automatic judgement in buttun_status */
                if (1 == (((p_instance_ctrl->p_ajrr[element_id]) >> CTSU_AUTO_FINAL_JUDGE_BIT) & 0x1))
                {
                    *p_button_status |= ((uint64_t) 1 << element_id);
                }
                else
                {
                    *p_button_status &= ~((uint64_t) 1 << element_id);
                }
            }
            else
 #endif
            {
                /* Reflect the final result bit of automatic judgement in buttun_status */
                /* Since the final result of the automatic judgement is output to the FJR bit of the final multi-clock, */
                /* the result of the final multi-clock is taken out.                                                   */
                if (1 ==
                    (((p_instance_ctrl->p_ajrr[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) +
                                               (CTSU_MAJORITY_MODE_ELEMENTS - 1)]) >> CTSU_AUTO_FINAL_JUDGE_BIT) & 0x1))
                {
                    *p_button_status |= ((uint64_t) 1 << element_id);
                }
                else
                {
                    *p_button_status &= ~((uint64_t) 1 << element_id);
                }
            }
        }

        p_instance_ctrl->state = CTSU_STATE_IDLE;
    }

#else
    FSP_PARAMETER_NOT_USED(p_button_status);
#endif

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function tunes the offset register(SO). Call after the measurement is completed.
 * If the return value is FSP_ERR_CTSU_INCOMPLETE_TUNING, tuning is not complete.
 * Execute the measurement and this function call routine until the return value becomes FSP_SUCCESS.
 * It is recommended to run this routine after R_CTSU_Open().
 * It can be recalled and tuned again.
 * When the automatic judgement is enabled, after the offset tuning is completed,the baseline initialization bit flag is set.
 * Implements @ref ctsu_api_t::offsetTuning.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_OffsetTuning
 *
 * @retval FSP_SUCCESS                           CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION                     Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN                      Module is not open.
 * @retval FSP_ERR_CTSU_SCANNING                 Scanning this instance.
 * @retval FSP_ERR_CTSU_INCOMPLETE_TUNING        Incomplete initial offset tuning.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_OffsetTuning (ctsu_ctrl_t * const p_ctrl)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
    uint16_t               element_id;
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
    uint16_t i;
 #endif
#endif

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    FSP_ERROR_RETURN(CTSU_STATE_SCANNING != p_instance_ctrl->state, FSP_ERR_CTSU_SCANNING);

    if (CTSU_TUNING_COMPLETE == p_instance_ctrl->tuning)
    {
        for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
        {
            /* Counter clear for re-offset tuning */
            *(p_instance_ctrl->p_element_complete_flag + element_id) = 0;
#if (BSP_FEATURE_CTSU_VERSION == 2)
            *(p_instance_ctrl->p_frequency_complete_flag + element_id) = 0;
#endif
            *(p_instance_ctrl->p_tuning_diff + element_id) = 0;
        }
    }

    p_instance_ctrl->tuning = CTSU_TUNING_INCOMPLETE;

    if (CTSU_STATE_SCANNED == p_instance_ctrl->state)
    {
        if (0 == p_instance_ctrl->p_ctsu_cfg->ajfen)
        {
            ctsu_correction_exec(p_instance_ctrl);
        }

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
        else
        {
            R_CTSU->CTSUOPT_b.AJFEN   = 0;
            R_CTSU->CTSUOPT_b.MTUCFEN = 0;

            for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
            {
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
                    if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
                    {
                        p_instance_ctrl->p_self_corr[(element_id * CTSU_CFG_NUM_SUMULTI) +
                                                     i] =
                            p_instance_ctrl->p_self_raw[(element_id * CTSU_CFG_NUM_SUMULTI) + i];
                    }

  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
                    else
                    {
                        p_instance_ctrl->p_mutual_pri_corr[(element_id * CTSU_CFG_NUM_SUMULTI) +
                                                           i] =
                            p_instance_ctrl->p_mutual_raw[(element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2)];
                    }
  #endif
  #if (CTSU_CFG_MAJORITY_MODE & CTSU_JUDGEMENT_MAJORITY_MODE)

                    /* Initial value setting of DTC transfer information for automatic judgement */
                    p_instance_ctrl->p_ajthr[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i]   = 0;
                    p_instance_ctrl->p_ajmmar[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i]  = 0;
                    p_instance_ctrl->p_ajblact[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i] = 0;
                    p_instance_ctrl->p_ajblar[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i]  = 0;
                    p_instance_ctrl->p_ajrr[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i]    = 0;
  #endif
                }

  #if ((CTSU_CFG_MAJORITY_MODE & CTSU_JUDGEMENT_MAJORITY_MODE) == 0)
                p_instance_ctrl->p_ajthr[element_id]   = 0;
                p_instance_ctrl->p_ajmmar[element_id]  = 0;
                p_instance_ctrl->p_ajblact[element_id] = 0;
                p_instance_ctrl->p_ajblar[element_id]  = 0;
                p_instance_ctrl->p_ajrr[element_id]    = 0;
  #endif
            }
        }
 #endif
#endif

        if (CTSU_TUNING_INCOMPLETE == p_instance_ctrl->tuning)
        {
            if ((CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md) ||
                (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md))
            {
                ctsu_initial_offset_tuning(p_instance_ctrl);
            }
        }

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
        if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
        {
            if (CTSU_TUNING_COMPLETE == p_instance_ctrl->tuning)
            {
                /* Initialization of baseline mean for automatic judgement */
                p_instance_ctrl->blini_flag = 1;
            }
        }
 #endif
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
        p_instance_ctrl->mcact_flag = 0;
 #endif
#endif
        p_instance_ctrl->state = CTSU_STATE_IDLE;
    }

    FSP_ERROR_RETURN(CTSU_TUNING_COMPLETE == p_instance_ctrl->tuning, FSP_ERR_CTSU_INCOMPLETE_TUNING);

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function scan stops the sensor as scanning by the CTSU.
 * Implements @ref ctsu_api_t::scanStop.
 * @retval FSP_SUCCESS              CTSU successfully scan stop.
 * @retval FSP_ERR_ASSERTION        Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN         Module is not open.
 * @note When using this API together with the diagnosis mode, please note the following:
 * Do not call this API between executing R_CTSU_ScanStart() and calling R_CTSU_DataGet().
 **********************************************************************************************************************/
fsp_err_t R_CTSU_ScanStop (ctsu_ctrl_t * const p_ctrl)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
#if (CTSU_CFG_PARAM_CHECKING_ENABLE)
    FSP_ASSERT(p_ctrl);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif

    if (CTSU_STATE_SCANNING == p_instance_ctrl->state)
    {
#if (BSP_FEATURE_CTSU_VERSION == 2)
        R_CTSU->CTSUCRA ^= 0x11;
#else
        R_CTSU->CTSUCR0 ^= 0x11;
#endif

#if (BSP_FEATURE_ICU_HAS_IELSR)
        R_BSP_IrqStatusClear(p_instance_ctrl->p_ctsu_cfg->write_irq);
        R_BSP_IrqStatusClear(p_instance_ctrl->p_ctsu_cfg->read_irq);
        R_BSP_IrqStatusClear(p_instance_ctrl->p_ctsu_cfg->end_irq);
#endif
        p_instance_ctrl->state    = CTSU_STATE_IDLE;
        p_instance_ctrl->wr_index = 0;
        p_instance_ctrl->rd_index = 0;
    }

    return err;
}

/*******************************************************************************************************************//**
 * Updates the user callback and has option of providing memory for callback structure.
 * Implements ctsu_api_t::callbackSet
 *
 * @retval FSP_SUCCESS                       Callback updated successfully.
 * @retval FSP_ERR_ASSERTION                 A required pointer is NULL.
 * @retval FSP_ERR_NOT_OPEN                  The control block has not been opened.
 * @retval FSP_ERR_NO_CALLBACK_MEMORY        p_callback is non-secure and p_callback_memory is either secure or NULL.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_CallbackSet (ctsu_ctrl_t * const          p_api_ctrl,
                              void (                     * p_callback)(ctsu_callback_args_t *),
                              void * const                 p_context,
                              ctsu_callback_args_t * const p_callback_memory)
{
    ctsu_instance_ctrl_t * p_ctrl = (ctsu_instance_ctrl_t *) p_api_ctrl;

#if (CTSU_CFG_PARAM_CHECKING_ENABLE)
    FSP_ASSERT(p_ctrl);
    FSP_ASSERT(p_callback);
    FSP_ERROR_RETURN(CTSU_OPEN == p_ctrl->open, FSP_ERR_NOT_OPEN);
#endif

#if BSP_TZ_SECURE_BUILD

    /* Get security state of p_callback */
    bool callback_is_secure =
        (NULL == cmse_check_address_range((void *) p_callback, sizeof(void *), CMSE_AU_NONSECURE));

 #if CTSU_CFG_PARAM_CHECKING_ENABLE

    /* In secure projects, p_callback_memory must be provided in non-secure space if p_callback is non-secure */
    ctsu_callback_args_t * const p_callback_memory_checked = cmse_check_pointed_object(p_callback_memory,
                                                                                       CMSE_AU_NONSECURE);
    FSP_ERROR_RETURN(callback_is_secure || (NULL != p_callback_memory_checked), FSP_ERR_NO_CALLBACK_MEMORY);
 #endif
#endif

    /* Store callback and context */
#if BSP_TZ_SECURE_BUILD
    p_ctrl->p_callback = callback_is_secure ? p_callback :
                         (void (*)(ctsu_callback_args_t *))cmse_nsfptr_create(p_callback);
#else
    p_ctrl->p_callback = p_callback;
#endif
    p_ctrl->p_context         = p_context;
    p_ctrl->p_callback_memory = p_callback_memory;

    return FSP_SUCCESS;
}

/*******************************************************************************************************************//**
 * @brief Disables specified CTSU control block. Implements @ref ctsu_api_t::close.
 *
 * @retval FSP_SUCCESS              CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION        Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN         Module is not open.
 * @note When using this API together with the diagnosis mode, please note the following:
 * Do not call this API between executing R_CTSU_ScanStart() and calling R_CTSU_DataGet().
 **********************************************************************************************************************/
fsp_err_t R_CTSU_Close (ctsu_ctrl_t * const p_ctrl)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;

    FSP_ASSERT(p_instance_ctrl);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);

    g_ctsu_element_index = (uint8_t) (g_ctsu_element_index - p_instance_ctrl->num_elements);
#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    if (CTSU_MODE_SELF_MULTI_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        g_ctsu_self_element_index = (uint8_t) (g_ctsu_self_element_index - p_instance_ctrl->num_elements);
    }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        g_ctsu_mutual_element_index = (uint8_t) (g_ctsu_mutual_element_index - p_instance_ctrl->num_elements);
    }
#endif
    if (0 == g_ctsu_element_index)
    {
#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
        err = ctsu_transfer_close(p_instance_ctrl);
#endif
    }

    p_instance_ctrl->state = CTSU_STATE_INIT;
    p_instance_ctrl->open  = false;

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function gets the sensor specific data values as scanned by the CTSU.
 * Call this function after calling the R_CTSU_DataGet() function.
 *
 * By setting the third argument to CTSU_SPECIFIC_RAW_DATA,
 * RAW data can be output from the second argument.
 *
 * By setting the third argument to CTSU_SPECIFIC_CCO_CORRECTION_DATA,
 * the cco corrected data can be output from the second argument.
 *
 * By setting the third argument to CTSU_SPECIFIC_CORRECTION_DATA,
 * the frequency corrected data can be output from the second argument. (CTSU2 VMM)
 *
 * By setting the third argument to CTSU_SPECIFIC_SELECTED_FREQ,
 * Get bitmap of the frequency values used in majority decision from the second argument. (CTSU2 VMM)
 * The bitmap is shown as follows.
 *
 * | 2bit                | 1bit                | 0bit                |
 * |---------------------|---------------------|---------------------|
 * | 3rd frequency value | 2nd frequency value | 1st frequency value |
 *
 * Implements @ref ctsu_api_t::specificDataGet.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_SpecificDataGet
 *
 *
 * @retval FSP_SUCCESS                           CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION                     Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN                      Module is not open.
 * @retval FSP_ERR_CTSU_SCANNING                 Scanning this instance.
 * @retval FSP_ERR_CTSU_INCOMPLETE_TUNING        Incomplete initial offset tuning.
 * @retval FSP_ERR_NOT_ENABLED                   Specify unsupported types.
 * @note When the specific_data_type is set to something other than CTSU_SPECIFIC_RAW_DATA, execute this API after
 * calling R_CTSU_DataGet().
 **********************************************************************************************************************/
fsp_err_t R_CTSU_SpecificDataGet (ctsu_ctrl_t * const       p_ctrl,
                                  uint16_t                * p_specific_data,
                                  ctsu_specific_data_type_t specific_data_type)
{
    fsp_err_t              err = FSP_SUCCESS;
    uint16_t               element_id;
    uint16_t               i;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ASSERT(p_specific_data);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    FSP_ERROR_RETURN(CTSU_STATE_SCANNING != p_instance_ctrl->state, FSP_ERR_CTSU_SCANNING);
    FSP_ERROR_RETURN(CTSU_TUNING_INCOMPLETE != p_instance_ctrl->tuning, FSP_ERR_CTSU_INCOMPLETE_TUNING);

#if (BSP_FEATURE_CTSU_VERSION == 1)
    FSP_ERROR_RETURN(CTSU_SPECIFIC_SELECTED_FREQ != specific_data_type, FSP_ERR_NOT_ENABLED);
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_MAJORITY_MODE & CTSU_JUDGEMENT_MAJORITY_MODE)
    FSP_ERROR_RETURN(CTSU_SPECIFIC_SELECTED_FREQ != specific_data_type, FSP_ERR_NOT_ENABLED);
    FSP_ERROR_RETURN(CTSU_SPECIFIC_CORRECTION_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
 #endif
 #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
    FSP_ERROR_RETURN(CTSU_SPECIFIC_RAW_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
 #endif

 #if (CTSU_CFG_MAJORITY_MODE & (CTSU_JUDGEMENT_MAJORITY_MODE | CTSU_VALUE_MAJORITY_MODE))
    if (p_instance_ctrl->p_ctsu_cfg->majority_mode == 1)
    {
        FSP_ERROR_RETURN(CTSU_SPECIFIC_CORRECTION_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
    }
 #endif

 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
    if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
    {
        if (1 == p_instance_ctrl->p_ctsu_cfg->majirimd)
        {
            FSP_ERROR_RETURN(CTSU_SPECIFIC_CCO_CORRECTION_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
            FSP_ERROR_RETURN(CTSU_SPECIFIC_CORRECTION_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
            FSP_ERROR_RETURN(CTSU_SPECIFIC_SELECTED_FREQ != specific_data_type, FSP_ERR_NOT_ENABLED);
        }
    }
    else
    {
        if (0 == p_instance_ctrl->p_ctsu_cfg->majority_mode)
        {
            FSP_ERROR_RETURN(CTSU_SPECIFIC_CCO_CORRECTION_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
        }
    }
 #endif

 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
    if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
    {
        if (0 == p_instance_ctrl->p_ctsu_cfg->majirimd)
        {
            FSP_ERROR_RETURN(CTSU_SPECIFIC_CORRECTION_DATA != specific_data_type, FSP_ERR_NOT_ENABLED);
            FSP_ERROR_RETURN(CTSU_SPECIFIC_SELECTED_FREQ != specific_data_type, FSP_ERR_NOT_ENABLED);
        }
    }
 #endif
#endif

    if (CTSU_SPECIFIC_RAW_DATA == specific_data_type)
    {
#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
            for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
            {
 #if (BSP_FEATURE_CTSU_VERSION == 1)
                *p_specific_data = (p_instance_ctrl->p_self_raw + element_id)->sen;
                p_specific_data++;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 2)
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
                    *p_specific_data = *(p_instance_ctrl->p_self_raw + (element_id * CTSU_CFG_NUM_SUMULTI) + i);
                    p_specific_data++;
                }
 #endif
            }
        }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
            {
 #if (BSP_FEATURE_CTSU_VERSION == 1)
                *p_specific_data = (p_instance_ctrl->p_mutual_raw + element_id)->pri_sen;
                p_specific_data++;
                *p_specific_data = (p_instance_ctrl->p_mutual_raw + element_id)->snd_sen;
                p_specific_data++;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 2)
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
                    *p_specific_data =
                        *(p_instance_ctrl->p_mutual_raw + (element_id * CTSU_CFG_NUM_SUMULTI * 2) + (i * 2));
                    p_specific_data++;
                    *p_specific_data =
                        *(p_instance_ctrl->p_mutual_raw + (element_id * CTSU_CFG_NUM_SUMULTI * 2) + (i * 2) + 1);
                    p_specific_data++;
                }
 #endif
            }
        }
#endif
    }
    else if (CTSU_SPECIFIC_CCO_CORRECTION_DATA == specific_data_type)
    {
#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
            for (element_id = 0; element_id < (p_instance_ctrl->num_elements); element_id++)
            {
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
 #if (BSP_FEATURE_CTSU_VERSION == 2)
  #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
                    if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
                    {
                        *p_specific_data =
                            *(p_instance_ctrl->p_self_raw + (element_id * CTSU_CFG_NUM_SUMULTI) + i);
                        p_specific_data++;
                    }
                    else
  #endif
 #endif
                    {
                        *p_specific_data =
                            *(p_instance_ctrl->p_self_corr + (element_id * CTSU_CFG_NUM_SUMULTI) + i);
                        p_specific_data++;
                    }
                }
            }
        }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            for (element_id = 0; element_id < (p_instance_ctrl->num_elements); element_id++)
            {
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
 #if (BSP_FEATURE_CTSU_VERSION == 2)
  #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
                    if (1 == p_instance_ctrl->p_ctsu_cfg->ajfen)
                    {
                        *p_specific_data =
                            *(p_instance_ctrl->p_mutual_raw +
                              ((element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2)));
                        p_specific_data++;
                        *p_specific_data =
                            *(p_instance_ctrl->p_mutual_raw +
                              ((element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2) + 1));
                        p_specific_data++;
                    }
                    else
  #endif
 #endif
                    {
                        *p_specific_data =
                            *(p_instance_ctrl->p_mutual_pri_corr + (element_id * CTSU_CFG_NUM_SUMULTI) +
                              i);
                        p_specific_data++;
                        *p_specific_data =
                            *(p_instance_ctrl->p_mutual_snd_corr + (element_id * CTSU_CFG_NUM_SUMULTI) +
                              i);
                        p_specific_data++;
                    }
                }
            }
        }
#endif
    }

#if (BSP_FEATURE_CTSU_VERSION == 2)
    else if (CTSU_SPECIFIC_CORRECTION_DATA == specific_data_type)
    {
 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
            for (element_id = 0; element_id < (p_instance_ctrl->num_elements); element_id++)
            {
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
                    *p_specific_data = *(p_instance_ctrl->p_self_mfc + (element_id * CTSU_CFG_NUM_SUMULTI) + i);
                    p_specific_data++;
                }
            }
        }
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            for (element_id = 0; element_id < (p_instance_ctrl->num_elements); element_id++)
            {
                for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
                {
                    *p_specific_data = *(p_instance_ctrl->p_mutual_pri_mfc + (element_id * CTSU_CFG_NUM_SUMULTI) + i);
                    p_specific_data++;
                    *p_specific_data = *(p_instance_ctrl->p_mutual_snd_mfc + (element_id * CTSU_CFG_NUM_SUMULTI) + i);
                    p_specific_data++;
                }
            }
        }
 #endif
    }
    else if (CTSU_SPECIFIC_SELECTED_FREQ == specific_data_type)
    {
 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
            for (element_id = 0; element_id < (p_instance_ctrl->num_elements); element_id++)
            {
                *p_specific_data = *(p_instance_ctrl->p_selected_freq_self + element_id);
                p_specific_data++;
            }
        }
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            for (element_id = 0; element_id < (p_instance_ctrl->num_elements); element_id++)
            {
                *p_specific_data = *(p_instance_ctrl->p_selected_freq_mutual + element_id);
                p_specific_data++;
            }
        }
 #endif
    }
#endif
    else
    {
    }

    return err;
}

/*******************************************************************************************************************//**
 * @brief This function inserts the value of the second argument as the measurement result value.
 * Call this function after calling the R_CTSU_DataInsert() function.
 * Implements @ref ctsu_api_t::dataInsert.
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_DataInsert
 *
 * @retval FSP_SUCCESS                           CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION                     Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN                      Module is not open.
 * @retval FSP_ERR_CTSU_SCANNING                 Scanning this instance.
 * @retval FSP_ERR_CTSU_INCOMPLETE_TUNING        Incomplete initial offset tuning.
 **********************************************************************************************************************/
fsp_err_t R_CTSU_DataInsert (ctsu_ctrl_t * const p_ctrl, uint16_t * p_insert_data)
{
    fsp_err_t              err             = FSP_SUCCESS;
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;
    uint16_t               element_id;
    uint16_t               majority_mode_elem_id;
#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ASSERT(p_insert_data);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    FSP_ERROR_RETURN(CTSU_STATE_SCANNING != p_instance_ctrl->state, FSP_ERR_CTSU_SCANNING);
    FSP_ERROR_RETURN(CTSU_TUNING_INCOMPLETE != p_instance_ctrl->tuning, FSP_ERR_CTSU_INCOMPLETE_TUNING);

#if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    if (CTSU_MODE_SELF_MULTI_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        /* Data output */
        for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
        {
            for (majority_mode_elem_id = 0;
                 majority_mode_elem_id < CTSU_MAJORITY_MODE_ELEMENTS;
                 majority_mode_elem_id++)
            {
                (p_instance_ctrl->p_self_data + (element_id * CTSU_MAJORITY_MODE_ELEMENTS) +
                 majority_mode_elem_id)->int_data = *p_insert_data;
                p_insert_data++;
            }
        }
    }
#endif
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
        {
            for (majority_mode_elem_id = 0;
                 majority_mode_elem_id < CTSU_MAJORITY_MODE_ELEMENTS;
                 majority_mode_elem_id++)
            {
                (p_instance_ctrl->p_mutual_pri_data + (element_id * CTSU_MAJORITY_MODE_ELEMENTS) +
                 majority_mode_elem_id)->int_data = *p_insert_data;
                p_insert_data++;
                (p_instance_ctrl->p_mutual_snd_data + (element_id * CTSU_MAJORITY_MODE_ELEMENTS) +
                 majority_mode_elem_id)->int_data = *p_insert_data;
                p_insert_data++;
            }
        }
    }
#endif

    return err;
}

/*******************************************************************************************************************//**
 * @} (end addtogroup CTSU)
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private Functions
 **********************************************************************************************************************/

/*******************************************************************************************************************//**
 * Internal ctsu private function.
 **********************************************************************************************************************/

#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_transfer_open
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_open (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_cfg_t              cfg;
    transfer_info_t           * p_info_bk;

    /* CTSUWR setting */
    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_tx;
    cfg        = *(p_transfer->p_cfg);
    p_info_bk  = cfg.p_info;
    cfg.p_info = NULL;

    err = p_transfer->p_api->open(p_transfer->p_ctrl, &cfg);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    cfg.p_info = p_info_bk;

    /* CTSURD setting */
    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_rx;
    cfg        = *(p_transfer->p_cfg);
    p_info_bk  = cfg.p_info;
    cfg.p_info = NULL;

    err = p_transfer->p_api->open(p_transfer->p_ctrl, &cfg);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    cfg.p_info = p_info_bk;

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_close
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_close (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;

    /* CTSUWR setting */
    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_tx;
    err        = p_transfer->p_api->close(p_transfer->p_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    /* CTSURD setting */
    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_rx;
    err        = p_transfer->p_api->close(p_transfer->p_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_configure
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_configure (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;

 #if (BSP_FEATURE_CTSU_VERSION == 2)

    /* After the initial offset tuning is completed, set the DTC for automatic judgement.                              */
    /* In addition, the method for which automatic judgement is disabled does not set the DTC for automatic judgement. */
    /* Alternatively, DTC for automatic judgment will not be set during correction measurement.                        */
    if ((0 == p_instance_ctrl->p_ctsu_cfg->ajfen) || (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status) ||
        (CTSU_TUNING_INCOMPLETE == p_instance_ctrl->tuning))
    {
        err = ctsu_transfer_normal(p_instance_ctrl);
        FSP_ERROR_RETURN(FSP_SUCCESS == err, err);
    }

  #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
    else
    {
        err = ctsu_transfer_autojudge(p_instance_ctrl);
        FSP_ERROR_RETURN(FSP_SUCCESS == err, err);
    }
  #endif
 #endif

 #if (BSP_FEATURE_CTSU_VERSION == 1)
    err = ctsu_transfer_ctsu1(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);
 #endif

    return FSP_SUCCESS;
}

 #if (BSP_FEATURE_CTSU_VERSION == 2)

/***********************************************************************************************************************
 * ctsu_transfer_normal
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_normal (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;

    err = ctsu_transfer_normal_ctsuwr(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    err = ctsu_transfer_normal_ctsurd(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_normal_ctsuwr
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_normal_ctsuwr (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_info_t           * p_info;
    uint16_t calc_transfer_count;
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    uint16_t transfer_mutual_num_elements = 0;
  #endif

    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_tx;
    p_info     = p_transfer->p_cfg->p_info;

    calc_transfer_count = p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI;

    ctsu_transfer_ctsuso_set(p_info, 0, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 0, p_instance_ctrl->p_ctsuwr, (void *) &R_CTSU->CTSUSO);

    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
        p_info[0].num_blocks = 1;
        p_info[0].p_src      = (void *) &(g_ctsu_correction_info.ctsuwr);
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    else if (CTSU_CORRECTION_RUN == g_ctsu_corrcfc_info.status)
    {
        p_info[0].num_blocks = 1;
        p_info[0].p_src      = (void *) &(g_ctsu_corrcfc_info.ctsuwr);
    }
  #endif
  #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    else if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        p_info[0].num_blocks = 1;
    }
  #endif
    else
    {
        if (CTSU_MODE_CURRENT_SCAN == p_instance_ctrl->md)
        {
            p_info[0].num_blocks = p_instance_ctrl->num_elements;
        }
        else
        {
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
            if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
            {
                if (true == p_instance_ctrl->serial_tuning_enable)
                {
                    if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
                    {
                        ctsu_transer_count_element(p_instance_ctrl->ctsuchac0, &transfer_mutual_num_elements);
                        ctsu_transer_count_element(p_instance_ctrl->ctsuchac1, &transfer_mutual_num_elements);
                        ctsu_transer_count_element(p_instance_ctrl->ctsuchac2, &transfer_mutual_num_elements);
                        ctsu_transer_count_element(p_instance_ctrl->ctsuchac3, &transfer_mutual_num_elements);
                        ctsu_transer_count_element(p_instance_ctrl->ctsuchac4, &transfer_mutual_num_elements);

                        p_info[0].num_blocks = transfer_mutual_num_elements * CTSU_CFG_NUM_SUMULTI;
                    }
                }
            }
  #endif
  #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
            if ((0 == p_instance_ctrl->p_ctsu_cfg->majority_mode) &&
                (CTSU_TUNING_INCOMPLETE != p_instance_ctrl->tuning))
            {
                ctsu_transfer_mcact_set(p_info, 0, calc_transfer_count);
                ctsu_transfer_address_set(p_info, 0, p_instance_ctrl->p_mcact1, (void *) &R_CTSU->CTSUMACT1);

                ctsu_transfer_ctsuso_set(p_info, 1, calc_transfer_count);
                ctsu_transfer_address_set(p_info, 1, p_instance_ctrl->p_ctsuwr, (void *) &R_CTSU->CTSUSO);

   #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
                if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
                {
                    if (true == p_instance_ctrl->serial_tuning_enable)
                    {
                        if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
                        {
                            p_info[0].num_blocks = transfer_mutual_num_elements * CTSU_CFG_NUM_SUMULTI;
                            p_info[1].num_blocks = transfer_mutual_num_elements * CTSU_CFG_NUM_SUMULTI;
                        }
                    }
                }
   #endif
            }
  #endif
        }
    }

    err = p_transfer->p_api->reconfigure(p_transfer->p_ctrl, p_info);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_normal_ctsurd
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_normal_ctsurd (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_info_t           * p_info;
    uint16_t calc_transfer_count;

    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_rx;
    p_info     = p_transfer->p_cfg->p_info;

    calc_transfer_count = p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI;

    ctsu_transfer_ctsuscnt_set(p_info, 0, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 0, (void *) &R_CTSU->CTSUSC, p_instance_ctrl->p_self_raw);

    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
        p_info[0].transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
        p_info[0].num_blocks = 1;
        p_info[0].p_dest     = (void *) &g_ctsu_correction_info.scanbuf;
        p_info[0].p_src      = (void *) &R_CTSU->CTSUSC;
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    else if (CTSU_CORRECTION_RUN == g_ctsu_corrcfc_info.status)
    {
        p_info[0].transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
        p_info[0].length     = g_ctsu_corrcfc_info.num_ts;
        p_info[0].num_blocks = 1;
        p_info[0].p_dest     = (void *) g_ctsu_corrcfc_info.scanbuf;
        p_info[0].p_src      = (void *) &R_CTSU->CTSUCFCCNT;
    }
  #endif
  #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    else if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        p_info[0].transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
        if (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
        {
            p_info[0].p_dest = (void *) p_instance_ctrl->p_self_raw;
            p_info[0].p_src  = (void *) &R_CTSU->CTSUCFCCNT;
        }
        else if ((CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) ||
                 (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state))
        {
            /* Set the address of register CTSUSCNT.SUCKCNT */
            p_info[0].p_src = (void *) ((&R_CTSU->CTSUSC) + 1);
        }
        else
        {
            p_info[0].p_src = (void *) &R_CTSU->CTSUSC;
        }
    }
  #endif
    else
    {
        if (CTSU_MODE_CURRENT_SCAN == p_instance_ctrl->md)
        {
            p_info[0].num_blocks = p_instance_ctrl->num_elements;
        }
        else
        {
            p_info[0].num_blocks = (uint16_t) (p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI);
        }

        p_info[0].p_dest = (void *) p_instance_ctrl->p_self_raw;
        p_info[0].p_src  = (void *) &R_CTSU->CTSUSC;
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            p_info[0].p_dest     = (void *) p_instance_ctrl->p_mutual_raw;
            p_info[0].num_blocks = (uint16_t) (p_info->num_blocks * 2); ///< Primary and Secondary
        }

   #if (CTSU_CFG_NUM_CFC != 0)
        if (CTSU_MODE_MUTUAL_CFC_SCAN == p_instance_ctrl->md)
        {
            p_info[0].length     = p_instance_ctrl->p_ctsu_cfg->num_rx;
            p_info[0].num_blocks = (uint16_t) (p_info[0].num_blocks / p_instance_ctrl->p_ctsu_cfg->num_rx);
            p_info[0].p_src      = (void *) &R_CTSU->CTSUCFCCNT;
        }
   #endif
  #endif
    }

    err = p_transfer->p_api->reconfigure(p_transfer->p_ctrl, p_info);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

  #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_transfer_autojudge
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_autojudge (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;

    err = ctsu_transfer_autojudge_ctsuwr(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    err = ctsu_transfer_autojudge_ctsurd(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_autojudge_ctsuwr
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_autojudge_ctsuwr (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_info_t           * p_info;
    uint16_t calc_transfer_count;

    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_tx;
    p_info     = p_transfer->p_cfg->p_info;

   #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
    if (1 == p_instance_ctrl->p_ctsu_cfg->majirimd)
    {
        calc_transfer_count = p_instance_ctrl->num_elements;

        /* For automatic judgement register transfer */
        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 0, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 0, (void *) p_instance_ctrl->p_ajthr, (void *) &R_CTSU->CTSUAJTHR);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 1, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 1, (void *) p_instance_ctrl->p_ajmmar, (void *) &R_CTSU->CTSUAJMMAR);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 2, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 2, (void *) p_instance_ctrl->p_ajblact, (void *) &R_CTSU->CTSUAJBLACT);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 3, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 3, (void *) p_instance_ctrl->p_ajblar, (void *) &R_CTSU->CTSUAJBLAR);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 4, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 4, (void *) p_instance_ctrl->p_ajrr, (void *) &R_CTSU->CTSUAJRR);

        /* For CTSUMCACT1 register transfer */
        ctsu_transfer_mcact_set(p_info, 5, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 5, p_instance_ctrl->p_mcact1, (void *) &R_CTSU->CTSUMACT1);

        /* For CTSUMCACT2 register transfer */
        ctsu_transfer_mcact_set(p_info, 6, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 6, p_instance_ctrl->p_mcact2, (void *) &R_CTSU->CTSUMACT2);

        /* For CTSUSO register transfer */
        ctsu_transfer_autojudge_ctsuso_set(p_info, 7, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 7, (void *) p_instance_ctrl->p_ctsuwr, (void *) &R_CTSU->CTSUSO);
    }
    else
   #endif
    {
        calc_transfer_count = p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI;

        /* For automatic judgement register transfer */
        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 0, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 0, (void *) p_instance_ctrl->p_ajthr, (void *) &R_CTSU->CTSUAJTHR);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 1, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 1, (void *) p_instance_ctrl->p_ajmmar, (void *) &R_CTSU->CTSUAJMMAR);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 2, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 2, (void *) p_instance_ctrl->p_ajblact, (void *) &R_CTSU->CTSUAJBLACT);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 3, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 3, (void *) p_instance_ctrl->p_ajblar, (void *) &R_CTSU->CTSUAJBLAR);

        ctsu_transfer_autojudge_ctsuwr_value_set(p_info, 4, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 4, (void *) p_instance_ctrl->p_ajrr, (void *) &R_CTSU->CTSUAJRR);

        /* For CTSUSO register transfer */
        ctsu_transfer_autojudge_ctsuso_set(p_info, 5, calc_transfer_count);
        ctsu_transfer_address_set(p_info, 5, (void *) p_instance_ctrl->p_ctsuwr, (void *) &R_CTSU->CTSUSO);
    }

    err = p_transfer->p_api->reconfigure(p_transfer->p_ctrl, p_info);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_autojudge_ctsurd
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_autojudge_ctsurd (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_info_t           * p_info;
    uint16_t calc_transfer_count;

    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_rx;
    p_info     = p_transfer->p_cfg->p_info;

   #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
    if (1 == p_instance_ctrl->p_ctsu_cfg->majirimd)
    {
        calc_transfer_count = p_instance_ctrl->num_elements;
    }
    else
   #endif
    {
        calc_transfer_count = p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI;
    }

    /* For automatic judgement register transfer */
    ctsu_transfer_autojudge_ctsurd_value_set(p_info, 0, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 0, (void *) &R_CTSU->CTSUAJMMAR, (void *) p_instance_ctrl->p_ajmmar);

    ctsu_transfer_autojudge_ctsurd_value_set(p_info, 1, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 1, (void *) &R_CTSU->CTSUAJBLACT, (void *) p_instance_ctrl->p_ajblact);

    ctsu_transfer_autojudge_ctsurd_value_set(p_info, 2, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 2, (void *) &R_CTSU->CTSUAJBLAR, (void *) p_instance_ctrl->p_ajblar);

    ctsu_transfer_autojudge_ctsurd_value_set(p_info, 3, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 3, (void *) &R_CTSU->CTSUAJRR, (void *) p_instance_ctrl->p_ajrr);

    /* For CTSUSCNT register transfer */
    ctsu_transfer_autojudge_ctsuscnt_set(p_info, 4, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 4, (void *) &R_CTSU->CTSUSC, (void *) p_instance_ctrl->p_self_raw);
   #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
    {
        p_info[4].p_dest = (void *) p_instance_ctrl->p_mutual_raw;
    }
   #endif

    err = p_transfer->p_api->reconfigure(p_transfer->p_ctrl, p_info);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

  #endif
 #endif

 #if (BSP_FEATURE_CTSU_VERSION == 1)

/***********************************************************************************************************************
 * ctsu_transfer_ctsu1
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_ctsu1 (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;

    err = ctsu_transfer_ctsu1_ctsuwr(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    err = ctsu_transfer_ctsu1_ctsurd(p_instance_ctrl);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_ctsu1_ctsuwr
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_ctsu1_ctsuwr (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_info_t           * p_info;
    uint16_t calc_transfer_count;

    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_tx;
    p_info     = p_transfer->p_cfg->p_info;

    calc_transfer_count = p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI;

    ctsu_transfer_ctsuso_set(p_info, 0, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 0, p_instance_ctrl->p_ctsuwr, (void *) &R_CTSU->CTSUSSC);

    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
        p_info[0].num_blocks = 1;
        p_info[0].p_src      = (void *) &(g_ctsu_correction_info.ctsuwr);
    }

  #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    else if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        p_info[0].num_blocks = 1;
        p_info[0].p_src      = (void *) &(g_ctsu_diag_info.ctsuwr);
    }
  #endif
    else if ((CTSU_CORRECTION_RUN != g_ctsu_correction_info.status) &&
             (CTSU_MODE_DIAGNOSIS_SCAN != p_instance_ctrl->md))
    {
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            if (true == p_instance_ctrl->serial_tuning_enable)
            {
                if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
                {
                    p_info[0].num_blocks = p_instance_ctrl->num_elements * 2;
                }
            }
        }
  #endif
    }
    else
    {
    }

    err = p_transfer->p_api->reconfigure(p_transfer->p_ctrl, p_info);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

/***********************************************************************************************************************
 * ctsu_transfer_ctsu1_ctsurd
 ***********************************************************************************************************************/
fsp_err_t ctsu_transfer_ctsu1_ctsurd (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;
    transfer_instance_t const * p_transfer;
    transfer_info_t           * p_info;
    uint16_t calc_transfer_count;

    p_transfer = p_instance_ctrl->p_ctsu_cfg->p_transfer_rx;
    p_info     = p_transfer->p_cfg->p_info;

    calc_transfer_count = p_instance_ctrl->num_elements * CTSU_CFG_NUM_SUMULTI;

    ctsu_transfer_ctsuscnt_set(p_info, 0, calc_transfer_count);
    ctsu_transfer_address_set(p_info, 0, (void *) &R_CTSU->CTSUSC, p_instance_ctrl->p_self_raw);

    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
        p_info[0].num_blocks = 1;
        p_info[0].p_dest     = (void *) &g_ctsu_correction_info.scanbuf;
    }

  #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    else if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        p_info[0].num_blocks = 1;
        p_info[0].p_dest     = (void *) &g_ctsu_diag_info.scanbuf;
    }
  #endif
    else if ((CTSU_CORRECTION_RUN != g_ctsu_correction_info.status) &&
             (CTSU_MODE_DIAGNOSIS_SCAN != p_instance_ctrl->md))
    {
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            p_info[0].p_dest     = p_instance_ctrl->p_mutual_raw;
            p_info[0].num_blocks = (uint16_t) (p_info->num_blocks * 2); ///< Primary and Secondary

            if (true == p_instance_ctrl->serial_tuning_enable)
            {
                if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
                {
                    p_info[0].num_blocks = p_instance_ctrl->num_elements * 2;
                }
            }
        }
  #endif
    }
    else
    {
    }

    err = p_transfer->p_api->reconfigure(p_transfer->p_ctrl, p_info);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    return FSP_SUCCESS;
}

 #endif

/***********************************************************************************************************************
 * ctsu_transfer_ctsuso_set
 ***********************************************************************************************************************/
void ctsu_transfer_ctsuso_set (transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode    = TRANSFER_CHAIN_MODE_DISABLED;
    p_info[array_number].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
 #if (BSP_FEATURE_CTSU_VERSION == 2)
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
 #endif
    p_info[array_number].transfer_settings_word_b.irq         = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode        = TRANSFER_MODE_BLOCK;
    p_info[array_number].transfer_settings_word_b.repeat_area = TRANSFER_REPEAT_AREA_DESTINATION;
 #if (BSP_FEATURE_CTSU_VERSION == 2)
    p_info[array_number].transfer_settings_word_b.size = TRANSFER_SIZE_4_BYTE;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    p_info[array_number].transfer_settings_word_b.size = TRANSFER_SIZE_2_BYTE;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 2)
    p_info[array_number].length = 1;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    p_info[array_number].length = 3;
 #endif
    p_info[array_number].num_blocks = set_transfer_count;
}

/***********************************************************************************************************************
 * ctsu_transfer_ctsuscnt_set
 ***********************************************************************************************************************/
void ctsu_transfer_ctsuscnt_set (transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode = TRANSFER_CHAIN_MODE_DISABLED;
 #if (BSP_FEATURE_CTSU_VERSION == 2)
    p_info[array_number].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_FIXED;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    p_info[array_number].transfer_settings_word_b.src_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
 #endif
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    p_info[array_number].transfer_settings_word_b.irq            = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode           = TRANSFER_MODE_BLOCK;
    p_info[array_number].transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_SOURCE;
    p_info[array_number].transfer_settings_word_b.size           = TRANSFER_SIZE_2_BYTE;
 #if (BSP_FEATURE_CTSU_VERSION == 2)
    p_info[array_number].length = 1;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    p_info[array_number].length = 2;
 #endif
    p_info[array_number].num_blocks = set_transfer_count;
}

/***********************************************************************************************************************
 * ctsu_transfer_address_set
 ***********************************************************************************************************************/
void ctsu_transfer_address_set (transfer_info_t * p_info,
                                uint8_t           array_number,
                                void            * p_set_source_addr,
                                void            * p_set_dest_addr)
{
    p_info[array_number].p_src  = p_set_source_addr;
    p_info[array_number].p_dest = p_set_dest_addr;
}

 #if (BSP_FEATURE_CTSU_VERSION == 2)
  #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_transfer_autojudge_value_set
 ***********************************************************************************************************************/
void ctsu_transfer_autojudge_ctsuwr_value_set (transfer_info_t * p_info,
                                               uint8_t           array_number,
                                               uint16_t          set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode     = TRANSFER_CHAIN_MODE_EACH;
    p_info[array_number].transfer_settings_word_b.src_addr_mode  = TRANSFER_ADDR_MODE_INCREMENTED;
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    p_info[array_number].transfer_settings_word_b.irq            = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode           = TRANSFER_MODE_REPEAT;
    p_info[array_number].transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_SOURCE;
    p_info[array_number].transfer_settings_word_b.size           = TRANSFER_SIZE_4_BYTE;
    p_info[array_number].length = set_transfer_count;
}

/***********************************************************************************************************************
 * ctsu_transfer_autojudge_ctsuso_set
 ***********************************************************************************************************************/
void ctsu_transfer_autojudge_ctsuso_set (transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode     = TRANSFER_CHAIN_MODE_DISABLED;
    p_info[array_number].transfer_settings_word_b.src_addr_mode  = TRANSFER_ADDR_MODE_INCREMENTED;
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    p_info[array_number].transfer_settings_word_b.irq            = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode           = TRANSFER_MODE_REPEAT;
    p_info[array_number].transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_SOURCE;
    p_info[array_number].transfer_settings_word_b.size           = TRANSFER_SIZE_4_BYTE;
    p_info[array_number].length = set_transfer_count;
}

/***********************************************************************************************************************
 * ctsu_transfer_autojudge_ctsurd_value_set
 ***********************************************************************************************************************/
void ctsu_transfer_autojudge_ctsurd_value_set (transfer_info_t * p_info,
                                               uint8_t           array_number,
                                               uint16_t          set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode     = TRANSFER_CHAIN_MODE_EACH;
    p_info[array_number].transfer_settings_word_b.src_addr_mode  = TRANSFER_ADDR_MODE_FIXED;
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    p_info[array_number].transfer_settings_word_b.irq            = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode           = TRANSFER_MODE_REPEAT;
    p_info[array_number].transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_DESTINATION;
    p_info[array_number].transfer_settings_word_b.size           = TRANSFER_SIZE_4_BYTE;
    p_info[array_number].length = set_transfer_count;
}

/***********************************************************************************************************************
 * ctsu_transfer_autojudge_ctsuscnt_set
 ***********************************************************************************************************************/
void ctsu_transfer_autojudge_ctsuscnt_set (transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode     = TRANSFER_CHAIN_MODE_DISABLED;
    p_info[array_number].transfer_settings_word_b.src_addr_mode  = TRANSFER_ADDR_MODE_FIXED;
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED;
    p_info[array_number].transfer_settings_word_b.irq            = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode           = TRANSFER_MODE_REPEAT;
    p_info[array_number].transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_DESTINATION;
    p_info[array_number].transfer_settings_word_b.size           = TRANSFER_SIZE_2_BYTE;
    p_info[array_number].length = set_transfer_count;
}

  #endif

  #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_transfer_mcact_set
 ***********************************************************************************************************************/
void ctsu_transfer_mcact_set (transfer_info_t * p_info, uint8_t array_number, uint16_t set_transfer_count)
{
    p_info[array_number].transfer_settings_word_b.chain_mode     = TRANSFER_CHAIN_MODE_EACH;
    p_info[array_number].transfer_settings_word_b.src_addr_mode  = TRANSFER_ADDR_MODE_INCREMENTED;
    p_info[array_number].transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED;
    p_info[array_number].transfer_settings_word_b.irq            = TRANSFER_IRQ_END;
    p_info[array_number].transfer_settings_word_b.mode           = TRANSFER_MODE_REPEAT;
    p_info[array_number].transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_SOURCE;
    p_info[array_number].transfer_settings_word_b.size           = TRANSFER_SIZE_4_BYTE;
    p_info[array_number].length = set_transfer_count;
}

  #endif

  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
static void ctsu_transer_count_element (uint32_t element_mask, uint16_t * num_element)
{
    uint8_t n;

    /* Get the number of measurable elements from enabled CHAC or CHTRC  */
    for (n = 0; n < CTSU_TRANSFER_TUNING_CH_REG_MAX_NUM; n++)
    {
        if (0x00000001 == ((element_mask >> n) & 0x00000001))
        {
            (*num_element)++;
        }
    }
}

  #endif
 #endif

#endif

/***********************************************************************************************************************
 * ctsu_initial_offset_tuning
 ***********************************************************************************************************************/
void ctsu_initial_offset_tuning (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t element_id;
    int32_t  diff          = 0;
    uint32_t complete_flag = 0;
    uint32_t num_complete  = 0;
    uint32_t target_val;
#if (BSP_FEATURE_CTSU_VERSION == 1)
    uint16_t               ctsuso;
    ctsu_correction_calc_t calc;
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
    uint16_t i;
    uint16_t element_top;
    uint16_t corr_data[CTSU_CFG_NUM_SUMULTI];
    int32_t  ctsuso;
    uint32_t snum;
    int32_t  offset_unit;
#endif

    /* element_id through each element for control block */
    for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
    {
        if (0 == *(p_instance_ctrl->p_element_complete_flag + element_id))
        {
#if (BSP_FEATURE_CTSU_VERSION == 1)
            if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
            {
                target_val = (p_instance_ctrl->tuning_self_target_value);
            }
            else
            {
                target_val = (p_instance_ctrl->tuning_mutual_target_value);
            }

            calc.snum  = (p_instance_ctrl->p_ctsuwr[element_id].ctsuso0 >> 10) & CTSU_SNUM_MAX;
            calc.sdpa  = (p_instance_ctrl->p_ctsuwr[element_id].ctsuso1 >> 8) & CTSU_SDPA_MAX;
            target_val = (uint32_t) (target_val * (uint32_t) ((calc.snum + 1) * (calc.sdpa + 1)) /
                                     g_ctsu_correction_info.ctsu_clock);
            if (CTSU_COUNT_MAX < target_val)
            {
                target_val = CTSU_COUNT_MAX;
            }

 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
            if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
            {
                diff = (int32_t) ((p_instance_ctrl->p_self_data + element_id)->int_data - target_val);
            }
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
            if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
            {
                diff = (int32_t) ((p_instance_ctrl->p_mutual_pri_data + element_id)->int_data - target_val);
            }
 #endif
            ctsuso = (p_instance_ctrl->p_ctsuwr[element_id].ctsuso0 & CTSU_TUNING_MAX);
            if (0 < diff)
            {
                if (*(p_instance_ctrl->p_tuning_diff + element_id) < 0)
                {
                    if ((-diff) > *(p_instance_ctrl->p_tuning_diff + element_id))
                    {
                        ctsuso++;      ///< Decrease count
                    }

                    complete_flag = 1;
                }
                else
                {
                    if (CTSU_TUNING_MAX == ctsuso) /* CTSUSO limit check    */
                    {
                        complete_flag = 1;
                    }
                    else
                    {
                        ctsuso++;                                                ///< Decrease count
                        (*(p_instance_ctrl->p_tuning_diff + element_id)) = diff; ///< Plus
                    }
                }
            }
            else if (0 == diff)
            {
                complete_flag = 1;
            }
            else
            {
                if (*(p_instance_ctrl->p_tuning_diff + element_id) > 0)
                {
                    if ((-diff) > *(p_instance_ctrl->p_tuning_diff + element_id))
                    {
                        ctsuso--;      ///< Increase count
                    }

                    complete_flag = 1;
                }
                else
                {
                    if (CTSU_TUNING_MIN == ctsuso) /* CTSUSO limit check    */
                    {
                        complete_flag = 1;
                    }
                    else
                    {
                        ctsuso--;                                                ///< Increase count
                        (*(p_instance_ctrl->p_tuning_diff + element_id)) = diff; ///< Minus
                    }
                }
            }

            p_instance_ctrl->p_ctsuwr[element_id].ctsuso0 &= (uint16_t) (~CTSU_TUNING_MAX);
            p_instance_ctrl->p_ctsuwr[element_id].ctsuso0 |= ctsuso;
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
            element_top = (uint16_t) (element_id * CTSU_CFG_NUM_SUMULTI);
            for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
            {
                /* Adjust only frequencies for which offset tuning is not completed */
                if (0 == (p_instance_ctrl->p_frequency_complete_flag[element_id] & (1 << i)))
                {
                    if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
                    {
                        target_val = (p_instance_ctrl->tuning_self_target_value / 2);
                    }
                    else
                    {
                        target_val = (p_instance_ctrl->tuning_mutual_target_value / 2);
                    }

                    if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
                    {
                        corr_data[i] = p_instance_ctrl->p_self_corr[element_top + i];
                    }
                    else
                    {
                        corr_data[i] = p_instance_ctrl->p_mutual_pri_corr[element_top + i];
                    }

                    snum = (p_instance_ctrl->p_ctsuwr[(element_id * CTSU_CFG_NUM_SUMULTI)].ctsuso >> 10) &
                           CTSU_SNUM_MAX;
                    target_val = (uint32_t) ((target_val * (snum + 1)) / (CTSU_SNUM_RECOMMEND + 1));
                    if (CTSU_COUNT_MAX < target_val)
                    {
                        target_val = CTSU_COUNT_MAX;
                    }

                    offset_unit = (int32_t) ((CTSU_CORRECTION_OFFSET_UNIT * (snum + 1)) / (CTSU_SNUM_RECOMMEND + 1));

                    /* Calculate CTSUSO equivalent difference between current value and target value */
                    diff = (int32_t) ((int32_t) corr_data[i] - (int32_t) target_val) /
                           (offset_unit >> p_instance_ctrl->range);

                    ctsuso  = (int32_t) (p_instance_ctrl->p_ctsuwr[element_top + i].ctsuso & CTSU_TUNING_MAX);
                    ctsuso += diff;

                    /* If the CTSUSO exceeds the minimum value or the maximum value, tuning complete */
                    if (ctsuso < 0)
                    {
                        ctsuso = 0;
                        p_instance_ctrl->p_frequency_complete_flag[element_id] += (uint8_t) (1 << i);
                    }
                    else if (ctsuso > CTSU_TUNING_MAX)
                    {
                        ctsuso = CTSU_TUNING_MAX;
                        p_instance_ctrl->p_frequency_complete_flag[element_id] += (uint8_t) (1 << i);
                    }
                    else
                    {
                        /* If the difference is large, tuning value may not be able to match, so create the next opportunity */
                        if (0 == diff)
                        {
                            p_instance_ctrl->p_frequency_complete_flag[element_id] += (uint8_t) (1 << i);
                        }
                    }

                    /* Set the result of the calculated CTSUSO */
                    p_instance_ctrl->p_ctsuwr[element_top + i].ctsuso &= (uint32_t) (~CTSU_TUNING_MAX);
                    p_instance_ctrl->p_ctsuwr[element_top + i].ctsuso |= (uint32_t) ctsuso;
                }

                /* Add completion status for each frequency */
                complete_flag += ((p_instance_ctrl->p_frequency_complete_flag[element_id] >> i) & 1);
            }
#endif
        }
        else
        {
            complete_flag = CTSU_CFG_NUM_SUMULTI;
        }

        if (CTSU_CFG_NUM_SUMULTI == complete_flag)
        {
            num_complete++;
            *(p_instance_ctrl->p_element_complete_flag + element_id) = 1;
        }

        complete_flag = 0;
    }

    if (num_complete == p_instance_ctrl->num_elements)
    {
        p_instance_ctrl->tuning = CTSU_TUNING_COMPLETE;
    }
}

/***********************************************************************************************************************
 * ctsu_moving_average
 ***********************************************************************************************************************/
void ctsu_moving_average (ctsu_data_t * p_average, uint16_t new_data, uint16_t average_num)
{
    uint32_t work;

    work  = (uint32_t) (((uint32_t) p_average->int_data << CTSU_CFG_DECIMAL_POINT) + p_average->decimal_point_data);
    work -= (uint32_t) (work / average_num);
    work += (uint32_t) (((uint32_t) new_data << CTSU_CFG_DECIMAL_POINT) / average_num);

    p_average->int_data           = (uint16_t) (work >> CTSU_CFG_DECIMAL_POINT);
    p_average->decimal_point_data = (uint16_t) (work & CTSU_CFG_DECIMAL_POINT_MASK);
}

/***********************************************************************************************************************
 * CTSUWR interrupt handler. This service routine sets the tuning for the next element to be scanned by hardware.
 ***********************************************************************************************************************/
void ctsu_write_isr (void)
{
    /* Save context if RTOS is used */
    FSP_CONTEXT_SAVE

#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
 #if (BSP_FEATURE_ICU_HAS_IELSR)

    /** Clear the BSP IRQ Flag     */
    R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());
 #endif
#else
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) R_FSP_IsrContextGet(irq);
 #if (BSP_FEATURE_ICU_HAS_IELSR)

    /** Clear the BSP IRQ Flag     */
    R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());
 #endif

    /* Write settings for current element */
    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
 #if (BSP_FEATURE_CTSU_VERSION == 2)
        R_CTSU->CTSUSO = g_ctsu_correction_info.ctsuwr.ctsuso;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
        R_CTSU->CTSUSSC = g_ctsu_correction_info.ctsuwr.ctsussc;
        R_CTSU->CTSUSO0 = g_ctsu_correction_info.ctsuwr.ctsuso0;
        R_CTSU->CTSUSO1 = g_ctsu_correction_info.ctsuwr.ctsuso1;
 #endif
    }

 #if (BSP_FEATURE_CTSU_VERSION == 2)
  #if (CTSU_CFG_NUM_CFC != 0)
    else if ((CTSU_CORRECTION_RUN == g_ctsu_corrcfc_info.status))
    {
        R_CTSU->CTSUSO = g_ctsu_corrcfc_info.ctsuwr.ctsuso;
    }
  #endif
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    else if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
  #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
        R_CTSU->CTSUSSC = g_ctsu_diag_info.ctsuwr.ctsussc;
        R_CTSU->CTSUSO0 = g_ctsu_diag_info.ctsuwr.ctsuso0;
        R_CTSU->CTSUSO1 = g_ctsu_diag_info.ctsuwr.ctsuso1;
  #endif
    }
 #endif
    else
    {
 #if (BSP_FEATURE_CTSU_VERSION == 2)
        R_CTSU->CTSUSO = p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
        R_CTSU->CTSUSSC = p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsussc;
        R_CTSU->CTSUSO0 = p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso0;
        R_CTSU->CTSUSO1 = p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso1;
 #endif
        p_instance_ctrl->wr_index++;
    }
#endif

    /* Restore context if RTOS is used */
    FSP_CONTEXT_RESTORE
}

/***********************************************************************************************************************
 * CTSURD interrupt handler. This service routine reads the sensor count and reference counter for
 * the current element and places the value in the scan data buffer. Note that the reference counter
 * does not work properly but is saved anyway for backward compatibility and potential future use.
 * Additionally, the SC register cannot be read again until RC is read.
 ***********************************************************************************************************************/
void ctsu_read_isr (void)
{
    /* Save context if RTOS is used */
    FSP_CONTEXT_SAVE

#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)

    /** Clear the BSP IRQ Flag     */
 #if (BSP_FEATURE_ICU_HAS_IELSR)
    R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());
 #endif
#else
    IRQn_Type irq = R_FSP_CurrentIrqGet();
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) R_FSP_IsrContextGet(irq);
 #if (BSP_FEATURE_ICU_HAS_IELSR)

    /** Clear the BSP IRQ Flag     */
    R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());
 #endif

    /* read current channel/element value */
    /* Store the reference counter for possible future use. Register must be read or scan will hang. */

 #if (BSP_FEATURE_CTSU_VERSION == 1)
    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
        g_ctsu_correction_info.scanbuf.sen = R_CTSU->CTSUSC;
        g_ctsu_correction_info.scanbuf.ref = R_CTSU->CTSURC;
    }

  #if (BSP_FEATURE_CTSU_VERSION == 1)
   #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    else if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        g_ctsu_diag_info.scanbuf.sen = R_CTSU->CTSUSC;
        g_ctsu_diag_info.scanbuf.ref = R_CTSU->CTSURC;
    }
   #endif
  #endif
    else
    {
  #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
            (p_instance_ctrl->p_self_raw + p_instance_ctrl->rd_index)->sen = R_CTSU->CTSUSC;
            (p_instance_ctrl->p_self_raw + p_instance_ctrl->rd_index)->ref = R_CTSU->CTSURC;
            p_instance_ctrl->rd_index++;
        }
  #endif
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
        {
            if (false == p_instance_ctrl->serial_tuning_enable)
            {
                if (1 == R_CTSU->CTSUST_b.CTSUPS)
                {
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->pri_sen = R_CTSU->CTSUSC;
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->pri_ref = R_CTSU->CTSURC;
                }
                else
                {
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->snd_sen = R_CTSU->CTSUSC;
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->snd_ref = R_CTSU->CTSURC;
                    p_instance_ctrl->rd_index++;
                }
            }
            else
            {
                p_instance_ctrl->serial_tuning_mutual_cnt++;
                if (p_instance_ctrl->serial_tuning_mutual_cnt % 2)
                {
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->pri_sen = R_CTSU->CTSUSC;
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->pri_ref = R_CTSU->CTSURC;
                }
                else
                {
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->snd_sen = R_CTSU->CTSUSC;
                    (p_instance_ctrl->p_mutual_raw + p_instance_ctrl->rd_index)->snd_ref = R_CTSU->CTSURC;
                    p_instance_ctrl->rd_index++;
                }
            }
        }
  #endif
    }
 #endif

 #if (BSP_FEATURE_CTSU_VERSION == 2)
  #if (CTSU_CFG_NUM_CFC != 0)
    uint16_t i;
  #endif
    if (CTSU_CORRECTION_RUN == g_ctsu_correction_info.status)
    {
        g_ctsu_correction_info.scanbuf = R_CTSU->CTSUSC;
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    else if (CTSU_CORRECTION_RUN == g_ctsu_corrcfc_info.status)
    {
        for (i = 0; i < g_ctsu_corrcfc_info.num_ts; i++)
        {
            g_ctsu_corrcfc_info.scanbuf[i] = R_CTSU->CTSUCFCCNT_b.CFCCNT;
        }
    }
  #endif
  #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    else if ((CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md) &&
             ((CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) ||
              (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state)
   #if (CTSU_CFG_NUM_CFC != 0)
              || (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
   #endif
             ))
    {
   #if (CTSU_CFG_NUM_CFC != 0)
        if (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
        {
            p_instance_ctrl->p_self_raw[p_instance_ctrl->rd_index] = R_CTSU->CTSUCFCCNTL;
            p_instance_ctrl->rd_index++;
        }
   #endif
        if ((CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) ||
            (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state))
        {
            p_instance_ctrl->p_self_raw[p_instance_ctrl->rd_index] = R_CTSU->CTSUSCNT_b.SUCKCNT;
            p_instance_ctrl->rd_index++;
        }
    }
  #endif
    else
    {
  #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == (CTSU_MODE_MUTUAL_FULL_SCAN & p_instance_ctrl->md))
        {
            p_instance_ctrl->p_self_raw[p_instance_ctrl->rd_index] = R_CTSU->CTSUSC;
            p_instance_ctrl->rd_index++;
        }
  #endif
  #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
        {
            p_instance_ctrl->p_mutual_raw[p_instance_ctrl->rd_index] = R_CTSU->CTSUSC;
            p_instance_ctrl->rd_index++;
        }

   #if (CTSU_CFG_NUM_CFC != 0)
        if (CTSU_MODE_MUTUAL_CFC_SCAN == p_instance_ctrl->md)
        {
            for (i = 0; i < p_instance_ctrl->p_ctsu_cfg->num_rx; i++)
            {
                p_instance_ctrl->p_mutual_raw[p_instance_ctrl->rd_index] = R_CTSU->CTSUCFCCNT_b.CFCCNT;
                p_instance_ctrl->rd_index++;
            }
        }
   #endif
  #endif
    }
 #endif
    if (1 == p_instance_ctrl->interrupt_reverse_flag)
    {
        p_instance_ctrl->interrupt_reverse_flag = 0;

        ctsu_end_interrupt(p_instance_ctrl);
    }
#endif

    /* Restore context if RTOS is used */
    FSP_CONTEXT_RESTORE
}

/***********************************************************************************************************************
 * CTSUFN interrupt handler. This service routine occurs when all elements have been scanned (finished).
 * The user's callback function is called if available.
 ***********************************************************************************************************************/
void ctsu_end_isr (void)
{
    /* Save context if RTOS is used */
    FSP_CONTEXT_SAVE

    IRQn_Type              irq             = R_FSP_CurrentIrqGet();
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) R_FSP_IsrContextGet(irq);
    uint16_t               rd_index;
#if (BSP_FEATURE_ICU_HAS_IELSR)

    /** Clear the BSP IRQ Flag     */
    R_BSP_IrqStatusClear(R_FSP_CurrentIrqGet());
#endif

    /* In CTSU1 and CTSU2 self-capacity and current measurement mode and diagnostic mode, */
    /* rd_index has the same value as wr_index.                                           */
    rd_index = p_instance_ctrl->rd_index;

#if (BSP_FEATURE_CTSU_VERSION == 1)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
    {
        if (true == p_instance_ctrl->serial_tuning_enable)
        {
            if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
            {
                rd_index = p_instance_ctrl->serial_tuning_mutual_cnt;
            }
        }
    }
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
    {
        if (true == p_instance_ctrl->serial_tuning_enable)
        {
            if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
            {
                rd_index = p_instance_ctrl->rd_index;
            }
            else
            {
                rd_index = p_instance_ctrl->rd_index / 2;
            }
        }
        else
        {
            /* In the mutual capacity of CTSU2, the value of rd_index is twice the value of wr_index. */
            rd_index = p_instance_ctrl->rd_index / 2;
        }
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    if (CTSU_MODE_MUTUAL_CFC_SCAN == p_instance_ctrl->md)
    {
        if (true == p_instance_ctrl->serial_tuning_enable)
        {
            if (0 == ((p_instance_ctrl->ctsucr1 >> 7) & 0x01))
            {
                rd_index = p_instance_ctrl->rd_index;
            }
            else
            {
                rd_index = (uint16_t) (p_instance_ctrl->rd_index / (p_instance_ctrl->p_ctsu_cfg->num_rx * 2));
            }
        }
        else
        {
            /* In the CFC mutual capacity of CTSU2, rd_index is twice the number of RX terminals in each wr_index. */
            rd_index = (uint16_t) (p_instance_ctrl->rd_index / (p_instance_ctrl->p_ctsu_cfg->num_rx * 2));
        }
    }
  #endif
 #endif
#endif

    /* Countermeasure for the problem that RD interrupt and FN interrupt are reversed. */
    if (rd_index != p_instance_ctrl->wr_index)
    {
        p_instance_ctrl->interrupt_reverse_flag = 1;

        return;
    }

    ctsu_end_interrupt(p_instance_ctrl);

    /* Restore context if RTOS is used */
    FSP_CONTEXT_RESTORE
}

void ctsu_end_interrupt (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    ctsu_callback_args_t args;

    /* Store callback arguments in memory provided by user if available.  This allows callback arguments to be
     * stored in non-secure memory so they can be accessed by a non-secure callback function. */
    ctsu_callback_args_t * p_args = p_instance_ctrl->p_callback_memory;
    if (NULL == p_args)
    {
        /* Store on stack */
        p_args = &args;
    }
    else
    {
        /* Save current arguments on the stack in case this is a nested interrupt. */
        args = *p_args;
    }

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state)
    {
        if (R_CTSU->CTSUERRS_b.CTSUICOMP == 1)
        {
            g_ctsu_diag_info.icomp = 1;
        }
    }
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_MODE_DIAGNOSIS_SCAN == p_instance_ctrl->md)
    {
        if (CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state)
        {
            if (0 == g_ctsu_diag_info.test_count)
            {
                /* TEST1: Check ICOMP0 in normal voltage condition */
                if (1 == R_CTSU->CTSUSR_b.ICOMP0)
                {
                    /* FAIL: Set diagnostic error */
                    g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OVER_VOLTAGE;
                }

                /* PASS: Check is skipped because ICOMP0 value is expected.
                 * Condition: 0 == R_CTSU->CTSUSR_b.ICOMP0 */
            }
            else
            {
                /* TEST2: Check ICOMP0 in over voltage condition */
                if (0 == R_CTSU->CTSUSR_b.ICOMP0)
                {
                    /* FAIL: Set diagnostic error */
                    g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OVER_VOLTAGE;
                }
                else
                {
                    /* PASS: Reset ICOMP0 */
                    R_CTSU->CTSUSR_b.ICOMPRST = 1;
                    if (1 == R_CTSU->CTSUSR_b.ICOMP0)
                    {
                        /* FAIL: Set diagnostic error */
                        g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OVER_VOLTAGE;
                    }

                    /* PASS: Check is skipped because ICOMP0 value is expected.
                     * Condition: 0 == R_CTSU->CTSUSR_b.ICOMP0 */
                }
            }
        }
        else if (CTSU_DIAG_OVER_CURRENT == g_ctsu_diag_info.state)
        {
            if (0 == g_ctsu_diag_info.test_count)
            {
                /* TEST1: Check ICOMP1 in normal current condition */
                if (1 == R_CTSU->CTSUSR_b.ICOMP1)
                {
                    /* FAIL: Set error result */
                    g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OVER_CURRENT;
                }

                /* PASS: Check is skipped because ICOMP1 value is expected.
                 * Condition: 0 == R_CTSU->CTSUSR_b.ICOMP1 */
            }
            else
            {
                /* TEST2: Check ICOMP1 in over current condition */
                if (0 == R_CTSU->CTSUSR_b.ICOMP1)
                {
                    /* FAIL: Set error result */
                    g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OVER_CURRENT;
                }
                else
                {
                    /* PASS: Reset ICOMP1 */
                    R_CTSU->CTSUSR_b.ICOMPRST = 1;
                    if (1 == R_CTSU->CTSUSR_b.ICOMP1)
                    {
                        /* FAIL: Set error result */
                        g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OVER_CURRENT;
                    }

                    /* PASS: Check is skipped because ICOMP1 value is expected.
                     * Condition: 0 == R_CTSU->CTSUSR_b.ICOMP1 */
                }
            }
        }
        else
        {
            /* Do nothing */
        }
    }
 #endif
#endif

    p_args->event = CTSU_EVENT_SCAN_COMPLETE;

    if (R_CTSU->CTSUST & CTSU_SOVF)
    {
        p_args->event  |= CTSU_EVENT_OVERFLOW;
        R_CTSU->CTSUST &= (uint8_t) (~CTSU_SOVF);
    }

#if (BSP_FEATURE_CTSU_VERSION == 2)
    if (R_CTSU->CTSUSR0 & (CTSU_ICOMP0 | CTSU_ICOMP1))
    {
        if (R_CTSU->CTSUSR0 & CTSU_ICOMP0)
        {
            p_args->event |= CTSU_EVENT_ICOMP;
        }

        if (R_CTSU->CTSUSR0 & CTSU_ICOMP1)
        {
            p_args->event |= CTSU_EVENT_ICOMP1;
        }

        R_CTSU->CTSUSR0 |= CTSU_ICOMPRST;
    }
#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
    if (R_CTSU->CTSUERRS_b.CTSUICOMP == 1)
    {
        R_CTSU->CTSUCR1 &= (uint8_t) (~0x01);
        __NOP();
        __NOP();
        R_CTSU->CTSUCR1 |= 0x01;
        p_args->event   |= CTSU_EVENT_ICOMP;
    }
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
    if (CTSU_CORRECTION_RUN != g_ctsu_correction_info.status)
    {
        if (CTSU_MODE_CORRECTION_SCAN == p_instance_ctrl->md)
        {
            R_CTSU->CTSUCRA_b.SDPSEL     = 0;
            R_CTSU->CTSUSUCLK0           = (uint16_t) ((CTSU_CFG_SUMULTI0 << 8) | g_ctsu_correction_info.suadj0);
            R_CTSU->CTSUCRB_b.SSCNT      = 1;
            R_CTSU->CTSUCALIB_b.SUCARRY  = 0;
            R_CTSU->CTSUCALIB_b.CCOCALIB = 0;
            R_CTSU->CTSUCALIB_b.CCOCLK   = 1;
            R_CTSU->CTSUCALIB_b.TSOC     = 0;
            R_CTSU->CTSUCRA_b.SDPSEL     = 1;
        }
    }
 #endif
#endif

#if (CTSU_CFG_MULTIPLE_ELECTRODE_CONNECTION_ENABLE == 1)
    if ((1 == p_instance_ctrl->tsod) && (CTSU_CAP_EXTERNAL == p_instance_ctrl->cap))
    {
        /* When using MEC, MD0 bit is set to single scan mode. */
        R_CTSU->CTSUMCH_b.MCH0 = (uint8_t) (p_instance_ctrl->mec_ts & CTSU_MEC_BIT6_MASK);

        /* Set MCH1 when using both MEC and Active Shield.*/
        if ((0 != p_instance_ctrl->ctsuchtrc0) ||
            (0 != p_instance_ctrl->ctsuchtrc1) ||
            (0 != p_instance_ctrl->ctsuchtrc2) ||
            (0 != p_instance_ctrl->ctsuchtrc3) ||
            (0 != p_instance_ctrl->ctsuchtrc4))
        {
            R_CTSU->CTSUMCH_b.MCH1 = (uint8_t) (p_instance_ctrl->mec_shield_ts & CTSU_MEC_BIT6_MASK);
        }
    }
#endif

    p_instance_ctrl->state        = CTSU_STATE_SCANNED;
    p_instance_ctrl->error_status = args.event;
    p_args->p_context             = p_instance_ctrl->p_context;

    /* If a callback was provided, call it with the argument */
    if (NULL != p_instance_ctrl->p_callback)
    {
#if BSP_TZ_SECURE_BUILD

        /* p_callback can point to a secure function or a non-secure function. */
        if (!cmse_is_nsfptr(p_instance_ctrl->p_callback))
        {
            /* If p_callback is secure, then the project does not need to change security state. */
            p_instance_ctrl->p_callback(p_args);
        }
        else
        {
            /* If p_callback is Non-secure, then the project must change to Non-secure state in order to call the callback. */
            ctsu_prv_ns_callback p_callback = (ctsu_prv_ns_callback) (p_instance_ctrl->p_callback);
            p_callback(p_args);
        }

#else

        /* If the project is not Trustzone Secure, then it will never need to change security state in order to call the callback. */
        p_instance_ctrl->p_callback(p_args);
#endif
    }

    if (NULL != p_instance_ctrl->p_callback_memory)
    {
        /* Restore callback memory in case this is a nested interrupt. */
        *p_instance_ctrl->p_callback_memory = args;
    }

    /* reset indexes */
    p_instance_ctrl->wr_index = 0;
    p_instance_ctrl->rd_index = 0;
#if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    p_instance_ctrl->serial_tuning_mutual_cnt = 0;
#endif
}

/***********************************************************************************************************************
 * ctsu_correction_process
 ***********************************************************************************************************************/
void ctsu_correction_process (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
#if (BSP_FEATURE_CTSU_VERSION == 2)
    uint32_t i;
    uint32_t j;
    uint32_t trimb;
    uint8_t  rtrim;
    uint16_t x1;
    uint16_t x0;
    uint16_t y0;
    uint16_t y1;
    uint16_t dac2vdc;
    uint8_t  trimb_byte;
    uint16_t dac_value;
    uint32_t ref_value;
    uint16_t c0;
    uint16_t c1;

    g_ctsu_correction_info.status = CTSU_CORRECTION_RUN;

    R_CTSU->CTSUCRA_b.MD0  = 1;
    R_CTSU->CTSUCRA_b.MD1  = 0;
    R_CTSU->CTSUCRA_b.MD2  = 0;
    R_CTSU->CTSUMCH_b.MCA0 = 1;
    R_CTSU->CTSUMCH_b.MCA1 = 0;
    R_CTSU->CTSUMCH_b.MCA2 = 0;
    R_CTSU->CTSUMCH_b.MCA3 = 0;
    R_CTSU->CTSUCRA_b.LOAD = 1;

    /* Setting time of measurement */
    g_ctsu_correction_info.ctsuwr.ctsuso = (CTSU_SNUM_RECOMMEND << 10);

    /* Measure the current input to the ICO by passing current through the internal resistance in each range. */
    /* The theoretical value of the current is 12.5uA. */
    R_CTSU->CTSUCHACA        = 1;
    R_CTSU->CTSUCHACB        = 0;
    R_CTSU->CTSUCALIB_b.TSOC = 1;
    for (i = 0; i < CTSU_RANGE_NUM; i++)
    {
        if (CTSU_RANGE_20UA == i)
        {
            R_CTSU->CTSUCRA_b.ATUNE1 = 0;
            R_CTSU->CTSUCRA_b.ATUNE2 = 1;
        }
        else if (CTSU_RANGE_40UA == i)
        {
            R_CTSU->CTSUCRA_b.ATUNE1 = 1;
            R_CTSU->CTSUCRA_b.ATUNE2 = 0;
        }
        else if (CTSU_RANGE_80UA == i)
        {
            R_CTSU->CTSUCRA_b.ATUNE1 = 0;
            R_CTSU->CTSUCRA_b.ATUNE2 = 0;
        }
        else
        {
            R_CTSU->CTSUCRA_b.ATUNE1 = 1;
            R_CTSU->CTSUCRA_b.ATUNE2 = 1;
        }

        R_CTSU->CTSUCRA_b.LOAD = 3;
        ctsu_correction_measurement(p_instance_ctrl, &g_ctsu_correction_info.base_value[i]);
        R_CTSU->CTSUCRA_b.LOAD = 1;
    }

    /* Measure by inputting each constant current from internal DAC to ICO. */
    R_CTSU->CTSUCRB_b.SSCNT      = 0;
    R_CTSU->CTSUCALIB_b.CCOCLK   = 0;
    R_CTSU->CTSUCALIB_b.CCOCALIB = 1;

    /* 2.5uA * (j + 1), SUCARRY and SSCNT = 3 are required for greater than 20uA */
    for (j = 0; j < CTSU_CORRECTION_POINT_NUM; j++)
    {
        R_CTSU->CTSUCRA_b.SDPSEL = 0;
        if (8 > j)
        {
            R_CTSU->CTSUSUCLK0 = (uint16_t) (((j + 1) * CTSU_CORRECTION_SUMULTI) - 1);
        }
        else
        {
            R_CTSU->CTSUCRB_b.SSCNT     = 3;
            R_CTSU->CTSUCALIB_b.SUCARRY = 1;
            R_CTSU->CTSUSUCLK0          = (uint16_t) (((j - 3) * CTSU_CORRECTION_SUMULTI) - 1);
        }

        R_CTSU->CTSUCRA_b.SDPSEL = 1;

        /* store CCODAC path value to tha last array of dac_value */
        ctsu_correction_measurement(p_instance_ctrl, &(g_ctsu_correction_info.dac_value[CTSU_RANGE_NUM - 1][j]));
    }

    /* Calculate the coefficient between step2 and step3 in each range. */
    trimb = R_CTSUTRIM->CTSUTRIMB;
    rtrim = R_CTSUTRIM->CTSUTRIMA_b.RTRIM;

    for (i = 0; i < CTSU_RANGE_NUM; i++)
    {
        /* Get resistance value from TRIMB register. */
        /* Error rate calculation on VCD path as UQ1.9 */
        trimb_byte = (uint8_t) ((trimb >> ((CTSU_RANGE_NUM - 1 - i) * 8)) & CTSU_CORRECTION_TRIMB_MAX);

        switch (trimb_byte & CTSU_CORRECTION_TRIMB_THRESHOLD1)
        {
            case CTSU_CORRECTION_TRIMB_THRESHOLD1:
            {
                if (rtrim > CTSU_CORRECTION_RTRIM_THRESHOLD1)
                {
                    x1 = CTSU_CORRECTION_BIT8; /* 0.05 */
                }
                else
                {
                    x1 = CTSU_CORRECTION_BIT9; /* 1.00 */
                }

                break;
            }

            case 0x00:
            {
                if (rtrim < CTSU_CORRECTION_RTRIM_THRESHOLD2)
                {
                    x1 = (CTSU_CORRECTION_BIT7 | CTSU_CORRECTION_BIT8); // 0.25 and 0.50
                }
                else
                {
                    x1 = (CTSU_CORRECTION_BIT7 | CTSU_CORRECTION_BIT9); // 0.25 and 1.00
                }

                break;
            }

            case CTSU_CORRECTION_TRIMB_SIGN_BIT:
            {
                x1 = CTSU_CORRECTION_BIT9;                          // 1.00
                break;
            }

            default:                                                // 0x40
            {
                x1 = (CTSU_CORRECTION_BIT7 | CTSU_CORRECTION_BIT8); // 0.25 and 0.50
                break;
            }
        }

        g_ctsu_correction_info.error_rate[i] = x1 + (trimb_byte & CTSU_CORRECTION_BIT6_0);

        /* searching for upper index of CCODAC path value containing VDC path value */
        /* Possibility of base_value > g_ctsu_correction_info.dac_value[][CTSU_CORRECTION_POINT_NUM - 1] */

        /* Theoretical value at VDC path measurement */
        x1 = (uint16_t) (((uint32_t) CTSU_CORRECTION_STD_VAL << 9) / g_ctsu_correction_info.error_rate[i]);

        j = x1 / CTSU_CORRECTION_STD_UNIT;              // upper index of theoretical value

        x0 = (uint16_t) (CTSU_CORRECTION_STD_UNIT * j); // lower side Theoretical value
        y0 = g_ctsu_correction_info.dac_value[CTSU_RANGE_NUM - 1][j - 1];
        y1 = g_ctsu_correction_info.dac_value[CTSU_RANGE_NUM - 1][j];

        /* Calculate CCODAC path value on VDC path current by linear interpolation */
        dac2vdc = (uint16_t) (y0 + ((uint32_t) (y1 - y0) * (x1 - x0)) / CTSU_CORRECTION_STD_UNIT);

        /* calculate VDC path conversion ratio */
        dac2vdc =
            (uint16_t) (((int32_t) g_ctsu_correction_info.base_value[i] << CTSU_CORRECTION_DIV_PRECISION) / dac2vdc);

        /* Correct CCODAC path value using conversion ratio, and make correction coefficients */
        for (j = 0; j < CTSU_CORRECTION_POINT_NUM; j++)
        {
            dac_value =
                (uint16_t) (((uint32_t) g_ctsu_correction_info.dac_value[CTSU_RANGE_NUM - 1][j] * dac2vdc) >>
                            CTSU_CORRECTION_DIV_PRECISION);
            ref_value = (CTSU_CORRECTION_STD_UNIT * (j + 1));

            g_ctsu_correction_info.dac_value[i][j] = dac_value;
            g_ctsu_correction_info.coef[i][j]      =
                (uint16_t) ((ref_value << CTSU_CORRECTION_DIV_PRECISION) / dac_value);
        }

        /* Expand correction range */
        c0 = g_ctsu_correction_info.coef[i][0];
        c1 = g_ctsu_correction_info.coef[i][1];
        x0 = g_ctsu_correction_info.dac_value[i][0];
        x1 = g_ctsu_correction_info.dac_value[i][1];

        g_ctsu_correction_info.coef[i][0] =
            (uint16_t) ((int32_t) c1 - (((int32_t) c1 - (int32_t) c0) * x1) / (x1 - x0));
        g_ctsu_correction_info.dac_value[i][0] = 0;

        c0 = g_ctsu_correction_info.coef[i][10];
        c1 = g_ctsu_correction_info.coef[i][11];
        x0 = g_ctsu_correction_info.dac_value[i][10];
        x1 = g_ctsu_correction_info.dac_value[i][11];

        g_ctsu_correction_info.coef[i][11] =
            (uint16_t) (c1 + (((int32_t) c1 - (int32_t) c0) * (CTSU_COUNT_MAX - x1)) / (x1 - x0));
        g_ctsu_correction_info.dac_value[i][11] = CTSU_COUNT_MAX;
    }

 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)
    for (i = 0; i < (CTSU_RANGE_NUM - 1); i++)
    {
        g_ctsu_correction_info.range_ratio[i] =
            (uint16_t) (((uint32_t) g_ctsu_correction_info.error_rate[i] << CTSU_SHIFT_AMOUNT) /
                        g_ctsu_correction_info.error_rate[CTSU_RANGE_160UA]);
    }
 #endif
    g_ctsu_correction_info.status = CTSU_CORRECTION_COMPLETE;
#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
    uint16_t second_std_val;
    uint32_t ctsu_sdpa;
    uint32_t pclkb_mhz;

 #if BSP_FEATURE_CTSU_HAS_TRMR
    uint8_t ctsutrimr_def;
 #endif

    g_ctsu_correction_info.status = CTSU_CORRECTION_RUN;

    pclkb_mhz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / CTSU_PCLKB_FREQ_MHZ;
    if (CTSU_PCLKB_FREQ_RANGE1 >= pclkb_mhz)
    {
        R_CTSU->CTSUCR1 |= (0 << 4);
        ctsu_sdpa        = pclkb_mhz - 1;
    }
    else if ((CTSU_PCLKB_FREQ_RANGE1 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE2 >= pclkb_mhz))
    {
        R_CTSU->CTSUCR1 |= (1 << 4);
        ctsu_sdpa        = (pclkb_mhz / 2) - 1;
    }
    else
    {
        R_CTSU->CTSUCR1 |= (2 << 4);
        ctsu_sdpa        = (pclkb_mhz / 4) - 1;
    }

    g_ctsu_correction_info.ctsu_clock = pclkb_mhz >> CTSU_CFG_PCLK_DIVISION;

    R_CTSU->CTSUCR1    |= (CTSU_MODE_SELF_MULTI_SCAN << 6);
    R_CTSU->CTSUCHAC[0] = 0x01;

    g_ctsu_correction_info.ctsuwr.ctsussc = (CTSU_SSDIV_0500 << 8);
    g_ctsu_correction_info.ctsuwr.ctsuso0 = 0x0000;

    /* Set CTSUSO1 */
    g_ctsu_correction_info.ctsuwr.ctsuso1 = (uint16_t) ((CTSU_ICOG_66 << 13) | (ctsu_sdpa << 8) | CTSU_RICOA_RECOMMEND);

    /* Correction measurement setting */
    R_CTSU->CTSUERRS_b.CTSUSPMD = 2;
    R_CTSU->CTSUERRS_b.CTSUTSOC = 1;
    R_BSP_SoftwareDelay(CTSU_WAIT_TIME, BSP_DELAY_UNITS_MICROSECONDS);

    /* First value measurement */
    ctsu_correction_measurement(p_instance_ctrl, &g_ctsu_correction_info.first_val);

    /* Second standard value create */
 #if BSP_FEATURE_CTSU_HAS_TRMR
    uint32_t work;

    /* Current trimming value storage */
    ctsutrimr_def = CTSU.CTSUTRMR;

    /*                       ctsutrimr_def + 273            ((ctsutrimr_def + 273) * 2560 * 128)      */
    /*    second_std_val  =  ------------------- * 40960 =  ------------------------------------ + 64 */
    /*                               528                                   33 * 128                   */

    work =
        (uint32_t) (((uint32_t) ctsutrimr_def + CTSU_CORRECTION_TRIMMING_OFFSET) * CTSU_CORRECTION_CALCULATION_FACTOR +
                    CTSU_CORRECTION_ROUNDING_OFFSET);
    second_std_val = (uint16_t) (work >> 7UL);

    /* 0xFF set in the current trimming register */
    CTSU.CTSUTRMR = 0xFF;
 #else
    second_std_val = (uint16_t) (CTSU_CORRECTION_2ND_STD_VAL * CTSU_WAFER_PARAMETER);
    g_ctsu_correction_info.ctsuwr.ctsuso1 |= (uint16_t) (CTSU_ICOG_40 << 13); /* ICO gain  66% -> 40% */
 #endif

    /* Second value measurement */
    ctsu_correction_measurement(p_instance_ctrl, &g_ctsu_correction_info.second_val);
 #if BSP_FEATURE_CTSU_HAS_TRMR

    /* Return the current trimming register to the initial value */
    CTSU.CTSUTRMR = ctsutrimr_def;
 #endif

    /* Normal measurement setting */
    R_CTSU->CTSUERRS_b.CTSUTSOC = 0;
    R_CTSU->CTSUERRS_b.CTSUSPMD = 0;

    R_BSP_SoftwareDelay(CTSU_WAIT_TIME, BSP_DELAY_UNITS_MICROSECONDS);

    if ((0 != g_ctsu_correction_info.first_val) && (0 != g_ctsu_correction_info.second_val))
    {
        if (g_ctsu_correction_info.second_val < g_ctsu_correction_info.first_val)
        {
            /* 1st coefficient create */
            g_ctsu_correction_info.first_coefficient = (CTSU_CORRECTION_1ST_STD_VAL << CTSU_SHIFT_AMOUNT) /
                                                       g_ctsu_correction_info.first_val;

            /* 2nd coefficient create */
            g_ctsu_correction_info.second_coefficient = ((uint32_t) second_std_val << CTSU_SHIFT_AMOUNT) /
                                                        g_ctsu_correction_info.second_val;

            g_ctsu_correction_info.status = CTSU_CORRECTION_COMPLETE;
        }
        else
        {
            g_ctsu_correction_info.status = CTSU_CORRECTION_ERROR;
        }
    }
    else
    {
        g_ctsu_correction_info.status = CTSU_CORRECTION_ERROR;
    }
#endif
}

/***********************************************************************************************************************
 * ctsu_correction_measurement
 ***********************************************************************************************************************/
void ctsu_correction_measurement (ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * data)
{
    uint16_t i;
    uint32_t sum = 0;

    for (i = 0; i < CTSU_CORRECTION_AVERAGE; i++)
    {
#if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
        ctsu_transfer_configure(p_instance_ctrl);
#else
        FSP_PARAMETER_NOT_USED(p_instance_ctrl);
#endif
        p_instance_ctrl->state = CTSU_STATE_SCANNING;
        R_CTSU->CTSUCR0       |= 0x01;
        while (p_instance_ctrl->state != CTSU_STATE_SCANNED)
        {
        }

#if (BSP_FEATURE_CTSU_VERSION == 2)
        sum += g_ctsu_correction_info.scanbuf;
#endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
        sum += g_ctsu_correction_info.scanbuf.sen;
#endif
    }

    *data = (uint16_t) (sum / CTSU_CORRECTION_AVERAGE);
}

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_TEMP_CORRECTION_SUPPORT == 1)

/***********************************************************************************************************************
 * ctsu_correction_scan_start
 ***********************************************************************************************************************/
void ctsu_correction_scan_start (void)
{
    g_ctsu_temp_reg_ctsucra = R_CTSU->CTSUCRA;

    R_CTSU->CTSUCRA_b.MD0  = 1;
    R_CTSU->CTSUCRA_b.MD1  = 0;
    R_CTSU->CTSUCRA_b.MD2  = 0;
    R_CTSU->CTSUMCH_b.MCA0 = 1;
    R_CTSU->CTSUMCH_b.MCA1 = 0;
    R_CTSU->CTSUMCH_b.MCA2 = 0;
    R_CTSU->CTSUMCH_b.MCA3 = 0;

    /* Setting time of measurement */
    g_ctsu_correction_info.ctsuwr.ctsuso = (CTSU_SNUM_RECOMMEND << 10);
    g_ctsu_correction_info.suadj0        = (uint8_t) (R_CTSU->CTSUSUCLK0 & CTSU_SUADJ_MAX);

    if (g_ctsu_correction_info.scan_index < CTSU_CORRECTION_POINT_NUM)
    {
        /* Dummy setting */
        R_CTSU->CTSUCHACA  = 1;
        R_CTSU->CTSUCHACB  = 0;
        R_CTSU->CTSUCHTRCA = 0;
        R_CTSU->CTSUCHTRCB = 0;

        /* Step3 : Measure by inputting each constant current from internal DAC to ICO. */
        R_CTSU->CTSUCRB_b.SSCNT      = 0;
        R_CTSU->CTSUCALIB_b.CCOCLK   = 0;
        R_CTSU->CTSUCALIB_b.CCOCALIB = 1;
        R_CTSU->CTSUCALIB_b.TSOC     = 1;

        R_CTSU->CTSUCRA_b.SDPSEL = 0;
        if (8 > g_ctsu_correction_info.scan_index)
        {
            R_CTSU->CTSUSUCLK0 = (uint16_t) (((g_ctsu_correction_info.scan_index + 1) * CTSU_CORRECTION_SUMULTI) - 1);
        }
        else
        {
            /* SUCARRY is required for greater than 10uA */
            R_CTSU->CTSUCRB_b.SSCNT     = 3;
            R_CTSU->CTSUCALIB_b.SUCARRY = 1;
            R_CTSU->CTSUSUCLK0          =
                (uint16_t) (((g_ctsu_correction_info.scan_index - 3) * CTSU_CORRECTION_SUMULTI) - 1);
        }

        R_CTSU->CTSUCRA_b.SDPSEL = 1;
    }
    else
    {
        /* Step2-b : Measure the current input to the ICO by passing current through the external resistance. */
        /*           The theoretical value of the current is 9.375uA. */
        R_CTSU->CTSUCRA_b.ATUNE1 = 1;
        R_CTSU->CTSUCRA_b.ATUNE2 = 1;
  #if (CTSU_CFG_TEMP_CORRECTION_TS < 32)
        R_CTSU->CTSUCHACA = (uint32_t) (1 << CTSU_CFG_TEMP_CORRECTION_TS);
        R_CTSU->CTSUCHACB = 0;
  #else
        R_CTSU->CTSUCHACA = 0;
        R_CTSU->CTSUCHACB = (uint32_t) (1 << (CTSU_CFG_TEMP_CORRECTION_TS - 32));
  #endif
        R_CTSU->CTSUCRA_b.DCMODE = 1;
        R_CTSU->CTSUCRA_b.DCBACK = 1;
        R_CTSU->CTSUSO_b.SO      = 0;
    }
}

/***********************************************************************************************************************
 * ctsu_correction_data_get
 ***********************************************************************************************************************/
fsp_err_t ctsu_correction_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * p_data)
{
  #if (CTSU_CFG_CALIB_RTRIM_SUPPORT == 1)
    adc_instance_t const * p_adc = p_instance_ctrl->p_ctsu_cfg->p_adc_instance;
  #endif
    uint32_t    i;
    uint32_t    j;
    uint16_t    x0;
    uint16_t    x1;
    uint16_t    y0;
    uint16_t    y1;
    uint16_t    dac2vdc;
    uint16_t    dac_value;
    uint32_t    ref_value;
    uint16_t    c0;
    uint16_t    c1;
    ctsu_data_t temp_avg_data = {0, 0};
    fsp_err_t   err           = FSP_SUCCESS;

    if (g_ctsu_correction_info.scan_index < CTSU_CORRECTION_POINT_NUM)
    {
        temp_avg_data.int_data = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][g_ctsu_correction_info.scan_index];
        ctsu_moving_average(&temp_avg_data, *p_instance_ctrl->p_self_raw, 4);
        g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][g_ctsu_correction_info.scan_index] = temp_avg_data.int_data;
        g_ctsu_correction_info.scan_index++;
    }
    else
    {
        if (0 == g_ctsu_correction_info.ex_base_value)
        {
            g_ctsu_correction_info.ex_base_value  = *p_instance_ctrl->p_self_raw;
            g_ctsu_correction_info.update_counter = CTSU_CFG_TEMP_CORRECTION_TIME;
        }
        else
        {
            temp_avg_data.int_data = g_ctsu_correction_info.ex_base_value;
            ctsu_moving_average(&temp_avg_data, *p_instance_ctrl->p_self_raw, 4);
            g_ctsu_correction_info.ex_base_value = temp_avg_data.int_data;
        }

        g_ctsu_correction_info.scan_index = 0;
        g_ctsu_correction_info.update_counter++;
    }

    /* Step4 : Calculate the coefficient between step2 and step3. */
    if (g_ctsu_correction_info.update_counter > CTSU_CFG_TEMP_CORRECTION_TIME)
    {
        /* Theoretical value at VDC path measurement */
        x1 =
            (uint16_t) (((uint32_t) CTSU_CORRECTION_STD_VAL << 9) /
                        g_ctsu_correction_info.error_rate[CTSU_RANGE_160UA]);

        j = x1 / CTSU_CORRECTION_STD_UNIT;              // upper index of theoretical value

        x0 = (uint16_t) (CTSU_CORRECTION_STD_UNIT * j); // lower side Theoretical value
        y0 = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][j - 1];
        y1 = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][j];

        /* Calculate CCODAC path value on VDC path current by linear interpolation */
        dac2vdc = (uint16_t) (y0 + ((uint32_t) (y1 - y0) * (x1 - x0)) / CTSU_CORRECTION_STD_UNIT);

        // calculate VDC path conversion ratio
        dac2vdc =
            (uint16_t) (((int32_t) g_ctsu_correction_info.base_value[CTSU_RANGE_160UA] <<
                         CTSU_CORRECTION_DIV_PRECISION) /
                        dac2vdc);

        /* Correct CCODAC path value using conversion ratio, and make correction coefficients */
        for (j = 0; j < CTSU_CORRECTION_POINT_NUM; j++)
        {
            dac_value =
                (uint16_t) (((uint32_t) g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][j] * dac2vdc) >>
                            CTSU_CORRECTION_DIV_PRECISION);
            ref_value = (CTSU_CORRECTION_STD_UNIT * (j + 1));

            g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][j] = dac_value;
            g_ctsu_correction_info.coef[CTSU_RANGE_160UA][j]      =
                (uint16_t) ((ref_value << CTSU_CORRECTION_DIV_PRECISION) / dac_value);
        }

        /* expand correction range */
        c0 = g_ctsu_correction_info.coef[CTSU_RANGE_160UA][0];
        c1 = g_ctsu_correction_info.coef[CTSU_RANGE_160UA][1];
        x0 = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][0];
        x1 = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][1];

        g_ctsu_correction_info.coef[CTSU_RANGE_160UA][0] =
            (uint16_t) ((int32_t) c1 - (((int32_t) c1 - (int32_t) c0) * x1) / (x1 - x0));
        g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][0] = 0;

        c0 = g_ctsu_correction_info.coef[CTSU_RANGE_160UA][10];
        c1 = g_ctsu_correction_info.coef[CTSU_RANGE_160UA][11];
        x0 = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][10];
        x1 = g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][11];

        g_ctsu_correction_info.coef[CTSU_RANGE_160UA][11] =
            (uint16_t) (c1 + (((int32_t) c1 - (int32_t) c0) * (CTSU_COUNT_MAX - x1)) / (x1 - x0));
        g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][11] = CTSU_COUNT_MAX;

        for (j = 1; j < (CTSU_CORRECTION_POINT_NUM - 1); j++)
        {
            for (i = 0; i < (CTSU_RANGE_NUM - 1); i++)
            {
                g_ctsu_correction_info.dac_value[i][j] =
                    (uint16_t) (((uint32_t) g_ctsu_correction_info.dac_value[CTSU_RANGE_160UA][j] *
                                 (uint32_t) g_ctsu_correction_info.range_ratio[i]) >> CTSU_SHIFT_AMOUNT);
                g_ctsu_correction_info.coef[i][j] =
                    (uint16_t) (((uint32_t) g_ctsu_correction_info.coef[CTSU_RANGE_160UA][j] *
                                 (uint32_t) g_ctsu_correction_info.range_ratio[i]) >> CTSU_SHIFT_AMOUNT);
            }
        }

  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
        ctsu_auto_correction_register_set(p_instance_ctrl);
  #endif

        g_ctsu_correction_info.update_counter = 0;
  #if (CTSU_CFG_CALIB_RTRIM_SUPPORT == 1)
        err = ctsu_correction_calib_rtrim(p_instance_ctrl, p_data);
        if (FSP_ERR_ALREADY_OPEN != err)
        {
            p_adc->p_api->close(p_adc->p_ctrl);
        }

        if (FSP_SUCCESS != err)
        {
            err = FSP_ERR_ABORTED;
        }
  #endif
    }
    else
    {
        /* Indicates that ADC measurement was not performed. */
        *p_data = CTSU_COUNT_MAX;
    }

    R_CTSU->CTSUCRA = g_ctsu_temp_reg_ctsucra;

    return err;
}

  #if (CTSU_CFG_CALIB_RTRIM_SUPPORT == 1)

/***********************************************************************************************************************
 * ctsu_correction_calib_rtrim
 ***********************************************************************************************************************/
fsp_err_t ctsu_correction_calib_rtrim (ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * p_data)
{
    adc_status_t           status;
    adc_instance_t const * p_adc = p_instance_ctrl->p_ctsu_cfg->p_adc_instance;
    uint16_t               i;
    uint16_t               adctdr_result;
    uint16_t               adctdr_ave;
    uint32_t               adctdr_sum;
    int16_t                diff;
    int16_t                dir  = 0;
    uint16_t               comp = 0;
    fsp_err_t              err;

    /* Initialize ADC for CTSU TSCAP */
    err = p_adc->p_api->open(p_adc->p_ctrl, p_adc->p_cfg);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

    err = p_adc->p_api->scanCfg(p_adc->p_ctrl, p_adc->p_channel_cfg);
    FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

   #if (0 == BSP_FEATURE_ADC_D_IS_AVAILABLE)
    R_ADC0->ADSSTRL = CTSU_CALIB_ADSSTRL;
   #endif

    /* Self single scan mode */
    R_CTSU->CTSUCRA_b.LOAD = 1;
    R_CTSU->CTSUCRA_b.MD0  = 0;
    R_CTSU->CTSUCRA_b.MD1  = 0;
    R_CTSU->CTSUCRA_b.MD2  = 0;

    /* Set Channel */
   #if (CTSU_CFG_TEMP_CORRECTION_TS < 32)
    R_CTSU->CTSUCHACA  |= (uint32_t) (1 << CTSU_CFG_TEMP_CORRECTION_TS);
    R_CTSU->CTSUCHTRCA &= (uint32_t) ~(1 << CTSU_CFG_TEMP_CORRECTION_TS);
   #else
    R_CTSU->CTSUCHACB  |= (uint32_t) (1 << (CTSU_CFG_TEMP_CORRECTION_TS - 32));
    R_CTSU->CTSUCHTRCB &= (uint32_t) ~(1 << (CTSU_CFG_TEMP_CORRECTION_TS - 32));
   #endif
    R_CTSU->CTSUMCH0 = CTSU_CFG_TEMP_CORRECTION_TS;

    /* 150uA current measurement */
    R_CTSU->CTSUCRA_b.DCMODE = 1;
    R_CTSU->CTSUCRA_b.DCBACK = 1;
    R_CTSU->CTSUSO_b.SO      = CTSU_CALIB_CTSUSO;
    R_CTSU->CTSUCRA_b.CSW    = 0;
    R_CTSU->CTSUCALIB_b.DRV  = 1;

    /* ADC scan */
    while (!comp)
    {
        adctdr_sum = 0;
        for (i = 0; i < CTSU_CALIB_AVERAGE_TIME; i++)
        {
            /* Software trigger start scan */
            err = p_adc->p_api->scanStart(p_adc->p_ctrl);
            FSP_ERROR_RETURN(FSP_SUCCESS == err, err);

            /* Wait for conversion to complete. */
            status.state = ADC_STATE_SCAN_IN_PROGRESS;
            while (ADC_STATE_SCAN_IN_PROGRESS == status.state)
            {
                err = p_adc->p_api->scanStatusGet(p_adc->p_ctrl, &status);
                FSP_ERROR_RETURN(FSP_SUCCESS == err, err);
            }

            /* Read A/D data then scan normal end */
   #if (1 == BSP_FEATURE_ADC_D_IS_AVAILABLE)
            err = p_adc->p_api->read(p_adc->p_ctrl, ADC_CHANNEL_TSCAP_VOLT, &adctdr_result);
   #else
            err = p_adc->p_api->read(p_adc->p_ctrl, ADC_CHANNEL_16, &adctdr_result);
   #endif
            FSP_ERROR_RETURN(FSP_SUCCESS == err, err);
            adctdr_sum += adctdr_result;
        }

        adctdr_ave = (uint16_t) ((adctdr_sum * 10) / CTSU_CALIB_AVERAGE_TIME);
        diff       = (int16_t) ((adctdr_ave - CTSU_CALIB_REF) / 10);

        /* The change unit of the voltage by the RTRIM register is about 4mV (4096 * 4) = 16.384 */
        if (diff > CTSU_CALIB_THRESHOLD)
        {
            if (0 <= dir)
            {
                R_CTSUTRIM->CTSUTRIMA_b.RTRIM = (uint8_t) (R_CTSUTRIM->CTSUTRIMA_b.RTRIM + 1);
                dir = 1;
            }
            else
            {
                comp = 1;
            }
        }
        else if (diff < -(CTSU_CALIB_THRESHOLD))
        {
            if (0 >= dir)
            {
                R_CTSUTRIM->CTSUTRIMA_b.RTRIM = (uint8_t) (R_CTSUTRIM->CTSUTRIMA_b.RTRIM - 1);
                dir = -1;
            }
            else
            {
                comp = 1;
            }
        }
        else
        {
            comp = 1;
        }
    }

    /* Restore register settings */
    R_CTSU->CTSUCALIB_b.DRV  = 0;
    R_CTSU->CTSUCRA_b.CSW    = 1;
    R_CTSU->CTSUCRA_b.DCMODE = 0;
    R_CTSU->CTSUCRA_b.DCBACK = 0;

    /* Indicates that ADC measurement was performed  */
    *p_data = R_CTSUTRIM->CTSUTRIMA_b.RTRIM;

    /* Close ADC for CTSU TSCAP */
    p_adc->p_api->close(p_adc->p_ctrl);

    return err;
}

  #endif
 #endif
#endif

#if ((CTSU_CFG_AUTO_CORRECTION_ENABLE == 0) || (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1))

/***********************************************************************************************************************
 * ctsu_correction_calc
 ***********************************************************************************************************************/
void ctsu_correction_calc (uint16_t * correction_data, uint16_t raw_data, ctsu_correction_calc_t * p_calc)
{
    uint32_t answer = 0;
    uint32_t cmp_data;
    uint8_t  calc_flag = 0;
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    uint16_t diff_val;
    int32_t  diff_coefficient;
    int32_t  mul_diffcoff_diff1valsval;
    uint32_t mul_coff1val_diffcorr;
    uint16_t coefficient;
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 2)
    int32_t  y0 = 0;
    int32_t  y1 = 0;
    int32_t  x0 = 0;
    int32_t  x1 = 0;
    uint16_t i;
    uint32_t scaled_data;
    uint32_t coef;
    bool     flag;
 #endif

 #if (BSP_FEATURE_CTSU_VERSION == 2)
  #if (CTSU_CFG_NUM_CFC != 0)
    if ((CTSU_CORRECTION_COMPLETE == g_ctsu_correction_info.status) ||
        (CTSU_CORRECTION_COMPLETE == g_ctsu_corrcfc_info.status))
    {
        calc_flag = 1;
    }

  #else
    if (CTSU_CORRECTION_COMPLETE == g_ctsu_correction_info.status)
    {
        calc_flag = 1;
    }
  #endif
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 1)
    if (CTSU_CORRECTION_COMPLETE == g_ctsu_correction_info.status)
    {
        calc_flag = 1;
    }
 #endif

    if (calc_flag)
    {
 #if (BSP_FEATURE_CTSU_VERSION == 1)

        /* Since the correction coefficient table is created with the recommended measurement time, */
        /* If the measurement time is different, adjust the value level. */
        cmp_data = (uint32_t) ((raw_data * g_ctsu_correction_info.ctsu_clock) /
                               (uint32_t) ((p_calc->snum + 1) * (p_calc->sdpa + 1)));

        /*               g_mul_coff1val_diffcorr - g_diff_cofficient * (g_ctsu_correction_info.first_val - raw_data) */
        /*  coefficient= ------------------------------------------------------------------------------------------  */
        /*                                      g_diff_correct_val                                                   */
        /*                                                                                                           */

        diff_val = (uint16_t) (g_ctsu_correction_info.first_val - g_ctsu_correction_info.second_val);

        /* Get multiplication of g_ctsu_correction_info.first_coefficient and difference of Correction value */
        mul_coff1val_diffcorr = g_ctsu_correction_info.first_coefficient * diff_val;

        /* Get difference of Correction coefficient */
        diff_coefficient =
            (int32_t) (g_ctsu_correction_info.first_coefficient - g_ctsu_correction_info.second_coefficient);

        /* Get multiplication of  g_diff_cofficient  and (g_ctsu_correction_info.first_val - raw_data_coff) */
        mul_diffcoff_diff1valsval = (diff_coefficient * (int32_t) (g_ctsu_correction_info.first_val - cmp_data));

        /* Get correction coefficient of scan data */
        coefficient = (uint16_t) (((int32_t) mul_coff1val_diffcorr - mul_diffcoff_diff1valsval) / diff_val);

        /* Get output count data */
        answer = (uint32_t) (((uint32_t) raw_data * (uint32_t) coefficient) >> CTSU_SHIFT_AMOUNT);
 #endif
 #if (BSP_FEATURE_CTSU_VERSION == 2)

        // Normalization measurement data to recommended measurement timed
        cmp_data = (uint32_t) raw_data * (CTSU_SNUM_RECOMMEND + 1);

        /* searching for upper index of measurement value containing raw_data by scaled measurement value   */
        /* if without scaling, should use binary search                                                     */
        /* index search must be stopped when i = 11 because dac_value[11] = 65535                           */

        if (CTSU_MODE_MUTUAL_CFC_SCAN != p_calc->md)
        {
            for (i = 1; i < CTSU_CORRECTION_POINT_NUM - 1; i++)
            {
                scaled_data = (uint32_t) (g_ctsu_correction_info.dac_value[p_calc->range][i]) * (p_calc->snum + 1);
                if (scaled_data > cmp_data)
                {
                    break;
                }
            }

            x0 = g_ctsu_correction_info.dac_value[p_calc->range][i - 1];
            x1 = g_ctsu_correction_info.dac_value[p_calc->range][i];

            // coef
            y0 = g_ctsu_correction_info.coef[p_calc->range][i - 1];
            y1 = g_ctsu_correction_info.coef[p_calc->range][i];

            x0 = (x0 * (p_calc->snum + 1)) / (CTSU_SNUM_RECOMMEND + 1);
            x1 = (x1 * (p_calc->snum + 1)) / (CTSU_SNUM_RECOMMEND + 1);
            {
                coef = (uint32_t) (((y1 - y0) * (raw_data - x0)) / (x1 - x0) + y0);
                flag = false;

                if (p_calc->snum < CTSU_SNUM_RECOMMEND) // 1st condition
                {
                    if (raw_data < coef)
                    {
                        answer = (coef >> CTSU_CORRECTION_DIV_PRECISION) * raw_data;
                    }
                    else
                    {
                        answer = coef * (raw_data >> CTSU_CORRECTION_DIV_PRECISION);
                    }

                    if (answer > CTSU_COUNT_MAX) // 2nd condition
                    {
                        flag   = true;
                        answer = CTSU_COUNT_MAX;
                        g_ctsu_correction_info.calculation_error = 1;
                    }
                }

                if (false == flag)
                {
                    answer = (uint32_t) (coef * raw_data) >> CTSU_CORRECTION_DIV_PRECISION;
                }
            }
        }

  #if (CTSU_CFG_NUM_CFC != 0)
        else
        {
            i = 0;
            while (1)
            {
                if ((cmp_data < g_ctsu_corrcfc_info.dac_value[p_calc->cfc][i]) || ((CTSU_CORRCFC_POINT_NUM - 1) == i))
                {
                    y0 = g_ctsu_corrcfc_info.ref_value[p_calc->cfc][i];
                    x0 = g_ctsu_corrcfc_info.dac_value[p_calc->cfc][i];
                    if (0 == i)
                    {
                        x1 = 0;
                        y1 = 0;
                    }
                    else
                    {
                        x1 = g_ctsu_corrcfc_info.dac_value[p_calc->cfc][i - 1];
                        y1 = g_ctsu_corrcfc_info.ref_value[p_calc->cfc][i - 1];
                    }

                    break;
                }

                i++;
            }

            if (CTSU_SNUM_RECOMMEND != p_calc->snum)
            {
                x0 = ((x0 * (p_calc->snum + 1)) / (CTSU_SNUM_RECOMMEND + 1));
                y0 = ((y0 * (p_calc->snum + 1)) / (CTSU_SNUM_RECOMMEND + 1));
                x1 = ((x1 * (p_calc->snum + 1)) / (CTSU_SNUM_RECOMMEND + 1));
                y1 = ((y1 * (p_calc->snum + 1)) / (CTSU_SNUM_RECOMMEND + 1));
            }

            answer = (uint32_t) (y0 - (((y0 - y1) * (x0 - raw_data)) / (x0 - x1)));
        }
  #endif
 #endif

        /* Value Overflow Check */
        if (CTSU_COUNT_MAX < answer)
        {
            *correction_data = CTSU_COUNT_MAX;
            g_ctsu_correction_info.calculation_error = 1;
        }
        else
        {
            *correction_data = (uint16_t) answer;
        }
    }
    else
    {
        *correction_data = raw_data;
    }
}

#endif

/***********************************************************************************************************************
 * ctsu_correction_exec
 ***********************************************************************************************************************/
void ctsu_correction_exec (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
#if (BSP_FEATURE_CTSU_VERSION == 1)
    ctsu_correction_ctsu1_exec(p_instance_ctrl);
#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
    ctsu_correction_ctsu2_exec(p_instance_ctrl);
#endif
}

#if (BSP_FEATURE_CTSU_VERSION == 1)
void ctsu_correction_ctsu1_exec (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t element_id;

    ctsu_correction_calc_t calc;
 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    ctsu_data_t * p_self_data;
    ctsu_data_t   average_self = {0, 0};
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    ctsu_data_t * p_pri_data;
    ctsu_data_t * p_snd_data;
    ctsu_data_t   average_pri = {0, 0};
    ctsu_data_t   average_snd = {0, 0};
 #endif

    for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
    {
        calc.snum = (p_instance_ctrl->p_ctsuwr[element_id].ctsuso0 >> 10) & CTSU_SNUM_MAX;
        calc.sdpa = (p_instance_ctrl->p_ctsuwr[element_id].ctsuso1 >> 8) & CTSU_SDPA_MAX;
 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
            p_self_data  = (p_instance_ctrl->p_self_data + element_id);
            average_self = *p_self_data;
            ctsu_correction_calc((p_instance_ctrl->p_self_corr + element_id),
                                 (p_instance_ctrl->p_self_raw + element_id)->sen,
                                 &calc);
            p_self_data->int_data = *(p_instance_ctrl->p_self_corr + element_id);
            if (1 < p_instance_ctrl->average)
            {
                ctsu_moving_average(&average_self, p_self_data->int_data, p_instance_ctrl->average);
                *p_self_data = average_self;
            }
        }
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
        p_pri_data = (p_instance_ctrl->p_mutual_pri_data + element_id);
        p_snd_data = (p_instance_ctrl->p_mutual_snd_data + element_id);
        if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
        {
            average_pri = *p_pri_data;
            average_snd = *p_snd_data;
            ctsu_correction_calc((p_instance_ctrl->p_mutual_pri_corr + element_id),
                                 (p_instance_ctrl->p_mutual_raw + element_id)->pri_sen, &calc);
            ctsu_correction_calc((p_instance_ctrl->p_mutual_snd_corr + element_id),
                                 (p_instance_ctrl->p_mutual_raw + element_id)->snd_sen, &calc);
            p_pri_data->int_data = *(p_instance_ctrl->p_mutual_pri_corr + element_id);
            p_snd_data->int_data = *(p_instance_ctrl->p_mutual_snd_corr + element_id);

            if (1 < p_instance_ctrl->average)
            {
                ctsu_moving_average(&average_pri, p_pri_data->int_data, p_instance_ctrl->average);
                ctsu_moving_average(&average_snd, p_snd_data->int_data, p_instance_ctrl->average);
                *p_pri_data = average_pri;
                *p_snd_data = average_snd;
            }
        }
 #endif
    }
}

#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
void ctsu_correction_ctsu2_exec (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t i;
    uint16_t element_id;
    uint16_t majority_mode_element_num;
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)
    ctsu_correction_calc_t calc;
    uint32_t               ctsuso;
    uint32_t               snum;
    uint32_t               so_value[CTSU_CFG_NUM_SUMULTI];
 #else
    uint16_t j;
 #endif

 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
    uint16_t    * p_self_corr;
    uint16_t    * p_self_mfc;
    ctsu_data_t * p_self_data;
    ctsu_data_t   average_self;
 #endif
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
    uint16_t    * p_pri_corr;
    uint16_t    * p_snd_corr;
    uint16_t    * p_pri_mfc;
    uint16_t    * p_snd_mfc;
    ctsu_data_t * p_pri_data;
    ctsu_data_t * p_snd_data;
    ctsu_data_t   average_pri;
    ctsu_data_t   average_snd;

  #if (CTSU_CFG_NUM_CFC != 0)
    uint8_t  ts_id;
    uint8_t  table_id;
    uint8_t  cfc_ts_table[CTSU_CFG_NUM_CFC];
    uint16_t offset = 0;
    uint16_t cfc_id = 0;
    uint16_t num_rx = 0;

    /* By inspection, calc.cfc is only used when calc.md == CTSU_MODE_MUTUAL_CFC_SCAN. It is initialized in this case.
     * However, GCC expects calc.cfc to be initialized before calling ctsu_correction_calc regardless of calc.md to
     * avoid the warning -Werror=maybe-uninitialized. */
    calc.cfc = 0;

    /* Create CFC-Rx table in ascending order at this instance */
    for (ts_id = 0; ts_id < CTSU_CORRCFC_TS_MAX; ts_id++)
    {
        if (1 == ((p_instance_ctrl->cfc_rx_bitmap >> ts_id) & 1))
        {
            for (table_id = 0; table_id < CTSU_CFG_NUM_CFC; table_id++)
            {
                if (g_ctsu_corrcfc_info.ts_table[table_id] == ts_id)
                {
                    cfc_ts_table[cfc_id] = table_id;
                    cfc_id++;
                }
            }
        }
    }
  #endif
 #endif
 #if (CTSU_CFG_MAJORITY_MODE & CTSU_JUDGEMENT_MAJORITY_MODE)
    if (1 == p_instance_ctrl->p_ctsu_cfg->majority_mode)
    {
        majority_mode_element_num = CTSU_MAJORITY_MODE_ELEMENTS;
    }
    else
 #endif
    {
        majority_mode_element_num = 1;
    }

 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)
    calc.range = p_instance_ctrl->range;
    calc.md    = p_instance_ctrl->md;
 #endif

    for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
    {
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)
        calc.snum = (p_instance_ctrl->p_ctsuwr[(element_id * CTSU_CFG_NUM_SUMULTI)].ctsuso >> 10) & CTSU_SNUM_MAX;
        for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
        {
            ctsuso =
                (p_instance_ctrl->p_ctsuwr[(element_id * CTSU_CFG_NUM_SUMULTI) + i].ctsuso & CTSU_TUNING_MAX);
            snum = (p_instance_ctrl->p_ctsuwr[(element_id * CTSU_CFG_NUM_SUMULTI)].ctsuso >> 10) &
                   CTSU_SNUM_MAX;
            so_value[i] = (ctsuso * CTSU_CORRECTION_OFFSET_UNIT * (snum + 1)) >> calc.range;
        }
 #endif

        if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->md)
        {
 #if (CTSU_CFG_NUM_SELF_ELEMENTS != 0)
            p_self_corr = p_instance_ctrl->p_self_corr + (element_id * CTSU_CFG_NUM_SUMULTI);
            for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
            {
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 0)
                ctsu_correction_calc(&p_self_corr[i],
                                     p_instance_ctrl->p_self_raw[(element_id * CTSU_CFG_NUM_SUMULTI) + i], &calc);
  #else
                *(p_self_corr + i) =
                    p_instance_ctrl->p_self_raw[(element_id * CTSU_CFG_NUM_SUMULTI) + i];
  #endif
            }

            for (i = 0; i < majority_mode_element_num; i++)
            {
                if (0 != p_instance_ctrl->average)
                {
                    p_self_data = (p_instance_ctrl->p_self_data + (element_id * majority_mode_element_num) + i);

                    /* Store last moving averaged data */
                    average_self.int_data           = p_self_data->int_data;
                    average_self.decimal_point_data = p_self_data->decimal_point_data;
  #if (CTSU_CFG_MAJORITY_MODE & CTSU_JUDGEMENT_MAJORITY_MODE)
                    if (p_instance_ctrl->p_ctsu_cfg->majority_mode == 1)
                    {
                        /* Skip the ctsu_correction_multi at Software JMM */
                        p_self_data->int_data = p_self_corr[i];
                    }
                    else
  #endif
                    {
                        p_self_mfc = (p_instance_ctrl->p_self_mfc + (element_id * CTSU_CFG_NUM_SUMULTI));

                        /* Matching values */
  #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)
                        ctsu_correction_freq(p_self_corr, so_value, p_self_mfc);
  #else
                        for (j = 0; j < CTSU_CFG_NUM_SUMULTI; j++)
                        {
                            p_self_mfc[j] = p_self_corr[j];
                        }
  #endif
                        *(p_instance_ctrl->p_selected_freq_self + element_id) =
                            ctsu_correction_multi(p_self_mfc, NULL, &(p_self_data->int_data), NULL);
                    }

                    /* Update moving averaged data */
                    ctsu_moving_average(&average_self, p_self_data->int_data, p_instance_ctrl->average);
                    *p_self_data = average_self;
                }
            }
        }
        else if (CTSU_MODE_CURRENT_SCAN == p_instance_ctrl->md)
        {
            p_self_data = (p_instance_ctrl->p_self_data + (element_id * CTSU_CFG_NUM_SUMULTI));

            /* Store last moving averaged data */
            average_self = *p_self_data;

            /* Correction */
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 0)
            ctsu_correction_calc(&(p_self_data->int_data), p_instance_ctrl->p_self_raw[element_id], &calc);
  #else
            p_self_data->int_data = p_instance_ctrl->p_self_raw[element_id];
  #endif

            /* Update moving averaged data */
            if (1 < p_instance_ctrl->average)
            {
                ctsu_moving_average(&average_self, p_self_data->int_data, p_instance_ctrl->average);
                *p_self_data = average_self;
            }
 #endif
        }
        else if (CTSU_MODE_MUTUAL_FULL_SCAN == p_instance_ctrl->md)
        {
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
            p_pri_corr = p_instance_ctrl->p_mutual_pri_corr + (element_id * CTSU_CFG_NUM_SUMULTI);
            p_snd_corr = p_instance_ctrl->p_mutual_snd_corr + (element_id * CTSU_CFG_NUM_SUMULTI);
            for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
            {
                if (true == p_instance_ctrl->serial_tuning_enable)
                {
                    if ((0 == p_instance_ctrl->ctsuchtrc0) && (0 == p_instance_ctrl->ctsuchtrc1) &&
                        (0 == p_instance_ctrl->ctsuchtrc2) && (0 == p_instance_ctrl->ctsuchtrc3) &&
                        (0 == p_instance_ctrl->ctsuchtrc4))
                    {
                        /* Serial tuning Phase1 */
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 0)
                        ctsu_correction_calc(&p_instance_ctrl->p_mutual_pri_corr[element_id],
                                             p_instance_ctrl->p_mutual_raw[element_id * CTSU_CFG_NUM_SUMULTI],
                                             &calc);
  #else
                        p_instance_ctrl->p_mutual_pri_corr[element_id] =
                            p_instance_ctrl->p_mutual_raw[element_id * CTSU_CFG_NUM_SUMULTI];
  #endif
                        break;
                    }
                }

  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 0)
                ctsu_correction_calc(&p_pri_corr[i],
                                     p_instance_ctrl->p_mutual_raw[(element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2)],
                                     &calc);
  #else
                p_pri_corr[i] =
                    p_instance_ctrl->p_mutual_raw[(element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2)];
  #endif

  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 0)
                ctsu_correction_calc(&p_snd_corr[i],
                                     p_instance_ctrl->p_mutual_raw[(element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2) + 1],
                                     &calc);
  #else
                p_snd_corr[i] =
                    p_instance_ctrl->p_mutual_raw[(element_id * CTSU_MUTUAL_BUF_SIZE) + (i * 2) + 1];
  #endif
            }

            for (i = 0; i < majority_mode_element_num; i++)
            {
                if (true == p_instance_ctrl->serial_tuning_enable)
                {
                    /* Serial Tuning Phase1 */
                    if ((0 == p_instance_ctrl->ctsuchtrc0) && (0 == p_instance_ctrl->ctsuchtrc1) &&
                        (0 == p_instance_ctrl->ctsuchtrc2) && (0 == p_instance_ctrl->ctsuchtrc3) &&
                        (0 == p_instance_ctrl->ctsuchtrc4))
                    {
                        break;
                    }
                }

                if (0 != p_instance_ctrl->average)
                {
                    p_pri_data = (p_instance_ctrl->p_mutual_pri_data + (element_id * majority_mode_element_num) + i);
                    p_snd_data = (p_instance_ctrl->p_mutual_snd_data + (element_id * majority_mode_element_num) + i);

                    /* Store last moving averaged data */
                    average_pri = *p_pri_data;
                    average_snd = *p_snd_data;
  #if (CTSU_CFG_MAJORITY_MODE & CTSU_JUDGEMENT_MAJORITY_MODE)
                    if (p_instance_ctrl->p_ctsu_cfg->majority_mode == 1)
                    {
                        /* Skip the ctsu_correction_multi at Software JMM */
                        p_pri_data->int_data = p_pri_corr[i];
                        p_snd_data->int_data = p_snd_corr[i];
                    }
                    else
  #endif
                    {
                        p_pri_mfc = (p_instance_ctrl->p_mutual_pri_mfc + (element_id * CTSU_CFG_NUM_SUMULTI));
                        p_snd_mfc = (p_instance_ctrl->p_mutual_snd_mfc + (element_id * CTSU_CFG_NUM_SUMULTI));

                        /* Matching values */
  #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)
                        ctsu_correction_freq(p_pri_corr, so_value, p_pri_mfc);
                        ctsu_correction_freq(p_snd_corr, so_value, p_snd_mfc);
  #else
                        for (j = 0; j < CTSU_CFG_NUM_SUMULTI; j++)
                        {
                            p_pri_mfc[j] = p_pri_corr[j];
                            p_snd_mfc[j] = p_snd_corr[j];
                        }
  #endif
                        *(p_instance_ctrl->p_selected_freq_mutual + element_id) =
                            ctsu_correction_multi(p_pri_mfc, p_snd_mfc, &(p_pri_data->int_data),
                                                  &(p_snd_data->int_data));
                    }

                    /* Update moving averaged data */
                    ctsu_moving_average(&average_pri, p_pri_data->int_data, p_instance_ctrl->average);
                    *p_pri_data = average_pri;
                    ctsu_moving_average(&average_snd, p_snd_data->int_data, p_instance_ctrl->average);
                    *p_snd_data = average_snd;
                }
            }
 #endif
        }
        else if (CTSU_MODE_MUTUAL_CFC_SCAN == p_instance_ctrl->md)
        {
 #if (CTSU_CFG_NUM_MUTUAL_ELEMENTS != 0)
  #if (CTSU_CFG_NUM_CFC != 0)
            if (CTSU_MODE_MUTUAL_CFC_SCAN == p_instance_ctrl->md)
            {
                num_rx = p_instance_ctrl->p_ctsu_cfg->num_rx;
                cfc_id = (uint16_t) (element_id % num_rx);
                if (0 == cfc_id)
                {
                    if (0 == element_id)
                    {
                        offset = 0;
                    }
                    else
                    {
                        offset = (uint16_t) (offset + (num_rx * CTSU_MUTUAL_BUF_SIZE));
                    }
                }
            }

            calc.cfc = cfc_ts_table[cfc_id];

            p_pri_corr = p_instance_ctrl->p_mutual_pri_corr + (element_id * CTSU_CFG_NUM_SUMULTI);
            p_snd_corr = p_instance_ctrl->p_mutual_snd_corr + (element_id * CTSU_CFG_NUM_SUMULTI);

            for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
            {
                ctsu_correction_calc(&p_pri_corr[i],
                                     p_instance_ctrl->p_mutual_raw[offset + cfc_id + (num_rx * i * 2)],
                                     &calc);

                ctsu_correction_calc(&p_snd_corr[i],
                                     p_instance_ctrl->p_mutual_raw[offset + cfc_id + (num_rx * i * 2) + num_rx], &calc);
            }

            if (0 != p_instance_ctrl->average)
            {
                p_pri_data = (p_instance_ctrl->p_mutual_pri_data + element_id);
                p_snd_data = (p_instance_ctrl->p_mutual_snd_data + element_id);

                /* Store last moving averaged data */
                average_pri = *p_pri_data;
                average_snd = *p_snd_data;

                p_pri_mfc = (p_instance_ctrl->p_mutual_pri_mfc + (element_id * CTSU_CFG_NUM_SUMULTI));
                p_snd_mfc = (p_instance_ctrl->p_mutual_snd_mfc + (element_id * CTSU_CFG_NUM_SUMULTI));

                /* Matching values */
                ctsu_correction_freq(p_pri_corr, so_value, p_pri_mfc);
                ctsu_correction_freq(p_snd_corr, so_value, p_snd_mfc);
                *(p_instance_ctrl->p_selected_freq_mutual + element_id) =
                    ctsu_correction_multi(p_pri_mfc, p_snd_mfc, &(p_pri_data->int_data), &(p_snd_data->int_data));

                /* Update moving averaged data */
                ctsu_moving_average(&average_pri, p_pri_data->int_data, p_instance_ctrl->average);
                *p_pri_data = average_pri;
                ctsu_moving_average(&average_snd, p_snd_data->int_data, p_instance_ctrl->average);
                *p_snd_data = average_snd;
            }
  #endif
 #endif
        }
        else
        {
        }
    }
}

#endif
#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 0)

/***********************************************************************************************************************
 * ctsu_correction_freq
 ***********************************************************************************************************************/
void ctsu_correction_freq (uint16_t * p_corr, uint32_t * p_so_value, uint16_t * p_mfc)
{
    uint32_t i;
    uint32_t sumulti[CTSU_CFG_NUM_SUMULTI];
    int32_t  calc[CTSU_CFG_NUM_SUMULTI];
    uint32_t total;

    sumulti[0] = CTSU_CFG_SUMULTI0 + 1;
  #if CTSU_CFG_NUM_SUMULTI >= 2
    sumulti[1] = CTSU_CFG_SUMULTI1 + 1;
  #endif
  #if CTSU_CFG_NUM_SUMULTI >= 3
    sumulti[2] = CTSU_CFG_SUMULTI2 + 1;
  #endif

    p_mfc[0] = p_corr[0];

    for (i = 1; i < CTSU_CFG_NUM_SUMULTI; i++)
    {
        total = (((uint32_t) p_corr[i] * (CTSU_SNUM_RECOMMEND + 1) + p_so_value[i]) * sumulti[0]) / sumulti[i];

        if (total > p_so_value[0])
        {
            calc[i] = (int32_t) ((total - p_so_value[0]) / (CTSU_SNUM_RECOMMEND + 1));
            if (calc[i] < CTSU_COUNT_MAX)
            {
                p_mfc[i] = (uint16_t) calc[i];
            }
            else
            {
                /* over flow */
                p_mfc[i] = CTSU_COUNT_MAX;
            }
        }
        else
        {
            /* under flow */
            calc[i]  = 0;
            p_mfc[i] = 0;
        }
    }
}

 #endif

/***********************************************************************************************************************
 * ctsu_correction_multi
 ***********************************************************************************************************************/
uint8_t ctsu_correction_multi (uint16_t * p_pri_mfc, uint16_t * p_snd_mfc, uint16_t * p_pri_data, uint16_t * p_snd_data)
{
    uint8_t selected_freq;
    int32_t add_pri;
    int32_t add_snd;
 #if CTSU_CFG_NUM_SUMULTI >= 3
    uint32_t i;
    int32_t  pri_calc[CTSU_CFG_NUM_SUMULTI];
    int32_t  snd_calc[CTSU_CFG_NUM_SUMULTI];
    int32_t  diff[CTSU_CFG_NUM_SUMULTI];
 #endif

 #if CTSU_CFG_NUM_SUMULTI == 1
    add_pri = p_pri_mfc[0];
    if (NULL != p_snd_mfc)
    {
        add_snd = p_snd_mfc[0];
    }
 #endif
 #if CTSU_CFG_NUM_SUMULTI == 2
    add_pri = p_pri_mfc[0] + p_pri_mfc[1];
    if (NULL != p_snd_mfc)
    {
        add_snd = p_snd_mfc[0] + p_snd_mfc[1];
    }
 #endif
 #if CTSU_CFG_NUM_SUMULTI >= 3
    for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
    {
        pri_calc[i] = (int32_t) p_pri_mfc[i];
        if (NULL == p_snd_mfc)
        {
            snd_calc[i] = 0;
        }
        else
        {
            snd_calc[i] = (int32_t) p_snd_mfc[i];
        }
    }

    diff[0] = (snd_calc[1] - pri_calc[1]) - (snd_calc[0] - pri_calc[0]);
    diff[1] = (snd_calc[2] - pri_calc[2]) - (snd_calc[0] - pri_calc[0]);
    diff[2] = (snd_calc[2] - pri_calc[2]) - (snd_calc[1] - pri_calc[1]);

    for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
    {
        if (diff[i] < 0)
        {
            diff[i] = -diff[i];
        }
    }

    /* Normally select freq0 and freq1 addition */
    /* If the following conditions are false, it is estimated to be a noise environment. */
    /* Compare with the combination with the other frequency difference (including margin). */
    if ((diff[0] < (diff[1] * 2)) && (diff[0] < ((diff[2] * 3) / 2)))
    {
        add_pri = pri_calc[0] + pri_calc[1];
        add_snd = snd_calc[0] + snd_calc[1];

        selected_freq = 0x3;
    }
    else
    {
        if (diff[1] < diff[2])
        {
            add_pri = pri_calc[0] + pri_calc[2];
            add_snd = snd_calc[0] + snd_calc[2];

            selected_freq = 0x5;
        }
        else
        {
            add_pri = pri_calc[1] + pri_calc[2];
            add_snd = snd_calc[1] + snd_calc[2];

            selected_freq = 0x6;
        }
    }
 #endif

    if (CTSU_COUNT_MAX < add_pri)
    {
        *p_pri_data = CTSU_COUNT_MAX;
    }
    else
    {
        *p_pri_data = (uint16_t) add_pri;
    }

    if (NULL != p_snd_data)
    {
        if (CTSU_COUNT_MAX < add_snd)
        {
            *p_snd_data = CTSU_COUNT_MAX;
        }
        else
        {
            *p_snd_data = (uint16_t) add_snd;
        }
    }

    return selected_freq;
}

 #if (CTSU_CFG_NUM_CFC != 0)

/***********************************************************************************************************************
 * ctsu_corrcfc_process
 ***********************************************************************************************************************/
void ctsu_corrcfc_process (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint8_t            i;
    uint8_t            j;
    uint8_t            ts_id;
    uint8_t            index;
    uint64_t           new_bitmap;
    ctsu_cfg_t const * p_cfg;
    uint16_t           base_value;
    uint16_t           base_conv_dac;
    int32_t            x0;
    int32_t            x1;
    int32_t            y0;

    g_ctsu_corrcfc_info.status = CTSU_CORRECTION_RUN;

    p_cfg = p_instance_ctrl->p_ctsu_cfg;
    index = g_ctsu_corrcfc_info.index;

    /* Get CFC-Rx bitmap at this instance. */
    p_instance_ctrl->cfc_rx_bitmap =
        ((uint64_t) (p_cfg->ctsuchac0 & (~p_cfg->ctsuchtrc0)) |
         ((uint64_t) (p_cfg->ctsuchac1 & (~p_cfg->ctsuchtrc1)) << (CTSU_CORRCFC_SHIFT8)) |
         ((uint64_t) (p_cfg->ctsuchac2 & (~p_cfg->ctsuchtrc2)) << (CTSU_CORRCFC_SHIFT8 * 2)) |
         ((uint64_t) (p_cfg->ctsuchac3 & (~p_cfg->ctsuchtrc3)) << (CTSU_CORRCFC_SHIFT8 * 3)) |
         ((uint64_t) (p_cfg->ctsuchac4 & (~p_cfg->ctsuchtrc4)) << (CTSU_CORRCFC_SHIFT8 * 4)));

    /* Get RX bitmap at this correction. */
    new_bitmap = p_instance_ctrl->cfc_rx_bitmap & (~g_ctsu_corrcfc_info.stored_rx_bitmap);

    /* Get TS id and number of TS in this instance. */
    g_ctsu_corrcfc_info.num_ts = 0;
    for (ts_id = 0; ts_id < CTSU_CORRCFC_TS_MAX; ts_id++)
    {
        if (1 == ((new_bitmap >> ts_id) & 1))
        {
            g_ctsu_corrcfc_info.ts_table[index + g_ctsu_corrcfc_info.num_ts] = ts_id;
            g_ctsu_corrcfc_info.num_ts++;
        }
    }

    /* Add the bitmap of this instance to the entire CFC correction information. */
    g_ctsu_corrcfc_info.stored_rx_bitmap |= p_instance_ctrl->cfc_rx_bitmap;

    /* Write Channel setting */
    R_CTSU->CTSUCHAC0 = (uint8_t) (new_bitmap);
    R_CTSU->CTSUCHAC1 = (uint8_t) (new_bitmap >> (CTSU_CORRCFC_SHIFT8));
    R_CTSU->CTSUCHAC2 = (uint8_t) (new_bitmap >> (CTSU_CORRCFC_SHIFT8 * 2));
    R_CTSU->CTSUCHAC3 = (uint8_t) (new_bitmap >> (CTSU_CORRCFC_SHIFT8 * 3));
    R_CTSU->CTSUCHAC4 = (uint8_t) (new_bitmap >> (CTSU_CORRCFC_SHIFT8 * 4));

    /* Set register of CFC self mode */
    R_CTSU->CTSUCRA_b.MD0  = 1;
    R_CTSU->CTSUCRA_b.MD1  = 0;
    R_CTSU->CTSUCRA_b.MD2  = 1;
    R_CTSU->CTSUMCH_b.MCA0 = 1;
    R_CTSU->CTSUMCH_b.MCA1 = 0;
    R_CTSU->CTSUMCH_b.MCA2 = 0;
    R_CTSU->CTSUMCH_b.MCA3 = 0;

    /* Set time of measurement */
    g_ctsu_corrcfc_info.ctsuwr.ctsuso = (CTSU_SNUM_RECOMMEND << 10);

    /* Step1 : Measure the current input to the CFC-ICO by passing bias current. */
    /*         The theoretical value of the current is 6uA. */
    ctsu_corrcfc_measurement(p_instance_ctrl, &g_ctsu_corrcfc_info.base_value[index], 1);

    /* Step2 : Measure by inputting each constant current from internal DAC to CFC-ICO. */
    R_CTSU->CTSUCRB_b.SSCNT     = 0;
    R_CTSU->CTSUCRA_b.LOAD      = 1;
    R_CTSU->CTSUCALIB_b.CCOCLK  = 0;
    R_CTSU->CTSUCALIB_b.CFCMODE = 1;

    for (j = 0; j < CTSU_CORRCFC_POINT_NUM; j++)
    {
        R_CTSU->CTSUCRA_b.SDPSEL = 0;
        R_CTSU->CTSUSUCLK0       = (uint16_t) (((j + CTSU_CORRCFC_CENTER_POINT) * CTSU_CORRECTION_SUMULTI) - 1);
        R_CTSU->CTSUCRA_b.SDPSEL = 1;

        ctsu_corrcfc_measurement(p_instance_ctrl, &g_ctsu_corrcfc_info.dac_value[index][j], CTSU_CORRCFC_POINT_NUM);
    }

    /* Reset register */
    R_CTSU->CTSUCALIB_b.CFCMODE = 0;
    R_CTSU->CTSUCALIB_b.CCOCLK  = 1;

    /* Step3 : Calculate the error between step1 and step2. */
    for (i = index; i < (index + g_ctsu_corrcfc_info.num_ts); i++)
    {
        /* Linear interpolation calculation */
        base_value = g_ctsu_corrcfc_info.base_value[i];
        j          = 1;
        while (1)
        {
            if ((base_value < g_ctsu_corrcfc_info.dac_value[index][j]) || ((CTSU_CORRCFC_POINT_NUM - 1) == j))
            {
                y0 = (uint16_t) (CTSU_CORRECTION_STD_UNIT * (j + CTSU_CORRCFC_CENTER_POINT));
                x0 = g_ctsu_corrcfc_info.dac_value[index][j];
                x1 = g_ctsu_corrcfc_info.dac_value[index][j - 1];
                break;
            }

            j++;
        }

        base_conv_dac = (uint16_t) (y0 - ((CTSU_CORRECTION_STD_UNIT * (x0 - base_value)) / (x0 - x1)));

        /* Error rate calculation */
        g_ctsu_corrcfc_info.error_rate[i] =
            (uint16_t) (((CTSU_CORRECTION_STD_UNIT * 4) << CTSU_SHIFT_AMOUNT) / base_conv_dac);

        for (j = 0; j < CTSU_CORRCFC_POINT_NUM; j++)
        {
            g_ctsu_corrcfc_info.ref_value[i][j] =
                (uint16_t) ((CTSU_CORRECTION_STD_UNIT * (j + CTSU_CORRCFC_CENTER_POINT) *
                             g_ctsu_corrcfc_info.error_rate[i]) >> CTSU_SHIFT_AMOUNT);
        }
    }

    g_ctsu_corrcfc_info.index = (uint8_t) (index + g_ctsu_corrcfc_info.num_ts);

    g_ctsu_corrcfc_info.status = CTSU_CORRECTION_COMPLETE;
}

/***********************************************************************************************************************
 * ctsu_corrcfc_measurement
 ***********************************************************************************************************************/
void ctsu_corrcfc_measurement (ctsu_instance_ctrl_t * const p_instance_ctrl, uint16_t * data, uint8_t point_num)
{
    uint16_t i;
    uint8_t  cfc_id;
    uint32_t sum[CTSU_CFG_NUM_CFC];

    /* initialize sum array */
    for (cfc_id = 0; cfc_id < CTSU_CFG_NUM_CFC; cfc_id++)
    {
        sum[cfc_id] = 0;
    }

    for (i = 0; i < CTSU_CORRECTION_AVERAGE; i++)
    {
  #if (CTSU_CFG_DTC_SUPPORT_ENABLE == 1)
        ctsu_transfer_configure(p_instance_ctrl);
  #else
        FSP_PARAMETER_NOT_USED(p_instance_ctrl);
  #endif
        p_instance_ctrl->state = CTSU_STATE_SCANNING;
        R_CTSU->CTSUCR0       |= 0x01;
        while (p_instance_ctrl->state != CTSU_STATE_SCANNED)
        {
        }

        /* Get sum */
        for (cfc_id = 0; cfc_id < CTSU_CFG_NUM_CFC; cfc_id++)
        {
            sum[cfc_id] += g_ctsu_corrcfc_info.scanbuf[cfc_id];
        }
    }

    /* Average sum data */
    for (cfc_id = 0; cfc_id < g_ctsu_corrcfc_info.num_ts; cfc_id++)
    {
        *(data + (cfc_id * point_num)) = (uint16_t) (sum[cfc_id] / CTSU_CORRECTION_AVERAGE);
    }
}

 #endif

 #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_auto_correction_register_set
 ***********************************************************************************************************************/
void ctsu_auto_correction_register_set (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t j;
    uint32_t corr_dac;
    uint32_t corr_coef;
    int16_t  range;

    /* Initialization of sensor counter auto correction table register number */
    R_CTSU->CTSUOPT_b.SCACTB = 0;

    /* Enter the correction factor and dac_value in CTSUSCNTACT for 12 correction tables */
    range = (int16_t) p_instance_ctrl->range;
    for (j = 0; j < CTSU_CORRECTION_POINT_NUM; j++)
    {
        corr_dac  = g_ctsu_correction_info.dac_value[range][j];
        corr_coef = g_ctsu_correction_info.coef[range][j];

        R_CTSU->CTSUSCNTACT = (corr_dac << 16) | (corr_coef);
    }
}

 #endif

 #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_multiclock_auto_correction_register_set
 ***********************************************************************************************************************/
void ctsu_multiclock_auto_correction_register_set (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t element_id;
    int16_t  offsetcoeff_1;
    int16_t  offsetcoeff_2;
    uint16_t elem_w_sumulti;

    for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
    {
        elem_w_sumulti = element_id * CTSU_CFG_NUM_SUMULTI;

        /* Calculating the Second Frequency */
        offsetcoeff_1 = ctsu_multiclock_auto_correction_calc(p_instance_ctrl, element_id, 2);

        /* Calculating the Third Frequency */
        offsetcoeff_2 = ctsu_multiclock_auto_correction_calc(p_instance_ctrl, element_id, 3);

        if (0 == p_instance_ctrl->p_ctsu_cfg->ajfen)
        {
            p_instance_ctrl->p_mcact1[elem_w_sumulti + 0] = 0;
            p_instance_ctrl->p_mcact1[elem_w_sumulti + 1] = ((uint32_t) offsetcoeff_1 << 16) |
                                                            (p_instance_ctrl->p_ctsuwr[elem_w_sumulti + 1].ctsuso &
                                                             CTSU_TUNING_MAX);

            p_instance_ctrl->p_mcact1[elem_w_sumulti + 2] = ((uint32_t) offsetcoeff_2 << 16) |
                                                            (p_instance_ctrl->p_ctsuwr[elem_w_sumulti + 2].ctsuso &
                                                             CTSU_TUNING_MAX);
        }
        else
        {
            p_instance_ctrl->p_mcact1[element_id] = ((uint32_t) offsetcoeff_1 << 16) |
                                                    (p_instance_ctrl->p_ctsuwr[elem_w_sumulti + 1].ctsuso &
                                                     CTSU_TUNING_MAX);

            p_instance_ctrl->p_mcact2[element_id] = ((uint32_t) offsetcoeff_2 << 16) |
                                                    (p_instance_ctrl->p_ctsuwr[elem_w_sumulti + 2].ctsuso &
                                                     CTSU_TUNING_MAX);
        }
    }
}

/***********************************************************************************************************************
 * ctsu_multiclock_auto_correction_calc
 ***********************************************************************************************************************/
int16_t ctsu_multiclock_auto_correction_calc (ctsu_instance_ctrl_t * const p_instance_ctrl,
                                              uint16_t                     element_id,
                                              uint8_t                      freq)
{
    uint8_t  current_mode;
    uint8_t  first_sumulti;
    uint8_t  freq_sumulti;
    uint16_t first_so;
    uint16_t freq_so;
    uint8_t  snum_recommend;
    uint8_t  snum;
    int32_t  numerator_1;              // ((SUMULTIn + 1)SO0 - (SUMULTI0 + 1)SOn)
    int32_t  numerator_2;              // ((Gcco * 20uA * 128us) / 1024)(OffsetDACmax / CurrentMode))
    int32_t  numerator_3;              // (SNUM + 1)
    int32_t  denominator;              // ((SUMULTIn + 1)(SNUM0 + 1)
    int16_t  offsetcoeff;
    int32_t  offsetcoeff_tmp;
    uint16_t elem_w_sumulti;
    elem_w_sumulti = element_id * CTSU_CFG_NUM_SUMULTI;

    if (CTSU_ATUNE12_80UA == p_instance_ctrl->p_ctsu_cfg->atune12)
    {
        current_mode = CTSU_AUTO_CURRENT_MODE_80UA;
    }
    else if (CTSU_ATUNE12_40UA == p_instance_ctrl->p_ctsu_cfg->atune12)
    {
        current_mode = CTSU_AUTO_CURRENT_MODE_40UA;
    }
    else if (CTSU_ATUNE12_20UA == p_instance_ctrl->p_ctsu_cfg->atune12)
    {
        current_mode = CTSU_AUTO_CURRENT_MODE_20UA;
    }
    else                               // (CTSU_ATUNE12_160UA == p_instance_ctrl->p_ctsu_cfg->atune12)
    {
        current_mode = CTSU_AUTO_CURRENT_MODE_160UA;
    }

    if (2 == freq)
    {
        freq_sumulti = CTSU_CFG_SUMULTI1 + 1;
        freq_so      = p_instance_ctrl->p_ctsuwr[elem_w_sumulti + 1].ctsuso & CTSU_TUNING_MAX;
    }
    else                               // 3 == freq
    {
        freq_sumulti = CTSU_CFG_SUMULTI2 + 1;
        freq_so      = p_instance_ctrl->p_ctsuwr[elem_w_sumulti + 2].ctsuso & CTSU_TUNING_MAX;
    }

    first_so       = p_instance_ctrl->p_ctsuwr[elem_w_sumulti].ctsuso & CTSU_TUNING_MAX;
    first_sumulti  = CTSU_CFG_SUMULTI0 + 1;
    snum_recommend = CTSU_SNUM_RECOMMEND + 1;
    snum           = (p_instance_ctrl->p_ctsuwr[elem_w_sumulti].ctsuso >> 10) & CTSU_SNUM_MAX;

    numerator_1 = ((int32_t) freq_sumulti * first_so) - ((int32_t) first_sumulti * freq_so);
    numerator_2 = (int32_t) (CTSU_AUTO_REF_COEFFICIENT * CTSU_AUTO_CORRECTION_OFFSET_DAC_MAX / current_mode);
    numerator_3 = (int32_t) snum + 1;
    denominator = (int32_t) (freq_sumulti * snum_recommend);

    /* overflow check int32 */
    if ((CTSU_AUTO_INT32_OVERFLOW_VALUE / numerator_2) <= (numerator_1 * numerator_3))
    {
        offsetcoeff = CTSU_AUTO_INT16_OVERFLOW_VALUE;
        p_instance_ctrl->p_correction_info->calculation_error = 1;
    }
    else if (-(CTSU_AUTO_INT32_OVERFLOW_VALUE / numerator_2) >= (numerator_1 * numerator_3))
    {
        offsetcoeff = CTSU_AUTO_INT16_UNDERFLOW_VALUE;
        p_instance_ctrl->p_correction_info->calculation_error = 1;
    }
    else
    {
        /* calculate offset coefficient with int32 */
        offsetcoeff_tmp = (numerator_1 * numerator_2 * numerator_3 / denominator);

        /* overflow check int16 */
        if (CTSU_AUTO_INT16_OVERFLOW_VALUE <= offsetcoeff_tmp)
        {
            offsetcoeff = CTSU_AUTO_INT16_OVERFLOW_VALUE;
            p_instance_ctrl->p_correction_info->calculation_error = 1;
        }
        else if (CTSU_AUTO_INT16_UNDERFLOW_VALUE >= offsetcoeff_tmp)
        {
            offsetcoeff = CTSU_AUTO_INT16_UNDERFLOW_VALUE;
            p_instance_ctrl->p_correction_info->calculation_error = 1;
        }
        else
        {
            offsetcoeff = (int16_t) offsetcoeff_tmp;
        }
    }

    return offsetcoeff;
}

 #endif

 #if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_auto_judge_threshold_calc
 ***********************************************************************************************************************/
void ctsu_auto_judge_threshold_calc (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint8_t element_id;
    uint8_t i;
    int16_t ajthr_h;
    int16_t ajthr_l;

    for (element_id = 0; element_id < p_instance_ctrl->num_elements; element_id++)
    {
  #if (CTSU_CFG_AUTO_MULTI_CLOCK_CORRECTION_ENABLE == 1)
        if (1 == p_instance_ctrl->p_ctsu_cfg->majirimd)
        {
            if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->p_ctsu_cfg->md)
            {
                p_instance_ctrl->p_ajthr[element_id] =
                    ((uint32_t) p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[element_id].threshold << 16) |
                    (p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[element_id].
                     threshold - p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[element_id].hysteresis);
            }
            else
            {
                ajthr_h =
                    (int16_t) (-(p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[element_id].threshold) +
                               p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[element_id].hysteresis);
                ajthr_l = (int16_t) (-(p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[element_id].threshold));
                p_instance_ctrl->p_ajthr[element_id] = ((uint32_t) ajthr_h << 16) |
                                                       (ajthr_l & CTSU_AUTO_MINUS_BIT_MASK);
            }
        }
        else
  #endif
        {
            for (i = 0; i < CTSU_CFG_NUM_SUMULTI; i++)
            {
                if (CTSU_MODE_SELF_MULTI_SCAN == p_instance_ctrl->p_ctsu_cfg->md)
                {
                    p_instance_ctrl->p_ajthr[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i] =
                        ((uint32_t) p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[(element_id *
                                                                                      CTSU_MAJORITY_MODE_ELEMENTS) +
                                                                                     i].threshold << 16) |
                        (p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) +
                                                                          i].threshold -
                         p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) +
                                                                          i].hysteresis);
                }
                else
                {
                    ajthr_h =
                        (int16_t) (-(p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[(element_id *
                                                                                       CTSU_MAJORITY_MODE_ELEMENTS) +
                                                                                      i].threshold) +
                                   p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[(element_id *
                                                                                     CTSU_MAJORITY_MODE_ELEMENTS) +
                                                                                    i].hysteresis);
                    ajthr_l =
                        (int16_t) (-(p_instance_ctrl->p_ctsu_cfg->p_ctsu_auto_buttons[(element_id *
                                                                                       CTSU_MAJORITY_MODE_ELEMENTS) +
                                                                                      i].threshold));
                    p_instance_ctrl->p_ajthr[(element_id * CTSU_MAJORITY_MODE_ELEMENTS) + i] =
                        ((uint32_t) ajthr_h << 16) | (ajthr_l & CTSU_AUTO_MINUS_BIT_MASK);
                }
            }
        }
    }
}

 #endif

#endif

/*******************************************************************************************************************//**
 * @brief Diagnosis the CTSU peripheral.
 * Please call the function when the return value of R_CTSU_DataGet is FSP_SUCCESS.
 * If an abnormality is detected in any of the diagnosis items, the corresponding diagnosis error is returned.
 * If all diagnosis is complete normally, FSP_SUCCESS is returned.
 * Implements @ref ctsu_api_t::diagnosis.
 *
 *
 * Example:
 * @snippet r_ctsu_example.c R_CTSU_Diagnosis
 *
 * @retval FSP_SUCCESS                             CTSU successfully configured.
 * @retval FSP_ERR_ASSERTION                       Null pointer passed as a parameter.
 * @retval FSP_ERR_NOT_OPEN                        Module is not open.
 * @retval FSP_ERR_CTSU_NOT_GET_DATA               The previous data has not been retrieved by DataGet.
 * @retval FSP_ERR_CTSU_DIAG_LDO_OVER_VOLTAGE      Diagnosis of LDO over voltage failed.
 * @retval FSP_ERR_CTSU_DIAG_CCO_HIGH              Diagnosis of CCO into 19.2uA failed.
 * @retval FSP_ERR_CTSU_DIAG_CCO_LOW               Diagnosis of CCO into 2.4uA failed.
 * @retval FSP_ERR_CTSU_DIAG_SSCG                  Diagnosis of SSCG frequency failed.
 * @retval FSP_ERR_CTSU_DIAG_DAC                   Diagnosis of non-touch count value failed.
 * @retval FSP_ERR_CTSU_DIAG_OUTPUT_VOLTAGE        Diagnosis of LDO output voltage failed.
 * @retval FSP_ERR_CTSU_DIAG_OVER_VOLTAGE          Diagnosis of over voltage detection circuit failed.
 * @retval FSP_ERR_CTSU_DIAG_OVER_CURRENT          Diagnosis of over current detection circuit failed.
 * @retval FSP_ERR_CTSU_DIAG_LOAD_RESISTANCE       Diagnosis of LDO internal resistance value failed.
 * @retval FSP_ERR_CTSU_DIAG_CURRENT_SOURCE        Diagnosis of LDO internal resistance value failed.
 * @retval FSP_ERR_CTSU_DIAG_SENSCLK_GAIN          Diagnosis of SENSCLK frequency gain failed.
 * @retval FSP_ERR_CTSU_DIAG_SUCLK_GAIN            Diagnosis of SUCLK frequency gain failed.
 * @retval FSP_ERR_CTSU_DIAG_CLOCK_RECOVERY        Diagnosis of SUCLK clock recovery function failed.
 * @retval FSP_ERR_CTSU_DIAG_CFC_GAIN              Diagnosis of CFC oscillator gain failed.
 **********************************************************************************************************************/

fsp_err_t R_CTSU_Diagnosis (ctsu_ctrl_t * const p_ctrl)
{
#if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    fsp_err_t diag_err;
#endif
    ctsu_instance_ctrl_t * p_instance_ctrl = (ctsu_instance_ctrl_t *) p_ctrl;

#if (CTSU_CFG_PARAM_CHECKING_ENABLE == 1)
    FSP_ASSERT(p_instance_ctrl);
    FSP_ERROR_RETURN(CTSU_OPEN == p_instance_ctrl->open, FSP_ERR_NOT_OPEN);
#endif
    FSP_ERROR_RETURN(CTSU_STATE_SCANNED != p_instance_ctrl->state, FSP_ERR_CTSU_NOT_GET_DATA);

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_DIAG_COMPLETE == g_ctsu_diag_info.state)
    {
        diag_err = ctsu_diag_ldo_over_voltage_result();
        if (FSP_SUCCESS != diag_err)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_INIT;

            return FSP_ERR_CTSU_DIAG_LDO_OVER_VOLTAGE;
        }

        diag_err = ctsu_diag_oscillator_high_result();
        if (FSP_SUCCESS != diag_err)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_INIT;

            return FSP_ERR_CTSU_DIAG_CCO_HIGH;
        }

        diag_err = ctsu_diag_oscillator_low_result();
        if (FSP_SUCCESS != diag_err)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_INIT;

            return FSP_ERR_CTSU_DIAG_CCO_LOW;
        }

        diag_err = ctsu_diag_sscg_result();
        if (FSP_SUCCESS != diag_err)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_INIT;

            return FSP_ERR_CTSU_DIAG_SSCG;
        }

        diag_err = ctsu_diag_dac_result();
        if (FSP_SUCCESS != diag_err)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_INIT;

            return FSP_ERR_CTSU_DIAG_DAC;
        }

        g_ctsu_diag_info.state = CTSU_DIAG_INIT;
    }
 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)
    if (CTSU_DIAG_COMPLETE == g_ctsu_diag_info.state)
    {
        diag_err = g_ctsu_diag_info.test_result;
        g_ctsu_diag_info.test_result = FSP_SUCCESS;
        g_ctsu_diag_info.test_count  = 0;
        g_ctsu_diag_info.state       = CTSU_DIAG_INIT;

        return diag_err;
    }
 #endif
#endif

    /* if all checks passed to this point, return success */
    return FSP_SUCCESS;
}

#if (BSP_FEATURE_CTSU_VERSION == 1)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)

/***********************************************************************************************************************
 * ctsu_diag_scan_start1
 ***********************************************************************************************************************/
static void ctsu_diag_scan_start1 (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    if (CTSU_DIAG_INIT == g_ctsu_diag_info.state)
    {
        g_ctsu_diag_info.state = CTSU_DIAG_OVER_VOLTAGE;
    }

    /* ctsu normal scan register save */
    g_ctsu_diag_reg.ctsucr0    = R_CTSU->CTSUCR0;
    g_ctsu_diag_reg.ctsucr1    = R_CTSU->CTSUCR1;
    g_ctsu_diag_reg.ctsusdprs  = R_CTSU->CTSUSDPRS;
    g_ctsu_diag_reg.ctsusst    = R_CTSU->CTSUSST;
    g_ctsu_diag_reg.ctsuchac0  = R_CTSU->CTSUCHAC[0];
    g_ctsu_diag_reg.ctsuchac1  = R_CTSU->CTSUCHAC[1];
    g_ctsu_diag_reg.ctsuchac2  = R_CTSU->CTSUCHAC[2];
    g_ctsu_diag_reg.ctsuchtrc0 = R_CTSU->CTSUCHTRC[0];
    g_ctsu_diag_reg.ctsuchtrc1 = R_CTSU->CTSUCHTRC[1];
    g_ctsu_diag_reg.ctsuchtrc2 = R_CTSU->CTSUCHTRC[2];
    g_ctsu_diag_reg.ctsudclkc  = R_CTSU->CTSUDCLKC;
    g_ctsu_diag_reg.ctsuerrs   = R_CTSU->CTSUERRS;

    /* scan register setting */
    if (CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state)
    {
        ctsu_diag_ldo_over_voltage_scan_start();
    }

    if (CTSU_DIAG_CCO_HIGH == g_ctsu_diag_info.state)
    {
        ctsu_diag_oscillator_high_scan_start();
    }

    if (CTSU_DIAG_CCO_LOW == g_ctsu_diag_info.state)
    {
        ctsu_diag_oscillator_low_scan_start();
    }

    if (CTSU_DIAG_SSCG == g_ctsu_diag_info.state)
    {
        ctsu_diag_sscg_scan_start();
    }

    if (CTSU_DIAG_DAC == g_ctsu_diag_info.state)
    {
        ctsu_diag_dac_scan_start(p_instance_ctrl);
    }
}

/***********************************************************************************************************************
 * ctsu_diag_data_get1
 ***********************************************************************************************************************/
static fsp_err_t ctsu_diag_data_get1 (void)
{
    fsp_err_t err;

    /* data get */
    if (CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state)
    {
        ctsu_diag_ldo_over_voltage_data_get();

        g_ctsu_diag_info.state = CTSU_DIAG_CCO_HIGH;
    }
    else if (CTSU_DIAG_CCO_HIGH == g_ctsu_diag_info.state)
    {
        ctsu_diag_oscillator_high_data_get();

        g_ctsu_diag_info.state = CTSU_DIAG_CCO_LOW;
    }
    else if (CTSU_DIAG_CCO_LOW == g_ctsu_diag_info.state)
    {
        ctsu_diag_oscillator_low_data_get();

        g_ctsu_diag_info.state = CTSU_DIAG_SSCG;
    }
    else if (CTSU_DIAG_SSCG == g_ctsu_diag_info.state)
    {
        ctsu_diag_sscg_data_get();

        g_ctsu_diag_info.state = CTSU_DIAG_DAC;
    }
    else if (CTSU_DIAG_DAC == g_ctsu_diag_info.state)
    {
        ctsu_diag_dac_data_get();
        if (CTSU_TUNING_INCOMPLETE == g_ctsu_diag_info.tuning)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_DAC;
        }
        else
        {
            g_ctsu_diag_info.loop_count++;
            if (6 <= g_ctsu_diag_info.loop_count)
            {
                g_ctsu_diag_info.state      = CTSU_DIAG_COMPLETE;
                g_ctsu_diag_info.loop_count = 0;
            }
        }
    }
    else
    {
    }

    /* register restore */
    R_CTSU->CTSUCR0                = g_ctsu_diag_reg.ctsucr0;
    R_CTSU->CTSUCR1                = g_ctsu_diag_reg.ctsucr1;
    R_CTSU->CTSUSDPRS              = g_ctsu_diag_reg.ctsusdprs;
    R_CTSU->CTSUSST                = g_ctsu_diag_reg.ctsusst;
    R_CTSU->CTSUCHAC[0]            = g_ctsu_diag_reg.ctsuchac0;
    R_CTSU->CTSUCHAC[1]            = g_ctsu_diag_reg.ctsuchac1;
    R_CTSU->CTSUCHAC[2]            = g_ctsu_diag_reg.ctsuchac2;
    R_CTSU->CTSUCHTRC[0]           = g_ctsu_diag_reg.ctsuchtrc0;
    R_CTSU->CTSUCHTRC[1]           = g_ctsu_diag_reg.ctsuchtrc1;
    R_CTSU->CTSUCHTRC[2]           = g_ctsu_diag_reg.ctsuchtrc2;
    R_CTSU->CTSUDCLKC              = g_ctsu_diag_reg.ctsudclkc;
    R_CTSU->CTSUERRS_b.CTSUSPMD    = 0;
    R_CTSU->CTSUERRS_b.CTSUTSOD    = 0;
    R_CTSU->CTSUERRS_b.CTSUDRV     = 0;
    R_CTSU->CTSUERRS_b.CTSUTSOC    = 0;
    R_CTSU->CTSUERRS_b.CTSUCLKSEL1 = 0;

    if (CTSU_DIAG_COMPLETE == g_ctsu_diag_info.state)
    {
        err = FSP_SUCCESS;
    }
    else
    {
        err = FSP_ERR_CTSU_DIAG_NOT_YET;
    }

    return err;
}

static void ctsu_diag_ldo_over_voltage_scan_start (void)
{
    uint32_t pclkb_mhz;
    uint32_t ctsu_sdpa;

    /* Set power on */
    R_CTSU->CTSUCR1 = ((CTSU_CFG_LOW_VOLTAGE_MODE << 2) | (CTSU_CSW_ON << 1) | CTSU_PON_ON);

    /* Synchronous Noise Reduction Setting */
    R_CTSU->CTSUSDPRS = ((CTSU_SOFF_ON << 6) | (CTSU_PRMODE_62_PULSES << 4) | CTSU_PRRATIO_RECOMMEND);

    /* High Pass Noise Reduction- ALWAYS 0x30 as per HW Manual */
    R_CTSU->CTSUDCLKC = ((CTSU_SSCNT << 4) | CTSU_SSMOD);

    /* Sensor Stabilization- ALWAYS 0x10 as per HW Manual */
    R_CTSU->CTSUSST = (CTSU_SST_RECOMMEND);

    /* Since CLK is rewritten by correction, set here. */
    R_CTSU->CTSUCR1 |= (uint8_t) (CTSU_CFG_PCLK_DIVISION << 4);

    pclkb_mhz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / CTSU_PCLKB_FREQ_MHZ;
    if (CTSU_PCLKB_FREQ_RANGE1 >= pclkb_mhz)
    {
        R_CTSU->CTSUCR1 |= (0 << 4);
        ctsu_sdpa        = pclkb_mhz - 1;
    }
    else if ((CTSU_PCLKB_FREQ_RANGE1 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE2 >= pclkb_mhz))
    {
        R_CTSU->CTSUCR1 |= (1 << 4);
        ctsu_sdpa        = (pclkb_mhz / 2) - 1;
    }
    else
    {
        R_CTSU->CTSUCR1 |= (2 << 4);
        ctsu_sdpa        = (pclkb_mhz / 4) - 1;
    }

    R_CTSU->CTSUCR1     |= (CTSU_MODE_SELF_MULTI_SCAN << 6);
    R_CTSU->CTSUCHAC[0]  = 0x01;
    R_CTSU->CTSUCHAC[1]  = 0x00;
    R_CTSU->CTSUCHAC[2]  = 0x00;
    R_CTSU->CTSUCHAC[3]  = 0x00;
    R_CTSU->CTSUCHAC[4]  = 0x00;
    R_CTSU->CTSUCHTRC[0] = 0x00;
    R_CTSU->CTSUCHTRC[1] = 0x00;
    R_CTSU->CTSUCHTRC[2] = 0x00;
    R_CTSU->CTSUCHTRC[3] = 0x00;
    R_CTSU->CTSUCHTRC[4] = 0x00;

    /* Correction measurement setting */
    R_CTSU->CTSUERRS_b.CTSUSPMD    = 0;
    R_CTSU->CTSUERRS_b.CTSUTSOD    = 0;
    R_CTSU->CTSUERRS_b.CTSUDRV     = 0;
    R_CTSU->CTSUERRS_b.CTSUCLKSEL1 = 0;
    R_CTSU->CTSUERRS_b.CTSUTSOC    = 1;
    g_ctsu_diag_info.icomp         = 0;

    g_ctsu_diag_info.ctsuwr.ctsussc = (CTSU_SSDIV_0500 << 8);
    g_ctsu_diag_info.ctsuwr.ctsuso0 = CTSU_DIAG_DAC_SO_MAX;
    g_ctsu_diag_info.ctsuwr.ctsuso1 = (uint16_t) ((CTSU_ICOG_66 << 13) | (ctsu_sdpa << 8) | CTSU_RICOA_RECOMMEND);
}

static void ctsu_diag_ldo_over_voltage_data_get (void)
{
    /* Nothing */
}

static fsp_err_t ctsu_diag_ldo_over_voltage_result (void)
{
    if (1 != g_ctsu_diag_info.icomp)
    {
        return FSP_ERR_CTSU_DIAG_LDO_OVER_VOLTAGE;
    }

    /* if all checks passed to this point, return success */
    return FSP_SUCCESS;
}

static void ctsu_diag_oscillator_high_scan_start (void)
{
    uint32_t ctsu_sdpa;
    uint32_t pclkb_mhz;

    /* Set power on */
    R_CTSU->CTSUCR1 = ((CTSU_CFG_LOW_VOLTAGE_MODE << 2) | (CTSU_CSW_ON << 1) | CTSU_PON_ON);

    /* Synchronous Noise Reduction Setting */
    R_CTSU->CTSUSDPRS = ((CTSU_SOFF_ON << 6) | (CTSU_PRMODE_62_PULSES << 4) | CTSU_PRRATIO_RECOMMEND);

    /* High Pass Noise Reduction- ALWAYS 0x30 as per HW Manual */
    R_CTSU->CTSUDCLKC = ((CTSU_SSCNT << 4) | CTSU_SSMOD);

    /* Sensor Stabilization- ALWAYS 0x10 as per HW Manual */
    R_CTSU->CTSUSST = (CTSU_SST_RECOMMEND);

    /* Since CLK is rewritten by correction, set here. */
    R_CTSU->CTSUCR1 |= (uint8_t) (CTSU_CFG_PCLK_DIVISION << 4);

    pclkb_mhz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / CTSU_PCLKB_FREQ_MHZ;
    if (CTSU_PCLKB_FREQ_RANGE1 >= pclkb_mhz)
    {
        R_CTSU->CTSUCR1 |= (0 << 4);
        ctsu_sdpa        = pclkb_mhz - 1;
    }
    else if ((CTSU_PCLKB_FREQ_RANGE1 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE2 >= pclkb_mhz))
    {
        R_CTSU->CTSUCR1 |= (1 << 4);
        ctsu_sdpa        = (pclkb_mhz / 2) - 1;
    }
    else
    {
        R_CTSU->CTSUCR1 |= (2 << 4);
        ctsu_sdpa        = (pclkb_mhz / 4) - 1;
    }

    R_CTSU->CTSUCR1     |= (CTSU_MODE_SELF_MULTI_SCAN << 6);
    R_CTSU->CTSUCHAC[0]  = 0x01;
    R_CTSU->CTSUCHAC[1]  = 0x00;
    R_CTSU->CTSUCHAC[2]  = 0x00;
    R_CTSU->CTSUCHAC[3]  = 0x00;
    R_CTSU->CTSUCHAC[4]  = 0x00;
    R_CTSU->CTSUCHTRC[0] = 0x00;
    R_CTSU->CTSUCHTRC[1] = 0x00;
    R_CTSU->CTSUCHTRC[2] = 0x00;
    R_CTSU->CTSUCHTRC[3] = 0x00;
    R_CTSU->CTSUCHTRC[4] = 0x00;

    R_CTSU->CTSUSO0_b.CTSUSO = 0;

    g_ctsu_diag_info.ctsuwr.ctsussc = (CTSU_SSDIV_0500 << 8);
    g_ctsu_diag_info.ctsuwr.ctsuso0 = 0x0000;
    g_ctsu_diag_info.ctsuwr.ctsuso1 = (uint16_t) ((CTSU_ICOG_66 << 13) | (ctsu_sdpa << 8) | CTSU_RICOA_RECOMMEND);

    /* Correction measurement setting */
    R_CTSU->CTSUERRS_b.CTSUSPMD = 2;   // 0x82
    R_CTSU->CTSUERRS_b.CTSUTSOC = 1;
    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
}

static void ctsu_diag_oscillator_high_data_get (void)
{
    g_ctsu_diag_info.cco_high = g_ctsu_diag_info.scanbuf.sen;
}

static fsp_err_t ctsu_diag_oscillator_high_result (void)
{
    if ((g_ctsu_diag_info.cco_high < CTSU_CFG_DIAG_CCO_HIGH_MAX) &&
        (g_ctsu_diag_info.cco_high > CTSU_CFG_DIAG_CCO_HIGH_MIN))
    {
    }
    else
    {
        return FSP_ERR_CTSU_DIAG_CCO_HIGH;
    }

    /* if all checks passed to this point, return success */
    return FSP_SUCCESS;
}

static void ctsu_diag_oscillator_low_scan_start (void)
{
    uint32_t ctsu_sdpa;
    uint32_t pclkb_mhz;

    /* Set power on */
    R_CTSU->CTSUCR1 = ((CTSU_CFG_LOW_VOLTAGE_MODE << 2) | (CTSU_CSW_ON << 1) | CTSU_PON_ON);

    /* Synchronous Noise Reduction Setting */
    R_CTSU->CTSUSDPRS = ((CTSU_SOFF_ON << 6) | (CTSU_PRMODE_62_PULSES << 4) | CTSU_PRRATIO_RECOMMEND);

    /* High Pass Noise Reduction- ALWAYS 0x30 as per HW Manual */
    R_CTSU->CTSUDCLKC = ((CTSU_SSCNT << 4) | CTSU_SSMOD);

    /* Sensor Stabilization- ALWAYS 0x10 as per HW Manual */
    R_CTSU->CTSUSST = (CTSU_SST_RECOMMEND);

    /* Since CLK is rewritten by correction, set here. */
    R_CTSU->CTSUCR1 |= (uint8_t) (CTSU_CFG_PCLK_DIVISION << 4);

    pclkb_mhz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / CTSU_PCLKB_FREQ_MHZ;
    if (CTSU_PCLKB_FREQ_RANGE1 >= pclkb_mhz)
    {
        R_CTSU->CTSUCR1 |= (0 << 4);
        ctsu_sdpa        = pclkb_mhz - 1;
    }
    else if ((CTSU_PCLKB_FREQ_RANGE1 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE2 >= pclkb_mhz))
    {
        R_CTSU->CTSUCR1 |= (1 << 4);
        ctsu_sdpa        = (pclkb_mhz / 2) - 1;
    }
    else
    {
        R_CTSU->CTSUCR1 |= (2 << 4);
        ctsu_sdpa        = (pclkb_mhz / 4) - 1;
    }

    R_CTSU->CTSUCR1     |= (CTSU_MODE_SELF_MULTI_SCAN << 6);
    R_CTSU->CTSUCHAC[0]  = 0x01;
    R_CTSU->CTSUCHAC[1]  = 0x00;
    R_CTSU->CTSUCHAC[2]  = 0x00;
    R_CTSU->CTSUCHAC[3]  = 0x00;
    R_CTSU->CTSUCHAC[4]  = 0x00;
    R_CTSU->CTSUCHTRC[0] = 0x00;
    R_CTSU->CTSUCHTRC[1] = 0x00;
    R_CTSU->CTSUCHTRC[2] = 0x00;
    R_CTSU->CTSUCHTRC[3] = 0x00;
    R_CTSU->CTSUCHTRC[4] = 0x00;

    R_CTSU->CTSUSO0_b.CTSUSO = 0;

    g_ctsu_diag_info.ctsuwr.ctsussc = (CTSU_SSDIV_0500 << 8);
    g_ctsu_diag_info.ctsuwr.ctsuso0 = 0x0000;
    g_ctsu_diag_info.ctsuwr.ctsuso1 = (uint16_t) ((CTSU_ICOG_66 << 13) | (ctsu_sdpa << 8) | CTSU_RICOA_RECOMMEND);

    /* Correction measurement setting */
    R_CTSU->CTSUERRS_b.CTSUSPMD = 0;   // 0x80
    R_CTSU->CTSUERRS_b.CTSUTSOC = 1;
    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
}

static void ctsu_diag_oscillator_low_data_get (void)
{
    g_ctsu_diag_info.cco_low = g_ctsu_diag_info.scanbuf.sen;
}

static fsp_err_t ctsu_diag_oscillator_low_result (void)
{
    if ((g_ctsu_diag_info.cco_low < CTSU_CFG_DIAG_CCO_LOW_MAX) &&
        (g_ctsu_diag_info.cco_low > CTSU_CFG_DIAG_CCO_LOW_MIN))
    {
    }
    else
    {
        return FSP_ERR_CTSU_DIAG_CCO_LOW;
    }

    /* if all checks passed to this point, return success */
    return FSP_SUCCESS;
}

static void ctsu_diag_sscg_scan_start (void)
{
    uint32_t ctsu_sdpa;
    uint32_t pclkb_mhz;

    /* Set power on */
    R_CTSU->CTSUCR1 = ((CTSU_CFG_LOW_VOLTAGE_MODE << 2) | (CTSU_CSW_ON << 1) | CTSU_PON_ON);

    /* Synchronous Noise Reduction Setting */
    R_CTSU->CTSUSDPRS = ((CTSU_SOFF_ON << 6) | (CTSU_PRMODE_62_PULSES << 4) | CTSU_PRRATIO_RECOMMEND);

    /* High Pass Noise Reduction- ALWAYS 0x30 as per HW Manual */
    R_CTSU->CTSUDCLKC = ((CTSU_SSCNT << 4) | CTSU_SSMOD);

    /* Sensor Stabilization- ALWAYS 0x10 as per HW Manual */
    R_CTSU->CTSUSST = (CTSU_SST_RECOMMEND);

    /* Since CLK is rewritten by correction, set here. */
    R_CTSU->CTSUCR1 |= (uint8_t) (CTSU_CFG_PCLK_DIVISION << 4);

    pclkb_mhz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / CTSU_PCLKB_FREQ_MHZ;
    if (CTSU_PCLKB_FREQ_RANGE1 >= pclkb_mhz)
    {
        R_CTSU->CTSUCR1 |= (0 << 4);
        ctsu_sdpa        = pclkb_mhz - 1;
    }
    else if ((CTSU_PCLKB_FREQ_RANGE1 < pclkb_mhz) && (CTSU_PCLKB_FREQ_RANGE2 >= pclkb_mhz))
    {
        R_CTSU->CTSUCR1 |= (1 << 4);
        ctsu_sdpa        = (pclkb_mhz / 2) - 1;
    }
    else
    {
        R_CTSU->CTSUCR1 |= (2 << 4);
        ctsu_sdpa        = (pclkb_mhz / 4) - 1;
    }

    R_CTSU->CTSUCR1 |= (CTSU_MODE_SELF_MULTI_SCAN << 6);

    R_CTSU->CTSUCHAC[0]  = 0x01;
    R_CTSU->CTSUCHAC[1]  = 0x00;
    R_CTSU->CTSUCHAC[2]  = 0x00;
    R_CTSU->CTSUCHAC[3]  = 0x00;
    R_CTSU->CTSUCHAC[4]  = 0x00;
    R_CTSU->CTSUCHTRC[0] = 0x00;
    R_CTSU->CTSUCHTRC[1] = 0x00;
    R_CTSU->CTSUCHTRC[2] = 0x00;
    R_CTSU->CTSUCHTRC[3] = 0x00;
    R_CTSU->CTSUCHTRC[4] = 0x00;

    R_CTSU->CTSUSO0_b.CTSUSO = 0;

    g_ctsu_diag_info.ctsuwr.ctsussc = (CTSU_SSDIV_0500 << 8);
    g_ctsu_diag_info.ctsuwr.ctsuso0 = 0x0000;
    g_ctsu_diag_info.ctsuwr.ctsuso1 = (uint16_t) ((CTSU_ICOG_66 << 13) | (ctsu_sdpa << 8) | CTSU_RICOA_RECOMMEND);

    /* Correction measurement setting */
    R_CTSU->CTSUERRS_b.CTSUSPMD    = 0;
    R_CTSU->CTSUERRS_b.CTSUTSOD    = 0;
    R_CTSU->CTSUERRS_b.CTSUDRV     = 0;
    R_CTSU->CTSUERRS_b.CTSUTSOC    = 1;
    R_CTSU->CTSUERRS_b.CTSUCLKSEL1 = 1;
}

static void ctsu_diag_sscg_data_get (void)
{
    g_ctsu_diag_info.sscg = g_ctsu_diag_info.scanbuf.ref;
}

static fsp_err_t ctsu_diag_sscg_result (void)
{
    if ((g_ctsu_diag_info.sscg > CTSU_CFG_DIAG_SSCG_MAX) || (g_ctsu_diag_info.sscg < CTSU_CFG_DIAG_SSCG_MIN))
    {
        return FSP_ERR_CTSU_DIAG_SSCG;
    }

    /* if all checks passed to this point, return success */
    return FSP_SUCCESS;
}

static void ctsu_diag_dac_initial_tuning (void)
{
    int32_t  diff          = 0;
    uint32_t complete_flag = 0;
    uint16_t ctsuso;

    diff = (int32_t) g_ctsu_diag_info.correct_data - (int32_t) CTSU_DIAG_DAC_TARGET_VALUE;

    ctsuso = g_ctsu_diag_info.ctsuwr.ctsuso0 & CTSU_TUNING_MAX;
    if (0 < diff)
    {
        if (g_ctsu_diag_info.tuning_diff < 0)
        {
            if ((-diff) > g_ctsu_diag_info.tuning_diff)
            {
                ctsuso++;
            }

            complete_flag = 1;
        }
        else
        {
            if (CTSU_TUNING_MAX == ctsuso)
            {
                complete_flag = 1;
            }
            else
            {
                ctsuso++;
                g_ctsu_diag_info.tuning_diff = diff;
            }
        }
    }
    else if (0 == diff)
    {
        complete_flag = 1;
    }
    else
    {
        if (g_ctsu_diag_info.tuning_diff > 0)
        {
            if ((-diff) > g_ctsu_diag_info.tuning_diff)
            {
                ctsuso--;
            }

            complete_flag = 1;
        }
        else
        {
            complete_flag = 1;
        }
    }

    g_ctsu_diag_info.ctsuwr.ctsuso0 &= (uint16_t) (~CTSU_TUNING_MAX);
    g_ctsu_diag_info.ctsuwr.ctsuso0 |= ctsuso;

    if (CTSU_CFG_NUM_SUMULTI == complete_flag)
    {
        g_ctsu_diag_info.tuning_diff = 0;
        g_ctsu_diag_info.so0_4uc_val = ctsuso;
        g_ctsu_diag_info.dac_init    = 3;
        g_ctsu_diag_info.tuning      = CTSU_TUNING_COMPLETE;
        g_ctsu_diag_info.loop_count  = 0;
        g_ctsu_diag_info.dac_cnt[0]  = g_ctsu_diag_info.correct_data;
    }
}

static void ctsu_diag_dac_scan_start (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint8_t temp;

    /* Set power on */
    R_CTSU->CTSUCR1 = ((CTSU_CFG_LOW_VOLTAGE_MODE << 2) | (CTSU_CSW_ON << 1) | CTSU_PON_ON);

    /* Synchronous Noise Reduction Setting */
    R_CTSU->CTSUSDPRS = ((CTSU_SOFF_ON << 6) | (CTSU_PRMODE_62_PULSES << 4) | CTSU_PRRATIO_RECOMMEND);

    /* High Pass Noise Reduction- ALWAYS 0x30 as per HW Manual */
    R_CTSU->CTSUDCLKC = ((CTSU_SSCNT << 4) | CTSU_SSMOD);

    /* Sensor Stabilization- ALWAYS 0x10 as per HW Manual */
    R_CTSU->CTSUSST = (CTSU_SST_RECOMMEND);

    /* Since CLK is rewritten by correction, set here. */
    R_CTSU->CTSUCR1 |= (uint8_t) (CTSU_CFG_PCLK_DIVISION << 4);

    temp            = (uint8_t) (R_CTSU->CTSUCR1 & ~(CTSU_CR1_MODIFY_BIT));
    R_CTSU->CTSUCR1 = (uint8_t) (temp | (p_instance_ctrl->ctsucr1 & CTSU_CR1_MODIFY_BIT) | CTSU_DAC_TEST_ATUNE1); // MD1, MD0, ATUNE1=1
  #if BSP_FEATURE_CTSU_HAS_TXVSEL
    R_CTSU->CTSUCR0 =
        (uint8_t) ((R_CTSU->CTSUCR0 & ~(CTSU_TXVSEL)) | (p_instance_ctrl->p_ctsu_cfg->txvsel & CTSU_TXVSEL));
  #endif

    /* Write Channel setting */
  #if (CTSU_CFG_DIAG_DAC_TS >= 0) && (CTSU_CFG_DIAG_DAC_TS < 8)
    R_CTSU->CTSUCHAC[0] = 1 << CTSU_CFG_DIAG_DAC_TS;
  #else
    R_CTSU->CTSUCHAC[0] = 0;
  #endif
  #if (CTSU_CFG_DIAG_DAC_TS >= 8) && (CTSU_CFG_DIAG_DAC_TS < 16)
    R_CTSU->CTSUCHAC[1] = 1 << (CTSU_CFG_DIAG_DAC_TS - 8);
  #else
    R_CTSU->CTSUCHAC[1] = 0;
  #endif
  #if (CTSU_CFG_DIAG_DAC_TS >= 16) && (CTSU_CFG_DIAG_DAC_TS < 24)
    R_CTSU->CTSUCHAC[2] = 1 << (CTSU_CFG_DIAG_DAC_TS - 16);
  #else
    R_CTSU->CTSUCHAC[2] = 0;
  #endif
  #if (CTSU_CFG_DIAG_DAC_TS >= 24) && (CTSU_CFG_DIAG_DAC_TS < 32)
    R_CTSU->CTSUCHAC[3] = 1 << (CTSU_CFG_DIAG_DAC_TS - 24);
  #else
    R_CTSU->CTSUCHAC[3] = 0;
  #endif
    R_CTSU->CTSUCHAC[4]  = 0;
    R_CTSU->CTSUCHTRC[0] = 0;
    R_CTSU->CTSUCHTRC[1] = 0;
    R_CTSU->CTSUCHTRC[2] = 0;
    R_CTSU->CTSUCHTRC[3] = 0;
    R_CTSU->CTSUCHTRC[4] = 0;

    g_ctsu_diag_info.ctsuwr.ctsussc = (uint16_t) (CTSU_SSDIV_1330 << 8);
    g_ctsu_diag_info.ctsuwr.ctsuso1 = (uint16_t) ((CTSU_ICOG_RECOMMEND << 13) | (7 << 8) | CTSU_RICOA_RECOMMEND);

    if (g_ctsu_diag_info.dac_init > 2)
    {
        /* Apply DAC current */
        if (0 == g_ctsu_diag_info.loop_count)
        {
            g_ctsu_diag_info.ctsuwr.ctsuso0 = (uint16_t) ((3 << 10) | g_ctsu_diag_info.so0_4uc_val);
        }
        else if (1 == g_ctsu_diag_info.loop_count)
        {
            g_ctsu_diag_info.ctsuwr.ctsuso0 =
                (uint16_t) ((3 << 10) | (g_ctsu_diag_info.so0_4uc_val - CTSU_DIAG_DAC_1UC));
        }
        else if (2 == g_ctsu_diag_info.loop_count)
        {
            g_ctsu_diag_info.ctsuwr.ctsuso0 =
                (uint16_t) ((3 << 10) | (g_ctsu_diag_info.so0_4uc_val - CTSU_DIAG_DAC_2UC));
        }
        else if (3 == g_ctsu_diag_info.loop_count)
        {
            g_ctsu_diag_info.ctsuwr.ctsuso0 =
                (uint16_t) ((3 << 10) | (g_ctsu_diag_info.so0_4uc_val - CTSU_DIAG_DAC_4UC));
        }
        else if (4 == g_ctsu_diag_info.loop_count)
        {
            g_ctsu_diag_info.ctsuwr.ctsuso0 =
                (uint16_t) ((3 << 10) | (g_ctsu_diag_info.so0_4uc_val - CTSU_DIAG_DAC_8UC));
        }
        else if (5 == g_ctsu_diag_info.loop_count)
        {
            g_ctsu_diag_info.ctsuwr.ctsuso0 =
                (uint16_t) ((3 << 10) | (g_ctsu_diag_info.so0_4uc_val - CTSU_DIAG_DAC_16UC));
        }
        else
        {
        }
    }

    if (g_ctsu_diag_info.dac_init == 0)
    {
        g_ctsu_diag_info.dac_init       = 1;
        g_ctsu_diag_info.so0_4uc_val    = 0;
        g_ctsu_diag_info.tuning_diff    = 0;
        g_ctsu_diag_info.ctsuwr.ctsuso0 = (3 << 10) + CTSU_DIAG_DAC_START_VALUE;
    }
}

static void ctsu_diag_dac_data_get (void)
{
    ctsu_correction_calc_t calc;

    calc.snum = (g_ctsu_diag_info.ctsuwr.ctsuso0 >> 10) & CTSU_SNUM_MAX;

    if (CTSU_DIAG_DAC == g_ctsu_diag_info.state)
    {
        calc.snum = 3;
    }

    calc.sdpa = (g_ctsu_diag_info.ctsuwr.ctsuso1 >> 8) & CTSU_SDPA_MAX;

    /* Correction process */
    ctsu_correction_calc(&g_ctsu_diag_info.correct_data, g_ctsu_diag_info.scanbuf.sen, &calc);

    if (g_ctsu_diag_info.tuning == CTSU_TUNING_COMPLETE)
    {
        g_ctsu_diag_info.dac_cnt[g_ctsu_diag_info.loop_count] = g_ctsu_diag_info.correct_data;
    }
    else
    {
        ctsu_diag_dac_initial_tuning();
    }
}

static fsp_err_t ctsu_diag_dac_result (void)
{
    uint8_t k;
    if (g_ctsu_diag_info.tuning == CTSU_TUNING_COMPLETE)
    {
        for (k = 0; k < 6; k++)
        {
            if ((g_ctsu_diag_info.dac_cnt[k] > dac_oscil_table[k][0]) ||
                (g_ctsu_diag_info.dac_cnt[k] < dac_oscil_table[k][1]))
            {
                return FSP_ERR_CTSU_DIAG_DAC;
            }
        }
    }

    /* if all checks passed to this point, return success */
    return FSP_SUCCESS;
}

 #endif
#endif

#if (BSP_FEATURE_CTSU_VERSION == 2)
 #if (CTSU_CFG_DIAG_SUPPORT_ENABLE == 1)

/*******************************************************************************************************************//**
 * Starts the diagnostic measurement process. Called within R_CTSU_ScanStart().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 *
 * @note This function is only supported by CTSU2.
 **********************************************************************************************************************/
static void ctsu_diag_scan_start2 (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t adc_err = FSP_SUCCESS;

    if ((CTSU_DIAG_INIT == g_ctsu_diag_info.state) || (CTSU_DIAG_ADC_ERROR == g_ctsu_diag_info.state))
    {
        adc_err = ctsu_diag_adc_open_check();
        if (FSP_SUCCESS == adc_err)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_OUTPUT_VOLTAGE;
        }
        else
        {
            g_ctsu_diag_info.state = CTSU_DIAG_ADC_ERROR;
        }
    }

    /* ctsu normal scan register save */
    ctsu_diag_regi_store2(p_instance_ctrl);

    /* scan register setting */
    if (CTSU_DIAG_OUTPUT_VOLTAGE == g_ctsu_diag_info.state)
    {
        ctsu_diag_output_voltage_scan_start(p_instance_ctrl);
        if (FSP_SUCCESS == g_ctsu_diag_info.test_result)
        {
            if (0 == g_ctsu_diag_info.onetime_exec_flag)
            {
                g_ctsu_diag_info.state = CTSU_DIAG_OVER_VOLTAGE;
            }
            else
            {
                g_ctsu_diag_info.state = CTSU_DIAG_LOAD_RESISTANCE;
            }
        }
        else
        {
            g_ctsu_diag_info.state = CTSU_DIAG_COMPLETE;
            ctsu_diag_regi_restore2();
        }
    }

    if (CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state)
    {
        ctsu_diag_over_voltage_scan_start(p_instance_ctrl);
    }

    if (CTSU_DIAG_OVER_CURRENT == g_ctsu_diag_info.state)
    {
        ctsu_diag_over_current_scan_start(p_instance_ctrl);
    }

    if (CTSU_DIAG_LOAD_RESISTANCE == g_ctsu_diag_info.state)
    {
        ctsu_diag_load_resistance_scan_start(p_instance_ctrl);
    }

    if (CTSU_DIAG_CURRENT_SOURCE == g_ctsu_diag_info.state)
    {
        ctsu_diag_current_source_scan_start(p_instance_ctrl);
    }

    if (CTSU_DIAG_SENSCLK == g_ctsu_diag_info.state)
    {
        ctsu_diag_sensclk_gain_scan_start();
    }

    if (CTSU_DIAG_SUCLK == g_ctsu_diag_info.state)
    {
        ctsu_diag_suclk_gain_scan_start();
    }

    if (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state)
    {
        ctsu_diag_clock_recovery_scan_start();
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    if (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
    {
        ctsu_diag_cfc_gain_scan_start();
    }
  #endif
}

/*******************************************************************************************************************//**
 * Retrieves diagnostic measurement data. Called within R_CTSU_DataGet().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 *
 * @retval  FSP_SUCCESS                    Diagnostic data successfully retrieved.
 * @retval  FSP_ERR_CTSU_DIAG_NOT_YET      Diagnostic data collection is not yet completed.
 * @retval  FSP_ERR_ABORTED                ADC usage is detected in the system.
 *
 * @note This function is only supported by CTSU2.
 **********************************************************************************************************************/
static fsp_err_t ctsu_diag_data_get2 (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    fsp_err_t err;

    if (CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state)
    {
        ctsu_diag_over_voltage_data_get();
    }

    if (CTSU_DIAG_OVER_CURRENT == g_ctsu_diag_info.state)
    {
        ctsu_diag_over_current_data_get();
    }

    if (CTSU_DIAG_LOAD_RESISTANCE == g_ctsu_diag_info.state)
    {
        ctsu_diag_load_resistance_data_get(p_instance_ctrl);
    }

    if (CTSU_DIAG_CURRENT_SOURCE == g_ctsu_diag_info.state)
    {
        ctsu_diag_current_source_data_get(p_instance_ctrl);
    }

    if (CTSU_DIAG_SENSCLK == g_ctsu_diag_info.state)
    {
        ctsu_diag_sensclk_gain_data_get(p_instance_ctrl);
    }

    if (CTSU_DIAG_SUCLK == g_ctsu_diag_info.state)
    {
        ctsu_diag_suclk_gain_data_get(p_instance_ctrl);
    }

    if (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state)
    {
        ctsu_diag_clock_recovery_data_get(p_instance_ctrl);
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    if (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
    {
        ctsu_diag_cfc_gain_data_get(p_instance_ctrl);
    }
  #endif

    /* register restore */
    ctsu_diag_regi_restore2();

    /* Diagnosis state transition */
    if ((CTSU_DIAG_OVER_VOLTAGE == g_ctsu_diag_info.state) &&
        (CTSU_DIAG_OVER_VOLTAGE_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
        g_ctsu_diag_info.state      = CTSU_DIAG_OVER_CURRENT;
    }
    else if ((CTSU_DIAG_OVER_CURRENT == g_ctsu_diag_info.state) &&
             (CTSU_DIAG_OVER_CURRENT_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
        g_ctsu_diag_info.state      = CTSU_DIAG_LOAD_RESISTANCE;
    }
    else if ((CTSU_DIAG_LOAD_RESISTANCE == g_ctsu_diag_info.state) &&
             (CTSU_DIAG_LOAD_RESISTANCE_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
        if (0 == g_ctsu_diag_info.onetime_exec_flag)
        {
            g_ctsu_diag_info.state = CTSU_DIAG_CURRENT_SOURCE;
        }
        else
        {
            g_ctsu_diag_info.state = CTSU_DIAG_SENSCLK;
        }
    }
    else if ((CTSU_DIAG_CURRENT_SOURCE == g_ctsu_diag_info.state) &&
             (CTSU_DIAG_CURRENT_SOURCE_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count        = 0;
        g_ctsu_diag_info.state             = CTSU_DIAG_SENSCLK;
        g_ctsu_diag_info.onetime_exec_flag = 1;
    }
    else if ((CTSU_DIAG_SENSCLK == g_ctsu_diag_info.state) &&
             (CTSU_DIAG_SENSCLK_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
        g_ctsu_diag_info.state      = CTSU_DIAG_SUCLK;
    }
    else if ((CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) && (CTSU_DIAG_SUCLK_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
        g_ctsu_diag_info.state      = CTSU_DIAG_CLOCK_RECOVERY;
    }
    else if ((CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state) &&
             (CTSU_DIAG_CLOCK_RECOVERY_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
  #if (CTSU_CFG_NUM_CFC != 0)
        g_ctsu_diag_info.state = CTSU_DIAG_CFC;
  #else
        g_ctsu_diag_info.state = CTSU_DIAG_COMPLETE;
  #endif
    }

  #if (CTSU_CFG_NUM_CFC != 0)
    else if ((CTSU_DIAG_CFC == g_ctsu_diag_info.state) && (CTSU_DIAG_CFC_TEST_NUM <= g_ctsu_diag_info.test_count))
    {
        g_ctsu_diag_info.test_count = 0;
        g_ctsu_diag_info.state      = CTSU_DIAG_COMPLETE;
    }
  #endif
    else
    {
        /* No processing */
    }

    if (FSP_SUCCESS != g_ctsu_diag_info.test_result)
    {
        g_ctsu_diag_info.state = CTSU_DIAG_COMPLETE;
    }

    if (CTSU_DIAG_COMPLETE == g_ctsu_diag_info.state)
    {
        err = FSP_SUCCESS;
    }
    else if (CTSU_DIAG_ADC_ERROR == g_ctsu_diag_info.state)
    {
        err = FSP_ERR_ABORTED;
    }
    else
    {
        err = FSP_ERR_CTSU_DIAG_NOT_YET;
    }

    return err;
}

/*******************************************************************************************************************//**
 * Saves current register values and configures common register settings for diagnosis.
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 *
 * @note This function is only supported by CTSU2.
 **********************************************************************************************************************/
static void ctsu_diag_regi_store2 (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Store registers settings */
    g_ctsu_diag_reg.ctsucra = R_CTSU->CTSUCRA;
    if ((CTSU_DIAG_SENSCLK == g_ctsu_diag_info.state) ||
        (CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) ||
        (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state)
  #if (CTSU_CFG_NUM_CFC != 0)
        || (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
  #endif
        )
    {
        g_ctsu_diag_reg.ctsucrb = R_CTSU->CTSUCRB;
    }

    g_ctsu_diag_reg.ctsucalib  = R_CTSU->CTSUCALIB;
    g_ctsu_diag_reg.ctsusuclka = R_CTSU->CTSUSUCLKA;
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
    g_ctsu_diag_reg.ctsuopt = R_CTSU->CTSUOPT;
  #endif

    /* Preconfigure registers */
    if ((CTSU_DIAG_SENSCLK == g_ctsu_diag_info.state) ||
        (CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) ||
        (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state)
  #if (CTSU_CFG_NUM_CFC != 0)
        || (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
  #endif
        )
    {
        R_CTSU->CTSUCRA_b.LOAD = 0x01;
    }

    R_CTSU->CTSUMCH_b.MCA0 = 1;
    R_CTSU->CTSUMCH_b.MCA1 = 0;
    R_CTSU->CTSUMCH_b.MCA2 = 0;
    R_CTSU->CTSUMCH_b.MCA3 = 0;

    R_CTSU->CTSUCALIB = 0;

    if ((CTSU_DIAG_OVER_CURRENT != g_ctsu_diag_info.state)
  #if (CTSU_CFG_NUM_CFC != 0)
        && (CTSU_DIAG_CFC != g_ctsu_diag_info.state)
  #endif
        )
    {
        R_CTSU->CTSUCALIB_b.TSOC = 1;
    }

  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
    R_CTSU->CTSUOPT_b.CCOCFEN = 0;
  #endif
    if ((CTSU_DIAG_OVER_VOLTAGE != g_ctsu_diag_info.state) &&
        (CTSU_DIAG_OVER_CURRENT != g_ctsu_diag_info.state) &&
        (CTSU_DIAG_CURRENT_SOURCE != g_ctsu_diag_info.state))
    {
        p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso = (uint32_t) (CTSU_SNUM_RECOMMEND << 10);
    }
}

/*******************************************************************************************************************//**
 * Restores the register values previously saved by ctsu_diag_regi_store2().
 *
 * @pre This function must be called when the state is the same as that of ctsu_diag_regi_store2().
 *
 * @note This function is only supported by CTSU2.
 **********************************************************************************************************************/
static void ctsu_diag_regi_restore2 (void)
{
    /* register restore */
    R_CTSU->CTSUCRA = g_ctsu_diag_reg.ctsucra;
    if ((CTSU_DIAG_SENSCLK == g_ctsu_diag_info.state) ||
        (CTSU_DIAG_SUCLK == g_ctsu_diag_info.state) ||
        (CTSU_DIAG_CLOCK_RECOVERY == g_ctsu_diag_info.state)
  #if (CTSU_CFG_NUM_CFC != 0)
        || (CTSU_DIAG_CFC == g_ctsu_diag_info.state)
  #endif
        )
    {
        R_CTSU->CTSUCRB = g_ctsu_diag_reg.ctsucrb;
    }

    R_CTSU->CTSUCALIB        = g_ctsu_diag_reg.ctsucalib;
    R_CTSU->CTSUCRA_b.SDPSEL = 0;
    R_CTSU->CTSUSUCLKA       = g_ctsu_diag_reg.ctsusuclka;
    R_CTSU->CTSUCRA_b.SDPSEL = 1;
  #if (CTSU_CFG_AUTO_CORRECTION_ENABLE == 1)
    R_CTSU->CTSUOPT = g_ctsu_diag_reg.ctsuopt;
  #endif
}

/*******************************************************************************************************************//**
 * Checks for ADC module usage in the system prior to diagnosis.
 *
 * @retval FSP_SUCCESS                     ADC usage is not detected.
 * @retval FSP_ERR_ALREADY_OPEN            ADC usage is detected.
 **********************************************************************************************************************/
static fsp_err_t ctsu_diag_adc_open_check (void)
{
    fsp_err_t err = FSP_SUCCESS;

  #if (BSP_FEATURE_ADC_D_IS_AVAILABLE == 1)

    /* For R_ADC_D Module */
    if ((1 == R_ADC_D->ADM0_b.ADCE) || (1 == R_ADC_D->ADM0_b.ADCS))
    {
        err = FSP_ERR_ALREADY_OPEN;
    }

  #else

    /* For R_ADC Module */
    if ((1 == R_ADC0->ADCSR_b.TRGE) || (1 == R_ADC0->ADCSR_b.ADST))
    {
        err = FSP_ERR_ALREADY_OPEN;
    }
  #endif

    return err;
}

/*******************************************************************************************************************//**
 * Initiates the Output Voltage Diagnosis process using ADC measurement. Called within ctsu_diag_scan_start2().
 *
 * This function performs diagnosis using ADC measurement instead of CTSU measurement. Since the diagnosis
 * completes within this function, subsequent measurement and data retrieval steps are not executed.
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_output_voltage_scan_start (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Initialize local variable */
    fsp_err_t err_ivref             = FSP_SUCCESS;
    fsp_err_t err_tscap             = FSP_SUCCESS;
    uint16_t  internal_vref_value   = 0;
    uint16_t  output_voltage_value  = 0;
    uint16_t  upper_threshold_value = 0;
    uint16_t  lower_threshold_value = 0;

    /* SO must be set to 0 */
    R_CTSU->CTSUSO_b.SO = 0;

    /* 1. Measure Internal Reference Voltage (VREF) */
    /* Use ADC to measure VREF as a basis for threshold calculation */
    err_ivref = ctsu_diag_adc_measure_average(p_instance_ctrl->p_ctsu_cfg->p_adc_ivref_instance,
                                              ADC_CHANNEL_VOLT,
                                              &internal_vref_value);

    /* Terminate test due to ADC error */
    if (FSP_SUCCESS != err_ivref)
    {
        p_instance_ctrl->p_ctsu_cfg->p_adc_instance->p_api->close(
            p_instance_ctrl->p_ctsu_cfg->p_adc_ivref_instance->p_ctrl);
    }

    /* 2. Calculate the upper and lower threshold values */
    upper_threshold_value = ctsu_diag_calc_threshold_output_voltage(true, internal_vref_value);
    lower_threshold_value = ctsu_diag_calc_threshold_output_voltage(false, internal_vref_value);

    /* 3. Test case configuration */
    for (g_ctsu_diag_info.test_count = 0;
         g_ctsu_diag_info.test_count < CTSU_DIAG_OUTPUT_VOLTAGE_TEST_NUM;
         g_ctsu_diag_info.test_count++)
    {
        switch (g_ctsu_diag_info.test_count)
        {
            case 0:
            {
                /* TEST1: IDLE current (NM/LV = 2.5uA/2uA), Current range = 20uA */
                R_CTSU->CTSUCRA_b.LOAD   = 0;
                R_CTSU->CTSUCRA_b.ATUNE1 = 0;
                R_CTSU->CTSUCRA_b.ATUNE2 = 1;
                break;
            }

            case 1:
            {
                /* TEST2: IDLE current (NM/LV = 2.5uA/2uA), Current range = 40uA */
                R_CTSU->CTSUCRA_b.LOAD   = 0;
                R_CTSU->CTSUCRA_b.ATUNE1 = 1;
                R_CTSU->CTSUCRA_b.ATUNE2 = 0;
                break;
            }

            case 2:
            {
                /* TEST3: IDLE current (NM/LV = 2.5uA/2uA), Current range = 80uA */
                R_CTSU->CTSUCRA_b.LOAD   = 0;
                R_CTSU->CTSUCRA_b.ATUNE1 = 0;
                R_CTSU->CTSUCRA_b.ATUNE2 = 0;
                break;
            }

            case 3:
            {
                /* TEST4: IDLE current (NM/LV = 2.5uA/2uA), Current range = 160uA */
                R_CTSU->CTSUCRA_b.LOAD   = 0;
                R_CTSU->CTSUCRA_b.ATUNE1 = 1;
                R_CTSU->CTSUCRA_b.ATUNE2 = 1;
                break;
            }

            case 4:
            {
                /* TEST5: Resistance Load Mode, Load Resistance = 60kOhm, Current range = 20uA */
                R_CTSU->CTSUCRA_b.LOAD   = 1;
                R_CTSU->CTSUCRA_b.LOAD   = 3;
                R_CTSU->CTSUCRA_b.ATUNE1 = 0;
                R_CTSU->CTSUCRA_b.ATUNE2 = 1;
                break;
            }

            case 5:
            {
                /* TEST6: Resistance Load Mode, Load Resistance = 30kOhm, Current range = 40uA */
                R_CTSU->CTSUCRA_b.LOAD   = 1;
                R_CTSU->CTSUCRA_b.LOAD   = 3;
                R_CTSU->CTSUCRA_b.ATUNE1 = 1;
                R_CTSU->CTSUCRA_b.ATUNE2 = 0;
                break;
            }

            case 6:
            {
                /* TEST7: Resistance Load Mode, Load Resistance = 15kOhm, Current range = 80uA */
                R_CTSU->CTSUCRA_b.LOAD   = 1;
                R_CTSU->CTSUCRA_b.LOAD   = 3;
                R_CTSU->CTSUCRA_b.ATUNE1 = 0;
                R_CTSU->CTSUCRA_b.ATUNE2 = 0;
                break;
            }

            case 7:
            {
                /* TEST8: Resistance Load Mode, Load Resistance = 7.5kOhm, Current range = 160uA */
                R_CTSU->CTSUCRA_b.LOAD   = 1;
                R_CTSU->CTSUCRA_b.LOAD   = 3;
                R_CTSU->CTSUCRA_b.ATUNE1 = 1;
                R_CTSU->CTSUCRA_b.ATUNE2 = 1;
                break;
            }

            /* All tests are complete. */
            default:
            {
                break;
            }
        }

        /* Select power supply calibration mode */
        R_CTSU->CTSUCALIB_b.DRV = 1;

        /* 4. Measure TSCAP Voltage */
        err_tscap = ctsu_diag_adc_measure_average(p_instance_ctrl->p_ctsu_cfg->p_adc_instance,
                                                  CTSU_ADC_VOLTAGE_MEASUREMENT_CHANNEL,
                                                  &output_voltage_value);

        /* Terminate test due to ADC error */
        if (FSP_SUCCESS != err_tscap)
        {
            p_instance_ctrl->p_ctsu_cfg->p_adc_instance->p_api->close(
                p_instance_ctrl->p_ctsu_cfg->p_adc_instance->p_ctrl);
        }

        /* 5. Compare and process result */
        if ((output_voltage_value <= lower_threshold_value) || (output_voltage_value >= upper_threshold_value) ||
            (FSP_SUCCESS != err_ivref) || (FSP_SUCCESS != err_tscap))
        {
            /* FAIL: Set diagnostic error */
            g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_OUTPUT_VOLTAGE;
        }

        /* PASS: Check is skipped because the measured value is within the expected range.
         * Condition: (output_voltage_value < upper_threshold_value) && (output_voltage_value > lower_threshold_value) */

        /* Reset for the next measurement */
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
    }

    /* Reset test count */
    g_ctsu_diag_info.test_count = 0;
}

/*******************************************************************************************************************//**
 * Measures the Internal Reference Voltage (VREF) or TSCAP Voltage.
 *
 * The implementation varies depending on the ADC module used (R_ADC or R_ADC_D). This function performs repeated
 * ADC measurements (for R_ADC_D module) or a single averaged ADC measurement (for R_ADC module) and calculates
 * the final average value.
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 * @param[in]  channel                     ADC channel number:
 *                                         - ADC_CHANNEL_VOLT: Internal Reference Voltage (VREF) measurement.
 *                                         - CTSU_ADC_VOLTAGE_MEASUREMENT_CHANNEL: TSCAP Voltage measurement.
 * @param[out] adc_measured_value          Pointer to store the average measured value in ADC counts.
 *
 * @retval FSP_SUCCESS                     Successfully completed.
 * @retval FSP_ERR_ALREADY_OPEN            ADC module is already open. (Only when parameter checking is enabled).
 * @retval FSP_ERR_INVALID_HW_CONDITION    Hardware condition does not match. (Only when parameter checking is enabled).
 * @retval FSP_ERR_IRQ_BSP_DISABLED        Callback is provided but interrupts are disabled.
 *                                         (Only when parameter checking is enabled).
 * @retval FSP_ERR_IP_CHANNEL_NOT_PRESENT  The specified ADC module does not exist.
 *                                         (Only when parameter checking is enabled).
 **********************************************************************************************************************/
static fsp_err_t ctsu_diag_adc_measure_average (adc_instance_t const * p_adc_instance,
                                                adc_channel_t          channel,
                                                uint16_t             * adc_measured_value)
{
    /* Initialize local variable */
    fsp_err_t    err = FSP_SUCCESS;
    adc_status_t status;

    /* ADC initialization */
    err = p_adc_instance->p_api->open(p_adc_instance->p_ctrl, p_adc_instance->p_cfg);

    /* The return value FSP_ERR_ALREADY_OPEN has been checked in the function ctsu_diag_adc_open_check() */
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* Scanning configuration */
    err = p_adc_instance->p_api->scanCfg(p_adc_instance->p_ctrl, p_adc_instance->p_channel_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

  #if (BSP_FEATURE_ADC_D_IS_AVAILABLE == 1)

    /* For R_ADC_D module */
    uint8_t dummy_count;

    /* Execute an extra measurement (dummy read) if measuring Internal VREF */
    dummy_count = (channel == ADC_CHANNEL_VOLT) ? 1 : 0;
    g_ctsu_diag_info.measurement_sum = 0;

    /* Measure 4times and accumulate the total value (+ 1 dummy read if Internal VREF) */
    for (g_ctsu_diag_info.measurement_count = 0;
         g_ctsu_diag_info.measurement_count < (CTSU_DIAG_AVERAGE_NUM + dummy_count);
         g_ctsu_diag_info.measurement_count++)
    {
        *adc_measured_value = 0;

        /* Start scan */
        err = p_adc_instance->p_api->scanStart(p_adc_instance->p_ctrl);
        if (err != FSP_SUCCESS)
        {
            return err;
        }

        /* Wait for scan completion */
        status.state = ADC_STATE_SCAN_IN_PROGRESS;
        while (ADC_STATE_SCAN_IN_PROGRESS == status.state)
        {
            (void) p_adc_instance->p_api->scanStatusGet(p_adc_instance->p_ctrl, &status);
        }

        /* Read measured value */
        err = p_adc_instance->p_api->read(p_adc_instance->p_ctrl, channel, adc_measured_value);

        /* Discard the first Internal VREF measurement */
        if (!((ADC_CHANNEL_VOLT == channel) && (0 == g_ctsu_diag_info.measurement_count)))
        {
            /* Accumulate measurement value */
            g_ctsu_diag_info.measurement_sum += *adc_measured_value;
        }
    }

    /* Calculate the final average value */
    *adc_measured_value = (uint16_t) (g_ctsu_diag_info.measurement_sum >> CTSU_DIAG_AVERAGE_SHIFT_NUM);
  #else

    /* For R_ADC module */
    /* Start scan */
    err = p_adc_instance->p_api->scanStart(p_adc_instance->p_ctrl);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* Wait for scan completion */
    status.state = ADC_STATE_SCAN_IN_PROGRESS;
    while (ADC_STATE_SCAN_IN_PROGRESS == status.state)
    {
        (void) p_adc_instance->p_api->scanStatusGet(p_adc_instance->p_ctrl, &status);
    }

    /* Read average value (hardware feature) */
    err = p_adc_instance->p_api->read(p_adc_instance->p_ctrl, channel, adc_measured_value);
  #endif

    /* Stop scan */
    (void) p_adc_instance->p_api->scanStop(p_adc_instance->p_ctrl);

    /* Close ADC module */
    p_adc_instance->p_api->close(p_adc_instance->p_ctrl);

    return err;
}

/*******************************************************************************************************************//**
 * Calculates the TSCAP output voltage threshold based on the measured Internal Reference Voltage (VREF).
 *
 * This function computes either the upper or lower threshold value depending on the specified threshold type.
 *
 * @param[in]  threshold_type              Specifies which threshold to calculate:
 *                                         - true:  Upper threshold.
 *                                         - false: Lower threshold.
 * @param[in]  internal_vref_value         Average measured value of the Internal Reference Voltage (VREF)
 *                                         in ADC counts.
 *
 * @return     The calculated threshold value (upper threshold or lower threshold).
 **********************************************************************************************************************/
static uint16_t ctsu_diag_calc_threshold_output_voltage (bool threshold_type, uint16_t internal_vref_value)
{
    /* Initialize local variable */
    uint32_t threshold;

    /* Threshold calculation */
    if (threshold_type)
    {
        /* Upper threshold value = (((V_tscaptyp * (1 + Err_tscap) * (AD_measured + Err_adc)) /
         * (V_base * (1 - Err_base))) + Err_adc) * (1 + margin) */
        threshold = (uint32_t)
                    (((CTSU_DIAG_TSCAP_VREF_TYP_MV *
                       (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_TSCAP_VOLT_TOL_MAX_PCT) *
                       (internal_vref_value + CTSU_DIAG_ADC_MEASUREMENT_ERROR))
                      /
                      (CTSU_DIAG_INTERNAL_VREF_TYP_MV *
                       (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_INTERNAL_VOLT_TOL_PCT)) +
                      CTSU_DIAG_ADC_MEASUREMENT_ERROR) *
                     (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_OUTPUT_VOLTAGE_SELF_TEST_MARGIN_PCT)) /
                    CTSU_DIAG_PERCENT_BASE;
    }
    else
    {
        /* Lower threshold value = (((V_tscaptyp * (1 - Err_tscap) * (AD_measured - Err_adc)) /
         * (V_base * (1 + Err_base))) - Err_adc) * (1 - margin) */
        threshold = (uint32_t)
                    (((CTSU_DIAG_TSCAP_VREF_TYP_MV *
                       (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_TSCAP_VOLT_TOL_MIN_PCT) *
                       (internal_vref_value - CTSU_DIAG_ADC_MEASUREMENT_ERROR))
                      /
                      (CTSU_DIAG_INTERNAL_VREF_TYP_MV *
                       (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_INTERNAL_VOLT_TOL_PCT)) -
                      CTSU_DIAG_ADC_MEASUREMENT_ERROR) *
                     (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_OUTPUT_VOLTAGE_SELF_TEST_MARGIN_PCT)) /
                    CTSU_DIAG_PERCENT_BASE;
    }

    return (uint16_t) (threshold);
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for Over Voltage Detection Diagnosis. Called within ctsu_diag_scan_start2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_over_voltage_scan_start (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Initialize local variable */
    uint32_t offset_current_value = 0;

    /* Pre-configuration for diagnosis */
    R_CTSU->CTSUCRA_b.LOAD   = 1;
    R_CTSU->CTSUCRA_b.LOAD   = 3;
    R_CTSU->CTSUCRA_b.ATUNE1 = 1;
    R_CTSU->CTSUCRA_b.ATUNE2 = 0;

    /* Test case configuration */
    if (0 == g_ctsu_diag_info.test_count)
    {
        /* TEST1: Normal voltage setting. Set Offset current = 0uA (SO = 0x0000).*/
        offset_current_value = CTSU_DIAG_OFFSET_CURRENT_0UA;
    }
    else
    {
        /* TEST2: Over voltage setting. Set Offset current = 80uA (SO = 0x0200). */
        offset_current_value = CTSU_DIAG_OFFSET_CURRENT_80UA;
    }

    /* Set CTSUSO value: Combine SO and SNUM. */
    p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso =
        offset_current_value | (CTSU_SNUM_RECOMMEND << 10);
}

/*******************************************************************************************************************//**
 * Increments the test count for Over Voltage Detection Diagnosis. Called within ctsu_diag_data_get2().
 *
 * This function follows the standardized diagnostic format. It manages the diagnostic sequence by updating
 * the test count, while the actual verification of CTSUSR.ICOMP0 (Over-Voltage Detection Flag) under normal
 * and over-voltage conditions is performed within the measurement end interrupt handler.
 **********************************************************************************************************************/
static void ctsu_diag_over_voltage_data_get (void)
{
    /* Increment test count */
    g_ctsu_diag_info.test_count++;
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for Over Current Detection Diagnosis. Called within ctsu_diag_scan_start2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_over_current_scan_start (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Initialize local variable */
    uint8_t sdpa_value = 0;

    /* Pre-configuration for diagnosis */
    R_CTSU->CTSUCALIB = 0;

    /* Test case configuration */
    if (0 == g_ctsu_diag_info.test_count)
    {
        /* TEST1: Normal voltage setting */
        R_CTSU->CTSUCRA_b.ATUNE1 = 1;                 // Current range = 160uA
        R_CTSU->CTSUCRA_b.ATUNE2 = 1;
        sdpa_value               = CTSU_CFG_SUMULTI0; // Configure SDPA based on SUCLK multiplier
    }
    else
    {
        /* TEST2: Over voltage setting */
        R_CTSU->CTSUCRA_b.ATUNE1 = 0;                                             // Current range = 20uA
        R_CTSU->CTSUCRA_b.ATUNE2 = 1;
        sdpa_value               = p_instance_ctrl->p_ctsu_cfg->p_elements->sdpa; // Use SDPA from QE configuration structure
    }

    /* Set CTSUSO value: Combine SO, SNUM, and SDPA (SUCLK Divider) */
    p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso =
        (CTSU_DIAG_OFFSET_CURRENT_0UA | (CTSU_SNUM_RECOMMEND << 10) | ((uint32_t) sdpa_value << 24));
}

/*******************************************************************************************************************//**
 * Increments the test count for Over Current Detection Diagnosis. Called within ctsu_diag_data_get2().
 *
 * This function follows the standardized diagnostic format. It manages the diagnostic sequence by updating
 * the test count, while the actual verification of CTSUSR.ICOMP1 (CTSU Sense Current Error Monitor) under normal
 * and over-current conditions is performed within the measurement end interrupt handler.
 **********************************************************************************************************************/
static void ctsu_diag_over_current_data_get (void)
{
    /* Increment test count */
    g_ctsu_diag_info.test_count++;
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for Load Resistance Diagnosis. Called within ctsu_diag_scan_start2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_load_resistance_scan_start (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Test case configuration */

    if (g_ctsu_diag_info.test_count <= 3)
    {
        /* Load resistance ON for TEST1 through TEST4 */
        R_CTSU->CTSUCRA_b.LOAD = 1;
        R_CTSU->CTSUCRA_b.LOAD = 3;
    }

    switch (g_ctsu_diag_info.test_count)
    {
        case 0:
        {
            /* TEST1: Load resistance = 60kOhm, Current range = 20uA */
            R_CTSU->CTSUCRA_b.ATUNE1 = 0;
            R_CTSU->CTSUCRA_b.ATUNE2 = 1;
            p_instance_ctrl->range   = CTSU_RANGE_20UA;
            break;
        }

        case 1:
        {
            /* TEST2: Load resistance = 30kOhm, Current range = 40uA */
            R_CTSU->CTSUCRA_b.ATUNE1 = 1;
            R_CTSU->CTSUCRA_b.ATUNE2 = 0;
            p_instance_ctrl->range   = CTSU_RANGE_40UA;
            break;
        }

        case 2:
        {
            /* TEST3: Load resistance = 15kOhm, Current range = 80uA */
            R_CTSU->CTSUCRA_b.ATUNE1 = 0;
            R_CTSU->CTSUCRA_b.ATUNE2 = 0;
            p_instance_ctrl->range   = CTSU_RANGE_80UA;
            break;
        }

        case 3:
        {
            /* TEST4: Load resistance = 7.5kOhm, Current range = 160uA */
            R_CTSU->CTSUCRA_b.ATUNE1 = 1;
            R_CTSU->CTSUCRA_b.ATUNE2 = 1;
            p_instance_ctrl->range   = CTSU_RANGE_160UA;
            break;
        }

        case 4:
        {
            /* TEST5: IDLE current, Current range = 20uA */
            R_CTSU->CTSUCRA_b.LOAD = 0;

            R_CTSU->CTSUCRA_b.ATUNE1 = 0;
            R_CTSU->CTSUCRA_b.ATUNE2 = 1;
            p_instance_ctrl->range   = CTSU_RANGE_20UA;
            break;
        }

        /* All tests are complete. */
        default:
        {
            break;
        }
    }
}

/*******************************************************************************************************************//**
 * Retrieves measurement data for Load Resistance Diagnosis. Called within ctsu_diag_data_get2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_load_resistance_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Initialize local variable */
    uint16_t load_resistance_value       = 0;
    uint16_t upper_threshold_value       = 0;
    uint16_t lower_threshold_value       = 0;
    uint32_t theoretical_value_variation = 0;

    /* Get raw count value and apply correction */
    ctsu_correction_calc_t calc;
    calc.snum  = CTSU_SNUM_RECOMMEND;
    calc.range = p_instance_ctrl->range;
    calc.md    = CTSU_MODE_DIAGNOSIS_SCAN;
    ctsu_correction_calc(&load_resistance_value, (uint16_t) (*(p_instance_ctrl->p_self_raw)), &calc);

    /* Accumulate sum and count */
    g_ctsu_diag_info.measurement_sum += load_resistance_value;
    g_ctsu_diag_info.measurement_count++;

    /* Check if enough samples have been collected for averaging */
    if (g_ctsu_diag_info.measurement_count >= CTSU_DIAG_AVERAGE_NUM)
    {
        /* 1. Calculate the average value */
        load_resistance_value = (uint16_t) (g_ctsu_diag_info.measurement_sum >> CTSU_DIAG_AVERAGE_SHIFT_NUM);

        /* 2. Store the first measurement value */
        if (0 == g_ctsu_diag_info.test_count)
        {
            g_ctsu_diag_info.load_resistance = load_resistance_value;
        }

        /* 3. Calculate thresholds */
        if (4 == g_ctsu_diag_info.test_count) // TEST5: For IDLE current test
        {
            upper_threshold_value = (uint16_t) ((CTSU_DIAG_IDLE_CURRENT_THEORETICAL_COUNT_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_IDLE_CURRENT_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_IDLE_CURRENT_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));

            lower_threshold_value = (uint16_t) ((CTSU_DIAG_IDLE_CURRENT_THEORETICAL_COUNT_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_IDLE_CURRENT_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_IDLE_CURRENT_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));
        }
        else                           // TEST1 through TEST4: For load resistance test
        {
            theoretical_value_variation =
                (uint32_t) ((CTSU_DIAG_LOAD_RESISTANCE_THEORETICAL_COUNT_VALUE <<
                             CTSU_DIAG_LOAD_RESISTANCE_VARIATION_SCALE_SHIFT) /
                            g_ctsu_correction_info.error_rate[g_ctsu_diag_info.test_count]);

            upper_threshold_value = (uint16_t) ((theoretical_value_variation *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_LOAD_RESISTANCE_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_LOAD_RESISTANCE_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));

            lower_threshold_value = (uint16_t) ((theoretical_value_variation *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_LOAD_RESISTANCE_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_LOAD_RESISTANCE_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));
        }

        /* 4. Compare and process result */
        if ((load_resistance_value <= lower_threshold_value) || (load_resistance_value >= upper_threshold_value))
        {
            /* FAIL: Set diagnostic error */
            g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_LOAD_RESISTANCE;
        }

        /* PASS: Check is skipped because the measured value is within the expected range.
         * Condition: (load_resistance_value < upper_threshold_value) && (load_resistance_value > lower_threshold_value) */

        /* Increment test cycle count and Reset for the next measurement */
        g_ctsu_diag_info.test_count++;
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
    }

    /* Wait for enough samples, exit function */
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for Current Offset Diagnosis. Called within ctsu_diag_scan_start2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_current_source_scan_start (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* DAC initial setting */
    R_CTSU->CTSUCALIB_b.DACMSEL  = 1;
    R_CTSU->CTSUCALIB_b.DACCARRY = 1;

    /* LDO Nch setting (20uAmode) */
    R_CTSU->CTSUCRA_b.LOAD   = 1;
    R_CTSU->CTSUCRA_b.LOAD   = 3;
    R_CTSU->CTSUCRA_b.ATUNE1 = 0;
    R_CTSU->CTSUCRA_b.ATUNE2 = 1;

    /* TEST1 through TEST8 */
    if (g_ctsu_diag_info.test_count <= 7)
    {
        /* Upper Current source setting (10uA) */
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_LOW_UNIT_SELECT | (1 << g_ctsu_diag_info.test_count);
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;

        /* Setting time of measurement */
        p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso = (CTSU_SNUM_RECOMMEND << 10) |
                                                                      CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;
    }
    /* TEST9 through TEST16 */
    else if (g_ctsu_diag_info.test_count <= 15)
    {
        /* Upper Current source setting (10uA) */
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_LOW_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT | (1 << (g_ctsu_diag_info.test_count - 8));

        p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso = (CTSU_SNUM_RECOMMEND << 10) |
                                                                      (CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT |
                                                                       (1 << (g_ctsu_diag_info.test_count - 8)));
    }
    /* TEST17 */
    else if (16 == g_ctsu_diag_info.test_count)
    {
        /* Lower Current source setting (1.25uA) */
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_SELECT | CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_DATA_MAX;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_SELECT | CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_DATA_MAX;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_LOW_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;

        p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso = (CTSU_SNUM_RECOMMEND << 10) |
                                                                      CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;
    }
    /* TEST18 through TEST25 */
    else if (g_ctsu_diag_info.test_count <= 24)
    {
        /* Lower Current source setting (1.25uA) */
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_SELECT |
                         (CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_DATA_MAX & ~(1 << (g_ctsu_diag_info.test_count - 17)));
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_SELECT | CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_DATA_MAX;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_LOW_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;

        p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso = (CTSU_SNUM_RECOMMEND << 10) |
                                                                      CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;
    }
    /* TEST26 through TEST27 */
    else
    {
        /* Lower Current source setting (1.25uA) */
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_SELECT | CTSU_DIAG_DAC_LOWER_CURRENT_LOW_UNIT_DATA_MAX;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_SELECT |
                         (CTSU_DIAG_DAC_LOWER_CURRENT_HIGH_UNIT_DATA_MAX & ~(1 << (g_ctsu_diag_info.test_count - 25)));
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_LOW_UNIT_SELECT;
        R_CTSU->CTSUSO = CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;

        p_instance_ctrl->p_ctsuwr[p_instance_ctrl->wr_index].ctsuso = (CTSU_SNUM_RECOMMEND << 10) |
                                                                      CTSU_DIAG_DAC_UPPER_CURRENT_HIGH_UNIT_SELECT;
    }
}

/*******************************************************************************************************************//**
 * Retrieves measurement data for Current Offset Diagnosis. Called within ctsu_diag_data_get2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_current_source_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t current_source_value;
    uint16_t average_data;
    uint16_t current_source_diff_value = 0;
    uint16_t upper_threshold_value;
    uint16_t lower_threshold_value;

    ctsu_correction_calc_t calc;
    calc.snum  = CTSU_SNUM_RECOMMEND;
    calc.range = CTSU_RANGE_20UA;
    calc.md    = CTSU_MODE_DIAGNOSIS_SCAN;
    ctsu_correction_calc(&current_source_value, *(p_instance_ctrl->p_self_raw), &calc);

    g_ctsu_diag_info.measurement_sum   += current_source_value;
    g_ctsu_diag_info.measurement_count += 1;

    if (CTSU_DIAG_AVERAGE_NUM <= g_ctsu_diag_info.measurement_count)
    {
        average_data = (uint16_t) (g_ctsu_diag_info.measurement_sum >> CTSU_DIAG_AVERAGE_SHIFT_NUM);

        /* Upper current source test */
        if (15 >= g_ctsu_diag_info.test_count)
        {
            if (g_ctsu_diag_info.load_resistance > average_data)
            {
                current_source_diff_value = g_ctsu_diag_info.load_resistance - average_data;
            }

            upper_threshold_value = (uint16_t) ((CTSU_DIAG_UPPER_CURRENT_SOURCE_DIFF_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_UPPER_CURRENT_SOURCE_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_UPPER_CURRENT_SOURCE_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));
            lower_threshold_value = (uint16_t) ((CTSU_DIAG_UPPER_CURRENT_SOURCE_DIFF_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_UPPER_CURRENT_SOURCE_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_UPPER_CURRENT_SOURCE_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));
        }
        else if (16 == g_ctsu_diag_info.test_count)
        {
            g_ctsu_diag_info.average_data_pre = average_data;
        }
        /* Lower current source test */
        else
        {
            if (average_data > g_ctsu_diag_info.average_data_pre)
            {
                current_source_diff_value = average_data - g_ctsu_diag_info.average_data_pre;
            }

            upper_threshold_value = (uint16_t) ((CTSU_DIAG_LOWER_CURRENT_SOURCE_DIFF_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_LOWER_CURRENT_SOURCE_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE +
                                                  CTSU_DIAG_LOWER_CURRENT_SOURCE_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));
            lower_threshold_value = (uint16_t) ((CTSU_DIAG_LOWER_CURRENT_SOURCE_DIFF_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_LOWER_CURRENT_SOURCE_SELF_TEST_MARGIN_PCT) *
                                                 (CTSU_DIAG_PERCENT_BASE -
                                                  CTSU_DIAG_LOWER_CURRENT_SOURCE_SPEC_MARGIN_PCT)) /
                                                (CTSU_DIAG_PERCENT_BASE * CTSU_DIAG_PERCENT_BASE));
        }

        if (16 != g_ctsu_diag_info.test_count)
        {
            if ((current_source_diff_value <= lower_threshold_value) ||
                (current_source_diff_value >= upper_threshold_value))
            {
                g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_CURRENT_SOURCE;
            }
        }

        g_ctsu_diag_info.test_count       += 1;
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
    }
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for SENSCLK Frequency Diagnosis. Called within ctsu_diag_scan_start2().
 **********************************************************************************************************************/
static void ctsu_diag_sensclk_gain_scan_start (void)
{
    /* Pre-configuration for diagnosis */
    R_CTSU->CTSUCALIB_b.CCOCALIB = 1;

    /* Test case configuration */
    ctsu_diag_cco_gain_config_test();
}

/*******************************************************************************************************************//**
 * Retrieves measurement data for SENSCLK Frequency Diagnosis. Called within ctsu_diag_data_get2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_sensclk_gain_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* Initialize local variable */
    uint16_t average_data;
    uint16_t sensclk_gain_diff_value = 0;
    uint16_t upper_threshold_value;
    uint16_t lower_threshold_value;

    /* Get raw count value, accumulate sum and count */
    g_ctsu_diag_info.measurement_sum += (uint16_t) (*(p_instance_ctrl->p_self_raw));
    g_ctsu_diag_info.measurement_count++;

    /* Check if enough samples have been collected for averaging */
    if (g_ctsu_diag_info.measurement_count >= CTSU_DIAG_AVERAGE_NUM)
    {
        /* 1. Calculate the average value */
        average_data = (uint16_t) (g_ctsu_diag_info.measurement_sum >> CTSU_DIAG_AVERAGE_SHIFT_NUM);

        if (0 != g_ctsu_diag_info.test_count)
        {
            /* 2. Calculate thresholds difference value */
            if (average_data > g_ctsu_diag_info.average_data_pre)
            {
                sensclk_gain_diff_value = average_data - g_ctsu_diag_info.average_data_pre;
            }

            /* 3. Calculate thresholds */
            upper_threshold_value = (uint16_t) (
                ((uint32_t) cco_gain_threshold_diff_table[(g_ctsu_diag_info.test_count - 1)][1] *
                 (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_SENSCLK_GAIN_SELF_TEST_MARGIN_PCT)) /
                CTSU_DIAG_PERCENT_BASE);

            lower_threshold_value = (uint16_t) (
                ((uint32_t) cco_gain_threshold_diff_table[(g_ctsu_diag_info.test_count - 1)][0] *
                 (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_SENSCLK_GAIN_SELF_TEST_MARGIN_PCT)) /
                CTSU_DIAG_PERCENT_BASE);

            /* 4. Compare and process result */
            if ((sensclk_gain_diff_value >= upper_threshold_value) ||
                (sensclk_gain_diff_value <= lower_threshold_value))
            {
                /* FAIL: Set diagnostic error */
                g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_SENSCLK_GAIN;
            }

            /* PASS: Check is skipped because the threshold difference value is within the expected range.
             * Condition: (sensclk_gain_diff_value < upper_threshold_value) && (sensclk_gain_diff_value > lower_threshold_value) */
        }

        /* Store the measurement value */
        g_ctsu_diag_info.average_data_pre = average_data;

        /* Increment test cycle count and Reset for the next measurement */
        g_ctsu_diag_info.test_count++;
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
    }

    /* Wait for enough samples, exit function */
}

/*******************************************************************************************************************//**
 * Configures CCODAC current sources for SENSCLK Frequency Diagnosis or SUCLK Frequency Diagnosis (with DWA).
 *
 * This function cumulatively turns on the CCODAC current source units from 1 to 12 to diagnose oscillation stability.
 **********************************************************************************************************************/
static void ctsu_diag_cco_gain_config_test (void)
{
    /* Test case configuration */
    R_CTSU->CTSUCRB_b.SSCNT  = 0;
    R_CTSU->CTSUCRA_b.SDPSEL = 0;

    if (g_ctsu_diag_info.test_count < 8)
    {
        /* TEST1 through TEST8: Configure CCODAC Gain from 1 unit (0x1F) up to 8 units (0xFF)
         * Value = 0x1F | (count << 5) (Value = 0x1F + count * 0x20) */
        R_CTSU->CTSUSUCLKA_b.SUADJ0 =
            (uint8_t) (CTSU_DIAG_CCODAC_GAIN_1_UNIT | (g_ctsu_diag_info.test_count << 5));
    }
    else
    {
        /* Apply CCODAC Gain 8 units (0xFF) for all remaining TEST9 ~ TEST12.
         * The switch only handles auxiliary register configurations (SSCNT|SUCARRY) */
        R_CTSU->CTSUSUCLKA_b.SUADJ0 = CTSU_DIAG_CCODAC_GAIN_8_UNIT;

        switch (g_ctsu_diag_info.test_count)
        {
            case 8:
            {
                /* TEST9: CCODAC Gain 8 units. Add 0x20 to SUADJD by setting SSCNT = 1. */
                R_CTSU->CTSUCRB_b.SSCNT = 1;
                break;
            }

            case 9:
            {
                /* TEST10: CCODAC Gain 8 units. Add 0x40 to SUADJD by setting SSCNT = 2. */
                R_CTSU->CTSUCRB_b.SSCNT = 2;
                break;
            }

            case 10:
            {
                /* TEST11: CCODAC Gain 8 units. Add 0x60 to SUADJD by setting SSCNT = 3. */
                R_CTSU->CTSUCRB_b.SSCNT = 3;
                break;
            }

            case 11:
            {
                /* TEST12: CCODAC Gain 8 units. Add 0x80 to SUADJD (SSCNT=3 for 0x60 + SUCARRY=1 for 0x20). */
                R_CTSU->CTSUCRB_b.SSCNT     = 3;
                R_CTSU->CTSUCALIB_b.SUCARRY = 1;
                break;
            }

            /* All tests are complete. */
            default:
            {
                break;
            }
        }
    }

    R_CTSU->CTSUCRA_b.SDPSEL = 1;
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for SUCLK Frequency Diagnosis. Called within ctsu_diag_scan_start2().
 **********************************************************************************************************************/
static void ctsu_diag_suclk_gain_scan_start (void)
{
    if (11 >= g_ctsu_diag_info.test_count)
    {
        R_CTSU->CTSUCALIB_b.SUMSEL = 0;
        ctsu_diag_cco_gain_config_test();
    }
    else
    {
        R_CTSU->CTSUCALIB_b.SUCARRY = 1;
        R_CTSU->CTSUCALIB_b.SUMSEL  = 1;
        ctsu_diag_suclk_gain_config_test();
    }
}

/*******************************************************************************************************************//**
 * Retrieves measurement data for SUCLK Frequency Diagnosis. Called within ctsu_diag_data_get2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_suclk_gain_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t average_data;
    uint16_t suclk_gain_diff_value = 0;
    uint16_t upper_threshold_value;
    uint16_t lower_threshold_value;

    g_ctsu_diag_info.measurement_sum   += *(p_instance_ctrl->p_self_raw);
    g_ctsu_diag_info.measurement_count += 1;

    if (CTSU_DIAG_AVERAGE_NUM <= g_ctsu_diag_info.measurement_count)
    {
        average_data = (uint16_t) (g_ctsu_diag_info.measurement_sum >> CTSU_DIAG_AVERAGE_SHIFT_NUM);

        if (0 == g_ctsu_diag_info.test_count)
        {
            g_ctsu_diag_info.average_data_pre = average_data;
        }
        else if (g_ctsu_diag_info.test_count <= 11)
        {
            if (average_data > g_ctsu_diag_info.average_data_pre)
            {
                suclk_gain_diff_value = average_data - g_ctsu_diag_info.average_data_pre;
            }

            g_ctsu_diag_info.average_data_pre = average_data;

            upper_threshold_value = (uint16_t) (
                ((uint32_t) cco_gain_threshold_diff_table[(g_ctsu_diag_info.test_count - 1)][1] *
                 (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_SUCLK_GAIN_SELF_TEST_MARGIN_PCT)) /
                CTSU_DIAG_PERCENT_BASE);
            lower_threshold_value = (uint16_t) (
                ((uint32_t) cco_gain_threshold_diff_table[(g_ctsu_diag_info.test_count - 1)][0] *
                 (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_SUCLK_GAIN_SELF_TEST_MARGIN_PCT)) /
                CTSU_DIAG_PERCENT_BASE);

            if ((suclk_gain_diff_value <= lower_threshold_value) ||
                (suclk_gain_diff_value >= upper_threshold_value))
            {
                g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_SUCLK_GAIN;
            }
        }
        else
        {
            upper_threshold_value = (uint16_t) ((CTSU_DIAG_NO_DWA_SUCLK_GAIN_MAX_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_SUCLK_GAIN_SELF_TEST_MARGIN_PCT)) /
                                                CTSU_DIAG_PERCENT_BASE);
            lower_threshold_value = (uint16_t) ((CTSU_DIAG_NO_DWA_SUCLK_GAIN_MIN_VALUE *
                                                 (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_SUCLK_GAIN_SELF_TEST_MARGIN_PCT)) /
                                                CTSU_DIAG_PERCENT_BASE);

            if ((average_data <= lower_threshold_value) ||
                (average_data >= upper_threshold_value))
            {
                g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_SUCLK_GAIN;
            }
        }

        g_ctsu_diag_info.test_count       += 1;
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
    }
}

/*******************************************************************************************************************//**
 * Configures CCODAC current sources for SUCLK Frequency Diagnosis (without DWA).
 *
 * This function individually turns on each CCODAC current source unit to diagnose stuck-at faults.
 **********************************************************************************************************************/
static void ctsu_diag_suclk_gain_config_test (void)
{
    /* TEST13 through TEST18 */
    if (g_ctsu_diag_info.test_count <= 17)
    {
        R_CTSU->CTSUSUCLKA_b.SUADJ0 = CTSU_DIAG_SUCLK_LOWER_CURRENT_SELECT |
                                      (1 << (g_ctsu_diag_info.test_count - 12));
        R_CTSU->CTSUSUCLKA_b.SUADJ0 = CTSU_DIAG_SUCLK_UPPER_CURRENT_SELECT;
    }
    /* TEST19 through TEST24 */
    else
    {
        R_CTSU->CTSUSUCLKA_b.SUADJ0 = CTSU_DIAG_SUCLK_LOWER_CURRENT_SELECT;
        R_CTSU->CTSUSUCLKA_b.SUADJ0 = CTSU_DIAG_SUCLK_UPPER_CURRENT_SELECT |
                                      (1 << (g_ctsu_diag_info.test_count - 18));
    }
}

/*******************************************************************************************************************//**
 * Initiates the measurement start process for SUCLK Clock Recovery Diagnosis. Called within ctsu_diag_scan_start2().
 **********************************************************************************************************************/
static void ctsu_diag_clock_recovery_scan_start (void)
{
    uint8_t sumulti  = 0;
    uint8_t freq_mhz = 0;

    R_CTSU->CTSUCALIB_b.CCOCLK = 1;
    R_CTSU->CTSUCRB_b.SST      = CTSU_DIAG_CLOCK_RECOVERY_SST_RECOMMEND;
    R_CTSU->CTSUCRA_b.SDPSEL   = 0;

    switch (g_ctsu_diag_info.test_count)
    {
        case 0:
        {
            R_CTSU->CTSUCRB_b.SSCNT = 0;
            sumulti                 = CTSU_DIAG_CLOCK_RECOVERY_TEST1_FREQUENCY;
            freq_mhz                = CTSU_DIAG_CLOCK_RECOVERY_TEST1_FREQUENCY_MHZ;
            break;
        }

        case 1:
        {
            R_CTSU->CTSUCRB_b.SSCNT = 0;
            sumulti                 = CTSU_DIAG_CLOCK_RECOVERY_TEST2_FREQUENCY;
            freq_mhz                = CTSU_DIAG_CLOCK_RECOVERY_TEST2_FREQUENCY_MHZ;
            break;
        }

        case 2:
        {
            R_CTSU->CTSUCRB_b.SSCNT = 0;
            sumulti                 = CTSU_DIAG_CLOCK_RECOVERY_TEST3_FREQUENCY;
            freq_mhz                = CTSU_DIAG_CLOCK_RECOVERY_TEST3_FREQUENCY_MHZ;
            break;
        }

  #if (CTSU_DIAG_CLOCK_RECOVERY_TEST_NUM == 4)
        case 3:
        {
            R_CTSU->CTSUCRB_b.SSCNT = 1;
            sumulti                 = CTSU_DIAG_CLOCK_RECOVERY_TEST4_FREQUENCY;
            freq_mhz                = CTSU_DIAG_CLOCK_RECOVERY_TEST4_FREQUENCY_MHZ;
            break;
        }
  #endif

        default:
        {
            break;
        }
    }

    R_CTSU->CTSUSUCLKA_b.SUMULTI0 = sumulti;
    R_CTSU->CTSUSUCLKA_b.SUADJ0   = (uint8_t) (((R_CTSUTRIM->CTSUTRIMA_b.SUADJD * freq_mhz) >>
                                                CTSU_DIAG_CLOCK_RECOVERY_FREQUENCY_32MHZ_SHIFT) -
                                               (CTSU_SUADJ_SSCNT_ADJ * R_CTSU->CTSUCRB_b.SSCNT));

    R_CTSU->CTSUCRA_b.SDPSEL = 1;
}

/*******************************************************************************************************************//**
 * Retrieves measurement data for SUCLK Clock Recovery Diagnosis. Called within ctsu_diag_data_get2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_clock_recovery_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    uint16_t average_data;
    uint16_t upper_threshold_value;
    uint16_t lower_threshold_value;

    g_ctsu_diag_info.measurement_sum   += *(p_instance_ctrl->p_self_raw);
    g_ctsu_diag_info.measurement_count += 1;

    if (CTSU_DIAG_AVERAGE_NUM <= g_ctsu_diag_info.measurement_count)
    {
        average_data = (uint16_t) (g_ctsu_diag_info.measurement_sum >> CTSU_DIAG_AVERAGE_SHIFT_NUM);

        upper_threshold_value = (uint16_t) (((uint32_t) clock_recovery_threshold_table[g_ctsu_diag_info.test_count][1] *
                                             (CTSU_DIAG_PERCENT_BASE + CTSU_DIAG_CLOCK_RECOVERY_SELF_TEST_MARGIN_PCT)) /
                                            CTSU_DIAG_PERCENT_BASE);
        lower_threshold_value = (uint16_t) (((uint32_t) clock_recovery_threshold_table[g_ctsu_diag_info.test_count][0] *
                                             (CTSU_DIAG_PERCENT_BASE - CTSU_DIAG_CLOCK_RECOVERY_SELF_TEST_MARGIN_PCT)) /
                                            CTSU_DIAG_PERCENT_BASE);

        if ((average_data <= lower_threshold_value) ||
            (average_data >= upper_threshold_value))
        {
            g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_CLOCK_RECOVERY;
        }

        g_ctsu_diag_info.test_count       += 1;
        g_ctsu_diag_info.measurement_count = 0;
        g_ctsu_diag_info.measurement_sum   = 0;
    }
}

  #if (CTSU_CFG_NUM_CFC != 0)

/*******************************************************************************************************************//**
 * Initiates the measurement start process for CFC Oscillator Gain Diagnosis. Called within ctsu_diag_scan_start2().
 **********************************************************************************************************************/
static void ctsu_diag_cfc_gain_scan_start (void)
{
    /* Pre-configuration for diagnosis */
    R_CTSU->CTSUCRA_b.MD2       = 1;
    R_CTSU->CTSUCRA_b.CFCON     = 1;
    R_CTSU->CTSUCRA_b.TXVSEL    = 1;
    R_CTSU->CTSUCRB_b.SSCNT     = 0;
    R_CTSU->CTSUCALIB_b.CFCMODE = 1;
    R_CTSU->CTSUCALIB_b.CFCRDMD = 1;

    /* Test case configuration */
    R_CTSU->CTSUSUCLK0 =
        (uint16_t) (((g_ctsu_diag_info.test_count + CTSU_CORRCFC_CENTER_POINT) * CTSU_CORRECTION_SUMULTI) - 1);

    if (CTSU_DIAG_CHACA_TSMAX > g_ctsu_corrcfc_info.ts_table[0])
    {
        R_CTSU->CTSUCHACA = (uint32_t) (1 << g_ctsu_corrcfc_info.ts_table[0]);
        R_CTSU->CTSUCHACB = 0;
    }
    else
    {
        R_CTSU->CTSUCHACA = 0;
        R_CTSU->CTSUCHACB = (uint32_t) (1 << (g_ctsu_corrcfc_info.ts_table[0] - CTSU_DIAG_CHACA_TSMAX));
    }

    R_CTSU->CTSUCHTRCA = 0;
    R_CTSU->CTSUCHTRCB = 0;
}

/*******************************************************************************************************************//**
 * Retrieves measurement data for CFC Oscillator Gain Diagnosis. Called within ctsu_diag_data_get2().
 *
 * @param[in]  p_instance_ctrl             Pointer to the control structure.
 **********************************************************************************************************************/
static void ctsu_diag_cfc_gain_data_get (ctsu_instance_ctrl_t * const p_instance_ctrl)
{
    /* This diagnostic uses a single measurement for evaluation,
     * so average_data_pre contains one-shot data, not averaged values */

    /* Initialize local variable */
    uint16_t cfc_gain_value;

    /* 1. Get raw count value */
    cfc_gain_value = (uint16_t) (*(p_instance_ctrl->p_self_raw));

    /* 2. Compare and process result */
    if (0 != g_ctsu_diag_info.test_count)
    {
        /* CFC gain diagnostic result check */
        if (cfc_gain_value <= g_ctsu_diag_info.average_data_pre)
        {
            /* FAIL: Set diagnostic error */
            g_ctsu_diag_info.test_result = FSP_ERR_CTSU_DIAG_CFC_GAIN;
        }

        /* PASS: Check is skipped because the measured value is the expected range.
         * Condition: (cfc_gain_value > g_ctsu_diag_info.average_data_pre) */
    }

    /* Store the measurement value */
    g_ctsu_diag_info.average_data_pre = cfc_gain_value;

    /* Increment test count */
    g_ctsu_diag_info.test_count++;
}

  #endif
 #endif
#endif
