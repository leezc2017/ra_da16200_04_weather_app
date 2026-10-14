/**
 * @file  weather_task.c
 * @brief 天气主状态机：联网 / 对时 / 天气轮询 / OLED 刷新 / 按键
 *
 * 移植自 GD32VW553 版 4.weather_app/src/weather_task.c。
 *
 * ============================ 与 GD32 版的差异 ============================
 * 1. 调度：FreeRTOS 任务 -> 裸机 10ms tick（weather_task_poll）
 * 2. 联网：wifi_prov_start() 阻塞 -> **被动等 +WFJAP URC**（实测上电自发）
 * 3. SNTP：lwip sntp_start_once() -> AT+NWSNTP=1,<server>,86400
 * 4. 时间：内部 s_epoch_base + gmtime() -> **每分钟查一次 AT+TIME**
 *    （RA 不用内部 RTC，DA16200 持有 RTC；实测 +TIME:2026-10-03,11:30:51）
 * 5. HTTP：weather_http_get() -> AT+NWHTC PAYLOAD 模式
 * 6. 按键：GD32 GPIO -> R_IOPORT_PinRead，事件改用 FIFO（§8.1 根因1）
 *
 * ============================ 引擎轮询权（关键设计约束） ============================
 * DESIGN §4 要求"同一时刻只允许一条命令在途"。因此 at_engine_poll() 在整个
 * 工程里**只有一个调用点**，就是本文件的 weather_task_poll()。
 * weather_http 不自己轮询引擎，而是靠 weather_http_on_engine_done() 被动接收结果。
 * 否则两处轮询同一引擎会互相抢状态机，表现为"命令随机丢失"。
 *
 * 命令归属(cmd_owner_t)决定引擎完成后把结果发给谁。
 */
#include <stdio.h>
#include <string.h>

#include "weather_task.h"
#include "weather_cfg.h"

#include "at_engine.h"
#include "weather_http.h"
#include "weather_parse.h"
#include "weather_text.h"
#include "weather_ui.h"
#include "key_scan.h"
#include "oled_port.h"

/** 命令归属：决定引擎结果交给谁处理 */
typedef enum {
    CMD_NONE = 0,      /**< 无命令在途 */
    CMD_AT,            /**< AT 自检: 启动时先验证模块响应 */
    CMD_TIME,          /**< AT+TIME */
    CMD_TZSET,         /**< AT+TZONE=<sec> */
    CMD_SNTP,          /**< AT+NWSNTP=... */
    CMD_NWIP,          /**< AT+NWIP=?: 读接口 IP（只用于取地址，不用于判断连接） */
    CMD_WFSTA,         /**< AT+WFSTA: 权威连接状态（手册 L1149：+WFSTA:1 已关联 / 0 未关联） */
    CMD_WFCAP,         /**< AT+WFCAP: 用 NVRAM 已存 profile 连接 */
    CMD_WFJAPA,        /**< AT+WFJAPA: 连接并写入 NVRAM */
    CMD_HTTP_NOW,      /**< AT+NWHTC now.json */
    CMD_HTTP_DAILY     /**< AT+NWHTC daily.json */
} cmd_owner_t;

/**
 * @brief 联网手段阶梯（2026-10-03 实测 + 修正）
 *
 * ⚠ 修正过一次错误结论。初版以为"模块不自连，因为 ERROR:-7 = NVRAM 无 profile"，
 *   但用户指出**之前确实拉到了天气**，且那次 `+WFJAP:1` 是开机自发出现的 ——
 *   说明 profile 是有的、模块是会自连的。
 *
 * 真实情况：`+WFJAP:1` 是**上电一次性 URC**。如果 DA16200 模块没跟着复位
 * （只复位了 RA 板子），它在 RA 起来之前就已经把这条 URC 发完了，
 * RA 永远等不到。而此时模块其实**已经连着 AP**，只是 RA 不知道。
 * `AT+WFCAP` 在"已连接"状态下报错，也容易被误读成"无 profile"。
 *
 * 所以阶梯的第一原则是**主动查询**，而不是被动等一次性 URC：
 *   0. NWIP_QUERY —— AT+NWIP=? 直接问"你现在的 IP 是多少"。
 *                   模块已连上就直接拿到 IP 收工，这是最快的路径，
 *                   也免疫 URC 错过的问题。回 ERROR/-1 表示没连上，再往下走。
 *   1. WAIT_URC   —— 短等 +WFJAP:1：兼容"RA 先起、模块后起"的冷启动顺序。
 *   2. WFCAP      —— 让模块用 NVRAM 里已存的 profile 连。
 *   3. WFJAPA     —— 显式带 SSID/PWD 连（并写 NVRAM），最兜底的手段。
 *
 * 阶段2 直接用 weather_cfg.h 里硬编码的 SSID/PWD 走第 3 步；
 * 阶段4 配网完成后 profile 已存，第 0/2 步就会命中。
 */
#define WIFI_STEP_NWIP_QUERY 0 /**< AT+NWIP=? 主动查 IP（最快路径） */
#define WIFI_STEP_WAIT_URC   1 /**< 短等 +WFJAP:1 URC */
#define WIFI_STEP_WFCAP      2 /**< 用 NVRAM profile 连 */
#define WIFI_STEP_WFJAPA     3 /**< 显式带 SSID/PWD 连接并写 NVRAM */

/** 最高阶梯连续失败这么多次后, 退回到第一级重跑整条链 */
#define WIFI_WFJAPA_MAX_TRY  3U

/**
 * @brief 同一个阶梯状态最多隔多少次重试再打一次日志
 *
 * 阶梯每 WEATHER_CONNECT_TIMEOUT_MS(8s) 跳一级，无限重试时会刷出成百上千行
 * 几乎相同的文本。实测一次失败的抓取里，光 "WFJAPA failed" 就占了 20+ 行，
 * 把真正有意义的状态迁移埋掉了。同签名只打第一遍，之后每 N 次打一行"还在原地"。
 */
#define LADDER_LOG_REPEAT    10U

/**
 * @brief AT+TZONE 最多重试几次后放弃对时
 *
 * ⚠ 这条是为了打破"初始化三段式互相阻塞"而加的（2026-10-12）。
 *
 * 原调度是一条严格串行链：
 *   TZONE 成功 -> SNTP 成功 -> 才发天气请求
 * 任一段失败都会把后续全部堵死。TZONE 只有 2500ms 超时，模块冷启动
 * 响应慢时很容易超时，于是天气永远取不到。
 *
 * 时区/对时只是显示层的锦上添花，不该阻塞天气数据。所以失败够多次就
 * 直接把 timezone_set 置 true 当作"已处理"，让链路继续往下走。
 */
#define TZ_MAX_FAIL          3U

/* ===================== 状态 ===================== */

static struct {
    /* 联网 */
    wifi_status_t status;
    uint32_t      connect_t0;

    /* 对时 */
    uint32_t time_poll_ms;      /**< 上次查 AT+TIME 的时刻 */
    bool    time_valid;         /**< 已拿到过有效时间 */
    bool    timezone_set;       /**< AT+TZONE 已成功下发 */
    bool    sntp_done;          /**< AT+NWSNTP 已投递 */
    bool    time_check_pending;  /**< SNTP 后延时验证 AT+TIME */
    uint32_t time_check_due_ms;  /**< SNTP 后验证时间的截止时刻 */
    bool    refresh_pending;     /**< 按键请求的刷新，网络不可用时保留 */
    uint8_t tz_fail;            /**< TZONE 连续失败次数, 到 TZ_MAX_FAIL 就不再堵链路 */
    char    time_str[6];        /**< 最近一次 AT+TIME 的 "HH:MM" */

    /* 天气 */
    weather_data_t wx;
    bool           wx_valid;
    uint32_t       wx_age_ms;

    /* UI */
    uint8_t  page;              /**< 0=主页 1=WiFi信息 */
    bool     ui_dirty;
    char     ip[16];
    char     ssid[33];
    uint8_t  fails;

    /* 命令归属 */
    cmd_owner_t cmd;

