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
#include <stddef.h>
#include <stdint.h>

#include "weather_task.h"
#include "weather_cfg.h"

#include "at_engine.h"
#include "weather_http.h"
#include "weather_parse.h"
#include "weather_text.h"
#include "weather_ui.h"
#include "key_scan.h"
#include "oled_port.h"
#include "provisioning_portal.h"
#include "hal_data.h"

/* daily 数据缺失时的高频重试（不再等满一个 refresh 周期） */
#define WEATHER_DAILY_RETRY        3U      /* 每轮最多补抓次数 */
#define WEATHER_DAILY_RETRY_GAP_MS 30000U  /* 两次补抓间隔 */

/** 命令归属：决定引擎结果交给谁处理 */
typedef enum {
    CMD_NONE = 0,      /**< 无命令在途 */
    CMD_AT,            /**< AT 自检: 启动时先验证模块响应 */
    CMD_BOOT_STA,      /**< Force DA16200 into STA mode during RA boot */
    CMD_TIME,          /**< AT+TIME */
    CMD_TZSET,         /**< AT+TZONE=<sec> */
    CMD_SNTP,          /**< AT+NWSNTP=... */
    CMD_NWIP,          /**< AT+NWIP=?: 读接口 IP（只用于取地址，不用于判断连接） */
    CMD_WFSTA,         /**< AT+WFSTA: 权威连接状态（手册 L1149：+WFSTA:1 已关联 / 0 未关联） */
    CMD_WFCAP,         /**< AT+WFCAP: 用 NVRAM 已存 profile 连接 */
    CMD_WFJAPA,        /**< AT+WFJAPA: 连接并写入 NVRAM */
    CMD_PROV_AT,       /**< Provisioning control AT command */
    CMD_PROV_RAW,      /**< ESC-S TCP reply is in flight */
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

/* Non-blocking DA16200 SoftAP portal phases. Every AT phase advances only after
 * its command completes; reboot/TCP response delays are deadline based. */
#define PROV_STEP_IDLE          0U
#define PROV_STEP_AP_DISCONNECT 1U
#define PROV_STEP_AP_MODE       2U
#define PROV_STEP_AP_PROFILE    3U
#define PROV_STEP_AP_REBOOT     4U
#define PROV_STEP_AP_BOOT       5U
#define PROV_STEP_AP_IP         6U
#define PROV_STEP_DHCP_STOP     7U
#define PROV_STEP_DHCP_RANGE    8U
#define PROV_STEP_DHCP_START    9U
#define PROV_STEP_TCP_MODE      10U
#define PROV_STEP_TCP_LISTEN    11U
#define PROV_STEP_SERVING       12U
#define PROV_STEP_TX_PENDING    13U
#define PROV_STEP_STA_TEARDOWN  14U
#define PROV_STEP_STA_MODE      15U
#define PROV_STEP_STA_REBOOT    16U
#define PROV_STEP_STA_BOOT      17U
#define PROV_STEP_STA_JOIN      18U
#define PROV_STEP_FAILED        19U
#define PROV_STEP_AP_BOOT_DONE  21U
#define PROV_STEP_TCP_CLOSE     22U

#define PROV_AP_BOOT_DELAY_MS   4000U
#define PROV_STA_BOOT_DELAY_MS  4000U
#define PROV_TCP_REPLY_DELAY_MS 150U
#define WIFI_HEALTH_POLL_MS     5000U

#define BOOT_STA_STEP_WFTAP      0U
#define BOOT_STA_STEP_WFMODE     1U
#define BOOT_STA_STEP_RESTART    2U
#define BOOT_STA_STEP_WAIT_INIT  3U
#define BOOT_STA_STEP_READY      4U
#define BOOT_STA_INIT_TIMEOUT_MS 8000U

/* Reserve the final 384 bytes of the 8 KiB Data Flash for two settings slots.
 * Slot/block geometry is asserted against the RA4M2 FSP capabilities below. */
#define WEATHER_CFG_FLASH_BASE       (0x08001E80UL)
#define WEATHER_CFG_SLOT_SIZE        (192U)
#define WEATHER_CFG_SLOT_COUNT       (2U)
#define WEATHER_CFG_MAGIC            (0x57434647UL)
#define WEATHER_CFG_VERSION          (1U)
#define WEATHER_CFG_COMMIT           (0x434F4D54UL)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t sequence;
    char ssid[33];
    char password[64];
    char city[33];
    uint32_t crc32;
    uint32_t commit;
    uint8_t reserved[40];
} weather_cfg_record_t;

_Static_assert(sizeof(weather_cfg_record_t) == WEATHER_CFG_SLOT_SIZE,
               "weather config slot must be 192 bytes");
_Static_assert((WEATHER_CFG_SLOT_SIZE % BSP_FEATURE_FLASH_HP_DF_BLOCK_SIZE) == 0U,
               "config slot must align to Data Flash erase blocks");
_Static_assert((WEATHER_CFG_SLOT_SIZE % BSP_FEATURE_FLASH_HP_DF_WRITE_SIZE) == 0U,
               "config slot must align to Data Flash write units");


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
    bool    refresh_pending;     /**< 手动刷新请求，网络不可用时保留 */
    uint8_t tz_fail;            /**< TZONE 连续失败次数, 到 TZ_MAX_FAIL 就不再堵链路 */
    char    time_str[6];        /**< 最近一次 AT+TIME 的 "HH:MM" */

    /* 天气 */
    weather_data_t wx;
    bool           wx_valid;
    uint32_t       wx_age_ms;
    /* daily data missing -> limited quick retries (user issue: TODAY/wind
     * rows stayed empty until the next full refresh). */
    uint32_t       daily_retry_due_ms;
    uint8_t        daily_retry_left;

    /* UI */
    uint8_t  page;              /**< 0=主页 1=WiFi信息 */
    bool     ui_dirty;
    char     ip[16];
    char     city[33];
    char     ssid[33];
    char     provision_password[64];
    char     provision_city[33];
    uint8_t  fails;

    /* 命令归属 */
    cmd_owner_t cmd;

    /* 联网阶梯(见 WIFI_STEP_* 注释) */
    uint8_t  wifi_step;
    uint8_t  wifi_sta_ok;        /**< AT+WFSTA 最近一次回答: 1=已关联 0=未关联 */
    uint16_t ladder_sig;         /**< 已打印的 (step<<8|err)，用于日志限流 */
    uint8_t  ladder_nprint;      /**< 同一签名已重复打印次数 */
    uint8_t  wifi_try;            /**< 当前阶梯已尝试次数, 用于退避 */
    uint32_t wifi_health_poll_ms;

    /* 心跳诊断(WEATHER_HB_DIAG_MS=0 时不打印) */
    uint32_t hb_t0;
    uint32_t hb_fast;      /**< fetch 在途时的 1s 心跳时基 */
    uint32_t fetch_t0;     /**< 本次 fetch 发起时刻, 供 OLED 显示已等秒数 */
    uint32_t fetch_sec;    /**< 上次刷新的已等秒数, 变了就置 ui_dirty 重绘 */
    bool boot_sta_ready;
    uint8_t boot_sta_step;
    uint8_t boot_sta_command_step;
    uint32_t boot_sta_deadline;
    uint8_t boot_sta_restart_retries;
    bool provision_requested;
    bool provision_active;
    bool provision_config_pending;
    bool provision_connect_accepted;
    bool provision_sta_connected;
    uint8_t provision_step;
    uint8_t provision_command_step;
    uint32_t provision_deadline;
    char provision_peer_ip[16];
    uint16_t provision_peer_port;
    char provision_rx[PROVISIONING_REQUEST_MAX + 1U];
    uint16_t provision_rx_len;
    char provision_tx[PROVISIONING_TX_MAX];
    uint16_t provision_tx_len;
    uint32_t provision_tx_done_mark;
    uint8_t provision_tx_next_step;
    bool provision_city_valid;
} s;

extern bool wifi_uart_write_async(const uint8_t *data, uint16_t length);
extern bool wifi_uart_write_busy(void);
extern uint32_t wifi_uart_write_done_count(void);

