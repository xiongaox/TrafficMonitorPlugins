#include "pch.h"
#include "KLineChart.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include <algorithm>
#include <cmath>

KLineDrawData CKLineChart::PrepareKLineDrawData(int x, int y, int w, int h, const std::vector<STOCK::KLinePoint>& klineData, const HoverState& hover)
{
	KLineDrawData d = {};
	d.x = x; d.y = y; d.w = w; d.h = h;
	d.klineData = &klineData;

	d.displayCount = min(hover.klinePeriodDays, static_cast<int>(klineData.size()));
	d.startIndex = klineData.size() - d.displayCount;

	d.maxPrice = 0;
	d.minPrice = (std::numeric_limits<STOCK::Price>::max)();
	for (int i = d.startIndex; i < klineData.size(); i++)
	{
		if (klineData[i].high > 0)
			d.maxPrice = max(d.maxPrice, klineData[i].high);
		if (klineData[i].low > 0)
			d.minPrice = min(d.minPrice, klineData[i].low);
	}

	const int minBarWidth = 7;
	const int gap = 1;
	d.maxVisibleKlines = min(d.displayCount, w / (minBarWidth + gap));
	d.scrollRange = max(0, d.displayCount - d.maxVisibleKlines);
	d.scrollPos = min(hover.scrollOffset, d.scrollRange);
	d.finalStartIndex = max(d.startIndex, static_cast<int>(klineData.size()) - d.maxVisibleKlines - d.scrollPos);
	d.barWidth = max(minBarWidth, (w - gap * (d.maxVisibleKlines - 1)) / d.maxVisibleKlines);
	d.gap = gap;

	if (d.maxPrice > d.minPrice)
		d.unitY = h / (d.maxPrice - d.minPrice);

	return d;
}

// 注：CalculatePeriodHighsLows 已移至 CStockIndicator 类。

std::vector<LabelInfo> CKLineChart::DrawKLineMonthLines(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover)
{
	const auto& klineData = *drawData.klineData;
	std::vector<LabelInfo> labelInfos;

	int interval = 1;
	if (hover.klinePeriodDays <= 250) interval = 1;
	else if (hover.klinePeriodDays <= 500) interval = 2;
	else interval = 3;

	int startMonth = -1, startYear = -1;
	for (int i = drawData.finalStartIndex; i < klineData.size(); i++)
	{
		const auto& item = klineData[i];
		if (item.day.length() >= 7)
		{
			startYear = atoi(item.day.substr(0, 4).c_str());
			startMonth = atoi(item.day.substr(5, 2).c_str());
			if (startMonth > 0 && startYear > 0) break;
		}
	}
	if (startMonth <= 0 || startYear <= 0) return labelInfos;

	CPen gridPen(PS_SOLID, 1, COLOR_GRAY_GRID);
	memDC.SelectObject(&gridPen);

	int lastMonth = -1, lastYear = -1;
	for (int i = drawData.finalStartIndex; i < klineData.size(); i++)
	{
		const auto& item = klineData[i];
		int year = -1, month = -1;
		if (item.day.length() >= 7)
		{
			year = atoi(item.day.substr(0, 4).c_str());
			month = atoi(item.day.substr(5, 2).c_str());
		}

		if (month > 0 && year > 0)
		{
			int monthDiff = (year - startYear) * 12 + (month - startMonth);
			if (monthDiff % interval == 0 && (month != lastMonth || year != lastYear))
			{
				int barX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap);
				memDC.MoveTo(barX, drawData.y);
				memDC.LineTo(barX, drawData.y + drawData.h);
				labelInfos.push_back({ year, month, barX });
				lastMonth = month;
				lastYear = year;
			}
		}
	}
	return labelInfos;
}

void CKLineChart::DrawKLineMonthLabels(CDC& memDC, const KLineDrawData& drawData, const std::vector<LabelInfo>& labelInfos)
{
	if (labelInfos.empty()) return;

	memDC.SetTextColor(COLOR_GRAY_TEXT);
	int lastLabelYear = -1;
	int labelTop = drawData.y + drawData.h + g_data.RDPI(2);
	CString tempLabel = _T("2025年12月");
	CSize tempSize = memDC.GetTextExtent(tempLabel);
	int labelBottom = labelTop + tempSize.cy;

	CRect fullLabelClearRect(drawData.x - tempSize.cx, labelTop, drawData.x + drawData.w + tempSize.cx, labelBottom + 4);
	memDC.FillSolidRect(fullLabelClearRect, COLOR_WHITE);

	lastLabelYear = -1;
	for (size_t i = 0; i < labelInfos.size(); i++)
	{
		const LabelInfo& info = labelInfos[i];
		int year = info.year;
		int month = info.month;
		int barX = info.barX;

		if (barX < drawData.x - tempSize.cx || barX > drawData.x + drawData.w + tempSize.cx)
		{
			if (lastLabelYear != -1) lastLabelYear = year;
			continue;
		}

		CString label;
		if (lastLabelYear == -1 || year != lastLabelYear)
			label.Format(_T("%d年%d月"), year, month);
		else
			label.Format(_T("%d月"), month);

		CSize labelSize = memDC.GetTextExtent(label);
		int labelX = barX - labelSize.cx / 2;
		labelX = max(drawData.x, labelX);
		if (labelX + labelSize.cx > drawData.x + drawData.w)
			labelX = drawData.x + drawData.w - labelSize.cx;
		labelX = max(drawData.x, labelX);

		memDC.SetBkMode(OPAQUE);
		memDC.SetBkColor(COLOR_WHITE);
		memDC.ExtTextOut(labelX, labelTop, 0, NULL, label, NULL);

		lastLabelYear = year;
	}
}

