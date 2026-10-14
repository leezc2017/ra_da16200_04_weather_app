# AGENTS.md

RA4M2 天气显示工程：OLED 显示 + WiFi（DA16200 AT 指令）+ 天气 API HTTP 解析。
目标是**不依赖 e² studio**，用 VS Code 完成编译、烧写、调试与寄存器查看。

工程文档在 `doc/README.md`（28 KB）和 `DESIGN.md`（35 KB）。

## 硬件

| 项 | 值 |
|---|---|
| MCU | R7FA4M2AD（Cortex-M33 r0p4，512 KB Flash） |
| 调试探针 | WCH-Link，ARM-SWD / WinUSB 模式，UID `<probe-uid>` |
| 探针串口 | WCH-Link CDC ACM → COM87（**本工程不用**） |
| WiFi 模组 | DA16200，走 SCI0 + AGT + ICU |
| 显示 | SPI OLED（`src/gpio_spi`、`src/oled`） |
| 内存 | 链接结果 `text 50284 / data 480 / bss 41738` |
| 工程模式 | **Flat**（非 TrustZone 双镜像）——pyOCD 只支持 Flat |

## 引脚分配（来自 `ra_gen/pin_data.c`，权威）

| 引脚 | 外设 | 用途 |
|---|---|---|
| P108 | `IOPORT_PERIPHERAL_DEBUG` | SWDIO / TMS |
| P300 | `IOPORT_PERIPHERAL_DEBUG` | SWCLK / TCK |
| **P109** | `SCI1_3_5_7_9` | **SCI9 TXD9 —— printf 输出** |
| **P110** | `SCI1_3_5_9` | SCI9 RXD9 |
| P410 / P411 | `SCI0_2_4_6_8` | SCI0（DA16200 AT 命令） |
| P005 / P006 | GPIO input | 机械按键（阶段2） |
| P207 | `IOPORT_PERIPHERAL_CTSU` | TSCAP 公共电容 |
| P415 | `IOPORT_PERIPHERAL_CTSU` | CTSU TS11（自容式） |
| P708 | `IOPORT_PERIPHERAL_CTSU` | CTSU TS12（自容式） |

## 串口 / printf

- 控制台实例叫 **`g_usb_uart`**，SCI **channel 9**，与上一工程同一条物理口。
- `printf` 经 `src/hal_entry.c` 的 `_write()` → `__io_putchar`（或 `fputc`，
  由 `PUTCHAR_PROTOTYPE` 切换）输出到 **SCI9（P109）**。
- SCI9 走**板载 USB 串口**，插板子自己的 USB 即可，不经过 WCH-Link。
- WCH-Link 的 COM87 需要额外接线才收得到，本工程**不接**。
- **SWO trace 不可用**：WCH-LinkE 的 ARM-SWD 模式没有 SWO 输出。
  trace 只有串口 printf 或 SEGGER RTT。
- 工程**未**链接 `--specs=rdimon.specs`；`hal_entry.c` 只覆盖 `_write`，
  `_sbrk` 用 FSP 的 `ra/fsp/src/bsp/mcu/all/bsp_sbrk.c`。

### 主循环的协作式调度（改动时注意）

`hal_entry.c` 用 `putchar()` 当空操作来给各轮询任务打拍：

```c
putchar('U'); usb_uart_rx_process();   /* USB 输入转 WIFI 串口 */
putchar('W'); wifi_uart_rx_process();  /* WIFI 响应回灌 USB */
putchar('T'); usb_tx_pump();           /* 泵 USB TX（实际才调 R_SCI_UART_Write）*/
putchar('K'); at_engine_tick(TICK_MS); /* AT 指令状态机 */
putchar('P'); at_probe_poll();         /* AUTO / AUTOB 探测 */
```

`__io_putchar` 本身是空的，它只是**节拍占位符**，真正的发送在
`usb_tx_pump()`。之前曾因每次 `__io_putchar` 都等 `TX_COMPLETE` 导致
115200 下输出卡顿，改成缓冲 + 泵送才解决。**不要把 `putchar` 改回阻塞式。**

`usb_tx_pump()` 调 `R_SCI_UART_Write` 前若上一包未完成会拿到
`FSP_ERR_IN_USE`，代码里有处理，改动时留意。

## 编译

