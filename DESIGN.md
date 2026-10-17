# 4.weather_app — RA4M2 + DA16200 天气显示站 实现思路

> 状态：**RA4M2 + DA16200 天气主链路已完成板级验证；主栈扩展至 4 KB 后实测完成 SNTP、HTTP 200、JSON 解析和持续心跳**
> 参考实现：GD32VW553 版 `..\..\..\GD32\workspace\GD32VW553\4.weather_app`
> 基础工程：本仓库 RA4M2 + DA16200 工程（USB↔WIFI 透传桥）
>
> **密钥管理**：心知 API Key 和个人 STA 测试凭据只放在 git-ignored `src/weather_secrets.h`，模板为 `src/weather_secrets.example.h`。固件二进制仍可被逆向提取编译进去的 Key；对外发布时应采用服务器代理或单独可撤销的发布 Key。

## 1. 目标

在 RA4M2 + DA16200 组合上复刻 GD32 版天气站功能：

- DA16200 联网 → SNTP 对时 → 定时拉取心知天气 API → 解析 → 显示
- OLED 主页显示天气与实时指标、今日温度范围和时间，页面切换（对齐 GD32 布局）；OLED 未连接时由串口逐行模拟
- 4 按键交互（2 机械 + 2 触摸，映射见 §6）
- **SoftAP 网页配网**（必须，见 §8 方案）+ 凭据 NVRAM 持久化

## 2. 两平台架构差异（方案立足点）

| 维度 | GD32VW553 参考版 | RA4M2 + DA16200 本工程 |
|---|---|---|
| 网络 | 片上 WiFi + lwip socket，代码直接 `getaddrinfo/recv` | **无网络能力**，全部经 AT 命令驱动 DA16200 |
| HTTP | `weather_http.c` 裸 socket 收包 | `AT+NWHTC=<url>,get`，payload 由模块回传 |
| SNTP | lwip `sntp_start_once()` + 时间钩子 | `AT+NWSNTP=1,ntp.aliyun.com,86400`；等待 8 秒后用 `AT+TIME` 验证 |
| 解析 | SDK cJSON | **移植单文件 cJSON**（MIT） |
| OS | FreeRTOS 多任务 | **裸机 while(1) 状态机**（沿用桥工程 10ms 节拍，不开 RTOS） |
| 凭据存储 | MCU Flash `wifi_credential_t` | Wi-Fi SSID/密码/城市从 **RA4M2 Data Flash 双槽记录**加载；DA16200 NVRAM profile 仅作为模组连接机制 |
| 显示/按键 | 软 SPI OLED + GPIO 按键 | 相同方案，**接线已确认**（OLED=`RA4M2OLED` 示例；按键 2 机械 P005/P006 + 2 触摸 CTSU） |

**核心结论：本工程的主要新增工作是把"透传桥"升级为"程序化 AT 命令引擎"**，其余模块（JSON 解析、天气文案、UI、按键）从 GD32 版直接移植。

## 3. 总体软件架构

```
┌─────────────────────────────────────────────┐
│ weather_task    主状态机（连网/对时/拉天气/刷新/按键/渲染） │
├──────────┬──────────┬───────────┬───────────┤
│weather_http│weather_parse│weather_text│weather_ui │
│ (构造URL → │ (cJSON移植)  │(39条code表,│ (OLED页面, │
│  AT+NWHTC) │             │ 直接复用)  │  移植)    │
├──────────┴──────────┴───────────┴───────────┤
│ at_engine    AT命令引擎（新增核心）：发送/响应采集/OK·ERROR·URC识别/超时 │
├─────────────────────────────────────────────┤
│ UART驱动（复用桥工程：环形缓冲 + 行组装 + TX完成标志）        │
├─────────────────────────────────────────────┤
│ DA16200 (PMOD2)                             │
└─────────────────────────────────────────────┘
     外设: OLED(复用RA4M2OLED示例) + 4按键(2机械+2触摸)   ← 接线已确认
```

## 4. AT 引擎设计（本工程核心新增）

基于桥工程已验证的机制（ISR 环形缓冲 → 主循环行组装），在其上加一层命令引擎：

```c
/* 实际接口（src/at_engine.h） */
at_result_t at_engine_start(cmd, timeout);                        /* AT_MODE_CMD */
at_result_t at_engine_start_ex(cmd, timeout, at_mode_t mode);    /* mode 见下 */
at_result_t at_engine_poll(void);       /* 每 tick 调用，非阻塞 */
```

- **发送**：`at_engine_start()` 只提交发送不等待；`at_engine_poll()` 在 `ST_SENDING`
  等 `wifi_uart_send_complete_flag`，1s 未完成判超时。**主循环阻塞 ≤ 1 tick（10ms）**。
- **接收**：主循环逐字节调 `at_engine_rx(c)`，引擎做两件事：
  - 原样采集到 `at_resp_t.buf`（4KB，`overflow` 标志截断）
  - 行级识别终止行与 URC
- **两种完成模式（`at_mode_t`）——这是 DA16200 的关键差异，2026-10-03 实测确定**：

  | 模式 | 终止条件 | 适用 |
  |---|---|---|
  | `AT_MODE_CMD` | `OK` / `ERROR:-x` | 绝大多数命令 |
  | `AT_MODE_PAYLOAD` | 末尾 `+NWHTCSTATUS:<n>` | **仅 `AT+NWHTC`** |

  NWHTC 发完命令只回一个 6 字节裸 `OK`（同步回执），正文随后**异步流式推送**，
  真正的结束是末尾 `+NWHTCSTATUS:<n>`。把 `OK` 当结束只会拿到 6 字节。
- **终止行竞态（2026-10-03 修复的真实 bug）**：主循环 10ms 才 poll 一次，
  而 5 字节命令 ~430µs 发完、模块 ~1ms 就回 `OK`。
  若只在 `ST_WAIT` 认终止行，`OK` 会在 `ST_SENDING` 期间到达而被**丢弃** → 必然超时。
  修法：任何非 IDLE 状态收到终止行都记 `term_seen`，由 `poll()` 统一收口。
- **URC 两条通道**（DESIGN §8.1 根因1 对策：禁止脉冲标志承载状态迁移）：
  - `at_engine_urc_take(prefix)` —— 消费型 4 槽 FIFO，用于"发命令后等结果"
  - `at_engine_urc_seen(prefix)` —— sticky 记录 8 条，**不受 FIFO 深度限制**，
    用于 45s 被动监听判定（URC 超 4 条也不漏判）
- **超时**：命令级超时（联网 30s、NWHTC 25s、HTTP 普通 2.5s）。
- **并发约束**：裸机单飞行——同一时刻只允许一条命令在途；按键/渲染在主循环继续跑。
  NWHTC 尤其**不可并发**（实测三条并发会把后两条的正文冲掉）。
