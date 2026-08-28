#include "pch.h"
#include "TimelineChart.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include "StockIndicator.h"
#include "KLineChart.h"
#include "IndicatorChart.h"
#include "StatusBarPanel.h"
#include <algorithm>
#include <cmath>
#include <set>

// 时间标记结构体（供分时图绘制函数共用）
struct TimeMarker {
	const TCHAR* label;
	int minutesFromStart;
};

// 辅助函数：绘制价格点标签（最高/最低价标注）
static void DrawPricePointLabel(CDC& memDC, int pointX, int pointY, int chartLeft, int chartTop, int chartWidth, int chartHeight,
	STOCK::Price price, bool isHigh, COLORREF color)
{
	CString label = CCommon::FormatFloat(price);
	CSize labelSize = memDC.GetTextExtent(label);
	const int gap = g_data.RDPI(4);
	const int arrowGap = g_data.RDPI(10);
	const int chartRight = chartLeft + chartWidth;
	const int chartBottom = chartTop + chartHeight;

	int labelX = pointX - labelSize.cx / 2;
	int labelY = isHigh ? pointY - labelSize.cy - arrowGap : pointY + arrowGap;
	bool useSideLabel = labelX < chartLeft || labelX + labelSize.cx > chartRight;

	if (useSideLabel)
	{
		if (pointX < chartLeft + chartWidth / 2)
		{
			label.Format(_T("\u2190%s"), CCommon::FormatFloat(price));
			labelSize = memDC.GetTextExtent(label);
			labelX = pointX + gap;
		}
		else
		{
			label.Format(_T("%s\u2192"), CCommon::FormatFloat(price));
			labelSize = memDC.GetTextExtent(label);
			labelX = pointX - labelSize.cx - gap;
		}
		labelY = pointY - labelSize.cy / 2;
		labelX = max(chartLeft, min(labelX, chartRight - labelSize.cx));
		labelY = max(chartTop, min(labelY, chartBottom - labelSize.cy));
		memDC.SetTextColor(color);
		memDC.TextOut(labelX, labelY, label);
		return;
	}

	labelY = max(chartTop, min(labelY, chartBottom - labelSize.cy));
	memDC.SetTextColor(color);
	memDC.TextOut(labelX, labelY, label);

	int fromX = labelX + labelSize.cx / 2;
	int fromY = isHigh ? labelY + labelSize.cy : labelY;
	if (abs(pointY - fromY) > g_data.RDPI(2))
	{
		CPen pen(PS_SOLID, 1, color);
		CPen* pOldPen = memDC.SelectObject(&pen);
		memDC.MoveTo(fromX, fromY);
		memDC.LineTo(pointX, pointY);

		int dir = (pointY >= fromY) ? 1 : -1;
		int arrowLen = g_data.RDPI(4);
		int arrowHalf = g_data.RDPI(3);
		memDC.MoveTo(pointX, pointY);
		memDC.LineTo(pointX - arrowHalf, pointY - dir * arrowLen);
		memDC.MoveTo(pointX, pointY);
		memDC.LineTo(pointX + arrowHalf, pointY - dir * arrowLen);
		memDC.SelectObject(pOldPen);
	}
}

void CTimelineChart::DrawTimelineHeader(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	CPoint origOrg = memDC.GetViewportOrg();
	memDC.SetViewportOrg(0, 0);

	CString macdSignal;
	auto stockData = g_data.GetStockData(hover.stockId);
	if (stockData && stockData->info.is_ok)
		macdSignal = stockData->macdTrendSignal;

	CStatusBarPanel statusBarPanel;
	statusBarPanel.DrawHeader(memDC, ctx.realtimeData, ctx.windowWidth, g_data.RDPI(26), macdSignal);
	memDC.SetViewportOrg(origOrg);
}

void CTimelineChart::DrawTimelineBackgroundHighlights(CDC& memDC, const TimelineDrawContext& ctx, UIViewMode viewMode)
{
	(void)memDC; (void)ctx; (void)viewMode;
	// 现代暗黑主题保持纯净统一背景
	return;
}

void CTimelineChart::DrawTimelineBackgroundHighlightsForArea(CDC& memDC, const TimelineDrawContext& ctx, int chartTop, int chartHeight, UIViewMode viewMode)
{
	(void)memDC; (void)ctx; (void)chartTop; (void)chartHeight; (void)viewMode;
	// 现代暗黑专业主题保持纯净统一背景
	return;
}

void CTimelineChart::DrawTimelineGridLines(CDC& memDC, const TimelineDrawContext& ctx)
{
	CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&pGrid);

	if (ctx.timelinePoint && !ctx.timelinePoint->empty())
	{
		const int totalPts = static_cast<int>(ctx.timelinePoint->size());
		const int numVLines = 6;
		for (int i = 0; i <= numVLines; i++)
		{
			int idx = totalPts * i / numVLines;
			if (idx >= totalPts) idx = totalPts - 1;
			int xPos = ctx.chartWidth * i / numVLines;
			memDC.MoveTo(xPos, ctx.priceChartTop);
			memDC.LineTo(xPos, ctx.priceChartTop + ctx.priceChartHeight);
		}
	}

	memDC.MoveTo(0, ctx.priceChartTop + ctx.priceChartHeight);
	memDC.LineTo(ctx.chartWidth, ctx.priceChartTop + ctx.priceChartHeight);

	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.niceStep > 0)
	{
		CPen pGridLine(PS_DOT, 1, COLOR_GRAY_GRID);
		memDC.SelectObject(&pGridLine);
		double priceRange = ctx.maxPrice - ctx.minPrice;
		double unitY = ctx.unitY;
		int labelCount = static_cast<int>(round(priceRange / ctx.niceStep));
		for (int i = 0; i <= labelCount; i++)
		{
			double labelPrice = round((ctx.minPrice + i * ctx.niceStep) * 1000.0) / 1000.0;
			int y = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((labelPrice - ctx.minPrice) * unitY));
			memDC.MoveTo(0, y);
			memDC.LineTo(ctx.chartWidth, y);
		}
	}

	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.realtimeData.prevClosePrice > 0
		&& ctx.realtimeData.prevClosePrice >= ctx.minPrice && ctx.realtimeData.prevClosePrice <= ctx.maxPrice)
	{
		CPen pMiddleLine(PS_DASHDOT, 1, COLOR_GRAY_MIDDLE);
		memDC.SelectObject(&pMiddleLine);
		int prevCloseY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((ctx.realtimeData.prevClosePrice - ctx.minPrice) * ctx.unitY));
		prevCloseY = max(ctx.priceChartTop, min(prevCloseY, ctx.priceChartTop + ctx.priceChartHeight));
		memDC.MoveTo(0, prevCloseY);
		memDC.LineTo(ctx.chartWidth, prevCloseY);
	}

	memDC.SelectObject(pOldPen);
}

