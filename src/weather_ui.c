/**
 * @file  weather_ui.c
 * @brief OLED 界面: 主页(时间/温度/天气/状态) + WiFi信息页 + 16x16图标
 *
 * 128x32 = 4个页(page), page0-1 为第一行(16px), page2-3 为第二行。
 */
#include <string.h>
#include <stdio.h>

#include "oled/oled.h"
#include "weather_ui.h"
#include "weather_text.h"

/* ---------------- 16x16 图标 (2页x16字节, LSB=上) ---------------- */
static const uint8_t BMP_SUNNY[32] = {
    0x00,0x00,0x00,0x00,0xF0,0xFD,0xFC,0xFC,0xFC,0xFC,0xFC,0xF0,0x00,0x00,0x00,0x00,
    0x00,0x0C,0x00,0x00,0x0F,0x3F,0x3F,0x3F,0xFF,0x3F,0x3F,0x0F,0x00,0x00,0x0C,0x00,
};
static const uint8_t BMP_CLOUDY[32] = {
    0x00,0x00,0x00,0x00,0xC0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xC0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x00,
};
static const uint8_t BMP_RAIN[32] = {
    0x00,0x00,0x00,0x00,0xC0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xC0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0F,0x0F,0x1F,0x0F,0x0F,0x5F,0x0F,0x0F,0x0F,0x0F,0x1F,0x0F,0x00,0x00,
};
static const uint8_t BMP_SNOW[32] = {
    0x00,0x00,0x00,0x00,0xC0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xC0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0F,0x0F,0x9F,0x0F,0x0F,0x6F,0x0F,0x0F,0x0F,0x0F,0x9F,0x0F,0x00,0x00,
};
static const uint8_t BMP_FOG[32] = {
    0x00,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x00,
    0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x00,
};
static const uint8_t BMP_THUNDER[32] = {
    0x00,0x00,0x00,0x00,0xC0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xC0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0F,0x0F,0x0F,0x0F,0x3C,0x3C,0xF0,0xF0,0x0F,0x0F,0x0F,0x0F,0x00,0x00,
};

static const uint8_t *s_icons[ICON_MAX] = {
    BMP_SUNNY, BMP_CLOUDY, BMP_RAIN, BMP_SNOW, BMP_FOG, BMP_THUNDER,
};

static uint8_t s_blink;     /* 配网闪烁计数 */

void ui_init(void)
{
    OLED_Clear();
}

void ui_boot_splash(void)
{
    OLED_Clear();
    OLED_ShowString(16, 0, (u8 *)"Lee Weather", 16);
    OLED_ShowString(8, 2, (u8 *)"Weather App", 16);
}

/* ---------------- 主页 ---------------- */
void ui_main_page(const ui_main_view_t *v, wifi_status_t st)
{
    char line[20];

    OLED_Clear();

    switch (st) {
    case WIFI_STAT_PROVISIONING:
        /* 闪烁提示 */
        s_blink++;
        OLED_ShowString(0, 0, (u8 *)"WiFi Config", 8);
        if (s_blink & 1) {
            OLED_ShowString(88, 0, (u8 *)"*", 8);
        }
        OLED_ShowString(0, 1, (u8 *)"SSID:leeWethWifi", 8);
        OLED_ShowString(0, 2, (u8 *)"PASS:12345678", 8);
        OLED_ShowString(0, 3, (u8 *)"Open 192.168.1.100", 8);
        return;

    case WIFI_STAT_CONNECTING:
        OLED_ShowString(0, 0, (u8 *)"WiFi connecting", 16);
        OLED_ShowString(0, 2, (u8 *)"please wait..", 16);
        return;

    case WIFI_STAT_FAIL:
        OLED_ShowString(0, 0, (u8 *)"offline", 16);
        OLED_ShowString(0, 2, (u8 *)"LongPress=cfg", 16);
        return;

    case WIFI_STAT_CONNECTED:
    case WIFI_STAT_IDLE:
    default:
        break;
    }

    /* 第一行: 时间 + 温度 */
    snprintf(line, sizeof(line), "%s %s", v->time_str, v->temp_str);
    OLED_ShowString(0, 0, (u8 *)line, 16);
    if (v->icon < ICON_MAX) {
        OLED_DrawBMP(112, 0, 16, 16, (u8 *)s_icons[v->icon]);
    }

    /* 第二行: 天气文案(+掉线标记) */
    snprintf(line, sizeof(line), "%s%s", v->offline ? "!" : "", v->wtext);
    OLED_ShowString(0, 2, (u8 *)line, 16);
}

/* ---------------- WiFi 信息页 (6x8 小字, 4行) ---------------- */
void ui_wifi_page(const ui_wifi_view_t *v, wifi_status_t st)
{
    char line[24];

    OLED_Clear();
    OLED_ShowString(0, 0, (u8 *)"WiFi info", 8);

    snprintf(line, sizeof(line), "S:%.18s", v->ssid);
    OLED_ShowString(0, 1, (u8 *)line, 8);

    snprintf(line, sizeof(line), "IP:%.14s", (v->ip && v->ip[0]) ? v->ip : "0.0.0.0");
    OLED_ShowString(0, 2, (u8 *)line, 8);

    switch (st) {
    case WIFI_STAT_PROVISIONING: snprintf(line, sizeof(line), "cfg mode (AP on)"); break;
    case WIFI_STAT_CONNECTING:   snprintf(line, sizeof(line), "connecting...");    break;
    case WIFI_STAT_FAIL:         snprintf(line, sizeof(line), "fail x%u", v->fails); break;
    case WIFI_STAT_CONNECTED:    snprintf(line, sizeof(line), "connected");        break;
    default:                     snprintf(line, sizeof(line), "idle");             break;
    }
    OLED_ShowString(0, 3, (u8 *)line, 8);
}
