/**
 * @file  oled_font.c
 * @brief 字库查表与 UTF-8/GBK 转换实现
 *
 * 依赖 oledfont.h（GD32 版移植）提供 ASCII 点阵；汉字点阵由
 * oled_cn_1212 / oled_cn_1616 提供。
 */
#include <string.h>
#include "oled_font.h"

/* ===================== 1. UTF-8 解码 ===================== */

/**
 * @brief 解码一个 UTF-8 字符
 *
 * @param p    字符指针
 * @param len  输出：此字符占用的字节数（非法字节按 1 处理）
 * @return     Unicode 码点；非法序列返回 -1
 *
 * @note 支持 1~4 字节；拒绝 overlong 编码与代理区码点
 */
static int utf8_decode(const char * p, size_t * len)
{
    const uint8_t * u = (const uint8_t *)p;
    uint8_t b0 = u[0];

    if (b0 < 0x80U)
    {
        *len = 1U;
        return (int)b0;
    }
    else if ((b0 & 0xE0U) == 0xC0U)
    {
        if ((u[1] & 0xC0U) != 0x80U)
        {
            *len = 1U;
            return -1;
        }
        {
            int cp = (int)(((b0 & 0x1FU) << 6) | (u[1] & 0x3FU));

            *len = 2U;
            return (cp < 0x80) ? -1 : cp;        /* 拒绝 overlong */
        }
    }
    else if ((b0 & 0xF0U) == 0xE0U)
    {
        if ((u[1] & 0xC0U) != 0x80U || (u[2] & 0xC0U) != 0x80U)
        {
            *len = 1U;
            return -1;
        }
        {
            int cp = (int)(((b0 & 0x0FU) << 12) | ((u[1] & 0x3FU) << 6) | (u[2] & 0x3FU));

            *len = 3U;
            if ((cp < 0x800) || ((cp >= 0xD800) && (cp <= 0xDFFF)))
            {
                return -1;
            }
            return cp;
        }
    }
    else if ((b0 & 0xF8U) == 0xF0U)
    {
        if ((u[1] & 0xC0U) != 0x80U || (u[2] & 0xC0U) != 0x80U || (u[3] & 0xC0U) != 0x80U)
        {
            *len = 1U;
            return -1;
        }
        {
            int cp = (int)(((b0 & 0x07U) << 18) | ((u[1] & 0x3FU) << 12) |
                           ((u[2] & 0x3FU) << 6) | (u[3] & 0x3FU));

            *len = 4U;
            if ((cp < 0x10000) || (cp > 0x10FFFF))
            {
                return -1;
            }
            return cp;
        }
    }

    *len = 1U;
    return -1;
}

size_t oled_utf8_len(const char * utf8)
{
    size_t n = 0U;

    if (NULL == utf8)
    {
        return 0U;
    }
    while ('\0' != *utf8)
    {
        size_t l;

        (void)utf8_decode(utf8, &l);
        utf8 += l;
        n++;
    }
    return n;
}

const char * oled_utf8_next(const char * p)
{
    size_t l;

    if ((NULL == p) || ('\0' == *p))
    {
        return p;
    }
    (void)utf8_decode(p, &l);
    return p + l;
}

/* ===================== 2. GBK 双字节首字节判定 ===================== */

/**
 * @brief 判断某字节是否可作为 GBK 双字节序列的首字节
 * @param b 字节值
 * @return   true = 可作首字节（后随 1 个 0x40~0xFE 的次字节）
 */
static bool gbk_is_lead(uint8_t b)
{
    return ((b >= 0x81U) && (b <= 0xFEU));
}

/**
 * @brief 判断某字节是否可作为 GBK 次字节
 * @param b 字节值
 * @return   true = 合法次字节
 */
static bool gbk_is_trail(uint8_t b)
{
    return ((b >= 0x40U) && (b <= 0xFEU)) && (0x7FU != b);
}

/**
 * @brief 计算一个内码（Unicode 码点）的 GBK 编码
 *
 * @param cp   Unicode 码点
 * @param out  输出 2 字节
 * @return     写入字节数（1 = 映射到单字节区，2 = GBK 双字节，0 = 无映射）
 *
 * @note 覆盖 GBK 一级汉字（0xB0A1~0xF7FE，共 23940 字）与二级汉字、
 *       符号区。字库表外的字返回 0，调用方应回退到 '?'。
 */