void CKLineChart::DrawKLineGrid(CDC& memDC, const KLineDrawData& drawData)
{
	CPen gridPen(PS_SOLID, 1, COLOR_GRAY_GRID);
	memDC.SelectObject(&gridPen);
	for (int i = 0; i <= 6; i++)
	{
		int gridY = drawData.y + static_cast<int>(i * drawData.h / 6.0f);
		memDC.MoveTo(drawData.x, gridY);
		memDC.LineTo(drawData.x + drawData.w, gridY);
	}
}

void CKLineChart::DrawYearAverageLines(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover)
{
	auto stockDataPtr = g_data.GetStockData(hover.stockId);
	auto* klineObj = stockDataPtr ? stockDataPtr->getKLineData() : nullptr;
	if (!klineObj) return;

	STOCK::Price avg1Year = klineObj->CalculateMAPeriod(1, 1);
	STOCK::Price avg2Year = klineObj->CalculateMAPeriod(2, 1);
	STOCK::Price avg3Year = klineObj->CalculateMAPeriod(3, 1);

	auto drawLine = [&](STOCK::Price price, COLORREF color) {
		if (price > 0 && price >= drawData.minPrice && price <= drawData.maxPrice)
		{
			int avgY = drawData.y + static_cast<int>((drawData.maxPrice - price) * drawData.unitY);
			CPen avgPen(PS_DOT, 1, color);
			memDC.SelectObject(&avgPen);
			memDC.MoveTo(drawData.x, avgY);
			memDC.LineTo(drawData.x + drawData.w, avgY);
		}
		};

	drawLine(avg1Year, COLOR_BLUE_AVG1);
	drawLine(avg2Year, COLOR_GREEN_AVG2);
	drawLine(avg3Year, COLOR_GREEN_AVG3);

	auto drawLabel = [&](STOCK::Price price, COLORREF color) {
		if (price > 0 && price >= drawData.minPrice && price <= drawData.maxPrice)
		{
			int avgY = drawData.y + static_cast<int>((drawData.maxPrice - price) * drawData.unitY);
			memDC.SetTextColor(color);
			CString priceTxt = CCommon::FormatFloat(price);
			CSize priceSize = memDC.GetTextExtent(priceTxt);
			memDC.TextOut(drawData.x + g_data.RDPI(2), avgY - priceSize.cy, priceTxt);
		}
		};

	drawLabel(avg1Year, COLOR_BLUE_AVG1);
	drawLabel(avg2Year, COLOR_GREEN_AVG2);
	drawLabel(avg3Year, COLOR_GREEN_AVG3);
}

void CKLineChart::DrawMAIndicators(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover)
{
	if (!hover.showMA) return;

	struct MAConfig {
		int period;
		COLORREF color;
		int width;
	};
	const MAConfig maConfigs[] = {
		{ 5, RGB(100, 100, 100), 1 },
		{ 13, RGB(255, 130, 0), 1 },
		{ 34, RGB(0, 80, 200), 1 },
		{ 55, RGB(0, 120, 60), 1 },
	};

	auto stockDataPtr = g_data.GetStockData(hover.stockId);
	auto* klineObj = stockDataPtr ? stockDataPtr->getKLineData() : nullptr;

	for (const auto& config : maConfigs)
	{
		double maValue = klineObj ? klineObj->CalculateMA(config.period) : 0;
		if (maValue > 0 && maValue >= drawData.minPrice && maValue <= drawData.maxPrice)
		{
			int maY = drawData.y + static_cast<int>((drawData.maxPrice - maValue) * drawData.unitY);
			CPen maPen(PS_DOT, config.width, config.color);
			memDC.SelectObject(&maPen);
			memDC.MoveTo(drawData.x, maY);
			memDC.LineTo(drawData.x + drawData.w, maY);

			CString maLabel;
			maLabel.Format(_T("MA%d"), config.period);
			memDC.SetTextColor(config.color);
			CSize labelSize = memDC.GetTextExtent(maLabel);
			memDC.TextOut(drawData.x + drawData.w - labelSize.cx - g_data.RDPI(2), maY - labelSize.cy, maLabel);
		}
	}
}