static bool provision_cmd_complete(at_result_t result);
static void provision_poll(uint32_t now);
static void provision_start(void);
static void provision_http_service(void);
static bool provision_send_at(const char *command, uint32_t timeout_ms, uint8_t next);
static bool provision_send_tcp(const char *payload, size_t length, uint8_t next);
static bool weather_cfg_load(void);
static bool weather_cfg_save(const provisioning_config_t *config);
static void weather_cfg_restore_defaults(void);
static void boot_sta_poll(uint32_t now);
static void wifi_link_lost(const char *reason);
static void wifi_health_poll(uint32_t now);

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
        case CMD_BOOT_STA:return "BOOT-STA";
        case CMD_TIME:    return "TIME";
        case CMD_TZSET:   return "TZONE";
        case CMD_SNTP:    return "SNTP";
        case CMD_NWIP:    return "NWIP";
        case CMD_WFSTA:   return "WFSTA";
        case CMD_WFCAP:   return "WFCAP";
        case CMD_WFJAPA:  return "WFJAPA";
        case CMD_PROV_AT: return "PROV-AT";
        case CMD_PROV_RAW:return "PROV-TX";
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
    s.wifi_health_poll_ms = at_engine_now_ms();
    s.timezone_set = false;
    s.sntp_done    = false;
    s.tz_fail      = 0U;      /* 每次重新连上都要清零, 否则沿用上一轮的失败计数 */
    s.wifi_try     = 0U;
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
 * @brief 从 +WFJAP:1 行里取出真实的 SSID
 *
 * 实测格式: +WFJAP:1,'CMCC-xxxx',192.168.1.2 —— SSID 是第一个引号对里的字段。
 * 模块实际加入哪一路, 就把哪一路回填进 s.ssid: 修复"联网中/失败页 SSID 显示
 * 问号"(配置还没记住时, 屏幕上根本没有可显示的 SSID)。
 *
 * @param line 输入行（不含 CRLF）
 * @param out  输出缓冲
 * @param size 缓冲大小
 * @return true=取到非空 SSID
 */
