# 4.weather_app 工程功能说明

## 1. 工程概述

本工程是 **RA4M2 + DA16200 天气显示站**（复刻 GD32VW553 版 `4.weather_app`），总体方案见根目录 `DESIGN.md`。

- **当前显示配置**：默认 `WEATHER_UI_SERIAL_SIM=0`，启用实体 SSD1306 OLED；设置为 `1` 可切回串口四行模拟。硬件软 SPI 使用 SCLK=P001、MOSI=P000、RES=P003、DC=P007、CS=P014。五个引脚已在 `configuration.xml` 和 `ra_gen/pin_data.c` 配置为 GPIO 输出高，并由 `oled_port_init()` 再次初始化。若屏幕仍无显示，需核对实际 OLED 排针标号、电源/地、逻辑电平、模块控制器及复位/时钟/数据波形；实体屏板级工作状态仍需实测确认。
- **NTP 配置**：主服务器为 `ntp.aliyun.com`，备用 `pool.ntp.org`。模块返回 NWSNTP `OK` 后等待 8 秒，再查询 `AT+TIME` 验证；时间查询完成后开始天气 HTTP 请求。
- **天气指标与按键**：now JSON 的可选 humidity/wind_speed 若缺失，会尝试使用 daily JSON 同名字段补齐；仍为 `--` 表示两次响应都没提供值。P005 短按在未联网/AT 忙时排队，联网后执行；P005 双击和 P006 单击翻页。CTSU TS11/TS12 的 FSP 6.6 QE Touch 文件和构建规则已接入，`make -B -j8 all` 可完整链接；触摸阈值及触摸事件仍未完成板上验收。
- **板载 LED**：LED1/P045 联网中约 200 ms 节拍闪烁、连接成功常亮；LED2/P044 天气请求中闪烁、天气有效后常亮；LED3/P002 在联网/天气请求活动期间熄灭，空闲时慢速呼吸（约 2 秒周期）。GPIO 高电平点亮，固件运行时配置。
- **RA 主栈**：`configuration.xml` 中 `config.bsp.common.main=0x1000`（4 KB）。2026-10-14 曾观察到 1 KB 栈版本在天气 UI 路径发生 INVSTATE HardFault，PC 落入主栈区；4 KB 版本已实测跑通 SNTP、now/daily HTTP 200、解析并持续心跳。栈配置应通过 RASC 生成，不要只改 `bsp_cfg.h`。
- **状态诊断**：`[hb] eng=0` 表示 AT 引擎闲置，此时 `mode=0`；`loop/stage/wrx/urx/wtx` 分别是主循环次数、最近主循环阶段、已处理 Wi‑Fi/USB RX 字节和 Wi‑Fi TX 完成次数。若发生 Cortex-M Fault，现场保存在 `.ram_noinit`，硬件 RESET 后可打印 `[FAULT]`；彻底断电不保证保留。
- **按键**：已验证的是 SW1/P005 短按刷新、双击翻页，SW2/P006 单击翻页；刷新在启动联网中排队，`[hb] rq=1` 表示尚待发送；Wi-Fi 进入 FAIL 后再短按会重启连接阶梯。触摸输入和长按配网尚未完成整机验收。
- **启动模式与配网**：开机先下发 `AT+WFTAP`、`AT+WFMODE=0`、`AT+RESTART`，只有收到 `+INIT:DONE,0` 才启动 STA 联网阶梯；仍报告 AP 模式或启动超时会有限重试，不会在未确认 STA 时循环尝试联网。配网请求时按需切换 SoftAP。SoftAP + `TRTS=8080` 与 RA-hosted HTTP GET/POST、STA 回连已有实机日志；无需修改模组 SDK 固件。**端口区别：80 显示 DA16200 LWIP 页面；RA 配网页在 8080，手动访问 `http://192.168.1.100:8080/`。**
- **RA Data Flash 设置**：SSID、密码、城市保存在 8 KB Data Flash 的最后 384 字节（`0x08001E80–0x08001FFF`），双槽、CRC 和序号校验；启动只读，首次无记录用编译默认值而不擦写。配网获得 STA 连接后写入另一槽并读回校验。页面提交先显示“已收到/正在连接”，不是最终 Flash 保存成功回执。**编译已通过；Flash 实际写入、断电保留、双槽掉电恢复尚待板上验证。** 离线 `PORTALTEST` 可测 parser/framing，阶段4尚未全部验收。
- **OLED 配网提示**：启动 SoftAP 时显示启动提示；AP/TCP listener ready 后显示热点名、密码和 `192.168.1.100:8080` 访问提示；POST 后切回 STA 时显示连接中的 SSID。Wi-Fi 状态页和连接/失败页显示当前生效 SSID。
- **2026-10-17 界面改版（板测反馈）**：主页底部行合并为 16px 大行「风速 xx 湿度 xx」（新增 16px 速/湿/度字模），「今日 lo/hi」行删除；配网页第 2 行固定显示 `AP:leeWethWifi`（手机要连的热点名）；联网中/失败页第 3 行显示 Data Flash 保存的城市（`[cfg] loaded` 日志带 `city='...'`；FLASH 里没城市就显示 IP，需重新配网填一次 CITY）；翻页改为 3 页循环（主页 → WiFi 信息 → 预报页）。第二轮修正：16px 速/湿/度/预/报 字模改用 **32px 抗锯齿 → 2:1 降采样**管线重生成（笔画更粗）；第三轮实锤修复：**发现 `weather_cn_font` 表错位**（GD32 原表 44 行装 45 槽且移植时又丢 XIN 行），尾部字模整体错一位（原来板上实际显示 速→度 / 湿→预，这就是一直"读错字"的真根因），现已恢复成 52 行 52 槽完全对齐（补"城"16px、新增"明/后"16px）。**预报页最新版式** = 16px 第 0–1 行「当前天气字 + 当前温度 ℃ + 时间（右锚）」，16px 第 2–3 行「明 lo/hi 后 lo/hi」；不再有独立当前温度行、"预报 Today Hi/Lo"标题与"今"行；中文标签与数字之间加冒号分隔（风速：xx 湿度：xx / 明：xx 后：xx，2026-10-18）；预报数据仍来自 `daily.json?days=3`（免费 key 上限 3 天）。
- **联网后链路监测**：CONNECTED 状态下每 5 秒空闲时查询一次 `AT+WFSTA`；同时消费 `+WFJAP:0` / `+WFSTA:0` 断线 URC。确认断线后清除旧 IP、切为 CONNECTING 并重启联网阶梯，LED1 从常亮转为连接闪烁。健康查询成功不置 OLED dirty；仅状态/内容变化时重绘，避免因轮询频繁刷新屏幕。
- **本地凭据与发布安全**：复制 `src/weather_secrets.example.h` 为 `src/weather_secrets.h`，只在本地填入心知 API Key 和个人测试 STA SSID/密码。`weather_secrets.h` 已加入 `.gitignore`。固件若编译进 API Key，仍可能从 ELF/HEX 提取；公开发布应采用后端代理或专用可撤销 Key，并在发布前确认产物不含真实 Key。
- **最近实机状态**：用户使用 Renesas Flash Programmer 烧写启动模式收敛版本后反馈“当前版本正常”；串口日志 `44.txt` 是该修复前的失败版本，显示开机直接进入 `WFSTA/WFCAP/WFJAPA` 且模块返回 `ERROR:-5`。新版本最新启动顺序仍建议留存串口日志，完整 Data Flash 掉电验收未完成。