- **命令所有权 `cmd_owner_t`（2026-10-03 修复的真实 bug，必读）**：
  引擎有多个使用方（`weather_task` 自己的时区/SNTP/时间命令，以及 `weather_http` 的
  NWHTC），裸机单飞行意味着**谁发命令谁负责在完成时收结果**。因此每个在途命令必须
  记录归属，由 `weather_task_poll()` 第 3 步统一分发：

  ```c
  at_result_t r = at_engine_poll();
  if (AT_RES_PENDING != r)
  {
      if (CMD_HTTP == s.cmd)  { weather_http_on_engine_done(r); ... }  /* HTTP */
      else if (CMD_NONE != s.cmd) { on_cmd_done(r); }                   /* 自己的 */
  }
  ```

  ⚠ **不能用 `weather_http_busy()` 当分发依据**。踩过的坑：`weather_http` 的 busy 状态
  若因上一次失败/超时被提前清掉，引擎本次 `OK` 就会落到"无人认领"分支被静默丢弃 →
  `fetch_weather_done()` 永不执行 → OLED 一直显示"waiting data.."，但串口能看到完整的
  HTTP 响应。归属必须由 `s.cmd` 显式记录：`fetch_weather_start()` 成功后置 `CMD_HTTP`，
  完成后清回 `CMD_NONE`。
- **诊断日志必须低频（反向教训，2026-10-03）**：USB TX 环只有 4KB 且
  `__io_putchar()` **满则丢、不阻塞**，因此任何高频打印都会把真正的
  `[WIFI]`/`[wx]` 挤掉，表现为"程序卡死"。曾因在 `at_engine_poll()` 里
  每 tick 打印一行收口摘要，600ms 就灌满环，主循环其实完全正常。
  由此定死三条：① 收口日志要判"本次 poll 是否真的迁移了状态"
  （用进入时的 `was_idle` 快照比对，不能只看 `state == IDLE`）；
  ② 诊断一律低频（心跳 30s，`WEATHER_HB_DIAG_MS`）；
  ③ 看引擎状态用 `at_engine_diag()` 快照而不是加打印。详见 doc/README.md §3.4.1。

## 5. 天气链路（对应 GD32 实现）

| 步骤 | GD32 做法 | RA 做法 |
|---|---|---|
| 联网 | `wifi_prov_start()` 阻塞重试3次 | **阶梯式，每级 8s（`WEATHER_CONNECT_TIMEOUT_MS`）**：`AT+NWIP=?` 主动查 IP（最快，免疫一次性 URC）→ 短等 `+WFJAP:1` URC（覆盖 RA 先起/模块后起的冷启动）→ `AT+WFCAP`（用 NVRAM profile）→ `AT+WFJAPA=<ssid>,<pwd>`（显式连 + 写 NVRAM，30s）。WFJAPA 连续失败 3 次回退到第一级重跑 |
| 取 IP | `wifi_get_ip()` | **主动 `AT+NWIP=?` 查询**（首选），从 `+NWIP:0,<ip>,<mask>,<gw>` 取第 2 字段；`+WFJAP:1,'ssid',<ip>` 走 `parse_ip()` 同一套解析（找第一个 `d.d.d.d`）。⚠ 早期版本"只从 URC 取 IP、不发 `AT+NWIP=?`"，因为该查询曾因长响应丢字节超时（风险#5）；风险#5 修好后查询已可靠，且**主动查询是唯一能免疫"URC 一次性、RA 没赶上"的办法**，故改为主路径 |
| 对时 | lwip SNTP 钩子 | **不使用 RA 内部 RTC**（用户已确认）。实测：`AT+TZONE=28800`（**单位确为秒**）+ `AT+NWSNTP=1,ntp.aliyun.com,86400` → 延时 `AT+TIME` 回报真实本地时间（UTC+8）。主页时钟每分钟查一次 `AT+TIME` |
| 拉天气 | 裸 socket GET，4KB 收包缓冲 | `AT+NWHTC=<seniverse now.json>,get` **PAYLOAD 模式**。✅ **裸 URL 即可，不要单引号**（风险#3 已清零：裸/引号结果完全一致 `len=690 body_len=274`）；✅ URL 含 `&`/`?` **不需转义**（URI 是第一个逗号前的单参数）；⚠ 串长必须 < `AT_CMD_MAX`(180) |
| 解析 | cJSON：`results[0].now.text/code/temperature` + `last_update` | **原样移植** `weather_parse.c`。实测 `hdr_sep=1`（**含 HTTP 响应头**）→ 必须先切 `\r\n\r\n`；切完 `json_direct=1`（正文首字符 `{`）→ 可直喂 cJSON |
| 刷新 | 15min：主循环按分钟计数 | 相同，挂在主状态机 tick 上（桥工程已有 10ms 节拍，累计秒/分） |
| 按键 | short=刷新 / double=翻页 / long=配网 | 移植 `key_scan.c` 三态逻辑（20ms 轮询 + 去抖） |

配置项沿用 `weather_cfg.h` 形式（WiFi/KEY/API/城市/刷新周期），去掉软 SPI/Flash 偏移等 GD32 专属项。

## 6. 显示与外设（已有现成资产：`RA4M2_100PIN_example\RA4M2OLED`）

- **OLED 驱动直接复用**：`src/oled/oled.c/h + oledfont.h`（SSD1306 128x32，`OLED_ShowChar/ShowString/ShowBMP` 等 GD32 同款 API）+ `src/oled_port.c`（软 SPI 回调；五个 pin 在 `configuration.xml`/`ra_gen/pin_data.c` 配为 GPIO 输出，并由 `oled_port_init()` 再初始化）+ `src/gpio_spi/`。
  - 软 SPI 接线：CLK=**P001**、DAT=**P000**、RES=**P003**、DC=**P007**、CS=**P014**（7PIN，100 kHz）；物理排针对应需以当前 PCB/排针图核对。
  - 当前已将这五个 pin 补入 RASC 配置；重新生成后应确认仍为 GPIO outputs。实体 OLED 是否响应仍须板上测量确认。
  - ~~RTC~~：**用户已确认不需要**；主页显示 IP 定位城市、实时天气/温度/湿度/风速、今日高低温和时间，WiFi 页显示 SSID/IP/状态。当前 `WEATHER_UI_SERIAL_SIM=0`，启动时会调用 `oled_port_init()` 初始化实体 OLED；设为 `1` 可切换到四行串口模拟。时间来源见 §5 对时行
- **按键：板上共 4 个（2 机械 + 2 触摸）**
  - 2 个机械键：`RA4M2GPIOINPUT` 示例读 **P005/P006**（极性/上拉实现时按原理图或实测确认）
  - 2 个触摸键：`RA4M2TOUCH` 示例使用 FSP CTSU + RM_TOUCH 中间件，电极为 TSCAP=P207、TS11=P415、TS12=P708；`button_status` bit0/bit1 对应两键
  - **按键映射（已确认 4 键全接入，建议如下，可微调）**：

    | 按键 | 功能 |
    |---|---|
    | 机械键1 | 短按=立即刷新 / 双击=翻页 / 长按3s=进入配网（GD32 三态） |
    | 机械键2 | 单击=翻页 |
    | 触摸键1 | 单击=立即刷新 |
    | 触摸键2 | 单击=翻页；长按=进入配网（备用） |
  - 触摸键接入注意：需把 `RA4M2TOUCH` 的 CTSU 外设配置合并进本工程 configuration.xml（`hal_data`/`pin_data` 同步），此一步**可能需要 e2studio 打开生成一次**——阶段3 先评估"手工合并 vs e2studio 一次配置"，评估结论回报后再动
