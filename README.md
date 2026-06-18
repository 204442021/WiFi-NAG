# EVtools WIFI-NAG / ESP32-S3 WiFi Gateway + Nag CAN Runtime

> WIFI-NAG firmware for the Waveshare ESP32-S3 RS485/CAN board.
> This build keeps the WIFI-MAX WiFi repeater, AP+STA+NAPT gateway, DNS proxy/filter, WebUI, and OTA workflow, then adds a focused TWAI CAN runtime for Nag suppression on CAN ID `880 / 0x370`.
> It is intentionally not a full FSD activation build: Legacy / HW3 / HW4 FSD injection, AP Auto Restore, auto sleep, recorder UI, and speed-limit injection paths are disabled or hidden for this product mode.

---

## 中文说明

### 1. 项目定位

`WIFI-NAG` 是面向微雪 Waveshare ESP32-S3 RS485/CAN 开发板的混合固件，目标是同时提供：

- AP 热点：给车机、手机、电脑连接；
- STA 客户端：连接手机热点或家庭 WiFi；
- AP+STA+NAPT：把上游网络转发给热点客户端；
- DNS Proxy：热点客户端 DNS 指向 ESP32；
- DNS 过滤：Tesla 根域名黑名单 + 具体子域名白名单例外；
- DNS cache：降低重复解析延迟；
- WebUI：WiFi、AP、DNS、网关状态、系统状态、CAN Write、Nag 模式、OTA；
- TWAI CAN：只保留 Nag 抑制所需的 `0x370` 监听和 echo 写入路径。

默认信息：

| 项目 | 当前值 |
| --- | --- |
| WebUI | `http://100.100.1.1/` |
| AP SSID | `EVtools` |
| AP 密码 | 来自 `platformio_profile.h`，默认示例为 `changeme` |
| OTA 用户名 | `admin` |
| OTA 密码 | 来自 `platformio_profile.h`，默认示例为 `changeme` |
| 默认构建环境 | `wifi_nag_ESP32_S3_CAN` |
| OTA 固件 | `.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin` |
| 分区表 | `partitions_16mb_ota_4096k_nvs64_boot.csv` |

> CAN 是本项目最高风险区域。默认上电后 CAN Write 关闭；关闭时只读监听，开启后才允许 Nag echo 写入。

### 2. 当前功能

#### WiFi 中继

- ESP32-S3 同时运行 AP 和 STA。
- AP 客户端默认网段为 `100.100.1.x`。
- 网关和 DNS 通常为 `100.100.1.1`。
- STA 连接上游热点后，为 AP 客户端启用 NAPT 转发。
- 支持保存多个上游 WiFi。
- 支持手动扫描、手动连接、保存网络轮询。
- 提供网络性能模式，降低 WebUI 轮询对 AP+STA+NAPT 转发的影响。

#### DNS Proxy / DNS 过滤

- AP 客户端 DNS 请求由 ESP32 本地 DNS Proxy 接收。
- DNS pending 查询容量：`128`。
- DNS response cache：`256` 条。
- 默认黑名单是 Tesla 根域名：
  - `tesla.cn`
  - `tesla.com`
  - `teslamotors.com`
  - `tesla.services`
- 过滤逻辑：
  1. 白名单命中具体子域名：放行；
  2. 黑名单根域名或其子域名命中：阻断；
  3. 其他域名：放行。
- 白名单用于具体子域名例外，不能直接把黑名单根域名重新放开。
- 支持保守 / 激进 Tesla 白名单模板。
- 支持 DNS 统计清零。
- 支持上游 DNS 选择：
  - 自动 DHCP DNS；
  - 阿里 DNS：`223.5.5.5`；
  - 腾讯 DNS：`119.29.29.29`；
  - 自定义 IPv4。

#### Nag CAN Runtime

- 使用 ESP32-S3 TWAI 控制器，默认 CAN 速率 `500 kbit/s`。
- 默认 TX/RX 引脚：
  - TX：`GPIO_NUM_15`
  - RX：`GPIO_NUM_16`
- 过滤目标：CAN ID `880 / 0x370`。
- Mode A：
  - 监听 EPAS `0x370`；
  - 当 `handsOnLevel == 0` 时复制原帧；
  - 写入 `handsOnLevel = 1`；
  - 输出固定 `+1.80 Nm` torque；
  - counter 低 4 bit 加 1；
  - checksum = `sum(byte0..byte6) + 0x73`。
- Mode A_V2：
  - 选择后立即开始计时；
  - 前 10 秒输出固定 `+1.80 Nm`；
  - 之后在配置的 min/max Nm 范围内做 2 秒周期三角扫描；
  - 范围限制为 `-1.80 .. +1.80 Nm`；
  - 会跳过自己刚发出的 echo，避免重复 echo。

#### WebUI

- WIFI-NAG 模式显示 `WIFI-NAG` 标识。
- 顶部控制为 `CAN Write`，不是 FSD 开关。
- `CAN Write OFF`：只读 CAN 监听，不写入。
- `CAN Write ON`：允许 Nag `0x370` counter+1 echo 写入。
- 提供 Nag Mode `A / A_V2` 和 A_V2 Nm 范围配置。
- 隐藏 Legacy / HW3 / HW4 选择、FSD 激活、AP Auto Restore、auto sleep、CAN recorder、HW3 speed、Legacy MPP 等 UI。
- 系统监测默认关闭，需要手动打开。
- Debug logging 默认关闭，避免串口日志影响运行时稳定性。

#### OTA

