/**
 * @file  oledfont.h
 * @brief ASCII(6x8/16x8) 与内置 16x16 汉字(Hzk) 点阵表的 **extern 声明**
 *
 * 数据来源：GD32 版 4.weather_app/src/oled/oledfont.h -> RA4M2OLED 示例
 *           -> 本工程（逐字节搬运，未改任何点阵数据）
 *
 * ? 重要：本头文件原把点阵写成 tentative definition（unsigned char xxx[] = {...}），
 *   但它被多个 .c 包含（C99 下会在每个 .o 里各生成一份符号），链接时必然
 *   multiple definition 报错。因此改为：
 *       本文件  -> extern 声明
 *       oled_font_data.c -> 唯一定义处
 *
 * 提供三张表：
 *   asc2_0806[][6]   6x8  ASCII 0x20~0x7E
 *   asc2_1608[][16]  16x8 ASCII 0x20~0x7E
 *   Hzk[][32]        16x16 汉字 7 字(按 GB2312 顺序, OLED_ShowChinese 按序号取)
 *
 * 更大汉字表见 oled_font.h 的 oled_cn_1212 / oled_cn_1616。
 *
 * ? 本文件为 GBK 无 BOM（字库注释含中文）。
 */

#ifndef __OLEDFONT_H
#define __OLEDFONT_H

extern const unsigned char asc2_0806[][6];      /**< 6x8  ASCII, 码位 0x20~0x7E */
extern const unsigned char asc2_1608[][16];     /**< 16x8 ASCII, 码位 0x20~0x7E */
extern const unsigned char Hzk[][32];          /**< 16x16 汉字 7 字 */

#endif /* __OLEDFONT_H */
