/**
 * @file  weather_ui.c
 * @brief 128x32 OLED UI, ported from the GD32 reference worktree
 *        (GD32VW553 4.weather_app @ fd56536, "Chinese + city" version).
 *
 * Main page row0 (16px): city name (CamelCase API city) + Chinese weather
 * glyphs selected by the Seniverse code + temperature + bitmap degree sign.
 * Rows2/3 (one 16px row): enlarged "风�?xx 湿度 xx" (16px CN glyphs, no
 * units; the today hi/lo row was removed per user review). Status pages use
 * Chinese 16px glyphs; provisioning/connecting/failed pages show the SSID
 * being connected plus the configured city. See weather_ui.h for the
 * documented deviations (ASCII debug lines, portal :8080, blink).
 */
#include <stdio.h>
#include <string.h>

#include "oled/oled.h"
#include "oled/weather_font.h"
#include "weather_ui.h"

static const uint8_t BMP_DEGREE[8] = {
    0x06, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00, 0x00,
};

static uint8_t s_blink;

typedef struct {
    const char *input;
    const char *display;
} city_display_map_t;

static const city_display_map_t s_city_display_map[] = {
    {"chifeng",   "ChiFeng"},
    {"beijing",   "BeiJing"},
    {"baoding",   "BaoDing"},
    {"shanghai",  "ShangHai"},
    {"guangzhou", "GuangZhou"},
    {"shenzhen",  "ShenZhen"},
};

