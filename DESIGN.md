# 4.weather_app — RA4M2 + DA16200 天气显示站 实现思路

> 状态：**方案评审完成（决策已全部确认），待开始阶段0**
> 参考实现：GD32VW553 版 `..\..\..\GD32\workspace\GD32VW553\4.weather_app`
> 基础工程：RA 版 `.`（USB↔WIFI 透传桥）

## 1. 目标

在 RA4M2 + DA16200 组合上复刻 GD32 版天气站功能：

- DA16200 联网 → SNTP 对时 → 定时拉取心知天气 API → 解析 → 显示
- OLED 显示时间/温度/天气文案，页面切换（对齐 GD32 布局）
- 4 按键交互（2 机械 + 2 触摸，映射见 §6）
- **SoftAP 网页配网**（必须，见 §8 方案）+ 凭据 NVRAM 持久化

## 2. 两平台架构差异（方案立足点）

| 维度 | GD32VW553 参考版 | RA4M2 + DA16200 本工程 |
|---|---|---|
| 网络 | 片上 WiFi + lwip socket，代码直接 `getaddrinfo/recv` | **无网络能力**，全部经 AT 命令驱动 DA16200 |
| HTTP | `weather_http.c` 裸 socket 收包 | `AT+NWHTC=<url>,get`，payload 由模块回传 |
| SNTP | lwip `sntp_start_once()` + 时间钩子 | `AT+NWSNTP=1,ntp.aliyun.com,86400`，读时命令待调研 |
| 解析 | SDK cJSON | **移植单文件 cJSON**（MIT） |
| OS | FreeRTOS 多任务 | **裸机 while(1) 状态机**（沿用桥工程 10ms 节拍，不开 RTOS） |
| 凭据存储 | MCU Flash `wifi_credential_t` | WiFi 凭据存 **DA16200 NVRAM**（`AT+WFJAPA` 自动保存）；城市见 §8 |
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
| 对时 | lwip SNTP 钩子 | **不使用 RA 内部 RTC**（用户已确认）。实测：`AT+TZONE=28800`（**单位确为秒**）+ `AT+NWSNTP=1,ntp.aliyun.com,86400` → `AT+TIME` 回 `+TIME:2026-10-03,11:30:51`（真实本地时间）。主页时钟每分钟查一次 `AT+TIME` |
| 拉天气 | 裸 socket GET，4KB 收包缓冲 | `AT+NWHTC=<seniverse now.json>,get` **PAYLOAD 模式**。✅ **裸 URL 即可，不要单引号**（风险#3 已清零：裸/引号结果完全一致 `len=690 body_len=274`）；✅ URL 含 `&`/`?` **不需转义**（URI 是第一个逗号前的单参数）；⚠ 串长必须 < `AT_CMD_MAX`(180) |
| 解析 | cJSON：`results[0].now.text/code/temperature` + `last_update` | **原样移植** `weather_parse.c`。实测 `hdr_sep=1`（**含 HTTP 响应头**）→ 必须先切 `\r\n\r\n`；切完 `json_direct=1`（正文首字符 `{`）→ 可直喂 cJSON |
| 刷新 | 15min：主循环按分钟计数 | 相同，挂在主状态机 tick 上（桥工程已有 10ms 节拍，累计秒/分） |
| 按键 | short=刷新 / double=翻页 / long=配网 | 移植 `key_scan.c` 三态逻辑（20ms 轮询 + 去抖） |

配置项沿用 `weather_cfg.h` 形式（WiFi/KEY/API/城市/刷新周期），去掉软 SPI/Flash 偏移等 GD32 专属项。

## 6. 显示与外设（已有现成资产：`RA4M2_100PIN_example\RA4M2OLED`）

- **OLED 驱动直接复用**：`src/oled/oled.c/h + oledfont.h`（SSD1306 128x32，`OLED_ShowChar/ShowString/ShowBMP` 等 GD32 同款 API）+ `src/oled_port.c`（软 SPI 回调，**运行时 `R_IOPORT_PinCfg` 动态配引脚**）+ `src/gpio_spi/`。
  - 示例固定接线：CLK=**P001**、DAT=**P000**、RES=**P003**、DC=**P007**、CS=**P014**（7PIN，软 SPI 100kHz）
  - 关键收益：**运行时 PinCfg → 不需要在 e2studio FSP 配置器里重新生成 ra_gen**，与桥工程引脚（P410/P411）无冲突，纯命令行可完成；**接线已确认与示例一致**
