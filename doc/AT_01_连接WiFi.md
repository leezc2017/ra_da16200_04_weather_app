# DA16200 AT 命令 ①：连接 WiFi

> 本文档只写**已经实测跑通**的命令。每条命令都标注了证据来源。

---

## 0. 资料来源与证据等级

| 等级 | 含义 | 来源 |
| --- | --- | --- |
| **[M]** | 官方手册原文 | `UM-WI-003, DA16200/DA16600 Host Interface and AT Command, Rev.3.5, Feb 7 2025`（183 页 PDF）<br>`..\ra4m2_da16200_wifi\refs\docs\UM-WI-003_DA16200_AT_Command_Manual.pdf` |
| **[M2]** | 旧版手册（仅作对照） | `umwi003_r23.txt`，Rev.2.3, 2021 |
| **[L]** | 本机串口实测日志 | `4.weather_app\log\AUTO.txt`、`AUTOB.txt`（2026-10-03，probe 模式，全流程跑通） |
| **[L2]** | 本机串口实测日志（当前固件） | `..\..\serial_capture.log`（21:36，WF 系命令回 `ERROR:-5`） |

> **模块自己列出的命令表**（[L] `AUTO.txt` L11–L254，发 `AT+` 得到，以 `OK` 结尾）确认本模组固件**包含**：
> `AT+WFMODE` `AT+WFSTAT` `AT+WFSTA` `AT+WFSCAN` `AT+WFJAP` `AT+WFJAPA` `AT+WFCAP` `AT+WFQAP` `AT+WFDIS` `AT+WFRSSI` `AT+WFCC` `AT+NWIP` `AT+NWHTC` …
> 所以下面用到的命令**确实存在**，不是猜的。

---

## 1. 完整可复制序列（连接 WiFi）

按顺序逐条发送，每条以 `\r\n` 结尾。

```
AT
AT+
AT+VER
AT+WFMODE
AT+WFMODE=0
AT+WFSTAT
AT+WFSCAN
AT+WFJAPA=CMCC-xxxx,<PASSWORD>
```

**成功判据（全部满足才算连上）**

| # | 判据 | 期望输出 |
| --- | --- | --- |
| 1 | 握手 | `OK` |
| 2 | 命令表 | 一大串 `AT+xxx`，最后 `OK` |
| 3 | 模式 | `+WFMODE:0`（0=STA） |
| 4 | 扫描 | 能列出 `CMCC-xxxx` 那一行，末尾 `OK` |
| 5 | 连接 | 先 `OK`，**之后**再来 URC：`+WFJAP:1,'CMCC-xxxx',192.168.1.6` |

> **[M] 手册强制要求：必须同时等到 `OK`（或 `ERROR`）和 Operation Result（`+WFJAP:1,'<SSID>',<IP>`）才算完成。只看到 `OK` 不代表连上。**
> 实测（[L]）响应是两段：
> ```
> [AT>] AT+WFJAPA=CMCC-xxxx,<PASSWORD>
> [WIFI] +WFDAP:0,DEAUTH
> [WIFI] OK
> [WIFI] +WFJAP:1,'CMCC-xxxx',192.168.1.6
> ```

---

## 2. 逐条命令详解

### 2.1 `AT` — 握手
- **[L]** `AT` → `OK`
- 作用：确认 AT 引擎活着、UART 通。**任何诊断都先发这条。**

### 2.2 `AT+` — 列出本固件支持的全部命令
- **[M]** L645（Rev2.3）：`AT+ (none) AT command list.`
- **[L]** 实测输出以 `OK` 结尾，命令表覆盖 WF / NW / MQ / HT / TR / OTA / PROV 等全族。
- **这是判断"某命令到底存不存在"的唯一权威手段**，比翻手册更准（手册与固件版本可能不一致）。

### 2.3 `AT+VER` / `AT+SDKVER` / `AT+CHIPNAME` — 版本
- **[M]** `AT+SDKVER` 在 Rev2.11 加入。
- 用途：固件换版本后若行为突变，用这两条对比。

### 2.4 `AT+WFMODE` — 读/设 Wi-Fi 模式
- **[M]** p.46：
  ```
  AT+WFMODE=<mode>      <mode>: 0 (STA), 1 (Soft AP), 2 (Concurrent Mode)
  Response: OK or ERROR
  AT+WFMODE             →  +WFMODE:<mode>
  ```
- **切 STA 必须是 0。** 若返回 `1`，模块当前是热点模式，STA 系命令不可用。

### 2.5 `AT+WFSTAT` — 详细状态（**裸发，不能加 `=?`**）
- **[M]** p.47：
  ```
  AT+WFSTAT
  +WFSTAT:sta0
  mac_address= <mac>
  wpa_state=DISCONNECTED
  disconnect_reason=0
  OK
  ```
