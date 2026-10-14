/**
 * @file  at_probe.c
 * @brief Stage-1 diagnostics: AUTO (full chain) + AUTOB (boot auto-reconnect)
 *
 * AUTO  : AT -> AT+(table) -> WFSCAN -> WFCAP/WFJAPA -> NWIP -> SYNTAX group
 *         -> TIME/DATE -> TZSET -> SNTP -> TIME2 -> NWHTC x3 -> RESULT
 *         Closes risk#1 (NWHTC body format), #2 (time cmd), #3 (URL quoting), #5 (long response)
 *
 * AUTOB : risk#4 -- store profile in NVRAM -> query DEFAP/FASTCONN -> WFQAP
 *         -> RESTART -> 45s PASSIVE listen (no command sent) -> verdict
 *
 * NOTE: all console strings are ASCII on purpose. The toolchain reads the
 *       sources as the system codepage (GBK), so Chinese literals come out as
 *       mojibake on the serial terminal. Keep printf() strings ASCII; Chinese
 *       belongs in comments and doc/.
 *
 * NOTE: NWHTC targets are seniverse API only (small JSON). Never point it at
 *       a real web page: baidu.com returns ~667KB and streams for ~60s at
 *       115200, which drowns everything else (2026-10-03 lesson).
 */
#include <stdio.h>
#include <string.h>
#include "at_probe.h"
#include "at_engine.h"
#include "weather_cfg.h"

extern volatile uint32_t usb_tx_drop_cnt;      /* hal_entry.c: USB TX ring drop count */
extern volatile uint32_t wifi_rx_byte_cnt;     /* hal_entry.c: WIFI RX bytes total */
extern volatile uint32_t wifi_tx_done_cnt;     /* hal_entry.c: WIFI TX completions */
extern volatile uint32_t wifi_rx_drop_cnt;     /* hal_entry.c: WIFI RX ring drop count */

/* ===================== 0. tunables ===================== */
#define CONN_URC_TIMEOUT_MS   30000U   /* wait +WFJAP URC */
#define SNTP_SETTLE_MS         8000U   /* settle time after NWSNTP */
#define LISTEN_MS             45000U   /* AUTOB passive listen window */
#define SAMPLE_MAX              96U    /* sample field width */

/* ===================== 1. helpers ===================== */

static const char * res_str (at_result_t r)
{
    switch (r)
    {
        case AT_RES_OK:       return "OK";
        case AT_RES_ERR:      return "ER";
        case AT_RES_TIMEOUT:  return "TMO";
        case AT_RES_ABORT:    return "ABT";
        case AT_RES_BUSY:     return "BSY";
        default:              return "??";
    }
}

/* ===================== 2. HTTP payload analysis (risk#1) =====================
 *
 * NWHTC response layout measured on 2026-10-03 (baidu.com):
 *   OK                              <- sync ack only, 6 bytes total
 *   Set-Cookie: ...                <- HTTP response headers ARE included
 *   Vary: ...
 *   Connection: close
 *   <!DOCTYPE html>...             <- body
 *   +NWHTCSTATUS:0                 <- real terminator
 * So the parser must split on CRLFCRLF before handing anything to cJSON.
 */
typedef struct {
    at_result_t r;
    int         status;            /* +NWHTCSTATUS:<n>, -1 = never seen */
    uint16_t    len;               /* raw bytes collected */
    int         body_off;          /* body start (after CRLFCRLF), -1 = no header */
    uint16_t    body_len;
    bool        overflow;          /* 4KB resp buffer truncated */
    int         err;               /* ERROR:-x */
    bool        hdr_sep;           /* CRLFCRLF present -> HTTP headers present */
    bool        json_direct;       /* body starts with '{' -> feed cJSON as-is */
    bool        json_after_ws;     /* body starts with '{' after whitespace */
    uint16_t    n_content;         /* body lines */
    char        lead;
    char        http_line[SAMPLE_MAX];  /* e.g. "HTTP/1.1 200 OK" */
    char        first[SAMPLE_MAX];      /* first body line */
    char        tail[SAMPLE_MAX];       /* last bytes of the whole packet */
} http_cap_t;

static void cap_reset (http_cap_t * c)
{
    memset(c, 0, sizeof(*c));
    c->r = AT_RES_PENDING;
    c->err = -1;
    c->status = -1;
    c->body_off = -1;
}

