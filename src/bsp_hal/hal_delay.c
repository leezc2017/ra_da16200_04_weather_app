/**
 * @file  bsp_hal/hal_delay.c
 * @brief 忙等延时实现, 封装 FSP 的 R_BSP_SoftwareDelay()
 */
#include <stdint.h>

#include "hal_data.h"
#include "bsp_hal/hal_delay.h"

void hal_delay_us(uint32_t us)
{
    R_BSP_SoftwareDelay(us, BSP_DELAY_UNITS_MICROSECONDS);
}

void hal_delay_ms(uint32_t ms)
{
    R_BSP_SoftwareDelay(ms, BSP_DELAY_UNITS_MILLISECONDS);
}
