/**
 * @file  at_engine.c
 * @brief DA16200 AT 命令引擎实现（异步两段式）
 *
 * 流: start(提交发送) -> SENDING(TX完成) -> WAIT(采集响应直到 OK/ERROR/超时)
 * 输入: at_engine_rx() 由 hal_entry 的 WIFI RX 路径逐字节注入。
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "at_engine.h"
#include "hal_data.h"
#include "weather_cfg.h"        /* AT_ENG_TRACE: 逐命令诊断开关(默认关) */

/* ⚠ 只暴露计数器, 不暴露"发送完成"标志（2026-10-04）。
 *   原来还有 wifi_uart_send_complete_flag, 引擎和 USB 转发两路共用它,
 *   后到者清零就把先到者的等待偷走, 导致 1000ms 后误判 TX 超时。
 *   现在各路记自己的计数器基线, 只增不减, 互不干扰。 */
extern volatile uint32_t wifi_rx_byte_cnt;          /* hal_entry.c: 诊断计数 */
extern volatile uint32_t wifi_tx_done_cnt;         /* hal_entry.c: TX 完成计数 */
extern volatile uint32_t wifi_rx_drop_cnt;         /* hal_entry.c: 诊断计数 */

typedef enum {
    ST_IDLE = 0,
    ST_SENDING,                             /* 等 UART TX 完成 */
    ST_WAIT                                 /* 采集响应直到终止/超时 */
} eng_state_t;

static struct {
    eng_state_t state;
    bool        submitted;                  /* R_SCI_UART_Write 已提交 */
    uint32_t    tx_mark;                     /* 提交瞬间的 wifi_tx_done_cnt, TX 完成的判据(见下) */
    uint32_t    send_t0;                    /* 发送起点(ms), 用于 TX 超时 */
    uint32_t    deadline;                   /* 响应截止(ms) */
    uint32_t    timeout_ms;
    uint32_t    now;

    char        cmd[AT_CMD_MAX];            /* 含自动补的 \r\n, NUL 结尾 */
    uint16_t    cmd_len;
    at_mode_t   mode;                       /* CMD / PAYLOAD(见 at_engine.h) */

    at_result_t final;                      /* 最近一次完成结果 */
    at_resp_t   resp;

    /* 终止行竞态修复(2026-10-03):
     * 主循环 10ms 才 poll 一次, 而 5 字节命令 ~430us 就发完、模块 ~1ms 就回 OK。
     * 若只在 ST_WAIT 里认终止行, OK 会在 ST_SENDING 期间到达而被丢弃 -> 必然超时。
     * 改为: 任何非 IDLE 状态收到终止行都记下, 由 poll 统一收口。 */
    bool        term_seen;
    at_result_t term_res;

    char        line[AT_LINE_MAX];          /* 行检测缓冲（含 +TRDTS 请求 URC） */
    uint16_t    line_len;
    bool        line_overflow;

    /* 行组装诊断(2026-10-03): 零开销计数器, 只在命令收口时汇总打印。
     * 用途: 区分"模块没发终止行"与"发了但行组装/匹配把它丢了"。
     *   dbg_lines  = 收到并完成组装的行数   (为 0 说明一个 \r\n 都没见)
     *   dbg_maxlen = 最长行长度             (== AT_LINE_MAX-1 说明触发行溢出)
     *   dbg_ovf    = 发生过行溢出的次数
     *   dbg_plus   = 以 '+' 开头的行数      (URC / +NWHTCSTATUS 走这条分支) */
    uint16_t    dbg_lines;
    uint16_t    dbg_maxlen;
    uint16_t    dbg_ovf;
    uint16_t    dbg_plus;

    /* 运行期累计(不随 start_ex 清零), 供 at_engine_diag() 心跳快照用。
     * 与上面 dbg_* 的区别: dbg_* 是"单条命令"的收口摘要, 这三个是"开机至今"。 */
    uint32_t    rx_total;
    uint32_t    line_total;
    uint32_t    plus_total;