void CKLineChart::DrawCurrentPriceLine(CDC& memDC, const KLineDrawData& drawData)
{
	const auto& stockInfo = *drawData.stockInfo;
	if (stockInfo.currentPrice <= 0 || stockInfo.currentPrice < drawData.minPrice || stockInfo.currentPrice > drawData.maxPrice)
		return;

	int currentPriceY = drawData.y + static_cast<int>((drawData.maxPrice - stockInfo.currentPrice) * drawData.unitY);
	COLORREF currentPriceColor = stockInfo.currentPrice >= stockInfo.prevClosePrice ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	CPen currentPricePen(PS_DASH, 1, currentPriceColor);
	memDC.SelectObject(&currentPricePen);
	memDC.MoveTo(drawData.x, currentPriceY);
	memDC.LineTo(drawData.x + drawData.w, currentPriceY);

	CString currentPriceTxt = CCommon::FormatFloat(stockInfo.currentPrice);
	memDC.SetTextColor(currentPriceColor);
	CSize cpSize = memDC.GetTextExtent(currentPriceTxt);
	memDC.TextOut(drawData.x + g_data.RDPI(2), currentPriceY - cpSize.cy, currentPriceTxt);

	CString latestTxt = _T("new");
	CSize latestSize = memDC.GetTextExtent(latestTxt);
	memDC.TextOut(drawData.x + drawData.w - latestSize.cx - g_data.RDPI(2), currentPriceY - latestSize.cy, latestTxt);
}

void CKLineChart::DrawPriceLabels(CDC& memDC, const KLineDrawData& drawData)
{
	memDC.SetTextColor(COLOR_GRAY_TEXT);
	double step = (drawData.maxPrice - drawData.minPrice) / 6.0;
	for (int i = 0; i <= 6; i++)
	{
		double price = drawData.maxPrice - i * step;
		CString priceTxt = CCommon::FormatFloat(price);
		int y = drawData.y + static_cast<int>(i * drawData.h / 6.0f);
		int textH = memDC.GetTextExtent(priceTxt).cy;
		// 顶部标签紧贴网格线下方，底部标签紧贴网格线上方，中间居中
		int labelY;
		if (i == 0) labelY = y + g_data.RDPI(1);
		else if (i == 6) labelY = y - textH - g_data.RDPI(1);
		else labelY = y - textH / 2;
		memDC.TextOut(drawData.x + g_data.RDPI(2), labelY, priceTxt);
	}
}

// ========== K线图绘制 ==========

void CKLineChart::DrawKLineBars(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover)
{
	const auto& klineData = *drawData.klineData;
	std::wstring buyDate = g_data.GetBuyDate(hover.stockId);

	for (int i = drawData.finalStartIndex; i < klineData.size(); i++)
	{
		const auto& item = klineData[i];
		int barX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap);

		int openY = drawData.y + static_cast<int>((drawData.maxPrice - item.open) * drawData.unitY);
		int closeY = drawData.y + static_cast<int>((drawData.maxPrice - item.close) * drawData.unitY);
		int highY = drawData.y + static_cast<int>((drawData.maxPrice - item.high) * drawData.unitY);
		int lowY = drawData.y + static_cast<int>((drawData.maxPrice - item.low) * drawData.unitY);

		COLORREF color = (item.close >= item.open) ? COLOR_RED_UP : COLOR_GREEN_DOWN;

		CPen linePen(PS_SOLID, 1, color);
		memDC.SelectObject(&linePen);
		memDC.MoveTo(barX + drawData.barWidth / 2, highY);
		memDC.LineTo(barX + drawData.barWidth / 2, lowY);

		int bodyTop = min(openY, closeY);
		int bodyBottom = max(openY, closeY);
		CBrush brush(color);
		memDC.FillRect(CRect(barX, bodyTop, barX + drawData.barWidth, bodyBottom), &brush);

		if (!buyDate.empty())
		{
			std::string itemDayStr(item.day.begin(), item.day.end());
			std::string buyDateStr(buyDate.begin(), buyDate.end());
			if (itemDayStr == buyDateStr)
			{
				CString buyTxt = _T("买");
				CSize buySize = memDC.GetTextExtent(buyTxt);
				int circleRadius = max(buySize.cx, buySize.cy) / 2;
				int buyX = barX + drawData.barWidth / 2;
				int buyY = highY - g_data.RDPI(2) - buySize.cy / 2;

				CPen circlePen(PS_SOLID, 2, COLOR_GOLDEN);
				CPen* pOldPen = memDC.SelectObject(&circlePen);
				CBrush circleBrush(COLOR_GOLDEN);
				memDC.SelectObject(&circleBrush);
				memDC.Ellipse(buyX - circleRadius, buyY - circleRadius, buyX + circleRadius, buyY + circleRadius);
				memDC.SetTextColor(COLOR_BLACK);
				memDC.TextOut(buyX - buySize.cx / 2, buyY - buySize.cy / 2, buyTxt);
				memDC.SelectObject(pOldPen);
			}
		}
	}
}

