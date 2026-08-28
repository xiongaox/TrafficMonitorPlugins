#pragma once

#include <StockDef.h>
#include "Common.h"
#include <string>
#include <vector>

// 盘口面板绘制
// 职责：在指定矩形区域内绘制五档买卖盘、委比、趋势判定、净比(1/5/10/20/99)、
//       振幅、换手率等盘口信息
// 数据来源：stockInfo（实时行情，其code字段作为数据缓存key）、klineData（K线数据，用于振幅计算）
// 说明：DrawTrend 依赖当前视图模式（分时/5分钟/30分钟/日K）来判定趋势方向
class COrderBookPanel
{
public:
	// 绘制盘口面板
	// left, right: 面板左右边界（含主标题栏下方的盘口标题栏区域）
	// height: 面板总高度（含盘口标题栏）
	// viewMode: 当前视图模式（用于趋势判定）
	void Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
		const std::vector<STOCK::KLinePoint>& klineData,
		UIViewMode viewMode);

private:
	// 盘口行数据
	struct OrderBookRow
	{
		STOCK::Price price;
		CString text;
		CString smallSuffix;
		CString rightAlignSuffix;  // 右对齐的瞬时变化量（+N/-N）
		COLORREF rightAlignSuffixColor{ RGB(0,0,0) };  // 右对齐后缀颜色
		CString cumVolSuffix;      // 累计成交量后缀（显示在瞬时变化量前面）
		COLORREF cumVolSuffixColor{ RGB(0,0,0) };  // 累计成交量后缀颜色
		COLORREF textColor;
		bool fillBackground{ false };
		COLORREF backgroundColor;
		bool drawSmallSuffix{ false };
		bool darkBackground{ false };  // 深色背景时文字改白色
		bool blink{ false };  // 闪烁效果：当前价=卖一/买一且挂单≤1万
		bool bold{ false };   // 粗体
	};

	// 布局上下文（由Draw计算，传递给各子函数）
	struct LayoutContext
	{
		int left;
		int right;
		int height;
		int headerHeight;
		int obTitleH;
		int topOffset;
		int panelW;
		int totalRows;
		int rowHeight;
		int contentH;
		int rem;
		int textX;
		// 计算第i行(0-based)的Y坐标
		int RowY(int i) const
		{
			if (i < rem) return topOffset + i * (rowHeight + 1);
			else return topOffset + rem * (rowHeight + 1) + (i - rem) * rowHeight;
		}
		// 计算第i行的高度
		int RowH(int i) const
		{
			return (i < rem) ? (rowHeight + 1) : rowHeight;
		}
	};

	// 绘制委比（行0）
	void DrawWeiBi(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// 绘制趋势判定（行1）
	void DrawTrend(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, UIViewMode viewMode);

	// 绘制卖盘行（最高+卖三~卖一，行2-5）
	void DrawAskRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, bool blinkOn);

	// 绘制净比00柱状图（行6）
	void DrawNetRatio00(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// 绘制买盘行（买一~买三+最低，行7-10）
	void DrawBidRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, bool blinkOn);

	// 绘制净比01/05/10/20（行11-14）
	void DrawNetRatioPeriods(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// 绘制净比99（行15）
	void DrawNetRatio99(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// 绘制振幅（行16）
	void DrawAmplitude(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo,
		const std::vector<STOCK::KLinePoint>& klineData);

	// 绘制换手率（行17）
	void DrawTurnoverRate(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo);

	// 辅助：绘制单行盘口文本（含小号后缀、右对齐后缀）
	void DrawOrderBookRowText(CDC& memDC, const OrderBookRow& row, int x, int y, int rowWidth, bool blinkOff = false);

	// 辅助：绘制净比条形图
	void DrawRatioBar(CDC& memDC, int x, int y, int w, int h, double ratio);

	// 辅助：在净比条形图上绘制文本
	void DrawNetRatioBarText(CDC& memDC, int x, int y, int w, int h, const CString& ratioText, const CString& diffText);

	// 辅助：获取净比颜色索引（0=0~30, 1=30~60, 2=60+）
	static int GetNetRatioColorIndex(double ratio);

	// 辅助：获取买一/卖一挂单瞬时变化量
	STOCK::Volume GetOrderDeltaLots(STOCK::Price price);

	// 辅助：获取盘口累计成交量（手）
	STOCK::Volume GetOrderBookCumVol(STOCK::Price price) const;

	// 辅助：计算净比趋势箭头
	static CString CalcNetRatioTrend(double ratio, double previousRatio);

	// 辅助：构建卖盘行数据
	OrderBookRow BuildAskRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const;

	// 辅助：构建买盘行数据
	OrderBookRow BuildBidRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const;

	// 辅助：绘制一组盘口行
	void DrawPriceRows(CDC& memDC, const LayoutContext& lc, const std::vector<OrderBookRow>& rows, int startRow, bool blinkOn);

private:
	// 缓存数据（原Draw中的static变量）
	static const COLORREF NET_RATIO_RED_COLORS[3];
	static const COLORREF NET_RATIO_GREEN_COLORS[3];
	// 净比99趋势缓存
	static std::map<std::wstring, double> m_lastNetRatioMap;
	static std::map<std::wstring, CString> m_lastNetRatioTrendMap;
	// 净比1/5/10/20趋势缓存
	static std::map<std::wstring, std::map<int, double>> m_lastPeriodRatioMap;
	static std::map<std::wstring, std::map<int, CString>> m_lastPeriodRatioTrendMap;
	// 挂单量累加缓存（每次Draw调用期间有效）
	std::shared_ptr<STOCK::StockData> m_stockDataForAccum;
};
