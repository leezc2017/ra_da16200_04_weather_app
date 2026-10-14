/**
 * @file  weather_parse.c
 * @brief 用 SDK 自带 cJSON 解析心知天气 weather/now.json
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"
#include "weather_parse.h"

int weather_parse_now(const char *body, weather_data_t *out)
{
    if (body == NULL || out == NULL) {
        return -1;
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return -1;
    }

    int ret = -1;
    weather_data_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    parsed.feels_like = -1;
    parsed.humidity = -1;
    parsed.wind_speed = -1;
    parsed.high = -1;
    parsed.low = -1;
    do {
        cJSON *results = cJSON_GetObjectItem(root, "results");
        if (!cJSON_IsArray(results) || cJSON_GetArraySize(results) < 1) {
            break;
        }
        cJSON *r0 = cJSON_GetArrayItem(results, 0);

        cJSON *now = cJSON_GetObjectItem(r0, "now");
        if (now == NULL) {
            break;
        }
        cJSON *t = cJSON_GetObjectItem(now, "text");
        cJSON *c = cJSON_GetObjectItem(now, "code");
        cJSON *tp = cJSON_GetObjectItem(now, "temperature");
        if (t == NULL || c == NULL || tp == NULL) {
            break;
        }
        strncpy(parsed.text, cJSON_GetStringValue(t) ? t->valuestring : "",
                sizeof(parsed.text) - 1);
        parsed.code = atoi(cJSON_GetStringValue(c) ? c->valuestring : "0");
        parsed.temperature = atoi(cJSON_GetStringValue(tp) ? tp->valuestring : "0");

        cJSON *feel = cJSON_GetObjectItem(now, "feels_like");
        cJSON *humidity = cJSON_GetObjectItem(now, "humidity");
        cJSON *wind = cJSON_GetObjectItem(now, "wind_speed");
        if (feel && cJSON_GetStringValue(feel)) {
            parsed.feels_like = atoi(feel->valuestring);
        }
        if (humidity && cJSON_GetStringValue(humidity)) {
            parsed.humidity = atoi(humidity->valuestring);
        }
        if (wind && cJSON_GetStringValue(wind)) {
            parsed.wind_speed = atoi(wind->valuestring);
        }

        cJSON *up = cJSON_GetObjectItem(root, "last_update");
        if (up && up->valuestring) {
            strncpy(parsed.update, up->valuestring, sizeof(parsed.update) - 1);
        }
        *out = parsed;
        ret = 0;
    } while (0);

    cJSON_Delete(root);
    return ret;
}

int weather_parse_daily(const char *body, weather_data_t *out)
{
    if (body == NULL || out == NULL) {
        return -1;
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return -1;
    }

    int ret = -1;
    do {
        cJSON *results = cJSON_GetObjectItem(root, "results");
        if (!cJSON_IsArray(results) || cJSON_GetArraySize(results) < 1) {
            break;
        }
        cJSON *r0 = cJSON_GetArrayItem(results, 0);
        cJSON *daily = cJSON_GetObjectItem(r0, "daily");
        if (!cJSON_IsArray(daily) || cJSON_GetArraySize(daily) < 1) {
            break;
        }
        cJSON *d0 = cJSON_GetArrayItem(daily, 0);
        cJSON *high = cJSON_GetObjectItem(d0, "high");
        cJSON *low = cJSON_GetObjectItem(d0, "low");
        if (high == NULL || low == NULL ||
            cJSON_GetStringValue(high) == NULL ||
            cJSON_GetStringValue(low) == NULL) {
            break;
        }

        out->high = atoi(high->valuestring);
        out->low = atoi(low->valuestring);

        /* Keep humidity and wind_speed from now.json for the live conditions
         * shown on the GD32-style main page; daily data supplies only the range. */
        out->daily_valid = 1U;
        ret = 0;
    } while (0);

    cJSON_Delete(root);
    return ret;
}

/* ===================== 解析器离线自检 =====================
 *
 * 下面这份 body 是 2026-10-04 直接向心知接口请求拿到的**真实响应**
 * （api.seniverse.com/v3/weather/now.json，location=ip&language=zh-Hans），
 * 一字未改，包含完整的中文 UTF-8 字段。把真实字节喂给真 cJSON，
 * 就不必靠"看字段名像不像对的"来下结论。
 *
 * ⚠ 2026-10-13 起请求语言改为 weather_cfg.h:WEATHER_LANG（当前 "en"），
 *   新响应的 text/name 取值会变成英文。本样本保留当年的中文字节不动：
 *   解析层只认 JSON key（results/now/text/code/temperature/last_update），
 *   不认取值，因此换成英文响应同样通过自检。
 *
 * 注意字段名：天气现象/编码/温度都在 results[0].now 下，
 * 键名分别是 text / code / temperature，三者**都是字符串**（所以要 atoi）。
 * 最后一次猜的 ads / lc 并不存在。
 */
static const char SELFTEST_BODY[] =
    "{\"results\":[{\"location\":{\"id\":\"WX4FBXXFKE4F\",\"name\":\"\xe5\x8c\x97\xe4\xba\xac\","
    "\"country\":\"CN\",\"path\":\"\xe5\x8c\x97\xe4\xba\xac,\xe5\x8c\x97\xe4\xba\xac,\xe4\xb8\xad\xe5\x9b\xbd\","
    "\"timezone\":\"Asia/Shanghai\",\"timezone_offset\":\"+08:00\"},"
    "\"now\":{\"text\":\"\xe6\x99\xb4\",\"code\":\"0\",\"temperature\":\"18\"}}],"
    "\"last_update\":\"2026-10-04T11:34:41+08:00\"}";

bool weather_parse_selftest(char * out_buf, uint16_t size)
{
    weather_data_t w;
    int r;

    if ((NULL == out_buf) || (0U == size))
    {
        return false;
    }

    memset(&w, 0, sizeof(w));
    r = weather_parse_now(SELFTEST_BODY, &w);

    if (0 != r)
    {
        snprintf(out_buf, size, "cJSON ret=%d", r);
        return false;
    }

    /* 三项都逐一核对, 不能只看"没报错" —— 静默取到空字段同样算坏。 */
    if ((0 != w.code) ||
        (18!= w.temperature) ||
        (0 != strcmp(w.text, "\xe6\x99\xb4")) ||            /* "晴" */
        (0 != strncmp(w.update, "2026-10-04T11:34:41", 19)))
    {
        snprintf(out_buf, size, "bad fields code=%d temp=%d text=[%s]",
                 w.code, w.temperature, w.text);
        return false;
    }

    snprintf(out_buf, size, "code=%d temp=%d text=%s(up=%uB)",
             w.code, w.temperature, w.update, (unsigned)sizeof(SELFTEST_BODY) - 1U);
    return true;
}
