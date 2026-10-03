#include "hal_data.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "at_engine.h"
#include "at_probe.h"
#include "oled_port.h"
#include "oled/oled.h"
#include "oled/oled_font.h"
#include "weather_text.h"
#include "weather_ui.h"

#define TICK_MS          (10U)       /* 主循环节拍 */
#define USB_TX_RING_SIZE  (4096U)     /* USB TX 环形缓冲: printf 入队, 主循环泵发, 不阻塞 */
#define RX_RING_SIZE      (8192U)     /* WIFI RX 环形缓冲: 容 2KB+ 突发(WFSCAN 实测 ~1.9KB) */
#define RX_LINE_MAX       (256U)      /* 单行最大长度 */
#define WIFI_TX_WAIT_MS   (200U)      /* 透传发送等待 TX 完成的兜底超时 */

fsp_err_t err = FSP_SUCCESS;

unsigned char usb_uart_send_buff[100];
volatile bool usb_uart_send_complete_flag = false;

/* USB TX 三指针环形缓冲: __io_putchar 只入队(满则丢并计数), usb_tx_pump() 在主循环泵发。
 *
 * 修复前 __io_putchar 每字节忙等 TX_COMPLETE, 与 115200 入向速率零余量,
 * 导致 WIFI ring 溢出丢字节(实测 WFSCAN 回显 BSSID 缺段/粘行)。
 *
 * 必须三指针: 单/双指针无法区分"已入队给硬件"与"硬件已发完",
 * 会导致 pump 反复重发同一段(2026-10-03 实测: banner 无限重播)。
 *   wr   : 生产者, __io_putchar 往这写
 *   rd   : 下一段要交给硬件的起点; pump 提交成功后 rd 前进 len
 *   done : 硬件已发完的终点, [done, rd) 为在途区不可写; TX_COMPLETE 里置成 rd
 */
static volatile char   usb_tx_ring[USB_TX_RING_SIZE];
static volatile uint16_t usb_tx_wr   = 0;
static volatile uint16_t usb_tx_rd   = 0;
static volatile uint16_t usb_tx_done = 0;
static volatile bool    usb_tx_busy = false;
volatile uint32_t       usb_tx_drop_cnt = 0;

/* 接收环形缓冲: 中断写入 rx_wr_index, 主循环读取 rx_rd_index */
static volatile char rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_wr_index = 0;
static volatile uint16_t rx_rd_index = 0;

static char rx_line[RX_LINE_MAX];
static uint16_t rx_line_len = 0;

void usb_uart_cb (uart_callback_args_t * p_args)
{
    switch (p_args->event)
    {
        case UART_EVENT_TX_COMPLETE:
        {
            usb_uart_send_complete_flag = true;
            usb_tx_done = usb_tx_rd;           /* [.., rd) 已发完, 该段可被复用 */
            usb_tx_busy = false;               /* 允许 pump 下一段 */
            break;
        }
        case UART_EVENT_RX_CHAR:
        {
            uint16_t next = (uint16_t)((rx_wr_index + 1U) % RX_RING_SIZE);
            if (next != rx_rd_index)            /* 缓冲满则丢弃 */
            {
                rx_ring[rx_wr_index] = (char)p_args->data;
                rx_wr_index = next;
            }
            break;
        }
        default:
        {
            break;
        }
    }
}


unsigned char wifi_uart_send_buff[100];
volatile bool wifi_uart_send_complete_flag = false;

/* 诊断计数器(供 at_engine/at_probe 打印): 区分"TX 没发出去"与"模块没回" */
volatile uint32_t wifi_rx_byte_cnt  = 0;   /* wifi_uart_cb 收到的字节总数 */
volatile uint32_t wifi_tx_done_cnt = 0;   /* WIFI 口 TX 完成次数 */
volatile uint32_t wifi_rx_drop_cnt = 0;   /* WIFI RX 环满丢弃的字节数 */

