# T-2CAN / EVtools ESP32-S3 CAN Dashboard

> DEV branch README for the Waveshare ESP32-S3 RS485/CAN build.  
> This repository is a local vehicle-CAN research firmware with WebUI, OTA, AP+STA+NAPT gateway, Tesla DNS filtering, and dashboard diagnostics.

---

## 中文说明

### 1. 项目定位

本项目基于 `ev-open-can-tools` 的 ESP-IDF Dashboard 架构，当前 DEV 版本主要适配：

- 开发板：Waveshare ESP32-S3 RS485/CAN
- CAN 驱动：ESP32-S3 内置 TWAI
- 默认构建环境：`waveshare_ESP32_S3_RS485_CAN`
- 默认硬件模式：HW3，可在 WebUI 中切换 Legacy / HW3 / HW4
- WebUI 入口：连接设备热点后打开 `http://100.100.1.1/`
- 默认热点：`EVtools`
- 默认热点密码：`12345678`
- 默认 OTA 用户名：`admin`
- 默认 OTA 密码：`12345678`

> 安全提醒：本项目会监听和修改车辆 CAN 帧。任何 CAN 注入都可能影响车辆行为。请只在你完全理解风险、车辆安全受控、符合当地法规的条件下测试。

### 2. DEV 版本主要功能

#### 固件与 OTA

- 修复真实 OTA 上传流程，避免旧版本“返回成功但没有写入固件”的假成功问题。
- WebUI OTA 改为直接上传 `.bin` 文件。
- OTA 成功条件改为固件确实写入完成后才返回成功。
- 保留车主弹窗底部版本与构建时间戳：`Version` + `OTA timestamp`。
- 构建时自动写入当前 OTA timestamp，方便区分 OTA 是否真的生效。
- 支持普通 OTA 固件：`.pio/build/waveshare_ESP32_S3_RS485_CAN/firmware.bin`。
- 支持合并输出 16MB 全量线刷包，从 `0x0` 写入。

#### WiFi / AP / STA / NAPT

- ESP32-S3 同时运行 AP 热点和 STA 客户端。
- AP 客户端默认网关为 `100.100.1.1`。
- STA 连接上游 WiFi 后，为 AP 客户端启用 STA-AP NAT 路由。
- 支持保存多个上游 WiFi。
- 未连接时按固定间隔轮询已保存 WiFi。
- 不再自动全量扫描附近 WiFi，避免扫描影响 AP+STA+NAPT 转发稳定性。
- 保留手动扫描和手动连接按钮。
- WiFi 连接超时已缩短为 10 秒，失败后固定等待 5 秒继续轮询保存网络。
- WebUI 显示 AP channel / STA channel / same or cross，便于判断单射频跨信道影响。

#### DNS 网关与 Tesla 过滤

- AP 客户端 DNS 指向 ESP32 网关，由固件内置 DNS Proxy 处理。
- DNS response cache 提升到 128 条。
- DNS 过滤简化为高效规则：
  - 白名单优先放行；
  - 黑名单根域名及其子域名阻断；
  - 其他域名默认放行。
- 默认黑名单覆盖 Tesla 根域名，例如 `tesla.cn`、`tesla.com`、`teslamotors.com`、`tesla.services`。
- 支持保守 / 激进白名单模板，模板采用“合并”逻辑，不覆盖用户手动添加的域名。
- 删除/隐藏 Strict DNS、CIDR 例外、纯白名单模式等专家过滤残留，减少误操作和热路径负担。
- 增加 DNS 统计清零按钮，避免旧的 slow / timeout / fail 计数误导判断。
- 支持上游 DNS 选择：自动、阿里 `223.5.5.5`、腾讯 `119.29.29.29`、自定义 IPv4。
- WebUI 显示 DNS latency、slow counters、pending、timeout、upstream fail、cache hit/miss。

#### WebUI 与车机适配

- 增加 UI Mode：Auto / Car / Phone。
- Auto 模式会根据横屏、大触控屏等特征自动进入 Car UI。
- Car UI 禁用不兼容车机浏览器的 select 交互，改为按钮化操作。
- Car UI 放大按钮和间距，降低动画与轮询频率。
- 增加左侧车机快速导航：Status / HW / Speed / WiFi / DNS / System / CAN。
- 系统状态界面增加更多硬件状态监测。
- CPU / 内存 / PSRAM / SPIFFS 等占用显示为进度条。
- 进度条按比例变色：30% 内绿色，60% 内黄色，80% 以上红色。
- 系统界面由单列优化为多列布局，桌面和车机大屏更易读。
- Network Performance Mode 降低 WebUI 轮询，减少对 AP+STA+NAPT 的干扰。

