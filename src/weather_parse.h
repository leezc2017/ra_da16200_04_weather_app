/**
 * @file  weather_parse.h
 * @brief 心知天气 v3 JSON 响应解析
 */
#ifndef _WEATHER_PARSE_H_
#define _WEATHER_PARSE_H_

#include <stdint.h>

typedef struct {
    char     text[32];      /* 天气现象: "Sunny" */
    int      code;          /* 天气代码 0~38 */
    int      temperature;   /* °C */
    char     update[24];    /* last_update: "2024-05-01T12:00:00+08:00" */
} weather_data_t;

/* 从 body 提取 results[0].now + last_update, 成功返回 0 */
int weather_parse_now(const char *body, weather_data_t *out);

#endif /* _WEATHER_PARSE_H_ */
