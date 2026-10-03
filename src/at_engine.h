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
#define AT_LINE_MAX        256U           /* 行检测缓冲(超长行截断, 不参与检测) */
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

void             at_engine_init(void);
void             at_engine_tick(uint32_t elapsed_ms);   /* 每主循环 tick 调用 */
uint32_t         at_engine_now_ms(void);
bool             at_engine_idle(void);
at_result_t      at_engine_start(const char * cmd, uint32_t timeout_ms);
at_result_t      at_engine_start_ex(const char * cmd, uint32_t timeout_ms, at_mode_t mode);
at_result_t      at_engine_poll(void);                   /* 每 tick 调用 */
const at_resp_t * at_engine_resp(void);
void             at_engine_abort(void);
void             at_engine_rx(char c);                   /* WIFI 口每字节注入 */
uint32_t         at_engine_rx_len(void);                 /* 诊断用: 已采集的响应字节数 */
bool             at_engine_urc_take(const char * prefix);/* 匹配并消费一条 URC */

/* --- 被动监听用（风险#4 开机自动重连）---
 * urc_seen() 只查不删, 且不受 4 槽 FIFO 深度限制(用 sticky 槽位记录),
 * 因此 45s 监听期内即使 URC 超过 4 条也不会漏判。 */
bool             at_engine_urc_seen(const char * prefix);
void             at_engine_urc_seen_clear(void);
void             at_engine_urc_trace(bool on);          /* 每条 URC 打时间戳 */

/**
 * @brief 匹配并取出一条 URC（消费型，返回行内容）
 *
 *与at_engine_urc_take 的区别：take 只返回 bool、不给行内容。
 *用于需要解析 URC 载荷的场景（如 +WFJAP:1,'ssid','ip' 里提取 IP）。
 *
 * @param prefix  匹配前缀（如 "+WFJAP:1"）
 * @param out     输出缓冲
 * @param out_size 缓冲大小
 * @return true=找到并拷贝成功，false=未找到或缓冲太小
 */
bool             at_engine_urc_get(const char * prefix, char * out, uint16_t out_size);

#endif /* AT_ENGINE_H */