#### CAN / FSD / AP Auto Restore

- 支持 Legacy / HW3 / HW4 handler 模式切换。
- Waveshare 默认 HW3 模式。
- 保留 HW3 自定义限速 / speed profile 相关控制。
- AP/EAP Auto Restore 增加 WebUI 开关与状态显示。
- AP Auto Restore 逻辑考虑刹车、挡位、方向盘角度、快速转向、TC/VDC/车身稳定介入等条件，避免在不安全状态下触发。
- 支持 CAN Sniffer、CAN Recorder、日志、诊断开关。
- 诊断类功能默认关闭，需要用户手动打开，避免影响网络和主循环性能。

### 3. 分区与固件输出

当前 Waveshare 16MB 分区布局：

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| nvs | data | nvs | `0x9000` | `0x10000` |
| otadata | data | ota | `0x19000` | `0x2000` |
| app0 | app | ota_0 | `0x20000` | `0x400000` |
| app1 | app | ota_1 | `0x420000` | `0x400000` |
| spiffs | data | spiffs | `0x820000` | `0x7C0000` |
| coredump | data | coredump | `0xFE0000` | `0x20000` |

常用固件：

- OTA 文件：`.pio/build/waveshare_ESP32_S3_RS485_CAN/firmware.bin`
- 16MB 全量包：通常输出到 `dist/`，从 `0x0` 写入

### 4. 构建与下载

推荐使用 PowerShell：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN
```

普通下载：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload --upload-port COM14
```

清除后下载：

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t erase --upload-port COM14
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload --upload-port COM14
```

如果你的串口不是 `COM14`，请先查看：

```powershell
pio device list
```

### 5. 本地配置文件

构建需要本地 `platformio_profile.h`。该文件包含热点名、热点密码、OTA 用户名、OTA 密码等本地配置，默认不会提交到 Git。

最小示例：

```cpp
#pragma once
#define DRIVER_TWAI
#define DASH_SSID "EVtools"
#define DASH_PASS "12345678"
#define DASH_OTA_USER "admin"
#define DASH_OTA_PASS "12345678"
```

### 6. 分支建议

- `main`：稳定可刷机版本。
- `dev`：当前集成测试版本，功能成熟后再合并 main。
- `codex/net-apsta-napt-dns`：WiFi / DNS / AP+STA+NAPT 优化来源分支。
- `ap-auto-restore`：AP/EAP Auto Restore 来源分支。
- `WEBUI`：车机 WebUI 优化来源分支。
- `codex/HW4`：HW4 FSD 激活优化测试分支。

---

## English

### 1. Project Scope

This DEV branch is an ESP-IDF Dashboard firmware derived from `ev-open-can-tools`, tuned for the Waveshare ESP32-S3 RS485/CAN board.

- Board: Waveshare ESP32-S3 RS485/CAN
- CAN driver: ESP32-S3 built-in TWAI
- Main PlatformIO env: `waveshare_ESP32_S3_RS485_CAN`
- Default vehicle mode: HW3, switchable in WebUI between Legacy / HW3 / HW4
- Dashboard URL: connect to the device hotspot, then open `http://100.100.1.1/`
- Default hotspot SSID: `EVtools`
- Default hotspot password: `12345678`
- Default OTA user: `admin`
- Default OTA password: `12345678`

> Safety warning: this firmware can observe and modify vehicle CAN frames. CAN injection can affect vehicle behavior. Test only in a controlled environment, with full understanding of the risks and applicable laws.

### 2. Main DEV Features

#### Firmware And OTA

- Fixes the real OTA upload path so the device no longer reports a fake success without writing firmware.
- WebUI OTA uploads raw `.bin` files directly.
- `/update` returns success only after firmware writing is actually complete.
- Owner popup keeps `Version` and `OTA timestamp` for verification.
- Build scripts update OTA timestamp automatically on each firmware build.
- Supports regular OTA firmware output: `.pio/build/waveshare_ESP32_S3_RS485_CAN/firmware.bin`.
- Supports merged 16MB full-flash image generation for flashing from `0x0`.

#### WiFi / AP / STA / NAPT

- Runs ESP32-S3 SoftAP and STA at the same time.
- AP clients use `100.100.1.1` as gateway and DNS.
- Enables STA-to-AP NAT routing after the upstream WiFi is connected.
- Supports multiple saved upstream WiFi networks.
- Rotates through saved networks while disconnected.
- Avoids automatic full WiFi scanning during reconnect, reducing AP+STA+NAPT disruption.
- Keeps manual scan and manual connect controls.
- Uses a 10-second STA connection timeout and a fixed 5-second retry interval.
- Shows AP channel, STA channel, and same/cross-channel status in WebUI.

