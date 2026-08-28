#pragma once

#include <afxwin.h>

// 现代暗黑专业主题表面底色体系 (Dark Surfaces)
#define COLOR_BG_DARK             RGB(18, 20, 26)      // 核心图表深色底 (#12141A)
#define COLOR_BG_HEADER           RGB(24, 27, 34)      // 顶部主标题栏 (#181B22)
#define COLOR_BG_SUBHEADER        RGB(21, 23, 30)      // 次级关联指数栏 (#15171E)
#define COLOR_BG_PANEL            RGB(20, 22, 29)      // 左侧列表与右侧盘口底色 (#14161D)
#define COLOR_BG_FOOTER           RGB(22, 24, 32)      // 底部系统状态栏 (#161820)
#define COLOR_BG_CARD             RGB(24, 27, 34)      // 卡片底色 (#181B22)
#define COLOR_CARD_SELECTED       RGB(28, 45, 75)      // 列表选中项高亮深蓝底
#define COLOR_ACCENT_BLUE         RGB(37, 99, 235)     // 品牌聚焦蓝 (Blue-600)
#define COLOR_DARK_GRAY_BORDER    RGB(38, 42, 54)      // 暗黑边框/分隔线 (#262A36)

// 文本颜色体系 (Typography)
#define COLOR_WHITE               RGB(241, 245, 249)   // 主文字高亮白 (Slate-100)
#define COLOR_BLACK               RGB(241, 245, 249)   // 暗黑主题下常规文本映射为浅色
#define COLOR_GRAY_TEXT           RGB(148, 163, 184)   // 次要辅助文字 (Slate-400)
#define COLOR_TEXT_DIM            RGB(100, 116, 139)   // 微弱文字/提示 (Slate-500)
#define COLOR_GRAY_PURPLE         RGB(148, 163, 184)   // 次要灰色

// 现代金融红绿语义色
#define COLOR_RED_UP              RGB(246, 70, 93)     // 现代金融红-上涨/多头 (#F6465D)
#define COLOR_GREEN_DOWN          RGB(14, 203, 129)    // 现代薄荷绿-下跌/空头 (#0ECB81)

// 网格与辅助线
#define COLOR_GRAY_GRID           RGB(32, 36, 46)      // 细微网格虚线
#define COLOR_GRAY_MIDDLE         RGB(55, 62, 78)      // 昨收平盘虚线

// 均线体系与技术指标色
#define COLOR_BLUE_COST           RGB(37, 99, 235)     // 宝蓝成本线
#define COLOR_BLUE_AVG1           RGB(59, 130, 246)    // 分时走势线/1年均线 (#3B82F6)
#define COLOR_GREEN_AVG2          RGB(56, 189, 248)    // 2年均线/MA10 (#38BDF8)
#define COLOR_GREEN_AVG3          RGB(192, 132, 252)   // 3年均线/MA20 (#C084FC)
#define COLOR_GOLDEN              RGB(245, 158, 11)    // 均价线/MA5 金黄 (#F59E0B)
#define COLOR_DARK_ORANGE         RGB(217, 119, 6)     // 暗橙色
#define COLOR_MACD_SUB            RGB(148, 163, 184)   // MACD辅助线

// 图表分时面积填充与深度条
#define COLOR_LIGHT_BLUE          RGB(22, 36, 62)      // 分时折线下方面积深蓝微渐变底
#define COLOR_LIGHT_GREEN         RGB(15, 45, 32)      // 浅绿背景
#define COLOR_DEPTH_SELL_BG       RGB(55, 22, 32)      // 卖单挂单深度条背景 (深红微透)
#define COLOR_DEPTH_BUY_BG        RGB(15, 48, 35)      // 买单挂单深度条背景 (深绿微透)
#define COLOR_DEPTH_SELL_HL       RGB(75, 28, 40)      // 卖一当前价高亮底
#define COLOR_DEPTH_BUY_HL        RGB(20, 68, 48)      // 买一当前价高亮底

// 涨幅/盈亏背景颜色
#define COLOR_BG_RED              RGB(159, 18, 57)     // >= 5% 红色背景
#define COLOR_BG_PURPLE           RGB(107, 33, 168)    // >= 10% 紫色背景
#define COLOR_BG_GREEN            RGB(6, 95, 70)       // <= -5% 绿色背景
#define COLOR_BG_DARK_GREEN       RGB(6, 78, 59)       // <= -10% 墨绿色背景
