/**
 * @file  weather_http.h
 * @brief NWHTC HTTP 取数封装（AT+NWHTC PAYLOAD 模式）
 *
 * 为什么不直接用 at_engine：
 *   NWHTC 的收尾逻辑特殊（见 at_engine.h AT_MODE_PAYLOAD），且每次请求前后
 *   要做 URL 构造、HTTP 头切除、状态码校验，封装一层避免调用方重复踩坑。
 *
 * 协议（2026-10-03 实测）：
 *   发 AT+NWHTC=<uri>,get
 *     -> 立刻回裸 OK（仅同步回执，不是结束）
 *     -> 异步流式推送完整 HTTP 响应（含响应头）
 *     -> 末尾 +NWHTCSTATUS:<n>  才是结束标志（0=成功）
 *   故必须用 AT_MODE_PAYLOAD，且切 \r\n\r\n 后才是 JSON body。
 *
 * ⚠ 单飞行：同一时刻只能有一条 NWHTC 在途（§4 并发约束）。
 * ⚠ URL 不能含逗号，或必须用单引号整体包住（实测裸 URL 里 & ? 都不需转义）。
 */
#ifndef _WEATHER_HTTP_H_
#define _WEATHER_HTTP_H_

#include <stdint.h>
#include <stdbool.h>
#include "at_engine.h"

#define WEATHER_HTTP_BODY_MAX  3072U   /* JSON body 上限(4KB resp 缓冲减去头) */

/**
 * @brief HTTP GET 请求结果
 */
typedef struct {
    int      len;                            /* body 字节数, <0 表示失败 */
    int      http_status;                    /* HTTP 状态码, 解析失败为 0 */
    int      nw_status;                      /* +NWHTCSTATUS 的值, -1=未收到 */
    char     content_type[48];               /* 响应 Content-Type, 无则空串 */
} weather_http_result_t;

/**
 * @brief 发起一次 GET（NWHTC PAYLOAD 模式，非阻塞）
 *
 * 投递后立即返回，由 weather_task_poll() 推进状态机。
 *
 * @param url         完整 URL（含 http:// 与 query），不含 ",get"
 * @param timeout_ms  等待 +NWHTCSTATUS 的上限(ms)
 * @return true=已投递, false=引擎忙或 URL 超长
 */
bool weather_http_get_start(const char * url, uint32_t timeout_ms);

/**
 * @brief 引擎命令完成回调（由 weather_task_poll 调用）
 *
 * ⚠ 本模块**不**自己轮询引擎 —— 轮询权归 weather_task（DESIGN §4 单飞行：
 * 同一时刻只能一条命令在途，必须有唯一的轮询点）。引擎一旦完成，
 * weather_task 按命令归属把结果分发给对应模块。
 *
 * @param r 引擎返回的结果
 */
void weather_http_on_engine_done(at_result_t r);

/**
 * @brief 查询是否正在请求
 * @return true=请求在途
 */
bool weather_http_busy(void);

/**
 * @brief 查询是否已完成（成功或失败），结果可取
 * @return true=已完成
 */
bool weather_http_done(void);

/**
 * @brief 取走结果并回到 IDLE（允许下一次请求）
 */
void weather_http_clear(void);

/**
 * @brief 取最近一次结果
 * @return 结果指针（结构体静态存储，非 NULL）
 */
const weather_http_result_t * weather_http_result(void);

/**
 * @brief 取最近的 body（仅在 weather_http_busy()==false 且 len>0 时有效）
 * @param size 输出 body 实际长度
 * @return body 指针（静态存储，'\0' 结尾）
 */
const char * weather_http_body(uint16_t * size);

#endif /* _WEATHER_HTTP_H_ */