> **板卡来源**：RA4M2 开发板与 DA16200 WiFi 模块均为向 **RA 生态工作室**申请的测试板卡。

### 基线功能（阶段0，继承自透传桥）

把 **RA 板的 USB 串口数据重定向到 DA16200 WiFi 模块的 UART**，形成一个透传测试桥，用于通过 AT 命令测试 DA16200 的 WiFi 功能。

数据流向：

```
PC 串口助手 ──USB──> RA4M2 (SCI9) ──板内UART──> DA16200 (SCI0, AT口)
   <── 打印 [WIFI] 回显 <──                    <── 模块响应/上报 ──
```

- 用户在 PC 串口助手里输入的每一行命令，经 USB 串口送入 RA，回显为 `[TX]` 并转发给 DA16200（自动补 `\r\n` 行结束符）。
- DA16200 返回的数据逐行打印为 `[WIFI]`，回到 PC 串口助手。

本阶段**不解析 AT 命令**，只做透明转发 + 行级打印（阶段1 将升级为程序化 AT 引擎）。

## 2. 硬件连接

| 项目 | 配置 |
|---|---|
| MCU | RA4M2（R7FA4M2AD），Cortex-M33 |
| USB 串口 | SCI 通道 9（`g_usb_uart`），115200 8N1 |
| WIFI 串口 | SCI 通道 0（`g_wifi_uart`），115200 8N1 |
| WIFI 侧引脚 | P410 = SCI0_RXD0（板收）、P411 = SCI0_TXD0（板发） |
| 对端模块 | DA16200，AT 命令口为 **UART1**（UART0 是调试控制台） |

接线注意：RA 的 TX/RX 需与 DA16200 的 RX/TX **交叉**相连，且接到模块的 UART1（AT 口），不是 UART0。

**实际连接方式：DA16200 WiFi 模块直接插在 RA4M2 板的 PMOD2 接口上**，无需飞线，模块通过 PMOD2 与 SCI0（P410/P411）相连。

### 实物连接图

![实物连接图：WiFi 模块插在 PMOD2](images/pmod2_connection.png)

## 3. 软件设计

### 3.0 阶段1 新增模块

| 文件 | 职责 |
|---|---|
| `src/at_engine.h/.c` | 异步两段式 AT 引擎。`at_engine_start(cmd,tmo)` = 普通命令（收到 `OK`/`ERROR` 即完成）；`at_engine_start_ex(cmd,tmo,AT_MODE_PAYLOAD)` = **NWHTC 专用**：发命令回裸 `OK` 只是同步回执，引擎继续采集直到末尾的 `+NWHTCSTATUS:<n>`。另有 `at_engine_urc_seen()`（sticky 记录 8 条，**不受 FIFO 深度限制**，用于 45s 被动监听判定）、`at_engine_urc_seen_clear()`、`at_engine_urc_trace()`（每条 URC 打 `[URC] +Nms` 时间戳） |
| `src/at_probe.c/.h` | 两套诊断 FSM（见 §4.1），把风险 #1~#5 一次收口 |
| `src/weather_cfg.h` | 用户配置：WiFi SSID/口令（留空 = NVRAM profile）、心知天气 key、城市、刷新周期、时区、NTP、NWHTC 诊断超时 |

### 3.0.1 串口控制台输出一律用 ASCII（重要）

固件里的 `printf` 字符串**必须用 ASCII**，中文只放注释和 `doc/`。

原因：源码是 UTF-8 无 BOM，而工具链按系统代码页（本机 GBK）读源文件，
`突发`(UTF-8 `E7AA81 E58F91`) 会被当成 GBK 读成 `绐佸` 再重新编码发出，
串口工具按 UTF-8 显示就是 `绐佸彂`。`.cproject` 未配 `-finput-charset/-fexec-charset`。

判读不受影响：所有关键字段（`OK`/`TMO`、`wifi_rx_bytes=`、`hdr_sep=`、`body_len=`、
`json_direct=`、`nwstatus=`、`+TIME:`、`+WFSCAN:`）都是 ASCII。

`at_probe.c` 的 `cap_http()` 把一次 NWHTC 响应拆成 `http_cap_t` 特征表输出，**不用肉眼看 `[WIFI]`**：

| 字段 | 判定什么 |
|---|---|
| `nwstatus` | `+NWHTCSTATUS:<n>` 的 n（0 = 成功）—— **这才是 NWHTC 的结束标志** |
| `len` / `ovf` | 实际拿到多少字节、4KB 是否够 |
| `hdr_sep` / `http` | 是否有 HTTP 响应头、首行状态行（如 `HTTP/1.1 200 OK`） |
| `body_off` / `body_len` | 切 `\r\n\r\n` 后正文起点与长度 |
| `json_direct` / `lead` | 正文首字符是 `{` → 可直喂 cJSON |
| `nlines` | 正文行数 |
| `body` / `tail` | 正文首行、整包尾部（看终止位置） |

输入约定（阶段1）：`AUTO` 启动全链路诊断，`AUTOB` 启动开机自动重连测试，`ABORT` 中止；
诊断期间其余输入被屏蔽（防透传与引擎 TX 冲突）；空闲时输入仍按阶段0透传。

