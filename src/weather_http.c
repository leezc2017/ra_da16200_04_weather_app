/**
 * @file  weather_http.c
 * @brief NWHTC HTTP 取数封装实现
 *
 * 状态机（每次 GET 走一遍）：
 *   IDLE --start--> SEND --(engine OK)--> WAIT_STATUS --(+NWHTCSTATUS)--> DONE
 *                                                                  |
 *                                                          (超时/ERROR)--> FAILED
 *
 * 所有步骤都由 weather_http_poll() 推进，单次调用阻塞 <= 1 tick（§4 硬性要求）。
 */
#include <stdio.h>
#include <string.h>
#include "weather_http.h"
#include "at_engine.h"

#define URL_BUF_MAX   224U    /* AT_CMD_MAX(180) 之上够放 seniverse 的 113 字节 URL */
#define CMD_BUF_MAX   256U    /* at_engine_start_ex 要求 <= AT_CMD_MAX-2 */

typedef enum {
    WH_IDLE = 0,
    WH_SEND,                       /* 已投递, 等引擎走到 WAIT */
    WH_WAIT_STATUS,                /* 引擎在 PAYLOAD 模式收集中 */
    WH_DONE,
    WH_FAILED
} wh_state_t;

static struct {
    wh_state_t          state;
    weather_http_result_t res;
    char                 body[WEATHER_HTTP_BODY_MAX + 1U];
    uint16_t             body_len;
    char                 content_type[48];
} s;

static const char * res_str (at_result_t r)
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

/**
 * @brief 从整个响应里切出 body 与 Content-Type
 *
 * 响应形如：
 *   \r\nHTTP/1.1 200 OK\r\nContent-Type: ...\r\n...\r\n\r\n<json>
 * 首个 CRLF 是模块加的引子，故先跳到第一行非空白再找 CRLFCRLF。
 *
 * @param buf  原始响应
 * @param n    长度
 * @param out_body     输出 body 起始指针
 * @param out_body_len 输出 body 长度
 * @param out_status   输出 HTTP 状态码(解析自首行)
 */
static void split_http (const char * buf, uint16_t n,
                        const char ** out_body, uint16_t * out_body_len,
                        int * out_status)
{
    uint16_t i = 0U;
    int hdr_end = -1;
    int code = 0;

    *out_body     = buf;
    *out_body_len = 0U;
    *out_status   = 0;

    /* 1) 定位状态行。
     *
     * ⚠ 不能假设 buf 开头就是状态行（2026-10-04 实测踩坑）：
     *   AT+NWHTC 发完命令后模块**先回一个 6 字节裸 OK**（同步回执），
     *   正文才异步流式推送。所以 resp.buf 的真实内容是：
     *       "OK\r\nHTTP/1.1 200 OK\r\nDate: ...\r\n\r\n{json}"
     *   旧实现从 buf[0] 开始找"第一个空格后的数字"，在 "OK" 这一行里
     *   找不到空格，直接越到行尾 -> http_status 恒为 0 ->
     *   fetch_weather_done() 判 !=200 -> 打印 FAIL -> wx_valid 永远为 false
     *   -> OLED 一直 "waiting data.."。实测响应确实完整到达（能看到
     *   +NWHTCSTATUS:0），但从没进到 cJSON。
     *
     *   正解：先找 "HTTP/" 再从它后面取状态码。
     */
    for (i = 0U; (i + 5U) < n; i++)
    {
        if (0 == strncmp(&buf[i], "HTTP/", 5U))
        {
            break;
        }
    }
    if ((i + 5U) < n)
    {
        uint16_t p = (uint16_t)(i + 5U);      /* 跳过 "HTTP/" */

        while ((p < n) && (' ' != buf[p]) && ('\r' != buf[p]) && ('\n' != buf[p]))
        {
            p++;                             /* 跳过 "1.1" */
        }
        while ((p < n) && (' ' == buf[p]))
        {
            p++;
        }
        while ((p < n) && (buf[p] >= '0') && (buf[p] <= '9'))
        {
            code = (code * 10) + (buf[p] - '0');
            p++;
        }
    }

    /* 2) 找 CRLFCRLF */
    for (i = 0U; (i + 3U) < n; i++)
    {
        if (('\r' == buf[i]) && ('\n' == buf[i + 1]) &&
            ('\r' == buf[i + 2]) && ('\n' == buf[i + 3]))
        {
            hdr_end = (int)(i + 4U);
            break;
        }
    }

    *out_status = code;
    if (hdr_end > 0)
    {
        *out_body     = &buf[hdr_end];
        *out_body_len = (uint16_t)(n - (uint16_t)hdr_end);
    }
    else
    {
        /* 没有头分隔符：整包当作 body（防御性，正常不会走到） */
        *out_body     = buf;
        *out_body_len = n;
    }
}