/* Find CRLFCRLF; return index just past it, or -1. */
static int find_hdr_end (const char * b, uint16_t n)
{
    uint16_t i;

    for (i = 0U; (i + 3U) < n; i++)
    {
        if (('\r' == b[i]) && ('\n' == b[i + 1]) &&
            ('\r' == b[i + 2]) && ('\n' == b[i + 3]))
        {
            return (int)(i + 4U);
        }
    }
    return -1;
}

/* Copy up to cap-1 bytes, NUL terminate. */
static void samp_set (char * dst, const char * src, size_t n)
{
    if (n > (size_t)(SAMPLE_MAX - 1U))
    {
        n = (size_t)(SAMPLE_MAX - 1U);
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Analyse the engine's current response into a feature table.
 * MUST be called before the next at_engine_start() overwrites resp. */
static void cap_http (http_cap_t * c, at_result_t r)
{
    const at_resp_t * resp = at_engine_resp();
    const char * b = resp->buf;
    uint16_t n = resp->len;
    int he;

    cap_reset(c);
    c->r        = r;
    c->status   = resp->status;
    c->len      = resp->len;
    c->overflow = resp->overflow;
    c->err      = resp->err_code;

    if (0U == n)
    {
        samp_set(c->tail, "", 0);
        return;
    }

    he = find_hdr_end(b, n);
    c->hdr_sep = (he > 0);
    c->body_off = he;
    c->body_len = (uint16_t)((he > 0) ? (int)n - he : (int)n);

    /* HTTP status line = first non-blank line of the packet.
     * Must skip leading CR/LF: the module emits a bare "\r\n" ahead of the
     * response, so scanning from b[0] would yield an empty string. */
    {
        uint16_t i = 0U;
        uint16_t s = 0U;

        while (i < n)
        {
            /* find start of line */
            while ((i < n) && (('\r' == b[i]) || ('\n' == b[i]) || (' ' == b[i]) || ('\t' == b[i])))
            {
                i++;
            }
            s = i;
            while ((i < n) && ('\r' != b[i]) && ('\n' != b[i]))
            {
                i++;
            }
            if (i > s)                                    /* non-blank line found */
            {
                break;
            }
        }
        samp_set(c->http_line, b + s, (size_t)(i - s));
    }

    /* Scan the BODY for: first non-blank char, first line, line count. */
    {
        uint16_t i = (uint16_t)((he > 0) ? he : 0);
        uint16_t end = n;
        bool got_first = false;
        bool lead_ws = true;

        c->n_content = 0U;
        while (i < end)
        {
            char line[SAMPLE_MAX];
            uint16_t ls = i;
            uint16_t ll = 0U;

            while ((i < end) && ('\n' != b[i]))
            {
                if (ll < (SAMPLE_MAX - 1U))
                {
                    line[ll++] = b[i];
                }
                i++;
            }
            if (i < end)
            {
                i++;                                  /* skip '\n' */
            }
            while ((ll > 0U) && ('\r' == line[ll - 1U]))
            {
                ll--;
            }
            line[ll] = '\0';

            if (lead_ws)
            {
                uint16_t k;

                for (k = ls; k < (uint16_t)(ls + ll); k++)
                {
                    if ((' ' != b[k]) && ('\t' != b[k]) && ('\r' != b[k]))
                    {
                        c->lead = b[k];
                        lead_ws = false;
                        break;
                    }
                }
            }

            if (ll > 0U)
            {
                c->n_content++;
                if (!got_first)
                {
                    samp_set(c->first, line, ll);
                    got_first = true;
                }
            }
        }
    }

    c->json_direct   = ('{' == c->lead);
    c->json_after_ws = ('{' == c->lead);

    samp_set(c->tail, (n > (SAMPLE_MAX - 1U)) ? (b + n - (SAMPLE_MAX - 1U)) : b,
             (n > (SAMPLE_MAX - 1U)) ? (size_t)(SAMPLE_MAX - 1U) : (size_t)n);
}

static void cap_print (const char * tag, const http_cap_t * c)
{
    printf("[PROBE] %s\r\n", tag);
    printf("[PROBE]   res=%s nwstatus=%d len=%u ovf=%d err=%d\r\n",
           res_str(c->r), c->status, (unsigned)c->len, (int)c->overflow, c->err);
    printf("[PROBE]   hdr_sep=%d http='%s' body_off=%d body_len=%u nlines=%u\r\n",
           (int)c->hdr_sep, c->http_line, c->body_off,
           (unsigned)c->body_len, (unsigned)c->n_content);
    printf("[PROBE]   json_direct=%d lead='%c'\r\n",
           (int)c->json_direct, ((char)0 != c->lead) ? c->lead : '-');
    printf("[PROBE]   body  : %s\r\n", c->first);
    printf("[PROBE]   tail  : %s\r\n", c->tail);
}

/* ===================== 3. URL builders (seniverse only) ===================== */

static const char * city_or_ip (void)
{
    return ('\0' != WEATHER_CITY[0]) ? WEATHER_CITY : "ip";
}

/* now.json  - the exact URL phase 2 will use */
static const char * url_now (char * buf, uint16_t size)
{
    snprintf(buf, size,
             "http://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=%s",
             WEATHER_API_KEY, city_or_ip(), WEATHER_LANG);
    return buf;
}

/* daily.json - bigger body, proves buffer headroom */
static const char * url_daily (char * buf, uint16_t size)
{
    snprintf(buf, size,
             "http://api.seniverse.com/v3/weather/daily.json?key=%s&location=%s&language=%s",
             WEATHER_API_KEY, city_or_ip(), WEATHER_LANG);
    return buf;
}

/* ===================== 4. AUTO state machine ===================== */

typedef enum {
    PS_IDLE = 0,
    PS_AT, PS_LIST, PS_SCAN,
    PS_CONN1, PS_CONN1_WAIT,                     /* AT+WFCAP + wait +WFJAP URC */
    PS_CONN2, PS_CONN2_WAIT,                     /* AT+WFJAPA fallback + wait URC */
    PS_IP,
    PS_SYNTAX,                                   /* array-driven query/HELP group */
    PS_TIME, PS_DATE, PS_TZSET, PS_SNTP, PS_SNTP_WAIT, PS_TIME2,
    PS_HTTP1, PS_HTTP2, PS_HTTP3,
    PS_REPORT
} probe_step_t;

typedef enum {
    RS_AT = 0, RS_LIST, RS_SCAN, RS_CONN, RS_IP, RS_SYNTAX,
    RS_TIME, RS_TZSET, RS_SNTP, RS_TIME2,
    RS_HTTP1, RS_HTTP2, RS_HTTP3,
    RS_CNT
} probe_rs_t;

static probe_step_t s_step;
static bool         s_running;
static uint32_t     s_deadline;
static char         s_res[RS_CNT][4];
static http_cap_t   s_cap[3];
static uint8_t      s_http_idx;

/* syntax query group: learn the real firmware-reported grammar */
static const struct {
    const char * cmd;
    uint16_t     tmo_ms;
} s_syn[] =
{
    { "AT+TZONE=?",     2500 },
    { "AT+TIME=?",      2500 },
    { "AT+NWSNTP=?",    2500 },
    { "AT+NWIP=?",      2500 },
    { "AT+WFSTAT=?",    2500 },
    { "HELP AT+NWHTC",  3000 },
    { "HELP AT+TIME",   3000 },
    { "HELP AT+NWSNTP", 3000 },
    { "HELP AT+DEFAP",  3000 },
    { "AT+DEFAP=?",     2500 },
    { "AT+GETFASTCONN", 2500 },
};
#define SYN_CNT  ((uint8_t)(sizeof(s_syn) / sizeof(s_syn[0])))

static uint8_t s_syn_idx;
static bool    s_syn_any_bad;

static void set_res (probe_rs_t rs, at_result_t r)
{
    snprintf(s_res[rs], sizeof(s_res[rs]), "%s", res_str(r));
}

static probe_rs_t slot_of (probe_step_t st)
{
    switch (st)
    {
        case PS_AT:       return RS_AT;
        case PS_LIST:     return RS_LIST;
        case PS_SCAN:     return RS_SCAN;
        case PS_CONN1:
        case PS_CONN2:    return RS_CONN;
        case PS_IP:       return RS_IP;
        case PS_SYNTAX:   return RS_SYNTAX;
        case PS_TIME:
        case PS_DATE:     return RS_TIME;
        case PS_TZSET:    return RS_TZSET;
        case PS_SNTP:     return RS_SNTP;
        case PS_TIME2:    return RS_TIME2;
        case PS_HTTP1:    return RS_HTTP1;
        case PS_HTTP2:    return RS_HTTP2;
        case PS_HTTP3:    return RS_HTTP3;
        default:          return RS_AT;
    }
}

static void advance (void);

/* Enter current step and kick off its action; advance on submit failure. */
static void begin (void)
{
    at_result_t r = AT_RES_PENDING;

    switch (s_step)
    {
        case PS_AT:
            printf("[PROBE] > AT\r\n");
            r = at_engine_start("AT", 2000);
            break;

        case PS_LIST:
            printf("[PROBE] > AT+ (command table: NWHTC/TZONE/DEFAP/FASTCONN/PROV*)\r\n");
            r = at_engine_start("AT+", 6000);
            break;

        case PS_SCAN:
            printf("[PROBE] > AT+WFSCAN (risk#5: check [WIFI] lines for gaps/stuck rows)\r\n");
            r = at_engine_start("AT+WFSCAN", 15000);
            break;

        case PS_CONN1:
            printf("[PROBE] > AT+WFCAP (NVRAM profile)\r\n");
            r = at_engine_start("AT+WFCAP", 3000);
            break;

        case PS_CONN1_WAIT:
        case PS_CONN2_WAIT:
            s_deadline = at_engine_now_ms() + CONN_URC_TIMEOUT_MS;
            printf("[PROBE] wait +WFJAP URC (max %us)\r\n",
                   (unsigned)(CONN_URC_TIMEOUT_MS / 1000U));
            return;

        case PS_CONN2:
        {
            char cmd[128];

            printf("[PROBE] > AT+WFJAPA fallback (weather_cfg.h ssid=%s)\r\n",
                   WEATHER_DEFAULT_SSID);
            snprintf(cmd, sizeof(cmd), "AT+WFJAPA=%s,%s",
                     WEATHER_DEFAULT_SSID, WEATHER_DEFAULT_PWD);
            r = at_engine_start(cmd, 5000);
            break;
        }

        case PS_IP:
            printf("[PROBE] > AT+NWIP=?\r\n");
            r = at_engine_start("AT+NWIP=?", 2500);
            break;

        case PS_SYNTAX:
            printf("[PROBE] > %s (syntax %u/%u)\r\n",
                   s_syn[s_syn_idx].cmd, (unsigned)(s_syn_idx + 1U), (unsigned)SYN_CNT);
            r = at_engine_start(s_syn[s_syn_idx].cmd, s_syn[s_syn_idx].tmo_ms);
            break;

        case PS_TIME:
            printf("[PROBE] > AT+TIME (risk#2: reads '+TIME:<date>,<time>')\r\n");
            r = at_engine_start("AT+TIME", 2500);
            break;

        case PS_DATE:
            printf("[PROBE] > AT+DATE (risk#2: expect ERROR, superseded by AT+TIME)\r\n");
            r = at_engine_start("AT+DATE", 2500);
            break;

        case PS_TZSET:
        {
            char cmd[48];

            printf("[PROBE] > AT+TZONE=%d (DESIGN sec5 assumption: seconds)\r\n",
                   (int)(WEATHER_TZ_HOURS * 3600));
            snprintf(cmd, sizeof(cmd), "AT+TZONE=%d", (int)(WEATHER_TZ_HOURS * 3600));
            r = at_engine_start(cmd, 2500);
            break;
        }

        case PS_SNTP:
        {
            char cmd[128];

            printf("[PROBE] > AT+NWSNTP=1,%s,86400 (outbound net check)\r\n", NTP_SERVER_1);
            snprintf(cmd, sizeof(cmd), "AT+NWSNTP=1,%s,86400", NTP_SERVER_1);
            r = at_engine_start(cmd, 6000);
            break;
        }

        case PS_SNTP_WAIT:
            s_deadline = at_engine_now_ms() + SNTP_SETTLE_MS;
            printf("[PROBE] wait %us for NTP settle\r\n", (unsigned)(SNTP_SETTLE_MS / 1000U));
            return;

        case PS_TIME2:
            printf("[PROBE] > AT+TIME again (after SNTP: epoch -> real time?)\r\n");
            r = at_engine_start("AT+TIME", 2500);
            break;

        /* ---- risk#1 + risk#3: NWHTC in PAYLOAD mode ----
         * Bare OK is only the sync ack; the engine now waits for the
         * trailing +NWHTCSTATUS:<n>. Only ONE NWHTC may be in flight. */
        case PS_HTTP1:
        {
            char url[192];

            url_now(url, sizeof(url));
            printf("[PROBE] > AT+NWHTC=<seniverse now.json>,get   (risk#1 raw URL)\r\n");
            {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "AT+NWHTC=%s,get", url);
                r = at_engine_start_ex(cmd, PROBE_HTTP_TIMEOUT_MS, AT_MODE_PAYLOAD);
            }
            break;
        }

        case PS_HTTP2:
        {
            char url[192];

            url_now(url, sizeof(url));
            printf("[PROBE] > AT+NWHTC='<seniverse now.json>',get (risk#3 quoted URL)\r\n");
            {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "AT+NWHTC='%s',get", url);
                r = at_engine_start_ex(cmd, PROBE_HTTP_TIMEOUT_MS, AT_MODE_PAYLOAD);
            }
            break;
        }

        case PS_HTTP3:
        {
            char url[192];

            url_daily(url, sizeof(url));
            printf("[PROBE] > AT+NWHTC=<seniverse daily.json>,get (bigger body / headroom)\r\n");
            {
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "AT+NWHTC=%s,get", url);
                r = at_engine_start_ex(cmd, PROBE_HTTP_TIMEOUT_MS, AT_MODE_PAYLOAD);
            }
            break;
        }

        case PS_REPORT:
        {
            printf("[PROBE] ===== RESULT =====\r\n");
            printf("[PROBE] usb_tx_drop=%lu (must be 0)\r\n", (unsigned long)usb_tx_drop_cnt);
            printf("[PROBE] LINK: wifi_tx_done=%lu wifi_rx_bytes=%lu wifi_rx_drop=%lu\r\n",
                   (unsigned long)wifi_tx_done_cnt, (unsigned long)wifi_rx_byte_cnt,
                   (unsigned long)wifi_rx_drop_cnt);
            printf("[PROBE] AT:%s LIST:%s SCAN:%s CONN:%s IP:%s SYNTAX:%s\r\n",
                   s_res[RS_AT], s_res[RS_LIST], s_res[RS_SCAN], s_res[RS_CONN],
                   s_res[RS_IP], s_res[RS_SYNTAX]);
            printf("[PROBE] TIME:%s TZSET:%s SNTP:%s TIME2:%s\r\n",
                   s_res[RS_TIME], s_res[RS_TZSET], s_res[RS_SNTP], s_res[RS_TIME2]);
            printf("[PROBE] HTTP1:%s HTTP2:%s HTTP3:%s\r\n",
                   s_res[RS_HTTP1], s_res[RS_HTTP2], s_res[RS_HTTP3]);
            printf("[PROBE] --- risk#1: NWHTC payload format ---\r\n");
            cap_print("HTTP1 seniverse now.json  (raw URL)",      &s_cap[0]);
            cap_print("HTTP2 seniverse now.json  (single-quoted)", &s_cap[1]);
            cap_print("HTTP3 seniverse daily.json (bigger body)",  &s_cap[2]);
            printf("[PROBE] risk#1 verdict: hdr_sep=1 => MUST split CRLFCRLF before cJSON;\r\n");
            printf("[PROBE]                json_direct=1 => body feeds cJSON with no cleanup;\r\n");
            printf("[PROBE]                nwstatus=0 => HTTP transaction OK.\r\n");
            printf("[PROBE] risk#3 verdict: compare HTTP1 vs HTTP2 (res/nwstatus/body_len).\r\n");
            printf("[PROBE] risk#5 verdict: ovf=0 AND no [WIFI] gaps AND usb_tx_drop=0.\r\n");
            printf("[PROBE] =================\r\n");
            s_running = false;
            s_step = PS_IDLE;
            return;
        }

        default:
            return;
    }

    if (AT_RES_PENDING != r)                    /* submit failed */
    {
        set_res(slot_of(s_step), r);
        advance();
    }
}