void CTimelineChart::DrawTimelinePriceLabels(CDC& memDC, const TimelineDrawContext& ctx)
{
	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.niceStep > 0)
	{
		int oldBkMode = memDC.SetBkMode(TRANSPARENT);
		double priceRange = ctx.maxPrice - ctx.minPrice;
		double unitY = ctx.unitY;

		int labelCount = static_cast<int>(round(priceRange / ctx.niceStep));
		for (int i = 0; i <= labelCount; i++)
		{
			double labelPrice = round((ctx.minPrice + i * ctx.niceStep) * 1000.0) / 1000.0;
			int y = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((labelPrice - ctx.minPrice) * unitY));
			CString priceTxt = ctx.realtimeData.IsETF() ? CCommon::FormatETFPrice(labelPrice) : CCommon::FormatFloat(labelPrice);
			CSize sz = memDC.GetTextExtent(priceTxt);
			int labelX = -sz.cx - g_data.RDPI(4);
			int labelY = y - sz.cy / 2;
			if (ctx.realtimeData.prevClosePrice > 0 && labelPrice > ctx.realtimeData.prevClosePrice + ctx.niceStep * 0.01)
				memDC.SetTextColor(COLOR_RED_UP);
			else if (ctx.realtimeData.prevClosePrice > 0 && labelPrice < ctx.realtimeData.prevClosePrice - ctx.niceStep * 0.01)
				memDC.SetTextColor(COLOR_GREEN_DOWN);
			else
				memDC.SetTextColor(COLOR_WHITE);
			memDC.TextOut(labelX, labelY, priceTxt);
		}

		if (ctx.realtimeData.prevClosePrice > 0
			&& ctx.realtimeData.prevClosePrice >= ctx.minPrice && ctx.realtimeData.prevClosePrice <= ctx.maxPrice)
		{
			memDC.SetTextColor(COLOR_TEXT_MUTED);
			CString prevTxt = ctx.realtimeData.IsETF() ? CCommon::FormatETFPrice(ctx.realtimeData.prevClosePrice) : CCommon::FormatFloat(ctx.realtimeData.prevClosePrice);
			CSize prevSize = memDC.GetTextExtent(prevTxt);
			int prevY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((ctx.realtimeData.prevClosePrice - ctx.minPrice) * unitY));
			int labelY = prevY - prevSize.cy / 2;
			memDC.TextOut(-prevSize.cx - g_data.RDPI(4), labelY, prevTxt);
		}
		memDC.SetBkMode(oldBkMode);
		return;
	}

	STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	memDC.SetTextColor(COLOR_RED_UP);
	CString upperLimitTxt = CCommon::FormatFloat(ctx.realtimeData.prevClosePrice + priceLimit);
	CSize upperSize = memDC.GetTextExtent(upperLimitTxt);
	int upperY = ctx.priceChartTop - upperSize.cy / 2;
	upperY = max(ctx.priceChartTop, min(upperY, ctx.priceChartTop + ctx.priceChartHeight - upperSize.cy));
	memDC.TextOut(-upperSize.cx - g_data.RDPI(4), upperY, upperLimitTxt);

	memDC.SetTextColor(COLOR_GREEN_DOWN);
	CString lowerLimitTxt = CCommon::FormatFloat(ctx.realtimeData.prevClosePrice - priceLimit);
	CSize lowerSize = memDC.GetTextExtent(lowerLimitTxt);
	int lowerY = ctx.priceChartTop + ctx.priceChartHeight - lowerSize.cy / 2;
	lowerY = max(ctx.priceChartTop, min(lowerY, ctx.priceChartTop + ctx.priceChartHeight - lowerSize.cy));
	memDC.TextOut(-lowerSize.cx - g_data.RDPI(4), lowerY, lowerLimitTxt);

	memDC.SetTextColor(COLOR_GRAY_PURPLE);
	CString middleTxt = CCommon::FormatFloat(ctx.realtimeData.prevClosePrice);
	CSize midSize = memDC.GetTextExtent(middleTxt);
	int midY = ctx.priceChartTop + ctx.priceChartHeight / 2 - midSize.cy / 2;
	midY = max(ctx.priceChartTop, min(midY, ctx.priceChartTop + ctx.priceChartHeight - midSize.cy));
	memDC.TextOut(-midSize.cx - g_data.RDPI(4), midY, middleTxt);

	memDC.SetBkMode(oldBkMode);
}

void CTimelineChart::DrawTimelineCostAndProfitLines(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	(void)memDC;
	(void)ctx;
	(void)hover;
}

void CTimelineChart::DrawTimelineGridAndLines(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	DrawTimelineBackgroundHighlights(memDC, ctx, hover.viewMode);
	DrawTimelineGridLines(memDC, ctx);
	DrawTimelinePriceLabels(memDC, ctx);
	DrawTimelineCostAndProfitLines(memDC, ctx, hover);
}

