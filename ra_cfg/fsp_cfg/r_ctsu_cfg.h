/* generated configuration header file - do not edit */
#ifndef R_CTSU_CFG_H_
#define R_CTSU_CFG_H_
#ifdef __cplusplus
                 extern "C" {
                 #endif

#define CTSU_CFG_PARAM_CHECKING_ENABLE            (BSP_CFG_PARAM_CHECKING_ENABLE)
#define CTSU_CFG_DTC_SUPPORT_ENABLE               (0)
#define CTSU_CFG_INT_PRIORITY_LEVEL               (12)
#define CTSU_CFG_AUTO_JUDGE_ENABLE                (0)
#ifndef QE_TOUCH_CONFIGURATION
#define CTSU_CFG_NUM_SELF_ELEMENTS                (2)
#define CTSU_CFG_NUM_MUTUAL_ELEMENTS              (0)
#if (CTSU_CFG_AUTO_JUDGE_ENABLE == 1)
                 #define CTSU_CFG_MAJORITY_MODE                       (2)
                 #define CTSU_CFG_NUM_AUTOJUDGE_SELF_ELEMENTS         (1)
                 #define CTSU_CFG_NUM_AUTOJUDGE_MUTUAL_ELEMENTS       (0)
                 #define CTSU_CFG_AUTO_CORRECTION_ENABLE              (1)
                 #else
#define CTSU_CFG_MAJORITY_MODE                       (1)
#define CTSU_CFG_NUM_AUTOJUDGE_SELF_ELEMENTS         (0)
#define CTSU_CFG_NUM_AUTOJUDGE_MUTUAL_ELEMENTS       (0)
#define CTSU_CFG_AUTO_CORRECTION_ENABLE              (0)
#endif
#define CTSU_CFG_LOW_VOLTAGE_MODE                 (0)
#define CTSU_CFG_PCLK_DIVISION                    (1)
#define CTSU_CFG_TSCAP_PORT                       (0x0207)
#define CTSU_CFG_VCC_MV                           (3300)
#define CTSU_CFG_DIAG_SUPPORT_ENABLE              (0)
#if (BSP_FEATURE_CTSU_VERSION == 2)
                 #define CTSU_CFG_NUM_CFC                          (0)
                 #define CTSU_CFG_NUM_CFC_TX                       (0)
                 #define CTSU_CFG_NUM_SUMULTI                      (3)
                 #define CTSU_CFG_SUMULTI0                         (0x3F)
                 #define CTSU_CFG_SUMULTI1                         (0x36)
                 #define CTSU_CFG_SUMULTI2                         (0x48)
                 #define CTSU_CFG_TEMP_CORRECTION_SUPPORT          (0)
                 #define CTSU_CFG_TEMP_CORRECTION_TS               (0)
                 #define CTSU_CFG_TEMP_CORRECTION_TIME             (0)
                 #define CTSU_CFG_CALIB_RTRIM_SUPPORT              (0)
                 #endif
#if (BSP_FEATURE_CTSU_VERSION == 1)
                 #define CTSU_CFG_NUM_SUMULTI                      (1)
                 #endif
#endif

#ifdef __cplusplus
                 }
                 #endif
#endif /* R_CTSU_CFG_H_ */