void CKLineChart::DrawKLineBuyMarkers(CDC& memDC, const KLineDrawData& drawData, const HoverState& hover)
{
	const auto& klineData = *drawData.klineData;
	std::wstring buyDate = g_data.GetBuyDate(hover.stockId);
	if (buyDate.empty()) return;

	for (int i = drawData.finalStartIndex; i < klineData.size(); i++)
	{
		const auto& item = klineData[i];
		std::string itemDayStr(item.day.begin(), item.day.end());
		std::string buyDateStr(buyDate.begin(), buyDate.end());
		if (itemDayStr == buyDateStr)
		{
			int barX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap);
			int highY = drawData.y + static_cast<int>((drawData.maxPrice - item.high) * drawData.unitY);

			CString buyTxt = _T("买");
			CSize buySize = memDC.GetTextExtent(buyTxt);
			int circleRadius = max(buySize.cx, buySize.cy) / 2;
			int buyX = barX + drawData.barWidth / 2;
			int buyY = highY - g_data.RDPI(2) - buySize.cy / 2;

			CPen circlePen(PS_SOLID, 2, COLOR_GOLDEN);
			CPen* pOldPen = memDC.SelectObject(&circlePen);
			CBrush circleBrush(COLOR_GOLDEN);
			memDC.SelectObject(&circleBrush);
			memDC.Ellipse(buyX - circleRadius, buyY - circleRadius, buyX + circleRadius, buyY + circleRadius);
			memDC.SetTextColor(COLOR_BLACK);
			memDC.TextOut(buyX - buySize.cx / 2, buyY - buySize.cy / 2, buyTxt);

			double costPrice = g_data.GetCostPrice(hover.stockId);
			if (costPrice > 0)
			{
				CString priceTxt = CCommon::FormatFloat(costPrice);
				CSize priceSize = memDC.GetTextExtent(priceTxt);

				CString percentTxt;
				CSize percentSize;
				if (drawData.stockInfo && drawData.stockInfo->currentPrice > 0)
				{
					double currentPrice = drawData.stockInfo->currentPrice;
					double changePercent = (costPrice - currentPrice) / currentPrice * 100;
					if (changePercent >= 0)
						percentTxt.Format(_T(" +%.1f%%"), changePercent);
					else
						percentTxt.Format(_T(" %.1f%%"), changePercent);
					percentSize = memDC.GetTextExtent(percentTxt);
				}

				int totalTextWidth = priceSize.cx + (percentTxt.IsEmpty() ? 0 : g_data.RDPI(2) + percentSize.cx);
				bool drawOnRight = (buyX + circleRadius + g_data.RDPI(2) + totalTextWidth <= drawData.x + drawData.w);

				memDC.SetTextColor(COLOR_GOLDEN);
				if (drawOnRight)
				{
					int priceX = buyX + circleRadius + g_data.RDPI(2);
					int priceY = buyY - priceSize.cy / 2;
					memDC.TextOut(priceX, priceY, priceTxt);

					if (!percentTxt.IsEmpty())
					{
						int percentX = priceX + priceSize.cx + g_data.RDPI(2);
						int percentY = buyY - percentSize.cy / 2;
						memDC.TextOut(percentX, percentY, percentTxt);
					}
				}
				else
				{
					int priceX = buyX - circleRadius - g_data.RDPI(2) - priceSize.cx;
					int priceY = buyY - priceSize.cy / 2;
					memDC.TextOut(priceX, priceY, priceTxt);

					if (!percentTxt.IsEmpty())
					{
						int percentX = priceX - g_data.RDPI(2) - percentSize.cx;
						int percentY = buyY - percentSize.cy / 2;
						memDC.TextOut(percentX, percentY, percentTxt);
					}
				}
			}

			memDC.SelectObject(pOldPen);
			break;
		}
	}
}