/* WIFI 接收环形缓冲: 中断写入 wifi_rx_wr_index, 主循环读取 wifi_rx_rd_index */
static volatile char wifi_rx_ring[RX_RING_SIZE];
static volatile uint16_t wifi_rx_wr_index = 0;
static volatile uint16_t wifi_rx_rd_index = 0;

static char wifi_rx_line[RX_LINE_MAX];
static uint16_t wifi_rx_line_len = 0;


/* DA16200 WIFI 口回调: 发送完成标志 + 收到字符入环形缓冲 */
void wifi_uart_cb (uart_callback_args_t * p_args)
{
    switch (p_args->event)
    {
        case UART_EVENT_TX_COMPLETE:
        {
            wifi_uart_send_complete_flag = true;
            wifi_tx_done_cnt++;
            break;
        }
        case UART_EVENT_RX_CHAR:
        {
            uint16_t next = (uint16_t)((wifi_rx_wr_index + 1U) % RX_RING_SIZE);
            if (next != wifi_rx_rd_index)            /* 缓冲满则丢弃 */
            {
                wifi_rx_ring[wifi_rx_wr_index] = (char)p_args->data;
                wifi_rx_wr_index = next;
                wifi_rx_byte_cnt++;
            }
            else
            {
                wifi_rx_drop_cnt++;
            }
            break;
        }
        default:
        {
            break;
        }
    }
}

#ifdef __GNUC__                                 //串口重定向
    #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
    #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/* USB TX 泵: 有空位且上一段已发完就提交下一段(一次发整段, 不逐字节忙等) */
static void usb_tx_pump (void)
{
    uint16_t len;

    if (usb_tx_busy || (usb_tx_rd == usb_tx_wr))
    {
        return;
    }

    /* 只发到环末尾的连续段, 避免跨尾回绕(FSP 单次传输须连续) */
    if (usb_tx_wr > usb_tx_rd)
    {
        len = (uint16_t)(usb_tx_wr - usb_tx_rd);
    }
    else
    {
        len = (uint16_t)(USB_TX_RING_SIZE - usb_tx_rd);
    }

    if (FSP_SUCCESS != R_SCI_UART_Write(&g_usb_uart_ctrl,
                                        (uint8_t const *)&usb_tx_ring[usb_tx_rd], len))
    {
        /* 提交失败(如 FSP_ERR_IN_USE): 整段丢弃且不置 busy, 让 pump 下轮重试新数据。
         * rd/done 同步前进, 保证生产者不会被"永不 done"卡死成持续丢字节。 */
        usb_tx_done = (uint16_t)((usb_tx_rd + len) % USB_TX_RING_SIZE);
        usb_tx_rd   = usb_tx_done;
        return;
    }

    /* 成功: rd 前进 len, 该段进入在途区 [done, rd); TX_COMPLETE 时 done 追上 */
    usb_tx_rd = (uint16_t)((usb_tx_rd + len) % USB_TX_RING_SIZE);
    usb_tx_busy = true;
}

PUTCHAR_PROTOTYPE
{
        uint16_t next = (uint16_t)((usb_tx_wr + 1U) % USB_TX_RING_SIZE);

        if (next == usb_tx_done)               /* 在途区+未发区已满: 丢字节并计数, 绝不阻塞主循环 */
        {
            usb_tx_drop_cnt++;
        }
        else
        {
            usb_tx_ring[usb_tx_wr] = (char)ch;
            usb_tx_wr = next;
        }
        return ch;
}

int _write(int fd,char *pBuffer,int size)
{
    for(int i=0;i<size;i++)
    {
        __io_putchar(*pBuffer++);
    }
    return size;
}