static void advance (void)
{
    switch (s_step)
    {
        case PS_AT:          s_step = PS_LIST;    break;
        case PS_LIST:        s_step = PS_SCAN;    break;
        case PS_SCAN:        s_step = PS_CONN1;   break;
        case PS_CONN1_WAIT:
        case PS_CONN2_WAIT:  s_step = PS_IP;      break;
        case PS_IP:          s_step = PS_SYNTAX;  s_syn_idx = 0U; s_syn_any_bad = false; break;
        case PS_SYNTAX:      s_step = PS_TIME;    break;
        case PS_TIME:        s_step = PS_DATE;    break;
        case PS_DATE:        s_step = PS_TZSET;   break;
        case PS_TZSET:       s_step = PS_SNTP;    break;
        case PS_SNTP:        s_step = PS_SNTP_WAIT; break;
        case PS_SNTP_WAIT:   s_step = PS_TIME2;   break;
        case PS_TIME2:       s_step = PS_HTTP1;   break;
        case PS_HTTP1:       s_step = PS_HTTP2;   break;
        case PS_HTTP2:       s_step = PS_HTTP3;   break;
        case PS_HTTP3:       s_step = PS_REPORT;  break;
        default:             s_step = PS_IDLE;    break;
    }

    begin();
}

static void probe_poll (void)
{
    at_result_t r;

    if (!s_running)
    {
        return;
    }

    /* Connect wait: consume URC (DESIGN 8.1 root cause 1: queue, not pulse flag) */
    if ((PS_CONN1_WAIT == s_step) || (PS_CONN2_WAIT == s_step))
    {
        if (at_engine_urc_take("+WFJAP:1"))
        {
            printf("[PROBE] connected: +WFJAP:1\r\n");
            set_res(RS_CONN, AT_RES_OK);
            advance();
        }
        else if (at_engine_urc_take("+WFJAP:0"))
        {
            printf("[PROBE] connect fail: +WFJAP:0\r\n");
            set_res(RS_CONN, AT_RES_ERR);
            if ((PS_CONN1_WAIT == s_step) && ('\0' != WEATHER_DEFAULT_SSID[0]))
            {
                s_step = PS_CONN2;
                begin();
            }
            else
            {
                advance();
            }
        }
        else if (at_engine_now_ms() >= s_deadline)
        {
            printf("[PROBE] +WFJAP URC timeout\r\n");
            set_res(RS_CONN, AT_RES_TIMEOUT);
            if ((PS_CONN1_WAIT == s_step) && ('\0' != WEATHER_DEFAULT_SSID[0]))
            {
                s_step = PS_CONN2;
                begin();
            }
            else
            {
                advance();
            }
        }

        return;
    }

    if (PS_SNTP_WAIT == s_step)                 /* pure timer, no command */
    {
        if (at_engine_now_ms() >= s_deadline)
        {
            advance();
        }
        return;
    }

    if (PS_IDLE == s_step)
    {
        return;
    }

    r = at_engine_poll();

    if (AT_RES_PENDING == r)
    {
        return;
    }

    if (AT_RES_ERR == r)
    {
        printf("[PROBE] step %d ERROR err_code=%d (step %s)\r\n",
               (int)s_step, at_engine_resp()->err_code, res_str(r));
    }
    else if (AT_RES_TIMEOUT == r)
    {
        printf("[PROBE] step %d TIMEOUT (step %s)\r\n", (int)s_step, res_str(r));
    }

    set_res(slot_of(s_step), r);

    /* NWHTC: snapshot + analyse BEFORE the next start overwrites resp */
    if ((PS_HTTP1 == s_step) || (PS_HTTP2 == s_step) || (PS_HTTP3 == s_step))
    {
        cap_http(&s_cap[s_http_idx], r);
        if (s_http_idx < 2U)
        {
            s_http_idx++;
        }
    }

    /* connect has a two-stage flow */
    if (PS_CONN1 == s_step)
    {
        if (AT_RES_OK == r)
        {
            s_step = PS_CONN1_WAIT;
        }
        else if ('\0' != WEATHER_DEFAULT_SSID[0])
        {
            s_step = PS_CONN2;
        }
        else
        {
            s_step = PS_IP;
        }
        begin();
        return;
    }
    if (PS_CONN2 == s_step)
    {
        s_step = (AT_RES_OK == r) ? PS_CONN2_WAIT : PS_IP;
        begin();
        return;
    }

    /* syntax group: run every query, then move on */
    if (PS_SYNTAX == s_step)
    {
        if (AT_RES_OK != r)
        {
            s_syn_any_bad = true;
        }
        s_syn_idx++;
        if (s_syn_idx < SYN_CNT)
        {
            begin();
        }
        else
        {
            set_res(RS_SYNTAX, s_syn_any_bad ? AT_RES_ERR : AT_RES_OK);
            advance();
        }
        return;
    }

    advance();
}

