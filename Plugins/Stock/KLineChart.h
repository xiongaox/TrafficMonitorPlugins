#pragma once

#include "ChartContext.h"
#include "Common.h"
#include "StockIndicator.h"
#include <StockDef.h>

// K线图绘制模块
// 职责：绘制日K/5分钟K/30分钟K的K线图、趋势图、量柱图，以及K线辅助元素（网格、均线、布林带等）
class CKLineChart
{
public:
	using PeriodPoint = CStockIndicator::PeriodPoint;

	// K线图悬停状态
	struct HoverState {
		bool isHoveringKLine{ false };
		bool isHoveringKLineVolume{ false };
		bool isHoveringKDJ{ false };
		int klineHoveredBarIndex{ -1 };
		CString klineHoverTip;
		CString klineVolumeHoverTip;
		CString klineTrendHoverTip;
		CString kdjHoverTip;
		bool showMA{ false };
		bool showBollBands{ true };
		bool showTrendView{ false };
		UIViewMode viewMode{ UI_VIEW_DAY_KLINE };
		int klinePeriodDays{ 250 };
		int scrollOffset{ 0 };
		std::wstring stockId;
	};

	// 准备K线绘制数据
	KLineDrawData PrepareKLineDrawData(int x, int y, int w, int h, const std::vector<STOCK::KLinePoint>& klineData, const HoverState& hover);

	// K线图辅助绘制
	std::vector<LabelInfo> DrawKLineMonthLines(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover);
	void DrawKLineMonthLabels(CDC& memDC, const KLineDrawData& drawData, const std::vector<LabelInfo>& labelInfos);
	void DrawMin5HourLines(CDC& memDC, const KLineDrawData& drawData);
	void DrawKLineGrid(CDC& memDC, const KLineDrawData& drawData);
	void DrawYearAverageLines(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover);
	void DrawMAIndicators(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover);
	void DrawCurrentPriceLine(CDC& memDC, const KLineDrawData& drawData);
	void DrawPriceLabels(CDC& memDC, const KLineDrawData& drawData);
	void DrawBollBands(CDC& memDC, const KLineDrawData& drawData);

	// K线图主体绘制
	void DrawKLineBars(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover);
	void DrawKLineBuyMarkers(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover);
	void DrawKLinePeriodMarkers(CDC& memDC, const KLineDrawData& drawData, const PeriodPoint periodHighs[3], const PeriodPoint periodLows[3]);
	// K线趋势图
	void DrawKLineTrendCurve(CDC& memDC, const KLineDrawData& drawData, std::vector<CPoint>& outPoints);
	void DrawKLineTrendBuyMarkers(CDC& memDC, const KLineDrawData& drawData, const std::vector<CPoint>& closePoints, const HoverState& hover);
	void DrawKLineTrendPeriodMarkers(CDC& memDC, const KLineDrawData& drawData, const std::vector<CPoint>& closePoints, const PeriodPoint periodHighs[3], const PeriodPoint periodLows[3]);
	// K线量柱图
};
