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
#define WEATHER_TZ_HOURS         8       /* 时区: UTC+8。实测 AT+TZONE 单位就是秒,
                                           故下发时用 WEATHER_TZ_HOURS*3600 */
#define NTP_SERVER_1             "ntp.aliyun.com"
#define NTP_SERVER_2             "pool.ntp.org"

/* ===================== 3.1 按键 ========================= */
/* 阶段2 只接 1 个机械键（GPIOINPUT 示例读 P005）。
 * 触摸键留阶段3（DESIGN §6 的 4 键映射在阶段3 全接入）。
 *
 * ⚠ P005 的输入模式/上拉由 FSP 侧配置：见 ra_gen/pin_data.c 的 g_pin_cfg。
 *   若按下无反应，先查该引脚是否配成输入 + 上拉，再调 WEATHER_KEY_ACTIVE_LOW。
 *   极性：1 = 按下为低电平(上拉式按钮)。 */
#define WEATHER_KEY_PIN          BSP_IO_PORT_00_PIN_05      /* P005 */

/** 1 = 按下为低电平(上拉) */
#define WEATHER_KEY_ACTIVE_LOW   1

/** 按键时序(ms) */
#define KEY_POLL_MS              20U      /* 轮询周期 */
#define KEY_LONG_MS           3000U      /* 长按判定阈值 */
#define KEY_DOUBLE_MS          500U      /* 双击间隔阈值 */

/* ===================== 4. 阶段1 诊断 ==================== */
/* NWHTC 诊断超时。NWHTC 是异步流式协议(见 at_engine.h AT_MODE_PAYLOAD):
 * 发命令回裸 OK 后正文才流式推送, 真正结束是末尾的 +NWHTCSTATUS:<n>,
 * 所以超时必须覆盖 DNS + 连接 + 传输全过程, 比普通命令宽。 */
#define PROBE_HTTP_TIMEOUT_MS    25000

/* ===================== 5. 阶段2 天气链路 ================ */
/* 天气请求超时(NWHTC 模式, 需覆盖 DNS+连接+传输) */
#define WEATHER_HTTP_TIMEOUT_MS  20000
/* 轮询节拍: 主循环每 tick 调一次 weather_task_poll() */
#define WEATHER_TICK_MS          10U
/* 时间查询周期(ms): RA 无 RTC, 主页时钟靠查 DA16200 的 AT+TIME */
#define WEATHER_TIME_POLL_MS     60000U
/* 联网判定超时: 超过此时长没收到 +WFJAP 则兜底发 AT+WFCAP */
#define WEATHER_CONNECT_TIMEOUT_MS  30000U
/* 下发 SNTP 后等对时生效的时间, 期间提前查一次 AT+TIME 验证 */
#define SNTP_SETTLE_MS           8000U

#endif /* WEATHER_CFG_H */