/* ===================== 5. AUTOB: boot auto-reconnect (risk#4) ===================== */

typedef enum {
    PB_IDLE = 0,
    PB_STORE, PB_STORE_W,                         /* store NVRAM profile + wait +WFJAP:1 */
    PB_SYN,                                       /* query DEFAP / FASTCONN / PROVSTAT */
    PB_QAP,                                       /* disconnect, clean state */
    PB_RESTART,                                   /* reboot module */
    PB_LISTEN,                                    /* 45s passive listen, NO command */
    PB_REPORT
} pboot_step_t;

/* queries that reveal how (or whether) the module auto-reconnects on boot */
static const struct {
    const char * cmd;
    uint16_t     tmo_ms;
} s_pbsyn[] =
{
    { "AT+DEFAP=?",      2500 },
    { "AT+GETFASTCONN",  2500 },
    { "AT+WFSTAT=?",     2500 },
    { "AT+PROVSTAT",     2500 },
};
#define PBSYN_CNT  ((uint8_t)(sizeof(s_pbsyn) / sizeof(s_pbsyn[0])))

static pboot_step_t s_pb_step;
static bool         s_pb_running;
static uint32_t     s_pb_deadline;
static at_result_t  s_pb_store_res;
static bool         s_pb_stored;
static uint8_t      s_pbsyn_idx;

