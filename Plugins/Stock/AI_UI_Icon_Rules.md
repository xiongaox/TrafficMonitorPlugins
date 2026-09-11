# Stock 插件 UI 图标与按钮设计规范

> 适用于 `TrafficMonitorPlugins/Plugins/Stock` 项目。
> 项目技术栈为 **C++ / MFC / Win32 / DLL**。
>
> 本文件用于约束 AI 在修改、重构或新增 UI 时的图标和按钮实现方式，避免 AI 自行绘制图标导致风格不统一、比例失衡、视觉粗糙等问题。

---

## 1. 核心原则

### 1.1 禁止 AI 自行绘制常规图标

对于已经存在于成熟图标库中的图标，**禁止自行设计或绘制 SVG/GDI/GDI+ 图形**。

禁止以下做法：

- 使用 `MoveTo` / `LineTo` / `Rectangle` / `Ellipse` 等 GDI 图形临时拼装图标
- 使用 GDI+ Path 手工绘制图标
- AI 自行生成复杂 SVG `path`
- AI 自行创建 BMP/PNG 图标来替代现有图标库
- 使用 Emoji 作为 UI 图标
- 使用 Unicode 字符模拟图标，除非该字符本身就是项目明确规定的 UI 字符
- 为一个已经存在的通用图标重新设计一套图形

例如，“删除”已经有成熟图标时，不允许 AI 自己画一个垃圾桶。

---

## 2. 图标库规范

### 2.1 首选：Lucide

GitHub：

https://github.com/lucide-icons/lucide

Lucide 作为本项目的**主要图标来源**。

优点：

- 风格简洁
- 线性图标统一
- 适合深色桌面 UI
- 图标比例和视觉重量较统一
- 适合股票、监控、工具类软件
- 图标名称清晰，AI 容易准确调用

### 2.2 备用：Fluent UI System Icons

GitHub：

https://github.com/microsoft/fluentui-system-icons

当 Lucide 缺少合适图标，或者某个图标更符合 Windows 原生 UI 语义时，可以使用 Fluent UI System Icons。

优先考虑以下场景：

- Windows 系统级操作
- Windows 11 风格 UI
- 更适合 Win32/MFC 的系统操作图标

### 2.3 第二备用：Material Design Icons

GitHub：

https://github.com/Templarian/MaterialDesign

当 Lucide 和 Fluent UI 都没有合适图标时，再考虑 Material Design Icons。

---

## 3. 图标选择优先级

严格遵循以下优先级：

```text
Lucide
  ↓
Fluent UI System Icons
  ↓
Material Design Icons
  ↓
最后才考虑自定义图标
```

只有在以上成熟图标库都无法提供合理图标时，才允许创建自定义图标。

如果确实需要自定义图标，应尽可能保持：

- 与现有图标相同的视觉风格
- 相同的线宽
- 相同的画布尺寸
- 相同的留白比例
- 相同的视觉重量

不得因为一个特殊图标而破坏整个项目的图标体系。

---

## 4. 常用图标映射

项目中常见操作优先使用以下图标：

| UI 操作 | 首选图标 |
|---|---|
| 删除 | `trash-2` |
| 编辑 | `pencil` |
| 上移 | `arrow-up` |
| 下移 | `arrow-down` |
| 确定 | `check` |
| 取消 | `x` |
| 添加 | `plus` |
| 搜索 | `search` |
| 设置 | `settings` |
| 刷新 | `refresh-cw` |
| 查看 | `eye` |
| 隐藏 | `eye-off` |
| 返回 | `arrow-left` |
| 前进 | `arrow-right` |
| 更多 | `ellipsis` |
| 关闭 | `x` |
| 保存 | `save` |
| 警告 | `triangle-alert` |
| 信息 | `info` |
| 下载 | `download` |
| 上传 | `upload` |

如果图标库中存在语义更准确的图标，应优先使用语义准确的版本，而不是机械套用上述名称。

---

## 5. 不允许使用 Emoji

不要使用以下形式作为正式 UI 图标：

```text
🗑
✏️
⬆️
⬇️
✅
❌
⚙️
🔍
```

原因：

- 不同 Windows 环境下字体可能不同
- Emoji 风格不可控
- 与项目其他按钮风格不一致
- 尺寸、基线和颜色难以统一
- 不适合正式桌面软件 UI