    /* 联网阶梯(见 WIFI_STEP_* 注释) */
    uint8_t  wifi_step;
    uint8_t  wifi_sta_ok;        /**< AT+WFSTA 最近一次回答: 1=已关联 0=未关联 */
    uint16_t ladder_sig;         /**< 已打印的 (step<<8|err)，用于日志限流 */
    uint8_t  ladder_nprint;      /**< 同一签名已重复打印次数 */
    uint8_t  wifi_try;            /**< 当前阶梯已尝试次数, 用于退避 */

    /* 心跳诊断(WEATHER_HB_DIAG_MS=0 时不打印) */
    uint32_t hb_t0;
    uint32_t hb_fast;      /**< fetch 在途时的 1s 心跳时基 */
    uint32_t fetch_t0;     /**< 本次 fetch 发起时刻, 供 OLED 显示已等秒数 */
    uint32_t fetch_sec;    /**< 上次刷新的已等秒数, 变了就置 ui_dirty 重绘 */
} s;

/* hal_entry.c 的诊断计数器(判断日志/数据是否被丢) */
extern volatile uint32_t usb_tx_drop_cnt;
extern volatile uint32_t wifi_rx_drop_cnt;
extern volatile uint32_t wifi_rx_byte_cnt;
extern volatile uint32_t diag_main_loops;
extern volatile uint32_t diag_wifi_rx_processed;
extern volatile uint32_t diag_usb_rx_processed;
extern volatile uint8_t diag_main_stage;
extern volatile uint32_t wifi_tx_done_cnt;

/**
 * @brief 低频心跳：确认主循环还活着、且引擎在收数据
 *
 * 定位"程序不动了"时非常有用：如果本行在刷新而 [WIFI] 不刷新，
 * 说明是 WIFI 口没数据；连本行都不刷新，才是主循环真的卡住。
 */
static void hb_print (uint32_t now);

/**
 * @brief 最近一次天气抓取的结论（缓存，等价于 [WX-RESULT] 那一行）
 *
 * 为什么缓存（2026-10-04）：
 *   [WX-RESULT] 是一次性打印，落在串口窗口外就再也拿不到，而心跳要等
 *   5s、fetch 在途时才有 1s 心跳，都不可靠。缓存之后**无论窗口多小**，
 *   心跳行里必然带着结论；OLED 也能直接显示，**完全不需要看日志**。
 *
 * 这样就不再需要"等多久再抓日志"这种提问 —— 结果是被动的、随时可读的。
 */
typedef enum {
    WXR_NONE = 0,      /**< 还没拉过 */
    WXR_OK,            /**< 解析成功 */
    WXR_HTTP_ERR,      /**< HTTP 状态非 200 */
    WXR_PARSE_ERR,     /**< 拿到 body 但 cJSON 解析失败 */
    WXR_TRANSPORT_ERR  /**< 引擎超时/错误 */
} wx_res_t;

static struct {
    wx_res_t kind;
    int      http;                 /**< HTTP 状态码, -1 = 未取到 */
    int      nw;                   /**< +NWHTCSTATUS 的 n, -1 = 未收到 */
    uint16_t body_len;
    int      temperature;
    uint8_t  code;                 /**< 心知天气 code */
    char     text[24];             /**< 英文文案(ASCII, 上屏用) */
    char     api_text[40];         /**< API 原文(UTF-8, 仅供串口查看) */
    uint32_t at_ms;                /**< 结论产生的引擎时刻 */
} s_wxr;

const char * weather_task_last_result (void)
{
    switch (s_wxr.kind)
    {
        case WXR_NONE:          return "no fetch yet";
        case WXR_OK:            return "OK";
        case WXR_HTTP_ERR:      return "HTTP ERR";
        case WXR_PARSE_ERR:     return "PARSE ERR";
        case WXR_TRANSPORT_ERR: return "TRANSPORT ERR";
        default:                return "?";
    }
}

const char * weather_task_last_text (void)
{
    return (s_wxr.text[0] != '\0') ? s_wxr.text : "--";
}

int weather_task_last_temp (void)
{
    return s_wxr.temperature;
}

/** 供 OLED 显示用：把最近结论拼成一行短文本（ASCII, 128x32 放得下） */
void weather_task_result_line (char * buf, uint16_t size)
{
    snprintf(buf, size, "%s %dC", weather_task_last_result(), s_wxr.temperature);
    if ((WXR_OK != s_wxr.kind) && (0 != s_wxr.nw))
    {
        snprintf(buf, size, "%s nw=%d", weather_task_last_result(), s_wxr.nw);
    }
}

void weather_task_heartbeat_force (void)
{
    hb_print(at_engine_now_ms());
}

/** @brief wifi_status_t -> 短名(ASCII, 便于日志 grep) */
static const char * wifi_st_name (wifi_status_t st)
{
    switch (st)
    {
        case WIFI_STAT_IDLE:          return "IDLE";
        case WIFI_STAT_PROVISIONING:  return "PROV";
        case WIFI_STAT_CONNECTING:    return "CONNECTING";
        case WIFI_STAT_CONNECTED:     return "CONNECTED";
        case WIFI_STAT_FAIL:          return "FAIL";
        default:                      return "?";
    }
}

/** @brief cmd_owner_t -> 短名(与实际下发的 AT 命令对应) */
static const char * cmd_st_name (cmd_owner_t c)
{
    switch (c)
    {
        case CMD_NONE:    return "none";
        case CMD_AT:      return "AT";
        case CMD_TIME:    return "TIME";
        case CMD_TZSET:   return "TZONE";
        case CMD_SNTP:    return "SNTP";
        case CMD_NWIP:    return "NWIP";
        case CMD_WFSTA:   return "WFSTA";
        case CMD_WFCAP:   return "WFCAP";
        case CMD_WFJAPA:  return "WFJAPA";
        case CMD_HTTP_NOW:   return "NWHTC-NOW";
        case CMD_HTTP_DAILY: return "NWHTC-DAY";
        default:          return "?";
    }
}

/**
 * @brief 状态总表：把所有状态位打成**一行**
 *
 * 存在的理由（2026-10-04）：之前每一层各自打印一小段，导致"当前到底什么状态"
 * 必须靠人把散落各处的行拼起来看，而其中关键几行又经常落在串口窗口之外 ——
 * 于是变成反复让用户贴日志、掐时间等。这里改成一行给全，**看一行就够**。
 *
 * 只在两个时机调用，都是低频：
 *   - 开机(warmup)          : 全部初始状态
 *   - 每次界面刷新(redraw)  : 每次状态迁移（联网成功/抓取完成/按键翻页）
 * 加上 5s/1s 心跳作为"没有变化时"的兜底。三者合起来覆盖全部时机。
 */
static void dump_state (const char * tag)
{
    at_engine_diag_t d;
    uint8_t k1 = 0U;
    uint8_t k2 = 0U;

    key_raw_levels(&k1, &k2);
    at_engine_diag(&d);

    printf("[ST] %-6s clk=%-5s | wifi=%-10s ip=%-15s ssid=%-10s f=%u | "
           "tz=%u sntp=%u wx=%u pg=%u | "
           "cmd=%-6s(%u) eng=%u mode=%u http=%u step=%u try=%u | "
           "res=%-12s hc=%d nw=%d blen=%u | "
           "rx=%lu ln=%lu plus=%lu urc=%u/%u drop=%lu/%lu | k1=%u k2=%u "
           "fsec=%u up=%lu\r\n",
           tag,
           (s.time_str[0] != '\0') ? s.time_str : "--:--",
           wifi_st_name(s.status), s.ip, s.ssid, (unsigned)s.fails,
           (unsigned)(s.timezone_set ? 1U : 0U),
           (unsigned)(s.sntp_done    ? 1U : 0U),
           (unsigned)(s.wx_valid     ? 1U : 0U),
           (unsigned)s.page,
           cmd_st_name(s.cmd), (unsigned)s.cmd,
           d.state, d.mode, (unsigned)weather_http_busy(),
           (unsigned)s.wifi_step, (unsigned)s.wifi_try,
           weather_task_last_result(), s_wxr.http, s_wxr.nw, (unsigned)s_wxr.body_len,
           (unsigned long)d.rx_bytes, (unsigned long)d.lines,
           (unsigned long)d.plus_lines,
           (unsigned)d.urc_cnt, (unsigned)d.seen_cnt,
           (unsigned long)wifi_rx_drop_cnt, (unsigned long)usb_tx_drop_cnt,
           (unsigned)k1, (unsigned)k2,
           /* fsec= 本次 fetch 已等秒数(未在途时为 0xFFFFFFFF, 打印成 up 后难以
            * 辨识, 所以在途时才显示有效值)。两个时间字段改名为 clk / up:
            * 之前都叫 t=, 我自己把 up 读成了"距离某事件的时间", 导致把
            * "[hb] 1s"误当成"请求发出很久了"。名字必须能自解释。 */
           weather_http_busy() ?
               (unsigned)((at_engine_now_ms() - s.fetch_t0) / 1000UL) : 0U,
           (unsigned long)(at_engine_now_ms() / 1000UL));
}