- **⚠️ 实测 `AT+WFSTAT=?` 回 `ERROR:-3`（Too many parameters）**——[L] `AUTO.txt` L261-263、`AUTOB.txt` L20-22 两处均如此。
  同理 `AT+DEFAP=?` → `ERROR:-3`（`AUTOB.txt` L13-15）。手册 p.24 明确写了**并非所有命令支持 `=?` 查询**。
- 关键字段：`wpa_state=COMPLETED` = 已关联；`sta0` = STA 接口，`softap1` = Soft-AP 接口。

### 2.6 `AT+WFSCAN` — 扫描
- **[M]** p.49：
  ```
  AT+WFSCAN [<ssid>]
  Response: +WFSCAN:<bssid><TAB><frequency><TAB><signal strength><TAB><flag><TAB><ssid><LF> …
  ```
- **[L]** 实测（**字段是 TAB 分隔**）：
  ```
  [AT>] AT+WFSCAN
  [WIFI] +WFSCAN:xx:xx:xx:xx:xx:xx	2427	-46	[WPA-PSK-CCMP+TKIP][WPA2-PSK-CCMP+TKIP][ESS]	CMCC-xxxx
  [WIFI] <bssid>	2467	-60	[WPA2-PSK-CCMP][WPS][ESS]	<ssid>
  …
  [WIFI] OK
  ```
- **射频好坏的判据**：能列出 AP 且 `CMCC-xxxx` 信号 `-46 dBm`（很强）。

### 2.7 `AT+WFJAPA=<ssid>,<pwd>` — 连接并把 profile 写进 NVRAM ★主力命令
- **[M]** p.55-56：
  ```
  AT+WFJAPA=<ssid>,<key>
  Response: OK or ERROR
  成功 Operation Result:  +WFJAP:1,'<SSID>','<IP Address>'
  失败:                    +WFJAP:0,<REASON>[,<REASON_CODE>]
                           ERROR:-425   (No SSID found)
  ```
- **[M] Note：**
  - 必须同时等到 `OK`/`ERROR` **和** `+WFJAP:1,'<SSID>',<IP>`；
  - 网络环境差时内部会重试，Operation Result 可能较慢；
  - 该命令执行后**不需要重启系统**；
  - **AP 配置参数（AP Profile）会存入 NVRAM。** ← 这意味着只需要成功一次，之后 `AT+WFCAP` 或断电重启都能自动重连。
- **[L]** 实测成功：
  ```
  [AT>] AT+WFJAPA=CMCC-xxxx,<PASSWORD>
  [WIFI] +WFDAP:0,DEAUTH
  [WIFI] OK
  [WIFI] +WFJAP:1,'CMCC-xxxx',192.168.1.6
  ```
  （`+WFDAP:0,DEAUTH` 是"先踢掉旧连接"的中间态，**不是失败**。）
- 密码长度 **8~63 字符**；SSID 里含逗号或单引号时必须用单引号包起来（[M] p.24）。

### 2.8 `AT+WFCAP` — 用 NVRAM 里已存的 profile 连接
- **[M]** p.56-57：
  ```
  Prerequisite: AP profile parameters should exist in NVRAM.
  AT+WFCAP
  OK
  +WFJAP:1,'MY_AP_SSID',192.168.0.7

  AT+WFCAP
  ERROR:-503      // No AP profile found
  AT+WFCAP
  ERROR:-460      // Already connected
  ```
- **[L]** 本机首次（NVRAM 无 profile）实测回 **`ERROR:-7`（No result）**——与手册写的 `-503` 不同，属固件差异，含义一致：**没 profile**。
- 用法：先跑一次 2.7 写入 profile，之后每次开机只需 `AT+WFCAP`（或干脆什么都不发，靠自动重连）。

### 2.9 `AT+WFSTA` — 查连接状态
- **[M]** p.57：
  ```
  AT+WFSTA
  +WFSTA:0        ← 未连
  OK
  AT+WFSTA
  +WFSTA:1        ← 已连
  OK
  Prerequisite: Station mode.
  Note: If the DA16200/DA16600 runs the command in Soft AP mode, it returns an error (-100).
  ```
- **裸发**，无参数。
- ⚠️ **当前固件此命令回 `ERROR:-5`，见第 4 节。**

### 2.10 `AT+WFQAP` — 断开
- **[M]** p.57：`Prerequisite: 已连接 AP`；**已断开时不会报错**。
- **[L]** 实测：`AT+WFQAP` → `+WFDAP:0,DEAUTH` + `OK`