void CTimelineChart::DrawTimelinePriceCurve(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	if (timelinePoint.empty())
		return;

	const int totalPoints = static_cast<int>(timelinePoint.size());
	const int xAxisPts = ctx.xAxisPoints > 0 ? ctx.xAxisPoints : totalPoints;

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	// 1. 提取价格点列与前值顺延（保证永不断线）
	std::vector<Gdiplus::PointF> pricePoints;
	pricePoints.reserve(totalPoints);
	STOCK::Price lastValidPrice = ctx.realtimeData.prevClosePrice > 0 ? ctx.realtimeData.prevClosePrice : 0;
	for (int i = 0; i < totalPoints; i++)
	{
		STOCK::Price p = timelinePoint[i].price;
		if (p > 0)
			lastValidPrice = p;
		else if (lastValidPrice > 0)
			p = lastValidPrice;

		float pointX = (ctx.chartWidth / static_cast<float>(xAxisPts)) * (i + 0.5f);
		float yVal = static_cast<float>((p - minPrice) * unitY);
		float py = ctx.priceChartTop + ctx.priceChartHeight - yVal;
		py = (std::max)(static_cast<float>(ctx.priceChartTop), (std::min)(py, static_cast<float>(ctx.priceChartTop + ctx.priceChartHeight)));
		pricePoints.push_back(Gdiplus::PointF(pointX, py));
	}

	// 2. 提取均价点列与前值顺延（保证均价线永不断线）
	std::vector<Gdiplus::PointF> avgPoints;
	avgPoints.reserve(totalPoints);
	STOCK::Price lastValidAvgPrice = (pricePoints.empty() ? 0 : timelinePoint[0].price);
	for (int i = 0; i < totalPoints; i++)
	{
		STOCK::Price ap = timelinePoint[i].averagePrice;
		if (ap > 0)
			lastValidAvgPrice = ap;
		else if (lastValidAvgPrice > 0)
			ap = lastValidAvgPrice;
		else if (timelinePoint[i].price > 0)
			ap = timelinePoint[i].price;

		float pointX = (ctx.chartWidth / static_cast<float>(xAxisPts)) * (i + 0.5f);
		float yVal = static_cast<float>((ap - minPrice) * unitY);
		float py = ctx.priceChartTop + ctx.priceChartHeight - yVal;
		py = (std::max)(static_cast<float>(ctx.priceChartTop), (std::min)(py, static_cast<float>(ctx.priceChartTop + ctx.priceChartHeight)));
		avgPoints.push_back(Gdiplus::PointF(pointX, py));
	}

	// 3. 布林带数据计算（自适应扩展累积窗口，从09:30第1分钟起连续出线，绝不断线）
	std::vector<Gdiplus::PointF> upperPoints, midPoints, lowerPoints;
	if (hover.showBollBands)
	{
		const int N = 20;
		const double K = 2.0;
		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;

		upperPoints.reserve(totalPoints);
		midPoints.reserve(totalPoints);
		lowerPoints.reserve(totalPoints);

		for (int i = 0; i < totalPoints; i++)
		{
			int globalIdx = ctx.startIndex + i;
			int availableCount = (std::min)(N, globalIdx + 1);
			int startJ = (std::max)(0, globalIdx - availableCount + 1);

			double sum = 0;
			int validCnt = 0;
			for (int j = startJ; j <= globalIdx && j < static_cast<int>(fullData.size()); j++)
			{
				if (fullData[j].price > 0)
				{
					sum += fullData[j].price;
					validCnt++;
				}
			}

			double ma = validCnt > 0 ? (sum / validCnt) : (i < static_cast<int>(pricePoints.size()) ? timelinePoint[i].price : minPrice);
			double variance = 0;
			for (int j = startJ; j <= globalIdx && j < static_cast<int>(fullData.size()); j++)
			{
				if (fullData[j].price > 0)
				{
					double diff = fullData[j].price - ma;
					variance += diff * diff;
				}
			}
			double stddev = validCnt > 1 ? std::sqrt(variance / validCnt) : 0.0;
			double upper = ma + K * stddev;
			double lower = ma - K * stddev;

			float pointX = (ctx.chartWidth / static_cast<float>(xAxisPts)) * (i + 0.5f);
			auto calcPy = [&](double price) -> float {
				float py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<float>((price - minPrice) * unitY);
				return (std::max)(static_cast<float>(ctx.priceChartTop), (std::min)(py, static_cast<float>(ctx.priceChartTop + ctx.priceChartHeight)));
			};

			upperPoints.push_back(Gdiplus::PointF(pointX, calcPy(upper)));
			midPoints.push_back(Gdiplus::PointF(pointX, calcPy(ma)));
			lowerPoints.push_back(Gdiplus::PointF(pointX, calcPy(lower)));
		}
	}

	// 4. MA均线点列（MA5/MA17/MA60）
	std::vector<Gdiplus::PointF> ma5Points, ma17Points, ma60Points;
	if (hover.showMA)
	{
		ma5Points.reserve(totalPoints);
		ma17Points.reserve(totalPoints);
		ma60Points.reserve(totalPoints);

		for (int i = 0; i < totalPoints; i++)
		{
			const auto& item = timelinePoint[i];
			float pointX = (ctx.chartWidth / static_cast<float>(xAxisPts)) * (i + 0.5f);
			auto calcPy = [&](STOCK::Price price) -> float {
				float py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<float>((price - minPrice) * unitY);
				return (std::max)(static_cast<float>(ctx.priceChartTop), (std::min)(py, static_cast<float>(ctx.priceChartTop + ctx.priceChartHeight)));
			};

			if (item.ma5 > 0) ma5Points.push_back(Gdiplus::PointF(pointX, calcPy(item.ma5)));
			if (item.ma17 > 0) ma17Points.push_back(Gdiplus::PointF(pointX, calcPy(item.ma17)));
			if (item.ma60 > 0) ma60Points.push_back(Gdiplus::PointF(pointX, calcPy(item.ma60)));
		}
	}

	// 5. GDI+ 抗锯齿丝滑渲染
	{
		Gdiplus::Graphics graphics(memDC.GetSafeHdc());
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

		// (1) 走势线下方面积柔和垂直线性渐变（同花顺高级金融质感）
		if (pricePoints.size() >= 2)
		{
			Gdiplus::GraphicsPath areaPath;
			float bottomY = static_cast<float>(ctx.priceChartTop + ctx.priceChartHeight);
			areaPath.AddLine(pricePoints[0].X, bottomY, pricePoints[0].X, pricePoints[0].Y);
			areaPath.AddCurve(pricePoints.data(), static_cast<int>(pricePoints.size()), 0.25f);
			areaPath.AddLine(pricePoints.back().X, pricePoints.back().Y, pricePoints.back().X, bottomY);
			areaPath.CloseFigure();

			Gdiplus::RectF gradRect(
				0.0f,
				static_cast<float>(ctx.priceChartTop),
				static_cast<float>(ctx.chartWidth),
				static_cast<float>(ctx.priceChartHeight)
			);
			Gdiplus::LinearGradientBrush areaBrush(
				gradRect,
				Gdiplus::Color(45, 33, 150, 243),  // 顶部半透明宝蓝
				Gdiplus::Color(2, 33, 150, 243),   // 底部近透明消隐
				Gdiplus::LinearGradientModeVertical
			);
			graphics.FillPath(&areaBrush, &areaPath);
		}

		// (2) 布林带（半透明抗锯齿平滑虚线）
		if (hover.showBollBands && midPoints.size() >= 2)
		{
			Gdiplus::Pen upperPen(Gdiplus::Color(190, 248, 113, 113), 1.1f);
			upperPen.SetDashStyle(Gdiplus::DashStyleDash);
			graphics.DrawCurve(&upperPen, upperPoints.data(), static_cast<int>(upperPoints.size()), 0.25f);

			Gdiplus::Pen midPen(Gdiplus::Color(190, 96, 165, 250), 1.1f);
			midPen.SetDashStyle(Gdiplus::DashStyleDash);
			graphics.DrawCurve(&midPen, midPoints.data(), static_cast<int>(midPoints.size()), 0.25f);

			Gdiplus::Pen lowerPen(Gdiplus::Color(190, 52, 211, 153), 1.1f);
			lowerPen.SetDashStyle(Gdiplus::DashStyleDash);
			graphics.DrawCurve(&lowerPen, lowerPoints.data(), static_cast<int>(lowerPoints.size()), 0.25f);
		}

		// (3) MA均线（平滑抗锯齿实线）
		if (hover.showMA)
		{
			if (ma5Points.size() >= 2)
			{
				Gdiplus::Pen ma5Pen(Gdiplus::Color(220, 251, 191, 36), 1.1f);
				ma5Pen.SetLineJoin(Gdiplus::LineJoinRound);
				graphics.DrawCurve(&ma5Pen, ma5Points.data(), static_cast<int>(ma5Points.size()), 0.25f);
			}
			if (ma17Points.size() >= 2)
			{
				Gdiplus::Pen ma17Pen(Gdiplus::Color(220, 56, 189, 248), 1.1f);
				ma17Pen.SetLineJoin(Gdiplus::LineJoinRound);
				graphics.DrawCurve(&ma17Pen, ma17Points.data(), static_cast<int>(ma17Points.size()), 0.25f);
			}
			if (ma60Points.size() >= 2)
			{
				Gdiplus::Pen ma60Pen(Gdiplus::Color(220, 192, 132, 252), 1.1f);
				ma60Pen.SetLineJoin(Gdiplus::LineJoinRound);
				graphics.DrawCurve(&ma60Pen, ma60Points.data(), static_cast<int>(ma60Points.size()), 0.25f);
			}
		}

		// (4) 分时均价线（1.3px 柔和暖金平滑曲线）
		if (avgPoints.size() >= 2)
		{
			Gdiplus::Pen avgPen(Gdiplus::Color(240, 255, 179, 0), 1.3f);
			avgPen.SetLineJoin(Gdiplus::LineJoinRound);
			avgPen.SetStartCap(Gdiplus::LineCapRound);
			avgPen.SetEndCap(Gdiplus::LineCapRound);
			graphics.DrawCurve(&avgPen, avgPoints.data(), static_cast<int>(avgPoints.size()), 0.25f);
		}

		// (5) 分时价格走势线（1.8px 经典同花顺亮蓝平滑曲线）
		if (pricePoints.size() >= 2)
		{
			Gdiplus::Pen pricePen(Gdiplus::Color(255, 33, 150, 243), 1.8f);
			pricePen.SetLineJoin(Gdiplus::LineJoinRound);
			pricePen.SetStartCap(Gdiplus::LineCapRound);
			pricePen.SetEndCap(Gdiplus::LineCapRound);
			graphics.DrawCurve(&pricePen, pricePoints.data(), static_cast<int>(pricePoints.size()), 0.25f);
		}
	}

	// 6. 最高/最低价标签
	if (!pricePoints.empty())
	{
		STOCK::Price hiPrice = 0, loPrice = (std::numeric_limits<STOCK::Price>::max)();
		int hiIdx = -1, loIdx = -1;
		for (int i = 0; i < totalPoints; i++)
		{
			STOCK::Price p = timelinePoint[i].price;
			if (p > 0)
			{
				if (p > hiPrice) { hiPrice = p; hiIdx = i; }
				if (p >= hiPrice) { hiPrice = p; hiIdx = i; }
				if (p < loPrice) { loPrice = p; loIdx = i; }
				if (p <= loPrice) { loPrice = p; loIdx = i; }
			}
		}

		if (hiIdx >= 0 && hiPrice > 0 && hiIdx < static_cast<int>(pricePoints.size()))
		{
			int hiX = static_cast<int>(round(pricePoints[hiIdx].X));
			int hiY = static_cast<int>(round(pricePoints[hiIdx].Y));
			DrawPricePointLabel(memDC, hiX, hiY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				hiPrice, true, COLOR_RED_UP);
		}

		if (loIdx >= 0 && loPrice > 0 && loIdx != hiIdx && loIdx < static_cast<int>(pricePoints.size()))
		{
			int loX = static_cast<int>(round(pricePoints[loIdx].X));
			int loY = static_cast<int>(round(pricePoints[loIdx].Y));
			DrawPricePointLabel(memDC, loX, loY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				loPrice, false, COLOR_GREEN_DOWN);
		}
	}

	// 绘制基金净值曲线
	if (ctx.realtimeData.IsETF())
	{
		auto priceToY = [&](double price) -> int {
			return ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
			};

		const auto& fullTimeline = *ctx.fullTimeline;
		std::map<int, double> iopvByIndex;

		// 优先使用内存中fullTimeline的iopv字段（ApplyTimeline/ApplyFundIOPV已填充）
		for (int i = 0; i < static_cast<int>(fullTimeline.size()); i++)
		{
			if (fullTimeline[i].iopv > 0)
			{
				iopvByIndex[i] = fullTimeline[i].iopv;
			}
		}

		// 始终从数据库补充净值数据（LoadLatestFundNavCache已过滤非交易时段，含午休11:30-13:00）
		// 不依赖内存iopv的覆盖度判断，避免上午数据已占满一半时跳过数据库，导致下午曲线缺失
		{
			auto navPoints = g_data.GetDbManager().LoadLatestFundNavCache(hover.stockId);
			if (!navPoints.empty())
			{
				std::map<std::string, int> fullTimeIndexMap;
				for (int i = 0; i < static_cast<int>(fullTimeline.size()); i++)
				{
					std::string hhmm = fullTimeline[i].time.substr(0, 5);
					fullTimeIndexMap[hhmm] = i;
				}

				for (const auto& nav : navPoints)
				{
					auto it = fullTimeIndexMap.find(nav.time);
					if (it != fullTimeIndexMap.end())
					{
						iopvByIndex[it->second] = nav.iopv;
					}
				}
			}
		}

		// 追加实时IOPV到最后一个分时点
		if (ctx.realtimeData.iopv > 0 && !fullTimeline.empty())
		{
			int lastIdx = static_cast<int>(fullTimeline.size()) - 1;
			iopvByIndex[lastIdx] = ctx.realtimeData.iopv;
		}
		if (!iopvByIndex.empty())
		{
			const COLORREF navColor = RGB(160, 32, 240);
			CPen navPen(PS_SOLID, 1, navColor);
			CPen* pOldPen = memDC.SelectObject(&navPen);
			bool firstNavPoint = true;

			int startIdx = ctx.startIndex;
			int visCount = ctx.visibleCount;
			int drawnCount = 0;

			for (const auto& kv : iopvByIndex)
			{
				int fullIdx = kv.first;
				double iopvVal = kv.second;

				if (fullIdx < startIdx || fullIdx >= startIdx + visCount)
					continue;

				int relIdx = fullIdx - startIdx;
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) * relIdx) + static_cast<int>(ctx.chartWidth / static_cast<float>(xAxisPts) / 2);
				int pointY = priceToY(iopvVal);
				if (firstNavPoint)
				{
					memDC.MoveTo(pointX, pointY);
					firstNavPoint = false;
				}
				else
				{
					memDC.LineTo(pointX, pointY);
				}
				drawnCount++;
			}

			memDC.SelectObject(pOldPen);
		}
	}

	// 绘制智能分析买卖点标记
	{
		auto stockData = g_data.GetStockData(hover.stockId);
		if (stockData)
		{
			auto min30KLineObj = stockData->getMin30KLineData();

			if (min30KLineObj && min30KLineObj->data.size() >= 22)
			{
				std::vector<STOCK::Bar> bars30;
				bars30.reserve(min30KLineObj->data.size());
				for (const auto& kp : min30KLineObj->data) bars30.push_back(STOCK::Bar::FromKLinePoint(kp));

				auto buySignals = std::vector<bool>(totalPoints, false);
				auto sellSignals = std::vector<bool>(totalPoints, false);
				auto forbidSignals = std::vector<bool>(totalPoints, false);
				auto buyReasons = std::vector<CString>(totalPoints);
				auto sellReasons = std::vector<CString>(totalPoints);
				auto noLabelSignals = std::vector<bool>(totalPoints, false);

				if (hover.viewMode < UI_VIEW_DAY_KLINE && ctx.fullTimeline && ctx.fullTimeline->size() >= 120)
				{
					std::vector<STOCK::Bar> bars1m = CSignalAnalyzer::ConvertTimelineToBars(*ctx.fullTimeline);
					auto ar = CSignalAnalyzer::AnalyzeSignalAtFromTimeline(bars1m, bars30, static_cast<int>(bars1m.size()) - 1);
					auto& allSignals = ar.batchSignals;

					int fullToVisibleOffset = ctx.startIndex;

					std::vector<bool> filteredSignals(allSignals.size(), false);
					{
						int lastDrawnBuyBar = -10, lastDrawnSellBar = -10;
						for (size_t i = 0; i < allSignals.size(); i++)
						{
							if (allSignals[i].isForbid) continue;
							int bi = allSignals[i].barIndex;
							if (allSignals[i].isBuy)
							{
								if (bi - lastDrawnBuyBar < 5)
									filteredSignals[i] = true;
								else
									lastDrawnBuyBar = bi;
							}
							else
							{
								if (bi - lastDrawnSellBar < 5)
									filteredSignals[i] = true;
								else
									lastDrawnSellBar = bi;
							}
						}
					}

					for (size_t si = 0; si < allSignals.size(); si++)
					{
						const auto& sig = allSignals[si];
						int k = sig.barIndex - fullToVisibleOffset;
						if (k < 0 || k >= totalPoints) continue;
						if (sig.isForbid)
						{
							forbidSignals[k] = true;
							buySignals[k] = false;
						}
						else if (!forbidSignals[k] || !sig.isBuy)
						{
							if (sig.isBuy)
							{
								if (!forbidSignals[k])
								{
									buySignals[k] = true;
									buyReasons[k] = sig.reason;
									if (filteredSignals[si])
										noLabelSignals[k] = true;
								}
							}
							else
							{
								sellSignals[k] = true;
								sellReasons[k] = sig.reason;
								if (filteredSignals[si])
									noLabelSignals[k] = true;
							}
						}
					}
				}
				else if (hover.viewMode >= UI_VIEW_DAY_KLINE)
				{
					STOCK::KLineData* klineObj = nullptr;
					if (hover.viewMode == UI_VIEW_DAY_KLINE)
						klineObj = stockData->getKLineData();
					else if (hover.viewMode == UI_VIEW_WEEK_KLINE)
						klineObj = stockData->getWeekKLineData();
					else if (hover.viewMode == UI_VIEW_MONTH_KLINE)
						klineObj = stockData->getMonthKLineData();

					if (klineObj && klineObj->data.size() >= 26)
					{
						std::vector<STOCK::Bar> barsK;
						barsK.reserve(klineObj->data.size());
						for (const auto& kp : klineObj->data) barsK.push_back(STOCK::Bar::FromKLinePoint(kp));

						auto ar = CSignalAnalyzer::AnalyzeSignalAt(barsK, bars30, static_cast<int>(barsK.size()) - 1);
						auto& allSignals = ar.batchSignals;

						std::vector<CSignalAnalyzer::SmartSignalPoint> signals;
						for (const auto& sig : allSignals)
						{
							if (sig.barIndex < 0 || sig.barIndex >= static_cast<int>(klineObj->data.size()))
								continue;
							signals.push_back(sig);
						}

						std::map<std::string, int> timeIndexMap;
						for (int k = 0; k < totalPoints; k++)
						{
							const auto& t = timelinePoint[k].time;
							timeIndexMap[t] = k;
							if (t.length() > 5 && t[5] == ':')
								timeIndexMap[t.substr(0, 5)] = k;
						}

						std::set<int> klineFilteredBarIndices;
						{
							bool lastDirIsBuy = false;
							bool hasLastDir = false;
							int lastBarIdx = -1;
							for (const auto& sig : signals)
							{
								if (sig.isForbid) { hasLastDir = false; continue; }
								int bi = sig.barIndex;
								if (bi == lastBarIdx) continue;
								if (hasLastDir && sig.isBuy == lastDirIsBuy)
									klineFilteredBarIndices.insert(bi);
								else
								{
									lastDirIsBuy = sig.isBuy;
									hasLastDir = true;
								}
								lastBarIdx = bi;
							}
						}

						for (const auto& sig : signals)
						{
							const auto& barKTime = klineObj->data[sig.barIndex].day;
							std::string timeStr;
							if (barKTime.length() >= 10)
								timeStr = barKTime.substr(5, 5);
							else
								timeStr = barKTime;

							auto it = timeIndexMap.find(timeStr);
							if (it == timeIndexMap.end())
								it = timeIndexMap.find(barKTime);
							if (it != timeIndexMap.end())
							{
								int k = it->second;
								if (sig.isForbid)
								{
									forbidSignals[k] = true;
									buySignals[k] = false;
								}
								else if (!forbidSignals[k] || !sig.isBuy)
								{
									if (sig.isBuy)
									{
										if (!forbidSignals[k])
										{
											buySignals[k] = true;
											buyReasons[k] = sig.reason;
											if (klineFilteredBarIndices.count(sig.barIndex))
												noLabelSignals[k] = true;
										}
									}
									else
									{
										sellSignals[k] = true;
										sellReasons[k] = sig.reason;
										if (klineFilteredBarIndices.count(sig.barIndex))
											noLabelSignals[k] = true;
									}
								}
							}
						}
					}
				}

				const int dotR = g_data.RDPI(3);
				const int labelOff = g_data.RDPI(8);
				int oldBkMode = memDC.SetBkMode(TRANSPARENT);
				auto drawSignalArrow = [&](int x, int fromY, int toY, COLORREF color) {
					CPen pen(PS_SOLID, 1, color);
					CPen* pOldP = memDC.SelectObject(&pen);
					memDC.MoveTo(x, fromY);
					memDC.LineTo(x, toY);

					int dir = (toY >= fromY) ? 1 : -1;
					int arrowLen = g_data.RDPI(4);
					int arrowHalf = g_data.RDPI(3);
					memDC.MoveTo(x, toY);
					memDC.LineTo(x - arrowHalf, toY - dir * arrowLen);
					memDC.MoveTo(x, toY);
					memDC.LineTo(x + arrowHalf, toY - dir * arrowLen);
					memDC.SelectObject(pOldP);
					};

				for (int i = 0; i < totalPoints; i++)
				{
					if (!buySignals[i] && !sellSignals[i] && !forbidSignals[i])
						continue;
					if (i >= static_cast<int>(pricePoints.size()))
						continue;

					int ptX = static_cast<int>(round(pricePoints[i].X));
					int ptY = static_cast<int>(round(pricePoints[i].Y));

					if (buySignals[i])
					{
						CBrush brush(COLOR_GREEN_DOWN);
						CPen pen(PS_SOLID, 1, COLOR_GREEN_DOWN);
						CBrush* pOldB = memDC.SelectObject(&brush);
						CPen* pOldP = memDC.SelectObject(&pen);
						memDC.Ellipse(ptX - dotR, ptY - dotR, ptX + dotR, ptY + dotR);
						memDC.SelectObject(pOldB);
						memDC.SelectObject(pOldP);
					}
					else if (sellSignals[i])
					{
						CBrush brush(COLOR_RED_UP);
						CPen pen(PS_SOLID, 1, COLOR_RED_UP);
						CBrush* pOldB = memDC.SelectObject(&brush);
						CPen* pOldP = memDC.SelectObject(&pen);
						memDC.Ellipse(ptX - dotR, ptY - dotR, ptX + dotR, ptY + dotR);
						memDC.SelectObject(pOldB);
						memDC.SelectObject(pOldP);
					}
				}
				memDC.SetBkMode(oldBkMode);
			}
		}
	}
}