static void hb_print (uint32_t now)
{
    at_engine_diag_t d;
    uint8_t k1 = 0U;
    uint8_t k2 = 0U;

    key_raw_levels(&k1, &k2);
    at_engine_diag(&d);
    /* ⚠ link= 这一段是"链路初始化进度"，排查"为什么没去拉天气"时最有用：
     *   tz/sntp/wx 三个 bit 依次点亮，cmd 是当前在途命令归属。
     *   三个 bit 全 0 且 cmd=AT(1)  -> 卡在开机自检
     *   tz=0 且 cmd=0              -> 卡在等 TZONE 发出（引擎不空闲）
     *   tz=1 sntp=0                -> 卡在等 SNTP
     *   tz=1 sntp=1 wx=0           -> 正在拉第一次天气
     *   wx=1                       -> 天气已就位，之后进入 15min 定时刷新
     * 只加在这一行里，不新增任何打印行（§3.4.1 诊断必须低频）。 */
     printf("[hb] up=%lus fsec=%us eng=%u mode=%u urc=%u/%u rx=%lu ln=%lu plus=%lu "
            "wifi_rx=%lu drop=%lu usb_drop=%lu st=%d wx=%s %dC "
            "kraw=%u%u res=%s/%d rq=%u loop=%lu stage=%u wrx=%lu urx=%lu wtx=%lu "
            "link[tz=%u sntp=%u wx=%u cmd=%u step=%u try=%u http=%u]\r\n",
           (unsigned long)(now / 1000UL),
           /* fsec= 本次 fetch 已等秒数(不在途时为 0)。2026-10-04 加:
            * 之前这行只有一个含糊的秒数, 我误把它读成"请求已发出多久",
            * 于是把"请求刚发出 1 秒"当成"抓取卡住了"。 */
           weather_http_busy() ? (unsigned)((now - s.fetch_t0) / 1000UL) : 0U,
           d.state, d.mode, d.urc_cnt, d.seen_cnt,
           (unsigned long)d.rx_bytes, (unsigned long)d.lines,
           (unsigned long)d.plus_lines,
           (unsigned long)wifi_rx_byte_cnt, (unsigned long)wifi_rx_drop_cnt,
           (unsigned long)usb_tx_drop_cnt,
           (int)s.status,
           s.wx_valid ? s.wx.text : "--", s.wx_valid ? s.wx.temperature : 0,
            (unsigned)k1, (unsigned)k2,
            weather_task_last_result(), s_wxr.http,
            (unsigned)(s.refresh_pending ? 1U : 0U),
            (unsigned long)diag_main_loops, (unsigned)diag_main_stage,
            (unsigned long)diag_wifi_rx_processed,
            (unsigned long)diag_usb_rx_processed,
            (unsigned long)wifi_tx_done_cnt,
            (unsigned)(s.timezone_set ? 1U : 0U),
           (unsigned)(s.sntp_done    ? 1U : 0U),
           (unsigned)(s.wx_valid     ? 1U : 0U),
           (unsigned)s.cmd,
           (unsigned)s.wifi_step,
           (unsigned)s.wifi_try,
           (unsigned)weather_http_busy());
}

/* ===================== 内部工具 ===================== */

/**
 * @brief 联网类命令完成后回到"等待"状态
 *
 * ⚠ 关键：只在**还没连上**时才回退状态。
 *
 * poll 的执行顺序是「先第 2 步取 URC → 再第 3 步收命令结果」，所以可能
 * 在同一次 poll 里先把状态置成 CONNECTED（第 2 步拿到 +NWIP / +WFJAP），
 * 紧接着第 3 步的 on_cmd_done() 才跑。若这里无条件写
 * `status = WIFI_STAT_CONNECTING`，就会把刚刚确认的 CONNECTED 打回去，
 * 阶梯重新开始 → 已经连着还在反复发 WFCAP/WFJAPA（2026-10-03 实测复现）。
 */
static void reconnect_if_needed (void)
{
    if (WIFI_STAT_CONNECTED != s.status)
    {
        s.status = WIFI_STAT_CONNECTING;
    }
}

/**
 * @brief 把整个连接流程标记为已连上
 *
 * @param how 来源描述，仅用于日志
 */
static void mark_connected (const char * how)
{
    s.status   = WIFI_STAT_CONNECTED;
    s.ui_dirty = true;
    s.timezone_set = false;
    s.sntp_done    = false;
    s.tz_fail      = 0U;      /* 每次重新连上都要清零, 否则沿用上一轮的失败计数 */
    s.wifi_try     = 0U;
    /* 阶段2 硬编码 SSID 连的，与配置里的名字对齐即可；
     * 阶段4 配网拿到真 SSID 后改为从配置/存储读。 */
    snprintf(s.ssid, sizeof(s.ssid), "%s", WEATHER_DEFAULT_SSID);
    printf("[wx] CONNECTED via %s, ip=%s\r\n", how, s.ip);
}

/**
 * @brief 从一行文本里取出 IP 字段
 *
 * 兼容两种实测格式：
 *   +WFJAP:1,'CMCC-xxxx',192.168.1.2   —— IP 在**最后**一个逗号后（SSID 带引号）
 *   +NWIP:0,192.168.1.2,255.255.255.0,192.168.1.1 —— IP 在**第 2** 个字段
 *
 * 做法：跳过前导 '+'，按逗号切成字段；先找第一个"看起来像 IPv4"的字段，
 * 找不到再退回"最后一个逗号后"。这样两种格式都不用分别写解析器。
 *
 * @param line 输入行（不含 CRLF）
 * @param out  输出缓冲
 * @param size 缓冲大小
 * @return true=取到
 */
static bool parse_ip (const char * line, char * out, uint16_t size)
{
    const char * p = line;
    uint8_t field = 0U;

    if ((NULL == line) || (NULL == out) || (0U == size))
    {
        return false;
    }

    while ((*p != '\0') && (*p != '\r') && (*p != '\n'))
    {
        if ((',' == *p) || ('\'' == *p))
        {
            p++;
            continue;
        }

        if (('0' <= *p) && (*p <= '9') && (0U == field))
        {
            /* 候选 IPv4 起点：必须形如 d.d.d.d */
            const char * q = p;
            uint8_t dots = 0U;
            uint8_t digits = 0U;

            while ((*q != '\0') && (*q != '\r') && (*q != '\n') &&
                   (*q != ',') && (*q != '\''))
            {
                if (('0' <= *q) && (*q <= '9'))
                {
                    digits++;
                }
                else if ('.' == *q)
                {
                    if (0U == digits)
                    {
                        break;                 /* ".." 不是 IP */
                    }
                    dots++;
                    digits = 0U;
                }
                else
                {
                    break;
                }
                q++;
            }
            if ((3U == dots) && (digits > 0U) && (('\0' == *q) || (',' == *q) ||
                                                ('\'' == *q) || ('\r' == *q) ||
                                                ('\n' == *q)))
            {
                uint16_t k = 0U;

                while ((p < q) && (k < (uint16_t)(size - 1U)))
                {
                    out[k++] = *p++;
                }
                out[k] = '\0';
                return true;
            }
        }

        if (',' == *p)
        {
            field++;
        }
        p++;
    }

    /* 兜底: 取最后一个逗号之后的内容 */
    {
        const char * last = strrchr(line, ',');

        if (NULL != last)
        {
            const char * q = last + 1;
            uint16_t k = 0U;

            while ((*q == ' ') || (*q == '\t'))
            {
                q++;
            }
            while ((*q != '\0') && (*q != ',') && (*q != '\'') &&
                   (*q != '\r') && (*q != '\n') && (k < (uint16_t)(size - 1U)))
            {
                out[k++] = *q++;
            }
            out[k] = '\0';
            return (0U != k);
        }
    }
    return false;
}

