/**
 * @file  oled_port.h
 * @brief SSD1306 OLED 硬件接口层 (GPIO 软SPI + 引脚回调注入)
 */
#ifndef _OLED_PORT_H_
#define _OLED_PORT_H_

void oled_port_init(void);   /* 引脚初始化 + OLED_Init */

#endif /* _OLED_PORT_H_ */
