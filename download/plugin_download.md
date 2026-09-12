# TrafficMonitor 股票插件下载与安装说明

本页面提供 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 股票行情看盘插件（Stock Plugin）的各架构预编译版本下载与详细安装使用指南。

---

## 📦 插件版本下载

请根据您使用的 TrafficMonitor 架构（可查看任务管理器中进程名或关于页面）选择下载对应的压缩包：

| 架构版本 | 适用系统 / 环境 | 下载链接 | 文件说明 |
| :--- | :--- | :---: | :--- |
| **x64 Release**（推荐） | 64 位 Windows 系统及 64 位 TrafficMonitor | [**Stock_V2.0.7_x64.zip**](./Stock_V2.0.7_x64.zip) | 推荐绝大多数用户使用，性能最优 |
| **x86 Release** | 32 位 Windows 系统或 32 位 TrafficMonitor | [**Stock_V2.0.7_x86.zip**](./Stock_V2.0.7_x86.zip) | 适用于 32 位兼容环境 |
| **ARM64EC Release** | Windows on ARM 平台（如高通骁龙芯片、Surface Pro X） | [**Stock_V2.0.7_arm64ec.zip**](./Stock_V2.0.7_arm64ec.zip) | 专为 ARM64 设备原生优化 |

---

## 🚀 安装与启用步骤

1. **解压文件**：
   下载上述对应架构的 zip 文件并解压，得到核心插件动态库文件 `Stock.dll`。

2. **放置插件**：
   将 `Stock.dll` 复制到 TrafficMonitor 程序所在目录下的 `plugins` 文件夹内：
   ```text
   TrafficMonitor/
   ├── TrafficMonitor.exe
   └── plugins/
       └── Stock.dll
   ```
   > 若当前目录下没有 `plugins` 文件夹，请手动新建一个。

3. **重启 TrafficMonitor**：
   退出并重新启动 TrafficMonitor 程序。

4. **开启任务栏显示**：
   - 在任务栏 TrafficMonitor 窗口上点击鼠标右键；
   - 选择 **“显示设置”**；
   - 在已加载的项目列表中勾选 **“股票”**，点击“确定”即可生效。

---

## 💡 核心功能亮点

- **多市场行情覆盖**：支持 A股、港股、美股、ETF 基金、黄金及主流市场指数；
- **任务栏极简盯盘**：常驻任务栏显示当前股价、涨跌幅或当日持仓盈亏；
- **原地沉浸式悬浮窗**：左键点击任务栏即可展开，支持分时走势、日K/周K/月K、MA均线（MA5/MA20/MA60等）、MACD、KDJ、RSI、布林带等专业技术指标；
- **盘口与筹码分析**：买卖五档实时挂单量盘口（PK）与筹码峰成本分布图（CM）；
- **ETF 重仓穿透**：一键透视持仓 ETF 底层权重成分股及实时表现；
- **深度量化行情中心**：行业板块主力资金流向树图 (Treemap)、全天主力资金流折线走势、ETF资金申赎榜及全市场涨跌趋势大盘体检；
- **现代管理与多端同步**：拼音联想快速添加自选/持仓，支持多自定义分组与 WebDAV 云端自动备份。

---

## ⚠️ 注意事项与排错

1. **位数不匹配导致插件未加载**：
   若在 TrafficMonitor 的“插件管理”中未看到股票插件，请检查下载的 DLL 架构是否与运行中的 TrafficMonitor 完全一致（如 64 位程序必须使用 x64 版本的 `Stock.dll`）。
2. **行情刷新机制**：
   平时插件在盘中自动高频轮询；若遇休眠唤醒或网络波动，可随时在任务栏右键菜单中点击 **“刷新股票信息”** 立即强制发起最新行情请求。



