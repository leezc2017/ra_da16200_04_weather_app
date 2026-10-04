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
#include "weather_task.h"
#include "key_scan.h"

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

/* USB 输入的一行: 本地命令(KEY/HB/AUTO/ABORT)处理, 否则转发给 WIFI 串口
 *
 * ⚠ KEY / HB 是为"串口窗口只看得到第一屏"设计的。
 *   串口助手一般只显示约 20~25 行, 而心跳要 5s 才出现、按键事件要等用户动手,
 *   两者都落在窗口之外 —— 实测连续七次抓取都只截到启动阶段的 AT+NWSNTP,
 *   因此按键/心跳**从来没出现在任何一次粘贴的日志里**, 无法据此判断。
 *   这两个命令由用户主动敲下, 响应直接追加到屏幕**底部**(当前可见区),
 *   完全绕开窗口大小限制。
 */
static void rx_line_flush (void)
{
    if (rx_line_len > 0U)
    {
        rx_line[rx_line_len] = '\0';

        if (0 == strcmp(rx_line, "KEY"))            /* 立即打印两个键的原始电平 */
        {
            uint8_t k1 = 0U;
            uint8_t k2 = 0U;

            key_raw_levels(&k1, &k2);
            printf("[KEY] P005 raw=%u  P006 raw=%u   (idle=1 1 / pressed=0)\r\n",
                   (unsigned)k1, (unsigned)k2);
            printf("[KEY] hold a key and type KEY again to see it change\r\n");
        }
        else if (0 == strcmp(rx_line, "HB"))        /* 立即打印一次心跳快照 */
        {
            weather_task_heartbeat_force();
        }
        else if (0 == strcmp(rx_line, "AUTO"))      /* 启动阶段1自动诊断 */
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

/* ===================== 初始化顺序 ===================== */

void hal_entry(void)
{
    /* Open the uart instances with initial configuration. */
    err = R_SCI_UART_Open(&g_usb_uart_ctrl, &g_usb_uart_cfg);
    assert(FSP_SUCCESS == err);

    err = R_SCI_UART_Open(&g_wifi_uart_ctrl, &g_wifi_uart_cfg);
    assert(FSP_SUCCESS == err);

    /* ⚠ 编译时间/版本：每次改动后第一眼就能确认"烧的是不是最新固件"。
     * 格式固定为 "2026-10-04 12:34:56"，可直接和 Git 提交时间对照。
     * __DATE__ / __TIME__ 由编译器展开，改了源码就会变 —— 若烧录后
     * 这行没变，说明烧的确实不是这版固件（这是排查过多次的问题）。 */
    printf("\r\nRA E2STUDIO 4.weather_app  build %s %s\r\n", __DATE__, __TIME__);
    printf("USB input -> WIFI uart (auto CRLF), WIFI response back to USB\r\n");
    printf("Commands: AUTO = stage1 probe, AUTOB = boot-reconnect test, ABORT = stop\r\n");
    printf("Local: KEY = key pin levels now, HB = heartbeat snapshot now\r\n");

    at_engine_init();
    oled_port_init();
    weather_task_init();                      /* 阶段2: 天气主状态机 */

    while (1)
    {
        /* 先搬 RX 再延时: 缩短字节在环里的驻留时间, 避免突发响应丢字节 */
        usb_uart_rx_process();               /* USB 输入转发给 WIFI 串口 */
        wifi_uart_rx_process();              /* WIFI 返回打印到 USB */
        usb_tx_pump();                       /* 泵发 USB TX 环(非阻塞打印的兑现点) */

        at_engine_tick(TICK_MS);             /* 引擎时基 */

        /* ⚠ 引擎轮询只有一个调用点（DESIGN §4 单飞行）：
         * 诊断模式走 at_probe_poll（内部自己轮询引擎），
         * 正常模式走 weather_task_poll（内部自己轮询引擎）。
         * 二者互斥，由 at_probe_running() 判定，绝不能同时轮询。 */
        if (at_probe_running())
        {
            at_probe_poll();                 /* AUTO / AUTOB 诊断中 */
        }
        else
        {
            weather_task_poll();             /* 正常天气状态机 */
        }

        R_BSP_SoftwareDelay(TICK_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
#if BSP_TZ_SECURE_BUILD
    /* Enter non-secure code */
    R_BSP_NonSecureEnter();
#endif
}
