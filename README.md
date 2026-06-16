# EVtools WIFI-MAX / ESP32-S3 WiFi Repeater + DNS Filter

> WIFI-MAX branch for the Waveshare ESP32-S3 RS485/CAN board.  
> This firmware turns the board into a lightweight 2.4 GHz WiFi repeater with AP+STA+NAPT routing, DNS proxy/filtering, OTA update, and a simplified WebUI.  
> CAN / FSD / HW3 / HW4 / AP Auto Restore features are intentionally disabled in this branch.

---

## 中文说明

### 1. 项目定位

`WIFI-MAX` 是一个专门面向 WiFi 中继和 DNS 过滤的精简分支，目标是在微雪 Waveshare ESP32-S3 RS485/CAN 开发板上提供：

- AP 热点：给车机、手机、电脑连接；
- STA 客户端：连接手机热点或家庭 WiFi；
- AP+STA+NAPT：把上游网络转发给热点客户端；
- DNS Proxy：热点客户端 DNS 指向 ESP32；
- DNS 过滤：Tesla 根域名黑名单 + 子域名白名单优先放行；
- DNS cache：降低重复解析延迟；
- WebUI：WiFi 扫描、保存、连接、DNS 设置、状态诊断、OTA；
- WiFi 性能优先：不启动 CAN 实时任务，不启用 FSD/CAN 注入功能。

默认信息：

| 项目 | 默认值 |
| --- | --- |
| WebUI | `http://100.100.1.1/` |
| AP SSID | `EVtools` |
| AP 密码 | `12345678` |
| OTA 用户名 | `admin` |
| OTA 密码 | `12345678` |
| 默认构建环境 | `wifi_max_ESP32_S3_CAN` |
| 默认分区 | `partitions_16mb_ota_4096k_nvs64.csv` |

> 注意：本分支虽然运行在带 CAN 接口的开发板上，但设计目标是 WiFi / DNS 网关，不做车辆 CAN 读取、修改或发送。

### 2. 当前功能

#### WiFi 中继

- ESP32-S3 同时运行 AP 和 STA。
- AP 客户端默认网段为 `100.100.1.x`。
- 网关和 DNS 通常为 `100.100.1.1`。
- STA 连接上游热点后，自动为 AP 客户端启用 NAPT 转发。
- 支持保存多个上游 WiFi。
- 支持手动扫描、手动连接、保存网络轮询。
- 默认避免频繁自动扫描，减少 AP+STA 转发抖动。

#### DNS Proxy / DNS 过滤

- AP 客户端 DNS 请求由 ESP32 本地 DNS Proxy 接收。
- 默认 DNS cache：`128` 条。
- 过滤规则采用简单高效逻辑：
  1. 白名单命中：放行；
  2. 黑名单根域名或其子域名命中：阻断；
  3. 其他域名：放行。
- 支持保守 / 激进 Tesla 白名单模板。
- 白名单模板采用“合并”逻辑，不覆盖用户手动添加的域名。
- 支持 DNS 统计清零。
- 支持上游 DNS 选择：
  - 自动；
  - 阿里 DNS：`223.5.5.5`；
  - 腾讯 DNS：`119.29.29.29`；
  - 自定义 IPv4。

#### WebUI

- WIFI-MAX 专用界面会隐藏 CAN / FSD / HW3 / HW4 相关内容。
- 保留 WiFi、AP、网关、DNS、OTA、系统状态页面。
- 支持车机浏览器适配：按钮化交互、更大间距、降低动画和轮询。
- 系统监测默认关闭，需要手动打开。
- 打开系统监测后可以查看：
  - CPU 频率；
  - CPU0 / CPU1 负载；
  - 内存 / PSRAM / Flash / SPIFFS；
  - FreeRTOS task load；
  - WiFi RSSI / AP 客户端数量。

#### OTA

- 支持 WebUI 上传 `.bin` 固件 OTA。
- OTA timestamp 会在构建时自动写入 UI，用于确认 OTA 是否真的生效。
- OTA 文件为：