/**
 * @brief 投递一条 AT 命令并登记归属
 *
 * 仅在 at_engine_idle() 为 true 时可成功（单飞行约束）。
 *
 * @param cmd   命令（引擎自动补 CRLF）
 * @param tmo   超时(ms)
 * @param owner 归属，完成时 on_cmd_done() 据此分发
 * @return true=已投递
 */
static bool at_send_owned (const char * cmd, uint32_t tmo, cmd_owner_t owner)
{
    at_result_t r;

    if (!at_engine_idle())
    {
        return false;
    }
    r = at_engine_start(cmd, tmo);
    if (AT_RES_PENDING != r)
    {
        printf("[wx] send '%s' rejected\r\n", cmd);
        return false;
    }
    s.cmd = owner;
    return true;
}

/**
 * @brief 从 +TIME:<date>,<time> 解析出 HH:MM 与 MM-DD
 *
 * 实测格式：`+TIME:2026-10-03,11:30:51`
 *
 * @param buf    不含 "+TIME:" 前缀的响应内容
 * @param time5  输出 "HH:MM"
 * @param size   输出缓冲大小
 * @param date11 输出 "MM-DD"（可传 NULL）
 */
static void time_line_parse (const char * buf, char * time5, uint16_t size, char * date11)
{
    const char * colon;
    int hh, mm;

    if (NULL != date11)
    {
        date11[0] = '\0';
    }
    time5[0] = '\0';

    colon = strchr(buf, ',');
    if (NULL == colon)
    {
        return;
    }
    colon++;

    if ((colon[0] < '0') || (colon[0] > '9') ||
        (colon[1] < '0') || (colon[1] > '9') ||
        (colon[3] < '0') || (colon[3] > '9') ||
        (colon[4] < '0') || (colon[4] > '9'))
    {
        return;
    }

    hh = (colon[0] - '0') * 10 + (colon[1] - '0');
    mm = (colon[3] - '0') * 10 + (colon[4] - '0');
    snprintf(time5, size, "%02d:%02d", hh, mm);

    /* 日期: buf 形如 "2026-10-03,11:30:51" -> 取 [5..9] = "10-03" */
    if ((NULL != date11) && (strlen(buf) >= 10U))
    {
        memcpy(date11, &buf[5], 5);
        date11[5] = '\0';
    }
}

/**
 * @brief 在引擎响应里找 +TIME: 行并解析
 * @param time5  输出
 * @param size   大小
 * @param date11 输出日期（可传 NULL）
 * @return true=找到并解析成功
 */
static bool take_time (char * time5, uint16_t size, char * date11)
{
    const char * b = at_engine_resp()->buf;
    uint16_t n = at_engine_resp()->len;
    uint16_t i;

    for (i = 0U; (i + 6U) < n; i++)
    {
        if (0 == strncmp(&b[i], "+TIME:", 6))
        {
            char line[64];
            uint16_t l = 0U;

            while (((i + 6U + l) < n) &&
                   ('\r' != b[i + 6U + l]) && ('\n' != b[i + 6U + l]))
            {
                if (l < (uint16_t)(sizeof(line) - 1U))
                {
                    line[l] = b[i + 6U + l];
                    l++;                      /* 必须递增, 否则 l 恒为 0 该循环永不退出 */
                }
                else
                {
                    break;
                }
            }
            line[l] = '\0';
            time_line_parse(line, time5, size, date11);
            return (time5[0] != '\0');
        }
    }
    return false;
}

/**
 * @brief 构造心知天气 now.json 的完整 URL
 *
 * 实测：裸 URL 即可，`&` `?` 都不需转义或加单引号。
 *
 * @param buf  输出
 * @param size 大小
 */
static void build_url (char * buf, uint16_t size, bool daily)
{
    if (daily) {
        snprintf(buf, size,
                 "http://api.seniverse.com/v3/weather/daily.json?key=%s&location=%s&language=%s&unit=c&start=0&days=1",
                 WEATHER_API_KEY,
                 ('\0' != WEATHER_CITY[0]) ? WEATHER_CITY : "ip",
                 WEATHER_LANG);
    } else {
        snprintf(buf, size,
                 "http://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=%s",
                 WEATHER_API_KEY,
                 ('\0' != WEATHER_CITY[0]) ? WEATHER_CITY : "ip",
                 WEATHER_LANG);
    }
}

/* ===================== 天气拉取 ===================== */

/**
 * @brief 投递一次天气请求（非阻塞）
 * @return true=已投递
 */
static bool fetch_weather_start (void)
{
    char url[224];

    if (s.status != WIFI_STAT_CONNECTED)
    {
        printf("[wx] skip fetch: not connected (st=%d)\r\n", (int)s.status);
        return false;
    }
    if (weather_http_busy())
    {
        printf("[wx] skip fetch: http busy\r\n");
        return false;
    }
    if ('\0' == WEATHER_API_KEY[0])
    {
        printf("[wx] skip fetch: no API key\r\n");
        return false;
    }

    build_url(url, sizeof(url), false);
    printf("[wx] fetch %s\r\n", url);
    if (weather_http_get_start(url, WEATHER_HTTP_TIMEOUT_MS))
    {
        s.cmd = CMD_HTTP_NOW;        /* 当前是 now.json 请求 */
        /* 记发起时刻, 让 OLED 能显示"已经等了几秒"。
         * 2026-10-04 的误判: 抓到的日志停在 [hb] 1s(请求刚发出 1 秒)就被当成
         * "抓取有问题"。屏幕上必须能看出"还在飞"和"等了几秒"。 */
        s.fetch_t0  = at_engine_now_ms();
        s.fetch_sec = 0xFFFFFFFFU;    /* 强制首帧就重绘 */
        s.ui_dirty  = true;
        return true;
    }
    return false;
}

/**
 * @brief 天气请求完成后解析 JSON
 */
static bool fetch_weather_now_done (void)
{
    const weather_http_result_t *res = weather_http_result();
    weather_data_t parsed;

    s_wxr.at_ms    = at_engine_now_ms();
    s_wxr.http     = res->http_status;
    s_wxr.nw       = res->nw_status;
    s_wxr.body_len = (res->len > 0) ? (uint16_t)res->len : 0U;

    if ((res->len <= 0) || (res->http_status != 200)) {
        s_wxr.kind = (res->len <= 0) ? WXR_TRANSPORT_ERR : WXR_HTTP_ERR;
        printf("[WX-RESULT] NOW FAIL http=%d nw=%d len=%d\r\n",
               res->http_status, res->nw_status, res->len);
        s.ui_dirty = true;
        return false;
    }

    if (weather_parse_now(weather_http_body(NULL), &parsed) != 0) {
        s_wxr.kind = WXR_PARSE_ERR;
        printf("[WX-RESULT] NOW PARSE-FAIL http=200 len=%u\r\n",
               (unsigned)s_wxr.body_len);
        s.ui_dirty = true;
        return false;
    }

    s.wx = parsed;
    s.wx_valid = true;
    s.ui_dirty = true;
    printf("[WX-NOW] %s %dC code=%d humidity=%d wind=%d up=%s\r\n",
           s.wx.text, s.wx.temperature, s.wx.code, s.wx.humidity,
           s.wx.wind_speed, s.wx.update);
    return true;
}

static bool fetch_weather_daily_start (void)
{
    char url[224];

    build_url(url, sizeof(url), true);
    printf("[wx] fetch daily %s\r\n", url);
    if (!weather_http_get_start(url, WEATHER_HTTP_TIMEOUT_MS)) {
        return false;
    }
    s.cmd = CMD_HTTP_DAILY;
    s.fetch_t0 = at_engine_now_ms();
    s.fetch_sec = 0xFFFFFFFFU;
    s.ui_dirty = true;
    return true;
}