阶段2 另增本地命令（不占透传通路）：`KEY` 打印两个机械键原始电平；`HB`
立即打印一次心跳快照；`WXTEST` 离线跑真实心知响应（不碰网络），用于
把"解析层"与"传输层"切开（见 DESIGN §9 #16）。

### 3.1 中断回调（ISR 只做入队）

- `usb_uart_cb()`：USB 串口收到字符（`UART_EVENT_RX_CHAR`）写入 `rx_ring[]` 环形缓冲（8KB，满则丢弃）；`UART_EVENT_TX_COMPLETE` 清 `usb_tx_busy` 允许 pump 下一段。
- `wifi_uart_cb()`：WIFI 串口同理写入 `wifi_rx_ring[]`（8KB）。

接收**不使用 `R_SCI_UART_Read`**，完全依靠 `UART_EVENT_RX_CHAR` 事件逐字节入队，避免读长缓存导致的 `UART_EVENT_RX_COMPLETE` 等待问题。

### 3.2 主循环（src/hal_entry.c `hal_entry()`）

每 10ms 轮询一次，**顺序为「先搬 RX 再延时」**（缩短字节在环里的驻留时间，突发响应不易丢）：

1. `usb_uart_rx_process()`：从 `rx_ring` 取字节，按行组装（`\r` 或 `\n` 作为行结束），满行调用 `rx_line_flush()`（识别 `AUTO`/`AUTOB`/`ABORT` 本地命令，否则转发）。
2. `wifi_uart_rx_process()`：同理从 `wifi_rx_ring` 组行，满行以 `[WIFI]` 打印；**每个字节同时喂给 `at_engine_rx()`**（引擎状态机 + URC 识别）。
3. `usb_tx_pump()`：把 USB TX 环里的待发数据整段提交（兑现非阻塞打印）。
4. `at_engine_tick(10)` → 轮询：**诊断序列/正常天气状态机二选一**（`at_probe_running()` 判定，不能同时轮询引擎，DESIGN §4）。空闲时立即返回，主循环阻塞 ≤ 1 tick。
5. `R_BSP_SoftwareDelay(10ms)`：节拍。

### 3.3 命令转发（`rx_line_flush()` / `wifi_line_send()`）

- 打印 `[TX] <命令>` 回显到 USB。
- `wifi_line_send()`：复制到临时缓冲，若行尾没有 `\r`/`\n` 则**自动补 `\r\n`**（这就是"PC 端按回车即可直接发 AT 命令"的实现），然后 `R_SCI_UART_Write` 发给 DA16200。2026-10-04 起**不再用共享的“发送完成标志”**判断完成：改为“提交前记一次 `wifi_tx_done_cnt` 基线，计数器变了即视为完成”（详见 §3.1 与设计文档 §9 #17）；引擎在途时会直接拒发并提示用 `HB`/`WXTEST`。

### 3.4 输出标记约定

| 标记 | 含义 |
|---|---|
| `[TX]` | 用户输入的命令（已回显并转发给模块） |
| `[WIFI]` | DA16200 的响应或异步上报（如 `+WFJAP:1,...`、`+INIT:DONE`） |
| `[AT>]` | AT 引擎发出的命令（AUTO/AUTOB 诊断期间） |
| `[PROBE]` | 自动诊断的步骤横幅与结果汇总 |
| `[URC]` | URC 时间戳追踪（AUTOB 被动监听期间，每条 URC 一行） |
| `[wx]` | 天气状态机（`weather_task.c`）的决策日志，如 `ip from URC:` / `TZONE OK` / `fetch <url>` |
| `[USB]` | USB 侧提示（如诊断期间输入被屏蔽） |
| `[ERR]` | 串口写失败等错误 |

### 3.4.1 打印路径（重要，勿退化）

`printf` 链路是 **USB TX 环形缓冲（4KB）+ 主循环 `usb_tx_pump()`**，**绝不能**改回
"`R_SCI_UART_Write` 单字节 + `while(send_complete_flag==false){}` 忙等"。

原因（2026-10-03 实测教训）：115200 8N1 下每字节 86.8µs，USB 发 1 字节正好 86.8µs，
与 WIFI 入向速率**零余量**。旧实现一收到字节就忙等发完，WIFI RX 环（当时仅 512B）
必然溢出，`wifi_uart_cb` 里 `if (next != wifi_rx_rd_index)` **静默丢字节**。
后果是 `AT+WFSCAN` 回显出现 BSSID 缺段（`d8::cc:87:f4:1e`）、两条 AP 粘成一行、
GBK SSID 被劈开——**丢字节，不是缓冲区截断**，任何基于 `[WIFI]` 行的解析都不可信。

现配置：USB TX 环 4KB、WIFI RX 环 8KB（容 2KB+ 突发），`printf` 满时丢并累加
`usb_tx_drop_cnt`，在 `AUTO` 汇总和 `[hb]` 心跳里打印（**验收要求恒为 0**）。

#### ⚠ 反向教训：诊断日志本身也会淹掉日志（2026-10-03）

排查"程序不动了"时，曾在 `at_engine_poll()` 里加了一条**每 tick 都打印**的收口摘要
（引擎停在 `ST_IDLE` 时 `s.final` 仍是上次结果，于是每 10ms 打印一行 ~70B）。
**600ms 就灌满 4KB 环**，`__io_putchar()` 满则丢（它不阻塞）→ 真正的
`[WIFI]`/`[wx]` 全被挤掉 → 板上看起来像卡死，主循环其实跑得好好的。

由这条得到的硬性约定：

| 约定 | 说明 |
|---|---|
| 收口类日志必须判"本次 poll 是否真的迁移了状态" | 不能只看 `state == IDLE`，要用进入时的快照比对（`was_idle`） |
| 诊断日志一律**低频** | 心跳 30s 一行（`WEATHER_HB_DIAG_MS`），单行 ≤100B |
| 需要看引擎状态就取**快照**而不是加打印 | `at_engine_diag()` / `at_engine_diag_t` |
| 判断"程序是否活着"看 `[hb]` 是否在刷新 | `[hb]` 推进但 `[WIFI]` 不动 → WIFI 口问题；`[hb]` 也不动 → 主循环真卡住 |

`[hb]` 行字段判读：