- 板载 LED：P044/P045/P002（可作状态指示）

### 6.1 按键刷新与未联网行为

机械键 P005 短按请求天气刷新、双击翻页、长按 3 秒为配网入口；P006 单击翻页。
扫描和事件消费每个主循环 tick 都执行，不依赖 Wi-Fi 已连接。短按刷新在未联网或 AT
命令忙时排队，待联网且命令空闲后执行，不静默丢弃。GD32 参考版同样先设置 refresh
请求标志，再由网络任务在连接后消费，因此启动/断网期间按键仍应有效。
若连接状态已进入终态 FAIL，P005 短按会重启 Wi-Fi 阶梯，并在成功联网后执行刷新。

天气 `now.json` 的湿度/风速可能缺失；解析器保留 -1，由 `daily.json` 今日
humidity/wind_speed 字段补齐（只覆盖实际返回的非负值）。UI 的 `--` 表示两份
响应均未提供该指标，不代表 JSON 或天气请求失败。手动刷新在 Wi-Fi 未连接或 AT
命令忙时排队，成功投递后才清除 pending 标志。

触摸键 TS11/TS12 不能按 GPIO 读。P207/P415/P708 pin mux 已配置，但当前天气工程尚未
启用 CTSU + `rm_touch` middleware、IRQ、实例及扫描状态机，故触摸目前没有按键事件。
本机只找到 FSP 4.0 的 `RA4M2TOUCH` 示例；当前工程使用 FSP 6.6，而对应
`r_ctsu`/`rm_touch` 源码未安装在 workspace/toolchain 目录。接入前需在 RASC 6.6 中添加
CTSU/RM_TOUCH，生成 FSP 6.6 实例及中断，再将触摸边沿（两个电极状态 bit0/bit1）转换成
与机械键相同的 `key_event` FIFO。扫描必须非阻塞，不能在主循环同步忙等 CTSU 完成。

### 6.2 板载 LED 状态定义

| LED | MCU 引脚 | 含义 | 表现 |
|---|---|---|---|
| LED1 | P045 | Wi-Fi 状态 | 连接中按约 200 ms 节拍闪烁；连接成功常亮；失败/配网状态熄灭 |
| LED2 | P044 | 天气状态 | HTTP 天气请求在途时按约 200 ms 节拍闪烁；成功取到天气后常亮；尚无有效天气时熄灭 |
| LED3 | P002 | 慢速呼吸/活动提示 | Wi-Fi 连接或天气请求期间熄灭；系统空闲时约 2 秒一个呼吸周期 |

三灯高电平点亮，由 `hal_entry.c` 在 `R_IOPORT_Open()` 后动态配置，不在当前
`ra_gen/pin_data.c` 的 FSP 引脚表中。LED2 仅表示数据是否有效，不区分请求失败和
尚未请求；失败原因以 `[hb] res=` 为准。LED3 的软件呼吸 PWM 只在人眼可辨的慢速
节拍运行，不用作精密时基。网络一旦曾连上，断线重连期间 LED1 会熄灭，重新进入
CONNECTING 后闪烁。

### 6.3 主栈配置与已知 HardFault 根因

RA4M2 的裸机主栈由 `configuration.xml` 的 `config.bsp.common.main` 配置；当前为
`0x1000`（4 KB），RASC 生成值在 `ra_cfg/fsp_cfg/bsp/bsp_cfg.h`。不要只手改生成头文件。

故障实测（2026-10-14）：1 KB 栈版本保存的 HardFault 现场为 `CFSR=0x00100000`
(INVSTATE)、`HFSR=0x40000000`、`stage=6`；堆栈 PC=`0x20002292` 落在 map 中
`g_main_stack` 的 `0x200021e0..0x200025e0` 区间，且当时位于天气 UI 刷新路径。
扩至 4 KB 后，板级日志成功完成 `AT+TIME`、now/daily HTTP 200、JSON 解析并持续输出
心跳（loop 计数增长）。这是栈/返回地址破坏的强证据，4 KB 修复已实测有效；若将来
增加深层调用或大型局部变量，仍需重新检查栈余量。

## 7. 实施阶段（每阶段独立可验收）

### 阶段0：工程创建
- 复制 `4.weather_app` → `4.weather_app`，全量替换工程名（复用上次重命名的流程），更新 doc/README
- **验收**：命令行编译 0 错误

### 阶段1：AT 引擎 + 链路实测（无新硬件依赖）✅ 已完成
- 实现 `at_engine`（命令/响应/URC/超时/payload 采集）
- 实测确认 §9 风险 #1~#5，固化 NWHTC 响应格式解析
- 串口命令仍可手工输入（保留透传调试模式）
- **验收**：`AUTO` 13 步全通（`AT:OK LIST:OK SCAN:OK CONN:OK IP:OK SYNTAX:OK
  TIME:ER TZSET:OK SNTP:OK TIME2:OK HTTP1:OK HTTP2:OK HTTP3:OK`），
  拿到真实天气（`temperature:"20"`），风险 #1/#2/#3/#4/#5 全部清零
- **遗留**：`AT+DATE` 返回 `ERROR:-1`（确认不存在，非缺陷）；
  `AT+DEFAP=?` / `AT+WFSTAT=?` 均 `ERROR:-3`（查询形式不是 `=?`，阶段4 前需 `HELP` 查）

### 阶段1.5：OLED 显示链路验证（2026-10-03 插入）✅ 已完成
> 插入原因：阶段3 才做 OLED，但**坐标约定和字库编码必须提前验证**，
> 否则阶段3 返工。范围严格限定为"移植 + 自检"，不做正式 UI 接线。

- 移植 `RA4M2OLED` 示例的 `oled_port.c/.h` + `gpio_spi/` + `bsp_hal/hal_delay.c` + `oled/`
  （**全部原生搬运，不改引脚与时序**）。接线 CLK=P001 DAT=P000 RES=P003 DC=P007 CS=P014
- 原样移植 GD32 版 `weather_ui.c/.h`（4 页面布局与显示内容不变）
- 新增 `oled_font.c/.h`：UTF-8 ⇄ GBK 转换 + 字库查表（API 返回 UTF-8，字库按 GBK 索引）
- 新增 `tools/glyph2c.py`：从 TTF 生成汉字点阵，内置天气站字符集
- **验收**：`AUTO` 之外，上电自检渲染 boot/主页/WiFi页三个真实页面且 OLED 无异常显示

**⚠ 关键约定（OLED 坐标，极易搞错）**

```c
OLED_Set_Pos(x, y):  0xb0 + y     // y 是【页号 0~3】，不是像素纵坐标！
```

| 参数 | 含义 |
|---|---|
| `x` | 列 0~127 |
| `y` | **页号 0~3**（每页 8 像素），只有 4 页 |
| `sizey` | **只能是 8 或 16**。8→`asc2_0806` 6px宽占1页 `x+=6`；16→`asc2_1608` 8px宽占2页 `x+=8` |
| 其它值 | `OLED_ShowChar` 直接 `return`，**什么都不显示且不报错**（曾因此误判） |

