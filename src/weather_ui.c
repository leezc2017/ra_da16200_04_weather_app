/**
 * @file  weather_ui.c
 * @brief GD32 weather layout rendered on the OLED or as four serial rows.
 */
#include <stdio.h>
#include <string.h>

#include "weather_cfg.h"
#include "weather_ui.h"

#if !WEATHER_UI_SERIAL_SIM
#include "oled/oled.h"
#endif

/* 128 pixels / 6 pixels per 8px glyph = 21 visible ASCII characters. */
static void screen_show(const char *page, char rows[4][22])
{
#if WEATHER_UI_SERIAL_SIM
    printf("[OLED] PAGE=%s 128x32\r\n", page);
    for (unsigned i = 0; i < 4U; i++) {
        printf("[OLED] L%u=%s\r\n", i, rows[i]);
    }
#else
    (void)page;
    OLED_Clear();
    for (unsigned i = 0; i < 4U; i++) {
        OLED_ShowString(0, (u8)i, (u8 *)rows[i], 8);
    }
#endif
}

void ui_init(void)
{
#if WEATHER_UI_SERIAL_SIM
    printf("[OLED] serial simulator ready (128x32, 4 rows)\r\n");
#else
    OLED_Clear();
#endif
}

void ui_boot_splash(void)
{
    char rows[4][22] = {{0}};

    snprintf(rows[0], sizeof(rows[0]), "Lee Weather");
    snprintf(rows[2], sizeof(rows[2]), "Weather App");
    screen_show("SPLASH", rows);
}

void ui_main_page(const ui_main_view_t *v, wifi_status_t st)
{
    char rows[4][22] = {{0}};

    if (v == NULL) {
        return;
    }

    switch (st) {
    case WIFI_STAT_PROVISIONING:
        snprintf(rows[0], sizeof(rows[0]), "WiFi provisioning");
        snprintf(rows[1], sizeof(rows[1]), "AP configuration");
        snprintf(rows[2], sizeof(rows[2]), "Waiting for setup");
        break;

    case WIFI_STAT_CONNECTING:
        snprintf(rows[0], sizeof(rows[0]), "WiFi connecting");
        snprintf(rows[1], sizeof(rows[1]), "SSID:%.15s", WEATHER_DEFAULT_SSID);
        snprintf(rows[2], sizeof(rows[2]), "Please wait...");
        break;

    case WIFI_STAT_FAIL:
        snprintf(rows[0], sizeof(rows[0]), "WiFi offline");
        snprintf(rows[1], sizeof(rows[1]), "SSID:%.15s", WEATHER_DEFAULT_SSID);
        snprintf(rows[2], sizeof(rows[2]), "Check connection");
        break;

    case WIFI_STAT_CONNECTED:
    case WIFI_STAT_IDLE:
    default:
        if (v->temp_str[0] != '\0') {
            snprintf(rows[0], sizeof(rows[0]), "%.2s %.14s %s",
                     v->city[0] ? v->city : "IP",
                     v->wtext[0] ? v->wtext : "--",
                     v->temp_str);
        } else {
            snprintf(rows[0], sizeof(rows[0]), "%.2s TEMP:--",
                     v->city[0] ? v->city : "IP");
            snprintf(rows[1], sizeof(rows[1]), "%.21s",
                     v->wtext[0] ? v->wtext : "Waiting for data");
        }

        if (v->humidity_str[0] || v->wind_str[0]) {
            snprintf(rows[1], sizeof(rows[1]), "HUM:%s%% WIND:%skm/h",
                     v->humidity_str[0] ? v->humidity_str : "--",
                     v->wind_str[0] ? v->wind_str : "--");
        } else if (rows[1][0] == '\0') {
            snprintf(rows[1], sizeof(rows[1]), "HUM:-- WIND:--");
        }

        if (v->low_str[0] || v->high_str[0]) {
            snprintf(rows[2], sizeof(rows[2]), "TODAY:%s/%sC",
                     v->low_str[0] ? v->low_str : "--",
                     v->high_str[0] ? v->high_str : "--");
        } else if (rows[2][0] == '\0') {
            snprintf(rows[2], sizeof(rows[2]), "TODAY:--/--C");
        }
        snprintf(rows[3], sizeof(rows[3]), "TIME:%.5s",
                 v->time_str[0] ? v->time_str : "--:--");
        if (v->offline) {
            size_t n = strlen(rows[0]);
            if (n < sizeof(rows[0]) - 1U) {
                rows[0][n] = '!';
                rows[0][n + 1U] = '\0';
            }
        }
        break;
    }

    screen_show("MAIN", rows);
}

void ui_wifi_page(const ui_wifi_view_t *v, wifi_status_t st)
{
    char rows[4][22] = {{0}};
    const char *status;

    if (v == NULL) {
        return;
    }

    switch (st) {
    case WIFI_STAT_PROVISIONING: status = "PROVISIONING"; break;
    case WIFI_STAT_CONNECTING:   status = "CONNECTING";   break;
    case WIFI_STAT_FAIL:         status = "FAILED";       break;
    case WIFI_STAT_CONNECTED:    status = "CONNECTED";    break;
    default:                     status = "IDLE";          break;
    }

    snprintf(rows[0], sizeof(rows[0]), "WiFi INFO");
    snprintf(rows[1], sizeof(rows[1]), "SSID:%.15s",
             v->ssid ? v->ssid : WEATHER_DEFAULT_SSID);
    snprintf(rows[2], sizeof(rows[2]), "IP:%.14s",
             (v->ip && v->ip[0]) ? v->ip : "0.0.0.0");
    snprintf(rows[3], sizeof(rows[3]), "STATUS:%.12s", status);
    screen_show("WIFI", rows);
}