int _read(int fd, char *pBuffer, int size)
{
    (void)fd;
    (void)pBuffer;
    (void)size;
    return 0;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

/* 通过 WIFI 串口发送一行数据, 没有以 \r\n 结尾则自动补上 */
static void wifi_line_send (const char * p_buf, uint16_t len)
{
    static char tx_buf[RX_LINE_MAX + 2];
    uint16_t tx_len = 0U;
    uint16_t wait;

    if (len > RX_LINE_MAX)
    {
        len = RX_LINE_MAX;
    }

    memcpy(tx_buf, p_buf, len);
    tx_len = len;

    if ((0U == tx_len) || (('\r' != tx_buf[tx_len - 1U]) && ('\n' != tx_buf[tx_len - 1U])))
    {
        tx_buf[tx_len++] = '\r';            /* 没有换行则自动补 \r\n */
        tx_buf[tx_len++] = '\n';
    }

    err = R_SCI_UART_Write(&g_wifi_uart_ctrl, (uint8_t *)tx_buf, tx_len);
    if (FSP_SUCCESS == err)
    {
        /* 兜底超时: 避免 TX 中断丢失时永久卡死主循环 */
        for (wait = 0U; (wait < WIFI_TX_WAIT_MS) && (false == wifi_uart_send_complete_flag); wait++)
        {
            R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MILLISECONDS);
        }
        wifi_uart_send_complete_flag = false;
    }
    else
    {
        printf("[ERR] wifi write, err=%d\r\n", (int)err);
    }
}

/* USB 输入的一行: 本地命令(AUTO/ABORT)处理, 否则转发给 WIFI 串口 */
static void rx_line_flush (void)
{
    if (rx_line_len > 0U)
    {
        rx_line[rx_line_len] = '\0';

        if (0 == strcmp(rx_line, "AUTO"))           /* 启动阶段1自动诊断 */
        {
            at_probe_start();
        }
        else if (0 == strcmp(rx_line, "AUTOB"))     /* 风险#4: 开机自动重连测试 */
        {
            at_probe_boot_start();
        }
        else if (0 == strcmp(rx_line, "ABORT"))     /* 中止诊断 */
        {
            at_probe_abort();
        }
        else if (at_probe_running())                /* 诊断期间屏蔽透传 */
        {
            printf("[USB] probe running, type ABORT to stop (input ignored)\r\n");
        }
        else
        {
            printf("[TX] %s\r\n", rx_line);         /* 输入命令回显到 USB */
            wifi_line_send(rx_line, rx_line_len);   /* 转发给 WIFI 串口 */
        }

        rx_line_len = 0U;
    }
}

/* 取出 USB 串口收到的数据, 收到换行符作为一行结束并转发给 WIFI 串口 */
static void usb_uart_rx_process (void)
{
    while (rx_rd_index != rx_wr_index)
    {
        char c = rx_ring[rx_rd_index];
        rx_rd_index = (uint16_t)((rx_rd_index + 1U) % RX_RING_SIZE);

        if ((c == '\r') || (c == '\n'))     /* 换行符作为结束标志 */
        {
            rx_line_flush();                /* 输出整行(\r\n 只输出一次) */
        }
        else if (rx_line_len < (RX_LINE_MAX - 1U))
        {
            rx_line[rx_line_len++] = c;
        }
        else                                /* 缓冲满, 先输出再存, 防止溢出 */
        {
            rx_line_flush();
            rx_line[rx_line_len++] = c;
        }
    }
}

static void wifi_rx_line_flush (void)
{
    if (wifi_rx_line_len > 0U)
    {
        wifi_rx_line[wifi_rx_line_len] = '\0';
        printf("[WIFI] %s\r\n", wifi_rx_line);
        wifi_rx_line_len = 0U;
    }
}


