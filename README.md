# Stock++

这是用于 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的股票自选、行情与量化监控插件项目（原 TrafficMonitorPlugins 精简重构版）。

## 功能介绍

- 支持 A 股、ETF、港股、美股实时行情展示与持仓监控
- 支持自定义股票代码、自选列表及智能换手/振幅等统计
- 内置独立插件测试器（PluginTester），方便脱离主程序实时调试验证

## 项目结构

- Plugins/Stock/: 股票插件核心源码
- utilities/: 基础公共依赖库与 JSON 解析支持
- PluginTester/: 独立插件测试器（MFC 调试台）
- include/: TrafficMonitor 插件接口规范头文件
- Stock++.sln: 仅保留 Stock 相关项目的 Visual Studio 解决方案

## 编译与调试

1. 使用 Visual Studio 2022 打开根目录下的 Stock++.sln。
2. 选择 Release 与 x64 平台进行编译。
3. 编译生成的 Stock.dll 可在根目录双击 启动测试器.lnk 直接进行实时功能测试。