统一使用图标库的矢量/字体图标。

---

## 6. 图标资源应集中管理

不要在每一个 `.cpp` 文件里重复处理图标。

建议建立统一的 Icon 管理层，例如：

```text
Plugins/Stock/
├─ Icons/
│  ├─ trash-2.svg
│  ├─ pencil.svg
│  ├─ arrow-up.svg
│  ├─ arrow-down.svg
│  ├─ check.svg
│  ├─ x.svg
│  └─ ...
```

并通过统一接口供 UI 使用。

推荐类似下面的调用方式：

```cpp
Icons::Delete()
Icons::Edit()
Icons::ArrowUp()
Icons::ArrowDown()
Icons::Check()
Icons::Close()
```

或者：

```cpp
SetButtonIcon(IDC_BTN_DELETE, Icons::Delete);
SetButtonIcon(IDC_BTN_EDIT, Icons::Edit);
SetButtonIcon(IDC_BTN_UP, Icons::ArrowUp);
SetButtonIcon(IDC_BTN_DOWN, Icons::ArrowDown);
```

具体 API 可以根据现有代码结构决定，但必须保证**图标来源和样式集中管理**。

---

## 7. MFC / Win32 实现要求

这是一个 MFC/Win32 DLL 项目。

因此实现图标时：

- 优先使用 Windows/MFC 原生可兼容方案
- 不要为了显示几个图标引入 Qt
- 不要引入 Dear ImGui
- 不要引入与项目现有架构无关的大型 GUI 框架
- 不要引入不必要的第三方运行时依赖

图标资源应该尽量能够：

- 随 DLL 一起发布
- 不依赖外部网络
- 不依赖运行时访问 GitHub
- 不要求用户额外安装字体或运行库（除非项目明确决定采用字体图标方案）

优先考虑将图标资源作为项目资源或构建产物随 DLL 一起分发。

---

## 8. 图标尺寸规范

整个项目应尽量统一图标尺寸。

推荐基准：

```text
小型按钮：14×14 ~ 16×16
普通按钮：16×16 ~ 18×18
工具栏按钮：18×18 ~ 20×20
大型操作区域：20×20 ~ 24×24
```

实际尺寸应根据现有 UI 密度统一，不要同一个窗口里一个图标 12px、另一个 24px、另一个 32px。

### 视觉尺寸比物理尺寸更重要

即使所有图标使用相同画布尺寸，不同图标的视觉面积也可能不同。

需要确保：

- 图标视觉中心一致
- 上下左右留白合理
- 不出现某个图标明显偏大或偏小
- 箭头、垃圾桶、编辑等图标的视觉重量接近

---

## 9. 线宽和视觉风格

同一界面中的图标必须尽可能保持统一的视觉重量。

不要出现：

```text
细线 Lucide
+
粗线 Font Awesome
+
实心 Material 图标
```

混杂使用。

默认优先使用**线性、简洁、低视觉噪声**的图标。

尤其是当前 Stock 插件的深色桌面 UI，应避免图标过于厚重、卡通化或复杂。

---

## 10. 按钮规范

图标不是孤立处理的，按钮整体也要统一。

### 10.1 图标按钮

例如：

```text
[ 删除 ]
[ 编辑 ]
[ ↑ ]
[ ↓ ]
```

应保证：

- 图标垂直居中
- 文本与图标间距一致
- 左右内边距一致
- 所有同组按钮高度一致
- 所有按钮的 hover / pressed / disabled 状态一致

### 10.2 图标 + 文字

推荐：

```text
[ 🗑 删除 ]
[ ✏ 编辑 ]
```

实际实现中不要使用 Emoji，而是：

```text
[ <icon> 删除 ]
[ <icon> 编辑 ]
```

图标与文字之间保持统一间距。

---

## 11. Hover / Pressed / Disabled 状态

图标的状态也必须统一。

至少需要考虑：

```text
Normal
Hover
Pressed
Disabled
```

不要只改变按钮背景，却让图标状态完全不协调。

如果项目使用自绘按钮，应让图标和文字跟随按钮状态变化。

例如：

```text
Normal   → 普通亮度
Hover    → 略微增强
Pressed  → 与按钮按下状态一致
Disabled → 降低视觉强调
```

不要通过随机颜色改变来制造状态。

---