static void pb_begin (void)
{
    at_result_t r = AT_RES_PENDING;

    switch (s_pb_step)
    {
        case PB_STORE:
            printf("[PROBE] ==== AUTOB: risk#4 NVRAM profile boot auto-reconnect ====\r\n");
            printf("[PROBE] > AT+WFJAPA=<ssid>,<pwd> (store into NVRAM; pwd not echoed)\r\n");
            {
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "AT+WFJAPA=%s,%s",
                         WEATHER_DEFAULT_SSID, WEATHER_DEFAULT_PWD);
                r = at_engine_start(cmd, 5000);
            }
            break;

        case PB_STORE_W:
            s_pb_deadline = at_engine_now_ms() + CONN_URC_TIMEOUT_MS;
            printf("[PROBE] wait +WFJAP:1 (max %us) to confirm profile stored\r\n",
                   (unsigned)(CONN_URC_TIMEOUT_MS / 1000U));
            return;

        case PB_SYN:
            printf("[PROBE] > %s (auto-reconnect knobs %u/%u)\r\n",
                   s_pbsyn[s_pbsyn_idx].cmd, (unsigned)(s_pbsyn_idx + 1U),
                   (unsigned)PBSYN_CNT);
            r = at_engine_start(s_pbsyn[s_pbsyn_idx].cmd, s_pbsyn[s_pbsyn_idx].tmo_ms);
            break;

        case PB_QAP:
            printf("[PROBE] > AT+WFQAP (disconnect, clean state)\r\n");
            r = at_engine_start("AT+WFQAP", 5000);
            break;

        case PB_RESTART:
            printf("[PROBE] > AT+RESTART (module reboot; watch for +INIT:DONE)\r\n");
            r = at_engine_start("AT+RESTART", 5000);
            break;

        case PB_LISTEN:
            /* KEY: drop the store-phase +WFJAP:1 so only a SPONTANEOUS one counts. */
            at_engine_urc_seen_clear();
            at_engine_urc_trace(true);
            s_pb_deadline = at_engine_now_ms() + LISTEN_MS;
            printf("[PROBE] >>> PASSIVE LISTEN %us: NO command will be sent <<<\r\n",
                   (unsigned)(LISTEN_MS / 1000U));
            printf("[PROBE] >>> verdict = did +WFJAP:1 appear ON ITS OWN? <<<\r\n");
            return;

        case PB_REPORT:
        {
            bool auto_reconn;

            at_engine_urc_trace(false);
            auto_reconn = at_engine_urc_seen("+WFJAP:1");

            printf("[PROBE] ===== AUTOB RESULT (risk#4) =====\r\n");
            printf("[PROBE] profile store : res=%s  +WFJAP:1 seen=%s\r\n",
                   res_str(s_pb_store_res), s_pb_stored ? "YES" : "NO");
            if (!s_pb_stored)
            {
                printf("[PROBE] VERDICT: INCONCLUSIVE - profile not stored, retest\r\n");
            }
            else
            {
                printf("[PROBE] spontaneous +WFJAP:1 in %us idle : %s\r\n",
                       (unsigned)(LISTEN_MS / 1000U), auto_reconn ? "YES" : "NO");
                printf("[PROBE] VERDICT: boot auto-reconnect = %s\r\n",
                       auto_reconn ? "YES (NVRAM profile connects on power-up)"
                                    : "NO  (needs AT+WFCAP fallback at every boot)");
            }
            printf("[PROBE] see [URC] lines above for the full timeline\r\n");
            printf("[PROBE] ====================================\r\n");
            s_pb_running = false;
            s_pb_step = PB_IDLE;
            return;
        }

        default:
            return;
    }

    if (AT_RES_PENDING != r)                    /* submit failed */
    {
        if (PB_STORE == s_pb_step)
        {
            s_pb_store_res = r;
            s_pb_stored = false;
            s_pb_step = PB_SYN;                  /* store cmd failed, skip the wait */
        }
        else
        {
            s_pb_step = (PB_RESTART == s_pb_step) ? PB_LISTEN
                     : ((PB_LISTEN  == s_pb_step) ? PB_REPORT : PB_RESTART);
        }
        pb_begin();
    }
}

