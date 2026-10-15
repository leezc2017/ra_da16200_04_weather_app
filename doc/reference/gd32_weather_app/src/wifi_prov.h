/**
 * @file  wifi_prov.h
 * @brief WiFi 凭据存储 + 配网流程 + 连接状态
 */
#ifndef _WIFI_PROV_H_
#define _WIFI_PROV_H_

#include <stdint.h>

#define WIFI_SSID_MAX_LEN   32
#define WIFI_PWD_MAX_LEN    64
#define WIFI_CITY_MAX_LEN   32

typedef enum {
    WIFI_STAT_IDLE = 0,      /* 上电, 未开始 */
    WIFI_STAT_PROVISIONING,  /* SoftAP 配网中 */
    WIFI_STAT_CONNECTING,    /* STA 连接中 */
    WIFI_STAT_CONNECTED,     /* 已连上, 拿到IP */
    WIFI_STAT_FAIL,          /* 重试耗尽 */
} wifi_status_t;

typedef struct {
    uint32_t magic;              /* 'WCFG' 校验 */
    uint32_t crc;                /* ssid+pwd+city 的CRC32 */
    char     ssid[WIFI_SSID_MAX_LEN + 1];
    char     pwd[WIFI_PWD_MAX_LEN + 1];
    char     city[WIFI_CITY_MAX_LEN + 1];  /* 城市拼音 */
} wifi_credential_t;

/* 凭据存取 */
int  wifi_cred_store(const char *ssid, const char *pwd, const char *city);
int  wifi_cred_load(wifi_credential_t *out);

/* 生命周期 */
int  wifi_prov_init(void);                     /* 读Flash凭据 */
void wifi_prov_start(void);                    /* 启动: 有凭据直连, 无则配网 */
void wifi_prov_enter_provisioning(void);       /* 手动进入配网(长按) */
void wifi_prov_cancel_provisioning(void);      /* 取消配网, 回连或进FAIL */
void wifi_prov_check_alive(void);              /* 断线检测->重连 */

/* 状态查询(UI用) */
wifi_status_t    wifi_prov_status(void);
const char      *wifi_prov_current_ssid(void);
const char      *wifi_prov_current_password(void);
const char      *wifi_prov_ip_str(void);       /* 已连接时的IP字符串, 未连返回NULL */
int8_t           wifi_prov_rssi(void);         /* 已连接时有效 */
uint8_t          wifi_prov_fail_count(void);
const char      *wifi_prov_city(void);         /* 当前城市(从flash读取) */
void             wifi_prov_city_set(const char *city);  /* 设置城市(存flash) */

/* 主循环周期调用: 配网期间探测"已拿到IP"=配网成功 */
int  wifi_prov_poll(void);                      /* 返回1表示本次刚完成配网 */

/* 事件回调(由WiFi事件线程上下文调用, 内部只做记录+消息通知) */
void wifi_prov_events_bind(void);              /* 注册 eloop 事件 */

#endif /* _WIFI_PROV_H_ */
