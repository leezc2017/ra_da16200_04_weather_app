/**
 * @file  bsp_hal/hal_delay.h
 * @brief 忙等延时接口, 实现见 bsp_hal/hal_delay.c (封装 FSP 的 R_BSP_SoftwareDelay)
 *        oled.c / gpio_spi.c 原样移植, 仅依赖此头
 */
#ifndef _BSP_HAL_DELAY_H_
#define _BSP_HAL_DELAY_H_

#include <stdint.h>

void hal_delay_ms(uint32_t ms);
void hal_delay_us(uint32_t us);

#endif /* _BSP_HAL_DELAY_H_ */
