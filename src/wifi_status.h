/**
 * @file  wifi_status.h
 * @brief WiFi/配网连接状态枚举（平台无关）
 *
 * 从 GD32 版 wifi_prov.h 抽出。原因是 weather_ui.h 需要 wifi_status_t，
 * 但 wifi_prov.c 是 GD32 片上 WiFi 的实现，RA 工程用 AT 引擎驱动
 * DA16200，两者不能共用同一个头文件。
 *
 * 五态与 GD32 版保持一致（对齐 §8.1 的 UI/状态机一致性设计）：
 *   IDLE -> CONNECTING -> CONNECTED
 *                  |
 *                  +-> PROVISIONING (长按进配网)
 *                  +-> FAIL (重试耗尽)
 */
#ifndef _WIFI_STATUS_H_
#define _WIFI_STATUS_H_

typedef enum {
    WIFI_STAT_IDLE = 0,      /* 上电, 未开始 */
    WIFI_STAT_PROVISIONING,  /* SoftAP 配网中 */
    WIFI_STAT_CONNECTING,    /* STA 连接中 */
    WIFI_STAT_CONNECTED,     /* 已连上, 拿到 IP */
    WIFI_STAT_FAIL,          /* 重试耗尽 */
} wifi_status_t;

#endif /* _WIFI_STATUS_H_ */