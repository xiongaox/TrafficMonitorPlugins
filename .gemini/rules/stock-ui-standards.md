# Stock Plugin UI Design and Development Standards

本文档定义了 TrafficMonitorPlugins/Plugins/Stock 模块的 UI 规范与构建同步规则。在开发任何新功能或修改界面时，必须严格遵守。

---

## 1. 核心视觉规范（Dark Theme Design System）

股票插件管理面板与悬浮窗口均采用统一的暗黑现代设计系统，**严禁引入任何 Windows 默认的亮白/经典灰色控件外观**。

### 色彩体系
- **主背景（Dialog BG）**：#0D0F15 (RGB: 13, 15, 21)
- **卡片底色（Card Container）**：#181B22 (RGB: 24, 27, 34)
- **控件底色（Input/Combobox Fill）**：#0D0F15 (RGB: 13, 15, 21)
- **列表交替行底色**：偶数行 #14161D (RGB: 20, 22, 29)，奇数行 #161920 (RGB: 22, 25, 32)
- **选中态底色（Selection/Active）**：#1C2D4B (RGB: 28, 45, 75)
- **品牌蓝强调色（Primary Accent）**：#2563EB (RGB: 37, 99, 235)
- **默认边框色（Default Border）**：#343A48 (RGB: 52, 58, 72)
- **悬停边框色（Hover Border）**：#4B5569 (RGB: 75, 85, 105)
- **聚焦边框色（Focused Border）**：#2563EB (RGB: 37, 99, 235)
- **主要文本（Primary Text）**：#F1F5F9 (RGB: 241, 245, 249)
- **次要/标签文本（Muted Text）**：#94A3B8 (RGB: 148, 163, 184)

### 字体与 DPI 缩放
- 统一使用 微软雅黑 字体。
- **所有坐标、宽高、边距、字体大小** 必须通过 g_data.DPI(...) 或 g_data.RDPI(...) 换算，确保高分屏缩放下对齐。

---

## 2. 控件组件规范

### 下拉框（ComboBox）
- 必须使用自绘类 CDarkComboBox（在 ManagerDialog.h 中定义）。
- .rc 资源定义中必须声明：CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP。
- 高度统一设置为 g_data.DPI(26)（与 Edit 输入框一致），在 OnInitDialog 中调用 m_combo.SetItemHeight(-1, g_data.DPI(26)) 与 m_combo.SetItemHeight(0, g_data.DPI(24))。

### 输入框（Edit Control）
- 布局时必须使用 PlaceEditInField(IDC_..., fieldRect) 注册字段矩形。
- 在 OnPaint 中统一由 DrawControlBorder(g, IDC_...) 绘制底色与状态边框。

### 按钮（Button）
- 必须在 OnDrawItem 中支持自绘，或者使用暗色刷子。确定按钮统一采用 #2563EB 品牌蓝填充与圆角矩形。

### 列表（ListCtrl）与表头（HeaderCtrl）
- 表头必须子类化为 CFlatHeaderCtrl。
- 列表项必须通过 OnListCustomDraw 处理交替行底色和选中态背景。

---

## 3. 构建与同步脚本规范

- sync-stock.ps1 及相关工具在进行文件替换时：
  - 如果无法自动结束已存在的进程或目标文件被占用，**必须立即报错退出（Fail Fast）**，绝对不允许使用 while 循环无限阻塞等待。