128x32 视觉排布：`page0~1` = 第一行（16px），`page2~3` = 第二行（16px）。

**⚠ 字库编码链**

```
心知 API ──UTF-8──> cJSON ──code查表──> 英文文案(ASCII) ──> OLED
                    (不比较中文字符串)
```
`oledfont.h` 的 `Hzk[]` 按 GB2312 顺序索引，故若要上中文需 `oled_utf8_to_gbk()` 转码。
**一律用 weather code 查表**，不要拿 `now.text` 做 `strcmp`（编码不同必然失配）。

### 阶段2：天气链路（串口 OLED 模拟）← 当前进行中
- 联网状态机（IDLE/CONNECTING/CONNECTED/FAIL，对齐 GD32 五态简化）
- 当前阶段固定 WiFi 凭据；天气位置使用 `ip`，城市由服务端按来源 IP 定位。SoftAP 模式切换和网页配置留在阶段4
- SNTP 对时 + 时间获取；顺序请求 `now.json` 与 `daily.json`，分别解析实况和今日高低温，天气文案按 code 映射
- 串口按 `[OLED] PAGE=...` 与 `L0`–`L3` 输出主页/WiFi 信息页；每行限制为 21 个 ASCII 字符，对应 128×32 OLED 小字体宽度。模拟模式跳过 OLED 引脚初始化
- 15 分钟定时刷新、按键单击触发刷新
- **at_engine 切全异步 + 按键事件 FIFO**（§4 硬性要求），验证连接中/失败中按键立即响应（§8.1 根因2 的修复点）
- **验收**：串口逐行显示完整主页和 WiFi 信息页；daily 请求失败时仍显示有效实况；单击立即刷新；**连接失败阻塞期间长按/单击 10/10 次即时响应**。当前尚未编译或上板验证

#### 阶段2 设计要点（2026-10-03 定稿）

**与 GD32 版的任务结构差异**

| 项 | GD32 版 | RA 版 |
|---|---|---|
| 调度 | FreeRTOS 任务 + `sys_ms_sleep` | **裸机 10ms tick**，挂在 `hal_entry` 主循环 |
| 联网 | `wifi_prov_start()` 阻塞 | **被动等 `+WFJAP:1` URC**（实测上电自发，§5） |
| SNTP | lwip `sntp_start_once()` + 时间钩子 | `AT+NWSNTP=1,<server>,86400`；读时用 `AT+TIME` |
| 时间源 | 内部 `s_epoch_base` + `sys_time_get()` | **每分钟查一次 `AT+TIME`**（DA16200 持有 RTC） |
| HTTP | `weather_http_get()` 裸 socket | `AT+NWHTC=<url>,get` **PAYLOAD 模式** |
| 按键 | `key_scan.c` 读 GD32 GPIO | 移植逻辑，GPIO 读法换成 `R_IOPORT_PinRead` |

**新增模块**

| 文件 | 职责 |
|---|---|
| `src/weather_task.c/.h` | 主状态机（裸机，非阻塞）。每 tick 调用 `weather_task_poll()` |
| `src/weather_http.c/.h` | NWHTC PAYLOAD 模式封装：发命令→等 `+NWHTCSTATUS`→切 `\r\n\r\n`→交出 body |
| `src/key_scan.c/.h` | 移植 GD32 三态逻辑（short/double/long），GPIO 换 `R_IOPORT_PinRead` |
| `src/cJSON.c/.h` | MIT 许可，从 GD32 SDK 拷贝（`MSDK/util/`） |

**上电自检序列（2026-10-03 新增，用户要求「每次开机先用 AT 命令测试模块，再走后续」）**

上电后不再"什么都不发、纯等自发 URC"，而是**先用一条裸 `AT` 确认模块活着**：

```
上电 ──> at_send_owned("AT", 2000ms, CMD_AT)   # 自检
      ──> [wx] AT self-test: OK, module is alive
      ──> 被动等 +WFJAP:1 URC  ──> 取 IP / TZONE / SNTP / 首次拉天气
```

| 设计点 | 原因 |
|---|---|
| 自检归属 `CMD_AT` | 引擎只有一个轮询点，结果必须经 `on_cmd_done()` 回来（§4 命令所有权） |
| 自检失败**不致命** | 只打印 `TIMEOUT`/`ERROR` 后继续等 `+WFJAP`，避免模块冷启动慢就被判死 |
| 自检与 URC 监听**并行不冲突** | 引擎非 IDLE 期间照样收 URC（`urc_push` 不看状态），所以自检在途时 `+WFJAP` 不会丢 |
| 仍保留 35s `AT+WFCAP` 兜底 | 自检只验"模块活着"，验不了"连上 AP 了" |

**⚠ 与 GD32 版的三个关键差异（踩过坑）**

1. **时间不能本地算** —— GD32 有 `s_epoch_base` + `gmtime()`，RA 没有 RTC，
   只能查 `AT+TIME`（每分钟 1 次，`AT+TIME` 已在实测中确认存在）。
   未同步时显示 `--:--`。
2. **命令串不能超 `AT_CMD_MAX`(180)** —— 心知 URL 含 key 实测 113 字节，
   加上 `AT+NWHTC=` 和 `,get` 仍在限内，但**若加引号/加长参数会溢出**，
   故 `weather_http` 内部用 256 字节缓冲并在 `at_engine_start` 处做长度校验。
3. **NWHTC 期间不能发别的命令** —— 单飞行约束（§4）。
   所以时间查询必须排在天气请求**之后**，不能并行。

**按键映射（阶段2 只接 1 个机械键，触摸键留阶段3）**

| 事件 | 功能 |
|---|---|
| 短按 | 立即刷新天气 |
| 双击 | 翻页（3 页循环：主页 → WiFi 信息页 → 温度预报页；2026-10-17 由双页改为 3 页） |
| 长按 3s | 进入/退出配网（阶段4 实现） |

### 阶段3：OLED + 按键（4 键全接入）
- 复用 `RA4M2OLED` 示例的 oled/oled_port/gpio_spi（运行时配引脚，**无需 FSP 配置器操作**），移植 GD32 `weather_ui.c` 页面并在评审后按 RA 需求改版（见下"2026-10-17 界面改版记录"）
- 2 机械键按 §6 映射接入；2 触摸键拷贝 `RA4M2TOUCH` 的 CTSU 配置（先评估合并方式，见 §6 注意事项）
- **验收**：行为与 GD32 版 README 的 UI 布局一致，4 键映射表全部生效