/* 取出串口收到的数据, 收到换行符作为一行结束并打印 */
static void wifi_uart_rx_process (void)
{
    while (wifi_rx_rd_index != wifi_rx_wr_index)
    {
        char c = wifi_rx_ring[wifi_rx_rd_index];
        wifi_rx_rd_index = (uint16_t)((wifi_rx_rd_index + 1U) % RX_RING_SIZE);

        at_engine_rx(c);                    /* 喂给 AT 引擎(状态机+URC识别) */

        if ((c == '\r') || (c == '\n'))     /* 换行符作为结束标志 */
        {
            wifi_rx_line_flush();                /* 输出整行(\r\n 只输出一次) */
        }
        else if (wifi_rx_line_len < (RX_LINE_MAX - 1U))
        {
            wifi_rx_line[wifi_rx_line_len++] = c;
        }
        else                                /* 缓冲满, 先输出再存, 防止溢出 */
        {
            wifi_rx_line_flush();
            wifi_rx_line[wifi_rx_line_len++] = c;
        }
    }
}




/* ===================== 字符集自检 =====================
 * 一次性验证 UTF-8 <-> GBK 转换与字库查表。阶段3 接 OLED 前的必要步骤:
 * 若 GBK 映射算错, 中文上屏会变成错字且不易定位, 在 PC 上先跑通最省事。
 * 串口打印一律 hex, 避免再次踩中"源码中文被 GBK 工具链读成乱码"的坑。 */

/**
 * @brief GBK 编码对照项: { Unicode 码点, 期望 GBK 两字节 }
 * 取常用字 + 边界字, 覆盖一级汉字首/中/尾
 */
typedef struct {
    int         cp;         /**< Unicode 码点 */
    uint8_t     gbk[2];     /**< 期望的 GBK 编码 */
} gbk_ref_t;

static const gbk_ref_t s_gbk_ref[] =
{
    { 0x4E00, {0xB0, 0xA1} },   /* 一   一级首 */
    { 0x4E2D, {0xB0, 0xAD} },   /* 中 */
    { 0x56FD, {0xB0, 0xB7} },   /* 国 */
    { 0x65F6, {0xB4, 0xB4} },   /* 时 */
    { 0x6E29, {0xB4, 0xCE} },   /* 温 */
    { 0x5929, {0xB4, 0xCC} },   /* 天 */
    { 0x6C14, {0xC6, 0xF8} },   /* 气 */
    { 0x98CE, {0xB7, 0xE7} },   /* 风 */
    { 0x96E8, {0xD2, 0xBC} },   /* 雨 */
    { 0x96EA, {0xD2, 0xC9} },   /* 雪 */
    { 0x6674, {0xC7, 0xED} },   /* 晴 */
    { 0x51AC, {0xB6, 0xC8} },   /* 冬 */
    { 0x9A6C, {0xEF, 0xCF} },   /* 麦 一级尾附近 */
    { 0x4E2D, {0x00, 0x00} },   /* 占位, 下方过滤 */
};

