#pragma once

#include <afxwin.h>

// 图表绘制共享颜色常量
// 注：这些颜色宏在各图表模块（CStockListPanel/COrderBookPanel/CChartPrice 等）间共享

#define COLOR_WHITE               RGB(255, 255, 255)
#define COLOR_BLACK               RGB(30, 35, 45)

// 现代专业金融红绿语义色（护眼柔和版）
#define COLOR_RED_UP              RGB(246, 70, 93)     // 现代高级红-上涨/盈利 (#F6465D)
#define COLOR_GREEN_DOWN          RGB(14, 203, 129)    // 现代薄荷绿-下跌/亏损 (#0ECB81)
#define COLOR_GRAY_TEXT           RGB(120, 130, 145)   // 次要灰色文字
#define COLOR_GRAY_GRID           RGB(228, 231, 238)   // 浅灰网格线
#define COLOR_GRAY_MIDDLE         RGB(160, 168, 180)   // 中灰虚线
#define COLOR_GRAY_PURPLE         RGB(154, 151, 157)   // 灰紫色
#define COLOR_BLUE_COST           RGB(37, 99, 235)     // 现代宝蓝成本线
#define COLOR_DARK_GRAY_BORDER    RGB(218, 222, 230)   // 边框线
#define COLOR_BLUE_AVG1           RGB(59, 130, 246)    // 蓝色-1年均线
#define COLOR_GREEN_AVG2          RGB(14, 165, 233)    // 2年均线
#define COLOR_GREEN_AVG3          RGB(168, 85, 247)    // 3年均线
#define COLOR_LIGHT_BLUE          RGB(239, 246, 255)   // 淡蓝色背景
#define COLOR_LIGHT_GREEN         RGB(240, 253, 244)   // 淡绿色背景
#define COLOR_GOLDEN              RGB(245, 158, 11)    // 黄金色 (MA5)
#define COLOR_DARK_ORANGE         RGB(217, 119, 6)     // 暗橙色
#define COLOR_MACD_SUB            RGB(160, 170, 185)   // MACD辅助线浅灰色

// 现代盘口买卖五档深度条背景色（柔和浅色）
#define COLOR_DEPTH_SELL_BG       RGB(254, 226, 230)   // 卖单深度背景浅红
#define COLOR_DEPTH_BUY_BG        RGB(220, 252, 238)   // 买单深度背景浅绿

// 自选股列表与控件现代主题色
#define COLOR_ACCENT_BLUE         RGB(37, 99, 235)     // 品牌聚焦蓝
#define COLOR_PANEL_BG            RGB(248, 250, 252)   // 面板浅灰底色
#define COLOR_CARD_SELECTED       RGB(235, 243, 255)   // 列表选中项背景

// 涨幅/盈亏背景颜色
#define COLOR_BG_RED              RGB(239, 68, 68)     // >= 5% 红色背景
#define COLOR_BG_PURPLE           RGB(168, 85, 247)    // >= 10% 紫色背景
#define COLOR_BG_GREEN            RGB(34, 197, 94)     // <= -5% 绿色背景
#define COLOR_BG_DARK_GREEN       RGB(21, 128, 61)     // <= -10% 墨绿色背景