void CKLineChart::DrawKLinePeriodMarkers(CDC& memDC, const KLineDrawData& drawData, const PeriodPoint periodHighs[3], const PeriodPoint periodLows[3])
{
	std::vector<std::pair<PeriodPoint, bool>> markers;
	for (int p = 0; p < 3; p++)
	{
		if (periodHighs[p].index >= 0) markers.push_back({ periodHighs[p], true });
		if (periodLows[p].index >= 0) markers.push_back({ periodLows[p], false });
	}

	auto drawMarker = [&](const PeriodPoint& pt, bool isHigh) {
		if (pt.index < drawData.finalStartIndex || pt.index >= drawData.klineData->size()) return;
		int barX = drawData.x + (pt.index - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap);
		int markerY = drawData.y + static_cast<int>((drawData.maxPrice - pt.price) * drawData.unitY);

		CString txt = isHigh ? _T("高") : _T("低");
		COLORREF circleColor = isHigh ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CSize txtSize = memDC.GetTextExtent(txt);
		int circleRadius = max(txtSize.cx, txtSize.cy) / 2;
		int centerX = barX + drawData.barWidth / 2;
		int centerY = isHigh ? markerY - g_data.RDPI(2) - circleRadius : markerY + g_data.RDPI(2) + circleRadius;

		CPen circlePen(PS_SOLID, 2, circleColor);
		CPen* pOldPen = memDC.SelectObject(&circlePen);
		CBrush circleBrush(circleColor);
		memDC.SelectObject(&circleBrush);
		memDC.Ellipse(centerX - circleRadius, centerY - circleRadius, centerX + circleRadius, centerY + circleRadius);
		memDC.SetTextColor(COLOR_WHITE);
		memDC.TextOut(centerX - txtSize.cx / 2, centerY - txtSize.cy / 2, txt);

		CString priceTxt = CCommon::FormatFloat(pt.price);
		CSize priceSize = memDC.GetTextExtent(priceTxt);

		CString percentTxt;
		CSize percentSize;
		if (drawData.stockInfo && drawData.stockInfo->currentPrice > 0)
		{
			double currentPrice = drawData.stockInfo->currentPrice;
			double changePercent = (pt.price - currentPrice) / currentPrice * 100;
			if (changePercent >= 0)
				percentTxt.Format(_T(" +%.1f%%"), changePercent);
			else
				percentTxt.Format(_T(" %.1f%%"), changePercent);
			percentSize = memDC.GetTextExtent(percentTxt);
		}

		int totalTextWidth = priceSize.cx + (percentTxt.IsEmpty() ? 0 : g_data.RDPI(2) + percentSize.cx);
		bool drawOnRight = (centerX + circleRadius + g_data.RDPI(2) + totalTextWidth <= drawData.x + drawData.w);

		memDC.SetTextColor(circleColor);
		if (drawOnRight)
		{
			int priceX = centerX + circleRadius + g_data.RDPI(2);
			int priceY = centerY - priceSize.cy / 2;
			memDC.TextOut(priceX, priceY, priceTxt);

			if (!percentTxt.IsEmpty())
			{
				int percentX = priceX + priceSize.cx + g_data.RDPI(2);
				int percentY = centerY - percentSize.cy / 2;
				memDC.TextOut(percentX, percentY, percentTxt);
			}
		}
		else
		{
			int priceX = centerX - circleRadius - g_data.RDPI(2) - priceSize.cx;
			int priceY = centerY - priceSize.cy / 2;
			memDC.TextOut(priceX, priceY, priceTxt);

			if (!percentTxt.IsEmpty())
			{
				int percentX = priceX - g_data.RDPI(2) - percentSize.cx;
				int percentY = centerY - percentSize.cy / 2;
				memDC.TextOut(percentX, percentY, percentTxt);
			}
		}

		memDC.SelectObject(pOldPen);
		};

	for (const auto& m : markers) drawMarker(m.first, m.second);
}



// ========== 走势图绘制 ==========

void CKLineChart::DrawKLineTrendCurve(CDC& memDC, const KLineDrawData& drawData, std::vector<CPoint>& outPoints)
{
	const auto& klineData = *drawData.klineData;
	outPoints.clear();

	CPen pLine(PS_SOLID, 1, COLOR_DARK_GRAY_BORDER);
	memDC.SelectObject(&pLine);

	for (int i = drawData.finalStartIndex; i < klineData.size(); i++)
	{
		const auto& item = klineData[i];
		int pointX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap) + drawData.barWidth / 2;
		int pointY = drawData.y + static_cast<int>((drawData.maxPrice - item.close) * drawData.unitY);
		outPoints.push_back(CPoint(pointX, pointY));
	}

	if (!outPoints.empty())
	{
		memDC.MoveTo(outPoints[0].x, outPoints[0].y);
		for (size_t i = 1; i < outPoints.size(); i++)
			memDC.LineTo(outPoints[i].x, outPoints[i].y);
	}
}