/**
 * @brief 从响应头里取 Content-Type
 * @param buf 响应
 * @param n   长度
 * @param end 头结束偏移(body 起点)
 * @param out 输出缓冲
 * @param size 输出缓冲大小
 */
static void grab_content_type (const char * buf, uint16_t n, int end,
                               char * out, uint16_t size)
{
    static const char key[] = "Content-Type:";
    const uint16_t klen = (uint16_t)(sizeof(key) - 1U);
    uint16_t i;

    out[0] = '\0';
    if (end <= 0)
    {
        return;
    }

    for (i = 0U; ((uint16_t)(i + klen) < (uint16_t)end) && ((uint16_t)(i + klen) <= n); i++)
    {
        if (0 == strncmp(&buf[i], key, klen))
        {
            uint16_t v = (uint16_t)(i + klen);
            uint16_t o = 0U;

            while ((v < (uint16_t)end) && ('\r' != buf[v]) && ('\n' != buf[v]) && ('\0' != buf[v]))
            {
                if ((' ' == buf[v]) || ('\t' == buf[v]))
                {
                    if (0U == o)
                    {
                        v++;                    /* 跳过后续空白, 必须先前进否则连续空白会死循环 */
                        continue;
                    }
                }
                if (o < (uint16_t)(size - 1U))
                {
                    out[o++] = buf[v];
                }
                v++;
            }
            out[o] = '\0';
            return;
        }
    }
}

bool weather_http_get_start (const char * url, uint32_t timeout_ms)
{
    static char cmd[CMD_BUF_MAX];
    size_t url_len;
    at_result_t r;

    if ((NULL == url) || (WH_IDLE != s.state))
    {
        if (WH_IDLE != s.state)
        {
            printf("[wx] http busy, skip\r\n");
        }
        return false;
    }

    url_len = strlen(url);
    if ((0U == url_len) || ((url_len + 16U) >= (size_t)AT_CMD_MAX))
    {
        printf("[wx] url too long: %lu\r\n", (unsigned long)url_len);
        return false;
    }

    /* 命令总长 = "AT+NWHTC=" + url + ",get" + 自动补的 CRLF，必须 < AT_CMD_MAX */
    snprintf(cmd, sizeof(cmd), "AT+NWHTC=%s,get", url);

    memset(&s.res, 0, sizeof(s.res));
    s.res.len          = -1;
    s.res.http_status  = 0;
    s.res.nw_status    = -1;
    s.body_len         = 0U;
    s.body[0]          = '\0';
    s.content_type[0]  = '\0';

    r = at_engine_start_ex(cmd, timeout_ms, AT_MODE_PAYLOAD);
    if (AT_RES_PENDING != r)
    {
        printf("[wx] http start rejected: %s\r\n", res_str(r));
        s.state = WH_IDLE;
        return false;
    }

    s.state = WH_WAIT_STATUS;      /* 已在途, 等 weather_task 轮询引擎后回调 */
    return true;
}

/**
 * @brief 引擎命令完成时由 weather_task 调用，解析响应
 *
 * ⚠ 本函数**不**自己调 at_engine_poll()：引擎轮询权归 weather_task（单飞行）。
 *
 * @param r 引擎结果
 */