**2026-10-17 界面改版记录（板测反馈 + 用户新需求）**
- **主页**：第 0 行（16px）= 城市(API `location.name` > 配网城市 > IP) + 中文天气字 + 当前温度 + ℃ 度符；第 2–3 行合并为一行 **16px 大行「风速 xx 湿度 xx」**（新增 16px 速/湿/度字模，SimSun 光栅）；原「今日 lo/hi」8px 行**删除**（与顶部温度重复），串口仍打 `[oled] today(hid)=...` 便于核实高低温数据。
- **配网页（第 0 页）**：标题「WiFi配网」16px + 右半闪烁 `*`；第 2 行固定显示 **`AP:leeWethWifi`**（手机要连的热点名，不随保存的家里 SSID 变）；第 3 行 `192.168.1.100:8080`。信息行只在第 2–3 页，避免 6x8 行擦除 16px 标题下沿（GD32 原版缺陷）。
- **联网中/失败页**：第 2 行显示正在连接的 SSID（`+WFJAP:1` URC 里的真实 SSID 会回填 `s.ssid`，新 `parse_ssid()`）；第 3 行显示 **FLASH 中保存的城市**（`s.city`，`[cfg] loaded` 日志会带 `city='...'`）。若 FLASH 记录没存城市会显示 IP——需要重新配网一次把 CITY 填上（portal 表单含 city 字段）。
- **新增预报页（第 2 页）**：8px 标题「预报 Today Hi/Lo」+ 3 行「今/明/后 + low/high + ℃」。数据来自 `daily.json?days=3`，解析层新增 `f_low[3]/f_high[3]/f_count`；**免费 key 上限 3 天**（实测请求 days=4 也只返回今天+未来两天，做不到 4 天）。

**2026-10-17 界面改版记录 · 第二轮（板测反馈修改）**
- **16px 字模管线修正**：主页「风速/湿度」大行动用户仍读成"预湿"——根因是我用 SimSun 原生 16px 位图生成，笔画太细、和 GD32 原有粗壮字模风格不搭。速/湿/度/预/报 五个 16px 字模改为 **32px 抗锯齿光栅 → 2:1 OR 降采样** 重新生成（与 8px 字库同管线），笔画变粗、五个字形已在主机侧逐行解码核对。
- **预报页重画（用户新版式）**：第 0–1 行 = **16px 当前时间**（`HH:MM`，未同步时 `--:--`）；第 2 行（8px）= **当前温度 + ℃ 度符**；第 3 行（8px）= **明 lo/hi + 后 lo/hi**（各自带 ℃）。原「预报 Today Hi/Lo」标题和「今」行删除（"不用提示高低温度"；当天温度即顶部当前温度）。`ui_forecast_view_t` 新增 `time_str/temp_str`，`weather_task` 页 2 分发处填充；串口行改为 `[oled] forecast time=.. temp=.. tom=.. dat=..`。今（JIN）与预/报 8px 字模保留在表中备用。
- **CITY 仍显示 IP 的结论**：代码核查 `provisioning` POST 解析（city 是必填字段）与 `weather_cfg_restore_defaults()`（先 `weather_cfg_load()`、仅全无记录才回默认）均无丢失路径；唯一解释是**当前 FLASH 记录里城市为空**（上次配网时 city 留空，表单允许空值）。处理：重新配网一次并填写 CITY（pinyin，如 `baoding`）；开机日志 `[cfg] loaded RA Data Flash settings (... city='...')` 可直接核实存没存上。

**2026-10-17 界面改版记录 · 第三轮（板测反馈；字库错位实锤修复）**
- **字库错位真根因（次轮"风速读成预湿"的真正原因）**：主机侧确定性解析器比对发现——**GD32 原版 `weather_cn_font` 只有 44 行却对应 45 个枚举槽（xian2 行重复），移植时又丢掉了 XIN 行**，数组比枚举少 1 行。后果：尾部所有字模错一位——板上实际显示 速槽→度、度槽→湿、湿槽→**预**，即「预湿」+ 湿度值 100，与板测描述完全吻合；GD32 界面从不使用 40 号之后的字形所以原版没暴露。修复：恢复丢失的 XIN 行（GD32[40] 原数据）、xian2/shi2 两行按 GD32 原始顺序归位、cheng(43) 用 32px-AA 管线新生成的"城"16px 字模补上、shi3(44) 保持零行，并追加 明16/后16（同一管线）共 **52 行，枚举 52 槽完全对齐**（主机侧已逐槽解码核对）。
- **预报页第三版（用户最终版式）**：第 0–1 行（16px）= **当前天气字 + 当前温度 + ℃ + 时间**（时间右锚 x=88，5 字符 40px）；第 2–3 行（16px）= **明 lo/hi + 后 lo/hi**（16px 明/后字模 + 16px 数值 + 8px 小度符，放不下时整体跳过"后"段避免越界）。再无独立"当前温度"行——当前温度并入第 0 行，与天气字同排（`ui_forecast_view_t` 新增 `wcode`，无数据 -1）；`oled_cn8_show` 随之删除（告警 clean）。若再观察到温度"—"，需抓串口 `[WX]`/`[oled] f0` 行判断是无数据还是解析问题。
- **构建**：make exit=0 无新告警，text 82192 / data 492 / bss 59038。



### 阶段4：SoftAP 网页配网与 RA Data Flash 设置持久化（核心链路有实机日志，完整验收待完成）
- **启动模式收敛**：最新代码在 AT 自检后先执行 `AT+WFTAP` → `AT+WFMODE=0` → `AT+RESTART`，并等待 `+INIT:DONE,0` 才进入联网阶梯；若仍处于 AP 模式会有限重试。配网入口仍按请求切换到 SoftAP。用户最新 RFP 烧写后确认启动模式行为正常。
- **联网状态监测**：CONNECTED 后以 5 秒间隔在 AT 空闲时发送 `AT+WFSTA`，并独立消费 `+WFJAP:0` / `+WFSTA:0` URC；断线时清空旧 IP、回 CONNECTING 并重启阶梯，LED1 随 status 变为闪烁。正常健康轮询不触发 OLED 重绘，只有状态或显示数据变化设置 `ui_dirty`。
- **RA 设置存储实现**：使用 RA4M2 Data Flash 最后 384 字节 `0x08001E80–0x08001FFF`，分为两个 192 字节记录槽，含 magic/version/sequence/SSID/password/city/CRC/commit；启动只读，首次空白不自动擦写；新记录写入非当前槽并读回验证，保留上一有效槽。Data Flash 8 KB、64 字节擦除块、4 字节编程粒度。该地址区尚需确认未被其他固件功能占用，并待实机写入/断电恢复验证。
- **核心网页配网链路已实机验证成功，无需修改 DA16200 SDK 固件。** DA16200 AT firmware 启动 SoftAP/DHCP/TCP server；RA4M2 接收 `+TRDTS`、解析 GET/POST 并用 raw `<ESC>S` 返回网页；POST 后关闭 TCP session、停 SoftAP、切 STA，`+WFJAP:1` 已在 `doc` 留存的最新板测日志中出现。
- **端口实测结论：端口 80 显示 DA16200 自带 LWIP 页面；端口 8080 是本项目 RA-hosted 配网页。** 用户提供的 `33 - 副本.txt` 在 `AT+TRTS=8080` 后记录手机 peer `192.168.1.2` 的 `+TRCTS`、GET `/`、GET `/favicon.ico` 和 POST `/cfg`，之后 `WFJAPA` 返回 `+WFJAP:1`。手机需明确打开 `http://192.168.1.100:8080/`；不要使用 80 端口或依赖自动 captive popup。
- `TRTS` 只提供 TCP socket，不提供 HTTP parser、HTTP 页面或 URI handler；这部分由 RA 实现。UM-WI-003 p.112 的 connected-to-AP prerequisite 与 R01AN7287 SoftAP TCP Server 序列表面存在张力；针对本目标模组/固件，现有板测证据显示 SoftAP 下 `TRTS=8080` 可用，后续以此实际固件日志为准。
- STA 切回和凭据连接已有表单提交实机日志；RA Data Flash 双槽代码已实现且编译通过，但**Flash 写入、重启读回和断电保留尚未实机验证**，不可将阶段4全部验收完成。
- 公开 SDK 镜像的 `httpd_init()` / `httpd_post_*` 路径仅作为不改 AT 固件方案的备选，不是当前首选；本任务无需获取或烧写定制 SDK firmware。
- 工程内 `da16200_tcp_ra4m2/src/dialog_wifi_demo.c` 不是本地 HTTP server：其中 HTML 是注释，实际 demo 在联网后向外部 `api.heclouds.com:80` 发 POST。
- 页面提交后先返回 HTTP 202（已收到，正在连接）；模组按模式切换及重启流程返回 STA，连接成功后才写入并校验 RA Data Flash 记录。当前没有同一 TCP 会话上的最终持久化成功页面回执。
- §8.1 的 GD32 根因对策已落实到状态机；完整硬件健壮性回归仍待进行。
- **剩余验收**：Flash 首次空白、写入/读回、二次更新、断电恢复；错误凭据/重复进退配网健壮性回归；最新启动 STA 收敛代码尚待板测；网页被端口80的 LWIP 页面响应不算 RA 页面验收。端口8080 GET/POST 和此前 STA 回连已有实机日志。