    char        urc[AT_URC_FIFO][AT_URC_LINE];
    uint8_t     urc_head;
    uint8_t     urc_cnt;

    bool        trace;                            /* URC 打时间戳(被动监听) */
    uint32_t    trace_t0;

    /* sticky 环: 曾出现过的 URC, 与上面会被 start() 清空的消费 FIFO 完全独立,
     * 保证 45s 被动监听期内 URC 超过 FIFO 深度也不漏判(风险#4)。 */
    char        seen_buf[AT_SEEN_MAX][AT_URC_LINE];
    uint8_t     seen_cnt;
    at_engine_urc_callback_t urc_cb;
    void       * urc_context;
} s;

#define TX_SUBMIT_TIMEOUT_MS  1000U         /* 提交/发送超过 1s 判失败 */

/** @brief at_result_t -> 短字符串(收口诊断用) */
static const char * res_name (at_result_t r)
{
    switch (r)
    {
        case AT_RES_OK:      return "OK";
        case AT_RES_ERR:     return "ER";
        case AT_RES_TIMEOUT: return "TMO";
        case AT_RES_ABORT:   return "ABT";
        case AT_RES_BUSY:    return "BSY";
        default:             return "??";
    }
}

void at_engine_init(void)
{
    at_engine_urc_callback_t urc_callback = s.urc_cb;
    void * urc_context = s.urc_context;
    memset(&s, 0, sizeof(s));
    s.urc_cb = urc_callback;
    s.urc_context = urc_context;
    s.final = AT_RES_PENDING;
    s.mode = AT_MODE_CMD;
    s.resp.err_code = -1;
    s.resp.status = -1;
}

void at_engine_tick(uint32_t elapsed_ms)
{
    s.now += elapsed_ms;
}

void at_engine_diag(at_engine_diag_t * out)
{
    if (NULL == out)
    {
        return;
    }

    out->state      = (uint8_t)s.state;
    out->mode       = (ST_IDLE == s.state) ? (uint8_t)AT_MODE_CMD : (uint8_t)s.mode;
    out->urc_cnt    = s.urc_cnt;
    out->seen_cnt   = s.seen_cnt;
    out->line_len   = s.line_len;
    out->rx_bytes   = s.rx_total;
    out->lines      = s.line_total;
    out->plus_lines = s.plus_total;
}

uint32_t at_engine_now_ms(void)
{
    return s.now;
}

bool at_engine_idle(void)
{
    return (ST_IDLE == s.state);
}

/** @brief 当前模式(AT_MODE_CMD=0 / AT_MODE_PAYLOAD=1)。供串口提示用 */
uint8_t at_engine_mode(void)
{
    return (ST_IDLE == s.state) ? (uint8_t)AT_MODE_CMD : (uint8_t)s.mode;
}

const at_resp_t * at_engine_resp(void)
{
    return &s.resp;
}

static void urc_fifo_clear(void)
{
    s.urc_head = 0;
    s.urc_cnt = 0;
}

static void urc_push(const char * line)
{
    uint8_t idx;
    size_t n;

    if (s.urc_cnt < AT_URC_FIFO)
    {
        idx = (uint8_t)((s.urc_head + s.urc_cnt) % AT_URC_FIFO);
        s.urc_cnt++;
    }
    else                                        /* 满则丢最旧, 保最新 */
    {
        idx = s.urc_head;
        s.urc_head = (uint8_t)((s.urc_head + 1U) % AT_URC_FIFO);
    }

    n = strlen(line);
    if (n > (size_t)(AT_URC_LINE - 1U))
    {
        n = (size_t)(AT_URC_LINE - 1U);
    }
    memcpy(s.urc[idx], line, n);
    s.urc[idx][n] = '\0';

    if (s.trace)                                /* 被动监听: 逐条打时间戳 */
    {
        printf("[URC] +%6ums %s\r\n", (unsigned)(s.now - s.trace_t0), s.urc[idx]);
    }

    if (s.seen_cnt < AT_SEEN_MAX)                /* sticky 记录(满则丢最旧) */
    {
        memcpy(s.seen_buf[s.seen_cnt], s.urc[idx], AT_URC_LINE);
        s.seen_cnt++;
    }
    else
    {
        memmove(&s.seen_buf[0], &s.seen_buf[1], (size_t)(AT_SEEN_MAX - 1U) * AT_URC_LINE);
        memcpy(s.seen_buf[AT_SEEN_MAX - 1U], s.urc[idx], AT_URC_LINE);
    }
}