void CKLineChart::DrawKLineTrendBuyMarkers(CDC& memDC, const KLineDrawData& drawData, const std::vector<CPoint>& closePoints, const HoverState& hover)
{
	const auto& klineData = *drawData.klineData;
	std::wstring buyDate = g_data.GetBuyDate(hover.stockId);
	if (buyDate.empty() || closePoints.empty()) return;

	for (int i = drawData.finalStartIndex; i < klineData.size(); i++)
	{
		const auto& item = klineData[i];
		std::string itemDayStr(item.day.begin(), item.day.end());
		std::string buyDateStr(buyDate.begin(), buyDate.end());
		if (itemDayStr == buyDateStr)
		{
			int pointIdx = i - drawData.finalStartIndex;
			if (pointIdx >= 0 && pointIdx < closePoints.size())
			{
				CString buyTxt = _T("买");
				CSize buySize = memDC.GetTextExtent(buyTxt);
				int circleRadius = max(buySize.cx, buySize.cy) / 2;
				int buyX = closePoints[pointIdx].x;
				int buyY = closePoints[pointIdx].y - g_data.RDPI(2) - buySize.cy / 2;

				CPen circlePen(PS_SOLID, 2, COLOR_GOLDEN);
				CPen* pOldPen = memDC.SelectObject(&circlePen);
				CBrush circleBrush(COLOR_GOLDEN);
				memDC.SelectObject(&circleBrush);
				memDC.Ellipse(buyX - circleRadius, buyY - circleRadius, buyX + circleRadius, buyY + circleRadius);
				memDC.SetTextColor(COLOR_BLACK);
				memDC.TextOut(buyX - buySize.cx / 2, buyY - buySize.cy / 2, buyTxt);

				double costPrice = g_data.GetCostPrice(hover.stockId);
				if (costPrice > 0)
				{
					CString priceTxt = CCommon::FormatFloat(costPrice);
					CSize priceSize = memDC.GetTextExtent(priceTxt);

					CString percentTxt;
					CSize percentSize;
					if (drawData.stockInfo && drawData.stockInfo->currentPrice > 0)
					{
						double currentPrice = drawData.stockInfo->currentPrice;
						double changePercent = (costPrice - currentPrice) / currentPrice * 100;
						if (changePercent >= 0)
							percentTxt.Format(_T(" +%.1f%%"), changePercent);
						else
							percentTxt.Format(_T(" %.1f%%"), changePercent);
						percentSize = memDC.GetTextExtent(percentTxt);
					}

					int totalTextWidth = priceSize.cx + (percentTxt.IsEmpty() ? 0 : g_data.RDPI(2) + percentSize.cx);
					bool drawOnRight = (buyX + circleRadius + g_data.RDPI(2) + totalTextWidth <= drawData.x + drawData.w);

					memDC.SetTextColor(COLOR_GOLDEN);
					if (drawOnRight)
					{
						int priceX = buyX + circleRadius + g_data.RDPI(2);
						int priceY = buyY - priceSize.cy / 2;
						memDC.TextOut(priceX, priceY, priceTxt);

						if (!percentTxt.IsEmpty())
						{
							int percentX = priceX + priceSize.cx + g_data.RDPI(2);
							int percentY = buyY - percentSize.cy / 2;
							memDC.TextOut(percentX, percentY, percentTxt);
						}
					}
					else
					{
						int priceX = buyX - circleRadius - g_data.RDPI(2) - priceSize.cx;
						int priceY = buyY - priceSize.cy / 2;
						memDC.TextOut(priceX, priceY, priceTxt);

						if (!percentTxt.IsEmpty())
						{
							int percentX = priceX - g_data.RDPI(2) - percentSize.cx;
							int percentY = buyY - percentSize.cy / 2;
							memDC.TextOut(percentX, percentY, percentTxt);
						}
					}
				}

				memDC.SelectObject(pOldPen);
			}
			break;
		}
	}
}

