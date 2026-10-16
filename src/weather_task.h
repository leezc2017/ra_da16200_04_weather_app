/**
 * @file  weather_task.h
 * @brief 天气主状态机（裸机 10ms tick，非阻塞）
 *
 * 移植自 GD32VW553 版 weather_task.c，架构对应关系：
 *
 *   GD32 FreeRTOS 任务          ->  RA 裸机主循环
 *   weather_task(void *arg)      ->  weather_task_init() + weather_task_poll()
 *   while(1) { ... sleep(20) }   ->  while(1) { poll(); delay(10ms) }
 *
 * 单次 poll 阻塞 <= 1 tick（DESIGN §4 硬性要求）：
 * AT 命令全部异步投递，靠 at_engine_poll()/weather_http_poll() 推进，
 * 绝不阻塞等待，故连接失败期间按键仍能即时响应（§8.1 根因2 验收点）。
 */
#ifndef _WEATHER_TASK_H_
#define _WEATHER_TASK_H_

#include <stdint.h>
#include <stdbool.h>
#include "wifi_status.h"

/**
 * @brief 初始化天气任务（OLED、按键、SNTP 启动）
 *
 * 调用前提：两个串口与 OLED 移植层已初始化。
 */
void weather_task_init(void);

/**
 * @brief 主状态机推进（主循环每 tick 调用，10ms）
 */
void weather_task_poll(void);

/**
 * @brief 立即打印一次心跳快照（不等 WEATHER_HB_DIAG_MS 周期）
 *
 * 供串口输入 "HB" 调用。用途：串口助手一般只显示第一屏约 20~25 行，
 * 而心跳要等 5s、按键事件要等用户动手，两者都落在窗口外。
 * 由用户主动触发，响应直接追加到当前可见区，绕开窗口大小限制。
 */
void weather_task_heartbeat_force(void);

/* --- 最近一次天气抓取结论（缓存，见 weather_task.c 内 s_wxr 说明） ---
 * 存在的理由：一次性的 [WX-RESULT] 打印很容易落在串口窗口外而彻底看不到，
 * 缓存后结论是**被动可读**的：心跳行带 res= 字段，OLED 也能直接显示。
 * 这样判断"天气到底通没通"不需要抓日志、不需要掐时间。 */

/** @brief 结论字符串: "OK" / "HTTP ERR" / "PARSE ERR" / "TRANSPORT ERR" / "no fetch yet" */
const char * weather_task_last_result(void);
/** @brief 结论对应��温度(无数据显示 0) */
int          weather_task_last_temp(void);
/** @brief 上屏用的天气文案(ASCII, code 查表; 无数据返回 "--") */
const char * weather_task_last_text(void);
/** @brief 把结论拼成一行短文本, 供 OLED 显示 */
void         weather_task_result_line(char * buf, uint16_t size);

/**
 * @brief 当前 WiFi/配网状态（供诊断打印）
 * @return 五态之一
 */
wifi_status_t weather_task_wifi_status(void);
bool          weather_task_weather_valid(void);
bool          weather_task_provisioning_active(void);
void          weather_task_tcp_urc(const char *line, uint16_t length);

/**
 * @brief 触发一次天气刷新（按键短按用）
 */
void weather_task_refresh_now(void);

#endif /* _WEATHER_TASK_H_ */