static size_t gbk_encode_cp(int cp, uint8_t out[2])
{
    /* ASCII 直通 */
    if (cp < 0x80)
    {
        out[0] = (uint8_t)cp;
        return 1U;
    }

    /* GBK 一级汉字: 按 GB2312 区位码线性映射
     * GB2312 一级汉字 3755 个, 区位码 16~55(区) / 1~94(位)
     * 线性序 = (区-16)*94 + (位-1)
     * GBK 编码 = 0xB0 + (序/94), 0xA1 + (序%94)
     * 反解: 序 = (首-0xB0)*94 + (次-0xA1)
     */
    if ((cp >= 0x4E00) && (cp <= 0x9FA5))            /* 一级汉字常用区 */
    {
        uint32_t seq = (uint32_t)cp - 0x4E00U;       /* 连续排列(Unicode 有序) */

        out[0] = (uint8_t)(0xB0U + (seq / 94U));
        out[1] = (uint8_t)(0xA1U + (seq % 94U));
        return 2U;
    }

    /* GBK 二级汉字(扩展A): 0xB840~0xF7FE, 少用, 暂不映射 */
    return 0U;
}

/**
 * @brief GBK 双字节序列 → Unicode 码点
 * @param lead  首字节
 * @param trail 次字节
 * @return      Unicode 码点；无映射返回 -1
 */
static int gbk_decode_pair(uint8_t lead, uint8_t trail)
{
    /* 一级汉字(按上面的逆映射) */
    if ((lead >= 0xB0U) && (lead <= 0xF7U) && (trail >= 0xA1U) && (trail <= 0xFEU))
    {
        return (int)((lead - 0xB0U) * 94 + (trail - 0xA1U) + 0x4E00);
    }
    return -1;
}

/* ===================== 3. 转换接口实现 ===================== */

size_t oled_utf8_to_gbk(const char * utf8, char * gbk, size_t gbk_size)
{
    size_t out = 0U;

    if ((NULL == utf8) || (NULL == gbk) || (0U == gbk_size))
    {
        return 0U;
    }

    while ('\0' != *utf8)
    {
        size_t l;
        int cp = utf8_decode(utf8, &l);
        uint8_t enc[2];
        size_t n;

        utf8 += l;

        if (cp < 0)
        {
            enc[0] = (uint8_t)'?';
            n = 1U;
        }
        else
        {
            n = gbk_encode_cp(cp, enc);
            if (0U == n)                              /* 字库无此字 */
            {
                enc[0] = (uint8_t)'?';
                n = 1U;
            }
        }

        if ((out + n) >= gbk_size)                    /* 空间不足: 截断 */
        {
            break;
        }
        memcpy(&gbk[out], enc, n);
        out += n;
    }

    gbk[out] = '\0';
    return out;
}