void CTimelineChart::DrawTimelineHoverOverlay(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	if (!hover.isHoveringVolume || hover.hoveredBarIndex < 0)
		return;

	const auto& timelinePoint = *ctx.timelinePoint;
	if (hover.hoveredBarIndex >= static_cast<int>(timelinePoint.size()))
		return;

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	const int xSlots = ctx.xAxisPoints > 0 ? ctx.xAxisPoints : static_cast<int>(timelinePoint.size());
	const auto& item = timelinePoint[hover.hoveredBarIndex];
	int hoverX = static_cast<int>(ctx.chartWidth / static_cast<float>(xSlots) * hover.hoveredBarIndex + ctx.chartWidth / static_cast<float>(xSlots) / 2);

	int dotY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((item.price - minPrice) * unitY));

	COLORREF dotColor = (item.price >= ctx.realtimeData.prevClosePrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;

	CPen crossPen(PS_DOT, 1, RGB(70, 130, 210));
	memDC.SelectObject(&crossPen);
	memDC.MoveTo(hoverX, ctx.priceChartTop);
	memDC.LineTo(hoverX, ctx.positionY);

	memDC.MoveTo(0, dotY);
	memDC.LineTo(ctx.chartWidth, dotY);

	int yAxisW = g_data.RDPI(50);
	CPoint origOrg = memDC.GetViewportOrg();
	memDC.OffsetViewportOrg(-yAxisW, 0);

	CString hoverPriceStr = ctx.realtimeData.IsETF() ? CCommon::FormatETFPrice(item.price) : CCommon::FormatFloat(item.price);
	CSize hoverPriceSize = memDC.GetTextExtent(hoverPriceStr);
	int priceLabelX = yAxisW - hoverPriceSize.cx - g_data.RDPI(3);
	int priceLabelY = dotY - hoverPriceSize.cy / 2;
	priceLabelY = max(ctx.priceChartTop, min(priceLabelY, ctx.priceChartTop + ctx.priceChartHeight - hoverPriceSize.cy));
	CRect priceBgRect(priceLabelX - g_data.RDPI(2), priceLabelY, priceLabelX + hoverPriceSize.cx + g_data.RDPI(2), priceLabelY + hoverPriceSize.cy);
	memDC.FillSolidRect(priceBgRect, RGB(33, 40, 56));
	memDC.Draw3dRect(priceBgRect, COLOR_DARK_GRAY_BORDER, COLOR_DARK_GRAY_BORDER);
	memDC.SetTextColor(dotColor);
	memDC.SetBkMode(TRANSPARENT);
	memDC.TextOut(priceLabelX, priceLabelY, hoverPriceStr);

	// 仅在副图为成交量(CJL)或竞价模式时绘制量柱十字光标和左侧刻度标签
	if (hover.viewMode == UI_VIEW_AUCTION || hover.timelineIndicator == 0)
	{
		STOCK::Volume maxVol = 0;
		for (const auto& tp : timelinePoint)
		{
			if (tp.volume > maxVol)
				maxVol = tp.volume;
		}
		if (maxVol > 0 && item.volume > 0)
		{
			float volRatio = static_cast<float>(item.volume) / static_cast<float>(maxVol);
			int volBarY = ctx.volumeChartTop + ctx.volumeChartHeight - static_cast<int>(volRatio * ctx.volumeChartHeight);
			CPen volCrossPen(PS_DOT, 1, RGB(70, 130, 210));
			memDC.SelectObject(&volCrossPen);
			memDC.MoveTo(0, volBarY);
			memDC.LineTo(ctx.chartWidth, volBarY);

			memDC.OffsetViewportOrg(-yAxisW, 0);
			STOCK::Volume volInLots = item.volume / 100;
			CString volLabel = CCommon::FormatVolumeInt(volInLots);
			CSize volLabelSize = memDC.GetTextExtent(volLabel);
			int volLabelX = yAxisW - volLabelSize.cx - g_data.RDPI(3);
			int volLabelY = volBarY - volLabelSize.cy / 2;
			volLabelY = max(ctx.volumeChartTop, min(volLabelY, ctx.volumeChartTop + ctx.volumeChartHeight - volLabelSize.cy));
			CRect volBgRect(volLabelX - g_data.RDPI(2), volLabelY, volLabelX + volLabelSize.cx + g_data.RDPI(2), volLabelY + volLabelSize.cy);
			memDC.FillSolidRect(volBgRect, RGB(33, 40, 56));
			memDC.Draw3dRect(volBgRect, COLOR_DARK_GRAY_BORDER, COLOR_DARK_GRAY_BORDER);
			memDC.SetTextColor(COLOR_WHITE);
			memDC.SetBkMode(TRANSPARENT);
			memDC.TextOut(volLabelX, volLabelY, volLabel);
			memDC.SetViewportOrg(origOrg);
		}
	}

	CString timeStr;
	if (!item.fullTime.empty() && hover.viewMode >= UI_VIEW_DAY_KLINE)
	{
		// K线模式：悬停高亮显示完整日期 yyyy-mm-dd
		timeStr = CString(item.fullTime.c_str());
	}
	else
	{
		timeStr = CString(item.time.c_str());
		if (timeStr.GetLength() >= 5)
			timeStr = timeStr.Left(5);
	}
	CSize timeSize = memDC.GetTextExtent(timeStr);
	int timeLabelX = hoverX - timeSize.cx / 2;
	timeLabelX = max(g_data.RDPI(2), min(timeLabelX, ctx.chartWidth - timeSize.cx - g_data.RDPI(2)));
	int timeLabelY = ctx.positionY;
	CRect timeBgRect(timeLabelX - g_data.RDPI(3), timeLabelY, timeLabelX + timeSize.cx + g_data.RDPI(3), timeLabelY + timeSize.cy);
	memDC.FillSolidRect(timeBgRect, RGB(33, 40, 56));
	memDC.Draw3dRect(timeBgRect, COLOR_DARK_GRAY_BORDER, COLOR_DARK_GRAY_BORDER);
	memDC.SetTextColor(COLOR_WHITE);
	memDC.SetBkMode(TRANSPARENT);
	memDC.TextOut(timeLabelX, timeLabelY, timeStr);
}