## 8. SoftAP 配网方案（AT 固件 TCP Server 路径；网页闭环已板测）

采用已有 DA16200 AT 固件的 `TRTS` TCP Server（不是 `NWHTS`，也不需要 SDK 改固件）；HTTP 由 RA 实现。目标模组的 SoftAP TCP/RA HTTP 闭环已实机通过。端口 80 为 DA16200 LWIP 页面，配网页固定端口 8080，须手动访问 `http://192.168.1.100:8080/`。

```
长按进配网（RA 状态机 → SoftAP profile + AP 模式 + DHCP）
  ① 按 UM-WI-003 p.49-50 写入 `AT+WFSAP` profile
  ② STA 模式配置后按手册重启，并等待 `+INIT:DONE,1`
  ③ 配置 AP 口 `AT+NWIP=1,...` 与 `AT+NWDHR` / `AT+NWDHS`
  ④ `AT+TCPDATAMODE=1` 配置 hex 收包，`AT+TRTS=8080` 开 TCP server
     等待 `+TRCTS:0,<ip>,<port>`；按 `+TRDTS` 长度解码请求，用 raw `<ESC>S` 回复
手机连配网 AP → 浏览器访问 `http://192.168.1.100:8080/` → RA 返回表单并处理 POST
手机连配网 AP → 配网页提交 STA 凭据/城市 → 回复成功后停 SoftAP
  ⑤ 配置 STA 模式并重启 → 等 `+INIT:DONE,0`
  ⑥ `AT+WFJAPA=<ssid>,<pwd>` → 等 OK 与 `+WFJAP:1` → 回联网状态