```
[hb] 12s eng=0 mode=0 urc=0/0 rx=18 ln=1 plus=1 wifi_rx=18 drop=0 usb_drop=0 st=1 wx=-- 0C
     │        │        │       │        │      │     │        │    │      │      └ 天气
     │        │        │       │        │      │     │        │    │      └ wifi_status_t
     │        │        │       │        │      │     │        │    └ 2=CONNECTING
     │        │        │       │        │      │     │        └ usb_tx_drop（必须恒为 0）
     │        │        │       │        │      │     └ wifi_rx_drop（必须恒为 0）
     │        │        │       │        │      └ 开机至今收到的 WIFI 字节数（不涨=口没数据）
     │        │        │       │        └ 以 '+' 开头的行数
     │        │        │       └ 组装完成的行数
     │        │        └ 已组装行/已见 URC（URC FIFO 深度 / sticky 记录数）
     │        └ at_mode_t: 0=CMD 1=PAYLOAD
     └ 引擎状态: 0=IDLE 1=SENDING 2=WAIT
```

### 3.5 初始化顺序

`hal_entry()` 中先 `R_SCI_UART_Open` 两个串口（USB、WIFI），再打印开机 banner：

```
RA E2STUDIO 4.weather_app
USB input -> WIFI uart (auto CRLF), WIFI response back to USB
Commands: AUTO = stage1 probe, AUTOB = boot-reconnect test, ABORT = stop
Local: KEY = key pin levels now, HB = heartbeat snapshot now
       WXTEST = parse the real seniverse body offline (no network)
```

注意：阶段1 的 `AUTO`/`AUTOB`/`ABORT` 与阶段2 的天气状态机**互斥**，诊断
期间走 `at_probe_poll()`，正常期间走 `weather_task_poll()`，由
`at_probe_running()` 判定，绝不能同时轮询引擎（DESIGN §4）。

> 2026-10-04 本地命令（`KEY`/`HB`/`WXTEST`）的输入与透传路径已做归属
> 隔离：`wifi_uart_send_complete_flag` 已彻底移除（见 DESIGN §9 #17），
> 本地命令不再把"发送完成"标志偷走——否则 AT 引擎的 ST_SENDING 会被
> 误判超时（见 commit 4456638 的根因说明）。

随后 `at_engine_init()` → 根据 `WEATHER_UI_SERIAL_SIM` 决定是否调用 `oled_port_init()` → `weather_task_init()`。串口模拟模式下不访问 OLED 引脚。

`weather_task_init()` **上电先发一条裸 `AT` 做模块自检**（2026-10-03 用户要求：
「每次开机先用 AT 命令测试模块，再走后续」），再进入天气状态机：

```
[wx] task init
[AT>] AT                          <- CMD_AT 自检, 2000ms
[wx] AT self-test sent
[WIFI] OK
[wx] AT self-test: OK, module is alive
[wx] step 0 idle 8000ms (try=0), escalating
[AT>] AT+NWIP=?                   <- 主动查 IP：模块已连上就直接收工
[WIFI] +NWIP:0,192.168.1.2,255.255.255.0,192.168.1.1
[WIFI] OK
[wx] +NWIP urc='+NWIP:0,192.168.1.2,...'
[wx] CONNECTED via +NWIP query, ip=192.168.1.2
```

要点：

- 自检**失败不致命**，打印 `TIMEOUT`/`ERROR` 后继续走阶梯。
- **阶梯第一级是主动查询 `AT+NWIP=?`，不是被动等**。原因：`+WFJAP:1` 是模块上电时
  发**一次**的 URC；只复位 RA 板子时它早就发完了，RA 永远等不到，而模块其实一直连着。
  主动查询免疫这个问题。
- 完整阶梯（每级 8s，`WEATHER_CONNECT_TIMEOUT_MS`）：
  `AT+NWIP=?` → 短等 `+WFJAP:1`（覆盖 RA 先起的冷启动）→ `AT+WFCAP` →
  `AT+WFJAPA=<ssid>,<pwd>`（30s，显式连 + 写 NVRAM）。
- ⚠ `AT+WFCAP` 回 `ERROR:-7` **不等于"NVRAM 无 profile"**。模块已连上时它的语义是
  "不必再连"。真实是否已连上由第一级的 `AT+NWIP=?` 回读判定。
- `WFJAPA` 连续失败 3 次（`WIFI_WFJAPA_MAX_TRY`）回退到第一级重跑整条链。
  计数放在 `on_cmd_done()` 里 —— 放 poll 里会每 10ms tick 自增，几秒就误判用尽。
- IP 解析统一走 `parse_ip()`：找第一个形如 `d.d.d.d` 的字段，
  `+WFJAP:1,'ssid',1.2.3.4` 和 `+NWIP:0,1.2.3.4,mask,gw` 两种格式共用一套代码。

## 4. 使用方法

1. 打开 PC 串口助手，连接 RA 的 USB 串口，115200 8N1。
2. 复位板子，看到 banner 即固件运行正常。
3. 直接输入 AT 命令并回车（例如 `AT+TIME` / `AT+WFSCAN` / `AT+WFJAPA=...`），仍然是透传。

```
AT
[WIFI] OK

AT+VER
[WIFI] <版本信息>

HELP AT+WFJAPA
[WIFI]     AT+WFJAPA=<ssid>[,<key>][,<hidden>]
[WIFI] OK
```

4. 异步上报（连接结果等）随时会以 `[WIFI]` 打出来，属正常现象。
5. 输入 `AUTO` / `AUTOB` 运行诊断（见 §4.1）；`ABORT` 中止。

### 4.1 两条诊断序列

**`AUTO` —— 全链路诊断，收口风险 #1/#2/#3/#5**

