/**
 * @file  oled_font.h
 * @brief OLED 字库查表 + UTF-8/GBK 字符集转换（阶段3 显示链路的编码层）
 *
 * ============================ 移植来源 ============================
 * oled.c / oled.h / oledfont.h **原样来自 RA4M2OLED 示例**（回调注入架构，零改动）：
 *   - oled.c 的 OLED_ShowChar/ShowString/ShowChinese 依赖 oledfont.h 里的
 *     asc2_0806[][6] / asc2_1608[][16] / Hzk[][32]
 *   - oled_port.c 是示例自带的 RA4M2 GPIO 软 SPI 适配层（**未改动**）
 *   - spi 时序由 gpio_spi/gpio_spi.c 提供（SPI_MODE0, 100kHz）
 * 本文件是在其之上新增的一层，只做两件示例没做的事：
 *
 * ============================ 1. 为什么需要编码转换 ============================
 * 实测（2026-10-03）心知 API 返回 **UTF-8**
 *   （响应头 `Content-Type: application/json; charset=utf-8`，
 *     location.name 形如 "北京市"，now.text 亦为 UTF-8）。
 * 而 oledfont.h 的 Hzk[] 汉字表是**按 GB2312 顺序**索引的
 *   （GBK 双字节码 = (区-16)*94+(位-1) 线性映射）。
 * 所以 UTF-8 文本上屏前必须先转 GBK，否则全是错字。
 *
 * ============================ 2. 汉字字库扩充 ============================
 * oledfont.h 的 Hzk[] 只有 7 个汉字，不够用。本文件另提供
 *   oled_cn_1212 / oled_cn_1616 —— 按 GBK 首字节索引的可扩充查表字库，
 *   由 tools/glyph2c.py 从 TTF 生成（见该脚本的 CHARSET）。
 *
 * ============================ 3. 全工程编码约定（强制） ============================
 * - **printf / 串口日志字符串：必须 ASCII**。工具链按系统代码页(GBK)读 UTF-8
 *   源文件，中文会变乱码（"突发" -> "绐佸彂"）。改代码时勿加中文日志。
 * - **需要进入固件的中文数据（字库、注释）：源文件用 GBK 无 BOM**，
 *   与 GD32 版 / RA 示例一致。oledfont.h、weather_text.c 即是 GBK。
 * - **OLED 显示链路内部统一 UTF-8**，只在查字库前转 GBK。
 * - **一律用 weather code 查表**，不要拿 API 的 now.text 做 strcmp
 *   （编码不同必然失配，见 weather_text.h）。
 */
#ifndef OLED_FONT_H
#define OLED_FONT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ===================== 1. 字模尺寸与索引 ===================== */

/** ASCII 字模档位 */
typedef enum {
    OLED_FONT_6X8  = 0,   /**< 6x8  ASCII（oledfont.h 已有 asc2_0806） */
    OLED_FONT_16X8 = 1,   /**< 16x8 ASCII（oledfont.h 已有 asc2_1608） */
    OLED_FONT_12X12= 2,   /**< 12x12 中英混排（oled_cn_1212） */
    OLED_FONT_16X16= 3    /**< 16x16 中文（oled_cn_1616） */
} oled_font_t;

/**
 * @brief 取一个字符的字模指针
 *
 * @param ch    UTF-8 编码的首字节（调用方需保证已解码为单字符）
 * @param font  字模档位
 * @param w     输出：字模宽度（像素）
 * @param h     输出：字模高度（像素）
 * @return      指向字模数据的指针；查不到时返回 ASCII 的 '?' 字模
 *
 * @note  字模为逐行存储，每行 (w+7)/8 字节，共 h 行 —— SSD1306 竖向 8 像素点阵
 */
const uint8_t * oled_font_get(uint8_t ch, oled_font_t font, uint8_t * w, uint8_t * h);

/**
 * @brief 字符宽度（像素），用于排版计算
 * @param ch   UTF-8 首字节
 * @param font 字模档位
 * @return     宽度像素数
 */
uint8_t oled_char_width(uint8_t ch, oled_font_t font);

/* ===================== 2. UTF-8 / GBK 转换 ===================== */

/**
 * @brief UTF-8 字符串转 GBK
 *
 * OLED 点阵字模按 GBK/ASCII 索引（历史原因，见 oledfont.h 注释），
 * 而 API 返回 UTF-8，故上屏前需转换。
 *
 * @param utf8  输入 UTF-8 字符串（'\0' 结尾）
 * @param gbk   输出缓冲
 * @param gbk_size 输出缓冲字节数
 * @return      写入的字节数（不含结尾 '\0'）；缓冲不足时返回所需长度
 */
size_t oled_utf8_to_gbk(const char * utf8, char * gbk, size_t gbk_size);

/**
 * @brief GBK 字符串转 UTF-8
 *
 * @param gbk    输入 GBK 字符串
 * @param gbk_len 输入长度，或传 0 表示按 '\0' 截断
 * @param utf8   输出缓冲
 * @param utf8_size 输出缓冲字节数
 * @return       写入的字节数（不含结尾 '\0'）
 */
size_t oled_gbk_to_utf8(const char * gbk, size_t gbk_len, char * utf8, size_t utf8_size);

/**
 * @brief 取 UTF-8 字符串的字符数（中文按 1 个字符计）
 * @param utf8 UTF-8 字符串
 * @return     字符个数
 */
size_t oled_utf8_len(const char * utf8);

/**
 * @brief 跳过 UTF-8 字符串的一个字符，返回下一个字符的指针
 * @param p 当前字符指针
 * @return  下一个字符指针；p 指向结尾时返回原 p
 */
const char * oled_utf8_next(const char * p);

/* ===================== 3. 字库数据（外部文件提供） ===================== */

/* ASCII 点阵 + 原有 16x16 汉字表(Hzk)，来自 oledfont.h（GD32 原版，GBK 无 BOM）。
 * oled.c 的 OLED_ShowChinese() 直接引用 Hzk[]，因此必须包含本文件。
 *
 * 本模块额外提供：
 *   - oled_cn_1212 / oled_cn_1616：可扩充的汉字查表字库（见下方说明）
 *   - UTF-8 <-> GBK 转换（API 返回 UTF-8，字库按 GBK 索引）
 *
 * ⚠ 三个 .c/.h 文件的编码约定见 README §3.0.1。 */
#include "oledfont.h"

/**
 * 12x12 中英混排点阵表
 * 结构：连续存放若干条 { 首字节, 宽度, 数据[36] }，以首字节 0 结束。
 * 由 tools/glyph2c.py 从 TTF/点阵图生成，勿手改。
 */
extern const uint8_t oled_cn_1212[];

/**
 * 16x16 中文点阵表，结构同上，每条数据 32 字节。
 * 由 tools/glyph2c.py 生成，勿手改。
 */
extern const uint8_t oled_cn_1616[];

#endif /* OLED_FONT_H */