```text
.pio/build/wifi_max_ESP32_S3_CAN/firmware.bin
```

### 3. 性能配置

WiFi-Max 环境使用专用 sdkconfig，并叠加 WIFI-MAX 默认优化：

```text
sdkconfig.wifi_max_ESP32_S3_CAN
sdkconfig.wifi_max.defaults
```

关键配置：

| 配置 | 当前值 |
| --- | --- |
| CPU | `240 MHz` |
| Flash | `16 MB` |
| PSRAM | `80 MHz` |
| BLE | disabled |
| Power Management | disabled |
| FreeRTOS runtime stats | enabled |
| WiFi static RX buffer | `16` |
| WiFi dynamic RX buffer | `64` |
| WiFi dynamic TX buffer | `64` |
| WiFi AMPDU TX/RX | enabled |
| WiFi BA window | `12 / 12` |
| lwIP sockets | `24` |
| lwIP TCP/IP recv mbox | `64` |
| TCP send buffer | `16384` |
| TCP window | `16384` |
| TCP/IP task stack | `4096` |

运行时调优：

- 关闭 WiFi 省电：`WIFI_PS_NONE`；
- AP / STA 固定 20 MHz 带宽，优先稳定兼容；
- AP 使用 802.11 g/n，STA 保留 802.11 b/g/n 兼容；
- 发射功率设置为 ESP-IDF quarter-dBm 标尺下的 `78`，约 `19.5 dBm`；
- WiFi / lwIP 继续运行在 Core1；
- WebUI task 和 DNS task 在 WIFI-MAX 下放到 Core0，减少和 WiFi/lwIP 热路径互抢。

这些配置用于让 WiFi / NAPT / DNS 尽量获得更多 CPU 和内存资源，同时保持车机和手机 2.4 GHz 兼容性。

### 4. 硬件限制

ESP32-S3 的 WiFi 是单 2.4 GHz 射频：

- 支持 802.11 b/g/n；
- 不支持 WiFi 6；
- 不支持 5 GHz；
- AP+STA+NAPT 是单射频半双工转发；
- 实际速度无法达到手机直连热点；
- AP 与 STA 同信道通常更稳定；
- 跨信道会增加延迟并降低吞吐。

推荐设置：

- 手机热点固定 2.4 GHz；
- 尽量使用信道 1 / 6 / 11；
- 避免信道 13，部分车机或手机兼容性较差；
- 开发板远离金属遮挡和强干扰源；
- 使用稳定 USB 供电；
- 下载测速时关闭系统监测、任务列表、DNS 列表自动刷新。

### 5. 构建

推荐 PowerShell：

```powershell
pio run -e wifi_max_ESP32_S3_CAN
```

构建成功后 OTA 固件位于：

```text
.pio/build/wifi_max_ESP32_S3_CAN/firmware.bin
```

### 6. 清除并下载

根据实际串口修改 `COM14`：

```powershell
pio run -e wifi_max_ESP32_S3_CAN -t erase --upload-port COM14
pio run -e wifi_max_ESP32_S3_CAN -t upload --upload-port COM14
```

### 7. 生成完整 16MB BIN

当前分区表：

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| nvs | data | nvs | `0x9000` | `0x10000` |
| otadata | data | ota | `0x19000` | `0x2000` |
| app0 | app | ota_0 | `0x20000` | `0x400000` |
| app1 | app | ota_1 | `0x420000` | `0x400000` |
| spiffs | data | spiffs | `0x820000` | `0x7C0000` |
| coredump | data | coredump | `0xFE0000` | `0x20000` |

完整 16MB BIN 从 `0x0` 烧录，示例命令：

```powershell
$out='C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\BIN\WIFI-MAX-full-16M.bin'
py -3 $env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 merge_bin `
  -o $out `
  --flash_mode dio --flash_freq 80m --flash_size 16MB --fill-flash-size 16MB `
  0x0 .pio\build\wifi_max_ESP32_S3_CAN\bootloader.bin `
  0x8000 .pio\build\wifi_max_ESP32_S3_CAN\partitions.bin `
  0x19000 .pio\build\wifi_max_ESP32_S3_CAN\ota_data_initial.bin `
  0x20000 .pio\build\wifi_max_ESP32_S3_CAN\firmware.bin
```

