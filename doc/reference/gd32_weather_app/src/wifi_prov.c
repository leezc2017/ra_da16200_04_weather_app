/**
 * @file  wifi_prov.c
 * @brief WiFi 凭据Flash存储 + 连接流程 + SoftAP配网编排 + 状态查询
 *
 * 设计要点:
 *  - 轮询式状态机(不注册 eloop 事件), 避免与官方配网模块的事件注册冲突
 *  - 凭据存于 flash 用户区末页(带CRC32校验), 断电不丢
 *  - 连接用 wifi_management_connect(blocked=1), 官方例程同款阻塞模式
 */
#include <string.h>
#include <stdio.h>

#include "wifi_management.h"
#include "wifi_net_ip.h"
#include "wifi_softap_provisioning.h"
#include "raw_flash_api.h"

#include "weather_cfg.h"
#include "wifi_prov.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"

extern void weather_ui_state_changed(void);

/* 配网模块暴露的STA连接完成标志(wifi_softap_provisioning.c) */
extern volatile uint8_t g_prov_sta_connected;

#ifndef WIFI_VIF_INDEX_DEFAULT
#define WIFI_VIF_INDEX_DEFAULT  0
#endif

#define CFG_MAGIC          0x57434647UL   /* "WCFG" */

/* ---------------- 内部状态 ---------------- */
static wifi_status_t    s_status = WIFI_STAT_IDLE;
static wifi_credential_t s_cred;               /* RAM 当前生效凭据 */
static uint8_t          s_cred_valid;          /* flash 有凭据 */
static uint8_t          s_fail_count;
static uint8_t          s_provisioning;        /* 配网任务运行中 */
static char             s_ip_str[16];

static const char *wifi_status_name(wifi_status_t status)
{
    switch (status) {
    case WIFI_STAT_IDLE:
        return "IDLE";
    case WIFI_STAT_PROVISIONING:
        return "PROVISIONING";
    case WIFI_STAT_CONNECTING:
        return "CONNECTING";
    case WIFI_STAT_CONNECTED:
        return "CONNECTED";
    case WIFI_STAT_FAIL:
        return "FAIL";
    default:
        return "UNKNOWN";
    }
}

static void wifi_prov_status_log(const char *event)
{
    const char *ip = wifi_prov_ip_str();

    printf("[prov] %s: status=%s(%d), ssid=%s, ip=%s, fail=%u\r\n",
           event,
           wifi_status_name(s_status),
           (int)s_status,
           wifi_prov_current_ssid(),
           ip ? ip : "<none>",
           (unsigned)s_fail_count);
    weather_ui_state_changed();
}

/* ---------------- CRC32 ---------------- */
static uint32_t crc32_calc(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return ~crc;
}

/* ---------------- 凭据持久化 ---------------- */
/* 配网模块钩子的强实现: 保存到 flash 用户区 */
/* 由 wifi_softap_provisioning.c 副本在收到新凭据后调用 */
void weather_prov_save_hook(const char *ssid, const char *pwd, const char *city)
{
    printf("[prov] save_hook: ssid=%s city=%s\r\n", ssid, city ? city : "(null)");
    const char *safe_city = city ? city : "";
    int ret = wifi_cred_store(ssid, pwd, safe_city);
    printf("[prov] wifi_cred_store ret=%d\r\n", ret);
    strncpy((char *)s_cred.ssid, ssid, WIFI_SSID_MAX_LEN);
    strncpy((char *)s_cred.pwd,  pwd,  WIFI_PWD_MAX_LEN);
    strncpy((char *)s_cred.city, safe_city, WIFI_CITY_MAX_LEN);
    s_cred.ssid[WIFI_SSID_MAX_LEN] = '\0';
    s_cred.pwd[WIFI_PWD_MAX_LEN]   = '\0';
    s_cred.city[WIFI_CITY_MAX_LEN] = '\0';
    s_cred_valid = 1;
}

int wifi_cred_load(wifi_credential_t *out)
{
    if (out == NULL ||
        raw_flash_read(WEATHER_CFG_FLASH_OFFSET, out, sizeof(*out)) != 0) {
        return -1;
    }
    if (out->magic != CFG_MAGIC) {
        return -1;
    }
    uint32_t crc = crc32_calc((const uint8_t *)out->ssid,
                              WIFI_SSID_MAX_LEN + 1 + WIFI_PWD_MAX_LEN + 1 + WIFI_CITY_MAX_LEN + 1);
    if (crc != out->crc) {
        return -1;
    }
    return 0;
}

