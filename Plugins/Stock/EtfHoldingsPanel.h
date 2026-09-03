#pragma once

#include <StockDef.h>
#include <Common.h>
#include <afxwin.h>
#include <vector>
#include <string>

// ETF 持仓面板绘制
// 职责：在指定矩形区域内绘制当前 ETF 的持仓成分股列表（序号、名称、成交量、今涨跌、仓位）
//       支持垂直滚动，隐藏滚动条，支持点击成分股切换
class CEtfHoldingsPanel
{
public:
	// 单行高度
	static int GetRowHeight();
	// 表头高度
	static int GetTableHeaderHeight();

	// 命中测试：返回点击的 item 下标（0 ~ itemCount-1），未命中返回 -1
	static int HitTest(CPoint pt, int left, int right, int height, int scrollOffset, int itemCount);

	// 绘制 ETF 持仓面板
	// left, right: 面板左右边界
	// height: 面板总高度（从 headerHeight 起算，含盘口标题栏）
	// data: ETF 持仓数据
	// scrollOffset: 列表垂直滚动偏移量
	// currentStockId: 当前选中的股票代码（用于高亮行）
	void Draw(CDC& memDC, int left, int right, int height, const STOCK::EtfHoldingsData& data,
		int scrollOffset = 0, const std::wstring& currentStockId = L"");
};