static void fetch_weather_publish (void)
{
    s_wxr.kind        = WXR_OK;
    s_wxr.temperature = s.wx.temperature;
    s_wxr.code        = (uint8_t)s.wx.code;
    snprintf(s_wxr.text, sizeof(s_wxr.text), "%s",
             weather_text_get(s.wx.code));
    snprintf(s_wxr.api_text, sizeof(s_wxr.api_text), "%.16s", s.wx.text);

    s.wx_valid  = true;
    s.wx_age_ms = 0U;
    s.ui_dirty  = true;
    printf("[WX-RESULT] OK %s %dC code=%d high=%d low=%d humidity=%d wind=%d\r\n",
           s.wx.text, s.wx.temperature, s.wx.code,
           s.wx.high, s.wx.low, s.wx.humidity, s.wx.wind_speed);
}

static void fetch_weather_daily_done (void)
{
    const weather_http_result_t *res = weather_http_result();

    if ((res->len <= 0) || (res->http_status != 200)) {
        printf("[WX-DAILY] unavailable http=%d nw=%d len=%d\r\n",
               res->http_status, res->nw_status, res->len);
    } else if (weather_parse_daily(weather_http_body(NULL), &s.wx) != 0) {
        printf("[WX-DAILY] parse failed len=%d\r\n", res->len);
    } else {
        printf("[WX-DAILY] high=%dC low=%dC humidity=%d wind=%d\r\n",
               s.wx.high, s.wx.low, s.wx.humidity, s.wx.wind_speed);
    }

    /* Daily data is optional: keep a successful now.json result if this call fails. */
    fetch_weather_publish();
}

/* ===================== 命令结果分发 ===================== */

/**
 * @brief 处理普通（非 HTTP）命令完成
 * @param r 引擎结果
 */
static void on_cmd_done (at_result_t r)
{
    char time5[6];
    char date11[8];
    bool start_link = false;

    switch (s.cmd)
    {
        case CMD_AT:
            /* ⚠ 自检失败是**常态**，不是故障（2026-10-03 实测）：
             *   冷启动第一条 AT 常只收到半个 'O' 就被模块自己吞掉，
             *   引擎报 TIMEOUT@WAIT，但紧接着的 AT+NWIP=? 完全正常。
             * 所以这里只记日志，绝不据此判定模块死活 —— 真正判定联网状态
             * 靠阶梯第一级的 AT+NWIP=? 回读。 */
            printf("[wx] AT self-test: %s (cold-start may truncate reply)\r\n",
                   (AT_RES_OK == r) ? "OK" :
                   ((AT_RES_TIMEOUT == r) ? "TIMEOUT (normal on cold start)" : "ERROR"));
            /* 自检是上电后第一条命令, 等它结束就证明串口通了。此时立刻发
             * AT+NWIP=?, 不必再空等一个 WEATHER_CONNECT_TIMEOUT_MS 周期 ——
             * 实测该查询 ~50ms 就回, 白等 8s 只会让首屏多黑 8 秒。
             * 注意发送要放在函数末尾(清完 s.cmd 之后), 否则会被覆盖。 */
            start_link = (CMD_NONE != s.cmd);
            break;

        case CMD_TIME:
            s.time_check_pending = false;
            if (take_time(time5, sizeof(time5), date11))
            {
                snprintf(s.time_str, sizeof(s.time_str), "%s", time5);
                s.time_valid = true;
                s.ui_dirty   = true;
                printf("[wx] time %s (%s)\r\n", time5, date11);
            }
            else
            {
                printf("[wx] AT+TIME failed r=%d\r\n", (int)r);
            }
            break;

        case CMD_TZSET:
            printf("[wx] TZONE %s (unit=sec, tz=%d)\r\n",
                   (AT_RES_OK == r) ? "OK" : "FAIL", (int)(WEATHER_TZ_HOURS * 3600));
            s.timezone_set = (AT_RES_OK == r);
            if (AT_RES_OK == r)
            {
                s.tz_fail = 0U;
            }
            else if (++s.tz_fail >= TZ_MAX_FAIL)
            {
                /* 放弃对时，但必须放行链路：置 true 当作"已处理"，
                 * 否则 poll 里的 TZONE 分支会永远满足 !timezone_set，
                 * 把天气取数饿死。 */
                printf("[wx] TZONE failed %u times, skip time sync (weather fetch continues)\r\n",
                       (unsigned) s.tz_fail);
                s.timezone_set = true;
                s.sntp_done    = true;   /* SNTP 依赖时区，一并跳过 */
            }
            break;

        case CMD_SNTP:
            printf("[wx] SNTP %s (settle in %ums)\r\n",
                   (AT_RES_OK == r) ? "started" : "FAIL", (unsigned)SNTP_SETTLE_MS);
            /* SNTP 的 OK 仅代表命令被接受；延时后在已连接分支显式查 AT+TIME。 */
            s.time_check_pending = true;
            s.time_check_due_ms = at_engine_now_ms() + SNTP_SETTLE_MS;
            break;

        case CMD_WFSTA:
            /* +WFSTA:1 / +WFSTA:0 以 '+' 开头，会被 line_done() 收进 URC FIFO，
             * 由 poll 第 2b 步统一消费并据此 mark_connected。这里只留失败日志。 */
            if (AT_RES_OK != r)
            {
                printf("[wx] WFSTA query failed (err=%d)\r\n",
                       at_engine_resp()->err_code);
            }
            reconnect_if_needed();
            break;

        case CMD_NWIP:
            /* 结果不在这判: +NWIP:0,<ip>,<mask>,<gw> 以 '+' 开头会被 line_done()
             * 收进 URC FIFO, 由 poll 第 2c 步统一消费。这里只留失败日志。 */
            if (AT_RES_OK != r)
            {
                printf("[wx] NWIP query failed (err=%d)\r\n",
                       at_engine_resp()->err_code);
            }
            reconnect_if_needed();
            break;

        case CMD_WFCAP:
            /* ⚠ 不要再把 ERROR:-7 直接当成"无 profile"：只复位 RA 板子时模块往往
             * **已经连着**，这条错误只是"没必要再连"，profile 其实还在 NVRAM。
             * 真实是否已连上由阶梯第一级的 AT+WFSTA 回答决定。
             * 成功时不打印：阶梯下一跳的打点已经覆盖了状态迁移。 */
            if (AT_RES_OK != r)
            {
                printf("[wx] WFCAP rejected (err=%d)\r\n",
                       at_engine_resp()->err_code);
            }
            reconnect_if_needed();
            break;

        case CMD_WFJAPA:
            /* 失败日志由阶梯的限流打点负责（ladder_sig 含 err_code），
             * 这里只在成功时打一行，因为"发起连接被接受"是有意义的状态迁移。 */
            if (AT_RES_OK == r)
            {
                printf("[wx] WFJAPA accepted, waiting +WFSTA / +WFJAP\r\n");
            }
            reconnect_if_needed();
            /* 失败计数放在这里而不是 poll 里: on_cmd_done 每条命令只跑一次，
             * 若在 poll 里按 tick 自增, 10ms 一跳几秒就误判"用尽"。
             * ⚠ 已连上时不计失败: 命令本身回 OK, 且不该再退阶梯。 */
            if ((AT_RES_OK != r) && (WIFI_STAT_CONNECTED != s.status))
            {
                s.wifi_try++;
                if (s.wifi_try >= WIFI_WFJAPA_MAX_TRY)
                {
                    printf("[wx] WFJAPA failed %u times, restart ladder from NWIP query\r\n",
                           (unsigned)s.wifi_try);
                    s.wifi_try   = 0U;
                    s.wifi_step  = WIFI_STEP_NWIP_QUERY;
                    s.connect_t0 = at_engine_now_ms();
                }
            }
            break;

        default:
            break;
    }

    s.cmd = CMD_NONE;

    /* 开机自检一结束就主动查 IP(见 CMD_AT 分支的注释)。
     * 必须在此处发: 上面的 at_send_owned 会写 s.cmd, 在 switch 之前发会被覆盖。 */
    if (start_link)
    {
        s.wifi_step  = WIFI_STEP_NWIP_QUERY;
        s.connect_t0 = at_engine_now_ms();
        /* ⚠ 2026-10-12: 原来开机第一条是 AT+NWIP=?，但那条命令**无法判断连接状态**
         *   （第一字段是接口号，见 poll 第 2c 步注释）。换成权威的 AT+WFSTA。 */
        (void)at_send_owned("AT+WFSTA", 2500U, CMD_WFSTA);
    }
}

