/**
 * @file  at_probe.h
 * @brief 阶段1 自动诊断
 *
 *   at_probe_start()      AUTO  : 全链路诊断, 收口风险#1/#2/#3/#5
 *   at_probe_boot_start() AUTOB : 开机自动重连测试, 收口风险#4
 *   at_probe_abort()      ABORT : 中止
 *
 * USB 输入触发, 透传模式期间的行为见 hal_entry.c。
 */
#ifndef AT_PROBE_H
#define AT_PROBE_H

#include <stdbool.h>

void at_probe_start(void);
void at_probe_boot_start(void);
void at_probe_abort(void);
void at_probe_poll(void);                    /* 每 tick 调用 */
bool at_probe_running(void);

#endif /* AT_PROBE_H */
