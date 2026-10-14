# DA16200 AT 命令 ②：获取 IP

> 前置条件：**已连上 AP**。若尚未连接，先看 `AT_01_连接WiFi.md`。
> 证据等级标记（[M]/[L]/[L2]）同 ① 文档第 0 节。

---

## 1. 完整可复制序列（获取 IP）

```
AT
AT+WFSTA                 ← 确认已连（期望 +WFSTA:1 + OK）
AT+NWIP                  ← 读 IP（裸发）
AT+NWIP=?                ← 读 IP（查询形式，二者等价）
AT+NWDHC                 ← 看 DHCP 客户端状态
AT+NWDNS                 ← 看 DNS 服务器
AT+NWHOST=api.seniverse.com   ← DNS 解析，验证出网能力
AT+NWPING=0,192.168.1.1,4     ← ping 网关，验证链路
AT+WFRSSI                ← 查信号（连上才有意义）
```

**成功判据**

| # | 命令 | 期望输出 |
| --- | --- | --- |
| 1 | `AT+WFSTA` | `+WFSTA:1` + `OK` |
| 2 | `AT+NWIP` | `+NWIP:0,192.168.1.6,255.255.255.0,192.168.1.1` + `OK` |
| 3 | `AT+NWPING=0,192.168.1.1,4` | `+NWPING:4,4,0,0,0` + `OK`（4 发 4 收） |
| 4 | `AT+NWHOST=api.seniverse.com` | `+NWHOST:<ip>` + `OK` |

---

## 2. 逐条命令详解

### 2.1 `AT+NWIP` — 读 IP（**核心命令**）

**[M] Table 9「Network function command list」，Rev.3.5 p.37**

```
AT+NWIP=<iface>,<ip_addr>,<netmask>,<gw>      ← 设置静态 IP
  <iface>:  WLAN 接口. 0 (WLAN0, STA) , 1 (WLAN1, Soft AP)
  Response: OK or ERROR

AT+NWIP                                        ← 裸发 = 读
AT+NWIP=?                                      ← 查询形式
  Response: +NWIP:<iface>,<ip_addr>,<netmask>,<gw>

手册示例：
  AT+NWIP=0,192.168.0.100,255.255.255.0,192.168.0.1
  OK
  AT+NWIP
  +NWIP:0,192.168.0.100,255.255.255.0,192.168.0.1
  OK
  At+NWIP=?
  +NWIP:0,192.168.0.100,255.255.255.0,192.168.0.1
  OK

Note:
  · Enabled by default in the SDK.
  · The configuration is stored in NVRAM.
```

**[L] 本机实测（2026-10-03）**

```
[AT>] AT+NWIP=?
[WIFI] +NWIP:0,192.168.1.6,255.255.255.0,192.168.1.1
[WIFI] OK
```

#### ⚠️ 字段读法（最容易踩的坑）

```
+NWIP: <iface> , <ip> , <netmask> , <gateway>
         ↑
         第 1 个字段是【接口号】，不是 IP！
         0 = WLAN0（Station 模式，客户端）
         1 = WLAN1（Soft-AP 模式，热点自己）
```

| 观测值 | 含义 |
| --- | --- |
| `+NWIP:0,192.168.1.6,255.255.255.0,192.168.1.1` | ✅ STA 口，从路由器 DHCP 拿到的正常地址 |
| `+NWIP:1,10.0.0.1,255.255.255.0,10.0.0.1` | ⚠️ 读的是 **Soft-AP 口**。`10.0.0.1` 是 `AT+DEFAP` 定义的出厂热点默认 IP<br>**[M]** `HELP AT+DEFAP` 实测回显：`Factory setting(AP mode) --- SSID:DA16200_XXXXXX, Auth:WPA2/CCMP, IP:10.0.0.1, Enable DHCP` |
| `+NWIP:0,0.0.0.0,...` | STA 口没拿到地址 → DHCP 没跑或没连上 |

> **判"IP 到底有没有拿到"时，必须同时看第 1 字段是不是 `0`，以及第 2 字段不是 `0.0.0.0`。**

---

### 2.2 `AT+NWDHC` — DHCP 客户端