| 步 | 命令 | 收口什么 |
|---|---|---|
| 1 | `AT` | 链路通 |
| 2 | `AT+` | 命令表（风险#2/#8 的语法来源） |
| 3 | `AT+WFSCAN` | **风险#5**：`[WIFI]` 行必须无缺段/粘行 |
| 4 | `AT+WFCAP` → 失败回退 `AT+WFJAPA=<ssid>,<pwd>`，等 `+WFJAP` URC 30s | 联网（队列消费，非脉冲标志，DESIGN §8.1 根因1） |
| 5 | `AT+NWIP=?` | 拿到 IP |
| 6 | 语法组：`AT+TZONE=?` / `AT+TIME=?` / `AT+NWSNTP=?` / `AT+NWIP=?` / `AT+WFSTAT=?` / `HELP AT+NWHTC` / `HELP AT+TIME` / `HELP AT+NWSNTP` / `HELP AT+DEFAP` / `AT+DEFAP=?` / `AT+GETFASTCONN` | 各命令真实语法 + 自动重连相关旋钮（阶段2/4 硬编码依据） |
| 7 | `AT+TIME` → `AT+DATE` | **风险#2**：时间命令（`AT+TIME` 已实测存在，`AT+DATE` 实测 `ERROR:-1` 不存在） |
| 8 | `AT+TZONE=28800` | 时区单位（DESIGN §5 的假设，实测**确为秒**） |
| 9 | `AT+NWSNTP=1,ntp.aliyun.com,86400` → 等 8s → `AT+TIME` | 出网 + 对时是否生效 |
| 10 | `AT+NWHTC=<心知 now.json>,get` | **风险#1** 正文体量 + **风险#3** 的 `?` `&` |
| 11 | `AT+NWHTC='<心知 now.json>',get` | **风险#3**：单引号包裹是否必需（对比步 10） |
| 12 | `AT+NWHTC=<心知 daily.json>,get` | 更大正文，验证缓冲余量 |
| 13 | `[PROBE] ===== RESULT =====` | 汇总 + `usb_tx_drop` / `LINK:`（须为 0） |

步 10~12 用 `AT_MODE_PAYLOAD`，**引擎会一直采集到 `+NWHTCSTATUS:`**，
响应由 `cap_http()` 转成 `http_cap_t` 特征表输出，**不用肉眼看 `[WIFI]`**。

> ⚠️ **NWHTC 目标只用心知 API，不要用真实网页**。百度首页实测 **667KB**，
> 在 115200 下要推 **60 秒**，会把后续所有命令的输出淹没（2026-10-03 踩过）。
> 且 **同一时刻只能有一条 NWHTC 在途** —— 模块不支持并发。

**`AUTOB` —— 开机自动重连测试，收口风险 #4**

| 步 | 动作 |
|---|---|
| 1 | `AT+WFJAPA=<ssid>,<pwd>` 把 profile 写进 NVRAM |
| 2 | 等 `+WFJAP:1` 30s，确认**存成功**（否则结论无效，判 `INCONCLUSIVE`） |
| 3 | 查询自动重连旋钮：`AT+DEFAP=?` / `AT+GETFASTCONN` / `AT+WFSTAT=?` / `AT+PROVSTAT` |
| 4 | `AT+WFQAP` 断开，状态归零 |
| 5 | `AT+RESTART` 模块重启 |
| 6 | **清 sticky 标志 + 开 `[URC]` 时间戳追踪 → 45s 纯被动监听，期间不发任何命令** |
| 7 | 判定：监听期内是否**自发**出现 `+WFJAP:1` |

> 关键设计：第 6 步先 `at_engine_urc_seen_clear()` 清掉存 profile 阶段那条 `+WFJAP:1`，
> 且用 sticky 记录（8 条）而非 4 槽 FIFO 来判定，**监听期 URC 超过 4 条也不会漏判**。
>
> **为什么关心自动重连**：命令表里有 `AT+DEFAP`（默认 AP）和
> `AT+GETFASTCONN`/`AT+SETFASTCONN`（fast connect），很可能是模块的开机自连机制。
> 若 `AUTOB` 判定为 `NO`，阶段2/4 就必须每次上电都主动 `AT+WFCAP` 或 `AT+WFJAPA`，
> 这会直接影响开机到联网的耗时与 §8.1 根因2 的重试设计。

## 5. 典型 WiFi 测试流程（DA16200）

```
AT+WFMODE=?            查模式，0 = STA
AT+WFMODE=0            切到 Station 模式
AT+RESTART             重启生效，等 +INIT:DONE
AT+WFSCAN              扫描周边 AP
AT+WFJAPA=<ssid>,<密码>    连接并存 NVRAM（密码 8~63 位）
AT+WFCAP               用 NVRAM 里已存的 profile 连接（无 profile → ERROR:-7）
AT+NWIP=?              查 IP → +NWIP:0,<ip>,<mask>,<gw>
AT+TZONE=28800         时区（单位：秒，UTC+8）
AT+NWSNTP=1,ntp.aliyun.com,86400   NTP 校时（验证出网）
AT+TIME                读时间 → +TIME:<date>,<time>
AT+NWHTC=http://api.seniverse.com/v3/weather/now.json?key=<KEY>&location=ip,get
AT+WFQAP               断开
ATF                    恢复出厂（清 NVRAM）
```

连接成功的标志：先回 `OK`，随后异步回 `+WFJAP:1,'<ssid>','<ip>'`。

> ⚠️ NWHTC 目标只用**小响应**的 URL（心知 API ~1KB）。**不要用真实网页** ——
> 百度首页实测 667KB，115200 下要推 60 秒，会把后续输出全部淹没。

### 5.1 DA16200 命令速查（2026-10-03 `AT+` 命令表实测，约 200 条）

阶段2/4 直接相关的：

