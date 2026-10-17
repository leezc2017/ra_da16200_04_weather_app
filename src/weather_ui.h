/**
 * @file  weather_ui.h
 * @brief OLED UI layer (128x32), ported from GD32 reference @ fd56536.
 *
 * Layout baseline = GD32 newest version ("Chinese + city", 2026-09-16), with
 * the user-reviewed 2026-10-17 bottom-row rework:
 *   - main page row0 (16px): city name (ASCII CamelCase map, e.g. ChiFeng)
 *     + Chinese weather glyph(s) from the Seniverse code + temperature with
 *     a bitmap degree sign;
 *   - rows2/3 (one 16px row): enlarged "风速 xx  湿度 xx" (16px CN glyphs,
 *     no units); the TODAY row was removed at user request (it repeated the
 *     temperature info);
 *   - all status pages (provisioning / connecting / fetching / failed)
 *     render Chinese 16px glyphs from oled/weather_font.h;
 *   - WiFi info page = four 6x8 rows ("WiFi INFO"/SSID:/IP:/STATUS:).
 *
 * Deliberate RA deviations from the GD32 file (do not "fix" them away):
 *   1) the serial debug/[oled] lines are ASCII (RA console convention avoids
 *      GBK garbling on the SCI9 terminal);
 *   2) the provisioning portal address shows "192.168.1.100:8080" (port 80
 *      is occupied by the DA16200 LWIP page, the portal lives on 8080);
 *   3) GD32 bug fixes carried over (2026-10-17 review):
 *      a. provisioning page: the 16px title stays on pages 0-1 and the info
 *         rows moved to pages 2-3 -- GD32 wrote SSID at page 1, where the
 *         6x8 row erased the bottom half of the 16px glyphs;
 *      b. provisioning/connecting/failed pages show the SSID being
 *         connected instead of a fixed SoftAP/SSID row;
 *      c. the displayed city comes from the API response (location.name;
 *         priority parsed > configured > "IP") so IP-geolocation mode also
 *         shows the real city;
 *      d. bottom row is ONE enlarged 16px line "风速 xx  湿度 xx" (16px CN
 *         glyphs, generated from SimSun; no units, no TODAY row -- user
 *         decision 2026-10-17; wind + humidity already fill 16px width, so
 *         today hi/lo does not fit and was dropped);
 *      e. weather_task merges missing optional fields from the previous
 *         cycle and retries daily.json quickly (WEATHER_DAILY_RETRY), so
 *         hi/lo and wind no longer blank out until the next full refresh;
 *         the parsed value is still printed on serial ([oled] today(hid)).
 *         Connect-phase SSID additionally self-heals: the +WFJAP:1 URC's
 *         real SSID is parsed back into the state (parse_ssid).
 *   4) the blink "*" marker (DESIGN.md section 8.1 liveness) is kept on the
 *      provisioning page, in the free right half of the 16px title row;
 *   5) weather_activity_t is derived inside weather_task from its own state
 *      (wx_valid / http busy / connected) instead of a dedicated state
 *      machine: while data is already on screen a periodic re-fetch keeps
 *      rendering the data instead of blanking into the FETCHING page;
 *   6) ui_main_view_t keeps the RA-only fields (time_str/ssid/low_str/
 *      high_str/provisioning) filled by weather_task; ssid now also feeds
 *      the provisioning/connecting/failed pages.
 *
 * wifi_status_t comes from wifi_status.h (platform-neutral), matching GD32's
 * wifi_prov.h values 1:1 (§8.1 UI/state consistency rule).
 */
#ifndef _WEATHER_UI_H_
#define _WEATHER_UI_H_

#include <stdint.h>
#include "wifi_status.h"

typedef enum {
    WEATHER_ACT_IDLE = 0,
    WEATHER_ACT_PROVISIONING,
    WEATHER_ACT_WIFI_CONNECTING,
    WEATHER_ACT_WIFI_CONNECTED,
    WEATHER_ACT_FETCHING,
    WEATHER_ACT_SUCCESS,
    WEATHER_ACT_FAILED,
} weather_activity_t;

/* Main page view data */
typedef struct {
    char    time_str[6];     /* "12:34" or "--:--" (RA fill, not rendered) */
    char    temp_str[8];     /* "26", UI paints the degree bitmap next to it */
    char    feels_like_str[12];
    char    humidity_str[12];
    char    wind_str[12];
    char    range_str[24];   /* today's "low/high" (UI: TODAY:%s + degree) */
    char    wtext[16];       /* ASCII weather text (serial debug line only) */
    char    city[33];        /* parsed/configured city name */
    char    ssid[33];        /* current SSID (RA fill, not rendered) */
    char    low_str[8];      /* low_str/high_str kept alongside range_str */
    char    high_str[8];
    int     weather_code;    /* Seniverse code 0~38, selects CN glyphs; -1=none */
    uint8_t icon;            /* weather_icon_t, 0xFF=none (not rendered now) */
    uint8_t offline;         /* 1=data stale */
    uint8_t provisioning;    /* RA sub-state 0~3 (kept for debug) */
    weather_activity_t activity;
} ui_main_view_t;

/* WiFi info page view data */
typedef struct {
    const char *ssid;
    const char *ip;
    uint8_t     fails;
    uint8_t     page;       /* page index (extension) */
} ui_wifi_view_t;

void ui_init(void);
void ui_main_page(const ui_main_view_t *v, wifi_status_t st);
void ui_wifi_page(const ui_wifi_view_t *v, wifi_status_t st);
void ui_boot_splash(void);

#endif /* _WEATHER_UI_H_ */
