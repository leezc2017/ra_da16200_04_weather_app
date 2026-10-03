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
    CMD_TIME,          /**< AT+TIME */
    CMD_TZSET,         /**< AT+TZONE=<sec> */
    CMD_SNTP,          /**< AT+NWSNTP=... */
    CMD_WFCAP,         /**< AT+WFCAP 兜底重连 */
    CMD_HTTP           /**< AT+NWHTC (weather_http_get_start) */
} cmd_owner_t;

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
} s;

/* ===================== 内部工具 ===================== */

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

        case CMD_WFCAP:
            printf("[wx] WFCAP %s (err=%d); waiting +WFJAP URC\r\n",
                   (AT_RES_OK == r) ? "accepted" : "no-profile",
                   at_engine_resp()->err_code);
            s.status = WIFI_STAT_CONNECTING;       /* 回到等待 URC */
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
    s.status     = WIFI_STAT_CONNECTING;    /* 上电等自发 +WFJAP */
    s.connect_t0 = at_engine_now_ms();
    s.page       = 0U;
    s.ui_dirty   = true;
    s.cmd        = CMD_NONE;

    key_init();
    ui_init();
    printf("[wx] task init: waiting spontaneous +WFJAP (no AT command needed)\r\n");
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

    /* 联网成功处理：等 +WFJAP URC 并解析 IP */
    if ((WIFI_STAT_CONNECTING == s.status) || (WIFI_STAT_IDLE == s.status))
    {
        char urc[AT_URC_LINE];

        if (at_engine_urc_get("+WFJAP:1", urc, sizeof(urc)))
        {
            printf("[wx] +WFJAP:1 -> CONNECTED, urc='%s'\r\n", urc);

            /* 从 URC 行解析 IP：格式 +WFJAP:1,'SSID',IP
               实测的 +WFJAP:1,'CMCC-xxxx',192.168.1.2 使用单引号包裹 SSID、
               IP 不带引号。取最后一个逗号后的部分。 */
            const char *comma = strrchr(urc, ',');
            if (NULL != comma)
            {
                const char *p = comma + 1;
                uint16_t k = 0U;

                while (*p == ' ' || *p == '\t')
                {
                    p++;
                }
                while ((*p != '\0') && (*p != ',') && (*p != '\r') && (*p != '\n'))
                {
                    if (k < (uint16_t)(sizeof(s.ip) - 1U))
                    {
                        s.ip[k] = *p;
                    }
                    p++;
                    k++;
                }
                s.ip[k] = '\0';
                printf("[wx] ip from URC: %s\r\n", s.ip);
            }

            snprintf(s.ssid, sizeof(s.ssid), "%s", WEATHER_DEFAULT_SSID);
            s.status   = WIFI_STAT_CONNECTED;
            s.ui_dirty = true;
            s.timezone_set = false;
            s.sntp_done = false;
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
            printf("[wx] no +WFJAP in %lums, fallback AT+WFCAP\r\n",
                   (unsigned long)(now - s.connect_t0));
            s.connect_t0 = now;
            if (!at_send_owned("AT+WFCAP", 3000U, CMD_WFCAP))
            {
                printf("[wx] WFCAP busy, retry next tick\r\n");
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

    /* ---------- 5. 天气老化累加 ---------- */
    if (s.wx_valid)
    {
        s.wx_age_ms += WEATHER_TICK_MS;
    }

    /* ---------- 6. 重绘 ---------- */
    if (s.ui_dirty)
    {
        s.ui_dirty = false;
        ui_refresh();
    }
}