void CTimelineChart::DrawDayKLinePriceChart(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	if (timelinePoint.empty())
		return;

	const int totalPoints = static_cast<int>(timelinePoint.size());

	STOCK::Price maxPrice = ctx.maxPrice;
	STOCK::Price minPrice = ctx.minPrice;
	double unitY = ctx.unitY;
	if (maxPrice <= 0 || minPrice < 0 || maxPrice <= minPrice || unitY <= 0)
	{
		STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
		maxPrice = ctx.realtimeData.prevClosePrice + priceLimit;
		minPrice = ctx.realtimeData.prevClosePrice - priceLimit;
		const int pricePaddingY = g_data.RDPI(10);
		double paddingPrice = (maxPrice - minPrice) * pricePaddingY / ctx.priceChartHeight;
		maxPrice += paddingPrice;
		minPrice -= paddingPrice;
		unitY = ctx.priceChartHeight / (maxPrice - minPrice);
	}

	const auto& klineData = *ctx.klineData;
	int klineStartIdx = ctx.startIndex;
	int klineEndIdx = klineStartIdx + totalPoints;
	if (klineEndIdx > static_cast<int>(klineData.size()))
		klineEndIdx = static_cast<int>(klineData.size());

	if (klineStartIdx >= static_cast<int>(klineData.size()))
		return;

	float barTotalWidth = static_cast<float>(ctx.chartWidth) / totalPoints;
	int barWidth = max(1, static_cast<int>(barTotalWidth * 0.7));
	int gap = static_cast<int>(barTotalWidth) - barWidth;
	if (gap < 1) gap = 1;

	auto priceToY = [&](STOCK::Price price) -> int {
		return ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((price - minPrice) * unitY);
		};

	STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

	for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
	{
		const auto& kp = klineData[klineStartIdx + i];
		if (kp.close <= 0) continue;

		int centerX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(barTotalWidth / 2);
		int leftX = centerX - barWidth / 2;

		bool isUp = (kp.close >= kp.open);
		COLORREF barColor = isUp ? COLOR_RED_UP : COLOR_GREEN_DOWN;

		int openY = priceToY(kp.open);
		int closeY = priceToY(kp.close);
		int highY = priceToY(kp.high);
		int lowY = priceToY(kp.low);

		openY = max(ctx.priceChartTop, min(openY, ctx.priceChartTop + ctx.priceChartHeight));
		closeY = max(ctx.priceChartTop, min(closeY, ctx.priceChartTop + ctx.priceChartHeight));
		highY = max(ctx.priceChartTop, min(highY, ctx.priceChartTop + ctx.priceChartHeight));
		lowY = max(ctx.priceChartTop, min(lowY, ctx.priceChartTop + ctx.priceChartHeight));

		CPen barPen(PS_SOLID, 1, barColor);
		memDC.SelectObject(&barPen);
		memDC.MoveTo(centerX, highY);
		memDC.LineTo(centerX, lowY);

		int bodyTop = min(openY, closeY);
		int bodyBottom = max(openY, closeY);
		int bodyHeight = bodyBottom - bodyTop;
		if (bodyHeight < 1) bodyHeight = 1;

		CBrush brush(barColor);
		CBrush* pOldBrush = memDC.SelectObject(&brush);
		memDC.Rectangle(leftX, bodyTop, leftX + barWidth, bodyBottom + 1);
		memDC.SelectObject(pOldBrush);
	}

	if (hover.showMA)
	{
		// 日K线MA均线配色
		const COLORREF ma5Color = RGB(240, 117, 40);
		const COLORREF ma17Color = RGB(21, 101, 192);
		const COLORREF ma60Color = RGB(128, 40, 149);

		auto drawMALine = [&](int fieldOffset, COLORREF color) {
			CPen maPen(PS_SOLID, 1, color);
			memDC.SelectObject(&maPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				const auto& item = timelinePoint[i];
				STOCK::Price maVal = 0;
				switch (fieldOffset)
				{
				case 5: maVal = item.ma5; break;
				case 17: maVal = item.ma17; break;
				case 60: maVal = item.ma60; break;
				}
				if (maVal <= 0) { first = true; continue; }
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
				double yVal = (maVal - minPrice) * unitY;
				int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(yVal);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawMALine(5, ma5Color);
		drawMALine(17, ma17Color);
		drawMALine(60, ma60Color);
	}

	if (hover.showBollBands)
	{
		const int N = 20;
		const int K = 2;

		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;

		std::vector<double> upperBand(totalPoints, 0);
		std::vector<double> middleBand(totalPoints, 0);
		std::vector<double> lowerBand(totalPoints, 0);

		for (int i = 0; i < totalPoints; i++)
		{
			int globalIdx = ctx.startIndex + i;
			if (globalIdx < N - 1)
			{
				upperBand[i] = middleBand[i] = lowerBand[i] = 0;
				continue;
			}
			double sum = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				sum += fullData[j].price;
			}
			double ma = sum / N;
			double variance = 0;
			for (int j = globalIdx - N + 1; j <= globalIdx; j++)
			{
				double diff = fullData[j].price - ma;
				variance += diff * diff;
			}
			double stddev = std::sqrt(variance / N);
			middleBand[i] = ma;
			upperBand[i] = ma + K * stddev;
			lowerBand[i] = ma - K * stddev;
		}

		auto bandPriceToY = [&](double price) -> int {
			int py = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>(round((price - minPrice) * unitY));
			return max(ctx.priceChartTop, min(py, ctx.priceChartTop + ctx.priceChartHeight));
			};

		auto drawBandLine = [&](const std::vector<double>& band, COLORREF color) {
			CPen bandPen(PS_DASH, 1, color);
			memDC.SelectObject(&bandPen);
			bool first = true;
			for (int i = 0; i < totalPoints; i++)
			{
				if (band[i] <= 0) continue;
				int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
				int py = bandPriceToY(band[i]);
				if (first)
				{
					memDC.MoveTo(pointX, py);
					first = false;
				}
				else
				{
					memDC.LineTo(pointX, py);
				}
			}
			};

		drawBandLine(upperBand, COLOR_RED_UP);
		drawBandLine(middleBand, RGB(0, 0, 230));
		drawBandLine(lowerBand, COLOR_GREEN_DOWN);
	}

	// 最高/最低价标签
	{
		STOCK::Price hiPrice = 0, loPrice = (std::numeric_limits<STOCK::Price>::max)();
		int hiIdx = -1, loIdx = -1;
		for (int i = 0; i < totalPoints && (klineStartIdx + i) < klineEndIdx; i++)
		{
			const auto& kp = klineData[klineStartIdx + i];
			if (kp.high > 0)
			{
				if (kp.high > hiPrice) { hiPrice = kp.high; hiIdx = i; }
			}
			if (kp.low > 0)
			{
				if (kp.low < loPrice) { loPrice = kp.low; loIdx = i; }
			}
		}

		if (hiIdx >= 0 && hiPrice > 0)
		{
			int hiX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * hiIdx) + static_cast<int>(barTotalWidth / 2);
			int hiY = priceToY(hiPrice);
			DrawPricePointLabel(memDC, hiX, hiY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				hiPrice, true, COLOR_RED_UP);
		}

		if (loIdx >= 0 && loPrice > 0 && loIdx != hiIdx)
		{
			int loX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * loIdx) + static_cast<int>(barTotalWidth / 2);
			int loY = priceToY(loPrice);
			DrawPricePointLabel(memDC, loX, loY, 0, ctx.priceChartTop, ctx.chartWidth, ctx.priceChartHeight,
				loPrice, false, COLOR_GREEN_DOWN);
		}
	}
}

