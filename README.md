# cs_match_radar (CS 对局雷达)

基于 WinUI 3 (Windows App SDK) 和 C++/WinRT 开发的 CS2 对局战绩查询与实时对局雷达助手，支持 5E 平台与完美世界竞技平台。

## 特性

- **5E 对局与战绩查询**：支持玩家 UUID 查询历史对局战绩、详细数据统计、高光事件展示。
- **完美平台对局与战绩查询**：支持 SteamID 战绩查询与数据卡片。
- **完美平台扫码登录**：内嵌 WebView2 扫码与 Token 获取，加密保存至本地注册表。
- **实时对局雷达**：
  - 5E 对局通过 CDP (Chrome DevTools Protocol) 远程调试端口监听实时 Comet WebSocket 帧。
  - 完美对局通过自动解析本地客户端日志行。
  - 自动定锚双方 10 名玩家并展示段位、胜率、时长与安全风险评级。

## 技术栈

- **界面框架**：WinUI 3 / Windows App SDK (C++/WinRT, MSVC x64, C++20)
- **依赖管理**：vcpkg (curl, openssl, nlohmann-json), NuGet (Windows App SDK, WebView2)
- **网络与加密**：Windows.Web.Http, Windows.Networking.Sockets, Win32 CryptoAPI / DPAPI

## 构建说明

1. 使用 Visual Studio 2022+ 打开 `cs_match_radar.slnx`（或解决方案）。
2. 选择 `Debug` 或 `Release`，目标平台选择 `x64`。
3. 确保安装 Windows App SDK 组件及 C++/WinRT 支持。
4. 构建并运行 `CsMatchRadar` 项目。