#### DNS Gateway And Tesla Filtering

- AP client DNS is handled by the ESP32 DNS proxy.
- DNS response cache size is 128 entries.
- DNS filtering is simplified for performance:
  - whitelist overrides blacklist;
  - blacklisted root domains and their subdomains are blocked;
  - unrelated domains are allowed.
- Default Tesla root-domain blacklist includes `tesla.cn`, `tesla.com`, `teslamotors.com`, and `tesla.services`.
- Conservative and aggressive whitelist templates merge into the current list instead of replacing user entries.
- Strict DNS, CIDR exceptions, and whitelist-only expert options are removed/hidden to reduce complexity.
- Adds a DNS stats reset button.
- Supports upstream DNS selection: Auto, Ali `223.5.5.5`, Tencent `119.29.29.29`, and custom IPv4.
- WebUI displays DNS latency, slow counters, pending queries, timeouts, upstream failures, and cache hits/misses.

#### WebUI And In-Car Browser Support

- Adds UI Mode: Auto / Car / Phone.
- Auto mode detects wide landscape touch screens and switches to Car UI.
- Car UI avoids problematic `<select>` controls and uses button-based controls instead.
- Car UI increases button size and spacing, reduces animations and polling.
- Adds a side navigation bar for Status / HW / Speed / WiFi / DNS / System / CAN.
- Expands system status monitoring.
- CPU, heap, PSRAM, SPIFFS, and related metrics are shown with progress bars.
- Progress bars use green / yellow / red thresholds: green under 30%, yellow under 60%, red above 80%.
- System panels are optimized into a multi-column layout for desktop and vehicle screens.
- Network Performance Mode reduces WebUI polling load during AP+STA+NAPT forwarding.

#### CAN / FSD / AP Auto Restore

- Supports Legacy / HW3 / HW4 handler modes.
- Waveshare build defaults to HW3.
- Keeps HW3 custom speed and speed profile controls.
- Adds WebUI switch and status for AP/EAP Auto Restore.
- AP Auto Restore checks brake, gear, steering angle, fast steering, TC/VDC/stability activity, and retry timing before triggering.
- Supports CAN Sniffer, CAN Recorder, logs, and diagnostics.
- Heavy diagnostics are off by default and must be enabled manually.

### 3. Partition Layout And Firmware Outputs

Current Waveshare 16MB partition layout:

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| nvs | data | nvs | `0x9000` | `0x10000` |
| otadata | data | ota | `0x19000` | `0x2000` |
| app0 | app | ota_0 | `0x20000` | `0x400000` |
| app1 | app | ota_1 | `0x420000` | `0x400000` |
| spiffs | data | spiffs | `0x820000` | `0x7C0000` |
| coredump | data | coredump | `0xFE0000` | `0x20000` |

Common outputs:

- OTA firmware: `.pio/build/waveshare_ESP32_S3_RS485_CAN/firmware.bin`
- 16MB full-flash image: usually generated under `dist/`, flashed from `0x0`

### 4. Build And Flash

Build:

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN
```

Upload only:

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload --upload-port COM14
```

Erase and upload:

```powershell
pio run -e waveshare_ESP32_S3_RS485_CAN -t erase --upload-port COM14
pio run -e waveshare_ESP32_S3_RS485_CAN -t upload --upload-port COM14
```

List serial ports:

```powershell
pio device list
```

### 5. Local Profile

The build expects a local `platformio_profile.h`. This file stores local credentials and board choices and is not committed to Git.

Minimal example:

```cpp
#pragma once
#define DRIVER_TWAI
#define DASH_SSID "EVtools"
#define DASH_PASS "12345678"
#define DASH_OTA_USER "admin"
#define DASH_OTA_PASS "12345678"
```

### 6. Branch Model

- `main`: stable flashable baseline.
- `dev`: integrated test branch.
- `codex/net-apsta-napt-dns`: WiFi / DNS / AP+STA+NAPT optimization source branch.
- `ap-auto-restore`: AP/EAP Auto Restore source branch.
- `WEBUI`: in-car WebUI optimization source branch.
- `codex/HW4`: HW4 FSD activation test branch.

---

## License And Responsibility

This repository keeps the original project license files where applicable. Any vehicle-side testing is your own responsibility.
