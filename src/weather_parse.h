/**
 * @file  weather_parse.h
 * @brief 心知天气 v3 JSON 响应解析
 */
#ifndef _WEATHER_PARSE_H_
#define _WEATHER_PARSE_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char     text[32];      /* 天气现象: "Sunny" */
    int      code;          /* 天气代码 0~38 */
    int      temperature;   /* °C */
    int      feels_like;    /* 体感温度；未返回时为 -1 */
    int      humidity;      /* 湿度百分比；未返回时为 -1 */
    int      wind_speed;    /* 风速 km/h；未返回时为 -1 */
    int      high;          /* 今日最高温；未返回时为 -1 */
    int      low;           /* 今日最低温；未返回时为 -1 */
    uint8_t  daily_valid;   /* 今日预报是否成功解析 */
    char     update[24];    /* last_update: "2024-05-01T12:00:00+08:00" */
    char     location[33];  /* results[0].location.name, API returned city
                             * name (en: ASCII e.g. "Chifeng"); empty = none */
    int      f_high[3];     /* 每天最高温 °C (index0=今天; i<f_count 才有效) */
    int      f_low[3];      /* 每天最低温 °C */
    uint8_t  f_count;       /* 成功解析的天数(含当天), <=3; 0=无预报数据 */
} weather_data_t;

/* 从 body 提取 results[0].now + last_update, 成功返回 0 */
int weather_parse_now(const char *body, weather_data_t *out);

/* 将 daily.json 的今日预报合并到已有天气数据；成功返回 0。 */
int weather_parse_daily(const char *body, weather_data_t *out);

/**
 * @brief 解析器离线自检：拿一份**实测抓到的真实响应**跑一遍解析
 *
 * 为什么需要（2026-10-04）：
 *   第一次排查"天气出不来"时，我无法区分是**链路没把 body 带回来**还是
 *   **解析把这份 body 解析坏了**，而这两者的修法完全不同。更糟的是我
 *   没有把心知返回的真实 JSON 弄到手就按字段名猜（猜的是 ads/lc，
 *   实际是 code/temperature），等于在猜。
 *
 *   本机没有可用的主机 C 编译器（只有 arm-none-eabi，跑不了），所以把
 *   真实 body 嵌进固件，串口敲 WXTEST 即可在目标板上直接跑真 cJSON。
 *   一次烧录就能把"解析层"和"传输层"彻底切开：
 *     WXTEST PASS  -> 解析层没问题，锅一定在 NWHTC 传输/收口那一侧
 *     WXTEST FAIL  -> 解析层有问题，与网络无关
 *
 * @param out_buf  结果摘要缓冲（打印用，ASCII）
 * @param size     缓冲大小
 * @return true=解析成功且字段值与预期一致
 */
bool weather_parse_selftest(char * out_buf, uint16_t size);

#endif /* _WEATHER_PARSE_H_ */