**[M] p.39**

```
AT+NWDHC=<dhcpc>     <dhcpc>: 0 (stop), 1 (start)
  Response: OK or ERROR
AT+NWDHC             →  +NWDHC:<dhcpc>
  Prerequisite: The DA16200/DA16600 should be connected to AP.
  Note: 存 NVRAM。

示例：
  AT+NWDHC=1
  OK
  AT+NWDHC
  +NWDHC:1
  OK
```

**补充 [M] p.39 `AT+NWDHCLT` — 租约信息**

```
AT+NWDHCLT
  +NWDHCLT:<lease time>,<renew time>,<elapse time>
  OK
示例：AT+NWDHCLT → +NWDHCLT:86400,43200,13
  Prerequisite: DHCP client connected to the DHCP server.
```

> 默认 DHCP 是开的。**[M]** 明确 `AT+NWDHC` 的前置条件是"已连上 AP"——没连上就查 DHCP 没意义。

---

### 2.3 `AT+NWDNS` / `AT+NWDNS2` — DNS 服务器

**[M] p.37-38**

```
AT+NWDNS=<dns_ip>    设置 STA 口 DNS
  Response: OK or ERROR
AT+NWDNS             →  +NWDNS:<dns_ip>
AT+NWDNS=?           →  +NWDNS:<dns_ip>

AT+NWDNS2=<dns_ip>   第二个 DNS
AT+NWDNS2            →  +NWDNS2:<dns_ip>

示例：
  AT+NWDNS=8.8.8.8
  OK
  AT+NWDNS
  +NWDNS:8.8.8.8
  OK
```

**[M] 重要 Note（会决定你看到的错误码）**

- **DHCP 模式下** `AT+NWDNS=?` 返回的是 **DHCP 下发的 DNS**，**无视**你之前用 `AT+NWDNS=<dns_ip>` 设的值。
  如果没有 DHCP 下发数据，会回 **`ERROR:-7`（"No result" 或 "Not configured"）**。
- **静态 IP 模式下** `AT+NWDNS=?` 返回你之前设的值或默认值。
- DHCP 模式下用 `AT+NWDNS=<dns_ip>` 设的值，要**系统重启后**才在静态模式下生效。
- 全部**存 NVRAM**。

> 所以：**DHCP 环境下看到 `AT+NWDNS=?` 回 `ERROR:-7` 是正常现象，不代表 DNS 坏了。** 改用 `AT+NWHOST=<域名>` 直接验证解析更可靠。

---

### 2.4 `AT+NWHOST` — 域名解析（DNS 能力的直接判据）

**[M] p.38**

```
AT+NWHOST=<name>     <name>: Domain name
Response: +NWHOST:<ip>

示例：
  at+nwhost=www.Renesas Electronics-semiconductor.com
  +NWHOST:54.192.175.64
  OK

Note:
  · Enabled by default in the SDK.
  · The configuration is stored in NVRAM.
```

**本机建议用法**

```
AT+NWHOST=api.seniverse.com
→ +NWHOST:<ip> + OK
```

**判读**
- 回 `+NWHOST:<ip>` → DNS 正常，出网有希望。
- 回 `ERROR:-6` → 模组认为没连上 AP。
- 回 `ERROR:-7` → 无结果 / 未配置。
- 回 `ERROR:-10` → 命令超时（DNS 包发不出去或没人应答）。

---

### 2.5 `AT+NWPING` — 链路连通性测试

**[M] p.38-39**

```
AT+NWPING=<iface>,<dst_ip>,<count>
  <iface>  : WLAN 接口. 0 (WLAN0), 1 (WLAN1)
  <dst_ip> : 目标 IP
  <count>  : ICMP 发送次数
Response: +NWPING:<sent_count>,<recv_count>, …

示例：
  AT+NWPING=0,192.168.0.1,4
  +NWPING:4,4,0,0,0
  OK

Note: Enabled by default in the SDK.
```

**建议用法与判读**

```
AT+NWPING=0,192.168.1.1,4        ← ping 网关（把 IP 换成 +NWIP 的第 4 字段）
AT+NWPING=0,223.5.5.5,4          ← ping 公网 DNS（阿里 223.5.5.5）
```

