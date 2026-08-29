#include "pch.h"
#include "ChipPeakPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <algorithm>
#include <cmath>
#include <set>

void CChipPeakPanel::Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
	const STOCK::ChipDistribution& chipData, const std::vector<STOCK::TimelinePoint>& timelinePoint,
	UIViewMode viewMode)
{
	// 按比例分配行高，与盘口面板一致
	const int totalRows = 19;
	const int headerHeight = g_data.RDPI(26);  // 主标题栏高度
	const int obTitleH = g_data.RDPI(16);       // 盘口标题栏高度，与走势图标题栏一致
	const int topOffset = headerHeight + obTitleH;  // 内容从主标题栏+盘口标题栏下方开始
	const int panelW = right - left;
	// 绘制盘口标题栏背景 (#181B22)
	memDC.FillSolidRect(left, headerHeight, panelW, obTitleH, COLOR_BG_HEADER);
	const int rowHeight = (height - obTitleH) / totalRows;
	const int panelH = height - obTitleH;
	if (panelW <= 0 || panelH <= 0)
		return;
	const int rem = panelH % totalRows;    // 余数：前rem行多1px
	// 辅助：计算第i行(0-based)的Y坐标
	auto rowY = [&](int i) -> int {
		if (i < rem) return topOffset + i * (rowHeight + 1);
		else return topOffset + rem * (rowHeight + 1) + (i - rem) * rowHeight;
		};
	// 辅助：计算第i行的高度
	auto rowH = [&](int i) -> int {
		return (i < rem) ? (rowHeight + 1) : rowHeight;
		};

	// 填充内容区域深色背景 (#14161D)
	memDC.FillSolidRect(left, topOffset, panelW, panelH, COLOR_BG_PANEL);
	memDC.SetBkMode(TRANSPARENT);

	if (!chipData.IsValid())
	{
		memDC.SetTextColor(COLOR_TEXT_DIM);
		memDC.TextOut(left + g_data.RDPI(5), topOffset + max(0, (panelH - memDC.GetTextExtent(_T("暂无筹码数据")).cy) / 2), _T("暂无筹码数据"));
		return;
	}

	std::vector<STOCK::ChipPoint> points = chipData.points;
	if (viewMode < UI_VIEW_DAY_KLINE && stockInfo.circulatingAShares > 0 && !timelinePoint.empty())
	{
		const double CHIP_ATTRITION_N = 1.3;
		const double MAX_EFFECT_TURN = 0.85;
		const double PRICE_STEP = 0.01;
		const double FLOAT_CORRECT_THRESHOLD = 0.01;

		double yMin = 999999.0;
		double yMax = 0.0;
		for (const auto& point : chipData.points)
		{
			if (point.price > 0 && point.percent > 0.0001)
			{
				yMin = min(yMin, point.price);
				yMax = max(yMax, point.price);
			}
		}
		if (yMin > yMax || yMin <= 0.0)
		{
			for (const auto& point : chipData.points)
			{
				if (point.price > 0)
				{
					yMin = min(yMin, point.price);
					yMax = max(yMax, point.price);
				}
			}
		}

		double limitDown = stockInfo.prevClosePrice > 0 && stockInfo.priceLimit > 0 ? stockInfo.prevClosePrice - stockInfo.priceLimit : yMin;
		double limitUp = stockInfo.prevClosePrice > 0 && stockInfo.priceLimit > 0 ? stockInfo.prevClosePrice + stockInfo.priceLimit : yMax;
		double gridMin = floor(min(yMin, limitDown) * 100.0) / 100.0;
		double gridMax = ceil(max(yMax, limitUp) * 100.0) / 100.0;
		if (gridMax > gridMin)
		{
			std::vector<double> priceLevels;
			for (double cur = gridMin; cur <= gridMax + PRICE_STEP / 2; cur += PRICE_STEP)
				priceLevels.push_back(round(cur * 100.0) / 100.0);
			std::vector<double> chipArray(priceLevels.size(), 0.0);
			auto findPriceIndex = [&](double price) -> int {
				auto it = std::lower_bound(priceLevels.begin(), priceLevels.end(), round(price * 100.0) / 100.0);
				return static_cast<int>(it - priceLevels.begin());
				};

			for (const auto& point : chipData.points)
			{
				int idx = findPriceIndex(point.price);
				if (idx >= 0 && idx < static_cast<int>(chipArray.size()))
					chipArray[idx] += point.percent * stockInfo.circulatingAShares;
			}

			std::set<std::string> processedMinuteSet;
			for (const auto& item : timelinePoint)
			{
				if (item.volume <= 0 || processedMinuteSet.find(item.time) != processedMinuteSet.end())
					continue;
				processedMinuteSet.insert(item.time);

				double minuteTurn = static_cast<double>(item.volume) / static_cast<double>(stockInfo.circulatingAShares);
				double effTurn = min(MAX_EFFECT_TURN, minuteTurn * CHIP_ATTRITION_N);
				double retainRate = 1.0 - effTurn;
				double addTotalShare = effTurn * stockInfo.circulatingAShares;
				for (auto& val : chipArray)
					val *= retainRate;

				double price = item.price > 0 ? item.price : item.averagePrice;
				int idx = findPriceIndex(price);
				if (idx >= 0 && idx < static_cast<int>(chipArray.size()))
					chipArray[idx] += addTotalShare;

				double sumAll = 0.0;
				for (auto val : chipArray)
					sumAll += val;
				if (sumAll > 0 && fabs(sumAll - stockInfo.circulatingAShares) > FLOAT_CORRECT_THRESHOLD)
				{
					double scale = static_cast<double>(stockInfo.circulatingAShares) / sumAll;
					for (auto& val : chipArray)
						val *= scale;
				}
			}

			double totalShares = static_cast<double>(stockInfo.circulatingAShares);
			points.clear();
			points.reserve(priceLevels.size());
			double weightSum = 0.0;
			double profitShare = 0.0;
			double currentPrice = stockInfo.currentPrice > 0 ? stockInfo.currentPrice : stockInfo.prevClosePrice;
			for (size_t i = 0; i < priceLevels.size(); ++i)
			{
				weightSum += priceLevels[i] * chipArray[i];
				if (priceLevels[i] < currentPrice)
					profitShare += chipArray[i];
				STOCK::ChipPoint point;
				point.price = priceLevels[i];
				point.percent = totalShares > 0 ? chipArray[i] / totalShares : 0.0;
				points.push_back(point);
			}
		}
	}

	double totalPercent = 0.0;
	double weightSum = 0.0;
	double profitPercent = 0.0;
	double maxPercent = 0.0;
	double currentPrice = stockInfo.currentPrice > 0 ? stockInfo.currentPrice : stockInfo.prevClosePrice;
	for (const auto& point : points)
	{
		if (point.percent <= 0) continue;
		totalPercent += point.percent;
		weightSum += point.price * point.percent;
		if (point.price < currentPrice)
			profitPercent += point.percent;
		maxPercent = max(maxPercent, point.percent);
	}
	if (totalPercent <= 0 || maxPercent <= 0)
		return;

	double avgCost = weightSum / totalPercent;
	double cumPercent = 0.0;
	double chip90Low = 0.0;
	double chip90High = 0.0;
	bool findLow = false;
	std::sort(points.begin(), points.end(), [](const STOCK::ChipPoint& a, const STOCK::ChipPoint& b) { return a.price < b.price; });
	for (const auto& point : points)
	{
		cumPercent += point.percent;
		if (!findLow && cumPercent >= totalPercent * 0.05)
		{
			chip90Low = point.price;
			findLow = true;
		}
		if (cumPercent >= totalPercent * 0.95)
		{
			chip90High = point.price;
			break;
		}
	}

	// 筹码峰图：从行0开始，到底部倒数第3行
	const int chartRowEnd = totalRows - 3;
	const int chartTop = topOffset + g_data.RDPI(2);
	const int chartBottom = rowY(chartRowEnd);
	const int chartLeft = left + g_data.RDPI(6);
	const int chartRight = right - g_data.RDPI(6);
	const int chartH = chartBottom - chartTop;
	const int chartW = chartRight - chartLeft;
	if (chartH <= 0 || chartW <= 0)
		return;

	double minPrice = 999999.0;
	double maxPrice = 0.0;

	// 计算有效筹码价格区间（过滤累计头尾各0.5%的衰减残余噪点，避免远期极小残值拉大Y轴导致顶部大片空白）
	double cum = 0.0;
	double effMinPrice = points.front().price;
	double effMaxPrice = points.back().price;
	bool foundMin = false;
	for (const auto& pt : points)
	{
		if (pt.percent <= 0) continue;
		cum += pt.percent;
		if (!foundMin && cum >= totalPercent * 0.005)
		{
			effMinPrice = pt.price;
			foundMin = true;
		}
		if (cum >= totalPercent * 0.995)
		{
			effMaxPrice = pt.price;
			break;
		}
	}
	if (effMinPrice > effMaxPrice || effMinPrice <= 0.0)
	{
		effMinPrice = points.front().price;
		effMaxPrice = points.back().price;
	}

	minPrice = effMinPrice;
	maxPrice = effMaxPrice;
	if (currentPrice > 0)
	{
		minPrice = min(minPrice, currentPrice);
		maxPrice = max(maxPrice, currentPrice);
	}
	if (avgCost > 0)
	{
		minPrice = min(minPrice, avgCost);
		maxPrice = max(maxPrice, avgCost);
	}
	if (chip90Low > 0) minPrice = min(minPrice, chip90Low);
	if (chip90High > 0) maxPrice = max(maxPrice, chip90High);

	double padding = (maxPrice - minPrice) * 0.04;
	if (padding < 0.01) padding = 0.01;
	minPrice -= padding;
	maxPrice += padding;
	if (minPrice < 0.01) minPrice = 0.01;
	if (maxPrice <= minPrice)
		return;

	// 5点高斯加权平滑滤波，消除离散价格阶梯毛刺，形成类似同花顺的圆润波峰
	const size_t nPoints = points.size();
	std::vector<double> smoothed(nPoints, 0.0);
	static const double kernel[5] = { 1.0, 4.0, 6.0, 4.0, 1.0 };
	for (size_t i = 0; i < nPoints; ++i)
	{
		double wSum = 0.0;
		double vSum = 0.0;
		for (int k = -2; k <= 2; ++k)
		{
			int idx = static_cast<int>(i) + k;
			if (idx >= 0 && idx < static_cast<int>(nPoints))
			{
				double kw = kernel[k + 2];
				vSum += points[idx].percent * kw;
				wSum += kw;
			}
		}
		smoothed[i] = (wSum > 0.0) ? (vSum / wSum) : points[i].percent;
	}

	double maxSmoothed = 0.0;
	for (double val : smoothed)
		maxSmoothed = max(maxSmoothed, val);
	if (maxSmoothed <= 0.0)
		maxSmoothed = maxPercent;

	auto getPercentAtPrice = [&](double p) -> double {
		if (points.empty() || p < points.front().price || p > points.back().price)
			return 0.0;
		auto it = std::lower_bound(points.begin(), points.end(), p, [](const STOCK::ChipPoint& pt, double val) {
			return pt.price < val;
			});
		if (it == points.begin()) return smoothed[0];
		if (it == points.end()) return smoothed.back();
		size_t idx1 = it - points.begin();
		size_t idx0 = idx1 - 1;
		double p0 = points[idx0].price;
		double p1 = points[idx1].price;
		if (p1 <= p0) return smoothed[idx0];
		double t = (p - p0) / (p1 - p0);
		return smoothed[idx0] + t * (smoothed[idx1] - smoothed[idx0]);
		};

	CPen borderPen(PS_SOLID, 1, COLOR_DARK_GRAY_BORDER);
	CPen* oldPen = memDC.SelectObject(&borderPen);
	CBrush* pOldBrush = static_cast<CBrush*>(memDC.SelectStockObject(NULL_BRUSH));
	memDC.Rectangle(chartLeft, chartTop, chartRight, chartBottom);
	memDC.SelectObject(pOldBrush);
	memDC.SelectObject(oldPen);

	auto priceToY = [&](double price) -> int {
		int y = chartBottom - static_cast<int>((price - minPrice) / (maxPrice - minPrice) * chartH);
		return max(chartTop, min(y, chartBottom));
		};

	// 逐像素扫描填充平滑波峰，并收集外轮廓线点集
	std::vector<CPoint> greenContour;
	std::vector<CPoint> redContour;
	const int maxPeakW = chartW - g_data.RDPI(8);

	for (int y = chartTop; y < chartBottom; ++y)
	{
		double priceAtY = minPrice + static_cast<double>(chartBottom - y) / chartH * (maxPrice - minPrice);
		double pct = getPercentAtPrice(priceAtY);
		int barW = max(0, static_cast<int>(pct / maxSmoothed * maxPeakW));

		if (barW > 0)
		{
			COLORREF fillColor = (priceAtY <= currentPrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
			memDC.FillSolidRect(chartLeft, y, barW, 1, fillColor);
		}

		CPoint contourPt(chartLeft + barW, y);
		if (priceAtY > currentPrice)
		{
			greenContour.push_back(contourPt);
		}
		else
		{
			if (redContour.empty() && !greenContour.empty())
			{
				redContour.push_back(greenContour.back());
			}
			redContour.push_back(contourPt);
		}
	}

	// 绘制波峰外边缘轮廓线（平滑高光边缘）
	if (greenContour.size() >= 2)
	{
		CPen greenPen(PS_SOLID, 1, COLOR_GREEN_DOWN);
		oldPen = memDC.SelectObject(&greenPen);
		memDC.Polyline(greenContour.data(), static_cast<int>(greenContour.size()));
		memDC.SelectObject(oldPen);
	}
	if (redContour.size() >= 2)
	{
		CPen redPen(PS_SOLID, 1, COLOR_RED_UP);
		oldPen = memDC.SelectObject(&redPen);
		memDC.Polyline(redContour.data(), static_cast<int>(redContour.size()));
		memDC.SelectObject(oldPen);
	}

	if (avgCost >= minPrice && avgCost <= maxPrice)
	{
		int avgY = priceToY(avgCost);
		CPen avgPen(PS_DOT, 1, COLOR_BLUE_AVG1);
		oldPen = memDC.SelectObject(&avgPen);
		memDC.MoveTo(chartLeft, avgY);
		memDC.LineTo(chartRight, avgY);
		memDC.SelectObject(oldPen);
		// 均价标签绘制在线条右侧
		CString avgLabel;
		avgLabel.Format(_T("均:%s"), CCommon::FormatFloat(avgCost));
		memDC.SetTextColor(COLOR_BLUE_AVG1);
		CSize avgLabelSize = memDC.GetTextExtent(avgLabel);
		int avgLabelX = chartRight - avgLabelSize.cx - g_data.RDPI(2);
		int avgLabelY = max(chartTop, min(avgY - avgLabelSize.cy / 2, chartBottom - avgLabelSize.cy));
		memDC.TextOut(avgLabelX, avgLabelY, avgLabel);
	}

	if (currentPrice >= minPrice && currentPrice <= maxPrice)
	{
		int y = priceToY(currentPrice);
		CPen curPen(PS_DOT, 1, COLOR_GREEN_AVG3);
		oldPen = memDC.SelectObject(&curPen);
		memDC.MoveTo(chartLeft, y);
		memDC.LineTo(chartRight, y);
		memDC.SelectObject(oldPen);
		CString priceTxt;
		priceTxt.Format(_T("现 %s"), CCommon::FormatFloat(currentPrice));
		CSize txtSize = memDC.GetTextExtent(priceTxt);
		int paddingX = g_data.RDPI(3);
		int paddingY = g_data.RDPI(1);
		int labelW = txtSize.cx + paddingX * 2;
		int labelH = txtSize.cy + paddingY * 2;
		int labelLeft = chartRight - labelW - g_data.RDPI(2);
		int labelTop = min(max(chartTop, y - labelH / 2), chartBottom - labelH);
		CRect labelRect(labelLeft, labelTop, labelLeft + labelW, labelTop + labelH);
		memDC.FillSolidRect(&labelRect, COLOR_CARD_SELECTED);
		memDC.SetTextColor(COLOR_TEXT_PRIMARY);
		memDC.TextOut(labelRect.left + paddingX, labelRect.top + paddingY, priceTxt);
	}

	CString highTxt = CCommon::FormatFloat(maxPrice);
	CString lowTxt = CCommon::FormatFloat(minPrice);
	memDC.SetTextColor(COLOR_TEXT_MUTED);
	memDC.TextOut(chartRight - memDC.GetTextExtent(highTxt).cx - g_data.RDPI(2), chartTop + g_data.RDPI(1), highTxt);
	memDC.TextOut(chartRight - memDC.GetTextExtent(lowTxt).cx - g_data.RDPI(2), chartBottom - memDC.GetTextExtent(lowTxt).cy - g_data.RDPI(1), lowTxt);

	// 文字信息绘制在筹码峰图下方，拆分为3行
	// 行1：获利比例：标签 + 带红绿背景的数字
	CString profitLabelTxt = _T("获利比例:");
	double profitRatio = totalPercent > 0 ? (profitPercent / totalPercent * 100.0) : 0.0;
	CString profitNumTxt;
	profitNumTxt.Format(_T("%.1f%%"), profitRatio);
	int profitY = rowY(chartRowEnd) + max(0, (rowH(chartRowEnd) - memDC.GetTextExtent(profitLabelTxt).cy) / 2);
	int profitX = left + g_data.RDPI(5);
	memDC.SetTextColor(COLOR_TEXT_MUTED);
	memDC.TextOut(profitX, profitY, profitLabelTxt);
	{
		int labelW = memDC.GetTextExtent(profitLabelTxt).cx;
		int numH = memDC.GetTextExtent(profitNumTxt).cy;
		int barX = profitX + labelW + g_data.RDPI(3);
		// 总长度固定：从标签后到面板右边界，留少量右边距
		int barW = max(0, right - barX - g_data.RDPI(4));
		int barH = numH + g_data.RDPI(2);
		int barY = profitY - g_data.RDPI(1);

		// 红色长度 = 总宽度 × X/100（获利比例）；绿色长度 = 总宽度 × (100-X)/100（套牢比例）
		int redW = static_cast<int>(barW * profitRatio / 100.0 + 0.5);
		int greenW = barW - redW;
		if (redW > 0)
		{
			CRect redRect(barX, barY, barX + redW, barY + barH);
			memDC.FillSolidRect(&redRect, COLOR_RED_UP);
		}
		if (greenW > 0)
		{
			CRect greenRect(barX + redW, barY, barX + redW + greenW, barY + barH);
			memDC.FillSolidRect(&greenRect, COLOR_GREEN_DOWN);
		}

		// 数字绘制在背景条左侧（白色文字，带少量内边距）
		memDC.SetTextColor(COLOR_WHITE);
		memDC.TextOut(barX + g_data.RDPI(4), profitY, profitNumTxt);
	}

	// 行2：平均成本（标签与数值清晰分离）
	int avgYPos = rowY(chartRowEnd + 1) + max(0, (rowH(chartRowEnd + 1) - memDC.GetTextExtent(_T("平均成本:")).cy) / 2);
	int avgXPos = left + g_data.RDPI(5);
	CString avgCostLabel = _T("平均成本: ");
	CString avgCostValStr = CCommon::FormatFloat(avgCost);
	memDC.SetTextColor(COLOR_TEXT_MUTED);
	memDC.TextOut(avgXPos, avgYPos, avgCostLabel);
	memDC.SetTextColor(COLOR_TEXT_PRIMARY);
	memDC.TextOut(avgXPos + memDC.GetTextExtent(avgCostLabel).cx, avgYPos, avgCostValStr);

	// 行3：90%成本区间（使用清晰的波浪号 ~ 分隔，数值高亮，避免粘合）
	int rangeYPos = rowY(chartRowEnd + 2) + max(0, (rowH(chartRowEnd + 2) - memDC.GetTextExtent(_T("90%成本:")).cy) / 2);
	int rangeXPos = left + g_data.RDPI(5);
	CString rangeLabel = _T("90%成本: ");
	CString rangeValStr;
	rangeValStr.Format(_T("%s ~ %s"), CCommon::FormatFloat(chip90Low), CCommon::FormatFloat(chip90High));
	memDC.SetTextColor(COLOR_TEXT_MUTED);
	memDC.TextOut(rangeXPos, rangeYPos, rangeLabel);
	memDC.SetTextColor(COLOR_TEXT_PRIMARY);
	memDC.TextOut(rangeXPos + memDC.GetTextExtent(rangeLabel).cx, rangeYPos, rangeValStr);
}