int wifi_cred_store(const char *ssid, const char *pwd, const char *city)
{
    wifi_credential_t c;
    wifi_credential_t verify;
    int ret;

    if (ssid == NULL || pwd == NULL || city == NULL) {
        return -1;
    }

    memset(&c, 0, sizeof(c));
    c.magic = CFG_MAGIC;
    strncpy(c.ssid, ssid, WIFI_SSID_MAX_LEN);
    strncpy(c.pwd,  pwd,  WIFI_PWD_MAX_LEN);
    strncpy(c.city, city, WIFI_CITY_MAX_LEN);
    c.crc = crc32_calc((const uint8_t *)c.ssid,
                       WIFI_SSID_MAX_LEN + 1 + WIFI_PWD_MAX_LEN + 1 + WIFI_CITY_MAX_LEN + 1);

    ret = raw_flash_erase(WEATHER_CFG_FLASH_OFFSET, 4096);
    if (ret != 0) {
        printf("[prov] cred erase failed, ret=%d\r\n", ret);
        return -1;
    }

    ret = raw_flash_write_fast(WEATHER_CFG_FLASH_OFFSET, &c, sizeof(c));
    if (ret != 0) {
        printf("[prov] cred write failed, ret=%d\r\n", ret);
        return -1;
    }

    ret = wifi_cred_load(&verify);
    if (ret != 0 || memcmp(&verify, &c, sizeof(c)) != 0) {
        printf("[prov] cred readback failed, ret=%d city=%s\r\n", ret, c.city);
        return -1;
    }

    printf("[prov] cred saved: %s city=%s\r\n", c.ssid, c.city);
    return 0;
}

/* ---------------- 状态查询 ---------------- */
wifi_status_t wifi_prov_status(void)
{
    return s_status;
}

const char *wifi_prov_current_ssid(void)
{
    return s_cred.ssid[0] ? s_cred.ssid : "<none>";
}

const char *wifi_prov_current_password(void)
{
    return s_cred.pwd;
}

uint8_t wifi_prov_fail_count(void)
{
    return s_fail_count;
}

const char *wifi_prov_city(void)
{
    return s_cred.city[0] ? s_cred.city : "";
}

void wifi_prov_city_set(const char *city)
{
    strncpy((char *)s_cred.city, city, WIFI_CITY_MAX_LEN);
    s_cred.city[WIFI_CITY_MAX_LEN] = '\0';
    /* 更新flash中的凭据(保留ssid和pwd) */
    wifi_cred_store(s_cred.ssid, s_cred.pwd, s_cred.city);
}

int8_t wifi_prov_rssi(void)
{
    return 0;   /* SDK未提供直查API, UI不显示RSSI */
}

/* IP 字符串(已连接时有效), 查不到返回 NULL */
const char *wifi_prov_ip_str(void)
{
    struct wifi_ip_addr_cfg cfg;
    if (wifi_get_vif_ip(WIFI_VIF_INDEX_DEFAULT, &cfg) < 0 || cfg.ipv4.addr == 0) {
        return NULL;
    }
    snprintf(s_ip_str, sizeof(s_ip_str), "%u.%u.%u.%u",
             (unsigned)(cfg.ipv4.addr & 0xFF),
             (unsigned)((cfg.ipv4.addr >> 8) & 0xFF),
             (unsigned)((cfg.ipv4.addr >> 16) & 0xFF),
             (unsigned)((cfg.ipv4.addr >> 24) & 0xFF));
    return s_ip_str;
}

/* ---------------- 连接流程(阻塞) ---------------- */
static int sta_connect_blocked(void)
{
    printf("[prov] connecting to %s ...\r\n", s_cred.ssid);
    s_status = WIFI_STAT_CONNECTING;
    wifi_prov_status_log("connection started");

    wifi_management_disconnect();
    sys_ms_sleep(300);

    if (wifi_management_connect(s_cred.ssid, s_cred.pwd, 1) < 0) {
        printf("[prov] connect cmd failed\r\n");
        wifi_prov_status_log("connection command failed");
        return -1;
    }
    /* 等DHCP拿IP, 最多15s */
    for (int i = 0; i < 30; i++) {
        if (wifi_prov_ip_str() != NULL) {
            return 0;
        }
        sys_ms_sleep(500);
    }
    printf("[prov] connect ok but no ip\r\n");
    wifi_prov_status_log("connection timed out");
    return -1;
}