void weather_http_on_engine_done (at_result_t r)
{
    const at_resp_t * resp;
    const char * body = NULL;
    uint16_t body_len = 0U;
    int http_status = 0;

    if ((WH_WAIT_STATUS != s.state) && (WH_SEND != s.state))
    {
        return;                                 /* 不是本模块的命令 */
    }

    s.res.nw_status = at_engine_resp()->status;

    /* ⚠ 永远打印（不受任何开关控制）：这是 weather_http 的入口诊断，
     * 回答"引擎收口时到底带了多少字节进来"。NWHTC 期间 [WIFI] 回显已关，
     * 所以它必然紧跟在 [NWHTC] closed 之后，是窗口内仅剩的两行输出。 */
    printf("[wx] http done r=%s nw=%d rxlen=%u\r\n",
           res_str(r), s.res.nw_status, (unsigned)at_engine_resp()->len);

    if (AT_RES_OK != r)
    {
        printf("[wx] http failed: %s nwstatus=%d err=%d\r\n",
               res_str(r), s.res.nw_status, at_engine_resp()->err_code);
        s.res.len = -1;
        s.state   = WH_FAILED;
        return;
    }

    resp = at_engine_resp();
    if (resp->overflow)
    {
        printf("[wx] http resp overflow (>%uB), raise AT_RESP_BUF_SIZE\r\n",
               (unsigned)AT_RESP_BUF_SIZE);
    }

    split_http(resp->buf, resp->len, &body, &body_len, &http_status);
    grab_content_type(resp->buf, resp->len,
                      (int)resp->len - (int)body_len, s.content_type,
                      sizeof(s.content_type));

    if (body_len > WEATHER_HTTP_BODY_MAX)
    {
        body_len = WEATHER_HTTP_BODY_MAX;       /* 截断, JSON 会解析失败 */
    }
    memcpy(s.body, body, body_len);
    s.body[body_len]  = '\0';
    s.body_len        = body_len;
    s.res.len         = (int)body_len;
    s.res.http_status = http_status;

    printf("[wx] http ok: nwstatus=%d http=%d ctype=%s body=%uB\r\n",
           s.res.nw_status, http_status, s.content_type, (unsigned)body_len);

    /* ⚠ 把原始响应和 JSON 正文原样打出来（2026-10-04）。
     *
     * 之前 [WIFI] 回显在 payload 期间被关掉了（为了不让 HTTP 头把结论挤出
     * 串口窗口），但那样一来**响应内容自己也看不到**了，出现"收到多少字节、
     * 头长什么样、JSON 完整不完整"都无法核对。这里补回来，但只打**头 120 字节
     * + 正文 200 字节**，远小于原来 15 行的全量回显，窗口放得下。
     *
     * 用成对的 BEGIN/END 包起来（2026-10-04 追加）：串口窗口只有约 44 行，
     * 抓取很容易从中间开始，没有边界就分不清"这是正文"还是"这是头部"，
     * 也看不出这段是不是被窗口截断了。边界让任何一次抓取都自带上下文。
     *
     * 关键背景：AT+NWHTC 的真实字节流是
     *     "OK\r\nHTTP/1.1 200 OK\r\n<headers>\r\n\r\n{json}"
     * 开头那个裸 OK 是同步回执，会把"buf 开头即状态行"的假设打破（已修）。
     */
    printf("[RX] ===== HTTP BEGIN =====\r\n");
    {
        uint16_t show = (resp->len < 120U) ? resp->len : 120U;

        printf("[RX] resp_len=%u head(%u)=%.*s\r\n",
               (unsigned)resp->len, (unsigned)show, (int)show, resp->buf);
    }
    {
        uint16_t show = (body_len < 200U) ? body_len : 200U;

        printf("[RX] body(%u/%u)=%.*s\r\n",
               (unsigned)show, (unsigned)body_len, (int)show, body);
        if (show < body_len)
        {
            printf("[RX] ... body truncated in this print, total=%uB\r\n",
                   (unsigned)body_len);
        }
    }
    printf("[RX] ===== HTTP END =====\r\n");

    s.state = WH_DONE;
}

bool weather_http_busy (void)
{
    return ((WH_SEND == s.state) || (WH_WAIT_STATUS == s.state));
}

bool weather_http_done (void)
{
    return ((WH_DONE == s.state) || (WH_FAILED == s.state));
}

/**
 * @brief 取走结果（消费后回到 IDLE，允许下一次请求）
 */
void weather_http_clear (void)
{
    s.state = WH_IDLE;
}

const weather_http_result_t * weather_http_result (void)
{
    return &s.res;
}

const char * weather_http_body (uint16_t * size)
{
    if (NULL != size)
    {
        *size = s.body_len;
    }
    return s.body;
}