### 2.11 `AT+RESTART` — 重启模组（用于验证自动重连）
- **[L]** 实测（`AUTOB.txt` L31–45）：
  ```
  [AT>] AT+RESTART
  [WIFI] OK
  [WIFI] +INIT:DONE,0
  [WIFI] +ATPROV=STATUS 0
  [WIFI] +WFJAP:1,'CMCC-xxxx',192.168.1.6      ← 不发任何命令，自己连上了
  [PROBE] VERDICT: boot auto-reconnect = YES (NVRAM profile connects on power-up)
  ```
- **[M]** Table 8（p.37）启动响应：
  - `+INIT:DONE,<mode>`：启动完成，`0`=STA，`1`=Soft AP → **这条同时告诉你当前模式**
  - `+INIT:WAKEUP,<type>`：从 DPM/LPM 唤醒，type = `UC`/`NOBCN`/`DEAUTH`/`EXT`/`RTC`

### 2.12 `AT+WFDIS` — STA profile 启用开关（排查点）
- **[M]** p.58：
  ```
  AT+WFDIS=<disabled>   <disabled>: 1 (Unused), 0 (Used)
  AT+WFDIS              →  +WFDIS:<disabled>
  AT+WFDIS=?            →  +WFDIS:<disabled>
  Note: The "unused" flag is stored in the NVRAM.
  若设为 1，DA16200 重启时不会去连已配置的 AP。
  ```
- **存 NVRAM、跨重启生效**——是"莫名其妙不连"的高嫌疑项，见第 4 节。

### 2.13 `AT+WFRSSI` — 查信号强度（同时也是"是否真连上"的独立判据）
- **[M]** p.49：
  ```
  AT+WFRSSI            →  +RSSI:-41   + OK
  AT+WFRSSI=<timeout>  →  +RSSI:-25   + OK     <timeout> 单位秒
  未连接 AP 时:
  AT+WFRSSI            →  +RSSI:NOT_CONN   + ERROR:-400
  超时无结果:
                         ERROR:-801
  Prerequisite: The DA16200/DA16600 should be connected to AP.
  ```

---

## 3. 常用 Wi-Fi 相关错误码

**[M] Appendix I, Table 58（p.162）——本机用的是这张表的权威版本**

| 值 | 宏 | 含义 |
| --- | --- | --- |
| 0 | `AT_CMD_ERR_CMD_OK` | OK，无错误 |
| -1 | `AT_CMD_ERR_UNKNOWN_CMD` | **未知命令**（命令不存在） |
| -2 | `AT_CMD_ERR_INSUFFICENT_ARGS` | 参数不足 |
| -3 | `AT_CMD_ERR_TOO_MANY_ARGS` | 参数过多（例：`AT+WFSTAT=?`） |
| -4 | `AT_CMD_ERR_WRONG_ARGUMENTS` | 参数值错误 |
| **-5** | `AT_CMD_ERR_NOT_SUPPORTED` | **功能不支持** |
| **-6** | `AT_CMD_ERR_NOT_CONNECTED` | **未连接到 AP** |
| -7 | `AT_CMD_ERR_NO_RESULT` | 无结果 / 未配置 |
| -8 | `AT_CMD_ERR_TOO_LONG_RESULT` | 响应缓冲区溢出 |
| -9 | `AT_CMD_ERR_INSUFFICENT_CONFIG` | 功能未配置 |
| -10 | `AT_CMD_ERR_TIMEOUT` | 命令超时 |
| -11 | `AT_CMD_ERR_NVR_WRITE` | NVRAM 写失败 |
| -12 | `AT_CMD_ERR_RTM_WRITE` | Retention RAM 写失败 |
| -13 | `AT_CMD_ERR_SYS_BUSY` | 系统忙 |
| -14 | `AT_CMD_ERR_MEM_ALLOC` | 内存分配失败 |
| -20 | `AT_CMD_ERR_DATA_TX` | 数据发送失败 |
| -22 | `AT_CMD_ERR_IP_ADDRESS` | 获取 IP 失败 |
| -100 | `AT_CMD_ERR_COMMON_SYS_MODE` | **系统运行模式错误**（例：STA 模式命令在 SoftAP 下执行） |
| -110/-111 | | 参数类型错误 |

**Wi-Fi 专属错误码（散见各命令 Note）**

| 值 | 触发条件 | 出处 |
| --- | --- | --- |
| -113 | 国家码无效（`AT+WFCC`） | [M] p.49 |
| -400 | `AT+WFRSSI` 时未连接 AP | [M] p.49 |
| -425 | `AT+WFJAPA` 找不到 SSID | [M] p.56 |
| -460 | `AT+WFCAP` 已经连着 | [M] p.56 |
| -503 | `AT+WFCAP` NVRAM 里没有 AP profile | [M] p.56 |
| -801 | `AT+WFRSSI` 超时 | [M] p.49 |