/* ---------------- 生命周期 ---------------- */
int wifi_prov_init(void)
{
    wifi_credential_t loaded;

    memset(&s_cred, 0, sizeof(s_cred));
    s_cred_valid = (wifi_cred_load(&loaded) == 0);
    if (s_cred_valid) {
        memcpy(&s_cred, &loaded, sizeof(s_cred));
        printf("[prov] saved wifi: %s\r\n", s_cred.ssid);
        printf("[prov] saved city: %s\r\n", s_cred.city);
    } else {
        printf("[prov] no saved wifi\r\n");
    }
    return 0;
}

void wifi_prov_start(void)
{
    if (s_status == WIFI_STAT_CONNECTED) {
        return;
    }
    if (!s_cred_valid) {
        wifi_prov_enter_provisioning();     /* 无凭据: 直接配网 */
        return;
    }
    s_fail_count = 0;
    while (s_fail_count < 3) {
        if (sta_connect_blocked() == 0) {
            s_status = WIFI_STAT_CONNECTED;
            wifi_prov_status_log("wifi connected");
            return;
        }
        s_fail_count++;
        sys_ms_sleep(2000);
    }
    s_status = WIFI_STAT_FAIL;
    wifi_prov_status_log("wifi connection failed");
    printf("[prov] wifi failed x3, long-press key to provision\r\n");
}

void wifi_prov_enter_provisioning(void)
{
    if (s_provisioning) {
        return;
    }
    s_status = WIFI_STAT_PROVISIONING;
    wifi_prov_status_log("enter softap provisioning");
    s_provisioning = 1;
    wifi_management_disconnect();
    sys_ms_sleep(500);          /* 等WiFi完全退出STA模式 */
    wifi_softap_provisioning_start();
}

void wifi_prov_cancel_provisioning(void)
{
    if (!s_provisioning) {
        return;
    }
    s_provisioning = 0;
    wifi_softap_provisioning_stop();
    wifi_management_disconnect();
    sys_ms_sleep(500);
    /* 配网取消: 有凭据则回连, 无则失败态 */
    if (s_cred_valid) {
        wifi_prov_start();
    } else {
        s_status = WIFI_STAT_FAIL;
        wifi_prov_status_log("provisioning cancelled");
    }
}

/* 主循环周期调用: 配网期间探测"STA连接主路由成功"=配网完成 */
int wifi_prov_poll(void)
{
    if (s_provisioning && g_prov_sta_connected) {
        wifi_credential_t loaded;

        /* 配网完成: 模块已关闭SoftAP并连上主路由(真正联网) */
        s_provisioning = 0;
        /* 模块内部已持有新凭据, 重新读取flash(配网钩子已保存) */
        s_cred_valid = (wifi_cred_load(&loaded) == 0);
        if (s_cred_valid) {
            memcpy(&s_cred, &loaded, sizeof(s_cred));
            printf("[prov] loaded wifi: %s city=%s\r\n",
                   s_cred.ssid, s_cred.city);
        } else {
            /* 钩子未存到(比如旧版模块), 直接用当前连接的凭据 */
            printf("[prov] flash credentials invalid, keep RAM city=%s\r\n",
                   s_cred.city);
            s_cred_valid = 1;
        }
        s_status = WIFI_STAT_CONNECTED;
        g_prov_sta_connected = 0;
        wifi_prov_status_log("provisioning completed");
        return 1;
    }
    return 0;
}

/* 断线检测: 已连接但IP丢失 -> 重新走连接流程 */
void wifi_prov_check_alive(void)
{
    if (s_status != WIFI_STAT_CONNECTED) {
        return;
    }
    if (wifi_prov_ip_str() != NULL) {
        return;
    }
    s_status = WIFI_STAT_IDLE;
    wifi_prov_status_log("ip lost, reconnecting");
    wifi_prov_start();          /* 内部重试3次, 失败进FAIL */
}
