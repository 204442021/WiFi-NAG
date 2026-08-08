# WIFI-NAG V1.0.4

本版本完成 WiFi-NAG 后台 UI 主链一体化，将原有多层 Shell、BLE 组装页和运行时页面注入收敛为一个静态页面与一条根路由主链。

## 更新内容

- `GET /` 直接返回唯一完整的 gzip 仪表盘页面，不再经过 iframe、`document.write()` 或 `/dashboard` 二次取页。
- 顶部标题栏、NAG/CAN、NAG 扫动时间、BLE、Wi-Fi、网关、系统状态、调试日志和固件更新全部静态合入同一页面。
- NAG Sweep 与 BLE WebUI 模块收敛为 API/NVS 后端；原有协议、持久化、配对、绑定和状态同步逻辑保持不变。
- 四个主卡片改为原生手风琴折叠，默认折叠，单卡展开，并同步 `aria-expanded` 状态。
- `/dashboard` 与 `/legacy-dashboard` 保留为无缓存 302 兼容重定向，根页面由 `mcpDashboardSetup()` 唯一注册。
- 页面生成链改为确定性的 Python 压缩流程，支持 `scripts/minify_dashboard.py --check`，并固定 gzip 跨平台头字节与压缩依赖版本。

## 审核修复

- 修正 ESP-IDF WebServer 兼容层的 302 状态映射和重定向头调用，目标固件可正常编译。
- BLE 状态轮询增加请求防重入、1.5 秒超时和 `finally` 清理，单个 BLE API 异常不会阻塞其他卡片。
- 增加单一主链回归契约，直接解压并检查已生成的 gzip 页面，防止源码与固件内嵌页面漂移。

## 升级说明

1. 普通升级优先使用 `WIFI-NAG-V1.0.4-OTA.bin`，在 WIFI-NAG 后台通过本地手动 OTA 上传。
2. 全新安装或需要清除全部设置时，使用 `WIFI-NAG-V1.0.4-FACTORY-16MB.bin` 从地址 `0x0` 写入。
3. 完整线刷会清除 NVS、Wi-Fi、BLE 绑定和历史设置；刷写前请确认文件 SHA256。
4. 设备重启后，确认后台固件版本显示为 `V1.0.4`。
