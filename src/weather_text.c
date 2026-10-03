/**
 * @file  weather_text.c
 * @brief 心知天气代码映射表 (v3 API code 0~38)
 *        https://docs.seniverse.com/api/start/code.html
 *
 * ============================ 编码约定（重要） ============================
 * 本文件为 **GBK 无 BOM**。上表 text 全是 ASCII 英文, 与编码无关,
 * 可直接 strcmp / printf 上屏。
 *
 * 为什么用英文而不是 API 返回的 now.text:
 *   实测 API 返回 **UTF-8**（charset=utf-8, now.text 形如 "晴"），
 *   而本工程串口/日志链路在中文 Windows 工具链下会把源文件里的中文
 *   变成乱码, OLED 字库又是按 GBK 索引, 三者混用极易出错。
 *   **统一用 code 查表**, 绕开字符串编码匹配, 是最稳的路径。
 *
 * 阶段3 若要显示中文天气文案, 见 oled_font.h 的说明:
 *   扩本表为中文(文件仍 GBK 无 BOM) + 用 code 匹配 + 上屏前转 GBK 字库。
 */
#include "weather_text.h"

/** @brief code -> 文案/图标 的映射项 */
typedef struct {
    int           code;       /**< 心知天气 code 0~38 */
    const char   *text;       /**< 英文短文案, ASCII, 直接上屏 */
    weather_icon_t icon;      /**< 对应图标 */
} code_map_t;

/** 39 条映射表, 顺序与心知 v3 文档一致 */
static const code_map_t s_map[] = {
    { 0, "Sunny",   ICON_SUNNY   },
    { 1, "Clear",   ICON_SUNNY   },
    { 2, "Fair",    ICON_SUNNY   },
    { 3, "Cloudy",  ICON_CLOUDY  },
    { 4, "Overcast",ICON_CLOUDY  },
    { 5, "Foggy",   ICON_FOG     },
    { 6, "Haze",    ICON_FOG     },
    { 7, "Lt Haze", ICON_FOG     },
    { 8, "Md Haze", ICON_FOG     },
    { 9, "Hv Haze", ICON_FOG     },
    {10, "Shower",  ICON_RAIN    },
    {11, "Thshower",ICON_THUNDER },
    {12, "Tstorm",  ICON_THUNDER },
    {13, "Lt rain", ICON_RAIN    },
    {14, "Md rain", ICON_RAIN    },
    {15, "Hv rain", ICON_RAIN    },
    {16, "Storm",   ICON_RAIN    },
    {17, "Hv storm",ICON_RAIN    },
    {18, "Severe",  ICON_RAIN    },
    {19, "Freezrn", ICON_SNOW    },
    {20, "Sleet",   ICON_SNOW    },
    {21, "Rainsnow",ICON_SNOW    },
    {22, "Rainsnow",ICON_SNOW    },
    {23, "Shower",  ICON_RAIN    },
    {24, "Lt snow", ICON_SNOW    },
    {25, "Md snow", ICON_SNOW    },
    {26, "Hv snow", ICON_SNOW    },
    {27, "Shower",  ICON_SNOW    },
    {28, "Dust",    ICON_FOG     },
    {29, "BlowSand",ICON_FOG     },
    {30, "Sandst",  ICON_FOG     },
    {31, "Sandst",  ICON_FOG     },
    {32, "Sandst",  ICON_FOG     },
    {33, "SndStorm",ICON_FOG     },
    {34, "SndStorm",ICON_FOG     },
    {35, "Hail",    ICON_SNOW    },
    {36, "Sleet",   ICON_SNOW    },
    {37, "Windy",   ICON_CLOUDY  },
    {38, "Colder",  ICON_CLOUDY  },
};

#define N_MAP (sizeof(s_map) / sizeof(s_map[0]))

const char *weather_text_get(int code)
{
    for (unsigned i = 0; i < N_MAP; i++) {
        if (s_map[i].code == code) {
            return s_map[i].text;
        }
    }
    return "----";
}

weather_icon_t weather_icon_of(int code)
{
    for (unsigned i = 0; i < N_MAP; i++) {
        if (s_map[i].code == code) {
            return s_map[i].icon;
        }
    }
    return ICON_CLOUDY;
}
