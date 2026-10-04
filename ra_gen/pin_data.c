/* generated pin source file - do not edit */
#include "bsp_api.h"
#include "r_ioport.h"

const ioport_pin_cfg_t g_bsp_pin_cfg_data[] =
        {

        /* 机械按键(阶段2): GPIO 输入上拉, 配法参考 RA4M2GPIOINPUT demo。
         * ⚠ 这几行原本**不存在** —— 未在 FSP 里配置时 R_BSP_PinRead 读到的
         *   是未定义电平, 按键永远不触发(2026-10-04 实测 4 键全部无反应)。 */
        { .pin = BSP_IO_PORT_00_PIN_05, .pin_cfg = ((uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT) },

          { .pin = BSP_IO_PORT_00_PIN_06, .pin_cfg = ((uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT) },

          /* CTSU 触摸(阶段3): TSCAP 公共电容 + TS11/TS12 两个自容式通道,
         * 配法参考 RA4M2TOUCH demo。⚠ 触摸不是 GPIO, 读电平无效,
         *   必须由 r_ctsu + rm_touch 扫描, 阶段3 接入。 */
          { .pin = BSP_IO_PORT_02_PIN_07, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_CTSU) },

          { .pin = BSP_IO_PORT_04_PIN_15, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_CTSU) },

          { .pin = BSP_IO_PORT_07_PIN_08, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_CTSU) },

          { .pin = BSP_IO_PORT_01_PIN_08, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_DEBUG) },

          { .pin = BSP_IO_PORT_01_PIN_09, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_SCI1_3_5_7_9) },

          { .pin = BSP_IO_PORT_01_PIN_10, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_SCI1_3_5_7_9) },

          { .pin = BSP_IO_PORT_03_PIN_00, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_DEBUG) },

          { .pin = BSP_IO_PORT_04_PIN_10, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_SCI0_2_4_6_8) },

          { .pin = BSP_IO_PORT_04_PIN_11, .pin_cfg = ((uint32_t) IOPORT_CFG_PERIPHERAL_PIN
                  | (uint32_t) IOPORT_PERIPHERAL_SCI0_2_4_6_8) }, };

const ioport_cfg_t g_bsp_pin_cfg =
{ .number_of_pins = sizeof(g_bsp_pin_cfg_data) / sizeof(ioport_pin_cfg_t), .p_pin_cfg_data = &g_bsp_pin_cfg_data[0], };

#if BSP_TZ_SECURE_BUILD

void R_BSP_PinCfgSecurityInit(void);

/* Initialize SAR registers for secure pins. */
void R_BSP_PinCfgSecurityInit(void)
{
 #if (2U == BSP_FEATURE_IOPORT_VERSION)
    uint32_t pmsar[BSP_FEATURE_BSP_NUM_PMSAR];
 #else
    uint16_t pmsar[BSP_FEATURE_BSP_NUM_PMSAR];
 #endif
    memset(pmsar, 0xFF, BSP_FEATURE_BSP_NUM_PMSAR * sizeof(R_PMISC->PMSAR[0]));


    for(uint32_t i = 0; i < g_bsp_pin_cfg.number_of_pins; i++)
    {
        uint32_t port_pin = g_bsp_pin_cfg.p_pin_cfg_data[i].pin;
        uint32_t port = port_pin >> 8U;
        uint32_t pin = port_pin & 0xFFU;
        pmsar[port] &= (uint16_t) ~(1U << pin);
    }

    for(uint32_t i = 0; i < BSP_FEATURE_BSP_NUM_PMSAR; i++)
    {
 #if (2U == BSP_FEATURE_IOPORT_VERSION)
    #if BSP_SECONDARY_CORE_BUILD
        R_PMISC->PMSAR[i].PMSAR &= (uint16_t) pmsar[i];
    #else
        R_PMISC->PMSAR[i].PMSAR = (uint16_t) pmsar[i];
    #endif
 #else
        R_PMISC->PMSAR[i].PMSAR = pmsar[i];
 #endif
    }

}
#endif
