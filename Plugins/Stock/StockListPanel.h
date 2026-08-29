#pragma once

#include <string>
#include <vector>

// 左侧股票列表面板绘制
// 职责：在指定矩形区域内绘制股票列表（名称+代码），高亮当前选中股票，支持垂直滚动
// 数据来源：g_data 共享数据（线程安全加锁访问）
class CStockListPanel
{
public:
	// 获取用于自选列表显示的股票代码列表（过滤指数和港股）
	static std::vector<std::wstring> GetStockListCodes();

	// 绘制股票列表面板
	// x, y, w, h: 面板位置和尺寸
	// currentStockId: 当前选中的股票代码（用于高亮）
	// scrollOffset: 列表垂直滚动像素偏移
	void Draw(CDC& memDC, int x, int y, int w, int h, const std::wstring& currentStockId, int scrollOffset = 0);
};