static void pb_poll (void)
{
    at_result_t r;

    if (!s_pb_running)
    {
        return;
    }

    if (PB_STORE_W == s_pb_step)
    {
        if (at_engine_urc_take("+WFJAP:1"))
        {
            printf("[PROBE] profile stored (+WFJAP:1)\r\n");
            s_pb_stored = true;
            s_pb_store_res = AT_RES_OK;
            s_pb_step = PB_SYN;
            s_pbsyn_idx = 0U;
            pb_begin();
        }
        else if (at_engine_now_ms() >= s_pb_deadline)
        {
            printf("[PROBE] +WFJAP:1 timeout -> profile NOT stored\r\n");
            s_pb_stored = false;
            s_pb_store_res = AT_RES_TIMEOUT;
            s_pb_step = PB_SYN;
            s_pbsyn_idx = 0U;
            pb_begin();
        }
        return;
    }

    if (PB_LISTEN == s_pb_step)
    {
        if (at_engine_now_ms() >= s_pb_deadline)
        {
            s_pb_step = PB_REPORT;
            pb_begin();
        }
        return;
    }

    if (PB_IDLE == s_pb_step)
    {
        return;
    }

    r = at_engine_poll();
    if (AT_RES_PENDING == r)
    {
        return;
    }

    if (PB_STORE == s_pb_step)
    {
        s_pb_store_res = r;
        s_pb_stored = false;
        s_pb_step = (AT_RES_OK == r) ? PB_STORE_W : PB_SYN;
        s_pbsyn_idx = 0U;
        pb_begin();
        return;
    }

    if (PB_SYN == s_pb_step)
    {
        s_pbsyn_idx++;
        if (s_pbsyn_idx < PBSYN_CNT)
        {
            pb_begin();                         /* next query */
        }
        else
        {
            s_pb_step = PB_QAP;
            pb_begin();
        }
        return;
    }

    s_pb_step = (PB_RESTART == s_pb_step) ? PB_LISTEN : PB_RESTART;
    pb_begin();
}