### 8. 常见问题

#### 为什么 CPU 显示不是 240 MHz？

请确认正在运行的是 `wifi_max_ESP32_S3_CAN` 新固件，并且已清除下载。旧固件可能使用 160 MHz 配置。

#### 为什么任务列表看不到？

系统监测默认关闭。进入 WebUI 后打开 `System Monitor / 系统监测`，等待 2-3 秒，任务列表才会采样显示。

#### 为什么热点速度比手机直连慢？

ESP32-S3 是单 2.4 GHz WiFi 射频，AP+STA+NAPT 需要同一个射频同时服务上游和下游，属于半双工中继，速度一定低于手机直连。

#### DNS 过滤会不会影响下载速度？

DNS 主要影响首次解析和请求建立，不是持续下载速度的主要瓶颈。持续下载速度主要受 WiFi 信道、RSSI、NAPT、CPU 和上游热点影响。

---

## English

### 1. Project Scope

`WIFI-MAX` is a WiFi-only branch for the Waveshare ESP32-S3 RS485/CAN board. It focuses on WiFi repeater and DNS filtering features:

- SoftAP for car head units, phones, and laptops;
- STA client for phone hotspots or home WiFi;
- AP+STA+NAPT routing from upstream WiFi to AP clients;
- Local DNS proxy for AP clients;
- Tesla root-domain blacklist with subdomain whitelist override;
- DNS response cache;
- WebUI for WiFi, DNS, gateway status, diagnostics, and OTA;
- WiFi performance first: no CAN realtime task, no FSD/CAN injection.

Defaults:

| Item | Default |
| --- | --- |
| WebUI | `http://100.100.1.1/` |
| AP SSID | `EVtools` |
| AP password | `12345678` |
| OTA username | `admin` |
| OTA password | `12345678` |
| Build environment | `wifi_max_ESP32_S3_CAN` |
| Partition table | `partitions_16mb_ota_4096k_nvs64.csv` |

> This branch runs on a CAN-capable board, but CAN/FSD/HW3/HW4/AP Auto Restore features are intentionally disabled.

### 2. Features

#### WiFi Repeater

- Runs SoftAP and STA at the same time.
- AP clients normally receive `100.100.1.x` addresses.
- Gateway and DNS are normally `100.100.1.1`.
- Enables NAPT routing after STA connects to upstream WiFi.
- Supports saved upstream networks.
- Supports manual scan and manual connect.
- Avoids frequent automatic scans to reduce AP+STA+NAPT jitter.

#### DNS Proxy / DNS Filter

- AP client DNS requests are handled by the ESP32 DNS proxy.
- DNS response cache size: `128` entries.
- Filtering logic is intentionally simple:
  1. Whitelist match: allow;
  2. Blacklist root domain or subdomain match: block;
  3. Everything else: allow.
- Conservative and aggressive Tesla whitelist templates are supported.
- Templates are merged into the current whitelist and do not overwrite user entries.
- DNS counters can be reset from the WebUI.
- Upstream DNS options:
  - Auto;
  - Ali DNS: `223.5.5.5`;
  - Tencent DNS: `119.29.29.29`;
  - Custom IPv4.

#### WebUI

- WIFI-MAX UI hides CAN / FSD / HW3 / HW4 sections.
- Keeps WiFi, AP, gateway, DNS, OTA, and system status pages.
- Car-browser friendly controls: larger buttons, wider spacing, lower animation and polling.
- System monitor is off by default.
- When enabled, it shows:
  - CPU frequency;
  - CPU0 / CPU1 load;
  - heap / PSRAM / Flash / SPIFFS;
  - FreeRTOS task load;
  - WiFi RSSI and AP client count.

#### OTA

- Supports WebUI `.bin` firmware upload.
- OTA timestamp is written at build time so OTA results can be verified.
- OTA firmware path:

```text
.pio/build/wifi_max_ESP32_S3_CAN/firmware.bin
```

### 3. Performance Configuration

WiFi-Max uses its own sdkconfig and an extra WIFI-MAX defaults layer:

```text
sdkconfig.wifi_max_ESP32_S3_CAN
sdkconfig.wifi_max.defaults
```

Key settings:

| Setting | Value |
| --- | --- |
| CPU | `240 MHz` |
| Flash | `16 MB` |
| PSRAM | `80 MHz` |
| BLE | disabled |
| Power Management | disabled |
| FreeRTOS runtime stats | enabled |
| WiFi static RX buffer | `16` |
| WiFi dynamic RX buffer | `64` |
| WiFi dynamic TX buffer | `64` |
| WiFi AMPDU TX/RX | enabled |
| WiFi BA window | `12 / 12` |
| lwIP sockets | `24` |
| lwIP TCP/IP recv mbox | `64` |
| TCP send buffer | `16384` |
| TCP window | `16384` |
| TCP/IP task stack | `4096` |

Runtime tuning:

- WiFi power save is disabled with `WIFI_PS_NONE`.
- AP and STA use 20 MHz bandwidth for compatibility and stability.
- AP uses 802.11 g/n; STA keeps 802.11 b/g/n compatibility.
- TX power is set to `78` in ESP-IDF quarter-dBm units, about `19.5 dBm`.
- WiFi and lwIP stay on Core1.
- WebUI and DNS tasks are placed on Core0 in WIFI-MAX builds to reduce contention with the WiFi/lwIP hot path.

### 4. Hardware Limits

ESP32-S3 has a single 2.4 GHz WiFi radio:

- Supports 802.11 b/g/n;
- Does not support WiFi 6;
- Does not support 5 GHz;
- AP+STA+NAPT is single-radio half-duplex forwarding;
- Throughput cannot match a direct phone-hotspot connection;
- Same-channel AP/STA operation is usually more stable;
- Cross-channel operation increases latency and lowers throughput.

Recommended setup:

- Use 2.4 GHz hotspot mode;
- Prefer channels 1 / 6 / 11;
- Avoid channel 13 for better client compatibility;
- Keep the board away from metal shielding and strong interference;
- Use a stable USB power source;
- Disable system monitor and task stats during throughput tests.

### 5. Build

```powershell
pio run -e wifi_max_ESP32_S3_CAN
```

OTA firmware output:

```text
.pio/build/wifi_max_ESP32_S3_CAN/firmware.bin
```

### 6. Erase and Upload

Change `COM14` to your actual serial port:

```powershell
pio run -e wifi_max_ESP32_S3_CAN -t erase --upload-port COM14
pio run -e wifi_max_ESP32_S3_CAN -t upload --upload-port COM14
```

### 7. Full 16MB Image

Partition table:

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| nvs | data | nvs | `0x9000` | `0x10000` |
| otadata | data | ota | `0x19000` | `0x2000` |
| app0 | app | ota_0 | `0x20000` | `0x400000` |
| app1 | app | ota_1 | `0x420000` | `0x400000` |
| spiffs | data | spiffs | `0x820000` | `0x7C0000` |
| coredump | data | coredump | `0xFE0000` | `0x20000` |

A full 16MB image should be flashed at offset `0x0`.

### 8. FAQ

#### Why does CPU show 160 MHz instead of 240 MHz?

Make sure the board is running the latest `wifi_max_ESP32_S3_CAN` firmware. Older builds may have used a 160 MHz sdkconfig.

#### Why is the task table empty?

System Monitor is off by default. Enable it in the WebUI and wait 2-3 seconds for `/task_stats` sampling.

#### Why is the repeater slower than a direct phone hotspot?

ESP32-S3 uses one 2.4 GHz radio for both STA and AP. AP+STA+NAPT forwarding is half-duplex and cannot match direct hotspot throughput.

#### Does DNS filtering reduce download speed?

DNS mostly affects initial name resolution. Sustained download speed is mainly limited by WiFi channel, RSSI, NAPT, CPU, and upstream hotspot quality.