- 支持 WebUI 上传 `.bin` 固件 OTA。
- OTA 固件路径：

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```

### 3. 性能配置

`wifi_nag_ESP32_S3_CAN` 复用 WIFI-MAX 的 WiFi 优化配置，并恢复 TWAI CAN runtime：

```text
sdkconfig.defaults
sdkconfig.wifi_max.defaults
sdkconfig.wifi_nag_ESP32_S3_CAN
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

运行时调优：

- 关闭 WiFi 省电：`WIFI_PS_NONE`；
- AP / STA 固定 20 MHz 带宽，优先稳定兼容；
- AP 使用 802.11 g/n，STA 保留 802.11 b/g/n 兼容；
- 发射功率设置为 ESP-IDF quarter-dBm 标尺下的 `78`，约 `19.5 dBm`；
- WiFi/lwIP 热路径保留在 Core1；
- WebUI、DNS task、CAN realtime task 尽量避免互相抢占。

### 4. 构建

推荐 PowerShell：

```powershell
pio run -e wifi_nag_ESP32_S3_CAN
```

构建成功后 OTA 固件位于：

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```

核心验证：

```powershell
pio test -e native_nag
pio run -e wifi_nag_ESP32_S3_CAN
```

如果修改了 WebUI 源文件：

```powershell
py -3 scripts/minify_dashboard.py
pio run -e wifi_nag_ESP32_S3_CAN
```

### 5. 清除并下载

根据实际串口修改 `COM14`：

```powershell
pio run -e wifi_nag_ESP32_S3_CAN -t erase --upload-port COM14
pio run -e wifi_nag_ESP32_S3_CAN -t upload --upload-port COM14
```

只上传固件：

```powershell
pio run -e wifi_nag_ESP32_S3_CAN -t upload --upload-port COM14
```

### 6. 生成完整 16MB BIN

当前分区表：

| Name | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| nvs | data | nvs | `0x9000` | `0x10000` |
| otadata | data | ota | `0x19000` | `0x2000` |
| boot | app | ota_0 | `0x20000` | `0x400000` |
| app1 | app | ota_1 | `0x420000` | `0x400000` |
| spiffs | data | spiffs | `0x820000` | `0x7C0000` |
| coredump | data | coredump | `0xFE0000` | `0x20000` |

完整 16MB BIN 从 `0x0` 烧录，示例命令：

```powershell
$out='C:\Users\Administrator\Desktop\FSD-CAN\AAA-ESP32S3\BIN\WIFI-NAG-full-16M.bin'
py -3 $env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 merge_bin `
  -o $out `
  --flash_mode dio --flash_freq 80m --flash_size 16MB --fill-flash-size 16MB `
  0x0 .pio\build\wifi_nag_ESP32_S3_CAN\bootloader.bin `
  0x8000 .pio\build\wifi_nag_ESP32_S3_CAN\partitions.bin `
  0x19000 .pio\build\wifi_nag_ESP32_S3_CAN\ota_data_initial.bin `
  0x20000 .pio\build\wifi_nag_ESP32_S3_CAN\firmware.bin
```

### 7. 硬件和测试建议

- 手机热点固定 2.4 GHz。
- 尽量使用信道 1 / 6 / 11。
- 避免信道 13，部分车机或手机兼容性较差。
- 开发板远离金属遮挡和强干扰源。
- 使用稳定 USB 供电。
- 下载测速时关闭系统监测、任务列表、DNS 列表自动刷新。
- 真实车辆 CAN 测试时先保持 `CAN Write OFF`，确认 `0x370` RX 和 WebUI 状态正常后再开启写入。
- 如果出现任何异常车辆行为，立即关闭 CAN Write 或移除设备。

### 8. 常见问题

#### WIFI-NAG 是不是 FSD 激活固件？

不是。当前产品模式只保留 Nag 抑制所需的 CAN runtime。FSD 激活、Legacy/HW3/HW4 handler、AP Auto Restore、auto sleep、recorder UI 和 speed injection 在 WIFI-NAG 下被禁用或隐藏。

#### CAN Write 关闭后还会发 CAN 吗？

不应该。`CAN Write OFF` 时 Nag runtime 仍可监听 `0x370`，但 `nagKillerRuntime` 会被 `canActive` gate 关闭，不发送 echo。

#### 为什么热点速度比手机直连慢？

ESP32-S3 是单 2.4 GHz WiFi 射频，AP+STA+NAPT 需要同一个射频同时服务上游和下游，属于半双工中继，速度一定低于手机直连。

#### DNS 过滤会不会影响下载速度？

DNS 主要影响首次解析和请求建立，不是持续下载速度的主要瓶颈。持续下载速度主要受 WiFi 信道、RSSI、NAPT、CPU 和上游热点影响。

#### 为什么 CPU 显示不是 240 MHz？

请确认正在运行的是 `wifi_nag_ESP32_S3_CAN` 新固件，并且已清除下载。旧固件可能使用 160 MHz 配置。

---

## English Summary

`WIFI-NAG` targets the Waveshare ESP32-S3 RS485/CAN board. It combines the WIFI-MAX WiFi repeater, AP+STA+NAPT gateway, DNS proxy/filtering, WebUI, and OTA workflow with a minimal TWAI CAN runtime for Nag suppression on CAN ID `880 / 0x370`.

This product mode is intentionally narrow:

- CAN Write is off by default.
- CAN Write off means read-only CAN monitoring.
- CAN Write on allows Nag `0x370` counter+1 echo writes.
- FSD activation, Legacy/HW3/HW4 injection, AP Auto Restore, auto sleep, CAN recorder UI, HW3 speed, and Legacy MPP paths are not part of WIFI-NAG.

Build:

```powershell
pio run -e wifi_nag_ESP32_S3_CAN
```

Test the Nag handler:

```powershell
pio test -e native_nag
```

OTA firmware:

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```
