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
 * @brief 当前 WiFi/配网状态（供诊断打印）
 * @return 五态之一
 */
wifi_status_t weather_task_wifi_status(void);

/**
 * @brief 触发一次天气刷新（按键短按用）
 */
void weather_task_refresh_now(void);

#endif /* _WEATHER_TASK_H_ */