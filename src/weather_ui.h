/**
 * @file  weather_ui.h
 * @brief OLED 界面绘制 (128x32, 两页16px)
 *
 * 原样移植自 GD32VW553 版 4.weather_app/src/weather_ui.h，布局与显示内容
 * 与 GD32 版完全一致（4 个页面：主页 / WiFi信息页 / 配网页 / 启动画面）。
 *
 * 唯一改动：wifi_status_t 的来源从 wifi_prov.h 改为 wifi_status.h。
 * GD32 的 wifi_prov.c 是片上 WiFi 实现，RA 工程用 AT 引擎驱动 DA16200，
 * 不能共用一个头文件，故把该枚举抽到平台无关的 wifi_status.h。
 *
 * ⚠ OLED 坐标约定（oled.c OLED_Set_Pos 里是 0xb0+y）：
 *   x    = 列 0~127
 *   y    = **页号** 0~3（每页 8 像素），不是像素纵坐标
 *   sizey= 8 -> 6px 宽字体，占 1 页，每字符 x+=6
 *   sizey=16 -> 8px 宽字体，占 2 页，每字符 x+=8
 *   其它值（如 12）OLED_ShowChar 会直接 return，不显示任何内容
 */
#ifndef _WEATHER_UI_H_
#define _WEATHER_UI_H_

#include <stdint.h>
#include "wifi_status.h"

/* 主页视图数据；串口模拟和实体 OLED 共用同一份内容。 */
typedef struct {
    char    time_str[6];    /* "12:34" 或 "--:--" */
    char    city[16];       /* 当前固定配置为 "IP" */
    char    ssid[33];       /* 当前生效/待连接的 Wi-Fi SSID */
    char    temp_str[8];    /* "26C" */
    char    wtext[16];      /* ASCII 天气文案 */
    char    humidity_str[8];
    char    wind_str[8];
    char    low_str[8];
    char    high_str[8];
    uint8_t icon;           /* weather_icon_t, 0xFF=无图标 */
    uint8_t offline;        /* 1=数据过期(掉线) */
    uint8_t provisioning;    /* 0=AP 启动中, 1=AP 可用, 2=切回 STA, 3=启动 STA 收敛 */
} ui_main_view_t;

/* WiFi信息页视图数据 */
typedef struct {
    const char *ssid;
    const char *ip;
    uint8_t     fails;
    uint8_t     page;       /* 翻页序号(扩展用) */
} ui_wifi_view_t;

void ui_init(void);
void ui_main_page(const ui_main_view_t *v, wifi_status_t st);
void ui_wifi_page(const ui_wifi_view_t *v, wifi_status_t st);
void ui_boot_splash(void);

#endif /* _WEATHER_UI_H_ */