bool at_engine_urc_seen(const char * prefix)
{
    size_t plen = strlen(prefix);
    uint8_t i;

    for (i = 0; i < s.seen_cnt; i++)
    {
        if (0 == strncmp(s.seen_buf[i], prefix, plen))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief 匹配并消费一条 URC，同时把行内容拷贝出来
 *
 * 遍历 URC FIFO，找到第一个匹配 prefix 的槽，把该行拷贝到 out，
 * 然后像 take() 一样把该槽移除。返回行内容；未找到或缓冲太小则返回 false。
 */
bool at_engine_urc_get(const char * prefix, char * out, uint16_t out_size)
{
    size_t plen = strlen(prefix);
    uint8_t i;

    if ((NULL == prefix) || (NULL == out) || (0U == out_size))
    {
        return false;
    }

    for (i = 0; i < s.urc_cnt; i++)
    {
        uint8_t idx = (uint8_t)((s.urc_head + i) % AT_URC_FIFO);

        if (0 == strncmp(s.urc[idx], prefix, plen))
        {
            size_t n = strlen(s.urc[idx]);

            if (n >= out_size)
            {
                n = out_size - 1U;
            }
            memcpy(out, s.urc[idx], n);
            out[n] = '\0';

            /* 移除该槽（同 take） */
            {
                uint8_t j;
                for (j = i; j + 1U < s.urc_cnt; j++)
                {
                    uint8_t to = (uint8_t)((s.urc_head + j) % AT_URC_FIFO);
                    uint8_t with = (uint8_t)((s.urc_head + j + 1U) % AT_URC_FIFO);
                    memcpy(s.urc[to], s.urc[with], AT_URC_LINE);
                }
                s.urc_cnt--;
            }
            return true;
        }
    }

    return false;
}

void at_engine_urc_callback_set(at_engine_urc_callback_t callback, void *context)
{
    s.urc_cb = callback;
    s.urc_context = context;
}

void at_engine_urc_seen_clear(void)
{
    s.seen_cnt = 0;
}

void at_engine_urc_trace(bool on)
{
    s.trace = on;
    s.trace_t0 = s.now;
}

bool at_engine_payload_active(void)
{
    return ((AT_MODE_PAYLOAD == s.mode) && (ST_IDLE != s.state));
}

bool at_engine_urc_take(const char * prefix)
{
    size_t plen = strlen(prefix);
    uint8_t i;

    for (i = 0; i < s.urc_cnt; i++)
    {
        uint8_t idx = (uint8_t)((s.urc_head + i) % AT_URC_FIFO);

        if (0 == strncmp(s.urc[idx], prefix, plen))
        {
            uint8_t j;
            for (j = i; j + 1U < s.urc_cnt; j++)        /* 移除该槽 */
            {
                uint8_t to = (uint8_t)((s.urc_head + j) % AT_URC_FIFO);
                uint8_t from = (uint8_t)((s.urc_head + j + 1U) % AT_URC_FIFO);
                memcpy(s.urc[to], s.urc[from], AT_URC_LINE);
            }
            s.urc_cnt--;
            return true;
        }
    }

    return false;
}

at_result_t at_engine_start(const char * cmd, uint32_t timeout_ms)
{
    return at_engine_start_ex(cmd, timeout_ms, AT_MODE_CMD);
}

at_result_t at_engine_start_ex(const char * cmd, uint32_t timeout_ms, at_mode_t mode)
{
    size_t n;

    if (ST_IDLE != s.state)
    {
        return AT_RES_BUSY;
    }

    n = strlen(cmd);
    if ((0U == n) || (n > (size_t)(AT_CMD_MAX - 2U)))
    {
        return AT_RES_ERR;
    }

    memcpy(s.cmd, cmd, n);
    s.cmd_len = (uint16_t)n;

    if (('\r' != s.cmd[s.cmd_len - 1U]) && ('\n' != s.cmd[s.cmd_len - 1U]))
    {
        s.cmd[s.cmd_len++] = '\r';              /* 没有行结束则自动补 \r\n */
        s.cmd[s.cmd_len++] = '\n';
    }

    s.cmd[s.cmd_len] = '\0';

    s.resp.len = 0;
    s.resp.overflow = false;
    s.resp.err_code = -1;
    s.resp.status = -1;
    s.mode = mode;
    s.line_len = 0;
    s.line_overflow = false;
    s.final = AT_RES_PENDING;
    s.submitted = false;
    s.term_seen = false;
    s.term_res = AT_RES_PENDING;
    s.dbg_lines = 0U;
    s.dbg_maxlen = 0U;
    s.dbg_ovf   = 0U;
    s.dbg_plus  = 0U;
    s.send_t0 = s.now;
    s.timeout_ms = timeout_ms;
    urc_fifo_clear();                           /* 防止陈旧 URC 误匹配后续等待 */

    s.state = ST_SENDING;

    printf("[AT>] %s", s.cmd);                   /* cmd 已以 \r\n 结尾 */

    return AT_RES_PENDING;
}

/* 行结束处理: 终止行 / URC 检测 */
static void line_done(void)
{
    s.line[s.line_len] = '\0';

    /* 行组装诊断计数(零开销, 详见结构体注释) */
    s.dbg_lines++;
    s.line_total++;
    if (s.line_len > s.dbg_maxlen)
    {
        s.dbg_maxlen = s.line_len;
    }
    if (s.line_overflow)
    {
        s.dbg_ovf++;
    }
    if ('+' == s.line[0])
    {
        s.dbg_plus++;
        s.plus_total++;
    }

    if (s.line_overflow)
    {
        return;
    }

    if (0 == strcmp(s.line, "OK"))
    {
        /* PAYLOAD 模式: OK 只是同步回执, 正文还在流, 不能终止 */
        if ((AT_MODE_CMD == s.mode) && (ST_IDLE != s.state))
        {
            s.term_seen = true;
            s.term_res  = AT_RES_OK;
        }
    }
    else if ((ST_IDLE != s.state) && (0 == strcmp(s.line, "DONE")))
    {
        /* AT+RESTART reports DONE before reboot; use it as its command ACK. */
        s.term_seen = true;
        s.term_res = AT_RES_OK;
    }
    else if (0 == strncmp(s.line, "ERROR", 5))
    {
        if (ST_IDLE != s.state)                 /* 两种模式都立即终止: 不会有正文 */
        {
            const char * dash = strchr(s.line, '-');

            s.resp.err_code = (NULL != dash) ? atoi(dash + 1) : -1;
            s.term_seen = true;
            s.term_res  = AT_RES_ERR;
        }
    }
    else if (0 == strncmp(s.line, "+NWHTCSTATUS", 12))
    {
        const char * colon = strchr(s.line, ':');

        /* base 0: 同时兼容 "0" 与 "0x00" 两种写法 */
        if (NULL != colon)
        {
            s.resp.status = (int)strtol(colon + 1, NULL, 0);
        }

        if ((AT_MODE_PAYLOAD == s.mode) && (ST_IDLE != s.state))
        {
            s.term_seen = true;
            s.term_res  = (0 == s.resp.status) ? AT_RES_OK : AT_RES_ERR;
        }

        urc_push(s.line);
    }
    else if (0 == strncmp(s.line, "+TRDTS:", 7U))
    {
        /* TCP request payloads are passed in-band to the owner instead of the
         * short AT command response buffer. Length-delimited data may be split
         * into multiple +TRDTS URCs, so don't overwrite one with the next. */
        if (NULL != s.urc_cb)
        {
            s.urc_cb(s.line, s.line_len, s.urc_context);
        }
    }
    else if ('+' == s.line[0])
    {
        urc_push(s.line);                       /* URC: 空闲与在途都收 */
    }
}

void at_engine_rx(char c)
{
    s.rx_total++;

    /* 1) 原始字节采集(命令在途期间, 含 SENDING 尾部与 WAIT) */
    if (ST_IDLE != s.state)
    {
        if (s.resp.len < (uint16_t)AT_RESP_BUF_SIZE)
        {
            s.resp.buf[s.resp.len++] = c;
        }
        else
        {
            s.resp.overflow = true;
        }
    }

    /* 2) 行组装(始终进行, 空闲期也要识别 URC) */
    if (('\r' == c) || ('\n' == c))
    {
        if (s.line_len > 0U)
        {
            line_done();
            s.line_len = 0;
            s.line_overflow = false;
        }
    }
    else if (s.line_len < (AT_LINE_MAX - 1U))
    {
        s.line[s.line_len++] = c;
    }
    else
    {
        s.line_overflow = true;                 /* 超长行截断, 不参与终止检测 */
    }
}

/* 超时诊断: 区分"TX 没发出去"(SENDING)与"TX 发出去了但模块没回"(WAIT)。
 * 与逐命令收口摘要一样受 AT_ENG_TRACE 控制: 冷启动自检超时是常态,
 * 每条都打会把启动日志顶出串口窗口。 */
static void eng_timeout (const char * stage)
{
#if (AT_ENG_TRACE > 0)
    printf("[ENG] TIMEOUT@%s  wifi_rx=%lu wifi_txdone=%lu rxdrop=%lu\r\n",
           stage, (unsigned long)wifi_rx_byte_cnt, (unsigned long)wifi_tx_done_cnt,
           (unsigned long)wifi_rx_drop_cnt);
#else
    (void)stage;
#endif
}

at_result_t at_engine_poll(void)
{
    /* 进入时已空闲 -> 本次 poll 不做任何状态迁移, 不能打收口摘要。
     * ⚠ 少了这个判断, 空闲期每 tick 都会打印 final(上一次结果),
     *   10ms 一行把 USB TX 环刷爆, 真正的 [WIFI]/[wx] 日志全被挤掉
     *   (表现为"程序不动了"), usb_tx_drop_cnt 也会一路上涨。 */
    const bool was_idle = (ST_IDLE == s.state);

    switch (s.state)
    {
        case ST_SENDING:
        {
            if (s.term_seen)                    /* 终止行已到: 优先收口 */
            {
                s.state = ST_IDLE;
                s.final = s.term_res;
                break;
            }

            if (!s.submitted)
            {
                /* ⚠ 用**计数器**而不是共享 bool 判 TX 完成（2026-10-04 修）。
                 *
                 * 原来这里等的是全局 wifi_uart_send_complete_flag，而
                 * wifi_line_send()（USB 输入转发给 WIFI 口那条路）会
                 * 忙等这个标志然后**把它清掉**。两个消费者共用一个
                 * 无归属的标志 => 谁后到谁清掉，引擎的等待被偷走，
                 * 于是 1000ms 后 TX_SUBMIT_TIMEOUT_MS 判超时，一条命令
                 * 直接 TIMEOUT。这也解释了"串口敲命令从来没反应"：
                 * 敲进去的字节既转发去撞引擎，又把引擎的等待清了。
                 *
                 * 现在改成"Write 之前记下计数器基线，计数器变了就算完成"。
                 * 计数器只增不减，两路互不干扰，也不需要清零。
                 * 基线必须在 Write **之前**取：若在其后取，而 TX_COMPLETE
                 * 中断恰好在两者之间到达，基线会把那次完成吞掉，
                 * 引擎就会一直等到 1000ms 超时。 */
                uint32_t mark = wifi_tx_done_cnt;

                if (FSP_SUCCESS == R_SCI_UART_Write(&g_wifi_uart_ctrl,
                                                    (uint8_t const *)s.cmd,
                                                    s.cmd_len))
                {
                    s.submitted = true;
                    s.send_t0 = s.now;
                    s.tx_mark  = mark;
                }
                else if ((s.now - s.send_t0) > TX_SUBMIT_TIMEOUT_MS)
                {
                    printf("[ENG] WRITE REJECTED (FSP err) cmd='%s'\r\n", s.cmd);
                    s.state = ST_IDLE;
                    s.final = AT_RES_TIMEOUT;
                }
            }
            else if (wifi_tx_done_cnt != s.tx_mark)
            {
                s.state = ST_WAIT;
                s.deadline = s.now + s.timeout_ms;
            }
            else if ((s.now - s.send_t0) > TX_SUBMIT_TIMEOUT_MS)
            {
                eng_timeout("SENDING");         /* TX 完成中断没来 */
                s.state = ST_IDLE;
                s.final = AT_RES_TIMEOUT;
            }
            break;
        }

        case ST_WAIT:
        {
            if (s.term_seen)
            {
                s.state = ST_IDLE;
                s.final = s.term_res;
                break;
            }

            if (s.now >= s.deadline)
            {
                eng_timeout("WAIT");            /* 发出去了, 模块没回 */
                s.state = ST_IDLE;
                s.final = AT_RES_TIMEOUT;
            }
            break;
        }

        default:
        {
            break;
        }
    }

    /* 收口摘要: 仅在本次 poll 把状态从"在途"推进到 IDLE 的那一 tick 打印一次。
     * 由 AT_ENG_TRACE 控制(默认关, 见 weather_cfg.h: 每条命令一行会把启动日志
     * 顶出串口窗口)。判读:
     *   lines=0        -> 一个 \r\n 都没组装出来(收不到数据/收字节路径断)
     *   max=255        -> 触发行溢出(AT_LINE_MAX-1), 超长行不参与终止检测
     *   plus=0         -> 没收到任何 '+' 开头行, +NWHTCSTATUS 没进来
     *   plus>0 且 nw=-1 -> '+' 行有但 status 没解析出来
     *   ovfbuf=1       -> 4KB 响应缓冲溢出(正文被截) */
    /* ⚠ 这条**不受 AT_ENG_TRACE 控制**，永远打印。
     *
     * 2026-10-04 排查"收到响应但不解析"卡了很久：日志窗口约 44 行，
     * 而天气响应期间 [WIFI] 回显已关，所以这条收口信息是
     * "first fetch issued" 之后的第一条可见输出，必须保证它一定出现。
     * 它直接回答"引擎到底有没有把 NWHTC 命令收口、收到多少字节"。
     */
    if ((!was_idle) && (ST_IDLE == s.state) &&
        (AT_RES_PENDING != s.final) && (AT_MODE_PAYLOAD == s.mode))
    {
        printf("[NWHTC] closed res=%s nw=%d rxlen=%u lines=%u plus=%u ovfbuf=%u\r\n",
               res_name(s.final), s.resp.status, (unsigned)s.resp.len,
               (unsigned)s.dbg_lines, (unsigned)s.dbg_plus,
               s.resp.overflow ? 1U : 0U);
    }

#if (AT_ENG_TRACE > 0)
    if ((!was_idle) && (ST_IDLE == s.state) && (AT_RES_PENDING != s.final))
    {
        printf("[ENG] close=%s lines=%u max=%u ovf=%u plus=%u nw=%d len=%u ovfbuf=%u\r\n",
               res_name(s.final),
               (unsigned)s.dbg_lines, (unsigned)s.dbg_maxlen,
               (unsigned)s.dbg_ovf,    (unsigned)s.dbg_plus,
               s.resp.status, (unsigned)s.resp.len,
               s.resp.overflow ? 1U : 0U);
    }
#endif

    return (ST_IDLE == s.state) ? s.final : AT_RES_PENDING;
}

void at_engine_abort(void)
{
    s.state = ST_IDLE;
    s.final = AT_RES_ABORT;
    s.submitted = false;
    s.term_seen = false;
}

uint32_t at_engine_rx_len(void)
{
    return s.resp.len;
}
