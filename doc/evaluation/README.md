# RA4M2 / DA16200 评估报告索引

本目录按四个实际工程分别给出评估结论，覆盖 RA4M2 GPIO/CTSU 外设 demo、DA16200 AT 透传桥和天气集成工程。报告结论以工程源码、`configuration.xml`、生成的 `ra_gen/`/`ra_cfg/`、CDT make rules 和实际构建记录为依据。

| 报告 | 工程 | 主要评估内容 |
|---|---|---|
| [01 - RA4M2 LED Demo](01_RA4M2LED.md) | `..\RA4M2LED` | GPIO、FSP 生成配置、LED 与 GCC 构建 |
| [02 - RA4M2 Touch Demo](02_RA4M2TOUCH.md) | `..\RA4M2TOUCH` | CTSU/RM_TOUCH/QE Touch、引脚、阈值与构建 |
| [03 - DA16200 AT Bridge](03_DA16200_AT_Bridge.md) | `..\da16200_at_bridge` | RA4M2 UART 桥、DA16200 AT 使用边界和阻塞式设计风险 |
| [04 - Weather App Integration](04_Weather_App.md) | `.` | RA4M2 + DA16200 完整架构、配网、Flash、构建和发布风险 |

## 本次构建核验摘要

| 工程 | 本次 clean build | 结果/备注 |
|---|---|---|
| RA4M2LED | GCC 13.2.1, `make -B -j8 all` | 通过；FSP 3.2.0 工程，当前编译器兼容构建不代表配置包已升级 |
| RA4M2TOUCH | GCC 13.2.1, `make -B -j8 all` | 链接通过；存在 `qe_touch_main()` 缺少原型 warning；阈值未做完整硬件验收 |
| da16200_at_bridge | 同一命令 | 未能从当前工作区 clean build：makefile.init/compiler/include paths 指向原开发机目录；已有 ELF 不算可复现构建 |
| 4.weather_app | GCC 13.2.1, `make -B -j8 all` | 全量通过，含 `ra_gen`、`qe_gen` 和 `r_flash_hp`；Flash 擦写/掉电行为仍待硬件验证 |

LED/Touch/Weather 的 FSP 源码和 GCC command line 在本机核验；Bridge 的限制是工程元数据绝对路径，不是已证明的源码编译错误。所有项目的板级外设结果应分别按对应报告中的验收边界理解。

## 评估边界

- 这里的“构建通过”表示工作区现有 make rules 与安装的编译器下实际链接通过，不等同于功能/硬件验收。
- e² studio / RASC 生成的 `Debug/` makefiles、`ra_gen/`、`ra_cfg/`、FSP 源码必须成套检查；只升级一部分会造成 ABI、头文件或驱动规则不匹配。
- 天气工程报告不包含发布用 ELF/HEX；个人 `src/weather_secrets.h` 必须由开发者本地创建且不能提交。
- 对外发布时不得嵌入个人 Seniverse Key。MCU 固件中的 API Key 可从 ELF/HEX 逆向提取，建议由受控服务端代理天气 API。
- `assets/*.svg` 是根据工程引脚/配置/代码绘制的报告示意图，非原始 e² studio 截图、示波器截图或板级测试证据。