void CKLineChart::DrawKLineTrendPeriodMarkers(CDC& memDC, const KLineDrawData& drawData, const std::vector<CPoint>& closePoints, const PeriodPoint periodHighs[3], const PeriodPoint periodLows[3])
{
	std::vector<std::pair<PeriodPoint, bool>> markers;
	for (int p = 0; p < 3; p++)
	{
		if (periodHighs[p].index >= 0) markers.push_back({ periodHighs[p], true });
		if (periodLows[p].index >= 0) markers.push_back({ periodLows[p], false });
	}

	auto drawMarker = [&](const PeriodPoint& pt, bool isHigh) {
		if (pt.index < drawData.finalStartIndex || pt.index >= drawData.klineData->size()) return;
		int pointIdx = pt.index - drawData.finalStartIndex;
		if (pointIdx < 0 || pointIdx >= closePoints.size()) return;
		CPoint ptPos = closePoints[pointIdx];

		CString txt = isHigh ? _T("高") : _T("低");
		COLORREF circleColor = isHigh ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CSize txtSize = memDC.GetTextExtent(txt);
		int circleRadius = max(txtSize.cx, txtSize.cy) / 2;
		int centerX = ptPos.x;
		int centerY = isHigh ? ptPos.y - g_data.RDPI(2) - circleRadius : ptPos.y + g_data.RDPI(2) + circleRadius;

		CPen circlePen(PS_SOLID, 2, circleColor);
		CPen* pOldPen = memDC.SelectObject(&circlePen);
		CBrush circleBrush(circleColor);
		memDC.SelectObject(&circleBrush);
		memDC.Ellipse(centerX - circleRadius, centerY - circleRadius, centerX + circleRadius, centerY + circleRadius);
		memDC.SetTextColor(COLOR_WHITE);
		memDC.TextOut(centerX - txtSize.cx / 2, centerY - txtSize.cy / 2, txt);

		CString priceTxt = CCommon::FormatFloat(pt.price);
		CSize priceSize = memDC.GetTextExtent(priceTxt);

		CString percentTxt;
		CSize percentSize;
		if (drawData.stockInfo && drawData.stockInfo->currentPrice > 0)
		{
			double currentPrice = drawData.stockInfo->currentPrice;
			double changePercent = (pt.price - currentPrice) / currentPrice * 100;
			if (changePercent >= 0)
				percentTxt.Format(_T(" +%.1f%%"), changePercent);
			else
				percentTxt.Format(_T(" %.1f%%"), changePercent);
			percentSize = memDC.GetTextExtent(percentTxt);
		}

		int totalTextWidth = priceSize.cx + (percentTxt.IsEmpty() ? 0 : g_data.RDPI(2) + percentSize.cx);
		bool drawOnRight = (centerX + circleRadius + g_data.RDPI(2) + totalTextWidth <= drawData.x + drawData.w);

		memDC.SetTextColor(circleColor);
		if (drawOnRight)
		{
			int priceX = centerX + circleRadius + g_data.RDPI(2);
			int priceY = centerY - priceSize.cy / 2;
			memDC.TextOut(priceX, priceY, priceTxt);

			if (!percentTxt.IsEmpty())
			{
				int percentX = priceX + priceSize.cx + g_data.RDPI(2);
				int percentY = centerY - percentSize.cy / 2;
				memDC.TextOut(percentX, percentY, percentTxt);
			}
		}
		else
		{
			int priceX = centerX - circleRadius - g_data.RDPI(2) - priceSize.cx;
			int priceY = centerY - priceSize.cy / 2;
			memDC.TextOut(priceX, priceY, priceTxt);

			if (!percentTxt.IsEmpty())
			{
				int percentX = priceX - g_data.RDPI(2) - percentSize.cx;
				int percentY = centerY - percentSize.cy / 2;
				memDC.TextOut(percentX, percentY, percentTxt);
			}
		}

		memDC.SelectObject(pOldPen);
		};

	for (const auto& m : markers) drawMarker(m.first, m.second);
}