```powershell
$mk = "$env:LOCALAPPDATA\Programs\Renesas\RA\e2studio_v2026-07_fsp_v6.6.0\eclipse\plugins\com.renesas.ide.exttools.gnumake.win32.x86_64_4.3.1.v20240909-0854\mk\make.exe"
& $mk -j8 all      # 在 Debug\ 目录下执行
```

- 复用 CDT 生成的 makefile（`Debug\makefile` + 各 `subdir.mk`），**不需要 EIDE**。
- 工具链：Arm GNU GCC 13.2.1（`.../e2studio_v2026-07_fsp_v6.6.0/toolchains/gcc_arm/arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi/bin`）。
- GCC / busybox / make 的路径由 `Debug/makefile.init` 自动注入 PATH，
  调用方只需提供 make.exe 的路径。
- 关键编译选项：`-mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 -O2 -gdwarf-4 -fno-strict-aliasing`
- 关键宏：`-D_RA_CORE=CM33 -D_RENESAS_RA_`
- 链接脚本：`-T fsp.ld` → `script/fsp.ld` → `INCLUDE memory_regions.ld` + `INCLUDE fsp_gen.ld`
- 产物：`Debug/4.weather_app.elf`、`.hex`、`.map`、`.rpd`、`.sbd`

## 烧写 / 调试（pyOCD）

```powershell
& pyocd list -v
& pyocd load  -t r7fa4m2ad -u <probe-uid> -f 4000000 ".\Debug\4.weather_app.elf"
& pyocd erase -t r7fa4m2ad -u <probe-uid> -f 4000000
& pyocd gdbserver -t r7fa4m2ad -u <probe-uid> -f 4000000
```

- **pyOCD 不在系统 PATH**：`pip install pyocd` 后把 Python 的 `Scripts` 目录加进 PATH，之后直接写 `pyocd`。
- **target 名必须手写**：WCH-Link 不上报目标信息，`pyocd list` 的 Target 列恒为 `n/a`。
- target 名按 flash 大小区分：`r7fa4m2ab` = 256 KB / `r7fa4m2ac` = 384 KB /
  `r7fa4m2ad` = 512 KB。本工程用 `r7fa4m2ad`。
- RA_DFP **6.6.0** 已装在 pyOCD 的 pack 缓存里，flash loader 来自它。
- SWD 频率 **4000 kHz**。不稳定就降回 1000。
- VS Code：`F5` 走 Cortex-Debug（`.vscode/launch.json`），`Ctrl+Shift+B` 编译，
  其他任务见 `.vscode/tasks.json`。
- 寄存器视图用 `.vscode/svd/R7FA4M2AD.svd`（从 RA_DFP 6.6.0 抽出）。

## 哪些文件不能手改

| 路径 | 原因 |
|---|---|
| `ra_gen/` | RASC 生成（pin/hal/vector/common data）。**注意 `pin_data.c` 里有人工补充的中文注释，重新生成会丢失注释**（引脚配置本身在 `configuration.xml` 里，会正确再生） |
| `Debug/` | 构建产物，含 makefile、`compile_commands.json`、`fsp_gen.ld`、`memory_regions.ld` |
| `.cproject` | 工具链生成 |
| `ra/fsp/` | FSP 库源码，随 FSP 版本升级，不要改 |
| `ra_cfg/fsp_cfg/`、`ra_cfg.txt` | RASC 生成 |
| `1s.json`、`AUTO1.txt`、`log/` | **抓取/日志留存，不要动**（`1s.json` 2.8 MB 是天气 API 响应样本；`log/AUTO.txt`、`log/AUTOB.txt` 是 AT 探测串口记录） |

要改引脚、时钟或外设配置，改 `configuration.xml`，再用 RASC 重新生成。
已配置的项包括 `p005.input`、`p006.input`、`ctsu0.mode.enabled`、
`ctsu0.ts11.p415`、`ctsu0.ts12.p708`、`ctsu0.tscap.p207`。

## 硬件阶段划分（源码注释里的约定）

- **阶段2**：机械按键 —— P005 / P006，**已在 FSP 里配置为 GPIO input**。
  之前未配置时 `R_BSP_PinRead` 读到未定义电平，4 键全无反应（2026-10-04 实测）。
- **阶段3**：CTSU 触摸 —— TSCAP(P207) + TS11(P415) + TS12(P708)。
  **触摸不是 GPIO，读电平无效**，必须由 `r_ctsu` + `rm_touch` 扫描，目前尚未接入。

新增硬件阶段时，请同步更新 `pin_data.c` 的注释与本文件。