static int ascii_equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void city_display_name(const char *city, char *out, size_t out_size)
{
    size_t i;
    int word_start = 1;

    if (out_size == 0) {
        return;
    }
    if (city == NULL || city[0] == '\0' ||
        (city[0] == 'i' && city[1] == 'p' && city[2] == '\0') ||
        (city[0] == 'I' && city[1] == 'P' && city[2] == '\0')) {
        snprintf(out, out_size, "IP");
        return;
    }
    for (i = 0; i < sizeof(s_city_display_map) / sizeof(s_city_display_map[0]); i++) {
        if (ascii_equal_ci(city, s_city_display_map[i].input)) {
            snprintf(out, out_size, "%s", s_city_display_map[i].display);
            return;
        }
    }
    for (i = 0; city[i] && i + 1 < out_size; i++) {
        char c = city[i];
        if (word_start && c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        out[i] = c;
        word_start = (c == ' ' || c == '-' || c == '_');
    }
    out[i] = '\0';
}

static void oled_cn_show(u8 x, u8 y, weather_cn_glyph_t glyph)
{
    if (glyph < WX_CN_MAX) {
        OLED_ShowChineseGlyph(x, y, weather_cn_font[glyph]);
    }
}

/* helper: render one 8x8 CN glyph from weather_cn8_font (forecast page) */
static void oled_cn8_show(u8 x, u8 y, weather_cn8_glyph_t glyph)
{
    if (glyph < WX_CN8_MAX) {
        OLED_ShowChinese8Glyph(x, y, weather_cn8_font[glyph]);
    }
}

static void oled_cn_sequence(u8 x, u8 y,
                             const weather_cn_glyph_t *glyphs, u8 count)
{
    for (u8 i = 0; i < count; i++) {
        oled_cn_show((u8)(x + i * 16U), y, glyphs[i]);
    }
}

/* Seniverse code 0~38 -> Chinese glyph sequence ("qing"/"duoyun"/...). */
static u8 weather_cn_sequence(int code, weather_cn_glyph_t *glyphs)
{
    switch (code) {
    case 0: case 1: case 2:
        glyphs[0] = WX_CN_QING; return 1;
    case 3:
        glyphs[0] = WX_CN_DUO; glyphs[1] = WX_CN_YUN; return 2;
    case 4:
        glyphs[0] = WX_CN_YIN; return 1;
    case 5:
        glyphs[0] = WX_CN_WU; return 1;
    case 6: case 7: case 8: case 9:
        glyphs[0] = WX_CN_MAI; return 1;
    case 10: case 23:
        glyphs[0] = WX_CN_ZHEN; glyphs[1] = WX_CN_YU; return 2;
    case 11:
        glyphs[0] = WX_CN_LEI; glyphs[1] = WX_CN_ZHEN; return 2;
    case 12:
        glyphs[0] = WX_CN_LEI; glyphs[1] = WX_CN_YU; return 2;
    case 13:
        glyphs[0] = WX_CN_XIAO; glyphs[1] = WX_CN_YU; return 2;
    case 14:
        glyphs[0] = WX_CN_ZHONG; glyphs[1] = WX_CN_YU; return 2;
    case 15: case 16: case 17: case 18:
        glyphs[0] = WX_CN_DA; glyphs[1] = WX_CN_YU; return 2;
    case 19:
        glyphs[0] = WX_CN_DONG; glyphs[1] = WX_CN_YU; return 2;
    case 20: case 21: case 22: case 36:
        glyphs[0] = WX_CN_YU; glyphs[1] = WX_CN_JIA; return 2;
    case 24:
        glyphs[0] = WX_CN_XIAO; glyphs[1] = WX_CN_XUE; return 2;
    case 25:
        glyphs[0] = WX_CN_ZHONG; glyphs[1] = WX_CN_XUE; return 2;
    case 26:
        glyphs[0] = WX_CN_DA; glyphs[1] = WX_CN_XUE; return 2;
    case 27:
        glyphs[0] = WX_CN_ZHEN; glyphs[1] = WX_CN_XUE; return 2;
    case 28:
        glyphs[0] = WX_CN_FENG; return 1;
    case 29:
        glyphs[0] = WX_CN_FENG; return 1;
    case 30: case 31: case 32: case 33: case 34:
        glyphs[0] = WX_CN_FENG; return 1;
    case 35:
        glyphs[0] = WX_CN_BING; glyphs[1] = WX_CN_BAO; return 2;
    case 37:
        glyphs[0] = WX_CN_DA; glyphs[1] = WX_CN_FENG; return 2;
    case 38:
        glyphs[0] = WX_CN_JIANG; glyphs[1] = WX_CN_WEN; return 2;
    default:
        glyphs[0] = WX_CN_WU; return 1;
    }
}

static const char *weather_activity_name(weather_activity_t activity)
{
    switch (activity) {
    case WEATHER_ACT_PROVISIONING:    return "provisioning";
    case WEATHER_ACT_WIFI_CONNECTING: return "wifi connecting";
    case WEATHER_ACT_WIFI_CONNECTED:  return "wifi connected";
    case WEATHER_ACT_FETCHING:        return "weather loading";
    case WEATHER_ACT_SUCCESS:         return "weather success";
    case WEATHER_ACT_FAILED:          return "weather failed";
    default:                          return "idle";
    }
}

void ui_init(void)
{
    printf("[oled] clear\r\n");
    OLED_Clear();
}

void ui_forecast_page(const ui_forecast_view_t *v)
{
    /* 2026-10-17 redesign (2nd round): no forecast/Hi-Lo header anymore.
     * rows 0-1 = 16px current time, row2 = 8px current temperature,
     * row3 = ming/hou lo/hi per user sketch. */
    const char *time_s = v->time_str[0] ? v->time_str : "--:--";
    const char *temp_s = v->temp_str[0] ? v->temp_str : "--";
    const char *tom    = v->row_str[1][0] ? v->row_str[1] : "--";
    const char *dat    = v->row_str[2][0] ? v->row_str[2] : "--";
    u8 x;

    OLED_Clear();
    printf("[oled] forecast time=%s temp=%s tom=%s dat=%s\r\n",
           time_s, temp_s, tom, dat);

    /* rows 0-1: current time in 16px digits (5 chars = 40 px). */
    OLED_ShowString(0, 0, (u8 *)time_s, 16);
    printf("[oled] L0-1=%s\r\n", time_s);

    /* row2: current temperature (UI paints the degree bitmap itself). */
    OLED_ShowString(0, 2, (u8 *)temp_s, 8);
    x = (u8)((u16)strlen(temp_s) * 6U);
    if (x <= 118U) {
        OLED_DrawBMP(x, 2, 8, 8, (u8 *)BMP_DEGREE);
    }
    printf("[oled] L2=%s\r\n", temp_s);

    /* row3: tomorrow lo/hi, then day-after lo/hi, one 8px row. */
    oled_cn8_show(0, 3, WX_CN8_MING);
    OLED_ShowString(10, 3, (u8 *)tom, 8);
    x = (u8)(10U + strlen(tom) * 6U);
    if (x <= 118U) {
        OLED_DrawBMP(x, 3, 8, 8, (u8 *)BMP_DEGREE);
        x = (u8)(x + 10U);
    }
    if (x <= 116U) {
        oled_cn8_show(x, 3, WX_CN8_HOU);
        x = (u8)(x + 10U);
    }
    if (x <= 116U) {
        OLED_ShowString(x, 3, (u8 *)dat, 8);
        x = (u8)(x + strlen(dat) * 6U);
        if (x <= 118U) {
            OLED_DrawBMP(x, 3, 8, 8, (u8 *)BMP_DEGREE);
        }
    }
    printf("[oled] L3=%s/%s\r\n", tom, dat);
}

void ui_boot_splash(void)
{
    printf("[oled] splash: Lee Weather App\r\n");
    OLED_Clear();
    OLED_ShowString(8, 0, (u8 *)"Lee", 16);
    oled_cn_show(32, 0, WX_CN_TIAN);
    oled_cn_show(48, 0, WX_CN_QI);
    OLED_ShowString(8, 2, (u8 *)"Weather App", 16);
}

void ui_main_page(const ui_main_view_t *v, wifi_status_t st)
{
    char line[32];
    char display_city[sizeof(v->city)];
    u8 degree_x;

    switch (st) {
    case WIFI_STAT_PROVISIONING:
        s_blink++;
        printf("[oled] main status=provisioning (%u) ap=leeWethWifi\r\n",
               (unsigned)v->provisioning);
        printf("[oled] L0=WiFi%s\r\n", (s_blink & 1) ? " *" : "");
        printf("[oled] L2=AP:leeWethWifi (fixed)\r\n");
        printf("[oled] L3=open 192.168.1.100:8080\r\n");
        OLED_Clear();
        /* 16px title lives on pages 0-1 ONLY, x0..63; blink mark in the
         * free right half. Info rows start at page 2 -- no overlap with
         * the tall glyphs (GD32 original crammed SSID at page 1, which
         * erased the bottom half of the 16px title). */
        OLED_ShowString(0, 0, (u8 *)"WiFi", 16);
        oled_cn_show(32, 0, WX_CN_PEI);
        oled_cn_show(48, 0, WX_CN_WANG);
        if (s_blink & 1) {
            OLED_ShowString(64, 0, (u8 *)"*", 16);
        }
        /* User decision 2026-10-17: the FIRST page must always show the
         * fixed SoftAP name (what the phone joins), NOT the saved home
         * SSID. The home SSID shows on the connecting page instead. */
        OLED_ShowString(0, 2, (u8 *)"AP:leeWethWifi", 8);
        /* Port 80 belongs to the DA16200 LWIP page; portal serves on 8080. */
        OLED_ShowString(0, 3, (u8 *)"192.168.1.100:8080", 8);
        return;
    case WIFI_STAT_CONNECTING:
        s_blink++;
        city_display_name(v->city[0] ? v->city : NULL,
                          display_city, sizeof(display_city));
        printf("[oled] main status=connecting ssid=%s city=%s\r\n",
               v->ssid[0] ? v->ssid : "?", display_city);
        printf("[oled] L0=connecting\r\n");
        printf("[oled] L2=SSID:%.16s\r\n",
               v->ssid[0] ? v->ssid : "?");
        printf("[oled] L3=CITY:%.12s\r\n", display_city);
        OLED_Clear();
        oled_cn_show(0, 0, WX_CN_LIAN);
        oled_cn_show(16, 0, WX_CN_WANG);
        oled_cn_show(32, 0, WX_CN_ZHONG);
        snprintf(line, sizeof(line), "SSID:%.16s",
                 v->ssid[0] ? v->ssid : "?");
        OLED_ShowString(0, 2, (u8 *)line, 8);
        snprintf(line, sizeof(line), "CITY:%.12s", display_city);
        OLED_ShowString(0, 3, (u8 *)line, 8);
        return;
    case WIFI_STAT_FAIL:
        s_blink++;
        city_display_name(v->city[0] ? v->city : NULL,
                          display_city, sizeof(display_city));
        printf("[oled] main status=connect failed ssid=%s city=%s\r\n",
               v->ssid[0] ? v->ssid : "?", display_city);
        printf("[oled] L0=failed\r\n");
        printf("[oled] L2=SSID:%.16s\r\n",
               v->ssid[0] ? v->ssid : "?");
        printf("[oled] L3=%s\r\n", (s_blink & 1) ? "long press cfg"
                                                 : display_city);
        OLED_Clear();
        oled_cn_show(0, 0, WX_CN_LIAN);
        oled_cn_show(16, 0, WX_CN_JIE);
        oled_cn_show(32, 0, WX_CN_SHI);
        oled_cn_show(48, 0, WX_CN_BAI);
        snprintf(line, sizeof(line), "SSID:%.16s",
                 v->ssid[0] ? v->ssid : "?");
        OLED_ShowString(0, 2, (u8 *)line, 8);
        /* Row 3 alternates the recovery hint and the configured city so
         * both stay visible without a second info row. */
        if (s_blink & 1) {
            OLED_ShowString(0, 3, (u8 *)"long press cfg", 8);
        } else {
            snprintf(line, sizeof(line), "CITY:%.12s", display_city);
            OLED_ShowString(0, 3, (u8 *)line, 8);
        }
        return;
    default:
        break;
    }

    OLED_Clear();
    printf("[oled] main status=%d activity=%s\r\n",
           (int)st, weather_activity_name(v->activity));

    if (v->activity == WEATHER_ACT_FETCHING) {
        printf("[oled] L0=fetching weather\r\n");
        printf("[oled] L2=waiting data\r\n");
        oled_cn_show(0, 0, WX_CN_HUO);
        oled_cn_show(16, 0, WX_CN_QU);
        oled_cn_show(32, 0, WX_CN_TIAN);
        oled_cn_show(48, 0, WX_CN_QI);
        oled_cn_show(0, 2, WX_CN_TIAN);
        oled_cn_show(16, 2, WX_CN_QI);
        oled_cn_show(32, 2, WX_CN_SHU);
        oled_cn_show(48, 2, WX_CN_JU);
        return;
    }
    if (v->activity == WEATHER_ACT_FAILED) {
        printf("[oled] L0=fetch failed\r\n");
        printf("[oled] L2=check network\r\n");
        oled_cn_show(0, 0, WX_CN_HUO);
        oled_cn_show(16, 0, WX_CN_QU);
        oled_cn_show(32, 0, WX_CN_SHI);
        oled_cn_show(48, 0, WX_CN_BAI);
        oled_cn_show(0, 2, WX_CN_LIAN);
        oled_cn_show(16, 2, WX_CN_JIE);
        oled_cn_show(32, 2, WX_CN_ZHONG);
        return;
    }

    city_display_name(v->city[0] ? v->city : "IP",
                      display_city, sizeof(display_city));
    snprintf(line, sizeof(line), "%.7s", display_city);
    OLED_ShowString(0, 0, (u8 *)line, 16);

    if (v->weather_code >= 0) {
        weather_cn_glyph_t glyphs[4];
        u8 count = weather_cn_sequence(v->weather_code, glyphs);
        oled_cn_sequence(64, 0, glyphs, count);
    } else {
        OLED_ShowString(64, 0, (u8 *)"--", 16);
    }

    OLED_ShowString(96, 0,
                    (u8 *)(v->temp_str[0] ? v->temp_str : "--"), 16);
    degree_x = (u8)(96U +
                    strlen(v->temp_str[0] ? v->temp_str : "--") * 8U);
    if (degree_x <= 120U) {
        OLED_DrawBMP(degree_x, 0, 8, 8, (u8 *)BMP_DEGREE);
    }

    /* Bottom big row (16px, spans pages 2-3): "wind speed xx  humidity xx".
     * No units, no TODAY row (removed per review: it duplicated the temps).
     * 16px layout can hold exactly two pairs (2*48px + spaces <= 128px);
     * adding a third pair (today) would not fit -- today gets shown via the
     * top row temperature instead. */
    {
        u8 x = 0;
        const char *wind = v->wind_str[0] ? v->wind_str : "--";
        const char *hum  = v->humidity_str[0] ? v->humidity_str : "--";

        oled_cn_show(x, 2, WX_CN_FENG); x = (u8)(x + 16U);
        oled_cn_show(x, 2, WX_CN_SU);   x = (u8)(x + 16U);
        OLED_ShowString(x, 2, (u8 *)wind, 16);
        x = (u8)(x + strlen(wind) * 8U);
        OLED_ShowString(x, 2, (u8 *)" ", 16); x = (u8)(x + 8U);
        oled_cn_show(x, 2, WX_CN_SHI_D); x = (u8)(x + 16U);
        oled_cn_show(x, 2, WX_CN_DU_D);  x = (u8)(x + 16U);
        OLED_ShowString(x, 2, (u8 *)hum, 16);
    }

    printf("[oled] L0=city:%s %sC icon=%u\r\n",
           display_city, v->temp_str,
           (unsigned)v->icon);
    printf("[oled] L1=wx:%s\r\n",
           v->wtext[0] ? v->wtext : "-");
    printf("[oled] L2=%s: speed %s hum %s (16px row)\r\n",
           display_city,
           v->wind_str[0] ? v->wind_str : "--",
           v->humidity_str[0] ? v->humidity_str : "--");
    printf("[oled] today(hid)=%sC\r\n",
           v->range_str[0] ? v->range_str : "--");
}

void ui_wifi_page(const ui_wifi_view_t *v, wifi_status_t st)
{
    char line[24];

    OLED_Clear();
    OLED_ShowString(0, 0, (u8 *)"WiFi INFO", 8);
    printf("[oled] wifi status=%d\r\n", (int)st);
    printf("[oled] L0=WiFi INFO\r\n");

    snprintf(line, sizeof(line), "SSID:%.15s", v->ssid);
    OLED_ShowString(0, 1, (u8 *)line, 8);
    printf("[oled] L1=%s\r\n", line);

    snprintf(line, sizeof(line), "IP:%.17s",
             (v->ip && v->ip[0]) ? v->ip : "0.0.0.0");
    OLED_ShowString(0, 2, (u8 *)line, 8);
    printf("[oled] L2=%s\r\n", line);

    switch (st) {
    case WIFI_STAT_PROVISIONING:
        snprintf(line, sizeof(line), "STATUS:PROVISIONING");
        break;
    case WIFI_STAT_CONNECTING:
        snprintf(line, sizeof(line), "STATUS:CONNECTING");
        break;
    case WIFI_STAT_FAIL:
        snprintf(line, sizeof(line), "STATUS:FAILED x%u", v->fails);
        break;
    case WIFI_STAT_CONNECTED:
        snprintf(line, sizeof(line), "STATUS:CONNECTED");
        break;
    default:
        snprintf(line, sizeof(line), "STATUS:IDLE");
        break;
    }
    OLED_ShowString(0, 3, (u8 *)line, 8);
    printf("[oled] L3=%s\r\n", line);
}