void CKLineChart::DrawBollBands(CDC& memDC, const KLineDrawData& drawData)
{
	const auto& klineData = *drawData.klineData;
	const int N = 20;       // 布林带周期
	const int K = 2;        // 标准差倍数

	int endIndex = static_cast<int>(klineData.size());
	int beginIndex = drawData.finalStartIndex;
	if (endIndex <= beginIndex)
		return;

	// 计算布林带：MA20 ± 2σ
	std::vector<double> upperBand(endIndex, 0);
	std::vector<double> middleBand(endIndex, 0);
	std::vector<double> lowerBand(endIndex, 0);

	for (int i = beginIndex; i < endIndex; i++)
	{
		if (i < N - 1)
		{
			// 数据不足，无法计算
			upperBand[i] = middleBand[i] = lowerBand[i] = 0;
			continue;
		}
		double sum = 0;
		for (int j = i - N + 1; j <= i; j++)
		{
			sum += klineData[j].close;
		}
		double ma = sum / N;
		double variance = 0;
		for (int j = i - N + 1; j <= i; j++)
		{
			double diff = klineData[j].close - ma;
			variance += diff * diff;
		}
		double stddev = std::sqrt(variance / N);
		middleBand[i] = ma;
		upperBand[i] = ma + K * stddev;
		lowerBand[i] = ma - K * stddev;
	}

	// 使用 drawData 的 unitY 进行坐标转换
	auto priceToY = [&](double price) {
		return drawData.y + static_cast<int>((drawData.maxPrice - price) * drawData.unitY);
		};

	// 绘制上轨（金色）
	auto drawBandLine = [&](const std::vector<double>& band, COLORREF color) {
		CPen bandPen(PS_SOLID, 1, color);
		memDC.SelectObject(&bandPen);
		bool first = true;
		for (int i = drawData.finalStartIndex; i < endIndex; i++)
		{
			if (band[i] <= 0) continue;
			int barX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap) + drawData.barWidth / 2;
			int py = priceToY(band[i]);
			if (first)
			{
				memDC.MoveTo(barX, py);
				first = false;
			}
			else
			{
				memDC.LineTo(barX, py);
			}
		}
		};

	drawBandLine(upperBand, COLOR_GOLDEN);
	drawBandLine(middleBand, COLOR_GRAY_PURPLE);
	drawBandLine(lowerBand, COLOR_GOLDEN);

	// 填充上下轨之间的区域（浅色）
	CPen nullPen(PS_NULL, 1, RGB(0, 0, 0));
	CPen* pOldFillPen = memDC.SelectObject(&nullPen);
	int prevUpperY = -1;
	int prevLowerY = -1;
	int prevX = -1;
	for (int i = drawData.finalStartIndex; i < endIndex; i++)
	{
		if (upperBand[i] <= 0 || lowerBand[i] <= 0) continue;
		int barX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap) + drawData.barWidth / 2;
		int upperY = priceToY(upperBand[i]);
		int lowerY = priceToY(lowerBand[i]);
		if (prevUpperY >= 0 && prevLowerY >= 0 && prevX >= 0)
		{
			// 绘制梯形填充（用4个点构成的多边形）
			CBrush fillBrush(RGB(240, 240, 220));
			CPoint pts[4] = {
				CPoint(prevX, prevUpperY),
				CPoint(barX, upperY),
				CPoint(barX, lowerY),
				CPoint(prevX, prevLowerY)
			};
			CBrush* pOldBrush = memDC.SelectObject(&fillBrush);
			memDC.Polygon(pts, 4);
			memDC.SelectObject(pOldBrush);
		}
		prevUpperY = upperY;
		prevLowerY = lowerY;
		prevX = barX;
	}
	memDC.SelectObject(pOldFillPen);
}

void CKLineChart::DrawMin5HourLines(CDC& memDC, const KLineDrawData& drawData)
{
	const auto& klineData = *drawData.klineData;
	if (klineData.empty())
		return;

	// 在5分钟K线模式下，K线数据通常是5分钟间隔
	// 我们在整点位置（9:30, 10:00, 10:30, ...）绘制垂直网格线和时间标签
	// 假设 klineData[i].day 字段在5分钟模式下存放时间字符串 "HH:MM"
	// 如果 day 字段是日期，则跳过此绘制

	CPen hourPen(PS_DOT, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&hourPen);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);
	memDC.SetTextColor(COLOR_GRAY_TEXT);

	int lastHour = -1;
	for (int i = drawData.finalStartIndex; i < static_cast<int>(klineData.size()); i++)
	{
		const auto& item = klineData[i];
		// 解析 day 字段，判断是否包含时间信息
		// 5分钟K线模式下 day 格式可能是 "YYYY-MM-DD HH:MM" 或仅 "HH:MM"
		const std::string& dayStr = item.day;
		if (dayStr.empty()) continue;

		// 尝试提取小时部分
		int hour = -1;
		size_t spacePos = dayStr.find(' ');
		if (spacePos != std::string::npos && dayStr.length() >= spacePos + 3)
		{
			// "YYYY-MM-DD HH:MM" 格式
			hour = atoi(dayStr.substr(spacePos + 1, 2).c_str());
		}
		else if (dayStr.length() >= 5 && dayStr[2] == ':')
		{
			// "HH:MM" 格式
			hour = atoi(dayStr.substr(0, 2).c_str());
		}

		if (hour < 0) continue;

		// 在整点（且非第一根）绘制网格线
		if (hour != lastHour && lastHour >= 0)
		{
			int barX = drawData.x + (i - drawData.finalStartIndex) * (drawData.barWidth + drawData.gap);
			memDC.MoveTo(barX, drawData.y);
			memDC.LineTo(barX, drawData.y + drawData.h);

			// 时间标签
			CString label;
			label.Format(_T("%d:00"), hour);
			int labelY = drawData.y + drawData.h + g_data.RDPI(2);
			CSize sz = memDC.GetTextExtent(label);
			int labelX = barX - sz.cx / 2;
			labelX = max(g_data.RDPI(2), min(labelX, drawData.x + drawData.w - sz.cx - g_data.RDPI(2)));
			memDC.TextOut(labelX, labelY, label);
		}
		lastHour = hour;
	}

	memDC.SetBkMode(oldBkMode);
	memDC.SelectObject(pOldPen);
}