| 输出 | 判读 |
| --- | --- |
| `+NWPING:4,4,0,0,0` | 4 发 4 收，**链路完全正常** |
| `+NWPING:4,0,…` | 4 发 0 收 → 网关不通 / 路由问题 |
| `ERROR:-6` | 模组认为未连 AP |

> ping 公网通 = 路由器到外网通，可以进入第 ③ 步访问心知。

---

### 2.6 `AT+WFRSSI` — 信号强度 / 是否真连上

**[M] p.49**（同 ① 文档 2.13）

```
AT+WFRSSI            →  +RSSI:-41   + OK
AT+WFRSSI=<timeout>  →  +RSSI:-25   + OK      <timeout> 单位秒
未连接 AP:  +RSSI:NOT_CONN + ERROR:-400
超时无结果: ERROR:-801
Prerequisite: The DA16200/DA16600 should be connected to AP.
```

> `+RSSI:NOT_CONN` + `ERROR:-400` 是**最省事的"到底连没连上"判据**，比 `AT+WFSTA` 更不容易误读。

---

### 2.7 需要静态 IP 时

**[M] p.37**

```
AT+NWIP=0,192.168.0.100,255.255.255.0,192.168.0.1
OK
```

- `<iface>` 必须是 `0`（STA 口）。
- **存 NVRAM**，重启后仍在。
- 若在 **Soft AP 模式**改了 IP，还要：
  - 用 `AT+NWDHR` 重设 DHCP server 地址池；
  - DHCP server 正在跑时必须 `AT+RESTART`，或 `AT+NWDHS=0` → `AT+NWDHS=1` 重启它。

---

## 3. 常见错误码速查（本节相关）

**[M] Appendix I, Table 58（p.162）**

| 值 | 含义 | 在本节哪条命令上会遇到 |
| --- | --- | --- |
| -3 | Too many parameters | `AT+NWIP=?` 一般不会；但 `AT+WFSTAT=?` 会 |
| **-5** | Unsupported function | 功能不可用（见 ① 文档第 4 节） |
| **-6** | **Not connected to an AP** | `AT+NWDHC` / `AT+WFRSSI` / `AT+NWPING` 的前置条件没满足 |
| -7 | No result / Not configured | **`AT+NWDNS=?` 在 DHCP 模式下无下发数据时属正常** |
| -10 | Command timeout | `AT+NWHOST` DNS 超时、`AT+WFRSSI` 未在超时内拿到 |
| -22 | IP address get failure | 取 IP 失败 |
| -400 | 未连接 AP（`AT+WFRSSI` 专属） | [M] p.49 |
| -801 | RSSI 超时（`AT+WFRSSI` 专属） | [M] p.49 |

---

## 4. 三步验收清单

| 步骤 | 命令 | 通过标准 |
| --- | --- | --- |
| **1. 链路** | `AT+NWPING=0,<网关>,4` | `+NWPING:4,4,0,0,0` |
| **2. DNS** | `AT+NWHOST=api.seniverse.com` | `+NWHOST:<ip>` |
| **3. IP 确认** | `AT+NWIP` | 第 1 字段 `=0`，第 2 字段不是 `0.0.0.0` |

三条全过 → 进入 `AT_03_心知天气.md`。

---

## 5. 本机实测记录（2026-10-03，[L] `AUTO.txt`）

```
[AT>] AT+NWIP=?
[WIFI] +NWIP:0,192.168.1.6,255.255.255.0,192.168.1.1
[WIFI] OK

[AT>] AT+TZONE=?
[WIFI] +TZONE:28800
[WIFI] OK

[AT>] AT+TIME
[WIFI] +TIME:2026-10-03,11:56:29
[WIFI] OK

[AT>] AT+NWSNTP=1,ntp.aliyun.com,86400
[WIFI] OK
```

> `AT+NWSNTP` 是**外网可达性的旁证**——SNTP 能同步成功，说明 TCP 出网没问题。
> 手册语法：`AT+NWSNTP=<sntp>(,<server_ip>,<period>)`（[L] `HELP AT+NWSNTP` 实测回显）。