static void charset_selftest (void)
{
    const uint8_t * font = NULL;
    char utf8[64];
    char gbk[64];
    char back[64];
    uint32_t pass = 0U;
    uint32_t fail = 0U;
    uint32_t i;

    printf("[FONT] ==== charset selftest ====\r\n");

    /* --- 1) GBK 编码对照 --- */
    for (i = 0U; i < (sizeof(s_gbk_ref) / sizeof(s_gbk_ref[0])); i++)
    {
        uint8_t enc[2];
        uint32_t c;

        if (0x00 == s_gbk_ref[i].gbk[0])
        {
            continue;
        }

        /* cp -> UTF-8 (手工编码, 全部是 BMP 三字节) -> GBK 往返 */
        c = (uint32_t)s_gbk_ref[i].cp;
        utf8[0] = (char)(uint8_t)(0xE0U | ((c >> 12) & 0x0FU));
        utf8[1] = (char)(uint8_t)(0x80U | ((c >> 6) & 0x3FU));
        utf8[2] = (char)(uint8_t)(0x80U | (c & 0x3FU));
        utf8[3] = '\0';

        (void)oled_utf8_to_gbk(utf8, gbk, sizeof(gbk));
        if ((0U == (uint8_t)gbk[0]) || (0U == (uint8_t)gbk[1]))
        {
            enc[0] = 0U;
            enc[1] = 0U;
        }
        else
        {
            enc[0] = (uint8_t)gbk[0];
            enc[1] = (uint8_t)gbk[1];
        }

        if ((enc[0] == s_gbk_ref[i].gbk[0]) && (enc[1] == s_gbk_ref[i].gbk[1]))
        {
            pass++;
        }
        else
        {
            fail++;
            printf("[FONT] GBK cp=%04X expect=%02X%02X got=%02X%02X\r\n",
                   (unsigned)s_gbk_ref[i].cp,
                   s_gbk_ref[i].gbk[0], s_gbk_ref[i].gbk[1],
                   enc[0], enc[1]);
        }
    }
    printf("[FONT] gbk table : pass=%lu fail=%lu\r\n",
           (unsigned long)pass, (unsigned long)fail);

    /* --- 2) GBK -> UTF-8 往返一致性 --- */
    {
        uint32_t p2 = 0U;
        uint32_t f2 = 0U;

        for (i = 0U; i < (sizeof(s_gbk_ref) / sizeof(s_gbk_ref[0])); i++)
        {
            if (0x00 == s_gbk_ref[i].gbk[0])
            {
                continue;
            }
            gbk[0] = (char)s_gbk_ref[i].gbk[0];
            gbk[1] = (char)s_gbk_ref[i].gbk[1];
            gbk[2] = '\0';

            (void)oled_gbk_to_utf8(gbk, 2U, back, sizeof(back));
            (void)oled_utf8_to_gbk(back, gbk, sizeof(gbk));   /* 复用 gbk 作输出 */

            if (((uint8_t)gbk[0] == s_gbk_ref[i].gbk[0]) &&
                ((uint8_t)gbk[1] == s_gbk_ref[i].gbk[1]))
            {
                p2++;
            }
            else
            {
                f2++;
                printf("[FONT] roundtrip cp=%04X -> %02X%02X -> %02X%02X\r\n",
                       (unsigned)s_gbk_ref[i].cp,
                       s_gbk_ref[i].gbk[0], s_gbk_ref[i].gbk[1],
                       (uint8_t)gbk[0], (uint8_t)gbk[1]);
            }
        }
        printf("[FONT] gbk<->utf8: pass=%lu fail=%lu\r\n",
               (unsigned long)p2, (unsigned long)f2);
    }

    /* --- 3) UTF-8 长度/前进 --- */
    {
        /* "中A" = E4B8AD 41 -> 2 字符 */
        const char * s = "\xE4\xB8\xAD" "A";
        size_t n = oled_utf8_len(s);
        const char * p1 = oled_utf8_next(s);
        const char * p2 = oled_utf8_next(p1);

        printf("[FONT] utf8_len(E4B8AD 'A')=%lu (want 2)\r\n", (unsigned long)n);
        printf("[FONT] utf8_next: p1-s=%ld p2-s=%ld (want 3,4)\r\n",
               (long)(p1 - s), (long)(p2 - s));
    }

    /* --- 4) 字库查表 + 容量 --- */
    {
        uint8_t w = 0U;
        uint8_t h = 0U;

        font = oled_font_get((uint8_t)'A', OLED_FONT_6X8, &w, &h);
        printf("[FONT] asc 6x8 'A': w=%u h=%u ptr=%s\r\n",
               w, h, (NULL != font) ? "ok" : "NULL");

        font = oled_font_get((uint8_t)'A', OLED_FONT_16X8, &w, &h);
        printf("[FONT] asc 16x8 'A': w=%u h=%u ptr=%s\r\n",
               w, h, (NULL != font) ? "ok" : "NULL");

        /* 汉字 GBK 首字节 B0A1 = '一' 的首字节 B0 */
        font = oled_font_get(0xB0U, OLED_FONT_12X12, &w, &h);
        printf("[FONT] cn 12x12 0xB0: w=%u h=%u ptr=%s (0=empty lib)\r\n",
               w, h, (NULL != font) ? "ok" : "NULL");

        font = oled_font_get(0xB0U, OLED_FONT_16X16, &w, &h);
        printf("[FONT] cn 16x16 0xB0: w=%u h=%u ptr=%s (0=empty lib)\r\n",
               w, h, (NULL != font) ? "ok" : "NULL");
    }

    /* --- 5) 文案表抽查(ASCII, 应直接可用) --- */
    {
        printf("[FONT] text(0)=%s text(13)=%s text(999)=%s\r\n",
               weather_text_get(0), weather_text_get(13), weather_text_get(999));
    }

    /* --- 6) OLED 硬件 + UI 自检: 渲染 GD32 版的真实页面 --- */
    {
        ui_main_view_t mv;
        ui_wifi_view_t wv;

        oled_port_init();

        /* 启动画面(与 GD32 版一致) */
        ui_boot_splash();
        printf("[FONT] OLED ui_boot_splash drawn\r\n");

        /* 主页: 已联网形态。图标用 ICON_SUNNY=0，与 weather_text.h 对应 */
        memset(&mv, 0, sizeof(mv));
        (void)snprintf(mv.time_str, sizeof(mv.time_str), "12:34");
        (void)snprintf(mv.temp_str, sizeof(mv.temp_str), "26C");
        (void)snprintf(mv.wtext, sizeof(mv.wtext), "Sunny");
        mv.icon     = ICON_SUNNY;
        mv.offline  = 0U;
        ui_main_page(&mv, WIFI_STAT_CONNECTED);
        printf("[FONT] OLED ui_main_page(CONNECTED) drawn\r\n");

        /* WiFi 信息页: 6x8 小字 4 行 */
        wv.ssid  = "CMCC-xxxx";
        wv.ip    = "192.168.1.6";
        wv.fails = 0U;
        wv.page  = 0U;
        ui_wifi_page(&wv, WIFI_STAT_CONNECTED);
        printf("[FONT] OLED ui_wifi_page(CONNECTED) drawn\r\n");

        printf("[FONT] expect boot->'Lee Weather'/'Weather App'\r\n");
        printf("[FONT] expect main->'12:34 26C' + 'Sunny' + icon\r\n");
        printf("[FONT] expect wifi ->'WiFi info'/'S:CMCC-xxxx'/'IP:192.168.1.6'\r\n");
    }

    printf("[FONT] ==== done ====\r\n");
}