/* ===================== 时间显示 ===================== */

/**
 * @brief 取主页显示的时间字符串
 *
 * RA 不用内部 RTC，时间来自 DA16200 的 AT+TIME（每分钟查一次）。
 * 未拿到有效时间时显示 "--:--"（对齐 GD32 版未同步时的行为）。
 *
 * @param buf  输出
 * @param size 大小
 */
static void time_str_get (char * buf, uint16_t size)
{
    if (!s.time_valid)
    {
        snprintf(buf, size, "--:--");
        return;
    }
    snprintf(buf, size, "%s", s.time_str);
}

/* ===================== UI ===================== */

/**
 * @brief 重绘当前页
 */
static void ui_refresh (void)
{
    char time5[6];

    if (0U == s.page)
    {
        ui_main_view_t mv;

        memset(&mv, 0, sizeof(mv));
        time_str_get(time5, sizeof(time5));
        snprintf(mv.time_str, sizeof(mv.time_str), "%s", time5);
        /* Stage 1 uses Seniverse IP geolocation rather than a configured city. */
        snprintf(mv.city, sizeof(mv.city), "IP");

        if (s.wx_valid)
        {
            snprintf(mv.temp_str, sizeof(mv.temp_str), "%dC", s.wx.temperature);
            /* 心知中文 API 文案是 UTF-8，而 OLED 点阵字模按
             * GB2312/GBK 索引的（oledfont.h 的 Hzk[]）。把 UTF-8 字节直接喂
             * OLED_ShowString 会按 GBK 拆成两个错字 —— 这不是本工程的移植
             * 遗漏：GD32 版 weather_ui.c / oled.c 与本工程**逐字节相同**，
             * 它同样如此（两边都没有编码转换）。
             *
             * 而中文这条路目前根本铺好：oled_font_data/oled_cn_font.c 里
             * 只有占位记录、没有任何真实字模数据，oledfont.h 的 Hzk[] 也只有
             * 7 个汉字。英文模式可直接显示 ASCII now.text；非英文模式暂用 code
             * 表兜底，待中文字模生成后再接入完整 UTF-8 到 GBK 显示转换。 */
            /* WEATHER_LANG=en yields ASCII now.text, so show the API's actual
             * condition. Code-table fallback is for non-English/UTF-8 UI paths.
             * This avoids code 5 Foggy being mislabeled as code 30 Sandst. */
            if (0 == strcmp(WEATHER_LANG, "en"))
            {
                snprintf(mv.wtext, sizeof(mv.wtext), "%.15s", s.wx.text);
            }
            else
            {
                snprintf(mv.wtext, sizeof(mv.wtext), "%s",
                         weather_text_get(s.wx.code));
            }
            if (s.wx.humidity >= 0)
            {
                snprintf(mv.humidity_str, sizeof(mv.humidity_str), "%d",
                         s.wx.humidity);
            }
            if (s.wx.wind_speed >= 0)
            {
                snprintf(mv.wind_str, sizeof(mv.wind_str), "%d",
                         s.wx.wind_speed);
            }
            if (s.wx.daily_valid && (s.wx.low >= 0) && (s.wx.high >= 0))
            {
                snprintf(mv.low_str, sizeof(mv.low_str), "%d", s.wx.low);
                snprintf(mv.high_str, sizeof(mv.high_str), "%d", s.wx.high);
            }
            mv.icon    = (uint8_t)weather_icon_of(s.wx.code);
            mv.offline = (s.status != WIFI_STAT_CONNECTED) ? 1U : 0U;
        }
        else
        {
            /* ⚠ 没拿到天气时**直接把当前阶段显示在屏幕上**（2026-10-04）。
             *
             * 之前只显示 "waiting data.."，无法区分"请求还在飞"、"HTTP 非 200"
             * 和"cJSON 解析失败"三种完全不同的情况，只能靠翻串口日志 ——
             * 而结论行偏偏容易落在串口窗口外。2026-10-04 这次又因此误判：
             * 抓到的日志停在 `[hb] 1s ... res=no fetch yet`，那只是**请求刚发出
             * 1 秒**，模块还没回包，就被当成"抓取有问题"。屏幕必须能区分
             * "还在飞"和"卡住了"，否则每次都会重复这个误判。
             *
             * 优先级（从上到下）：
             *   1. 请求在途        -> "fetching.. Ns"（带已等秒数，看得见它在走）
             *   2. 已联网但没结论  -> 结论本身（OK/ERR/no fetch yet）
             *   3. 还没联网        -> 联网阶段（connecting / connecting.. Ns） */
            if (weather_http_busy())
            {
                uint32_t el = (uint32_t)(at_engine_now_ms() - s.fetch_t0) / 1000U;

                snprintf(mv.wtext, sizeof(mv.wtext), "fetching.. %us", (unsigned)el);
            }
            else if (s.status == WIFI_STAT_CONNECTED)
            {
                weather_task_result_line(mv.wtext, sizeof(mv.wtext));
            }
            else if (s.connect_t0 != 0U)
            {
                uint32_t el = (uint32_t)(at_engine_now_ms() - s.connect_t0) / 1000U;

                snprintf(mv.wtext, sizeof(mv.wtext), "connecting.. %us", (unsigned)el);
            }
            mv.icon = 0xFFU;
        }
        ui_main_page(&mv, s.status);
    }
    else
    {
        ui_wifi_view_t wv;

        memset(&wv, 0, sizeof(wv));
        wv.ssid  = (s.ssid[0] != '\0') ? s.ssid : WEATHER_DEFAULT_SSID;
        wv.ip    = s.ip;
        wv.fails = s.fails;
        wv.page  = s.page;
        ui_wifi_page(&wv, s.status);
    }
}

/* ===================== 对外接口 ===================== */

void weather_task_init (void)
{
    memset(&s, 0, sizeof(s));
    s.status     = WIFI_STAT_CONNECTING;
    s.connect_t0 = at_engine_now_ms();
    s.wifi_step  = WIFI_STEP_NWIP_QUERY;  /* 先主动查 IP, 再考虑被动等/主动连 */
    s.wifi_try   = 0U;
    s.page       = 0U;
    s.ui_dirty   = true;
    s.cmd        = CMD_NONE;
    s.hb_t0      = at_engine_now_ms();
    s.hb_fast    = s.hb_t0;

    key_init();
    ui_init();
    printf("[wx] task init: boot %s %s, first step = NWIP query\r\n",
           __DATE__, __TIME__);

    /* 用户要求: 每次开机先用 AT 命令测试模块, 再走后续联网/天气。
     *
     * ⚠ 超时只给 800ms 而不是 2000ms（2026-10-04 调整）：
     *   实测冷启动这条 AT 必定被截断（只回 1 个字符就 TIMEOUT），它只是
     *   "模块活着没有"的形式确认。而引擎时钟是按主循环 tick 累加的，
     *   OLED 重绘一轮要 ~100ms，所以 2000ms 的引擎时间在真实时间上远不止
     *   2 秒，整轮开机白等 —— 之前"抓日志等不到结果"就有它一份。
     *   自检失败不致命（下一条 AT+NWIP=? 才是真正判定），所以压到 800ms。
     */
    if (!at_send_owned("AT", 800U, CMD_AT))
    {
        printf("[wx] AT self-test could not submit (engine busy)\r\n");
    }
    else
    {
        printf("[wx] AT self-test sent\r\n");
    }

    /* 开机状态总表: 之后每次界面刷新都会再打一行, 两行即可看清整个启动过程 */
    dump_state("boot");
}