## 12. 与当前 Stock 插件 UI 的适配

当前项目采用深色桌面 UI。

因此图标设计应遵循：

- 简洁
- 低干扰
- 不抢文字信息的视觉焦点
- 与深色背景具有足够对比度
- 不使用过多鲜艳颜色
- 操作语义优先于装饰性

特别是以下操作：

```text
删除
编辑
上移
下移
确定
取消
```

应使用非常容易识别的标准图标，不要重新设计。

---

## 13. AI 修改代码时的强制要求

当 AI 修改任何 `.cpp` / `.h` / `.rc` 文件涉及 UI 时，必须先检查：

1. 项目中是否已经存在统一的 Icon 管理方式
2. 是否已经存在对应图标资源
3. Lucide 是否已经提供该图标
4. 是否可以复用已有按钮样式
5. 是否可以复用已有尺寸、颜色和状态逻辑

禁止在不知道项目现有 UI 结构的情况下直接新增一套图标绘制逻辑。

---

## 14. 修改前必须先搜索

当需要添加一个图标时，AI 应先搜索项目：

```text
Icons
Icon
SetButtonIcon
SetIcon
CImageList
CBitmap
HICON
LoadIcon
LoadImage
```

以及搜索已经存在的按钮实现。

不要重复实现同类功能。

例如已经有：

```cpp
SetButtonIcon(...);
```

就不要重新写一套：

```cpp
LoadSvg(...);
DrawIcon(...);
```

---

## 15. 禁止为了一个图标大规模重构

引入图标库时必须遵循“最小改动原则”。

不要因为增加几个按钮图标，就：

- 重写整个 `ManagerDialog.cpp`
- 更换 GUI 框架
- 修改现有窗口布局系统
- 引入大型依赖
- 重写整个主题系统

应该优先：

```text
现有 UI
  ↓
增加统一图标资源
  ↓
增加 Icon 封装
  ↓
逐个替换已有图标
```

保证现有功能不受影响。

---

## 16. 自定义图标的特殊要求

只有以下情况才允许自定义：

- 图标库中完全不存在对应语义
- 项目特有的业务功能
- 股票插件特有的专属状态
- 必须表达项目独有概念

自定义时必须：

1. 首先参考 Lucide 风格
2. 采用统一画布尺寸
3. 使用统一线宽
4. 保持统一视觉中心
5. 保持统一留白
6. 不要加入无意义装饰

禁止“看起来差不多就算了”的手工图标。

---

## 17. 最终目标

本项目 UI 图标应达到以下效果：

```text
统一
整洁
克制
易识别
Windows 桌面软件风格
```

而不是：

```text
AI 每次临时设计一个图标
↓
每个图标长得不一样
↓
线宽不一样
↓
比例不一样
↓
按钮视觉重量不一样
```

**图标应该是项目基础设计系统的一部分，而不是 AI 临时生成的装饰。**

---

## 18. 给 AI 的最终执行规则

在本项目中，遇到任何 UI 图标需求时，严格执行：

```text
1. 先复用现有 Icon 资源
2. 没有则优先寻找 Lucide
3. Lucide 没有则寻找 Fluent UI System Icons
4. 仍然没有再考虑 Material Design Icons
5. 成熟图标库存在时，禁止自己画图标
6. 禁止使用 Emoji 代替正式 UI 图标
7. 禁止在多个 cpp 中重复实现图标加载/绘制逻辑
8. 所有图标统一尺寸、线宽、视觉重量和状态
9. 图标资源集中管理
10. 尽量将资源随 DLL 一起发布
11. 不为了图标引入 Qt / ImGui 等无关 GUI 框架
12. 修改前先检查项目现有 UI 和 Icon 实现
13. 遵循最小改动原则
14. 如果必须自定义图标，必须保持现有图标体系的风格一致
```

---

## 19. 当前项目建议

对于 `TrafficMonitorPlugins/Plugins/Stock`，优先采用：

```text
C++ / MFC / Win32
        │
        ├── Lucide Icons（主图标库）
        │
        ├── Fluent UI System Icons（Windows 风格备用）
        │
        └── Material Design Icons（数量不足时备用）
```

推荐首先把当前 UI 中的：

```text
删除
编辑
上移
下移
确定
取消
```

统一替换为成熟图标库中的标准图标，并建立统一的 Icon 管理接口。