| 命令 | 用途 | 备注 |
|---|---|---|
| `AT+TIME` / `AT+TIME=<date>,<time>` | 读 / 设时间 | 主页时钟的唯一来源 |
| `AT+TZONE[=<秒>]` | 时区 | **单位是秒**，UTC+8 = `28800` |
| `AT+NWSNTP=<0\|1>[,<server>,<period>]` | SNTP | 也可写 `AT+NWSNTP1` / `AT+NWSNTP2` |
| `AT+NWHTC=<uri>,<get\|post\|put>[,<msg>]` | HTTP 客户端 | **异步流式**，见 §9.1 风险#1 |
| `AT+NWIP[=...]` | IP | 读回 `+NWIP:0,<ip>,<mask>,<gw>` |
| `AT+WFJAPA=<ssid>,<pwd>[,<hidden>]` | 连接并存 NVRAM | 连接成功回 `+WFJAP:1,'<ssid>','<ip>'` |
| `AT+WFCAP` | 用 NVRAM profile 连接 | 无 profile 时 `ERROR:-7` |
| `AT+WFQAP` | 断开 | |
| `AT+NWHTS=1` | DA16200 AT HTTP Server | AT server 按 UM-WI-003 Rev 2.12 p.96–97 要求已连接 AP；本工程不用该命令。实测 SoftAP TCP/80 显示 DA16200 LWIP 页面，RA 门户通过 TCP `TRTS=8080` 提供；访问 `http://192.168.1.100:8080/`，无需改 SDK 固件 |
| **`AT+DEFAP`** | 默认 AP | **可能是开机自连机制**，待 `AUTOB` 验 |
| **`AT+GETFASTCONN` / `AT+SETFASTCONN`** | fast connect | **可能是开机自连机制**，待 `AUTOB` 验 |
| `AT+WFSTAT` / `AT+WFSTA` / `AT+WFRSSI` / `AT+WFLCST` | 状态 / 已连站点 | `WFLCST` 可作 §8.1 的回读校验 |
| `AT+WFMODE` / `AT+WFSAP` | 设置 DA16200 SoftAP mode/profile | UM-WI-003 第49–50页；`WFSAP` 在 STA 模式配置后需重启才启动 SoftAP |
| `AT+TCPDATAMODE` / `AT+TRTS=<port>` | TCP 数据模式 / TCP Server 监听 | SoftAP + TCP Server 已实测通过，无需改 SDK 固件。SoftAP 下 `TRTS=8080` 返回 OK，手机已成功接入；该用法见 R01AN7287EJ0100 §2.1。UM-WI-003 p.112 有 connected-to-AP prerequisite 表述。服务器事件 `+TRCTS`/`+TRXTS` (p.114–115)，收包/回包 `+TRDTS`/`<ESC>S` (p.116–117)。|
| 配网网页 HTTP 承载 | 已实测：AT SoftAP TCP Server + RA HTTP | `doc` 保存的 `33 - 副本.txt` 实机日志确认 RA 收到浏览器 `GET /`、`GET /favicon.ico` 和 `POST /cfg`，提交后 `+WFJAP:1` 成功回 STA。**端口 80 是 DA16200 LWIP 页面；本项目 RA 配网页面用 8080**，手机需手动打开 `http://192.168.1.100:8080/`，不要用 80 或 captive 自动弹页。 |
| `<ESC>S` | TCP client 数据发送 | 实际 ESC 字节 `0x1B` + `S`，后续字段及数据格式依 UM-WI-003 第116–117页 |
| `AT+NWPING` / `AT+NWHOST` | 出网诊断 | |

#### 5.1.1 SoftAP 网页配网实测结果与验收步骤

实测依据：用户提供的桌面日志（build `Oct 16 2026 13:14:21`）。日志确认手机 TCP 接入（`+TRCTS`）、GET `/`、GET `/favicon.ico`、POST `/cfg` 均到达 RA，HTTP POST 回复完成后执行 TCP close / SoftAP teardown / STA restart，最终收到 `+WFJAP:1,<SSID>,<IP>`。原始日志由用户保存在桌面，没有复制进仓库，避免纳入私人 Wi-Fi 信息。

已通过的功能链路无需再从头排查；后续按需复测：

1. 在 RA 控制台输入 `PORTALTEST`，应看到 `[PORTALTEST] PASS`。该命令只验证本地 HTTP 表单、Content-Length 和 TCP hex framing，不替代实机测试。
2. 长按 P005 进入配网；手机连接设备当前显示的 SoftAP SSID / 密码后，必须手动输入 **`http://192.168.1.100:8080/`**。端口区别已经实机确认：`http://192.168.1.100/`（TCP/80）显示 DA16200 的 LWIP 页面；`http://192.168.1.100:8080/`（TCP/8080）才是 RA 提供的配置页。手机 captive popup 可能探测 80 端口，不要用它判断 RA 页面。
3. 如需复核链路，观察 `+TRCTS`、GET 对应的 `+TRDTS`，以及提交 `/cfg` 后的 STA 回连结果；出现 `+WFJAP:1` 表示 Wi-Fi 已连上。

仍需完成的边界验收：错误密码、空字段、带空格/百分号编码的城市及编码的 `&`/`=` 密码；检查失败时不误报连接成功、状态机可重新进入。含逗号/单引号的 SSID/密码当前会拒绝（AT 参数为逗号分隔）。Data Flash 双槽代码已实现并编译，但实际写入、重启读回和断电保留尚未完成板测，因此阶段4尚未全部验收。不要手工执行 `ATF` / `AT+PROVSTART`，两者会清除 NVRAM 配置。

#### SSID / 密码 / 城市的 RA Data Flash 持久化

用户要求 Wi-Fi SSID、密码、城市**全部以 RA4M2 Data Flash 作为设置来源**；DA16200 NVRAM profile 只保留为模组连接机制，不作为 RA 设置的唯一副本。`configuration.xml` 已配置 `r_flash_hp` / `g_flash0`（Data Flash programming enabled，BGO disabled）；当前生成的 `ra_gen/hal_data.*` 和 FSP driver 已纳入工程并完成编译链接。RASC 重新生成时需检查 QE Touch/CTSU 配置，避免覆盖当前已验证触摸配置。个人测试 SSID/密码及天气 API Key 放在被忽略的 `src/weather_secrets.h`；从 `src/weather_secrets.example.h` 复制后本地填写，不得提交。

实现保留 Data Flash 末尾 `0x08001E80–0x08001FFF`（384 字节），分两个 192-byte slot；记录含 magic/version/sequence/SSID/password/city/CRC/commit。启动只读、无有效记录时使用编译默认设置但不擦写；配置提交时写非当前槽并读回校验，保留上一有效槽。仍需先确认该地址区无其他用途，再实机验证首次空白、写入/读回、二次更新和断电恢复；保存成功页面最终回执及串口密码脱敏也待完善。

连接成功时序（实测）：
```
+WFJAP:1,'CMCC-xxxx',192.168.1.6     ← 连上
+WFDAP:0,DEAUTH                      ← 连接时也会出现
```

| 码 | 含义 | 码 | 含义 |
|---|---|---|---|
| -1 | 未知命令 | -5 | 功能不支持（如非 STA 模式下发 WFJAPA） |
| -2 | 参数太少 | -6 | 未连接 AP |
| -3 | 参数太多 | -7 | 无结果 |
| -4 | 参数值错误 | -9 | 功能未配置 |

