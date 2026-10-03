/**
 * @file  weather_text.h
 * @brief 心知天气代码 -> 显示文案/图标索引
 *
 * ============================ 编码约定 ============================
 * 本文件与 weather_text.c 为 **GBK 无 BOM**。
 *
 * 实测（2026-10-03）：心知 API 返回 **UTF-8**
 *   （响应头 `Content-Type: application/json; charset=utf-8`，
 *     now.text 与 location.name 均为 UTF-8，如 "北京市"、"晴"）。
 * 因此：
 *   - 下面的 s_map **目前存英文**（"Sunny"/"Clear"/"Lt rain"），
 *     与 ASCII 字库匹配，可直接上屏，无编码问题。
 *   - 阶段3 若要显示**中文天气文案**，有两种做法：
 *       (a) 扩 s_map 为中文表 —— 文件须仍为 GBK 无 BOM，
 *           且表内字符串要与 API 的 UTF-8 用 oled_gbk_to_utf8() 比对，
 *           切勿直接 strcmp；
 *       (b) 用心知 code 做 key，显示时经 oled_font.h 的 GBK 字库查表，
 *           不做字符串比较（更稳，推荐）。
 *   - 一律用 code 匹配，**不要用 now.text 直接 strcmp**。
 */
#ifndef _WEATHER_TEXT_H_
#define _WEATHER_TEXT_H_

#include <stdint.h>

typedef enum {
    ICON_SUNNY = 0,
    ICON_CLOUDY,
    ICON_RAIN,
    ICON_SNOW,
    ICON_FOG,
    ICON_THUNDER,
    ICON_MAX,
} weather_icon_t;

/**
 * @brief 心知天气 code -> 短文案
 * @param code 心知天气代码 0~38
 * @return     文案字符串（静态常量，'\0' 结尾）；未知 code 返回 "----"
 */
const char      *weather_text_get(int code);

/**
 * @brief 心知天气 code -> 图标索引
 * @param code 心知天气代码 0~38
 * @return     图标枚举；未知 code 返回 ICON_CLOUDY
 */
weather_icon_t   weather_icon_of(int code);

#endif /* _WEATHER_TEXT_H_ */
