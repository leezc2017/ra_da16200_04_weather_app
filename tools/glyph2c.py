#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
glyph2c.py — 把 TTF 字体渲染成 SSD1306 点阵字库 C 源文件

用途
----
天气站 OLED 要显示中文, 但不能手搓点阵。本工具用 Pillow 把 TTF 渲染成
1bpp 位图, 再打包成 oled_font.h 约定���查表格式:

    重复 { GBK首字节, 宽度, 点阵数据[stride] }, 以 GBK首字节 == 0 结束
    stride = size * size / 8   (12x12 -> 18? 不, 见下)

⚠ stride 约定必须与 src/oled/oled_font.c 的 cn_lookup() 一致。
   当前实现: 12x12 -> 36 字节 (每行 2 字节 x 12 行)
             16x16 -> 32 字节 (每行 2 字节 x 16 行)
   即"竖向 8 像素分组"的 SSD1306 常见排布, 每行向上对齐取 (size+7)/8*8 位。

用法
----
    python glyph2c.py --ttf simsun.ttf --size 12 --out ../src/oled/oled_cn_1212.c
    python glyph2c.py --ttf simsun.ttf --size 16 --out ../src/oled/oled_cn_1616.c

    # 只看字符集覆盖情况, 不生成文件
    python glyph2c.py --ttf simsun.ttf --chars "天气温度晴阴雨雪城市设置"

依赖
----
    pip install pillow

字体
----
Windows 自带 %WINDIR%\\Fonts\\simsun.ttc (宋体, GBK 完整)，
或任意含 CJK 字形的 TTF。用 --list-fonts 可看本机可用字体。
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("需要 Pillow: pip install pillow")


# ---------------------------------------------------------------- 查表格式

def stride_for(size: int) -> int:
    """每条字模的字节数: (size+7)//8 * size"""
    return ((size + 7) // 8) * size


def record_prefix_byte(gbk_first: int, width: int) -> bytes:
    """记录头: [GBK首字节, 宽度]"""
    return bytes([gbk_first, width])


# ---------------------------------------------------------------- 渲染

def render_glyph(font, ch: str, size: int) -> bytes:
    """渲染单字为竖向点阵字节序列

    布局: 每 size 行, 每行 (size+7)//8 字节, 位从低位到高位对应像素从左到右。
    """
    row_bytes = (size + 7) // 8
    img = Image.new("1", (size, size), 0)
    draw = ImageDraw.Draw(img)
    # 用 bbox 居中
    try:
        bbox = font.getbbox(ch)
    except AttributeError:
        bbox = (0, 0, size, size)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    draw.text(((size - w) // 2 - bbox[0], (size - h) // 2 - bbox[1]),
              ch, fill=1, font=font)

    out = bytearray()
    px = img.load()
    for y in range(size):
        acc = 0
        for x in range(size):
            if px[x, y]:
                acc |= 1 << x          # SSD1306: 低位在左
        out.append(acc & 0xFF)
        if row_bytes > 1:
            out.append((acc >> 8) & 0xFF)
    return bytes(out)


# ---------------------------------------------------------------- 生成

def build(ttf: str, size: int, chars: str, var_name: str, out: Path,
          title: str) -> int:
    font = ImageFont.truetype(ttf, size)
    stride = stride_for(size)

    body = []
    skipped = []
    for ch in chars:
        try:
            gbk = ch.encode("gbk")
        except UnicodeEncodeError:
            skipped.append(ch)
            continue
        if len(gbk) != 2:                # 跳过 ASCII 与单字节符号
            skipped.append(ch)
            continue
        lead = gbk[0]
        data = render_glyph(font, ch, size)
        assert len(data) == stride, f"{ch}: {len(data)} != {stride}"

        hexbytes = ",".join(f"0x{b:02X}" for b in data)
        # 用 GBK 注释标出是哪个字(注释以 GBK 存储, 读源码才正常)
        body.append(f"    {record_prefix_byte(lead, size)[0]:#04x},"
                    f"{size},"
                    f"{_wrap_hex(hexbytes)}"
                    f"  /* \"{ch}\" GBK={gbk[0]:02X}{gbk[1]:02X} */")

    src = [
        "/**",
        f" * @file  {out.name}",
        f" * @brief {title}",
        " *",
        " * 本文件由 tools/glyph2c.py 自动生成, 勿手改。",
        f" * 字体: {Path(ttf).name}  点阵: {size}x{size}  字符数: {len(body)}",
        " *",
        " * 存储格式: 重复 { GBK首字节, 宽度, 点阵数据[stride] }, 以首字节 0 结束。",
        f" * stride = {stride} 字节。查询逻辑见 oled_font.c 的 cn_lookup()。",
        " *",
        " * ⚠ 本文件以 GBK 无 BOM 保存(写文件用 encoding='gbk'), 否则汉字变乱码。",
        " */",
        '#include "oled_font.h"',
        "",
        f"const uint8_t {var_name}[] =",
        "{",
    ]
    src.extend(body)
    src.append("    0x00, 0x00,")
    src.append("};")
    src.append("")

    text = "\r\n".join(src)
    # 关键: GBK 无 BOM
    out.write_bytes(text.encode("gbk"))

    if skipped:
        print(f"[warn] 跳过 {len(skipped)} 个非 GBK 双字节字符: "
              f"{''.join(skipped[:40])}", file=sys.stderr)
    return len(body)


def _wrap_hex(hexstr: str, per_line: int = 12) -> str:
    """把长 hex 串按每行 12 个字节折行"""
    items = hexstr.split(",")
    lines = []
    for i in range(0, len(items), per_line):
        lines.append(",".join(items[i:i + per_line]))
    return ",\n      ".join(lines)


# ---------------------------------------------------------------- 字符集

# 天气站界面用到的全部汉字（按页面归类，便于校对）
CHARSET = (
    # 主页: 时间/温度/天气
    "时间温度天气晴阴云雨雪雾霾雷电冰雹大风"
    "晴朗多云阴天小雨大雨暴雨阵雨雷阵雨雪"
    "城市北京上海广州深圳杭州西安成都重庆"
    # 连接状态
    "连接中已连接断开失败重试信号网络配置"
    "联网等待超时错误成功"
    # 配网页
    "配网设置密码账号首页地址浏览手机"
    "请输入确认取消保存返回退出"
    # 按键/提示
    "按键短按长按双击刷新翻页按键长按进入"
    "湿度风速气压能见度空气质量"
    # 单位与符号
    "年月日时分秒°％℃米秒"
    "晴雨雪云"
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True, help="TTF/TTC 字体路径")
    ap.add_argument("--size", type=int, default=12, help="点阵尺寸 12 或 16")
    ap.add_argument("--chars", default=None,
                    help="自定义字符集(默认用内置天气站字符集)")
    ap.add_argument("--out", required=True, help="输出 .c 文件")
    ap.add_argument("--var", default=None,
                    help="数组名(默认按尺寸取 oled_cn_1212/oled_cn_1616)")
    a = ap.parse_args()

    size = a.size
    if size not in (12, 16):
        sys.exit("--size 只支持 12 或 16(与 oled_font.c 的 stride 约定一致)")

    var = a.var or f"oled_cn_{size}{size}"
    title = f"{size}x{size} 中文点阵字库"
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    n = build(a.ttf, size, a.chars or CHARSET, var, out, title)
    print(f"[ok] {out} : {n} 字, {out.stat().st_size} 字节, "
          f"stride={stride_for(size)}, 编码 GBK")


if __name__ == "__main__":
    main()