### 5.2 OLED 坐标约定（**极易搞错，曾因此返工**）

`OLED_Set_Pos(x, y)` 实现是 `0xb0 + y` —— **`y` 是页号 0~3，不是像素纵坐标**。

| 参数 | 含义 |
|---|---|
| `x` | 列 0~127 |
| `y` | **页号 0~3**（每页 8 像素），128x32 屏共 4 页 |
| `sizey` | **只能 8 或 16**。8→`asc2_0806`，6px 宽，占 1 页，每字符 `x+=6`；16→`asc2_1608`，8px 宽，占 2 页，每字符 `x+=8` |
| 其它值（如 12） | `OLED_ShowChar` 直接 `return`，**什么都不显示且不报错** |

视觉排布：`page0~1` = 第一行（16px 高），`page2~3` = 第二行（16px 高）。
也可每页 8px 用 `sizey=8` 排 4 行小字（WiFi 信息页就是这么做的）。

### 5.3 字库与字符集

`oledfont.h` 的 `Hzk[]`（16x16）按 **GB2312 顺序**索引，GBK 双字节码 = `(区-16)*94+(位-1)` 线性映射。而心知 API 返回 **UTF-8**（响应头 `charset=utf-8`），所以上中文前必须转码：

```
心知 API ──UTF-8──> cJSON ──code查表──> 英文文案(ASCII) ──> OLED
```

- 天气解析以 `code` 作为稳定的天气类别/图标依据。当前 `WEATHER_LANG="en"` 时 API 的 `now.text` 是 ASCII，主页直接显示该字段，以保证屏显与 API/`[WX-RESULT]` 一致（例如 Foggy）；非英文时继续走本地 code 文案表，避免把 UTF-8 文案直接送入 GBK OLED 字库。
- 需要上中文时用 `oled_utf8_to_gbk()` 转码后查 `oled_cn_1212`/`oled_cn_1616`。
- `oledfont.h` 自带的 `Hzk[]` 只有 7 个汉字，不够用；扩充用：
  ```
  pip install pillow
  python tools/glyph2c.py --ttf %WINDIR%/Fonts/simsun.ttc --size 16 \
         --out src/oled_font_data/oled_cn_font.c
  ```
  脚本内置天气站字符集（按页面归类，便于校对）。

### 5.4 第一阶段固定配置与串口 OLED 模拟

- 开发者本地凭据放在忽略的 `src/weather_secrets.h`；模板为 `src/weather_secrets.example.h`。发布源码不包含个人 STA SSID/密码或真实天气 API Key。
- `WEATHER_UI_SERIAL_SIM=1` 时，`hal_entry()` 跳过 `oled_port_init()`；`weather_ui.c` 将主页和 WiFi 信息页输出为 `[OLED] PAGE=...` 及 `L0`–`L3` 四行。每行最多 21 个 ASCII 字符，对应 128×32 屏使用 6 像素宽字体时的可视宽度。
- 主页内容：IP 定位标记、实时天气/温度、实时湿度/风速、今日低温/高温和时间。实时数据来自 `now.json`，今日温度范围由其后的 `daily.json` 补充；daily 请求失败不影响实时天气显示。
- WiFi 信息页显示 SSID、当前 IP 和连接状态；按键翻页行为与主状态机相同。
- 设置 `WEATHER_UI_SERIAL_SIM=0` 可启用 OLED 硬件初始化与绘制。OLED 未连接时保持 `1`，避免访问未接硬件。

## 6. 编译

```
workdir: Debug
命令: "%FSP_ROOT%\eclipse\plugins\com.renesas.ide.exttools.gnumake.win32.x86_64_4.3.1.v20240909-0854\mk\make.exe" all
```

产物：`Debug/4.weather_app.elf`、`Debug/4.weather_app.hex`。

代码规模（当前，阶段1.5）：`text 29296, data 104, bss 37936`（RAM 128KB，充裕）。

## 7. 关键配置/背景知识

- `BSP_CFG_PARAM_CHECKING_ENABLE = 0`（ra_cfg/fsp_cfg/bsp/bsp_cfg.h）：FSP 参数检查关闭，对未 Open 的串口句柄调用写接口会因 `p_reg == NULL` 直接 HardFault，因此两个串口必须先成功 `Open`。
- DA16200 的 AT 命令手册：UM-WI-003；`HELP <命令>` 可让固件自报真实语法（以固件自报为准，不同 SDK 版本语法有差异）。
- WPA2 口令必须 8~63 字符；SSID/口令含逗号或单引号时需用单引号整体包住。

### 8.0 构建：源文件清单在 `Debug/src/subdir.mk`（e2studio 自动生成）

新增 `.c` 文件后**必须**让 `Debug/src/subdir.mk` 同步，否则不会被编译。需更新三处：

| 段落 | 内容 |
|---|---|
| `C_SRCS` / `C_DEPS` / `OBJS` | 加入 `../src/<path>.c` / `./src/<path>.d` / `./src/<path>.o` |
| 模式规则 | 若源文件在子目录（`oled/`、`gpio_spi/`、`bsp_hal/`、`oled_font_data/`），需为该子目录补一条 `src/<dir>/%.o: ../src/<dir>/%.c` 规则，并**先创建 `Debug/src/<dir>/` 空目录**，否则报 `open: src/<dir>/xxx.o.in: No such file or directory` |

行尾必须是 **CRLF**（混入裸 LF 会报 `missing separator`）。

> 长期方案：改用 e2studio 的 `sourceEntries` 正式注册源文件，让 IDE 重新生成 makefile，
> 免手工维护。当前 `.cproject` 里 `sourceEntries` 为空，故走手工维护路线。

### 8.0.1 版本管理（git）

本地 git 仓库，每次设计或实现变更后提交。

```
git add -A
git commit      # 提交信息要说明【为什么】改，而不只是改了什么
```

| 文件 | 作用 |
|---|---|
| `.gitignore` | 忽略 `Debug/`、`log/`、`AUTO*.txt`、`1s.json`、`.settings/`；**刻意提交** `ra/`（FSP SDK，5.3MB）、`ra_gen/`、`ra_cfg/`、e2studio 工程文件、`tools/` |
| `.gitattributes` | **关键**：本工程有 GBK 文件，若 git 做 CRLF 转换会改字节、破坏汉字。故把 `oledfont.h` / `oled_font_data.c` / `weather_text.c` / `weather_text.h` 标 `binary`；其余标 `text eol=lf`。`core.autocrlf=false` |