- ~~RTC~~：**用户已确认不需要**；显示内容完全对齐 GD32 版 `weather_ui.c`（主页 = 时间+温度+图标+天气文案；WiFi 页 4 行小字），时间来源见 §5 对时行
- **按键：板上共 4 个（2 机械 + 2 触摸）**
  - 2 个机械键：`RA4M2GPIOINPUT` 示例读 **P005/P006**（极性/上拉实现时按原理图或实测确认）
  - 2 个触摸键：`RA4M2TOUCH` 示例完整模板（FSP CTSU + RM_TOUCH 中间件，`qe_gen/` 生成物齐全，电极引脚 P300/P415/P708 系，`button_status` 1/2 两键），可直接拷贝配置
  - **按键映射（已确认 4 键全接入，建议如下，可微调）**：

    | 按键 | 功能 |
    |---|---|
    | 机械键1 | 短按=立即刷新 / 双击=翻页 / 长按3s=进入配网（GD32 三态） |
    | 机械键2 | 单击=翻页 |
    | 触摸键1 | 单击=立即刷新 |
    | 触摸键2 | 单击=翻页；长按=进入配网（备用） |
  - 触摸键接入注意：需把 `RA4M2TOUCH` 的 CTSU 外设配置合并进本工程 configuration.xml（`hal_data`/`pin_data` 同步），此一步**可能需要 e2studio 打开生成一次**——阶段3 先评估"手工合并 vs e2studio 一次配置"，评估结论回报后再动
- 板载 LED：P044/P045/P002（可作状态指示）

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

### 阶段2：天气链路（串口输出版）← 当前进行中
- 联网状态机（IDLE/CONNECTING/CONNECTED/FAIL，对齐 GD32 五态简化）
- SNTP 对时 + 时间获取、心知天气 GET + cJSON 解析 + code→文案
- 15 分钟定时刷新、按键单击触发刷新
- **at_engine 切全异步 + 按键事件 FIFO**（§4 硬性要求），验证连接中/失败中按键立即响应（§8.1 根因2 的修复点）
- **验收**：串口每 15 分钟打印一行天气（时间/温度/文案），单击立即刷新；**连接失败阻塞期间长按/单击 10/10 次即时响应**

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
| 双击 | 翻页（主页 ⇄ WiFi 信息页） |
| 长按 3s | 进入/退出配网（阶段4 实现） |

### 阶段3：OLED + 按键（4 键全接入）
- 复用 `RA4M2OLED` 示例的 oled/oled_port/gpio_spi（运行时配引脚，**无需 FSP 配置器操作**），移植 GD32 `weather_ui.c` 4 页面
- 2 机械键按 §6 映射接入；2 触摸键拷贝 `RA4M2TOUCH` 的 CTSU 配置（先评估合并方式，见 §6 注意事项）
- **验收**：行为与 GD32 版 README 的 UI 布局一致，4 键映射表全部生效

### 阶段4：SoftAP 网页配网（必须）+ 城市持久化
- 按 §8 方案实现：DA16200 SoftAP + DHCP + TCP Server → RA 极简 HTTP 配网页 → 手机提交凭据 → 切 STA 联网存 NVRAM
- 城市字段随配网页一起提交，存 DA16200 NVRAM（或 RA data flash，阶段4定）
- **§8.1 的 GD32 根因对策全部落实并验收**
- **验收**：①全新模块（无凭据）开机长按进配网 → 手机连 AP 打开配网页 → 提交 WiFi/城市 → 自动回连并拉到天气；断电重启免配置；②§8.1 四条健壮性验收全过

## 8. SoftAP 配网方案（已确认必须做）

参考 Renesas 官方 **Ready2Go AT Commands webinar**（SoftAP + Web 页面 demo）与 **RL78/G22 + DA16200 应用笔记 r01an7287ej0100**（AT 命令序列模板），全链路只用通用 AT 命令：