> **特别提醒（避免误判）：**
> - `ERROR:-5` **不是密码错**，密码/参数值错是 `-4`。
> - `ERROR:-5` **不是没连上 AP**，没连上是 `-6`。
> - `ERROR:-5` **不是模式错**，STA 命令在 SoftAP 下跑是 `-100`。
> - `ERROR:-5` **不是没 profile**，那是 `-503` / 实测 `-7`。

---

## 4. ⚠️ 当前固件 `ERROR:-5` —— 未定论，按下面决策树排查

### 4.1 已确认的事实

| 事实 | 证据 |
| --- | --- |
| `AT+WFJAPA=CMCC-xxxx,<PASSWORD>` **在本机成功过** | [L] `AUTO.txt` L409 附近：`+WFDAP:0,DEAUTH` / `OK` / `+WFJAP:1,'CMCC-xxxx',192.168.1.6` |
| profile 写入 NVRAM，**断电重启能自动重连** | [L] `AUTOB.txt` L41-45 `VERDICT: boot auto-reconnect = YES` |
| `AT+NWIP=?`、`AT+NWHTC` 当时也都成功 | [L] `AUTO.txt`：`+NWIP:0,192.168.1.6,...`、`HTTP/1.1 200 OK` + `+NWHTCSTATUS:0` |
| **现在** `AT+WFSTA` / `AT+WFCAP` / `AT+WFJAPA` 全部 `ERROR:-5` | [L2] `serial_capture.log` L3-4、L9-10、L14-15 |
| **现在** `AT+TIME` 仍然正常 | [L2] L58-60：`+TIME:1970-01-01,08:36:21` + `OK` |
| 当前日志里**从未出现 `+INIT:DONE`** | [L2] 全文检索无此行（对照 [L] `AUTOB.txt` L37 有） |

**⇒ AT 引擎和 UART 是通的（`AT+TIME` 能回），但 Wi-Fi 族命令整体被判定为"功能不支持"。根因尚未确定——不猜。**

### 4.2 排查决策树（按顺序发，第 3 步就能分流）

```
①  AT
    → 无 OK：链路/引擎问题，停

②  AT+
    → 表里有没有 AT+WFJAPA / AT+WFSTA？
       没有 = 固件根本没编译进去（换固件/找厂商）
       有   = 继续

③  AT+WFSCAN
    ├─ 能列出 AP（含 CMCC-xxxx）→ 射频和 Wi-Fi 子系统是好的，
    │                              问题出在"连接"这一步 → 走 ④
    └─ 也是 ERROR:-5            → Wi-Fi 子系统整体不可用 → 走 ⑤⑥⑦

④  只有连接族失败：
    AT+WFMODE           看是不是 0
    AT+WFDIS=?          +WFDIS:1 = profile 被停用（NVRAM）→ AT+WFDIS=0 恢复
    AT+WFSTAT           看 wpa_state= / 是 sta0 还是 softap1

⑤  AT+WFMODE
    ├─ +WFMODE:1  → 处于 Soft-AP 模式 → AT+WFMODE=0 后 AT+RESTART 复测
    ├─ +WFMODE:0  → 模式正常，继续 ⑥
    └─ 也是 -5    → 继续 ⑥

⑥  AT+WFDIS=?        +WFDIS:1 → AT+WFDIS=0 （存 NVRAM，之后 AT+RESTART）

⑦  AT+RESTART        观察有没有 +INIT:DONE,0
    ├─ 有 +INIT:DONE,0 且随后自发 +WFJAP:1 → 已恢复，问题是一次性状态
    └─ 有 +INIT:DONE,0 但 WF 命令仍 -5      → 状态标志类问题（④⑥ 已覆盖）
                                              若仍无解，把 ②③⑤⑥ 的输出回贴
```

### 4.3 三条可以立刻做的对照实验

| 实验 | 命令 | 期望 | 说明 |
| --- | --- | --- | --- |
| A | `AT+WFSCAN` | 能列出 `CMCC-xxxx -46` | 分流：射频是否活着 |
| B | `AT+WFDIS=?` | `+WFDIS:0` | `+WFDIS:1` 即根因，存 NVRAM |
| C | `AT+WFMODE` | `+WFMODE:0` | `+WFMODE:1` 即处于热点模式 |

> **不要**再纠结 `-5` 是不是密码错——`[M]` Table 58 已明确 `-5=Unsupported function`、`-4=Wrong parameter value`。

---

## 5. 参考：本工程相关配置

| 项 | 值 | 出处 |
| --- | --- | --- |
| SSID | `CMCC-xxxx` | `src/weather_cfg.h` |
| 密码 | `<PASSWORD>` | `src/weather_cfg.h` |
| 扫描到的 BSSID | `xx:xx:xx:xx:xx:xx`，信道 2427(MHz/ch14)，`-46 dBm` | [L] `AUTO.txt` |
| 连接后 IP | `192.168.1.6` | [L] `AUTO.txt` / `AUTOB.txt` |
