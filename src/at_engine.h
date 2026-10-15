/**
 * @file  at_engine.h
 * @brief DA16200 AT 命令引擎（异步两段式，见 DESIGN.md §4）
 *
 * 设计约束（DESIGN.md §8.1 根因2 对策）:
 *   - start 只提交发送, 不等待; poll 每 tick 推进, 主循环阻塞 <= 1 tick
 *   - 响应按字节采集到 resp.buf (4KB, 风险#5), 行级仅用于终止/URC 检测
 *   - 空闲期的 '+' 行进入 URC FIFO, 由 at_engine_urc_take() 消费（禁止脉冲标志）
 */
#ifndef AT_ENGINE_H
#define AT_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#define AT_RESP_BUF_SIZE   4096U          /* 响应/payload 采集缓冲 */
#define AT_CMD_MAX         180U           /* 单条命令上限(含自动补的 CRLF) */
#define AT_LINE_MAX        2048U          /* +TRDTS hex 模式可含 HTTP 请求行/头/表单 */
#define AT_URC_FIFO        4U             /* URC 消费队列深度 */
#define AT_URC_LINE        96U
#define AT_SEEN_MAX        8U             /* sticky "曾出现过" 记录条数 */

typedef enum {
    AT_RES_PENDING = 0,                   /* 命令在途 */
    AT_RES_OK,                            /* 成功 */
    AT_RES_ERR,                           /* 失败, 见 resp.err_code */
    AT_RES_TIMEOUT,                       /* 超时/发送失败 */
    AT_RES_BUSY,                          /* 引擎已有在途命令 */
    AT_RES_ABORT                          /* 被 at_engine_abort() 中止 */
} at_result_t;

/* 命令完成方式。DA16200 的两种协议完全不同(2026-10-03 实测):
 *   CMD     : 大部分命令, 收到 OK / ERROR 即完成
 *   PAYLOAD : AT+NWHTC 专用 —— 发完命令模块立刻回一个裸 OK(仅同步回执),
 *             正文随后异步流式推送(约 667KB), 真正的结束标志是末尾的
 *             +NWHTCSTATUS:<n> (0=成功)。把 OK 当结束只会拿到 6 字节。 */
typedef enum {
    AT_MODE_CMD = 0,
    AT_MODE_PAYLOAD
} at_mode_t;

typedef struct {
    char     buf[AT_RESP_BUF_SIZE];       /* 命令发出到终止之间的原始字节 */
    uint16_t len;                         /* 已用字节数 */
    bool     overflow;                    /* buf 满被截断 */
    int      err_code;                    /* ERROR:-x 的 x, 未知为 -1 */
    int      status;                      /* PAYLOAD 模式: +NWHTCSTATUS:<n> 的 n, 未收到为 -1 */
} at_resp_t;

/**
 * @brief 引擎快照（低频心跳诊断用）
 *
 * 为什么不靠"多打日志"排查：诊断日志本身会挤占 USB TX 环，10ms 一行就足以
 * 把真正的 [WIFI]/[wx] 全部挤掉（__io_putchar 满则丢），表现为"程序不动了"。
 * 所以改为**低频快照**，需要时由上层周期打印。
 */
typedef struct {
    uint8_t  state;                       /* 引擎内部状态机值: 0=IDLE 1=SENDING 2=WAIT */
    uint8_t  mode;                        /* at_mode_t */
    uint8_t  urc_cnt;                     /* URC 消费 FIFO 里待取的条数 */
    uint8_t  seen_cnt;                    /* sticky "曾出现过" 记录条数 */
    uint16_t line_len;                    /* 当前正在组装的那一行的长度 */
    uint32_t rx_bytes;                    /* 引擎启动至今收到的 WIFI 字节数 */
    uint32_t lines;                       /* 启动至今组装完成的行数 */
    uint32_t plus_lines;                  /* 其中以 '+' 开头的行数(URC/终止行) */
} at_engine_diag_t;

typedef void (*at_engine_urc_callback_t)(const char *line, uint16_t length, void *context);

void             at_engine_init(void);
void             at_engine_tick(uint32_t elapsed_ms);   /* 每主循环 tick 调用 */
uint32_t         at_engine_now_ms(void);
bool             at_engine_idle(void);

/** @brief 当前模式: 0=AT_MODE_CMD, 1=AT_MODE_PAYLOAD。给串口提示用 */
uint8_t          at_engine_mode(void);
at_result_t      at_engine_start(const char * cmd, uint32_t timeout_ms);
at_result_t      at_engine_start_ex(const char * cmd, uint32_t timeout_ms, at_mode_t mode);
at_result_t      at_engine_poll(void);                   /* 每 tick 调用 */
const at_resp_t * at_engine_resp(void);
void             at_engine_abort(void);
void             at_engine_rx(char c);                   /* WIFI 口每字节注入 */
uint32_t         at_engine_rx_len(void);                 /* 诊断用: 已采集的响应字节数 */
bool             at_engine_urc_take(const char * prefix);/* 匹配并消费一条 URC(只给 bool) */
void             at_engine_diag(at_engine_diag_t * out);  /* 取状态快照 */

/**
 * @brief 当前是否有 NWHTC 正文正在推送
 *
 * 用于关掉 [WIFI] 回显：payload 期间模块回的每个字节都已存进 resp.buf，
 * 解析走的是那份缓冲，回显是冗余的，而 15 行 HTTP 头会把真正的解析结论
 * 顶出串口窗口（实测窗口约 44 行）。
 */
bool             at_engine_payload_active(void);

/* --- 被动监听用（风险#4 开机自动重连）---
 * urc_seen() 只查不删, 且不受 4 槽 FIFO 深度限制(用 sticky 槽位记录),
 * 因此 45s 监听期内即使 URC 超过 4 条也不会漏判。 */
bool             at_engine_urc_seen(const char * prefix);
void             at_engine_urc_seen_clear(void);
void             at_engine_urc_trace(bool on);          /* 每条 URC 打时间戳 */

/**
 * @brief 匹配并取出一条 URC 的行内容（消费型）
 *
 * 与 at_engine_urc_take() 的区别：take 只返回 bool，本函数把整行拷出来，
 * 用于需要解析 URC 载荷的场景（如从 +WFJAP:1,'ssid','ip' 里提取 IP）。
 *
 * @param prefix   匹配前缀（如 "+WFJAP:1"）
 * @param out      输出缓冲
 * @param out_size 缓冲大小
 * @return true=找到并拷贝成功；false=未找到或缓冲太小
 */
bool             at_engine_urc_get(const char * prefix, char * out, uint16_t out_size);
/** Register callback for long binary/text URCs such as +TRDTS. */
void             at_engine_urc_callback_set(at_engine_urc_callback_t callback, void *context);

#endif /* AT_ENGINE_H */