/* ===================== 初始化顺序 ===================== */

void hal_entry(void)
{
    /* Open the uart instances with initial configuration. */
    err = R_SCI_UART_Open(&g_usb_uart_ctrl, &g_usb_uart_cfg);
    assert(FSP_SUCCESS == err);

    err = R_SCI_UART_Open(&g_wifi_uart_ctrl, &g_wifi_uart_cfg);
    assert(FSP_SUCCESS == err);

    printf("\r\nRA E2STUDIO 4.weather_app\r\n");
    printf("USB input -> WIFI uart (auto CRLF), WIFI response back to USB\r\n");
    printf("Commands: AUTO = stage1 probe, AUTOB = boot-reconnect test, ABORT = stop\r\n");

    at_engine_init();
    charset_selftest();                       /* 字符集 + OLED 链路自检 */

    while (1)
    {
        /* 先搬 RX 再延时: 缩短字节在环里的驻留时间, 避免突发响应丢字节 */
        usb_uart_rx_process();               /* USB 输入转发给 WIFI 串口 */
        wifi_uart_rx_process();              /* WIFI 返回打印到 USB */
        usb_tx_pump();                       /* 泵发 USB TX 环(非阻塞打印的兑现点) */

        at_engine_tick(TICK_MS);             /* 引擎时基 */
        (void)at_engine_poll();              /* 异步发送/超时推进 */
        at_probe_poll();                     /* 诊断序列推进(空闲时速返) */

        R_BSP_SoftwareDelay(TICK_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
#if BSP_TZ_SECURE_BUILD
    /* Enter non-secure code */
    R_BSP_NonSecureEnter();
#endif
}
