/**
 * @file  oled_cn_font.c
 * @brief 可扩充汉字点阵字库（12x12 / 16x16），当前为空占位
 *
 * ============================ 为什么需要它 ============================
 * oledfont.h 自带的 Hzk[] 只有 7 个汉字，撑不住天气站的界面文案。
 * 本文件提供按 GBK 首字节索引的查表字库，可随需要扩充：
 *
 *   存储格式（与 oled_font.c 的 cn_lookup() 对应）：
 *       重复 { GBK首字节, 宽度, 点阵数据[stride] }，以 GBK首字节==0 结束
 *       stride = ((size+7)/8) * size  ->  12x12: 36 字节, 16x16: 32 字节
 *       点阵逐行存储，每行 (size+7)/8 字节，位从低位到高位对应像素从左到右
 *       （SSD1306 竖向 8 像素分组）
 *
 * ============================ 怎么生成真实数据 ============================
 *   pip install pillow
 *   python tools/glyph2c.py --ttf %WINDIR%/Fonts/simsun.ttc --size 12 ^
 *          --out src/oled_font_data/oled_cn_font.c
 *
 * 脚本内置天气站字符集（CHARSET），覆盖界面全部用字。
 *
 * ⚠ 生成的文件必须是 GBK 无 BOM（脚本已固化 encoding='gbk'）。
 *
 * ============================ 与 oled_font.h 的关系 ============================
 * 本文件不 include oledfont.h（避免与其数据定义冲突），
 * 只 include oled/oled_font.h 拿到 oled_cn_1212/oled_cn_1616 的 extern 声明。
 */

#include "oled/oled_font.h"

/**
 * @brief 12x12 中英混排字库（空占位）
 *
 * 查不到字符时 oled_font_get() 回退到 ASCII 的 '?'，不会崩，
 * 因此空表也能安全运行，只是汉字显示为 '?'。
 */
const uint8_t oled_cn_1212[] =
{
    0x00, 0x00,                    /* 结束标记 */
};

/**
 * @brief 16x16 中文字库（空占位）
 *
 * @see oled_cn_1212 的格式说明
 */
const uint8_t oled_cn_1616[] =
{
    0x00, 0x00,                    /* 结束标记 */
};