```

- 目标页面直连 IP、字段按 GD32 页面 (`tools/portal.html`)：SSID / password / city；但页面只被手机看到、或看到 lwIP 默认欢迎页，不代表配置门户已接通
- RA HTML、表单解析和 HTTP parser 已有端口8080 GET/POST 实机日志验证；仍需回归 `+TRDTS`/`+TRCTS`/`+TRXTS` 和 TCP send 命令 framing/URC 顺序的异常及重复进出场景
- 开机先强制 DA16200 进入 STA 并等待 `+INIT:DONE,0`，之后才按 Data Flash 中的凭据启动联网阶梯；连接失败 3 次 → 长按进配网

### 8.1 GD32 版"进配网不生效"根因分析与 RA 侧对策（必做项）

GD32 版在连接失败等状态下长按切配网有概率不生效，源码分析出 5 个根因（行号为 GD32 工程）。RA 侧设计逐条对策：

| # | GD32 根因（证据） | RA 侧对策（设计约束） |
|---|---|---|
| 1 | **完成标志漏检**（主因）：`g_prov_sta_connected` 脉冲标志只存在几~几十 ms（wifi_softap_provisioning.c:147/182），主循环 200ms+OLED 软SPI 刷新才 poll 一次（weather_task.c:185/229）→ 错过后状态卡 PROVISIONING，下次长按被当成"取消"（weather_task.c:170），表象=进配网不生效 | **禁止脉冲标志承载状态迁移**：配网完成/阶段完成信号一律"事件队列（FIFO，消费即清）或可回读查询（如 `AT+WFLCST` 查已连站点）"，状态机只认队列/查询结果 |
| 2 | **阻塞期按键丢失/退化**（主因）：开机连接最长阻塞 ~110s、`fetch_weather` 的 DNS 无超时（weather_task.c:154/192/95），期间 key_poll 不跑，长按计时锚点被重置（key_scan.c:70-73），松手退化成短按还误触发刷新 | **at_engine 全异步 + 按键事件 FIFO（≥4 槽）**：主循环任何阻塞 ≤1 tick（见 §4）；长按在连接中/连接失败/拉天气中任意状态均可响应；按键事件入队不丢 |
| 3 | **SoftAP 启动静默失败**：disconnect 返回值不查、固定 sleep 500ms 代替握手（wifi_prov.c:245-246）、ap_start 返回值只打印（wifi_softap_provisioning.c:121-122）→ 状态=PROVISIONING 但 AP 没起，等一个永远不来的完成信号 | **§8 配网序列每步闭环**：每条 AT 检查 OK/ERROR，关键步骤回读校验（`AT+WFMODE=?`、`AT+WFSAP=?`），全部就绪才置 PROVISIONING 状态；任一步失败→回滚+错误码上 UI+有限重试 |
| 4 | **cancel 自动重连抢语义**：cancel 有凭据即自动 `wifi_prov_start()` 回连（wifi_prov.c:260-261），一次长按=关 AP+回连，而 UI 仍显示配网页 | **进/退配网状态转换显式化**：进配网前 `AT+WFQAP` 断 STA + 清在途命令队列（等 OK 闭环）；cancel 按当前有无凭据明确落到 CONNECTING 或 FAIL，**状态与 UI 同一 tick 刷新**；配网期间模块已重启为 SoftAP 模式，防后台自动回连抢模式 |
| 5 | **UI 反馈滞后/假反馈**：`enter_provisioning` 阻塞最多 10.5s 才刷 UI（weather_task.c:175），失败路径仍显示 "AP on"（weather_ui.c:121） | **PROVISIONING 页面显示子阶段**（切AP→DHCP→TCP→就绪），"连接此热点"提示只在 TCP Server 就绪确认后出现；失败显示具体步骤+错误码，做到"界面所见=模块真实状态" |

**对应验收（并入阶段4）**：
- 连接失败 / 拉天气超时中 → 长按进配网：连续 10 次全部生效（GD32 是概率性失败）
- 配网完成信号不漏检（连续 10 次完成都能自动回连）
- 长按中途状态切换（进→cancel→再进）语义与 UI 三者一致
- 任何 AT 步骤人为失败（如改错 SSID）→ UI 显示错误且状态机不死锁

## 9. 风险与调研清单

阶段1必须清零（#1~#5）：

| # | 事项 | 状态 / 如何确认 |
|---|---|---|
| 1 | **`AT+NWHTC` 响应回传格式**：payload 前是否有 HTTP 头、以什么标志结束（决定采集状态机） | **✅ 已清零（2026-10-03 实测，与原假设不同）**。`HELP` 自报 `AT+NWHTC=<uri>,<option>(,<msg>)`，option ∈ get\|post\|put。真实时序：① 发命令 → **立刻回一个裸 `OK`（仅 6 字节同步回执）** → ② **异步流式推送完整 HTTP 响应，含响应头**（实测有 `Set-Cookie`/`Vary`/`Connection: close`）→ ③ 正文 → ④ 末尾 `+NWHTCSTATUS:<n>`（`0`=成功）才是**真正结束标志**。<br>结论：① 结束标志**不是 `OK`**（把 `OK` 当结束只拿到 6 字节）；② **必须先切 `\r\n\r\n`** 再交 cJSON；③ 缓冲要够 —— 百度首页实测 **667KB / 推 60s**，4KB 只够心知这类小 JSON；④ **同一时刻只能一条 NWHTC 在途**。已实现为 `at_engine` 的 `AT_MODE_PAYLOAD` |
| 2 | **读取 DA16200 当前时间的 AT 命令** | **✅ 已清零**。读：`AT+TIME` → `+TIME:<date>,<time>`；写：`AT+TIME=<date>,<time>`。时区 `AT+TZONE` 单位**确认为秒**（`AT+TZONE=?` → `+TZONE:28800`），`AT+TZONE=28800` 生效。SNTP `AT+NWSNTP=<sntp>(,<server_ip>,<period>)` → `AT+NWSNTP=1,ntp.aliyun.com,86400` 后 `+TIME:2026-10-03,11:30:51` 为**真实本地时间(UTC+8)**。`AT+DATE` **不存在**（`ERROR:-1`）。DESIGN §5 假设全部成立 |
| 3 | AT 命令中 URL 特殊字符（`&` `?` `,`）是否需要单引号包裹 | **✅ 已清零（2026-10-03 心知 now.json 实测）**：裸 URL 直接可用，`&`/`?` 不转义、单引号可加可不加，三种写法结果一致（`len=690 body_len=274`）。从 `HELP` 语法看 URI 是**第一个逗号前的单参数**，单引号仅用于**值内含逗号**的场景 |
| 4 | DA16200 开机自动重连行为 | **✅ 已清零，但结论经过一次修正（2026-10-03）**<br>**修正过程值得记住**：先前仅凭 `AT+WFCAP → ERROR:-7` 就断定"模块不自连、NVRAM 无 profile"，并据此把阶梯设计成 `被动等 URC → WFCAP → WFJAPA`。用户指出**之前确实成功拉到过天气，且那次 `+WFJAP:1` 是开机自发出现的**，说明 profile 存在、模块会自连 —— 前一个结论是错的。<br>**真实机制**：`+WFJAP:1` 是模块上电时发**一次**的 URC。如果只复位 RA 板子而模块没断电，它在 RA 开始监听之前就把这条 URC 发完了，RA 永远等不到 —— 而此时模块**一直连着 AP**，只是 RA 不知道。此时 `AT+WFCAP` 报的 `ERROR:-7` 意思是"已经连了、不用再连"，**不是"无 profile"**。<br>**修正后的设计**：阶梯第一级改为**主动查询** `AT+NWIP=?`，从 `+NWIP:0,<ip>,<mask>,<gw>` 直接取 IP，不再依赖能否赶上一次性 URC：`NWIP_QUERY → WAIT_URC → WFCAP → WFJAPA`，每级 8s。冷启动（RA 先起）时 URC/WFCAP 两级仍能兜住 |
| 5 | 长响应完整性 | **✅ 已清零（是丢字节，非截断）**：根因 = `__io_putchar` 逐字节忙等 TX 完成 + WIFI RX 环仅 512B，115200 下零余量溢出静默丢。修法 = USB TX 环 4KB 非阻塞 `usb_tx_pump()`（三指针，区分"已入队"与"硬件已发完"）+ WIFI RX 环 8KB。**已验证通过**：`AT+WFSCAN` 15 条 AP 全部 BSSID 完整 6 段、字段结构一致、零缺段零粘行（修复前 20 条里有 10 处硬伤），`wifi_rx_drop=0`、`usb_tx_drop=0` |
| 14 | **poll 步骤顺序导致的"状态自我回退"** | **✅ 已清零（2026-10-03 实测复现并修复）**。`weather_task_poll()` 的第 2 步（取 URC）在第 3 步（收命令结果）**之前**，所以同一次 poll 里可能先由 `mark_connected()` 置 `CONNECTED`，紧接着 `on_cmd_done()` 就跑。若 `on_cmd_done()` 无条件写 `status = WIFI_STAT_CONNECTING`，会把刚确认的 CONNECTED 打回去 → 阶梯重启 → **已连着还在反复发 `WFCAP`/`WFJAPA`**。实测日志里 `+WFJAP:1` 被报了两次、`WFJAPA` 连发两次。修法：`reconnect_if_needed()` 只在非 CONNECTED 时回退（`48aec9a`）。<br>**教训**：命令返回时刻晚于 URC 到达是常态，凡在"命令完成回调"里改连接状态的地方，都必须先判断当前是否已连上 |
| 15 | **冷启动第一条 `AT` 自检必然超时** | **✅ 已清零（不是故障）**。实测 `AT` 自检只收到半个 `O`（`TIMEOUT@WAIT wifi_rx=5`），而紧接着的 `AT+NWIP=?` 完全正常、拿到 IP。原因是模块冷启动时第一条命令的回复被截断。结论：自检**超时属常态**，只作日志，联网状态一律以 `AT+NWIP=?` 回读为准（`48aec9a`） |
| 16 | **心知 v3 `now.json` 的真实字段结构** | **✅ 已清零（2026-10-04 直接向接口发请求取得，不再猜字段名）**。用配置里的 key 实打实请求 `http://api.seniverse.com/v3/weather/now.json?key=...&location=ip&language=zh-Hans`，返回 **189 字节**：<br>`{"results":[{"location":{"id":..,"name":"北京","country":"CN","path":"..","timezone":"Asia/Shanghai","timezone_offset":"+08:00"},"now":{"text":"晴","code":"0","temperature":"18"}}],"last_update":"2026-10-04T11:34:41+08:00"}`<br>要点：① 天气三要素在 **`results[0].now`** 下，键名是 **`text` / `code` / `temperature`**（此前假设的 `ads` / `lc` **根本不存在**）；② 三个值**都是 JSON 字符串**，故取用 `atoi`；③ `last_update` 在**根节点**，不在 `results[]` 里；④ `location=ip` 能直接定位，省掉地理库。真实 body 已固化进 `weather_parse_selftest()`（`src/weather_parse.c`），串口敲 `WXTEST` 可在目标板上用真 cJSON 验证解析层 |
| 17 | **引擎时钟与真实时钟的偏差** | **✅ 已清零（此前结论方向完全相反，已纠正）**。主循环一轮 = `delay(10ms)` + 工作 + `at_engine_tick(10)`，所以引擎时钟**≈真实时钟，最多略慢，绝不会快 10 倍**。OLED 重绘虽需 40~90ms，但受 `ui_dirty` 门控，全程十几次，总偏差 1~2 秒量级。因此 `WEATHER_HTTP_TIMEOUT_MS = 20000` 就是**约 20 秒真实时间**，对 `AT+NWHTC` 的 DNS+TCP+TLS+HTTP 已经充裕 —— **不要为了"看起来不够"而调大**，否则失败时命令阶梯会白等两分钟。<br>**教训（比结论重要）**：曾经把 `WEATHER_HTTP_TIMEOUT_MS` 从 20000 调到 120000，依据是"引擎时钟快 10 倍"这个错误前提；又因为把 `[hb] 1s` 里的 `up` 秒数误读成"距上次事件的时间"，把**请求刚发出 1 秒**判成"抓取卡住了"。凡是靠**日志里的时间数字**下的结论，都必须先确认那个数字的**语义和单位**，否则一次误读就会带偏整轮排查方向 |