## 故障排查

| 现象 | 处理 |
|---|---|
| `No available debug probes are connected` | WCH-Link 被 WCH-LinkUtility / MounRiver Studio 独占，关掉再试 |
| `Waiting for a debug probe matching unique ID` | 同上，或 USB 接触 / 供电问题；换 USB 口 |
| 探针列出但 Target 为 `n/a` | 正常，不是故障 |
| `Target type r7fa4m2ad not recognized` | pyOCD 版本过旧，或 RA_DFP 未安装（`pyocd pack show` 应显示 6.6.0） |
| DAP 能连但写 PC 失败 | 多半 DLM 被锁（`LCK_DBG` / `LCK_BOOT`），需 RFP 进 boot mode 做 factory reset |
| **`monitor reset` 后固件 SIGSEGV** | **平台级已知问题，调试流程必须避开 reset。** 崩在 `system.c:154` 的 `.data` memcpy → `Default_Handler`；**只复位不下载也崩**，而 USB 上电复位正常。详见下方「调试流程约束」 |
| **`monitor reset hw` 后探针消失** | nRESET 未接目标板，那一下打在 WCH-Link 自己身上。**不要用** |
| 断 `main` 永远不命中 | 烧写后固件已在运行，`main` 早过了。改断周期性函数（见 launch.json 配置②③） |
| 断某个 static 函数不命中 | `-O2` 会内联，`static` 函数可能没有 out-of-line 副本。用 `objdump -d` 确认有 `bl <地址>` 再设断点。已知 `usb_tx_pump` 被完全内联 |

## 调试流程约束（重要）

**不要用 `request: "launch"` 的调试配置。** 本平台上 GDB 的复位动作会让固件崩溃：

| 动作 | 结果 |
|---|---|
| USB 拔插上电 | ✅ 正常启动 |
| `pyocd load`（任务 `Flash: pyOCD load ELF`） | ✅ 正常启动 |
| GDB `monitor reset` / `reset halt` | ❌ SIGSEGV |
| GDB `monitor reset hw` | ❌ 探针掉线 |

`monitor reset` 崩溃点固定在 `ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.c:154`：

```c
memcpy(g_init_info.p_copy_list[i].p_base, g_init_info.p_copy_list[i].p_load, ...)
```

即 `SystemRuntimeInit()` 里把 `.data` 从 flash 拷到 SRAM 那一步，失败后落到 `startup.c:70`
的 `BSP_CFG_HANDLE_UNRECOVERABLE_ERROR(0)`。已排除的因素：vector table 正确
（SP=`0x200025e0`、Reset=`0x5665`）、`g_init_info` 内容有效
（`zero_count=1, copy_count=3`）。怀疑与 TrustZone 热复位后 SAR / MPU 状态有关，
**未定位根因**。

### 正确工作流

```text
1) 编译   Ctrl+Shift+B
2) 烧写   任务 Flash: pyOCD load ELF      ← pyocd 内部复位，不触发上述问题
3) 调试   launch.json 配置 ①（attach）     ← 不复位、不下载
```

附加调试时不要断 `main`，用周期性函数：`at_engine_tick`（主循环每轮调用）、
`take_time`（解析 `+TIME:` 响应，已验证会命中）。

### 探针掉线

`monitor reset hw` 之后可能出现 `No available debug probes are connected`，
这是探针被复位掉线，**拔插 USB 即可恢复**，不是固件问题。
| 串口输出断断续续 / 卡顿 | 检查 `usb_tx_pump` 是否被改回阻塞式 `__io_putchar` |
| GDB 输出 `could not convert 'xxx' from the host encoding (CP1252) to UTF-32` | **无害**。源码注释是 GBK、GDB 控制台是 CP1252所致；已实测 `set host-charset` 取 UTF-8/GBK/CP936/CP1252/ISO-8859-1/ASCII 均无法消除，但断点地址每次都能正确解析（`usb_tx_pump` → `0x1e48`），不要为此改动配置 |

## 验证

改动代码后至少跑一次：

```powershell
cd Debug
& $mk -j8 all          # 必须 exit 0
& pyocd.exe load -t r7fa4m2ad -u <probe-uid> -f 4000000 .\4.weather_app.elf
```

有物理板子时确认：烧录后板载 USB 串口能看到 `[PROBE]` / `[AT>]` / `[ENG]` 前缀的
调试输出，且 COM87 **不应**有输出。