void CTimelineChart::DrawPriceChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight, HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	int titleH = g_data.RDPI(16);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	CRect priceTitleRect(0, areaTop, ctx.chartWidth, areaTop + titleH);
	memDC.FillSolidRect(priceTitleRect, COLOR_BG_HEADER);

	if (!hover.timelinePriceTitleTip.IsEmpty())
	{
		memDC.SetTextColor(COLOR_WHITE);
		memDC.DrawText(hover.timelinePriceTitleTip, priceTitleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	}
	else if (hover.viewMode >= UI_VIEW_DAY_KLINE && ctx.klineData && !ctx.klineData->empty())
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;

		const auto& klineData = *ctx.klineData;
		int klineIdx = -1;
		if (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume)
		{
			klineIdx = ctx.startIndex + hover.hoveredBarIndex;
		}
		else
		{
			klineIdx = ctx.startIndex + static_cast<int>(timelinePoint.size()) - 1;
		}
		if (klineIdx < 0 || klineIdx >= static_cast<int>(klineData.size()))
			klineIdx = static_cast<int>(klineData.size()) - 1;

		if (klineIdx >= 0 && klineIdx < static_cast<int>(klineData.size()))
		{
			const auto& kp = klineData[klineIdx];
			STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

			auto drawKLineLabel = [&](const CString& label, STOCK::Price value, COLORREF labelColor, COLORREF valueColor) {
				CString valStr = CCommon::FormatFloat(value);
				memDC.SetTextColor(labelColor);
				CSize ls = memDC.GetTextExtent(label);
				memDC.TextOut(xPos, centerY - ls.cy / 2, label);
				xPos += ls.cx;
				memDC.SetTextColor(valueColor);
				CSize vs = memDC.GetTextExtent(valStr);
				memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
				xPos += vs.cx + g_data.RDPI(4);
				};

			drawKLineLabel(_T("开:"), kp.open, COLOR_TEXT_MUTED, (kp.open >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
			drawKLineLabel(_T("收:"), kp.close, COLOR_TEXT_MUTED, (kp.close >= prevClose ? COLOR_RED_UP : COLOR_GREEN_DOWN));
		}
	}
	else if (!timelinePoint.empty())
	{
		STOCK::Price prevClose = ctx.realtimeData.prevClosePrice;

		bool isHovering = (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume);
		STOCK::Price dispAvgPrice = isHovering ? hover.hoveredData.averagePrice : timelinePoint.back().averagePrice;
		if (dispAvgPrice <= 0)
			dispAvgPrice = isHovering ? hover.hoverMa1 : ctx.ma1;

		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;

		auto drawLabelValue = [&](const CString& labelText, STOCK::Price value, COLORREF labelColor, COLORREF valueColor) {
			CString valStr = CCommon::FormatFloat(value);
			memDC.SetTextColor(labelColor);
			CSize ls = memDC.GetTextExtent(labelText);
			memDC.TextOut(xPos, centerY - ls.cy / 2, labelText);
			xPos += ls.cx;
			memDC.SetTextColor(valueColor);
			CSize vs = memDC.GetTextExtent(valStr);
			memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
			xPos += vs.cx + g_data.RDPI(4);
			};

		auto cmpPrevClose = [prevClose](STOCK::Price p) -> COLORREF {
			if (prevClose <= 0) return COLOR_WHITE;
			if (p > prevClose) return COLOR_RED_UP;
			if (p < prevClose) return COLOR_GREEN_DOWN;
			return COLOR_WHITE;
			};

		// 左侧：现价
		drawLabelValue(_T("现:"), ctx.realtimeData.currentPrice, COLOR_TEXT_MUTED, cmpPrevClose(ctx.realtimeData.currentPrice));

		// 右侧：ETF显示净:xx 溢:+/-xx%，股票显示均:xx
		if (ctx.realtimeData.IsETF())
		{
			COLORREF iopvColor = COLOR_WHITE;
			if (ctx.realtimeData.iopv > ctx.realtimeData.currentPrice)
				iopvColor = COLOR_RED_UP;
			else if (ctx.realtimeData.iopv < ctx.realtimeData.currentPrice)
				iopvColor = COLOR_GREEN_DOWN;

			CString iopvLabel = _T("净:");
			CString iopvVal;
			iopvVal.Format(_T("%.4f"), ctx.realtimeData.iopv);
			CSize iopvLs = memDC.GetTextExtent(iopvLabel);
			CSize iopvVs = memDC.GetTextExtent(iopvVal);

			CString premLabel = _T(" 溢:");
			CString premVal;
			double premRate = ctx.realtimeData.iopvPremiumRate;
			if (premRate >= 0)
				premVal.Format(_T("+%.2f%%"), premRate);
			else
				premVal.Format(_T("%.2f%%"), premRate);
			COLORREF premColor = premRate > 0 ? COLOR_RED_UP : (premRate < 0 ? COLOR_GREEN_DOWN : COLOR_WHITE);
			CSize premLs = memDC.GetTextExtent(premLabel);
			CSize premVs = memDC.GetTextExtent(premVal);

			int rightX = ctx.chartWidth - g_data.RDPI(4) - iopvLs.cx - iopvVs.cx - premLs.cx - premVs.cx;
			memDC.SetTextColor(COLOR_TEXT_MUTED);
			memDC.TextOut(rightX, centerY - iopvLs.cy / 2, iopvLabel);
			rightX += iopvLs.cx;
			memDC.SetTextColor(iopvColor);
			memDC.TextOut(rightX, centerY - iopvVs.cy / 2, iopvVal);
			rightX += iopvVs.cx;
			memDC.SetTextColor(COLOR_TEXT_MUTED);
			memDC.TextOut(rightX, centerY - premLs.cy / 2, premLabel);
			rightX += premLs.cx;
			memDC.SetTextColor(premColor);
			memDC.TextOut(rightX, centerY - premVs.cy / 2, premVal);
		}
		else
		{
			CString avgLabel = _T("均:");
			CString avgVal = CCommon::FormatFloat(dispAvgPrice);
			COLORREF avgColor = cmpPrevClose(dispAvgPrice);
			CSize avgLs = memDC.GetTextExtent(avgLabel);
			CSize avgVs = memDC.GetTextExtent(avgVal);
			int rightX = ctx.chartWidth - g_data.RDPI(4) - avgLs.cx - avgVs.cx;
			memDC.SetTextColor(COLOR_TEXT_MUTED);
			memDC.TextOut(rightX, centerY - avgLs.cy / 2, avgLabel);
			rightX += avgLs.cx;
			memDC.SetTextColor(avgColor);
			memDC.TextOut(rightX, centerY - avgVs.cy / 2, avgVal);
		}

		// 分时模式：使用分时数据计算实时指标信号，设置按钮信号颜色
		// 将分时TimelinePoint转为1分钟Bar序列（open=high=low=close=price），各模式用各自数据
		{
			// 使用完整分时数据（ctx.fullTimeline），而非可见区域子集
			const auto& fullTl = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
			if (fullTl.size() >= 26)
			{
				std::vector<STOCK::Bar> bars1m = CSignalAnalyzer::ConvertTimelineToBars(fullTl);

				int signalEndIndex = -1;
				if (isHovering)
				{
					// ctx.startIndex + hover.hoveredBarIndex 是分时数据的全局索引，与bars1m索引一致
					int hoverIdx = ctx.startIndex + hover.hoveredBarIndex;
					if (hoverIdx >= 25 && hoverIdx < static_cast<int>(bars1m.size()))
						signalEndIndex = hoverIdx;
				}

				auto rtSig = CSignalAnalyzer::CalcRealtimeSignals(bars1m, signalEndIndex);

				static const COLORREF BUY_COLORS[] = {
					RGB(40, 240, 40),
					RGB(50, 180, 50),
					RGB(20, 130, 40)
				};
				static const COLORREF SELL_COLORS[] = {
					RGB(240, 40, 40),
					RGB(180, 50, 50),
					RGB(130, 20, 40)
				};

				// 超卖(买入信号=-1)→红色(要涨)，超买(卖出信号=1)→绿色(要跌)
				if (rtSig.boll != 0) hover.bollSignalColor = rtSig.boll == -1 ? SELL_COLORS[rtSig.bollStr - 1] : BUY_COLORS[rtSig.bollStr - 1];
				if (rtSig.macd != 0) hover.macdSignalColor = rtSig.macd == -1 ? SELL_COLORS[rtSig.macdStr - 1] : BUY_COLORS[rtSig.macdStr - 1];
				if (rtSig.kdj != 0) hover.kdjSignalColor = rtSig.kdj == -1 ? SELL_COLORS[rtSig.kdjStr - 1] : BUY_COLORS[rtSig.kdjStr - 1];
				if (rtSig.rsi != 0) hover.rsiSignalColor = rtSig.rsi == -1 ? SELL_COLORS[rtSig.rsiStr - 1] : BUY_COLORS[rtSig.rsiStr - 1];
				if (rtSig.wr != 0) hover.wrSignalColor = rtSig.wr == -1 ? SELL_COLORS[rtSig.wrStr - 1] : BUY_COLORS[rtSig.wrStr - 1];
				if (rtSig.ma != 0) hover.maSignalColor = rtSig.ma == -1 ? SELL_COLORS[rtSig.maStr - 1] : BUY_COLORS[rtSig.maStr - 1];
			}
		}
	}

	if (hover.viewMode >= UI_VIEW_DAY_KLINE && !timelinePoint.empty())
	{
		bool isHovering = (hover.hoveredBarIndex >= 0 && hover.isHoveringVolume);
		int displayIdx = isHovering ? hover.hoveredBarIndex : static_cast<int>(timelinePoint.size()) - 1;
		displayIdx = max(0, min(displayIdx, static_cast<int>(timelinePoint.size()) - 1));

		const int rightPadding = g_data.RDPI(4);
		const int itemGap = g_data.RDPI(6);
		int centerY = areaTop + titleH / 2;

		auto formatPrice = [](STOCK::Price value) -> CString {
			return CCommon::FormatFloat(value);
			};
		auto drawRightLabelValues = [&](const std::vector<std::pair<CString, COLORREF>>& items) {
			if (items.empty())
				return;

			int totalWidth = 0;
			for (const auto& item : items)
				totalWidth += memDC.GetTextExtent(item.first).cx + itemGap;
			totalWidth -= itemGap;

			int xPos = ctx.chartWidth - rightPadding - totalWidth;
			xPos = max(g_data.RDPI(4), xPos);
			for (const auto& item : items)
			{
				memDC.SetTextColor(item.second);
				CSize sz = memDC.GetTextExtent(item.first);
				memDC.TextOut(xPos, centerY - sz.cy / 2, item.first);
				xPos += sz.cx + itemGap;
			}
			};

		// 日K线/周K线/月K线模式：计算信号颜色
		auto stockData = g_data.GetStockData(hover.stockId);
		if (stockData)
		{
			std::vector<STOCK::Bar> bars;
			STOCK::KLineData* klineObj = nullptr;
			if (hover.viewMode == UI_VIEW_DAY_KLINE)
				klineObj = stockData->getKLineData();
			else if (hover.viewMode == UI_VIEW_WEEK_KLINE)
				klineObj = stockData->getWeekKLineData();
			else if (hover.viewMode == UI_VIEW_MONTH_KLINE)
				klineObj = stockData->getMonthKLineData();

			if (klineObj && klineObj->data.size() >= 26)
			{
				bars.reserve(klineObj->data.size());
				for (const auto& kp : klineObj->data) bars.push_back(STOCK::Bar::FromKLinePoint(kp));
			}

			if (bars.size() >= 26)
			{
				int signalEndIndex = -1;
				if (isHovering)
				{
					int hoverKlineIdx = ctx.startIndex + hover.hoveredBarIndex;
					if (hoverKlineIdx >= 25 && hoverKlineIdx < static_cast<int>(bars.size()))
						signalEndIndex = hoverKlineIdx;
				}

				auto rtSig = CSignalAnalyzer::CalcRealtimeSignals(bars, signalEndIndex);

				static const COLORREF BUY_COLORS[] = { RGB(40, 240, 40), RGB(50, 180, 50), RGB(20, 130, 40) };
				static const COLORREF SELL_COLORS[] = { RGB(240, 40, 40), RGB(180, 50, 50), RGB(130, 20, 40) };

				if (rtSig.boll != 0) hover.bollSignalColor = rtSig.boll == -1 ? SELL_COLORS[rtSig.bollStr - 1] : BUY_COLORS[rtSig.bollStr - 1];
				if (rtSig.macd != 0) hover.macdSignalColor = rtSig.macd == -1 ? SELL_COLORS[rtSig.macdStr - 1] : BUY_COLORS[rtSig.macdStr - 1];
				if (rtSig.kdj != 0) hover.kdjSignalColor = rtSig.kdj == -1 ? SELL_COLORS[rtSig.kdjStr - 1] : BUY_COLORS[rtSig.kdjStr - 1];
				if (rtSig.rsi != 0) hover.rsiSignalColor = rtSig.rsi == -1 ? SELL_COLORS[rtSig.rsiStr - 1] : BUY_COLORS[rtSig.rsiStr - 1];
				if (rtSig.wr != 0) hover.wrSignalColor = rtSig.wr == -1 ? SELL_COLORS[rtSig.wrStr - 1] : BUY_COLORS[rtSig.wrStr - 1];
				if (rtSig.ma != 0) hover.maSignalColor = rtSig.ma == -1 ? SELL_COLORS[rtSig.maStr - 1] : BUY_COLORS[rtSig.maStr - 1];
			}
		}

		if (hover.showMA)
		{
			STOCK::Price dispMa5 = isHovering ? hover.hoverMa5 : ctx.ma5;
			STOCK::Price dispMa17 = isHovering ? hover.hoverMa17 : ctx.ma17;
			STOCK::Price dispMa60 = isHovering ? hover.hoverMa60 : ctx.ma60;
			std::vector<std::pair<CString, COLORREF>> items;
			// 日/周/月K线MA均线配色
			const COLORREF ma5TextColor = RGB(240, 117, 40);
			const COLORREF ma17TextColor = RGB(21, 101, 192);
			const COLORREF ma60TextColor = RGB(128, 40, 149);
			if (dispMa5 > 0) items.push_back({ _T("MA5:") + formatPrice(dispMa5), ma5TextColor });
			if (dispMa17 > 0) items.push_back({ _T("MA17:") + formatPrice(dispMa17), ma17TextColor });
			if (dispMa60 > 0) items.push_back({ _T("MA60:") + formatPrice(dispMa60), ma60TextColor });
			drawRightLabelValues(items);
		}
	}

	memDC.SetBkMode(oldBkMode);

	TimelineDrawContext tmpCtx = ctx;
	tmpCtx.priceChartTop = areaTop + titleH;
	tmpCtx.priceChartHeight = areaHeight - titleH;

	if (hover.viewMode >= UI_VIEW_DAY_KLINE)
	{
		if (hover.showTrendView)
			DrawTimelinePriceCurve(memDC, tmpCtx, hover);
		else
			DrawDayKLinePriceChart(memDC, tmpCtx, hover);
	}
	else
		DrawTimelinePriceCurve(memDC, tmpCtx, hover);
}