阶段4配网调研（阶段4开工前清零）：

| # | 事项 | 如何确认 |
|---|---|---|
| 8 | **`TRTS` 在 SoftAP mode 的 AT 运行行为** | **✅ 已板测通过**：本项目 AT 固件 `WFMODE=1` 后 `AT+TRTS=8080` → `OK`，手机作为 TCP client 连入并触发 `+TRCTS`。虽然 UM-WI-003 p.112 有 connected-to-AP prerequisite，R01AN7287 SoftAP 示例与当前实测均支持该用法。 |
| 9 | **RA-hosted HTTP over AT TCP** | **✅ 核心流程已由 `33 - 副本.txt` 板测通过**：8080 GET `/`、`/favicon.ico`、POST `/cfg` 均到达 RA；POST 响应后 TCP close、SoftAP teardown、STA reconnect 成功。端口80观察到 DA16200 LWIP 内容，改访问 8080 后看到 RA 页面。仍待重测失败输入与健壮性用例。 |
| 10 | **SoftAP 回 STA + RA 持久化** | **STA 回连已板测通过**（`+WFJAP:1`）；RA Data Flash 双槽实现已编译，写入/重启读回/断电保留还未板测；最新开机强制 STA 版本用户反馈正常。 |

其他已知项：

| # | 事项 | 如何确认 |
|---|---|---|
| 6 | ~~OLED 接线~~ 已确认与 `RA4M2OLED` 示例一致；剩余：机械按键 P005/P006 的极性/上拉方式 | 实现时看原理图或实测 |
| 7 | 心知天气免费版限频 ~20 次/时 | 沿用 15 分钟刷新即可 |
| 11 | **DA16200 启动误留在 SoftAP** | **✅ 修复并由用户 RFP 烧写确认当前版本正常**。旧版开机直接执行 `WFSTA/WFCAP/WFJAPA`，模块在 `WFMODE=1` 时返回 `ERROR:-5`；新版先 `WFTAP → WFMODE=0 → RESTART` 并等待 `+INIT:DONE,0`，随后才跑联网阶梯。模块只复位 RA、DA16200 未重启时的旧一次性 URC 风险仍需注意。<br>另一条假象：中途"程序不动了"是诊断日志把 4KB USB TX 环灌满、`__io_putchar()` 静默丢字节所致（`a17d448`），已定死"诊断日志必须低频"的约定（doc/README.md §3.4.1） |
| 12 | 天气链路「收到完整响应但不解析」 | **待清零**（此前被联网问题挡在前面）。已排除：`s.cmd` 归属错分发（`be68f18`）、`weather_http_busy()` 被提前清（`be68f18`）、`line_overflow` 粘住（已验证正常清除）、`+NWHTCSTATUS` 前缀匹配（逐字节核对无误）、`split_http` 边界（已审，无越界）。下次用 `[ENG] close=` 的 `plus`/`nw`/`len` 与 `[hb]` 的 `rx` 定位 |

## 10. 决策记录（全部已确认）

| 决策项 | 结论 |
|---|---|
| 按键 | **B：4 键全接入**，映射表见 §6（可微调） |
| 配网 | **必须 SoftAP 网页配网**，方案见 §8 |
| 实施顺序 | 先串口输出验证（阶段2）、后 OLED（阶段3） |
| 工程位置 | `.` |
| IP 获取方式 | **从 `+WFJAP:1` URC 解析，不发 `AT+NWIP=?`**（2026-10-03，因长响应丢字节超时，见风险#5） |
| 上电流程 | **先 `AT` 自检，再被动等 `+WFJAP`**，然后 TZONE → SNTP → 拉天气（2026-10-03 用户要求） |
| 天气服务 | **心知天气 `now.json`**，不用百度（百度首页 667KB / 推 60s，会淹没后续命令） |
| 其他 | OLED 接线=`RA4M2OLED` 示例；不用 RA 内部 RTC；显示对齐 GD32 版 |

## 11. 可直接复用的资产

| 来源 | 内容 |
|---|---|
| GD32 `weather_parse/text/ui/key_scan/cfg` | 解析、39条文案表、4页面UI+图标、按键三态、配置头——平台无关，基本原样移植 |
| GD32 `weather_task/weather_http` | 状态机结构与 URL 构造逻辑照搬，HTTP 收发替换为 AT 引擎调用 |
| RA `4.weather_app` | 双 UART 驱动、ISR 环形缓冲、行组装、`[TX]/[WIFI]` 打印、编译/重命名流程 |
| RA `RA4M2_100PIN_example\RA4M2OLED` | SSD1306 驱动 + 软 SPI + 运行时引脚配置（免 FSP 重生成），接线已确认 |
| RA `RA4M2_100PIN_example\RA4M2GPIOINPUT` / `RA4M2TOUCH` / `RA4M2LED` | 机械键输入模板（P005/P006）、触摸键 CTSU+RM_TOUCH 模板（qe_gen 齐全）、板载 LED 状态指示（P044/P045/P002） |
| 新增第三方 | cJSON（单文件，无依赖） |

### 外部参考文档

- UM-WI-003 DA16200 AT-Command User Manual（SparkFun CDN Rev2v5）：`https://cdn.sparkfun.com/assets/6/f/f/7/8/UM-WI-003_DA16200_AT-Command_User_Manual_Rev_2v5.pdf`
- Renesas《Ultra-Low Power Wi-Fi Ready2Go with AT Commands》webinar 讲义（SoftAP+Web demo 的 AT 序列）：`https://www.renesas.com/en/document/tra/ultra-low-power-wi-fi-ready-2-go-commands-webinar-handouts`
- 应用笔记 r01an7287ej0100：RL78/G22 Wi-Fi Communication (Soft AP mode) with DA16200/DA16600（WFMODE/WFSAP/NWIP/NWDHS/TRTS 完整序列 + AT 框架样例）：`https://www.renesas.com/en/document/apn/rl78g22-wi-fi-communication-soft-ap-mode-da16200da16600`