void weather_task_refresh_now (void)
{
    s.refresh_pending = true;
    s.ui_dirty = true;
    printf("[wx] weather refresh queued\r\n");
}

wifi_status_t weather_task_wifi_status (void)
{
    return s.status;
}

bool weather_task_weather_valid (void)
{
    return s.wx_valid;
}

void weather_task_poll (void)
{
    uint32_t now = at_engine_now_ms();

    /* ---------- 1. 按键（最先处理，保证任何状态下即时响应） ----------
     * 映射按 DESIGN §6（板上 4 键 = 2 机械 + 2 触摸）：
     *   机械键1 P005: 短按=刷新 / 双击=翻页 / 长按3s=配网(阶段4)
     *   机械键2 P006: 单击=翻页
     *   触摸键1 TS11: 单击=刷新        （阶段3 接入）
     *   触摸键2 TS12: 单击=翻页/长按配网（阶段3 接入）
     * 触摸键用 key_id_t 区分即可，本函数不需要改，阶段3 只要让
     * key_scan 能报出 KEY_ID_TOUCH1/2 就能自动生效。 */
    key_poll(now);
    for (;;)
    {
        key_id_t    id = KEY_ID_MECH1;
        key_event_t e  = key_event_get_id(&id);

        if (KEY_EVT_NONE == e)
        {
            break;
        }

        /* 机械键2 / 触摸键2：单击=翻页（不做三态，省一次双击等待） */
        if ((KEY_ID_MECH2 == id) || (KEY_ID_TOUCH2 == id))
        {
            if (KEY_EVT_LONG == e)
            {
                printf("[wx] key%u: long -> provisioning (stage4)\r\n", (unsigned)id);
                s.ui_dirty = true;
            }
            else
            {
                s.page ^= 1U;
                printf("[wx] key%u: click -> page %u\r\n",
                       (unsigned)id, (unsigned)s.page);
                s.ui_dirty = true;
            }
            continue;
        }

        /* 机械键1 / 触摸键1：三态 */
        switch (e)
        {
            case KEY_EVT_SHORT:
                printf("[wx] key%u: short -> queue weather refresh\r\n", (unsigned)id);
                s.refresh_pending = true;
                s.ui_dirty = true;
                break;

            case KEY_EVT_DOUBLE:
                s.page ^= 1U;
                printf("[wx] key%u: double -> page %u\r\n",
                       (unsigned)id, (unsigned)s.page);
                s.ui_dirty = true;
                break;

            case KEY_EVT_LONG:
                /* 阶段4 接配网；当前仅打印，不改状态机 */
                printf("[wx] key%u: long -> provisioning (stage4)\r\n", (unsigned)id);
                s.ui_dirty = true;
                break;

            default:
                break;
        }
    }

    /* ---------- 2. 联网：URC 快速路径 + 超时阶梯 ---------- */
    if ((WIFI_STAT_CONNECTING == s.status) || (WIFI_STAT_IDLE == s.status))
    {
        char urc[AT_URC_LINE];

        /* 2a. 快速路径：+WFJAP:1 自带 IP，一步到位（模块刚上电时会自发这条） */
        if (at_engine_urc_get("+WFJAP:1", urc, sizeof(urc)))
        {
            printf("[wx] +WFJAP:1 urc='%s'\r\n", urc);
            if (parse_ip(urc, s.ip, sizeof(s.ip)))
            {
                mark_connected("+WFJAP:1 URC");
            }
            else
            {
                /* ⚠ 2026-10-12: 这里不能只说"没有 IP 字段"。
                 * parse_ip() 现在会拒绝 0.0.0.0 / x.x.x.0 / 169.254.* /
                 * 10.0.0.* 这些不可用地址，所以失败既可能是字段缺失，
                 * 也可能是拿到了占位地址 —— 两种都意味着还没真连上。 */
                printf("[wx] +WFJAP:1 has no usable IP, fall back to NWIP query\r\n");
                s.wifi_step  = WIFI_STEP_NWIP_QUERY;
                s.connect_t0 = now;
            }
        }
        /* 2b. +WFSTA:1 —— 手册 L1149 定义的**权威**连接状态。
         *
         * ⚠ 为什么必须用这条，而不是继续从 +NWIP 里猜（2026-10-12 修正）：
         *   手册 L832 明确 `AT+NWIP` 的第一个参数是**接口号**（0=WLAN0/STA，
         *   1=WLAN1/Soft-AP），`AT+NWIP=?` 的响应格式是 `+NWIP:<iface>,<ip>,...`。
         *   实测模块回的是 `+NWIP:1,10.0.0.1,255.255.255.0,10.0.0.1` ——
         *   第一个字段是 **1 = Soft-AP 接口**，10.0.0.1 是模组内置 SoftAP 的地址，
         *   完全不代表 STA 已经从 CMCC-xxxx 拿到地址。
         *   之前把"能解出一个像样 IP"当成"已连上"，就是被这条误导的。
         *   AT+WFSTA 没有这种歧义：1=已关联，0=未关联。
         */
        else if (at_engine_urc_get("+WFSTA:1", urc, sizeof(urc)))
        {
            printf("[wx] +WFSTA:1 => module IS associated with an AP\r\n");
            /* 关联成功但还不知道地址，接着问一次 IP */
            (void)at_send_owned("AT+NWIP=?", 2500U, CMD_NWIP);
        }
        else if (at_engine_urc_get("+WFSTA:0", urc, sizeof(urc)))
        {
            /* 未关联。这不是故障，只是阶梯没爬上去，不打印以免刷屏 */
            s.wifi_sta_ok = false;
        }
        /* 2c. +NWIP 回读：模块已连上但 URC 被错过时走这里 */
        else if (at_engine_urc_get("+NWIP", urc, sizeof(urc)))
        {
            printf("[wx] +NWIP urc='%s'\r\n", urc);
            if (parse_ip(urc, s.ip, sizeof(s.ip)))
            {
                mark_connected("+NWIP query");
            }
        }
        else if (at_engine_urc_take("+WFJAP:0"))
        {
            printf("[wx] +WFJAP:0 -> CONNECT FAILED\r\n");
            s.status   = WIFI_STAT_FAIL;
            s.fails++;
            s.ui_dirty = true;
        }
        else if ((WIFI_STAT_CONNECTING == s.status) &&
                 ((now - s.connect_t0) >= WEATHER_CONNECT_TIMEOUT_MS))
        {
            char cmd[96];
            int  lad_err = at_engine_resp() ? at_engine_resp()->err_code : 0;
            uint16_t lad_sig = (uint16_t)(((unsigned)s.wifi_step << 8) |
                                          ((unsigned)(lad_err & 0xFF)));

            /* 日志限流：阶梯每 8s 一跳，不限流的话一次失败会刷出上百行几乎相同的文本，
             * 把真正有意义的状态迁移埋掉。同一个 (step, err) 只打第一遍，
             * 之后每 LADDER_LOG_REPEAT 次再打一次"还在原地"。 */
            if (s.ladder_sig != lad_sig)
            {
                s.ladder_sig    = lad_sig;
                s.ladder_nprint = 0U;
                printf("[wx] step %u idle %lums (try=%u) -> escalate (err=%d)\r\n",
                       (unsigned)s.wifi_step,
                       (unsigned long)(now - s.connect_t0),
                       (unsigned)s.wifi_try,
                       (int)lad_err);
            }
            else if (++s.ladder_nprint >= LADDER_LOG_REPEAT)
            {
                s.ladder_nprint = 0U;
                printf("[wx] step %u still stuck (try=%u)\r\n",
                       (unsigned)s.wifi_step, (unsigned)s.wifi_try);
            }
            s.connect_t0 = now;
            s.status = WIFI_STAT_CONNECTING;
            s.ui_dirty = true;

            switch (s.wifi_step)
            {
                case WIFI_STEP_NWIP_QUERY:
                    /* 主动问连接状态：模块若已连上这一步就收工，且免疫 URC 错过 */
                    (void)at_send_owned("AT+WFSTA", 2500U, CMD_WFSTA);
                    s.wifi_step = WIFI_STEP_WAIT_URC;
                    break;

                case WIFI_STEP_WAIT_URC:
                    /* 等不到 URC -> 让模块用 NVRAM profile 自己连 */
                    (void)at_send_owned("AT+WFCAP", 3000U, CMD_WFCAP);
                    s.wifi_step = WIFI_STEP_WFCAP;
                    break;

                case WIFI_STEP_WFCAP:
                default:
                    /* 显式带 SSID/PWD：连 + 写 NVRAM，最兜底。
                     *
                     * ⚠ 2026-10-12 修正：原来这里是 `case WFCAP:` 和 `default:`
                     *   合并的，fallthrough 进去后无条件执行 `s.wifi_try = 0U`。
                     *   结果是：每次重试都把失败计数清零，于是
                     *   on_cmd_done 里的 `wifi_try >= WIFI_WFJAPA_MAX_TRY` 永远不成立，
                     *   阶梯卡在第 3 级无限循环 AT+WFJAPA，既不换手段也不退���重爬。
                     *   现场证据：日志里 step=3 try=0 / try=1 交替出现且永不变。
                     *   现在计数完全交给 on_cmd_done 维护，这里不再碰 wifi_try。 */
                    snprintf(cmd, sizeof(cmd), "AT+WFJAPA=%s,%s",
                             WEATHER_DEFAULT_SSID, WEATHER_DEFAULT_PWD);
                    (void)at_send_owned(cmd, 30000U, CMD_WFJAPA);
                    s.wifi_step = WIFI_STEP_WFJAPA;
                    break;
            }
        }

    }

    /* ---------- 3. 引擎轮询（工程内唯一调用点, §4 单飞行） ---------- */
    {
        at_result_t r = at_engine_poll();

        if (AT_RES_PENDING != r)
        {
            if ((CMD_HTTP_NOW == s.cmd) || (CMD_HTTP_DAILY == s.cmd))
            {
                /* HTTP 命令: 结果归 weather_http */
                weather_http_on_engine_done(r);
                if (weather_http_done())
                {
                    cmd_owner_t completed = s.cmd;
                    weather_http_clear();
                    s.cmd = CMD_NONE;

                    if (CMD_HTTP_NOW == completed)
                    {
                        if (fetch_weather_now_done())
                        {
                            if (!fetch_weather_daily_start())
                            {
                                printf("[WX-DAILY] request start failed; using now data\r\n");
                                fetch_weather_publish();
                            }
                        }
                    }
                    else
                    {
                        fetch_weather_daily_done();
                    }
                }
            }
            else if (CMD_NONE != s.cmd)
            {
                on_cmd_done(r);
            }
        }
    }

    /* ---------- 4. 命令调度（仅在引擎空闲时, 优先级从高到低） ---------- */
    if (at_engine_idle() && !weather_http_busy())
    {
        /* 4a. Explicit queued refresh (key/API) takes precedence over periodic fetch. */
        if (s.refresh_pending)
        {
            if (s.status == WIFI_STAT_CONNECTED && (CMD_NONE == s.cmd) &&
                fetch_weather_start())
            {
                s.refresh_pending = false;
                printf("[wx] queued key refresh issued\r\n");
            }
        }
        /* 4b. Weather refresh due */
        else if (s.wx_valid && (s.wx_age_ms >= (uint32_t)WEATHER_REFRESH_MIN * 60000U))
        {
            if (fetch_weather_start())
            {
                s.wx_age_ms = 0U;
            }
        }
        /* 4c. 联网后初始化链路: 查 IP -> 设时区 -> SNTP */
        else if (s.status == WIFI_STAT_CONNECTED)
        {
            if (s.time_check_pending)
            {
                /* Match the measured/manual sequence: finish the delayed TIME
                 * verification before starting the first weather HTTP request. */
                if (((int32_t)(at_engine_now_ms() - s.time_check_due_ms) >= 0) &&
                    (CMD_NONE == s.cmd))
                {
                    s.time_poll_ms = now;
                    (void)at_send_owned("AT+TIME", 2500U, CMD_TIME);
                }
            }
            else if (!s.timezone_set && (CMD_NONE == s.cmd))
            {
                char cmd[48];
                snprintf(cmd, sizeof(cmd), "AT+TZONE=%d", (int)(WEATHER_TZ_HOURS * 3600));
                (void)at_send_owned(cmd, 2500U, CMD_TZSET);
            }
            else if (s.timezone_set && !s.sntp_done && (CMD_NONE == s.cmd))
            {
                char cmd[128];
                s.sntp_done = true;
                snprintf(cmd, sizeof(cmd), "AT+NWSNTP=1,%s,86400", NTP_SERVER_1);
                (void)at_send_owned(cmd, 6000U, CMD_SNTP);
            }
            else if (!s.wx_valid && (CMD_NONE == s.cmd))
            {
                /* 链路就绪但还没拉到天气 -> 立即拉一次。
                 *
                 * ⚠ 不再要求 s.sntp_done（2026-10-12）。原来三段严格串行，
                 * 只要 TZONE 或 SNTP 没成功就永远走不到这里。取天气是主功能，
                 * 对时只是附加价值，不能让它挡住。
                 * 实际效果：连上后的第一个空闲 tick 就发天气请求，
                 * TZONE/SNTP 在此之前的那一两个 tick 里已完成或已放弃。 */
                if (fetch_weather_start())
                {
                    printf("[wx] first fetch issued\r\n");
                }
            }
        }
        /* 4d. 定时查时间 */
        else if ((CMD_NONE == s.cmd) && ((now - s.time_poll_ms) >= WEATHER_TIME_POLL_MS))
        {
            s.time_poll_ms = now;
            (void)at_send_owned("AT+TIME", 2500U, CMD_TIME);
        }
    }

    /* ---------- 5. 天气老化累加 + 心跳 ---------- */
    if (s.wx_valid)
    {
        s.wx_age_ms += WEATHER_TICK_MS;
    }

#if (WEATHER_HB_DIAG_MS > 0U)
    if ((now - s.hb_t0) >= (uint32_t)WEATHER_HB_DIAG_MS)
    {
        s.hb_t0 = now;
        hb_print(now);
    }
#endif

    /* ---------- 5b. fetch 在途时的 1s 心跳（仅在途时才有） ----------
     *
     * 2026-10-04 排查依据：常规心跳 5s 一行，而 NWHTC 一旦在途就可能把
     * 主循环拖住，5s 那行迟迟不来，无法区分"引擎停在哪个状态"和"只是没到点"。
     * 所以在 weather_http 在途期间改成 1s 一行，直接看到引擎状态迁移：
     *   eng=1 (SENDING) -> 发出去了，等 TX 完成
     *   eng=2 (WAIT)    -> 命令已发出，等 +NWHTCSTATUS
     *   eng=0 (IDLE)    -> 引擎已收口，下一步就是 [wx] http done
     * 不在途时行为不变，仍是 WEATHER_HB_DIAG_MS（低频），不违反诊断低频原则。 */
    if (weather_http_busy() && ((now - s.hb_fast) >= 1000U))
    {
        uint32_t sec = (uint32_t)(now - s.fetch_t0) / 1000U;

        s.hb_fast = now;

        /* 同一个 1s 节拍顺便刷新屏幕上的"已等 N 秒"。
         * 不另开节拍: 在途时本来就要 1s 一次, 复用它最省, 而且屏幕
         * 和日志说的是同一个数字, 不会出现两边对不上的情况。
         * 每秒只重绘一行文字, 开销可忽略(软 SPI 约 40~90ms/屏)。 */
        if (sec != s.fetch_sec)
        {
            s.fetch_sec = sec;
            s.ui_dirty  = true;
        }
        hb_print(now);
    }

    /* ---------- 6. 重绘 + 状态快照 ---------- */
    if (s.ui_dirty)
    {
        s.ui_dirty = false;
        ui_refresh();
        /* 每次状态迁移（联网成功 / 抓取完成 / 按键翻页 / 页面切换）都打一行
         * 完整状态，这样"当前处于哪个状态"永远是日志里可查的，不需要
         * 把各处零散的小段拼起来猜，也不需要用户掐时间等某一行出现。 */
        dump_state("redraw");
    }
}