static bool parse_ssid (const char * line, char * out, uint16_t size)
{
    const char * p;
    uint16_t k = 0U;

    if ((NULL == line) || (NULL == out) || (size < 2U))
    {
        return false;
    }

    p = strchr(line, '\'');
    if (NULL == p)
    {
        return false;
    }
    p++;
    while ((*p != '\0') && (*p != '\'') && (*p != '\r') && (*p != '\n') &&
           (k < (uint16_t)(size - 1U)))
    {
        out[k++] = *p++;
    }
    out[k] = '\0';
    return (0U != k);
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

    if (s.provision_active && (owner != CMD_PROV_AT) &&
        !((owner == CMD_WFJAPA) && (s.provision_step == PROV_STEP_STA_JOIN)))
        return false;
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

static bool provision_send_at(const char *command, uint32_t timeout_ms, uint8_t next)
{
    s.provision_command_step = s.provision_step;
    if (!at_send_owned(command, timeout_ms, CMD_PROV_AT)) return false;
    s.provision_step = next;
    return true;
}

static bool provision_send_tcp(const char *payload, size_t length, uint8_t next)
{
    char prefix[64];
    int prefix_len;
    if ((NULL == payload) || (0U == length) ||
        (length + 64U > sizeof(s.provision_tx)) || (s.provision_peer_port == 0U) ||
        wifi_uart_write_busy()) return false;

    /* UM-WI-003 p.116: ESC S <cid><length>,<remote_ip>,<remote_port>,r,<data>.
     * Shift first because callers may pass s.provision_tx as the payload. */
    prefix_len = snprintf(prefix, sizeof(prefix), "\x1B" "S0%u,%s,%u,r,", (unsigned)length,
                          s.provision_peer_ip, (unsigned)s.provision_peer_port);
    if ((prefix_len <= 0) || ((size_t)prefix_len >= sizeof(prefix)) ||
        ((size_t)prefix_len + length > sizeof(s.provision_tx))) return false;
    memmove(s.provision_tx + (size_t)prefix_len, payload, length);
    memcpy(s.provision_tx, prefix, (size_t)prefix_len);
    s.provision_tx_len = (uint16_t)((size_t)prefix_len + length);
    s.provision_tx_done_mark = wifi_uart_write_done_count();
    s.provision_tx_next_step = next;
    if (!wifi_uart_write_async((const uint8_t *)s.provision_tx, s.provision_tx_len)) return false;
    s.provision_step = PROV_STEP_TX_PENDING;
    s.cmd = CMD_PROV_RAW;
    return true;
}

static uint32_t weather_cfg_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;
    for (i = 0U; i < length; i++)
    {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return ~crc;
}

static bool weather_cfg_record_valid(const weather_cfg_record_t *record)
{
    if ((record->magic != WEATHER_CFG_MAGIC) ||
        (record->version != WEATHER_CFG_VERSION) ||
        (record->record_size != sizeof(*record)) ||
        (record->commit != WEATHER_CFG_COMMIT) ||
        (NULL == memchr(record->ssid, '\0', sizeof(record->ssid))) ||
        (NULL == memchr(record->password, '\0', sizeof(record->password))) ||
        (NULL == memchr(record->city, '\0', sizeof(record->city))) ||
        (record->ssid[0] == '\0') ||
        (strlen(record->password) < 8U) || (strlen(record->password) > 63U))
        return false;
    return record->crc32 == weather_cfg_crc32((const uint8_t *)record,
                                               offsetof(weather_cfg_record_t, crc32));
}

static bool weather_cfg_read_slot(uint32_t slot, weather_cfg_record_t *record)
{
    const uintptr_t address = (uintptr_t)WEATHER_CFG_FLASH_BASE +
                              ((uintptr_t)slot * WEATHER_CFG_SLOT_SIZE);
    memcpy(record, (const void *)address, sizeof(*record));
    return weather_cfg_record_valid(record);
}

static bool weather_cfg_load(void)
{
    weather_cfg_record_t a;
    weather_cfg_record_t b;
    bool valid_a;
    bool valid_b;
    const weather_cfg_record_t *selected;
    fsp_err_t err;

    if ((NULL == g_flash0.p_api) ||
        (g_flash0.p_api->open(g_flash0.p_ctrl, g_flash0.p_cfg) != FSP_SUCCESS))
        return false;
    valid_a = weather_cfg_read_slot(0U, &a);
    valid_b = weather_cfg_read_slot(1U, &b);
    err = g_flash0.p_api->close(g_flash0.p_ctrl);
    if (FSP_SUCCESS != err) return false;
    if (!valid_a && !valid_b) return false;

    /* Signed subtraction handles sequence wrap as long as slots are not more
     * than 2^31 commits apart (which is far beyond the flash endurance). */
    selected = (!valid_a) ? &b : ((!valid_b) ? &a :
                (((int32_t)(b.sequence - a.sequence) > 0) ? &b : &a));
    snprintf(s.ssid, sizeof(s.ssid), "%s", selected->ssid);
    snprintf(s.provision_password, sizeof(s.provision_password), "%s", selected->password);
    snprintf(s.city, sizeof(s.city), "%s", selected->city);
    s.provision_city_valid = (selected->city[0] != '\0');
    printf("[cfg] loaded RA Data Flash settings (seq=%lu, city='%s')\r\n",
           (unsigned long)selected->sequence, selected->city);
    return true;
}

static void weather_cfg_restore_defaults(void)
{
    if (!weather_cfg_load())
    {
        snprintf(s.ssid, sizeof(s.ssid), "%s", WEATHER_DEFAULT_SSID);
        snprintf(s.provision_password, sizeof(s.provision_password), "%s", WEATHER_DEFAULT_PWD);
        snprintf(s.city, sizeof(s.city), "%s", WEATHER_CITY);
        s.provision_city_valid = false;
        printf("[cfg] restored compiled defaults (no committed record)\r\n");
    }
}

static bool weather_cfg_save(const provisioning_config_t *config)
{
    weather_cfg_record_t a;
    weather_cfg_record_t b;
    weather_cfg_record_t record;
    weather_cfg_record_t verify;
    bool valid_a;
    bool valid_b;
    uint32_t target_slot;
    uint32_t next_sequence;
    uint32_t target_address;
    fsp_err_t err;

    if ((NULL == config) || (NULL == g_flash0.p_api) ||
        (config->ssid[0] == '\0') || (strlen(config->ssid) >= sizeof(record.ssid)) ||
        (strlen(config->password) < 8U) || (strlen(config->password) > 63U) ||
        (strlen(config->city) >= sizeof(record.city)))
        return false;
    if (g_flash0.p_api->open(g_flash0.p_ctrl, g_flash0.p_cfg) != FSP_SUCCESS)
        return false;

    valid_a = weather_cfg_read_slot(0U, &a);
    valid_b = weather_cfg_read_slot(1U, &b);
    if (!valid_a && !valid_b)
    {
        target_slot = 0U;
        next_sequence = 1U;
    }
    else if (!valid_a)
    {
        target_slot = 0U;
        next_sequence = b.sequence + 1U;
    }
    else if (!valid_b)
    {
        target_slot = 1U;
        next_sequence = a.sequence + 1U;
    }
    else if ((int32_t)(b.sequence - a.sequence) > 0)
    {
        target_slot = 0U;
        next_sequence = b.sequence + 1U;
    }
    else
    {
        target_slot = 1U;
        next_sequence = a.sequence + 1U;
    }

    memset(&record, 0xFF, sizeof(record));
    record.magic = WEATHER_CFG_MAGIC;
    record.version = WEATHER_CFG_VERSION;
    record.record_size = (uint16_t)sizeof(record);
    record.sequence = next_sequence;
    memset(record.ssid, 0, sizeof(record.ssid));
    memset(record.password, 0, sizeof(record.password));
    memset(record.city, 0, sizeof(record.city));
    snprintf(record.ssid, sizeof(record.ssid), "%s", config->ssid);
    snprintf(record.password, sizeof(record.password), "%s", config->password);
    snprintf(record.city, sizeof(record.city), "%s", config->city);
    record.crc32 = weather_cfg_crc32((const uint8_t *)&record,
                                     offsetof(weather_cfg_record_t, crc32));
    record.commit = WEATHER_CFG_COMMIT;
    memset(record.reserved, 0xFF, sizeof(record.reserved));

    target_address = WEATHER_CFG_FLASH_BASE + (target_slot * WEATHER_CFG_SLOT_SIZE);
    err = g_flash0.p_api->erase(g_flash0.p_ctrl, target_address,
                                WEATHER_CFG_SLOT_SIZE / BSP_FEATURE_FLASH_HP_DF_BLOCK_SIZE);
    if (FSP_SUCCESS == err)
    {
        err = g_flash0.p_api->write(g_flash0.p_ctrl, (uint32_t)(uintptr_t)&record,
                                    target_address, sizeof(record));
    }
    if (FSP_SUCCESS == err)
    {
        memcpy(&verify, (const void *)(uintptr_t)target_address, sizeof(verify));
        if (!weather_cfg_record_valid(&verify) ||
            (0 != memcmp(&record, &verify, sizeof(record))))
            err = FSP_ERR_WRITE_FAILED;
    }
    {
        const fsp_err_t close_err = g_flash0.p_api->close(g_flash0.p_ctrl);
        if ((FSP_SUCCESS == err) && (FSP_SUCCESS != close_err)) err = close_err;
    }
    if (FSP_SUCCESS != err)
    {
        printf("[cfg] Data Flash save failed: %d\r\n", (int)err);
        return false;
    }

    printf("[cfg] Data Flash slot %lu committed (sequence=%lu)\r\n",
           (unsigned long)target_slot, (unsigned long)next_sequence);
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
                 "http://api.seniverse.com/v3/weather/daily.json?key=%s&location=%s&language=%s&unit=c&start=0&days=3",
                 WEATHER_API_KEY,
                  (s.city[0] != '\0') ? s.city : (('\0' != WEATHER_CITY[0]) ? WEATHER_CITY : "ip"),
                 WEATHER_LANG);
    } else {
        snprintf(buf, size,
                 "http://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=%s",
                 WEATHER_API_KEY,
                  (s.city[0] != '\0') ? s.city : (('\0' != WEATHER_CITY[0]) ? WEATHER_CITY : "ip"),
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
        s.daily_retry_left = WEATHER_DAILY_RETRY;
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

    /* now.json 整体覆盖 s.wx 时会把上一轮的 daily(高低温)与短报文里缺失的
     * 湿度/风速一并清掉 —— 表现为"今日温度/风速有时不显示, 要刷新一次"。
     * 覆盖前把上一轮的可选字段合并回来(数据略旧好过整行空白)。 */
    if (s.wx_valid)
    {
        if (s.wx.daily_valid)
        {
            parsed.daily_valid = 1U;
            parsed.low  = s.wx.low;
            parsed.high = s.wx.high;
        }
        if ((parsed.humidity < 0) && (s.wx.humidity >= 0))
        {
            parsed.humidity = s.wx.humidity;
        }
        if ((parsed.wind_speed < 0) && (s.wx.wind_speed >= 0))
        {
            parsed.wind_speed = s.wx.wind_speed;
        }
        if ((parsed.location[0] == '\0') && (s.wx.location[0] != '\0'))
        {
            strncpy(parsed.location, s.wx.location, sizeof(parsed.location) - 1);
            parsed.location[sizeof(parsed.location) - 1] = '\0';
        }
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

    switch (s.cmd)
    {
        case CMD_AT:
            /* 冷启动 AT 自检可能被截断；不以自检结果判定模块死活。
             * 后续启动模式收敛会尝试 STA 配置，并等待模块重启 URC。 */
            printf("[wx] AT self-test: %s (cold-start may truncate reply)\r\n",
                   (AT_RES_OK == r) ? "OK" :
                   ((AT_RES_TIMEOUT == r) ? "TIMEOUT (normal on cold start)" : "ERROR"));
            printf("[wx] AT self-test complete; boot STA mode enforcement follows\r\n");
            break;

        case CMD_BOOT_STA:
            if (AT_RES_OK != r)
            {
                if ((s.boot_sta_command_step == BOOT_STA_STEP_WFTAP) &&
                    (r == AT_RES_ERR))
                {
                    /* WFTAP may be rejected when the SoftAP interface is
                     * already down. Still explicitly select STA and restart. */
                    printf("[wx] WFTAP rejected (err=%d); continue with WFMODE=0\r\n",
                           at_engine_resp()->err_code);
                    s.boot_sta_step = BOOT_STA_STEP_WFMODE;
                    break;
                }
                printf("[wx] boot STA command step %u failed r=%d err=%d\r\n",
                       (unsigned)s.boot_sta_command_step, (int)r,
                       at_engine_resp()->err_code);
                if (s.boot_sta_restart_retries < 2U)
                {
                    s.boot_sta_restart_retries++;
                    s.boot_sta_step = BOOT_STA_STEP_WFTAP;
                    printf("[wx] retry STA mode enforcement (%u/2)\r\n",
                           (unsigned)s.boot_sta_restart_retries);
                }
                else
                {
                    s.boot_sta_step = BOOT_STA_STEP_READY;
                    s.status = WIFI_STAT_FAIL;
                    s.fails++;
                    s.ui_dirty = true;
                }
                break;
            }
            if (s.boot_sta_command_step == BOOT_STA_STEP_WFTAP)
            {
                s.boot_sta_step = BOOT_STA_STEP_WFMODE;
            }
            else if (s.boot_sta_command_step == BOOT_STA_STEP_WFMODE)
            {
                s.boot_sta_step = BOOT_STA_STEP_RESTART;
            }
            else if (s.boot_sta_command_step == BOOT_STA_STEP_RESTART)
            {
                s.boot_sta_step = BOOT_STA_STEP_WAIT_INIT;
                s.boot_sta_deadline = at_engine_now_ms() + BOOT_STA_INIT_TIMEOUT_MS;
                printf("[wx] module restart requested in STA mode; waiting +INIT:DONE,0\r\n");
            }
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
            /* Connection-attempt WFSTA URCs are consumed by the connecting
             * branch. In CONNECTED state this command is a periodic health poll. */
            if (AT_RES_OK != r)
            {
                printf("[wx] WFSTA query failed (err=%d)\r\n",
                       at_engine_resp()->err_code);
            }
            if (s.status == WIFI_STAT_CONNECTED)
            {
                char state[AT_URC_LINE];
                if (at_engine_urc_get("+WFSTA:0", state, sizeof(state)))
                {
                    (void)at_engine_urc_take("+WFSTA:0");
                    wifi_link_lost("WFSTA:0");
                }
                else if (at_engine_urc_get("+WFSTA:1", state, sizeof(state)))
                {
                    (void)at_engine_urc_take("+WFSTA:1");
                    s.wifi_health_poll_ms = at_engine_now_ms();
                }
                else
                {
                    /* No response/URC: retry after one health interval. */
                    s.wifi_health_poll_ms = at_engine_now_ms();
                }
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
            if ((AT_RES_OK != r) && s.provision_active &&
                (s.provision_step == PROV_STEP_STA_JOIN))
            {
                s.provision_connect_accepted = false;
                s.provision_sta_connected = false;
                s.wifi_try++;
                if (s.wifi_try >= WIFI_WFJAPA_MAX_TRY)
                {
                    s.provision_active = false;
                    s.provision_step = PROV_STEP_IDLE;
                    weather_cfg_restore_defaults();
                    s.status = WIFI_STAT_FAIL;
                    s.fails++;
                    s.ui_dirty = true;
                }
            }
            else if ((AT_RES_OK != r) && (WIFI_STAT_CONNECTED != s.status))
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
            if (s.provision_active && (s.provision_step == PROV_STEP_STA_JOIN) &&
                (AT_RES_OK == r))
            {
                s.provision_connect_accepted = true;
                s.provision_sta_connected = false;
                s.provision_deadline = at_engine_now_ms() + 30000U;
            }
            break;

        case CMD_PROV_AT:
            (void)provision_cmd_complete(r);
            break;
        case CMD_PROV_RAW:
            /* UART transmit completion is polled separately. */
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

    if (s.provision_active)
    {
        ui_main_view_t mv;

        memset(&mv, 0, sizeof(mv));
        snprintf(mv.ssid, sizeof(mv.ssid), "%s", s.ssid);
        if (s.provision_step >= PROV_STEP_STA_TEARDOWN)
            mv.provisioning = 2U;
        else if ((s.provision_step >= PROV_STEP_SERVING) &&
                 (s.provision_step < PROV_STEP_STA_TEARDOWN))
            mv.provisioning = 1U;
        ui_main_page(&mv, WIFI_STAT_PROVISIONING);
        return;
    }

    if (!s.boot_sta_ready && (WIFI_STAT_FAIL != s.status))
    {
        ui_main_view_t mv;

        memset(&mv, 0, sizeof(mv));
        snprintf(mv.ssid, sizeof(mv.ssid), "%s", s.ssid);
        mv.provisioning = 3U;
        ui_main_page(&mv, WIFI_STAT_PROVISIONING);
        return;
    }

    if (2U == s.page)
    {
        /* 预报页: 今天 + 未来两天 (免费版 daily.json 上限 3 天)。 */
        ui_forecast_view_t fv;
        uint8_t fc, fi;

        memset(&fv, 0, sizeof(fv));
        if (s.wx_valid)
        {
            fc = (s.wx.f_count > 3U) ? 3U : s.wx.f_count;
            fv.count = fc;
            for (fi = 0U; fi < 3U; fi++)
            {
                if (fi < fv.count)
                {
                    snprintf(fv.row_str[fi], sizeof(fv.row_str[0]),
                             "%d/%d", s.wx.f_low[fi], s.wx.f_high[fi]);
                }
                else
                {
                    snprintf(fv.row_str[fi], sizeof(fv.row_str[0]), "--");
                }
            }
        }
        ui_forecast_page(&fv);
        return;
    }

    if (1U == s.page)
    {
        ui_wifi_view_t wv;

        memset(&wv, 0, sizeof(wv));
        wv.ssid  = (s.ssid[0] != '\0') ? s.ssid : WEATHER_DEFAULT_SSID;
        wv.ip    = s.ip;
        wv.fails = s.fails;
        wv.page  = s.page;
        ui_wifi_page(&wv, s.status);
        return;
    }

    /* s.page == 0: 主页 (天气 + 城市) */
    {
        ui_main_view_t mv;

        memset(&mv, 0, sizeof(mv));
        time_str_get(time5, sizeof(time5));
        snprintf(mv.ssid, sizeof(mv.ssid), "%s", s.ssid);
        snprintf(mv.time_str, sizeof(mv.time_str), "%s", time5);
        /* Stage 1 uses Seniverse IP geolocation rather than a configured city. */
        snprintf(mv.city, sizeof(mv.city), "IP");
        if (s.city[0] != '\0')
            snprintf(mv.city, sizeof(mv.city), "%.15s", s.city);
        /* API 解析出的实际城市名优先: 即使走 IP 定位也显示真实城市 */
        if (s.wx_valid && (s.wx.location[0] != '\0'))
            snprintf(mv.city, sizeof(mv.city), "%.32s", s.wx.location);
        /* GD32-latest UI contract: weather_code=-1 means "no data" (memset
         * would leave 0 = "sunny", so initialize explicitly). */
        mv.weather_code = -1;
        /* Derive activity for the GD32-latest UI pages. Unlike the GD32
         * state machine (which jumps to FETCHING on every re-fetch), while
         * valid data is already displayed we keep SUCCESS so the periodic
         * refresh does not blank the screen into the "fetching" page. */
        if (s.wx_valid)
            mv.activity = WEATHER_ACT_SUCCESS;
        else if (weather_http_busy())
            mv.activity = WEATHER_ACT_FETCHING;
        else if (s.status == WIFI_STAT_CONNECTED)
            mv.activity = WEATHER_ACT_FAILED;   /* conclusion lives in [WX-RESULT] log */
        else if (s.connect_t0 != 0U)
            mv.activity = WEATHER_ACT_WIFI_CONNECTING;
        else
            mv.activity = WEATHER_ACT_IDLE;

        if (s.wx_valid)
        {
            snprintf(mv.temp_str, sizeof(mv.temp_str), "%d", s.wx.temperature);
            /* The degree glyph is drawn by the UI after temp_str, so the
             * string itself must not carry a trailing "C". */
            if (s.wx.feels_like >= 0)
            {
                snprintf(mv.feels_like_str, sizeof(mv.feels_like_str), "%d",
                         s.wx.feels_like);
            }
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
                /* GD32 format: "low/high"; the UI appends the degree glyph. */
                snprintf(mv.range_str, sizeof(mv.range_str), "%d/%d",
                         s.wx.low, s.wx.high);
            }
            mv.weather_code = s.wx.code;
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
}

/* ===================== 对外接口 ===================== */

void weather_task_init (void)
{
    memset(&s, 0, sizeof(s));
    s.status     = WIFI_STAT_CONNECTING;
    s.connect_t0 = at_engine_now_ms();
    s.wifi_step  = WIFI_STEP_NWIP_QUERY;
    s.boot_sta_ready = false;
    s.boot_sta_step = BOOT_STA_STEP_WFTAP;
    s.boot_sta_restart_retries = 0U;
    s.wifi_try   = 0U;
    s.page       = 0U;
    s.ui_dirty   = true;
    s.cmd        = CMD_NONE;
    s.hb_t0      = at_engine_now_ms();
    s.hb_fast    = s.hb_t0;
    snprintf(s.ssid, sizeof(s.ssid), "%s", WEATHER_DEFAULT_SSID);
    snprintf(s.provision_password, sizeof(s.provision_password), "%s", WEATHER_DEFAULT_PWD);
    snprintf(s.city, sizeof(s.city), "%s", WEATHER_CITY);
    if (!weather_cfg_load())
        printf("[cfg] no valid Data Flash record; using compiled defaults (read-only)\r\n");
    s.provision_city_valid = false;

    key_init();
    ui_init();
    printf("[wx] task init: boot %s %s, first action = force module STA mode\r\n",
           __DATE__, __TIME__);

    /* 用户要求: 每次开机先用 AT 命令测试模块, 再走后续联网/天气。
     *
     * ⚠ 超时只给 800ms 而不是 2000ms（2026-10-04 调整）：
     *   实测冷启动这条 AT 可能被截断（只回 1 个字符就 TIMEOUT），它只是
     *   "模块活着没有"的形式确认。而引擎时钟是按主循环 tick 累加的，
     *   OLED 重绘一轮要 ~100ms，所以 2000ms 的引擎时间在真实时间上远不止
     *   2 秒，整轮开机白等 —— 之前"抓日志等不到结果"就有它一份。
     *   自检失败不致命；启动 STA 流程仍会尝试发送模式切换命令。
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

bool weather_task_provisioning_active(void)
{
    return s.provision_active;
}

void weather_task_tcp_urc(const char *line, uint16_t length)
{
    char payload[PROVISIONING_HTTP_RX_MAX];
    size_t payload_length = 0U;

    if (!s.provision_active || (NULL == line) || (0U == length)) return;
    if (!provisioning_portal_trdts_decode(line, length, s.provision_peer_ip,
                                          sizeof(s.provision_peer_ip),
                                          &s.provision_peer_port, payload,
                                          sizeof(payload), &payload_length))
    {
        printf("[prov] rejected malformed +TRDTS record (%u bytes)\r\n", (unsigned)length);
        return;
    }
    if (s.provision_rx_len == 0U)
        printf("[prov] +TRDTS peer=%s:%u decoded=%u first='%c'\r\n",
               s.provision_peer_ip, (unsigned)s.provision_peer_port,
               (unsigned)payload_length,
               (payload_length > 0U && (uint8_t)payload[0] >= 0x20U &&
                (uint8_t)payload[0] <= 0x7EU) ? payload[0] : '.');
    if ((payload_length > PROVISIONING_REQUEST_MAX) ||
        ((size_t)s.provision_rx_len + payload_length > PROVISIONING_REQUEST_MAX))
    {
        s.provision_rx_len = 0U;
        printf("[prov] HTTP request buffer limit exceeded\r\n");
        return;
    }
    memcpy(s.provision_rx + s.provision_rx_len, payload, payload_length);
    s.provision_rx_len = (uint16_t)(s.provision_rx_len + payload_length);
    s.provision_rx[s.provision_rx_len] = '\0';
    s.ui_dirty = true;
}

wifi_status_t weather_task_wifi_status (void)
{
    return s.status;
}

bool weather_task_weather_valid (void)
{
    return s.wx_valid;
}

static void boot_sta_poll(uint32_t now)
{
    if (s.boot_sta_ready || s.provision_active || (CMD_NONE != s.cmd) || !at_engine_idle())
        return;

    if (s.boot_sta_step == BOOT_STA_STEP_WAIT_INIT)
    {
        if (at_engine_urc_take("+INIT:DONE,0"))
        {
            s.boot_sta_step = BOOT_STA_STEP_READY;
            s.boot_sta_ready = true;
            s.status = WIFI_STAT_CONNECTING;
            s.wifi_step = WIFI_STEP_NWIP_QUERY;
            s.wifi_try = 0U;
            s.connect_t0 = now;
            printf("[wx] module booted in STA mode; start normal connection ladder\r\n");
        }
        else if (at_engine_urc_take("+INIT:DONE,1"))
        {
            if (s.boot_sta_restart_retries < 2U)
            {
                s.boot_sta_restart_retries++;
                printf("[wx] module still reports SoftAP mode after STA restart; retrying (%u/2)\r\n",
                       (unsigned)s.boot_sta_restart_retries);
                s.boot_sta_step = BOOT_STA_STEP_WFTAP;
            }
            else
            {
                printf("[wx] module remains in SoftAP mode; stop automatic STA attempts\r\n");
                s.boot_sta_step = BOOT_STA_STEP_READY;
                s.status = WIFI_STAT_FAIL;
                s.fails++;
                s.ui_dirty = true;
            }
        }
        else if ((int32_t)(now - s.boot_sta_deadline) >= 0)
        {
            if (s.boot_sta_restart_retries < 2U)
            {
                s.boot_sta_restart_retries++;
                printf("[wx] STA boot URC timeout; retry mode enforcement (%u/2)\r\n",
                       (unsigned)s.boot_sta_restart_retries);
                s.boot_sta_step = BOOT_STA_STEP_WFTAP;
            }
            else
            {
                printf("[wx] cannot confirm STA boot; keep network ladder stopped\r\n");
                s.boot_sta_step = BOOT_STA_STEP_READY;
                s.status = WIFI_STAT_FAIL;
                s.fails++;
                s.ui_dirty = true;
            }
        }
        return;
    }

    switch (s.boot_sta_step)
    {
        case BOOT_STA_STEP_WFTAP:
            s.boot_sta_command_step = BOOT_STA_STEP_WFTAP;
            if (at_send_owned("AT+WFTAP", 3000U, CMD_BOOT_STA))
                s.boot_sta_step = BOOT_STA_STEP_WFMODE;
            break;
        case BOOT_STA_STEP_WFMODE:
            s.boot_sta_command_step = BOOT_STA_STEP_WFMODE;
            if (at_send_owned("AT+WFMODE=0", 3000U, CMD_BOOT_STA))
                s.boot_sta_step = BOOT_STA_STEP_RESTART;
            break;
        case BOOT_STA_STEP_RESTART:
            s.boot_sta_command_step = BOOT_STA_STEP_RESTART;
            (void)at_send_owned("AT+RESTART", 5000U, CMD_BOOT_STA);
            break;
        default:
            break;
    }
}

static void wifi_link_lost(const char *reason)
{
    if (s.status != WIFI_STAT_CONNECTED) return;
    printf("[wx] Wi-Fi link lost (%s); restart connection ladder\r\n", reason);
    s.status = WIFI_STAT_CONNECTING;
    s.ip[0] = '\0';
    s.wifi_sta_ok = false;
    s.wifi_step = WIFI_STEP_NWIP_QUERY;
    s.wifi_try = 0U;
    s.ladder_sig = 0xFFFFU;
    s.connect_t0 = at_engine_now_ms();
    s.timezone_set = false;
    s.sntp_done = false;
    s.time_check_pending = false;
    s.ui_dirty = true;
}

static void wifi_health_poll(uint32_t now)
{
    if ((s.status != WIFI_STAT_CONNECTED) || s.provision_active ||
        (CMD_NONE != s.cmd) || !at_engine_idle() ||
        weather_http_busy() ||
        ((now - s.wifi_health_poll_ms) < WIFI_HEALTH_POLL_MS))
        return;

    /* Advance the deadline before submitting so a busy/rejected attempt cannot
     * spin every main-loop tick. Command completion updates/retries the timer. */
    s.wifi_health_poll_ms = now;
    (void)at_send_owned("AT+WFSTA", 2500U, CMD_WFSTA);
}

static void provision_http_service(void)
{
    char path[80];
    size_t body_offset = 0U;
    size_t content_length = 0U;
    size_t total_length = 0U;
    size_t reply_length;
    provisioning_http_status_t parse_state;
    if (s.provision_rx_len == 0U) return;
    parse_state = provisioning_portal_request_state(s.provision_rx, s.provision_rx_len,
                                                     path, sizeof(path), &body_offset,
                                                     &content_length, &total_length);
    if (PROVISIONING_HTTP_INCOMPLETE == parse_state) return;
    printf("[prov] HTTP request state=%u method=%s path=%s rx=%u body=%u\r\n",
           (unsigned)parse_state,
           (0 == memcmp(s.provision_rx, "POST ", 5U)) ? "POST" :
           ((0 == memcmp(s.provision_rx, "GET ", 4U)) ? "GET" : "OTHER"),
           path, (unsigned)s.provision_rx_len, (unsigned)content_length);
    if (PROVISIONING_HTTP_TOO_LARGE == parse_state)
    {
        static const char too_large[] = "HTTP/1.0 413 Payload Too Large\r\nContent-Length:0\r\nConnection: close\r\n\r\n";
        s.provision_rx_len = 0U;
        (void)provision_send_tcp(too_large, sizeof(too_large) - 1U, PROV_STEP_SERVING);
        return;
    }
    if (PROVISIONING_HTTP_INVALID == parse_state)
    {
        /* Malformed or not-HTTP payload. Drop without changing portal lifetime. */
        s.provision_rx_len = 0U;
        return;
    }

    if (0 == memcmp(s.provision_rx, "GET ", 4U))
    {
        reply_length = provisioning_portal_get_response(path, s.provision_tx, sizeof(s.provision_tx));
        s.provision_rx_len = 0U;
        if (reply_length > 0U)
        {
            if (!provision_send_tcp(s.provision_tx, reply_length, PROV_STEP_SERVING))
                printf("[prov] GET response TX queue failed (%u bytes)\r\n", (unsigned)reply_length);
        }
        return;
    }

    if ((0 != strcmp(path, "/cfg")) || (0 != memcmp(s.provision_rx, "POST ", 5U)))
    {
        static const char not_found[] = "HTTP/1.0 404 Not Found\r\nContent-Length:0\r\nConnection: close\r\n\r\n";
        s.provision_rx_len = 0U;
        (void)provision_send_tcp(not_found, sizeof(not_found) - 1U, PROV_STEP_SERVING);
        return;
    }

    {
        provisioning_config_t candidate;
        bool parsed;
        if ((body_offset + content_length > s.provision_rx_len) ||
            (content_length == 0U) || (content_length > PROVISIONING_REQUEST_MAX)) return;
        memcpy(s.provision_tx, s.provision_rx + body_offset, content_length);
        parsed = provisioning_portal_parse(s.provision_tx, content_length, &candidate);
        if (parsed && (candidate.ssid[0] == '\0'))
        {
            /* This portal requires explicit credentials; match the required
             * browser fields instead of advertising unsupported reuse. */
            parsed = false;
        }
        if (!parsed)
        {
            reply_length = provisioning_portal_response(false, s.provision_tx, sizeof(s.provision_tx));
            s.provision_rx_len = 0U;
            if (reply_length > 0U)
                (void)provision_send_tcp(s.provision_tx, reply_length, PROV_STEP_SERVING);
            return;
        }
        /* Keep submitted values staged until STA succeeds and flash is updated. */
        snprintf(s.ssid, sizeof(s.ssid), "%s", candidate.ssid);
        snprintf(s.provision_password, sizeof(s.provision_password), "%s", candidate.password);
        snprintf(s.provision_city, sizeof(s.provision_city), "%s", candidate.city);
        s.provision_connect_accepted = false;
        /* This is intentionally not a success response: STA association and
         * the Data Flash commit happen after the SoftAP TCP session closes. */
        reply_length = provisioning_portal_pending_response(s.provision_tx,
                                                             sizeof(s.provision_tx));
        s.provision_rx_len = 0U;
        if (reply_length > 0U)
        {
            if (provision_send_tcp(s.provision_tx, reply_length, PROV_STEP_TCP_CLOSE))
                s.provision_config_pending = true;
        }
    }
}

static bool provision_cmd_complete(at_result_t result)
{
    const uint32_t now = at_engine_now_ms();
    const uint8_t command_step = s.provision_command_step;
    if (result != AT_RES_OK)
    {
        /* WFQAP is only meaningful while associated with an AP. Provisioning
         * must also work when STA is already disconnected, so an explicit
         * module ERROR here is non-fatal: continue with the mode switch.
         * A timeout/abort is still fatal because it indicates a UART/engine
         * problem rather than a rejected optional disconnect operation. */
        if ((command_step == PROV_STEP_AP_DISCONNECT) && (result == AT_RES_ERR))
        {
            printf("[prov] WFQAP rejected (err=%d); continue to SoftAP mode switch\r\n",
                   at_engine_resp()->err_code);
            s.provision_step = PROV_STEP_AP_MODE;
            return true;
        }
        if ((command_step == PROV_STEP_TCP_CLOSE) && (result == AT_RES_ERR))
        {
            printf("[prov] TCP session already closed (err=%d); continue teardown\r\n",
                   at_engine_resp()->err_code);
            s.provision_step = PROV_STEP_STA_TEARDOWN;
            s.provision_deadline = now + PROV_TCP_REPLY_DELAY_MS;
            return true;
        }
        printf("[prov] stage=%u AT failed result=%d err=%d\r\n", (unsigned)command_step,
               (int)result, at_engine_resp()->err_code);
        s.provision_step = PROV_STEP_FAILED;
        s.provision_active = false;
        s.status = WIFI_STAT_FAIL;
        s.ui_dirty = true;
        return false;
    }

    switch (command_step)
    {
        case PROV_STEP_AP_DISCONNECT:
            s.provision_step = PROV_STEP_AP_MODE;
            break;
        case PROV_STEP_AP_MODE:
            s.provision_step = PROV_STEP_AP_PROFILE;
            break;
        case PROV_STEP_AP_PROFILE:
            s.provision_step = PROV_STEP_AP_REBOOT;
            break;
        case PROV_STEP_AP_REBOOT:
            s.provision_step = PROV_STEP_AP_BOOT;
            s.provision_deadline = now + PROV_AP_BOOT_DELAY_MS;
            break;
        case PROV_STEP_AP_IP:
            s.provision_step = PROV_STEP_DHCP_STOP;
            break;
        case PROV_STEP_DHCP_STOP:
            s.provision_step = PROV_STEP_DHCP_RANGE;
            break;
        case PROV_STEP_DHCP_RANGE:
            s.provision_step = PROV_STEP_DHCP_START;
            break;
        case PROV_STEP_DHCP_START:
            s.provision_step = PROV_STEP_TCP_MODE;
            break;
        case PROV_STEP_TCP_MODE:
            s.provision_step = PROV_STEP_TCP_LISTEN;
            break;
        case PROV_STEP_TCP_LISTEN:
            s.provision_step = PROV_STEP_SERVING;
            s.status = WIFI_STAT_PROVISIONING;
            s.ui_dirty = true;
            printf("[prov] TCP listener acknowledged; open http://%s:%u/\r\n",
                   PROVISIONING_PORTAL_IP, (unsigned)PROVISIONING_PORTAL_PORT);
            break;
        case PROV_STEP_TCP_CLOSE:
            s.provision_step = PROV_STEP_STA_TEARDOWN;
            s.provision_deadline = now + PROV_TCP_REPLY_DELAY_MS;
            break;
        case PROV_STEP_STA_TEARDOWN:
            s.provision_step = PROV_STEP_STA_MODE;
            s.provision_deadline = now + PROV_TCP_REPLY_DELAY_MS;
            if (s.provision_city[0] != '\0')
            {
                snprintf(s.city, sizeof(s.city), "%s", s.provision_city);
                s.provision_city_valid = true; /* runtime only until FSP flash is generated */
            }
            break;
        case PROV_STEP_STA_MODE:
            s.provision_step = PROV_STEP_STA_REBOOT;
            s.provision_deadline = now + 300U;
            break;
        case PROV_STEP_STA_REBOOT:
            s.provision_step = PROV_STEP_STA_BOOT;
            s.provision_deadline = now + PROV_STA_BOOT_DELAY_MS;
            break;
        default:
            break;
    }
    return true;
}

static void provision_start(void)
{
    if ((!s.boot_sta_ready && (s.status != WIFI_STAT_FAIL)) ||
        !s.provision_requested || s.provision_active || !at_engine_idle() ||
        weather_http_busy()) return;
    s.provision_requested = false;
    s.provision_active = true;
    s.provision_city_valid = false;
    s.provision_step = PROV_STEP_AP_DISCONNECT;
    s.provision_config_pending = false;
    s.provision_connect_accepted = false;
    s.provision_sta_connected = false;
    s.provision_rx_len = 0U;
    s.provision_deadline = 0U;
    s.refresh_pending = false;
    s.status = WIFI_STAT_PROVISIONING;
    s.ui_dirty = true;
    key_event_flush();
    if (!provision_send_at("AT+WFQAP", 3000U, PROV_STEP_AP_MODE))
    {
        s.provision_active = false;
        s.status = WIFI_STAT_FAIL;
    }
    printf("[prov] entering SoftAP setup\r\n");
}

static void provision_poll(uint32_t now)
{
    char tcp_event[AT_URC_LINE];

    if (s.provision_requested && !s.provision_active) provision_start();
    if (!s.provision_active) return;

    /* UM-WI-003 p.114-115: +TRCTS/+TRXTS report TCP-server peer connect and
     * disconnect. Each +TRDTS record supplies the endpoint used for reply. */
    if (at_engine_urc_get("+TRCTS:", tcp_event, sizeof(tcp_event)))
    {
        s.provision_rx_len = 0U;
        s.provision_peer_port = 0U;
        printf("[prov] TCP client connected: %s\r\n", tcp_event + 7U);
        s.ui_dirty = true;
    }
    if (at_engine_urc_get("+TRXTS:", tcp_event, sizeof(tcp_event)))
    {
        printf("[prov] TCP client disconnected: %s\r\n", tcp_event + 7U);
        s.provision_rx_len = 0U;
        s.provision_peer_port = 0U;
        s.ui_dirty = true;
    }

    if ((s.cmd == CMD_PROV_RAW) && !wifi_uart_write_busy() &&
        (wifi_uart_write_done_count() != s.provision_tx_done_mark))
    {
        s.cmd = CMD_NONE;
        if (s.provision_step == PROV_STEP_TX_PENDING)
        {
            s.provision_step = s.provision_tx_next_step;
            if (s.provision_config_pending && (s.provision_step == PROV_STEP_TCP_CLOSE))
                s.provision_deadline = now + PROV_TCP_REPLY_DELAY_MS;
        }
    }

    if (s.provision_step == PROV_STEP_SERVING && (CMD_NONE == s.cmd))
        provision_http_service();
    if ((CMD_NONE != s.cmd) || !at_engine_idle()) return;

    if (s.provision_step == PROV_STEP_STA_JOIN)
    {
        char urc[AT_URC_LINE];

        if (!s.provision_connect_accepted)
        {
            char command[160];
            snprintf(command, sizeof(command), "AT+WFJAPA=%s,%s", s.ssid, s.provision_password);
            if (at_send_owned(command, 30000U, CMD_WFJAPA))
            {
                s.provision_connect_accepted = true;
                s.provision_deadline = now + 30000U;
            }
        }
        else if (at_engine_urc_get("+WFJAP:1", urc, sizeof(urc)))
        {
            /* Keep s.ssid in sync with what the module actually joined, so
             * the provisioning/connecting UI shows the true SSID. */
            (void)parse_ssid(urc, s.ssid, sizeof(s.ssid));
            s.provision_sta_connected = true;
        }

        if (s.provision_sta_connected)
        {
            provisioning_config_t accepted;
            snprintf(accepted.ssid, sizeof(accepted.ssid), "%s", s.ssid);
            snprintf(accepted.password, sizeof(accepted.password), "%s", s.provision_password);
            snprintf(accepted.city, sizeof(accepted.city), "%s", s.provision_city);
            if (!weather_cfg_save(&accepted))
            {
                /* The old committed slot remains valid. Restore it as the
                 * source for any later retry/reconnect. */
                weather_cfg_restore_defaults();
                s.provision_active = false;
                s.provision_step = PROV_STEP_FAILED;
                s.provision_connect_accepted = false;
                s.provision_sta_connected = false;
                s.status = WIFI_STAT_FAIL;
                s.fails++;
                s.ui_dirty = true;
                return;
            }
            snprintf(s.city, sizeof(s.city), "%s", accepted.city);
            s.provision_city_valid = (accepted.city[0] != '\0');
            s.provision_active = false;
            s.provision_step = PROV_STEP_IDLE;
            s.provision_connect_accepted = false;
            s.provision_sta_connected = false;
            s.status = WIFI_STAT_CONNECTING;
            s.wifi_step = WIFI_STEP_NWIP_QUERY;
            s.wifi_try = 0U;
            s.connect_t0 = now;
            s.ui_dirty = true;
        }
        else if (at_engine_urc_take("+WFJAP:0") ||
                 ((int32_t)(now - s.provision_deadline) >= 0))
        {
            s.provision_connect_accepted = false;
            s.wifi_try++;
            if (s.wifi_try >= WIFI_WFJAPA_MAX_TRY)
            {
                s.provision_active = false;
                s.provision_step = PROV_STEP_IDLE;
                s.provision_connect_accepted = false;
                s.provision_sta_connected = false;
                weather_cfg_restore_defaults();
                s.status = WIFI_STAT_FAIL;
                s.fails++;
                s.ui_dirty = true;
            }
            else
            {
                s.provision_step = PROV_STEP_STA_JOIN;
                s.provision_connect_accepted = false;
            }
        }
        return;
    }

    switch (s.provision_step)
    {
        case PROV_STEP_AP_MODE:
            (void)provision_send_at("AT+WFMODE=1", 3000U, PROV_STEP_AP_PROFILE);
            break;
        case PROV_STEP_AP_PROFILE:
            (void)provision_send_at("AT+WFSAP=leeWethWifi,3,1,12345678,1,CH", 3000U, PROV_STEP_AP_REBOOT);
            break;
        case PROV_STEP_AP_REBOOT:
            (void)provision_send_at("AT+RESTART", 5000U, PROV_STEP_AP_BOOT);
            break;
        case PROV_STEP_AP_BOOT:
            if (at_engine_urc_take("+INIT:DONE,1"))
            {
                s.provision_step = PROV_STEP_AP_BOOT_DONE;
                s.provision_deadline = now;
            }
            else if ((int32_t)(now - s.provision_deadline) >= 0)
            {
                printf("[prov] SoftAP boot URC missing; waiting for UART to recover\r\n");
                s.provision_deadline = now + 1500U;
            }
            break;
        case PROV_STEP_AP_BOOT_DONE:
            (void)provision_send_at("AT+NWIP=1,192.168.1.100,255.255.255.0,192.168.1.100", 4000U, PROV_STEP_AP_IP);
            break;
        case PROV_STEP_AP_IP:
            (void)provision_send_at("AT+NWDHS=0", 3000U, PROV_STEP_DHCP_STOP);
            break;
        case PROV_STEP_DHCP_STOP:
            (void)provision_send_at("AT+NWDHR=192.168.1.2,192.168.1.11", 3000U, PROV_STEP_DHCP_RANGE);
            break;
        case PROV_STEP_DHCP_RANGE:
            (void)provision_send_at("AT+NWDHS=1", 3000U, PROV_STEP_DHCP_START);
            break;
        case PROV_STEP_DHCP_START:
            (void)provision_send_at("AT+TCPDATAMODE=1", 3000U, PROV_STEP_TCP_MODE);
            break;
        case PROV_STEP_TCP_MODE:
            /* Renesas R01AN7287 SoftAP example starts its TCP listener after
             * SoftAP/IP/DHCP configuration. RA provides the HTTP layer. */
            (void)provision_send_at("AT+TRTS=8080", 5000U, PROV_STEP_TCP_LISTEN);
            break;
        case PROV_STEP_TCP_CLOSE:
            if ((int32_t)(now - s.provision_deadline) >= 0)
                (void)provision_send_at("AT+TRTRM=0", 3000U, PROV_STEP_STA_TEARDOWN);
            break;
        case PROV_STEP_TCP_LISTEN:
            s.provision_step = PROV_STEP_SERVING;
            s.status = WIFI_STAT_PROVISIONING;
            s.ui_dirty = true;
            printf("[prov] TCP listener acknowledged; open http://%s:%u/\r\n",
                   PROVISIONING_PORTAL_IP, (unsigned)PROVISIONING_PORTAL_PORT);
            break;
        case PROV_STEP_SERVING:
            /* UI was dirtied when listener startup completed. Do not redraw
             * every cooperative-loop iteration while waiting for a client. */
            break;
        case PROV_STEP_STA_TEARDOWN:
            if ((int32_t)(now - s.provision_deadline) >= 0)
                (void)provision_send_at("AT+WFTAP", 3000U, PROV_STEP_STA_MODE);
            break;
        case PROV_STEP_STA_MODE:
            if ((int32_t)(now - s.provision_deadline) >= 0)
            {
                (void)provision_send_at("AT+WFMODE=0", 3000U, PROV_STEP_STA_REBOOT);
            }
            break;
        case PROV_STEP_TX_PENDING:
            break;
        case PROV_STEP_FAILED:
            break;
        case PROV_STEP_STA_REBOOT:
            (void)provision_send_at("AT+RESTART", 5000U, PROV_STEP_STA_BOOT);
            break;
        case PROV_STEP_STA_BOOT:
            if (at_engine_urc_take("+INIT:DONE,0"))
            {
                s.provision_step = PROV_STEP_STA_JOIN;
                s.provision_connect_accepted = false;
                s.wifi_try = 0U;
                s.provision_deadline = now + PROV_STA_BOOT_DELAY_MS;
            }
            else if ((int32_t)(now - s.provision_deadline) >= 0)
            {
                printf("[prov] STA boot URC timeout\r\n");
                s.provision_active = false;
                s.provision_step = PROV_STEP_FAILED;
                s.status = WIFI_STAT_FAIL;
                s.fails++;
                s.ui_dirty = true;
            }
            break;
        case PROV_STEP_STA_JOIN:
            break;
        default:
            break;
    }
}

void weather_task_poll (void)
{
    uint32_t now = at_engine_now_ms();

    /* Always scan/drain keys before networking or AT state handling.
     * Mechanical SW1/P005: short=queue refresh, double=page, long=provisioning hook.
     * SW2/P006: click=page. Touch IDs use the same event mapping once CTSU is enabled. */
    key_poll(now);
    for (;;)
    {
        key_id_t    id = KEY_ID_MECH1;
        key_event_t e  = key_event_get_id(&id);

        if (KEY_EVT_NONE == e)
        {
            break;
        }

        /* SW2/P006 and touch2: click=page (no double-click delay). */
        if ((KEY_ID_MECH2 == id) || (KEY_ID_TOUCH2 == id))
        {
            if (KEY_EVT_LONG == e)
            {
                printf("[wx] key%u: long -> request provisioning\r\n", (unsigned)id);
                s.provision_requested = true;
                s.ui_dirty = true;
            }
            else
            {
                s.page = (uint8_t)((s.page + 1U) % 3U);  /* 0主/1WiFi/2预报 */
                printf("[wx] key%u: click -> page %u\r\n",
                       (unsigned)id, (unsigned)s.page);
                s.ui_dirty = true;
            }
            continue;
        }

        /* SW1/P005 and touch1: short/double/long state machine. */
        switch (e)
        {
            case KEY_EVT_SHORT:
                printf("[wx] key%u: short -> queue weather refresh\r\n", (unsigned)id);
                s.refresh_pending = true;
                if (WIFI_STAT_FAIL == s.status)
                {
                    /* A queued refresh must be able to recover a terminal
                     * connection failure instead of waiting forever. */
                    s.status = WIFI_STAT_CONNECTING;
                    s.wifi_step = WIFI_STEP_NWIP_QUERY;
                    s.wifi_try = 0U;
                    s.connect_t0 = now;
                    s.ladder_sig = 0xFFFFU;
                    s.ui_dirty = true;
                    printf("[wx] queued refresh restarts Wi-Fi connection ladder\r\n");
                }
                s.ui_dirty = true;
                break;

            case KEY_EVT_DOUBLE:
                s.page = (uint8_t)((s.page + 1U) % 3U);  /* 0主/1WiFi/2预报 */
                printf("[wx] key%u: double -> page %u\r\n",
                       (unsigned)id, (unsigned)s.page);
                s.ui_dirty = true;
                break;

            case KEY_EVT_LONG:
                printf("[wx] key%u: long -> request provisioning\r\n", (unsigned)id);
                s.provision_requested = true;
                s.ui_dirty = true;
                break;

            default:
                break;
        }
    }

    /* Consume unsolicited disconnect events even after the state is CONNECTED.
     * They update status once; the OLED only redraws because link-loss marks it dirty. */
    if (s.boot_sta_ready && !s.provision_active &&
        (s.status == WIFI_STAT_CONNECTED))
    {
        char link_event[AT_URC_LINE];
        if (at_engine_urc_get("+WFJAP:0", link_event, sizeof(link_event)))
        {
            (void)at_engine_urc_take("+WFJAP:0");
            wifi_link_lost("WFJAP:0 URC");
        }
        else if (at_engine_urc_get("+WFSTA:0", link_event, sizeof(link_event)))
        {
            (void)at_engine_urc_take("+WFSTA:0");
            wifi_link_lost("WFSTA:0 URC");
        }
    }

    if (!s.provision_active && s.provision_requested)
        provision_start();
    /* ---------- 2. 联网：URC 快速路径 + 超时阶梯 ---------- */
    if (!s.provision_active && s.boot_sta_ready &&
        ((WIFI_STAT_CONNECTING == s.status) || (WIFI_STAT_IDLE == s.status)))
    {
        char urc[AT_URC_LINE];

        /* 2a. 快速路径：+WFJAP:1 自带 IP，一步到位（模块刚上电时会自发这条） */
        if (at_engine_urc_get("+WFJAP:1", urc, sizeof(urc)))
        {
            char joined[33];

            printf("[wx] +WFJAP:1 urc='%s'\r\n", urc);
            /* 模块实际加入的 SSID 回填 s.ssid: 修"联网中/失败页 SSID 显示
             * 问号"——屏幕上要显示的就是真实加入的这一路。 */
            if (parse_ssid(urc, joined, sizeof(joined)) &&
                (0 != strcmp(joined, s.ssid)))
            {
                snprintf(s.ssid, sizeof(s.ssid), "%s", joined);
                s.ui_dirty = true;
                printf("[wx] joined SSID '%s'\r\n", joined);
            }
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
                              s.ssid, s.provision_password);
                     (void)at_send_owned(cmd, 30000U, CMD_WFJAPA);
                     s.wifi_step = WIFI_STEP_WFJAPA;
                     break;
            }
        }

    }

    /* ---------- 3. 引擎轮询（工程内唯一调用点, §4 单飞行） ---------- */
    {
        at_result_t r = at_engine_poll();

        if ((s.cmd == CMD_PROV_RAW) && !wifi_uart_write_busy() &&
            (wifi_uart_write_done_count() != s.provision_tx_done_mark))
        {
            s.cmd = CMD_NONE;
            if (s.provision_step == PROV_STEP_TX_PENDING)
            {
                s.provision_step = s.provision_tx_next_step;
                if (s.provision_config_pending &&
                    (s.provision_step == PROV_STEP_TCP_CLOSE))
                    s.provision_deadline = now + PROV_TCP_REPLY_DELAY_MS;
            }
        }

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
            else if ((CMD_NONE != s.cmd) && (CMD_PROV_RAW != s.cmd))
            {
                on_cmd_done(r);
            }
        }

        /* Advance provisioning only after dispatching the completion returned
         * by this poll. Otherwise the previous command's latched result can be
         * mistaken for the result of a newly submitted provisioning command. */
        provision_poll(now);
        boot_sta_poll(now);
    }

    wifi_health_poll(now);

    /* ---------- 4. 命令调度（仅在引擎空闲时, 优先级从高到低） ---------- */
    if (!s.provision_active && s.boot_sta_ready && at_engine_idle() && !weather_http_busy())
    {
        /* 4a. Explicit queued refresh (key/API) takes precedence over periodic fetch. */
        if (s.refresh_pending)
        {
            if (s.status == WIFI_STAT_CONNECTED && (CMD_NONE == s.cmd) &&
                fetch_weather_start())
            {
                s.refresh_pending = false;
                    printf("[wx] queued manual refresh issued\r\n");
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
        /* 4b2. TODAY/wind 行缺失 -> 限次快速补抓, 不等满 refresh 周期 */
        else if (s.wx_valid && (0U == s.wx.daily_valid) &&
                 (s.daily_retry_left > 0U) &&
                 ((int32_t)(now - s.daily_retry_due_ms) >= 0))
        {
            if (fetch_weather_start())
            {
                s.daily_retry_left--;
                s.daily_retry_due_ms = now + WEATHER_DAILY_RETRY_GAP_MS;
                printf("[wx] daily missing, retry (%u left)\r\n",
                       (unsigned)s.daily_retry_left);
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
