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
    CMD_NWIP,          /**< AT+NWIP=?: 主动查当前 IP（判断模块是否已连上） */
    CMD_WFCAP,         /**< AT+WFCAP: 用 NVRAM 已存 profile 连接 */
    CMD_WFJAPA,        /**< AT+WFJAPA: 连接并写入 NVRAM */
    CMD_HTTP           /**< AT+NWHTC (weather_http_get_start) */
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
    uint8_t  wifi_try;            /**< 当前阶梯已尝试次数, 用于退避 */

    /* 心跳诊断(WEATHER_HB_DIAG_MS=0 时不打印) */
    uint32_t hb_t0;
} s;

/* hal_entry.c 的诊断计数器(判断日志/数据是否被丢) */
extern volatile uint32_t usb_tx_drop_cnt;
extern volatile uint32_t wifi_rx_drop_cnt;
extern volatile uint32_t wifi_rx_byte_cnt;

/**
 * @brief 低频心跳：确认主循环还活着、且引擎在收数据
 *
 * 定位"程序不动了"时非常有用：如果本行在刷新而 [WIFI] 不刷新，
 * 说明是 WIFI 口没数据；连本行都不刷新，才是主循环真的卡住。
 */
static void hb_print (uint32_t now)
{
    at_engine_diag_t d;

    at_engine_diag(&d);
    printf("[hb] %lus eng=%u mode=%u urc=%u/%u rx=%lu ln=%lu plus=%lu "
           "wifi_rx=%lu drop=%lu usb_drop=%lu st=%d wx=%s %dC\r\n",
           (unsigned long)(now / 1000UL),
           d.state, d.mode, d.urc_cnt, d.seen_cnt,
           (unsigned long)d.rx_bytes, (unsigned long)d.lines,
           (unsigned long)d.plus_lines,
           (unsigned long)wifi_rx_byte_cnt, (unsigned long)wifi_rx_drop_cnt,
           (unsigned long)usb_tx_drop_cnt,
           (int)s.status,
           s.wx_valid ? s.wx.text : "--", s.wx_valid ? s.wx.temperature : 0);
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
static void build_url (char * buf, uint16_t size)
{
    snprintf(buf, size,
             "http://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=zh-Hans",
             WEATHER_API_KEY,
             ('\0' != WEATHER_CITY[0]) ? WEATHER_CITY : "ip");
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

    build_url(url, sizeof(url));
    printf("[wx] fetch %s\r\n", url);
    if (weather_http_get_start(url, WEATHER_HTTP_TIMEOUT_MS))
    {
        s.cmd = CMD_HTTP;            /* 重要: 标记引擎当前在服务 HTTP 命令 */
        return true;
    }
    return false;
}

/**
 * @brief 天气请求完成后解析 JSON
 */
static void fetch_weather_done (void)
{
    const weather_http_result_t * res = weather_http_result();

    if ((res->len <= 0) || (res->http_status != 200))
    {
        printf("[wx] fetch failed: len=%d http=%d nw=%d err=%d\r\n",
               res->len, res->http_status, res->nw_status, res->http_status);
        return;
    }

    {
        const char * body = weather_http_body(NULL);

        if (0 != weather_parse_now(body, &s.wx))
        {
            printf("[wx] parse failed: %.60s\r\n", body);
            return;
        }
    }

    s.wx_valid  = true;
    s.wx_age_ms = 0U;
    s.ui_dirty  = true;
    printf("[wx] OK: %s %dC code=%d up=%s\r\n",
           s.wx.text, s.wx.temperature, s.wx.code, s.wx.update);
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

    switch (s.cmd)
    {
        case CMD_AT:
            /* ⚠ 自检失败是**常态**，不是故障（2026-10-03 实测）：
             *   冷启动第一条 AT 常只收到半个 'O' 就被模块自己吞掉，
             *   引擎报 TIMEOUT@WAIT，但紧接着的 AT+NWIP=? 完全正常。
             * 所以这里只记日志，绝不据此判定模块死活 —— 真正判定联网状态
             * 靠阶梯第一级的 AT+NWIP=? 回读。 */
            printf("[wx] AT self-test: %s (cold-start may truncate reply; "
                   "continuing to NWIP query)\r\n",
                   (AT_RES_OK == r) ? "OK" :
                   ((AT_RES_TIMEOUT == r) ? "TIMEOUT (normal on cold start)" : "ERROR"));
            break;

        case CMD_TIME:
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
            break;

        case CMD_SNTP:
            printf("[wx] SNTP %s\r\n", (AT_RES_OK == r) ? "started" : "FAIL");
            /* SNTP 生效需数秒: 把下次查时间提前到 +SNTP_SETTLE_MS */
            s.time_poll_ms = (at_engine_now_ms() + SNTP_SETTLE_MS) -
                             (WEATHER_TIME_POLL_MS);
            break;

        case CMD_NWIP:
            /* 结果不在这判: +NWIP:0,<ip>,<mask>,<gw> 以 '+' 开头会被 line_done()
             * 收进 URC FIFO, 由 poll 第 2b 步统一消费。这里只记日志。 */
            printf("[wx] NWIP query %s (err=%d); waiting +NWIP / +WFJAP\r\n",
                   (AT_RES_OK == r) ? "OK" : "failed",
                   at_engine_resp()->err_code);
            reconnect_if_needed();
            break;

        case CMD_WFCAP:
            /* ⚠ 不要再把 ERROR:-7 直接当成"无 profile"：只复位 RA 板子时模块往往
             * **已经连着**，这条错误只是"没必要再连"，profile 其实还在 NVRAM。
             * 真实是否已连上由阶梯第一级的 AT+NWIP=? 回读决定。 */
            printf("[wx] WFCAP %s (err=%d); waiting +WFJAP / +NWIP\r\n",
                   (AT_RES_OK == r) ? "accepted" : "failed",
                   at_engine_resp()->err_code);
            reconnect_if_needed();
            break;

        case CMD_WFJAPA:
            printf("[wx] WFJAPA %s (err=%d); waiting +WFJAP / +NWIP\r\n",
                   (AT_RES_OK == r) ? "accepted" : "failed",
                   at_engine_resp()->err_code);
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

        if (s.wx_valid)
        {
            snprintf(mv.temp_str, sizeof(mv.temp_str), "%dC", s.wx.temperature);
            snprintf(mv.wtext, sizeof(mv.wtext), "%s", s.wx.text);
            mv.icon    = (uint8_t)weather_icon_of(s.wx.code);
            mv.offline = (s.status != WIFI_STAT_CONNECTED) ? 1U : 0U;
        }
        else
        {
            if (s.status == WIFI_STAT_CONNECTED)
            {
                snprintf(mv.wtext, sizeof(mv.wtext), "waiting data..");
            }
            mv.icon = 0xFFU;
        }
        ui_main_page(&mv, s.status);
    }
    else
    {
        ui_wifi_view_t wv;

        memset(&wv, 0, sizeof(wv));
        wv.ssid  = s.ssid;
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

    key_init();
    ui_init();
    printf("[wx] task init\r\n");

    /* 用户要求: 每次开机先用 AT 命令测试模块, 再走后续联网/天气 */
    if (!at_send_owned("AT", 2000U, CMD_AT))
    {
        printf("[wx] AT self-test could not submit (engine busy)\r\n");
    }
    else
    {
        printf("[wx] AT self-test sent\r\n");
    }
}

void weather_task_refresh_now (void)
{
    if (fetch_weather_start())
    {
        printf("[wx] manual refresh issued\r\n");
    }
}

wifi_status_t weather_task_wifi_status (void)
{
    return s.status;
}

void weather_task_poll (void)
{
    uint32_t now = at_engine_now_ms();

    /* ---------- 1. 按键（最先处理，保证任何状态下即时响应） ---------- */
    key_poll(now);
    for (;;)
    {
        key_event_t e = key_event_get();

        if (KEY_EVT_NONE == e)
        {
            break;
        }
        switch (e)
        {
            case KEY_EVT_SHORT:
                printf("[wx] key: short -> refresh\r\n");
                s.ui_dirty = true;
                (void)fetch_weather_start();
                break;

            case KEY_EVT_DOUBLE:
                s.page ^= 1U;
                printf("[wx] key: double -> page %u\r\n", (unsigned)s.page);
                s.ui_dirty = true;
                break;

            case KEY_EVT_LONG:
                /* 阶段4 接配网；当前仅打印，不改状态机 */
                printf("[wx] key: long (provisioning in stage4)\r\n");
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
                printf("[wx] +WFJAP:1 has no IP field, fall back to NWIP query\r\n");
                s.wifi_step  = WIFI_STEP_NWIP_QUERY;
                s.connect_t0 = now;
            }
        }
        /* 2b. +NWIP 回读：模块已连上但 URC 被错过时走这里 */
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

            printf("[wx] step %u idle %lums (try=%u), escalating\r\n",
                   (unsigned)s.wifi_step,
                   (unsigned long)(now - s.connect_t0),
                   (unsigned)s.wifi_try);
            s.connect_t0 = now;

            switch (s.wifi_step)
            {
                case WIFI_STEP_NWIP_QUERY:
                    /* 主动问 IP：模块若已连上这一步就收工，且免疫 URC 错过 */
                    (void)at_send_owned("AT+NWIP=?", 2500U, CMD_NWIP);
                    s.wifi_step = WIFI_STEP_WAIT_URC;
                    break;

                case WIFI_STEP_WAIT_URC:
                    /* 等不到 URC -> 让模块用 NVRAM profile 自己连 */
                    (void)at_send_owned("AT+WFCAP", 3000U, CMD_WFCAP);
                    s.wifi_step = WIFI_STEP_WFCAP;
                    break;

                case WIFI_STEP_WFCAP:
                default:
                    /* 显式带 SSID/PWD：连 + 写 NVRAM，最兜底 */
                    snprintf(cmd, sizeof(cmd), "AT+WFJAPA=%s,%s",
                             WEATHER_DEFAULT_SSID, WEATHER_DEFAULT_PWD);
                    (void)at_send_owned(cmd, 30000U, CMD_WFJAPA);
                    s.wifi_step = WIFI_STEP_WFJAPA;
                    s.wifi_try  = 0U;
                    break;
            }
        }

    }

    /* ---------- 3. 引擎轮询（工程内唯一调用点, §4 单飞行） ---------- */
    {
        at_result_t r = at_engine_poll();

        if (AT_RES_PENDING != r)
        {
            if (CMD_HTTP == s.cmd)
            {
                /* HTTP 命令: 结果归 weather_http */
                weather_http_on_engine_done(r);
                if (weather_http_done())
                {
                    fetch_weather_done();
                    s.ui_dirty = true;
                    weather_http_clear();
                    s.cmd = CMD_NONE;
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
        /* 4a. 天气到点刷新 */
        if (s.wx_valid && (s.wx_age_ms >= (uint32_t)WEATHER_REFRESH_MIN * 60000U))
        {
            if (fetch_weather_start())
            {
                s.wx_age_ms = 0U;
            }
        }
        /* 4b. 联网后初始化链路: 查 IP -> 设时区 -> SNTP */
        else if (s.status == WIFI_STAT_CONNECTED)
        {
            if (!s.timezone_set && (CMD_NONE == s.cmd))
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
            else if (s.sntp_done && !s.wx_valid && (CMD_NONE == s.cmd))
            {
                /* 链路就绪但还没拉到天气 -> 立即拉一次 */
                if (fetch_weather_start())
                {
                    printf("[wx] first fetch issued\r\n");
                }
            }
        }
        /* 4c. 定时查时间 */
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

    /* ---------- 6. 重绘 ---------- */
    if (s.ui_dirty)
    {
        s.ui_dirty = false;
        ui_refresh();
    }
}