```
长按进配网（RA状态机 → PROVISIONING）
  ① AT+WFMODE=1              切 SoftAP 模式
  ② AT+WFSAP=WeatherCfg,3,1,<pwd>,1   WPA2 配网SSID+密码
  ③ AT+RESTART → 等 +INIT:DONE
  ④ AT+NWIP=1,10.0.0.1,255.255.255.0,10.0.0.1     AP 侧 IP
  ⑤ AT+NWDHS=1,10.0.0.2,10.0.0.11,1800           DHCP 服务
  ⑥ AT+TRTS=80 → 开 TCP Server（监听 80 端口）
  ⑦ RA 经 AT TCP 数据命令收 HTTP 请求 / 回配网页   ← 极简 HTTP 服务
手机连 WeatherCfg AP → 浏览器访问 10.0.0.1 → 填 SSID/密码/城市
  ⑧ RA 收到 POST → AT+WFTAP 停 SoftAP / WFMODE=0 + RESTART
  ⑨ AT+WFJAPA=<ssid>,<pwd>  → +WFJAP:1 连接成功（凭据自动存 NVRAM）
  ⑩ 回 CONNECTED 状态，配网完成 UI 提示
```

- 配网页直连 IP（GD32 版同样是提示 `Open 192.168.1.100`，非强制 DNS 劫持），第一版不做 captive DNS
- 页面极简单表单（SSID/密码/城市），RA 侧 HTTP 解析只需处理 `GET /` 与 `POST /cfg`，缓冲按 §9 风险5 规格
- 开机有凭据 → `AT+RESTART` 自动回连（风险4 实测确认）或 `AT+WFCAP` 兜底；无凭据/连接失败 3 次 → 长按进配网

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

阶段4配网调研（阶段4开工前清零）：

| # | 事项 | 如何确认 |
|---|---|---|
| 8 | **`AT+TR*` TCP Server 命令族**：开监听（TRTS）、数据收发/URC 语法、单次数据长度上限 | `AT+` 命令列表 + `HELP AT+TR...` + webinar 讲义原文（见 §11）。命令表已确认存在：`TRTS`(server)/`TRTC`(client)/`TRUR`/`TRPRT`/`TRPALL`/`TRTALL`/`TRSAVE`/`TRUSE`/`TRSSL*`，另有 **`AT+TCPDATAMODE`**（TCP 数据透传模式）与 **`AT+PROVSTART`/`AT+PROVSTAT`**（**模块自带配网，阶段4 可能不必自建 HTTP 服务**）。优先查 `HELP AT+PROVSTART` |
| 9 | 配网流程模式切换时序：WFMODE 切换 + RESTART 期间 URC、`+WFJAP` 与 TCP Server 共存状态 | 阶段4前用串口手工演练完整 §8 序列 |
| 10 | RA 侧极简 HTTP 服务资源：配网页 HTML ≤1KB、4KB 缓冲够 POST body | 阶段4实现时验证 |

其他已知项：

| # | 事项 | 如何确认 |
|---|---|---|
| 6 | ~~OLED 接线~~ 已确认与 `RA4M2OLED` 示例一致；剩余：机械按键 P005/P006 的极性/上拉方式 | 实现时看原理图或实测 |
| 7 | 心知天气免费版限频 ~20 次/时 | 沿用 15 分钟刷新即可 |
| 11 | **DA16200「要插拔几次才正常」** | **✅ 已清零：URC 一次性 + 只复位 RA 板子，不是硬件问题**。`+WFJAP:1` 只在模块上电时发一次；只复位 RA 时它早发完了，RA 等不到，而模块其实一直连着。修法：阶梯第一级改为主动 `AT+NWIP=?` 查 IP（`56a41ae`）。<br>⚠ 本条曾被**错误诊断过一次**：中途仅凭 `ERROR:-7` 断定"NVRAM 无 profile、模块不自连"，是用户指出"之前成功拉到过天气"才纠正。教训：`ERROR:-7` 在"已连接"状态下的语义是"不必再连"，不能当成"无 profile"。<br>另一条假象：中途"程序不动了"是诊断日志把 4KB USB TX 环灌满、`__io_putchar()` 静默丢字节所致（`a17d448`），已定死"诊断日志必须低频"的约定（doc/README.md §3.4.1） |
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