/* ===================== 6. public API ===================== */

bool at_probe_running (void)
{
    return s_running || s_pb_running;
}

void at_probe_start (void)
{
    int i;

    if (s_running || s_pb_running)
    {
        printf("[USB] probe already running, type ABORT first\r\n");
        return;
    }

    for (i = 0; i < (int)RS_CNT; i++)
    {
        strncpy(s_res[i], "--", sizeof(s_res[i]) - 1U);
        s_res[i][sizeof(s_res[i]) - 1U] = '\0';
    }
    for (i = 0; i < 3; i++)
    {
        cap_reset(&s_cap[i]);
    }
    s_http_idx = 0U;
    s_syn_idx = 0U;
    s_syn_any_bad = false;

    s_running = true;
    s_step = PS_AT;
    printf("[PROBE] start: AT -> LIST -> SCAN -> CONN -> IP -> SYNTAX -> TIME/TZ/SNTP -> NWHTCx3\r\n");
    printf("[PROBE] (ABORT to stop; input is masked while running)\r\n");
    begin();
}

void at_probe_boot_start (void)
{
    if (s_running || s_pb_running)
    {
        printf("[USB] probe already running, type ABORT first\r\n");
        return;
    }

    if ('\0' == WEATHER_DEFAULT_SSID[0])
    {
        printf("[USB] AUTOB needs WEATHER_DEFAULT_SSID in weather_cfg.h\r\n");
        return;
    }

    s_pb_running = true;
    s_pb_step = PB_STORE;
    s_pb_stored = false;
    s_pbsyn_idx = 0U;
    s_pb_store_res = AT_RES_PENDING;
    at_engine_urc_seen_clear();
    pb_begin();
}

void at_probe_abort (void)
{
    if (!at_probe_running())
    {
        return;
    }

    at_engine_abort();
    at_engine_urc_trace(false);
    s_running = false;
    s_step = PS_IDLE;
    s_pb_running = false;
    s_pb_step = PB_IDLE;
    printf("[PROBE] aborted\r\n");
}

void at_probe_poll (void)
{
    if (s_pb_running)
    {
        pb_poll();
        return;
    }
    if (s_running)
    {
        probe_poll();
    }
}