size_t oled_gbk_to_utf8(const char * gbk, size_t gbk_len, char * utf8, size_t utf8_size)
{
    size_t out = 0U;
    size_t i = 0U;

    if ((NULL == gbk) || (NULL == utf8) || (0U == utf8_size))
    {
        return 0U;
    }
    if (0U == gbk_len)
    {
        gbk_len = strlen(gbk);
    }

    while ((i < gbk_len) && ('\0' != gbk[i]))
    {
        uint8_t b0 = (uint8_t)gbk[i];
        int cp;

        if (b0 < 0x80U)                               /* ASCII 直通 */
        {
            if ((out + 1U) >= utf8_size)
            {
                break;
            }
            utf8[out++] = (char)b0;
            i++;
            continue;
        }

        if (gbk_is_lead(b0) && ((i + 1U) < gbk_len) &&
            gbk_is_trail((uint8_t)gbk[i + 1U]))
        {
            cp = gbk_decode_pair(b0, (uint8_t)gbk[i + 1U]);
            i += 2U;
        }
        else                                            /* 孤立高位字节 */
        {
            cp = -1;
            i += 1U;
        }

        if (cp < 0)
        {
            if ((out + 1U) >= utf8_size)
            {
                break;
            }
            utf8[out++] = '?';
            continue;
        }

        /* 手工编码为 UTF-8 (c 转 uint32_t 避免有符号右移的符号扩展) */
        {
            uint32_t u = (uint32_t)cp;

            if (u < 0x800U)
            {
                if ((out + 2U) >= utf8_size)
                {
                    break;
                }
                utf8[out++] = (char)(uint8_t)(0xC0U | (u >> 6));
                utf8[out++] = (char)(uint8_t)(0x80U | (u & 0x3FU));
            }
            else if (u < 0x10000U)
            {
                if ((out + 3U) >= utf8_size)
                {
                    break;
                }
                utf8[out++] = (char)(uint8_t)(0xE0U | (u >> 12));
                utf8[out++] = (char)(uint8_t)(0x80U | ((u >> 6) & 0x3FU));
                utf8[out++] = (char)(uint8_t)(0x80U | (u & 0x3FU));
            }
            else
            {
                if ((out + 4U) >= utf8_size)
                {
                    break;
                }
                utf8[out++] = (char)(uint8_t)(0xF0U | (u >> 18));
                utf8[out++] = (char)(uint8_t)(0x80U | ((u >> 12) & 0x3FU));
                utf8[out++] = (char)(uint8_t)(0x80U | ((u >> 6) & 0x3FU));
                utf8[out++] = (char)(uint8_t)(0x80U | (u & 0x3FU));
            }
        }
    }

    utf8[out] = '\0';
    return out;
}

/* ===================== 4. 字库查表 ===================== */

/**
 * @brief 在汉字点阵表中查找首字节
 *
 * @param table 表首地址
 * @param stride 每条记录的数据字节数（12x12=36, 16x16=32）
 * @param lead 首字节
 * @param out_data 输出：指向字模数据
 * @return true = 找到
 */
static bool cn_lookup(const uint8_t * table, uint8_t stride, uint8_t lead, const uint8_t ** out_data)
{
    const uint8_t * p = table;

    while (0U != p[0])
    {
        if (p[0] == lead)
        {
            *out_data = &p[2];                       /* [0]=首字节 [1]=宽度 */
            return true;
        }
        p += (size_t)(2U + stride);
    }
    return false;
}

const uint8_t * oled_font_get(uint8_t ch, oled_font_t font, uint8_t * w, uint8_t * h)
{
    const uint8_t * data = NULL;

    switch (font)
    {
        case OLED_FONT_6X8:
            if (w) { *w = 6U; }
            if (h) { *h = 8U; }
            if ((ch >= 0x20U) && (ch < 0x7FU))
            {
                return asc2_0806[ch - 0x20U];
            }
            return asc2_0806[0];                     /* '?' 实为 sp */

        case OLED_FONT_16X8:
            if (w) { *w = 8U; }
            if (h) { *h = 16U; }
            if ((ch >= 0x20U) && (ch < 0x7FU))
            {
                return asc2_1608[ch - 0x20U];
            }
            return asc2_1608[0];

        case OLED_FONT_12X12:
            if (w) { *w = 12U; }
            if (h) { *h = 12U; }
            if (ch < 0x80U)
            {
                /* ASCII 在 12x12 档用 6x8 左对齐, 视觉上略小但省字库 */
                return asc2_0806[ch - 0x20U];
            }
            if (cn_lookup(oled_cn_1212, 36U, ch, &data))
            {
                return data;
            }
            return asc2_0806[0];

        case OLED_FONT_16X16:
            if (w) { *w = 16U; }
            if (h) { *h = 16U; }
            if (ch < 0x80U)
            {
                return asc2_1608[ch - 0x20U];
            }
            if (cn_lookup(oled_cn_1616, 32U, ch, &data))
            {
                return data;
            }
            return asc2_1608[0];

        default:
            if (w) { *w = 6U; }
            if (h) { *h = 8U; }
            return asc2_0806[0];
    }
}

uint8_t oled_char_width(uint8_t ch, oled_font_t font)
{
    uint8_t w = 6U;

    (void)oled_font_get(ch, font, &w, NULL);
    return w;
}