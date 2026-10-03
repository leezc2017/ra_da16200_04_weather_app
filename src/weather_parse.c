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
        strncpy(out->text, cJSON_GetStringValue(t) ? t->valuestring : "",
                sizeof(out->text) - 1);
        out->code = atoi(cJSON_GetStringValue(c) ? c->valuestring : "0");
        out->temperature = atoi(cJSON_GetStringValue(tp) ? tp->valuestring : "0");

        cJSON *up = cJSON_GetObjectItem(root, "last_update");
        if (up && up->valuestring) {
            strncpy(out->update, up->valuestring, sizeof(out->update) - 1);
        }
        ret = 0;
    } while (0);

    cJSON_Delete(root);
    return ret;
}
