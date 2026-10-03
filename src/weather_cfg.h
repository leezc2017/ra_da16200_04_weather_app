/**
 * @file  weather_cfg.h
 * @brief 天气显示站 用户配置（对应 GD32 版同名文件，随阶段推进补充）
 */
#ifndef WEATHER_CFG_H
#define WEATHER_CFG_H

/* ===================== 1. WiFi 配置 ===================== */
/* 留空 "" = 用 DA16200 NVRAM 中已保存的 profile 连接（AT+WFCAP）。
 * 填写后连接失败时回退 AT+WFJAPA=<ssid>,<pwd>；
 * AUTOB（开机自动重连测试）必须有 SSID 才会运行。
 * ⚠ 口令会随 [AT>] 回显明文出现在串口日志里，测试板可接受；量产前需改为
 *   不回显或从 DA16200 NVRAM 只存不读。 */
#define WEATHER_DEFAULT_SSID     "CMCC-xxxx"
#define WEATHER_DEFAULT_PWD      "<PASSWORD>"

/* ===================== 2. 心知天气 API =================== */
#define WEATHER_API_KEY          "<REDACTED_API_KEY>"
/* 城市拼音, 留空 "" = 按IP自动定位。例: "beijing" */
#define WEATHER_CITY             ""
#define WEATHER_REFRESH_MIN      15      /* 天气刷新周期(分钟) */
#define WEATHER_REQ_TIMEOUT_MS   9000    /* HTTP 请求超时(ms) */

/* ===================== 3. 时间 ========================== */
#define WEATHER_TZ_HOURS         8       /* 时区: UTC+8 */
#define NTP_SERVER_1             "ntp.aliyun.com"
#define NTP_SERVER_2             "pool.ntp.org"

/* ===================== 4. 阶段1 诊断 ==================== */
/* NWHTC 诊断超时。NWHTC 是异步流式协议(见 at_engine.h AT_MODE_PAYLOAD):
 * 发命令回裸 OK 后正文才流式推送, 真正结束是末尾的 +NWHTCSTATUS:<n>,
 * 所以超时必须覆盖 DNS + 连接 + 传输全过程, 比普通命令宽。 */
#define PROBE_HTTP_TIMEOUT_MS    25000

#endif /* WEATHER_CFG_H */