## 8. 文件结构

```
4.weather_app/
├── DESIGN.md              设计文档（总体方案/风险台账/实施阶段）—— 改设计先改这里
├── src/hal_entry.c        应用主逻辑（回调、环形缓冲、USB TX 泵、AUTO/AUTOB/ABORT、字符集自检）
├── src/at_engine.c/.h     异步两段式 AT 引擎 + CMD/PAYLOAD 双模式 + URC sticky/时间戳追踪
├── src/at_probe.c/.h      AUTO 全链路诊断 + AUTOB 开机重连测试 + NWHTC 响应分析
├── src/weather_cfg.h      用户配置（SSID/口令/API key/城市/时区/NTP/超时）
│
├── src/oled_port.c/.h     OLED 移植层（**原样来自 RA4M2OLED 示例**）
├── src/gpio_spi/          软 SPI 驱动（**原样来自示例**）
├── src/bsp_hal/           hal_delay（**原样来自示例**）
├── src/oled/oled.c/.h     SSD1306 驱动（**原样来自示例**，回调注入架构）
├── src/oled/oledfont.h    ASCII + Hzk 点阵声明（GBK 无 BOM，extern 声明）
├── src/oled/oled_font_data.c  点阵数据唯一定义处（原 oledfont.h 的数据）
├── src/oled/oled_font.c/.h    字库查表 + UTF-8⇄GBK 转换（**本工程新增**）
├── src/oled_font_data/    可扩充汉字表（占位，由 tools/glyph2c.py 生成）
│
├── src/weather_ui.c/.h    主页与 WiFi 信息页；支持串口四行模拟或 OLED 绘制
├── src/weather_text.c/.h  心知 code→文案/图标（**原样移植 GD32 版**，GBK 无 BOM）
├── src/weather_parse.c/.h JSON 解析（now 实况与 daily 今日温度范围，依赖 cJSON）
├── src/wifi_status.h      wifi_status_t 五态（从 GD32 wifi_prov.h 抽出，平台无关）
├── tools/glyph2c.py       TTF → 汉字点阵生成器
│
├── src/hal_warmstart.c    启动warm start（FSP生成）
├── ra_gen/                FSP 配置生成代码（hal_data、pin_data 等）
├── ra_cfg/                FSP 配置参数（含参数检查开关）
├── ra/                    FSP 驱动与 CMSIS 库
├── script/fsp.ld          链接脚本
├── Debug/                 构建产物与 makefile
├── doc/                   本说明文档
├── .project / .cproject   e2studio 工程文件
└── configuration.xml      FSP 配置快照
```

## 9. 阶段1 风险台账

### 9.1 已清零（2026-10-03 上板实测）

| 风险# | 项 | 结论 |
|---|---|---|
| **1** | `AT+NWHTC` 正文格式 | **异步流式协议，与原假设不同**（见下表）。结束标志是 `+NWHTCSTATUS:<n>`，**不是 `OK`**；**含 HTTP 响应头，必须先切 `\r\n\r\n`** |
| **2** | 时间命令 | **`AT+TIME`** 读 / `AT+TIME=<date>,<time>` 写；`AT+TZONE` 单位**确为秒**（`+TZONE:28800`）；SNTP 生效，`+TIME:2026-10-03,11:30:51` 是真实本地时间；`AT+DATE` 不存在（`ERROR:-1`） |
| **5** | 长响应完整性 | **是丢字节不是截断**，已修并**验证通过** |

**风险#1 的真实时序**（实测）：

```
[AT>] AT+NWHTC=<url>,get
[WIFI] OK                  ← 仅 6 字节的同步回执，不是结束
[WIFI] Set-Cookie: ...      ← HTTP 响应头（确实存在）
[WIFI] Connection: close
[WIFI] <!DOCTYPE html>...   ← 正文
[WIFI] +NWHTCSTATUS:0       ← 真正的结束标志
```

推论：① 引擎需 `AT_MODE_PAYLOAD`（收到 `OK` 不收口）；② 解析前切 `\r\n\r\n`；
③ 缓冲要够 —— **百度首页实测 667KB / 推 60s**，4KB 只够心知这类小 JSON；
④ 模块**不支持 NWHTC 并发**，同一时刻只能一条在途。

**风险#5 的根因与修法**：根因 = `__io_putchar` 逐字节忙等 TX 完成，与 115200 入向速率
零余量 → WIFI RX 环（当时 512B）溢出静默丢。修法 = USB TX 环 4KB 非阻塞
`usb_tx_pump()`（**必须三指针** `wr`/`rd`/`done`，区分"已入队给硬件"与"硬件已发完"，
否则会无限重发同一段）+ WIFI RX 环 8KB。验证：`AT+WFSCAN` 15 条 AP 全部 BSSID 完整
6 段、零缺段零粘行（修复前 20 条有 10 处硬伤），`wifi_rx_drop=0`、`usb_tx_drop=0`。

### 9.2 待测（跑 `AUTO` / `AUTOB` 后回填）

| 风险# | 待确认项 | 怎么测 | 结果 |
|---|---|---|---|
| **3** | `AT+NWHTC=<URL>` 对含 `?` `&` 的 URL 是否需单引号包裹 | `AUTO` 步 10 vs 11 对比（上一轮作废：三条命令在 667KB payload 推送期间并发发出） | 待测 |
| **4** | **开机自动重连**：NVRAM 有 profile 时上电是否自发 `+WFJAP:1` | `AUTOB`（45s 纯被动监听 + `AT+DEFAP`/`AT+GETFASTCONN` 查询） | 待测 |
| — | 心知 JSON 的实际 `body_len`，4KB 是否够 | `AUTO` 步 10 的 `body_len`/`ovf` | 待测 |
| — | `AT+PROVSTART`/`AT+PROVSTAT`（模块自带配网）能否替代自建 HTTP 服务 | `HELP AT+PROVSTART`（阶段4 决策点） | 待测 |
| — | `AT+TZONE` 改时区后 `AT+TIME` 是否随之偏移（验证单位语义） | 手工 `AT+TZONE=0` → `AT+TIME` → `AT+TZONE=28800` → `AT+TIME` | 待测 |
