#include "pch.h"
#include "IndicatorChart.h"
#include "TimelineChart.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <algorithm>

// ========== DrawMACDChart（分时数据版本） ==========

void CIndicatorChart::DrawMACDChart(CDC& memDC, int x, int y, int width, int height, const std::vector<STOCK::TimelinePoint>& timelinePoint, const std::vector<MACDData>& macdData, int startIndex /* = 0 */, int visibleCount /* = -1 */, int xAxisPoints /* = 0 */)
{
	if (timelinePoint.empty() || macdData.empty())
		return;

	int totalPts = static_cast<int>(timelinePoint.size());

	// 计算可见区域的 maxAbs（基于 macdData 中对应可见区域的部分）
	double maxAbs = 0;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(macdData.size()); i++)
	{
		int idx = startIndex + i;
		if (macdData[idx].valid)
		{
			maxAbs = (std::max)(maxAbs, std::abs(macdData[idx].dif));
			maxAbs = (std::max)(maxAbs, std::abs(macdData[idx].dea));
			maxAbs = (std::max)(maxAbs, std::abs(macdData[idx].bar));
		}
	}
	if (maxAbs == 0)
		return;

	int zeroY = y + height / 2;
	float unitY = (height / 2.0f - g_data.RDPI(2)) / static_cast<float>(maxAbs);

	CPen zeroPen(PS_DASHDOT, 1, COLOR_GRAY_MIDDLE);
	CPen* pOldPen = memDC.SelectObject(&zeroPen);
	memDC.MoveTo(x, zeroY);
	memDC.LineTo(x + width, zeroY);

	const int xSlots = (xAxisPoints > 0) ? xAxisPoints : totalPts;
	const int fixedGap = 1;
	int slotWidth = xSlots > 0 ? width / xSlots : 1;
	int barWidth = max(2, static_cast<int>((slotWidth - fixedGap) * 0.8));
	int halfSlot = slotWidth / 2;

	// 绘制 MACD 柱状图
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(macdData.size()); i++)
	{
		int idx = startIndex + i;
		if (!macdData[idx].valid)
			continue;

		int barX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + halfSlot - barWidth / 2;
		double barVal = macdData[idx].bar;
		int barHeight = static_cast<int>(std::abs(barVal) * unitY);
		barHeight = max(1, barHeight);

		COLORREF color = (barVal >= 0) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CBrush brush(color);
		int barY = (barVal >= 0) ? zeroY - barHeight : zeroY;
		memDC.FillRect(CRect(barX, barY, barX + barWidth, zeroY + (barVal >= 0 ? 0 : barHeight)), &brush);
	}

	// 绘制 DIF/DEA 双线（分段动态画笔：多头DIF粗橙实线+DEA浅灰细虚线，空头DEA粗蓝实线+DIF浅灰细虚线）
	{
		// 收集有效点的索引和坐标
		struct MacdPoint { int x; int yDif; int yDea; double dif; double dea; };
		std::vector<MacdPoint> pts;
		pts.reserve(totalPts);
		for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(macdData.size()); i++)
		{
			int idx = startIndex + i;
			if (!macdData[idx].valid) continue;
			int px = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + halfSlot;
			int pyDif = zeroY - static_cast<int>(macdData[idx].dif * unitY);
			int pyDea = zeroY - static_cast<int>(macdData[idx].dea * unitY);
			pts.push_back({ px, pyDif, pyDea, macdData[idx].dif, macdData[idx].dea });
		}
		if (pts.size() >= 2)
		{
			// 切分连续同状态区间（交叉点搭接1根bar，消除间隙）
			enum EMacdState { STATE_LONG, STATE_SHORT };
			struct Seg { int from; int to; EMacdState state; };
			std::vector<Seg> segList;
			int segStart = 0;
			EMacdState curState = (pts[0].dif >= pts[0].dea) ? STATE_LONG : STATE_SHORT;
			for (size_t i = 1; i < pts.size(); i++)
			{
				EMacdState st = (pts[i].dif >= pts[i].dea) ? STATE_LONG : STATE_SHORT;
				if (st != curState)
				{
					segList.push_back({ segStart, static_cast<int>(i), curState });
					segStart = static_cast<int>(i);
					curState = st;
				}
			}
			segList.push_back({ segStart, static_cast<int>(pts.size()) - 1, curState });

			// 分段绘制
			for (const auto& seg : segList)
			{
				std::vector<CPoint> ptsDif, ptsDea;
				ptsDif.reserve(seg.to - seg.from + 1);
				ptsDea.reserve(seg.to - seg.from + 1);
				for (int i = seg.from; i <= seg.to; i++)
				{
					ptsDif.push_back(CPoint(pts[i].x, pts[i].yDif));
					ptsDea.push_back(CPoint(pts[i].x, pts[i].yDea));
				}

				if (seg.state == STATE_LONG)
				{
					// 多头：DIF粗橙色实线（主线），DEA细黑色实线（辅助）
					CPen penMain(PS_SOLID, 2, COLOR_DARK_ORANGE);
					CPen penSub(PS_SOLID, 1, COLOR_BLACK);
					memDC.SelectObject(&penMain);
					memDC.Polyline(&ptsDif[0], static_cast<int>(ptsDif.size()));
					memDC.SelectObject(&penSub);
					memDC.Polyline(&ptsDea[0], static_cast<int>(ptsDea.size()));
				}
				else
				{
					// 空头：DEA粗蓝色实线（主线），DIF细黑色实线（辅助）
					CPen penMain(PS_SOLID, 2, COLOR_BLUE_AVG1);
					CPen penSub(PS_SOLID, 1, COLOR_BLACK);
					memDC.SelectObject(&penMain);
					memDC.Polyline(&ptsDea[0], static_cast<int>(ptsDea.size()));
					memDC.SelectObject(&penSub);
					memDC.Polyline(&ptsDif[0], static_cast<int>(ptsDif.size()));
				}
			}
		}
		else if (pts.size() == 1)
		{
			// 单点：画小圆点
			CPen penMain(PS_SOLID, 2, (pts[0].dif >= pts[0].dea) ? COLOR_DARK_ORANGE : COLOR_BLUE_AVG1);
			memDC.SelectObject(&penMain);
			memDC.SetPixel(pts[0].x, pts[0].yDif, (pts[0].dif >= pts[0].dea) ? COLOR_DARK_ORANGE : COLOR_BLACK);
			memDC.SetPixel(pts[0].x, pts[0].yDea, (pts[0].dif >= pts[0].dea) ? COLOR_BLACK : COLOR_BLUE_AVG1);
		}
	}

	memDC.SelectObject(pOldPen);

	// 绘制金叉死叉标记：金叉红色↑，死叉绿色↓，箭头紧贴圆点
	auto crossSignals = CStockIndicator::DetectMACDCross(macdData);
	const int dotRadius = g_data.RDPI(3);
	const int smallDotRadius = g_data.RDPI(2);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);
	int lastDrawnDir = 0; // 0=无, 1=金叉, -1=死叉

	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(crossSignals.size()); i++)
	{
		int idx = startIndex + i;
		if (crossSignals[idx] == MACDCrossSignal::None)
			continue;

		int markX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + halfSlot;
		int markY = zeroY - static_cast<int>(macdData[idx].dif * unitY);

		bool isGolden = (crossSignals[idx] == MACDCrossSignal::GoldenCross || crossSignals[idx] == MACDCrossSignal::RepeatedGoldenCross);
		bool isRepeated = (crossSignals[idx] == MACDCrossSignal::RepeatedGoldenCross || crossSignals[idx] == MACDCrossSignal::RepeatedDeathCross);
		int curDir = isGolden ? 1 : -1;

		// 方向反转时始终绘制箭头；同方向重复则只画圆点
		bool drawArrow = !isRepeated || (curDir != lastDrawnDir);
		if (drawArrow)
		{
			DrawCrossArrow(memDC, markX, markY, dotRadius, isGolden);
			lastDrawnDir = curDir;
		}

		// 绘制圆点
		int r = (isRepeated && !drawArrow) ? smallDotRadius : dotRadius;
		COLORREF dotColor = isGolden ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CBrush dotBrush(dotColor);
		CPen dotPen(PS_SOLID, 1, dotColor);
		CBrush* pOldBrush = memDC.SelectObject(&dotBrush);
		CPen* pOldDotPen = memDC.SelectObject(&dotPen);
		memDC.Ellipse(markX - r, markY - r, markX + r, markY + r);

		memDC.SelectObject(pOldBrush);
		memDC.SelectObject(pOldDotPen);
	}
	memDC.SetBkMode(oldBkMode);
}

// ========== DrawVolumeChartArea ==========

void CIndicatorChart::DrawVolumeChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight, bool drawTimeLabels, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
	int titleH = g_data.RDPI(16);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	CRect volumeTitleRect(0, areaTop, ctx.chartWidth, areaTop + titleH);
	memDC.FillSolidRect(volumeTitleRect, RGB(245, 245, 245));

	CString perVolStr, perAmtStr;
	bool isVolHovering = !hover.timelineVolumeTitleTip.IsEmpty();
	int displayVolIdx = isVolHovering ? hover.hoveredBarIndex : static_cast<int>(timelinePoint.size()) - 1;
	displayVolIdx = max(0, min(displayVolIdx, static_cast<int>(timelinePoint.size()) - 1));
	if (displayVolIdx >= 0 && displayVolIdx < static_cast<int>(timelinePoint.size()))
	{
		const auto& displayPt = timelinePoint[displayVolIdx];
		perVolStr = CCommon::FormatVolumeInt(displayPt.volume / 100);
		double displayAmount = static_cast<double>(displayPt.volume) * displayPt.price;
		perAmtStr = CCommon::FormatAmount(displayAmount);
	}

	auto calcVolumeMA = [&](int globalIndex, int period) -> double {
		if (!ctx.fullTimeline || globalIndex < period - 1 || globalIndex >= static_cast<int>(ctx.fullTimeline->size()))
			return 0.0;
		double sum = 0.0;
		for (int i = globalIndex - period + 1; i <= globalIndex; i++)
			sum += static_cast<double>((*ctx.fullTimeline)[i].volume);
		return sum / period;
		};

	auto formatVolumeMA = [](double volume) -> CString {
		return CCommon::FormatVolumeInt(volume / 100.0);
		};

	const int globalVolIdx = ctx.startIndex + displayVolIdx;
	double volMa5 = calcVolumeMA(globalVolIdx, 5);
	double prevVolMa5 = calcVolumeMA(globalVolIdx - 1, 5);
	double volMa10 = calcVolumeMA(globalVolIdx, 10);
	double prevVolMa10 = calcVolumeMA(globalVolIdx - 1, 10);

	int xPos = g_data.RDPI(4);
	int centerY = areaTop + titleH / 2;
	const COLORREF hoverBgColor = RGB(200, 220, 255);

	auto drawLabelValueHL = [&](const CString& labelText, const CString& valStr, COLORREF labelColor, COLORREF valueColor, bool highlight) {
		memDC.SetTextColor(labelColor);
		CSize ls = memDC.GetTextExtent(labelText);
		memDC.TextOut(xPos, centerY - ls.cy / 2, labelText);
		xPos += ls.cx;
		memDC.SetTextColor(valueColor);
		CSize vs = memDC.GetTextExtent(valStr);
		if (highlight)
		{
			CRect hlRect(xPos, areaTop + 1, xPos + vs.cx, areaTop + titleH - 1);
			memDC.FillSolidRect(hlRect, hoverBgColor);
		}
		memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
		xPos += vs.cx + g_data.RDPI(4);
		};

	auto drawVolumeMA = [&](const CString& labelText, double maValue, double prevMaValue, COLORREF maColor) {
		if (maValue <= 0)
			return;
		CString valueText = formatVolumeMA(maValue);
		memDC.SetTextColor(maColor);
		CString text = labelText + valueText;
		CSize textSize = memDC.GetTextExtent(text);
		memDC.TextOut(xPos, centerY - textSize.cy / 2, text);
		xPos += textSize.cx;
		if (prevMaValue > 0 && maValue != prevMaValue)
		{
			CString arrowText = maValue > prevMaValue ? _T("↑") : _T("↓");
			memDC.SetTextColor(maValue > prevMaValue ? COLOR_RED_UP : COLOR_GREEN_DOWN);
			CSize arrowSize = memDC.GetTextExtent(arrowText);
			memDC.TextOut(xPos, centerY - arrowSize.cy / 2, arrowText);
			xPos += arrowSize.cx;
		}
		xPos += g_data.RDPI(4);
		};

	memDC.SetTextColor(COLOR_GRAY_TEXT);
	if (!perVolStr.IsEmpty())
		drawLabelValueHL(_T("分量:"), perVolStr, COLOR_GRAY_TEXT, COLOR_GRAY_TEXT, isVolHovering);
	if (!perAmtStr.IsEmpty())
		drawLabelValueHL(_T("分额:"), perAmtStr, COLOR_GRAY_TEXT, COLOR_GRAY_TEXT, isVolHovering);
	if (hover.viewMode == UI_VIEW_MIN5_KLINE)
	{
		drawVolumeMA(_T("MA5:"), volMa5, prevVolMa5, RGB(0, 0, 230));
		drawVolumeMA(_T("MA10:"), volMa10, prevVolMa10, RGB(0, 166, 235));
	}

	memDC.SetBkMode(oldBkMode);

	TimelineDrawContext tmpCtx = ctx;
	tmpCtx.volumeChartTop = areaTop + titleH;
	tmpCtx.volumeChartHeight = areaHeight - titleH;

	// 先绘制背景高亮，确保数据不被覆盖
	CTimelineChart::DrawTimelineBackgroundHighlightsForArea(memDC, tmpCtx, tmpCtx.volumeChartTop, tmpCtx.volumeChartHeight, hover.viewMode);

	DrawVolumeChart(memDC, 0, tmpCtx.volumeChartTop, ctx.chartWidth, tmpCtx.volumeChartHeight, *ctx.timelinePoint, &ctx.realtimeData, 0, -1, ctx.xAxisPoints, hover.isHoveringVolume, hover.hoveredBarIndex);

	if (hover.viewMode == UI_VIEW_MIN5_KLINE && ctx.fullTimeline && !ctx.fullTimeline->empty() && ctx.timelinePoint && !ctx.timelinePoint->empty())
	{
		const auto& fullData = *ctx.fullTimeline;
		const auto& visibleData = *ctx.timelinePoint;
		const int totalPoints = static_cast<int>(visibleData.size());
		const int fullCount = static_cast<int>(fullData.size());
		const int startIndex = ctx.startIndex;
		int endIndex = min(fullCount, startIndex + totalPoints);

		STOCK::Volume maxVolume = 0;
		for (int i = startIndex; i < endIndex; i++)
			maxVolume = max(maxVolume, fullData[i].volume);

		auto calcVolMA = [&](int globalIndex, int period) -> double {
			if (globalIndex < period - 1)
				return 0.0;
			double sum = 0.0;
			for (int i = globalIndex - period + 1; i <= globalIndex; i++)
				sum += static_cast<double>(fullData[i].volume);
			return sum / period;
			};

		std::vector<double> ma5(totalPoints, 0.0);
		std::vector<double> ma10(totalPoints, 0.0);
		for (int i = 0; i < totalPoints; i++)
		{
			int globalIndex = startIndex + i;
			if (globalIndex >= 0 && globalIndex < fullCount)
			{
				ma5[i] = calcVolMA(globalIndex, 5);
				ma10[i] = calcVolMA(globalIndex, 10);
			}
		}

		if (maxVolume > 0)
		{
			auto volumeToY = [&](double volume) -> int {
				int py = tmpCtx.volumeChartTop + tmpCtx.volumeChartHeight - static_cast<int>(volume / static_cast<double>(maxVolume) * tmpCtx.volumeChartHeight);
				return max(tmpCtx.volumeChartTop, min(py, tmpCtx.volumeChartTop + tmpCtx.volumeChartHeight));
				};

			auto drawVolumeMALine = [&](const std::vector<double>& values, COLORREF color) {
				CPen pen(PS_SOLID, 1, color);
				CPen* pOldPen = memDC.SelectObject(&pen);
				bool first = true;
				for (int i = 0; i < totalPoints; i++)
				{
					if (values[i] <= 0) { first = true; continue; }
					int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
					int pointY = volumeToY(values[i]);
					if (first) { memDC.MoveTo(pointX, pointY); first = false; }
					else memDC.LineTo(pointX, pointY);
				}
				memDC.SelectObject(pOldPen);
				};

			drawVolumeMALine(ma5, RGB(0, 0, 230));
			drawVolumeMALine(ma10, RGB(0, 166, 235));
		}
	}

	CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
	CPen* pOldVolPen = memDC.SelectObject(&pGrid);
	int volumeY = tmpCtx.volumeChartTop;
	memDC.MoveTo(0, volumeY);
	memDC.LineTo(ctx.chartWidth, volumeY);

	if (!timelinePoint.empty())
	{
		STOCK::Volume maxVolume = 0;
		for (const auto& item : timelinePoint)
		{
			if (item.volume > maxVolume)
				maxVolume = item.volume;
		}
		if (maxVolume > 0)
		{
			CPen dotPen(PS_DOT, 1, COLOR_GRAY_MIDDLE);
			memDC.SelectObject(&dotPen);
			memDC.SetTextColor(COLOR_GRAY_TEXT);
			for (int i = 1; i <= 2; i++)
			{
				int yPos = volumeY + tmpCtx.volumeChartHeight * i / 3;
				memDC.MoveTo(0, yPos);
				memDC.LineTo(ctx.chartWidth, yPos);
				STOCK::Volume volAtLine = maxVolume * (3 - i) / 3;
				STOCK::Volume volInLots = volAtLine / 100;
				CString volLabel = CCommon::FormatVolumeInt(volInLots);
				CSize labelSize = memDC.GetTextExtent(volLabel);
				memDC.TextOut(-labelSize.cx - g_data.RDPI(3), yPos - labelSize.cy / 2, volLabel);
			}
			memDC.SelectObject(&pGrid);
		}
	}

	if (ctx.timelinePoint && !ctx.timelinePoint->empty())
	{
		const int totalPts = static_cast<int>(ctx.timelinePoint->size());
		const int numVLines = 6;
		for (int i = 0; i <= numVLines; i++)
		{
			int xPos2 = ctx.chartWidth * i / numVLines;
			memDC.MoveTo(xPos2, volumeY);
			memDC.LineTo(xPos2, volumeY + tmpCtx.volumeChartHeight);
		}
	}
	memDC.SelectObject(pOldVolPen);

	if (drawTimeLabels && !timelinePoint.empty())
	{
		const int totalPts = static_cast<int>(timelinePoint.size());
		const int numVLines = 6;
		memDC.SetTextColor(COLOR_GRAY_TEXT);
		for (int i = 0; i <= numVLines; i++)
		{
			int idx = totalPts * i / numVLines;
			if (idx >= totalPts) idx = totalPts - 1;
			int xPos2 = ctx.chartWidth * i / numVLines;
			CString timeLabel(timelinePoint[idx].time.c_str());
			if (timeLabel.GetLength() >= 5) timeLabel = timeLabel.Left(5);
			CSize labelSize = memDC.GetTextExtent(timeLabel);
			int labelX = max(0, min(xPos2 - labelSize.cx / 2, ctx.chartWidth - labelSize.cx));
			memDC.TextOut(labelX, tmpCtx.volumeChartTop + tmpCtx.volumeChartHeight + g_data.RDPI(2), timeLabel);
		}
	}
}

// ========== DrawMacdChartArea ==========

void CIndicatorChart::DrawMacdChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight, const CString& macdTitleTip, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
	int titleH = g_data.RDPI(16);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	CRect macdTitleRect(0, areaTop, ctx.chartWidth, areaTop + titleH);
	memDC.FillSolidRect(macdTitleRect, RGB(245, 245, 245));

	TimelineDrawContext tmpCtx = ctx;
	tmpCtx.macdChartTop = areaTop + titleH;
	tmpCtx.macdChartHeight = areaHeight - titleH;

	// 5分钟K线用7,15,5参数，分时(1分钟)用6,12,4参数，30分钟和日K用默认12,26,9
	int shortP = 12, longP = 26, signalP = 9;
	if (hover.viewMode == UI_VIEW_MIN30_KLINE)
	{
		shortP = 9; longP = 19; signalP = 7;
	}
	else if (hover.viewMode == UI_VIEW_MIN5_KLINE)
	{
		shortP = 7; longP = 15; signalP = 5;
	}

	else if (hover.viewMode < UI_VIEW_MIN5_KLINE)
	{
		shortP = 6; longP = 12; signalP = 4;
	}
	auto macdData = CStockIndicator::CalculateTimelineMACD(fullData, shortP, longP, signalP);

	// 标题栏：DIF标签红色/DEA标签蓝色，数值正数红色/负数绿色
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;
		// 分别绘制标签和数值，标签和数值可以不同颜色
		auto drawLabel = [&](const CString& label, COLORREF labelColor) {
			memDC.SetTextColor(labelColor);
			CSize ts = memDC.GetTextExtent(label);
			memDC.TextOut(xPos, centerY - ts.cy / 2, label);
			xPos += ts.cx;
			};
		auto drawValue = [&](const CString& value, double val) {
			COLORREF valColor = (val >= 0) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
			memDC.SetTextColor(valColor);
			CSize ts = memDC.GetTextExtent(value);
			memDC.TextOut(xPos, centerY - ts.cy / 2, value);
			xPos += ts.cx + g_data.RDPI(4);
			};
		auto formatMACDValue = [](double val) -> CString {
			CString s;
			double absVal = std::abs(val);
			if (absVal < 0.001 && absVal > 0)
				s.Format(_T("%.5f"), val);
			else if (absVal < 0.01)
				s.Format(_T("%.4f"), val);
			else
				s.Format(_T("%.3f"), val);
			return s;
			};

		// 获取当前显示的DIF/DEA值（悬停时从tip解析，非悬停时取最新数据）
		double difVal = 0, deaVal = 0;
		if (!hover.timelineMacdTitleTip.IsEmpty())
		{
			// 从 "DIF:xxx DEA:xxx" 解析数值
			CString tip = hover.timelineMacdTitleTip;
			int difPos = tip.Find(_T("DIF:"));
			int deaPos = tip.Find(_T("DEA:"));
			if (difPos >= 0 && deaPos >= 0)
			{
				CString difStr = tip.Mid(difPos + 4, deaPos - difPos - 4);
				difStr.TrimRight();
				CString deaStr = tip.Mid(deaPos + 4);
				deaStr.TrimRight();
				difVal = _ttof(difStr);
				deaVal = _ttof(deaStr);
			}
		}
		else
		{
			for (int i = static_cast<int>(macdData.size()) - 1; i >= 0; i--)
			{
				if (macdData[i].valid)
				{
					difVal = macdData[i].dif;
					deaVal = macdData[i].dea;
					break;
				}
			}
		}

		drawLabel(_T("DIF:"), COLOR_DARK_ORANGE);
		drawValue(formatMACDValue(difVal), difVal);
		drawLabel(_T("DEA:"), COLOR_BLUE_AVG1);
		drawValue(formatMACDValue(deaVal), deaVal);

		// DIF趋势箭头：根据线性回归斜率判断方向和强弱
		{
			// 悬停时取鼠标位置的数据计算趋势，非悬停时取最新数据
			int trendEndIdx = -1;
			if (!hover.timelineMacdTitleTip.IsEmpty() && hover.hoveredBarIndex >= 0)
			{
				// 悬停位置对应的全局索引
				trendEndIdx = ctx.startIndex + hover.hoveredBarIndex;
				if (trendEndIdx >= static_cast<int>(macdData.size()))
					trendEndIdx = static_cast<int>(macdData.size()) - 1;
			}
			else
			{
				// 取最后一个有效数据
				for (int i = static_cast<int>(macdData.size()) - 1; i >= 0; i--)
				{
					if (macdData[i].valid) { trendEndIdx = i; break; }
				}
			}

			if (trendEndIdx >= 0)
			{
				// 截取到悬停位置为止的子序列计算趋势
				std::vector<MACDData> subMacd(macdData.begin(), macdData.begin() + trendEndIdx + 1);
				// 1分钟用6根，5分钟用8根，30分钟和日K用10根
				int lookback = 10;
				if (hover.viewMode == UI_VIEW_MIN5_KLINE) lookback = 8;
				else if (hover.viewMode == UI_VIEW_TIMELINE) lookback = 6;
				auto trend = CStockIndicator::CalcDIFTrend(subMacd, lookback);
				if (trend.valid && trend.slope != 0.0)
				{
					bool isUp = trend.slope > 0;
					double refDif = std::abs(macdData[trendEndIdx].dif);
					double base = (refDif > 0.001) ? refDif : 0.001;
					double absSlope = std::abs(trend.slope);
					int arrowCount = 1;
					if (absSlope >= base * 0.3) arrowCount = 3;
					else if (absSlope >= base * 0.15) arrowCount = 2;

					COLORREF arrowColor = (macdData[trendEndIdx].dea >= 0) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
					CString arrows;
					for (int a = 0; a < arrowCount; a++)
						arrows += isUp ? _T("\u2191") : _T("\u2193");
					memDC.SetTextColor(arrowColor);
					CSize arrowSize = memDC.GetTextExtent(arrows);
					memDC.TextOut(xPos, centerY - arrowSize.cy / 2, arrows);
				}
			}
		}
	}

	memDC.SetBkMode(oldBkMode);

	// 先绘制背景高亮，确保数据不被覆盖
	CTimelineChart::DrawTimelineBackgroundHighlightsForArea(memDC, tmpCtx, tmpCtx.macdChartTop, tmpCtx.macdChartHeight, hover.viewMode);

	DrawMACDChart(memDC, 0, tmpCtx.macdChartTop, ctx.chartWidth, tmpCtx.macdChartHeight, timelinePoint, macdData, ctx.startIndex, -1, ctx.xAxisPoints);

	CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&pGrid);
	int macdY = tmpCtx.macdChartTop;
	memDC.MoveTo(0, macdY);
	memDC.LineTo(ctx.chartWidth, macdY);
	memDC.MoveTo(0, macdY + tmpCtx.macdChartHeight);
	memDC.LineTo(ctx.chartWidth, macdY + tmpCtx.macdChartHeight);

	const int totalPts = static_cast<int>(timelinePoint.size());
	const int numVLines = 6;
	if (totalPts > 0)
	{
		for (int i = 0; i <= numVLines; i++)
		{
			int xPos = ctx.chartWidth * i / numVLines;
			memDC.MoveTo(xPos, macdY);
			memDC.LineTo(xPos, macdY + tmpCtx.macdChartHeight);
		}
	}
	memDC.SelectObject(pOldPen);
}

// ========== DrawIndicatorChartArea ==========

void CIndicatorChart::DrawIndicatorChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight, bool drawTimeLabels, TimelineIndicator indicator, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	int titleH = g_data.RDPI(16);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);

	CRect indicatorTitleRect(0, areaTop, ctx.chartWidth, areaTop + titleH);
	memDC.FillSolidRect(indicatorTitleRect, RGB(245, 245, 245));

	if (indicator == TimelineIndicator::KDJ)
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;
		auto drawKDJLabel = [&](const CString& label, COLORREF labelColor) {
			memDC.SetTextColor(labelColor);
			CSize ts = memDC.GetTextExtent(label);
			memDC.TextOut(xPos, centerY - ts.cy / 2, label);
			xPos += ts.cx;
			};
		auto drawKDJValue = [&](const CString& value, double val) {
			// KDJ值：>50红色，<50绿色，=50灰色
			COLORREF valColor = (val > 50) ? COLOR_RED_UP : ((val < 50) ? COLOR_GREEN_DOWN : COLOR_GRAY_TEXT);
			memDC.SetTextColor(valColor);
			CSize ts = memDC.GetTextExtent(value);
			memDC.TextOut(xPos, centerY - ts.cy / 2, value);
			xPos += ts.cx + g_data.RDPI(4);
			};

		// 获取当前K/D/J值
		double kVal = 0, dVal = 0, jVal = 0;
		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
		// 5分钟K线用8,3,3参数，分时(1分钟)用7,3,3参数，30分钟和日K用默认9,3,3
		int kdjN = 9, kdjM1 = 3, kdjM2 = 3;
		if (hover.viewMode == UI_VIEW_MIN5_KLINE)
		{
			kdjN = 8; kdjM1 = 3; kdjM2 = 3;
		}
		else if (hover.viewMode == UI_VIEW_TIMELINE)
		{
			kdjN = 7; kdjM1 = 3; kdjM2 = 3;
		}
		auto kdjData = CStockIndicator::CalculateTimelineKDJ(fullData, kdjN, kdjM1, kdjM2);

		if (!hover.timelineKdjTitleTip.IsEmpty())
		{
			// 从 "K:xx.x D:xx.x J:xx.x" 解析数值
			CString tip = hover.timelineKdjTitleTip;
			int kPos = tip.Find(_T("K:"));
			int dPos = tip.Find(_T("D:"));
			int jPos = tip.Find(_T("J:"));
			if (kPos >= 0 && dPos >= 0 && jPos >= 0)
			{
				CString kStr = tip.Mid(kPos + 2, dPos - kPos - 2); kStr.TrimRight();
				CString dStr = tip.Mid(dPos + 2, jPos - dPos - 2); dStr.TrimRight();
				CString jStr = tip.Mid(jPos + 2); jStr.TrimRight();
				kVal = _ttof(kStr);
				dVal = _ttof(dStr);
				jVal = _ttof(jStr);
			}
		}
		else
		{
			for (int i = static_cast<int>(kdjData.size()) - 1; i >= 0; i--)
			{
				if (kdjData[i].valid)
				{
					kVal = kdjData[i].k;
					dVal = kdjData[i].d;
					jVal = kdjData[i].j;
					break;
				}
			}
		}

		drawKDJLabel(_T("K:"), COLOR_RED_UP);
		CString kStr; kStr.Format(_T("%.1f"), kVal);
		drawKDJValue(kStr, kVal);
		drawKDJLabel(_T("D:"), RGB(0, 68, 204));
		CString dStr; dStr.Format(_T("%.1f"), dVal);
		drawKDJValue(dStr, dVal);
		drawKDJLabel(_T("J:"), RGB(0, 136, 34));
		CString jStr; jStr.Format(_T("%.1f"), jVal);
		drawKDJValue(jStr, jVal);

		// J值趋势箭头
		{
			int trendEndIdx = -1;
			if (!hover.timelineKdjTitleTip.IsEmpty() && hover.hoveredBarIndex >= 0)
			{
				trendEndIdx = ctx.startIndex + hover.hoveredBarIndex;
				if (trendEndIdx >= static_cast<int>(kdjData.size()))
					trendEndIdx = static_cast<int>(kdjData.size()) - 1;
			}
			else
			{
				for (int i = static_cast<int>(kdjData.size()) - 1; i >= 0; i--)
				{
					if (kdjData[i].valid) { trendEndIdx = i; break; }
				}
			}

			if (trendEndIdx >= 0)
			{
				std::vector<KDJData> subKdj(kdjData.begin(), kdjData.begin() + trendEndIdx + 1);
				// 1分钟用6根，5分钟用8根，30分钟和日K用10根
				int lookback = 10;
				if (hover.viewMode == UI_VIEW_MIN5_KLINE) lookback = 8;
				else if (hover.viewMode == UI_VIEW_TIMELINE) lookback = 6;
				auto trend = CStockIndicator::CalcKDJTrend(subKdj, lookback);
				if (trend.valid && trend.slope != 0.0)
				{
					bool isUp = trend.slope > 0;
					// K-D差值范围通常-20~20，阈值：1个箭头=slope>=0.5, 2个>=1.5, 3个>=3
					double absSlope = std::abs(trend.slope);
					int arrowCount = 1;
					if (absSlope >= 3) arrowCount = 3;
					else if (absSlope >= 1.5) arrowCount = 2;

					// K>=D用红色，K<D用绿色
					COLORREF arrowColor = (kdjData[trendEndIdx].k >= kdjData[trendEndIdx].d) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
					CString arrows;
					for (int a = 0; a < arrowCount; a++)
						arrows += isUp ? _T("\u2191") : _T("\u2193");
					memDC.SetTextColor(arrowColor);
					CSize arrowSize = memDC.GetTextExtent(arrows);
					memDC.TextOut(xPos, centerY - arrowSize.cy / 2, arrows);
				}
			}
		}
	}
	else if (indicator == TimelineIndicator::WR)
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;
		auto drawWRLabel = [&](const CString& label, const CString& value, COLORREF color) {
			memDC.SetTextColor(color);
			CString text = label + value;
			CSize ts = memDC.GetTextExtent(text);
			memDC.TextOut(xPos, centerY - ts.cy / 2, text);
			xPos += ts.cx + g_data.RDPI(4);
			};
		if (!hover.timelineWrTitleTip.IsEmpty())
		{
			CString tip = hover.timelineWrTitleTip;
			int pos = 0;
			CString token;
			COLORREF colors[] = { RGB(0, 68, 204), RGB(204, 34, 34) };
			int colorIdx = 0;
			while ((token = tip.Tokenize(_T(" "), pos)) != _T(""))
			{
				drawWRLabel(_T(""), token, colors[min(colorIdx, 1)]);
				colorIdx++;
			}
		}
		else
		{
			drawWRLabel(_T("WR6:"), _T(""), RGB(0, 68, 204));
			drawWRLabel(_T("WR14:"), _T(""), RGB(204, 34, 34));
		}
	}
	else if (indicator == TimelineIndicator::RSI)
	{
		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;
		auto drawRSILabel = [&](const CString& label, const CString& value, COLORREF color) {
			memDC.SetTextColor(color);
			CString text = label + value;
			CSize ts = memDC.GetTextExtent(text);
			memDC.TextOut(xPos, centerY - ts.cy / 2, text);
			xPos += ts.cx + g_data.RDPI(4);
			};
		if (!hover.timelineRsiTitleTip.IsEmpty())
		{
			CString tip = hover.timelineRsiTitleTip;
			int pos = 0;
			CString token;
			COLORREF colors[] = { RGB(0, 68, 204), RGB(204, 34, 34) };
			int colorIdx = 0;
			while ((token = tip.Tokenize(_T(" "), pos)) != _T(""))
			{
				drawRSILabel(_T(""), token, colors[min(colorIdx, 1)]);
				colorIdx++;
			}
		}
		else
		{
			drawRSILabel(_T("RSI6:"), _T(""), RGB(0, 68, 204));
			drawRSILabel(_T("RSI14:"), _T(""), RGB(204, 34, 34));
		}
	}
	else
	{
		// 成交量标题栏（CJL模式）
		CString perVolStr, perAmtStr;
		bool isVolHovering = !hover.timelineVolumeTitleTip.IsEmpty();
		int displayVolIdx = isVolHovering ? hover.hoveredBarIndex : static_cast<int>(timelinePoint.size()) - 1;
		displayVolIdx = max(0, min(displayVolIdx, static_cast<int>(timelinePoint.size()) - 1));
		if (displayVolIdx >= 0 && displayVolIdx < static_cast<int>(timelinePoint.size()))
		{
			const auto& displayPt = timelinePoint[displayVolIdx];
			perVolStr = CCommon::FormatVolumeInt(displayPt.volume / 100);
			double displayAmount = static_cast<double>(displayPt.volume) * displayPt.price;
			perAmtStr = CCommon::FormatAmount(displayAmount);
		}

		auto calcVolumeMA = [&](int globalIndex, int period) -> double {
			if (!ctx.fullTimeline || globalIndex < period - 1 || globalIndex >= static_cast<int>(ctx.fullTimeline->size()))
				return 0.0;
			double sum = 0.0;
			for (int i = globalIndex - period + 1; i <= globalIndex; i++)
				sum += static_cast<double>((*ctx.fullTimeline)[i].volume);
			return sum / period;
			};

		auto formatVolumeMA = [](double volume) -> CString {
			return CCommon::FormatVolumeInt(volume / 100.0);
			};

		const int globalVolIdx = ctx.startIndex + displayVolIdx;
		double volMa5 = calcVolumeMA(globalVolIdx, 5);
		double prevVolMa5 = calcVolumeMA(globalVolIdx - 1, 5);
		double volMa10 = calcVolumeMA(globalVolIdx, 10);
		double prevVolMa10 = calcVolumeMA(globalVolIdx - 1, 10);

		int xPos = g_data.RDPI(4);
		int centerY = areaTop + titleH / 2;
		const COLORREF hoverBgColor = RGB(200, 220, 255);

		auto drawLabelValueHL = [&](const CString& labelText, const CString& valStr, COLORREF labelColor, COLORREF valueColor, bool highlight) {
			memDC.SetTextColor(labelColor);
			CSize ls = memDC.GetTextExtent(labelText);
			memDC.TextOut(xPos, centerY - ls.cy / 2, labelText);
			xPos += ls.cx;
			memDC.SetTextColor(valueColor);
			CSize vs = memDC.GetTextExtent(valStr);
			if (highlight)
			{
				CRect hlRect(xPos, areaTop + 1, xPos + vs.cx, areaTop + titleH - 1);
				memDC.FillSolidRect(hlRect, hoverBgColor);
			}
			memDC.TextOut(xPos, centerY - vs.cy / 2, valStr);
			xPos += vs.cx + g_data.RDPI(4);
			};

		auto drawVolumeMA = [&](const CString& labelText, double maValue, double prevMaValue, COLORREF maColor) {
			if (maValue <= 0)
				return;
			CString valueText = formatVolumeMA(maValue);
			memDC.SetTextColor(maColor);
			CString text = labelText + valueText;
			CSize textSize = memDC.GetTextExtent(text);
			memDC.TextOut(xPos, centerY - textSize.cy / 2, text);
			xPos += textSize.cx;
			if (prevMaValue > 0 && maValue != prevMaValue)
			{
				CString arrowText = maValue > prevMaValue ? _T("↑") : _T("↓");
				memDC.SetTextColor(maValue > prevMaValue ? COLOR_RED_UP : COLOR_GREEN_DOWN);
				CSize arrowSize = memDC.GetTextExtent(arrowText);
				memDC.TextOut(xPos, centerY - arrowSize.cy / 2, arrowText);
				xPos += arrowSize.cx;
			}
			xPos += g_data.RDPI(4);
			};

		memDC.SetTextColor(COLOR_GRAY_TEXT);
		if (!perVolStr.IsEmpty())
			drawLabelValueHL(_T("分量:"), perVolStr, COLOR_GRAY_TEXT, COLOR_GRAY_TEXT, isVolHovering);
		if (!perAmtStr.IsEmpty())
			drawLabelValueHL(_T("分额:"), perAmtStr, COLOR_GRAY_TEXT, COLOR_GRAY_TEXT, isVolHovering);
		if (hover.viewMode == UI_VIEW_MIN5_KLINE)
		{
			drawVolumeMA(_T("MA5:"), volMa5, prevVolMa5, RGB(0, 0, 230));
			drawVolumeMA(_T("MA10:"), volMa10, prevVolMa10, RGB(0, 166, 235));
		}
	}

	memDC.SetBkMode(oldBkMode);

	TimelineDrawContext tmpCtx = ctx;

	if (indicator == TimelineIndicator::KDJ)
	{
		tmpCtx.macdChartTop = areaTop + titleH;
		tmpCtx.macdChartHeight = areaHeight - titleH;
		CTimelineChart::DrawTimelineBackgroundHighlightsForArea(memDC, tmpCtx, tmpCtx.macdChartTop, tmpCtx.macdChartHeight, hover.viewMode);
		DrawTimelineKDJSection(memDC, tmpCtx, hover);
	}
	else if (indicator == TimelineIndicator::WR)
	{
		tmpCtx.macdChartTop = areaTop + titleH;
		tmpCtx.macdChartHeight = areaHeight - titleH;
		CTimelineChart::DrawTimelineBackgroundHighlightsForArea(memDC, tmpCtx, tmpCtx.macdChartTop, tmpCtx.macdChartHeight, hover.viewMode);
		DrawTimelineWRSection(memDC, tmpCtx, hover);
	}
	else if (indicator == TimelineIndicator::RSI)
	{
		tmpCtx.macdChartTop = areaTop + titleH;
		tmpCtx.macdChartHeight = areaHeight - titleH;
		CTimelineChart::DrawTimelineBackgroundHighlightsForArea(memDC, tmpCtx, tmpCtx.macdChartTop, tmpCtx.macdChartHeight, hover.viewMode);
		DrawTimelineRSISection(memDC, tmpCtx, hover);
	}
	else
	{
		// 成交量模式（MACD枚举值现在表示成交量）
		tmpCtx.volumeChartTop = areaTop + titleH;
		tmpCtx.volumeChartHeight = areaHeight - titleH;
		CTimelineChart::DrawTimelineBackgroundHighlightsForArea(memDC, tmpCtx, tmpCtx.volumeChartTop, tmpCtx.volumeChartHeight, hover.viewMode);
		DrawVolumeChart(memDC, 0, tmpCtx.volumeChartTop, ctx.chartWidth, tmpCtx.volumeChartHeight, *ctx.timelinePoint, &ctx.realtimeData, 0, -1, ctx.xAxisPoints, hover.isHoveringVolume, hover.hoveredBarIndex);

		if (hover.viewMode == UI_VIEW_MIN5_KLINE && ctx.fullTimeline && !ctx.fullTimeline->empty() && ctx.timelinePoint && !ctx.timelinePoint->empty())
		{
			const auto& fullData = *ctx.fullTimeline;
			const auto& visibleData = *ctx.timelinePoint;
			const int totalPoints = static_cast<int>(visibleData.size());
			const int fullCount = static_cast<int>(fullData.size());
			const int startIndex = ctx.startIndex;
			int endIndex = min(fullCount, startIndex + totalPoints);

			STOCK::Volume maxVolume = 0;
			for (int i = startIndex; i < endIndex; i++)
				maxVolume = max(maxVolume, fullData[i].volume);

			auto calcVolumeMA2 = [&](int globalIndex, int period) -> double {
				if (globalIndex < period - 1)
					return 0.0;
				double sum = 0.0;
				for (int i = globalIndex - period + 1; i <= globalIndex; i++)
					sum += static_cast<double>(fullData[i].volume);
				return sum / period;
				};

			std::vector<double> ma5(totalPoints, 0.0);
			std::vector<double> ma10(totalPoints, 0.0);
			for (int i = 0; i < totalPoints; i++)
			{
				int globalIndex = startIndex + i;
				if (globalIndex >= 0 && globalIndex < fullCount)
				{
					ma5[i] = calcVolumeMA2(globalIndex, 5);
					ma10[i] = calcVolumeMA2(globalIndex, 10);
				}
			}

			if (maxVolume > 0)
			{
				auto volumeToY = [&](double volume) -> int {
					int py = tmpCtx.volumeChartTop + tmpCtx.volumeChartHeight - static_cast<int>(volume / static_cast<double>(maxVolume) * tmpCtx.volumeChartHeight);
					return max(tmpCtx.volumeChartTop, min(py, tmpCtx.volumeChartTop + tmpCtx.volumeChartHeight));
					};

				auto drawVolumeMALine = [&](const std::vector<double>& values, COLORREF color) {
					CPen pen(PS_SOLID, 1, color);
					CPen* pOldPen = memDC.SelectObject(&pen);
					bool first = true;
					for (int i = 0; i < totalPoints; i++)
					{
						if (values[i] <= 0) { first = true; continue; }
						int pointX = static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) * i) + static_cast<int>(ctx.chartWidth / static_cast<float>(totalPoints) / 2);
						int pointY = volumeToY(values[i]);
						if (first) { memDC.MoveTo(pointX, pointY); first = false; }
						else memDC.LineTo(pointX, pointY);
					}
					memDC.SelectObject(pOldPen);
					};

				drawVolumeMALine(ma5, RGB(0, 0, 230));
				drawVolumeMALine(ma10, RGB(0, 166, 235));
			}
		}

		// 成交量网格线
		CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
		CPen* pOldVolPen = memDC.SelectObject(&pGrid);
		int volumeY = tmpCtx.volumeChartTop;
		memDC.MoveTo(0, volumeY);
		memDC.LineTo(ctx.chartWidth, volumeY);

		if (!timelinePoint.empty())
		{
			STOCK::Volume maxVolume = 0;
			for (const auto& item : timelinePoint)
			{
				if (item.volume > maxVolume)
					maxVolume = item.volume;
			}
			if (maxVolume > 0)
			{
				CPen dotPen(PS_DOT, 1, COLOR_GRAY_MIDDLE);
				memDC.SelectObject(&dotPen);
				memDC.SetTextColor(COLOR_GRAY_TEXT);
				for (int i = 1; i <= 2; i++)
				{
					int yPos = volumeY + tmpCtx.volumeChartHeight * i / 3;
					memDC.MoveTo(0, yPos);
					memDC.LineTo(ctx.chartWidth, yPos);
					STOCK::Volume volAtLine = maxVolume * (3 - i) / 3;
					STOCK::Volume volInLots = volAtLine / 100;
					CString volLabel = CCommon::FormatVolumeInt(volInLots);
					CSize labelSize = memDC.GetTextExtent(volLabel);
					memDC.TextOut(-labelSize.cx - g_data.RDPI(3), yPos - labelSize.cy / 2, volLabel);
				}
				memDC.SelectObject(&pGrid);
			}
		}

		// X轴时间竖线
		if (ctx.timelinePoint && !ctx.timelinePoint->empty())
		{
			const int totalPts = static_cast<int>(ctx.timelinePoint->size());
			const int numVLines = 6;
			for (int i = 0; i <= numVLines; i++)
			{
				int xPos = ctx.chartWidth * i / numVLines;
				memDC.MoveTo(xPos, volumeY);
				memDC.LineTo(xPos, volumeY + tmpCtx.volumeChartHeight);
			}
		}
		memDC.SelectObject(pOldVolPen);
	}

	// 如果 drawTimeLabels 为 true，在区域下方绘制X轴时间标签
	if (drawTimeLabels && !timelinePoint.empty())
	{
		const int totalPts = static_cast<int>(timelinePoint.size());
		const int numVLines = 6;
		memDC.SetTextColor(COLOR_GRAY_TEXT);
		int chartBottom = areaTop + areaHeight;
		for (int i = 0; i <= numVLines; i++)
		{
			int idx = totalPts * i / numVLines;
			if (idx >= totalPts) idx = totalPts - 1;
			int xPos = ctx.chartWidth * i / numVLines;
			CString timeLabel(timelinePoint[idx].time.c_str());
			if (timeLabel.GetLength() >= 5) timeLabel = timeLabel.Left(5);
			CSize labelSize = memDC.GetTextExtent(timeLabel);
			int labelX = max(0, min(xPos - labelSize.cx / 2, ctx.chartWidth - labelSize.cx));
			memDC.TextOut(labelX, chartBottom + g_data.RDPI(2), timeLabel);
		}
	}
}

// ========== DrawMACDChart（K线数据版本） ==========

void CIndicatorChart::DrawMACDChart(CDC& memDC, int x, int y, int width, int height,
	const std::vector<STOCK::KLinePoint>& klineData,
	const std::vector<MACDData>& macdData, int klinePeriodDays,
	int scrollOffset, int startIndex /* = 0 */, int visibleCount /* = -1 */)
{
	if (klineData.empty() || macdData.empty())
		return;

	int total = static_cast<int>(macdData.size());
	int endIdx = total;
	if (visibleCount > 0)
	{
		endIdx = (std::min)(total, startIndex + visibleCount);
		startIndex = (std::max)(0, (std::min)(startIndex, total - 1));
		if (endIdx <= startIndex) endIdx = startIndex + 1;
	}
	else
	{
		startIndex = 0;
	}

	double maxAbs = 0;
	for (const auto& m : macdData)
	{
		if (m.valid)
		{
			maxAbs = (std::max)(maxAbs, std::abs(m.dif));
			maxAbs = (std::max)(maxAbs, std::abs(m.dea));
			maxAbs = (std::max)(maxAbs, std::abs(m.bar));
		}
	}
	if (maxAbs == 0)
		return;

	int zeroY = y + height / 2;
	float unitY = (height / 2.0f - g_data.RDPI(2)) / static_cast<float>(maxAbs);

	CPen zeroPen(PS_DASHDOT, 1, COLOR_GRAY_MIDDLE);
	CPen* pOldPen = memDC.SelectObject(&zeroPen);
	memDC.MoveTo(x, zeroY);
	memDC.LineTo(x + width, zeroY);

	int displayCount = min(klinePeriodDays, static_cast<int>(klineData.size()));
	int maxVisibleKlines = min(displayCount, width / 3);
	int finalStartIndex = max(0, static_cast<int>(klineData.size()) - maxVisibleKlines - scrollOffset);
	finalStartIndex = max(0, min(finalStartIndex, static_cast<int>(klineData.size()) - maxVisibleKlines));

	int totalVisible = static_cast<int>(klineData.size()) - finalStartIndex;
	if (totalVisible <= 0) { memDC.SelectObject(pOldPen); return; }

	int slotWidth = width / totalVisible;
	int barWidth = max(2, static_cast<int>((slotWidth - 1) * 0.8));
	int halfSlot = slotWidth / 2;

	int drawStart = max(startIndex, finalStartIndex);
	int drawEnd = min(endIdx, static_cast<int>(macdData.size()));

	for (int i = drawStart; i < drawEnd; i++)
	{
		if (!macdData[i].valid)
			continue;
		int barX = x + (i - finalStartIndex) * slotWidth + halfSlot - barWidth / 2;
		double barVal = macdData[i].bar;
		int barHeight = static_cast<int>(std::abs(barVal) * unitY);
		barHeight = max(1, barHeight);
		COLORREF color = (barVal >= 0) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CBrush brush(color);
		int barY = (barVal >= 0) ? zeroY - barHeight : zeroY;
		memDC.FillRect(CRect(barX, barY, barX + barWidth, zeroY + (barVal >= 0 ? 0 : barHeight)), &brush);
	}

	// 绘制 DIF/DEA 双线（分段动态画笔：多头DIF粗橙实线+DEA浅灰细虚线，空头DEA粗蓝实线+DIF浅灰细虚线）
	{
		struct MacdPoint { int x; int yDif; int yDea; double dif; double dea; };
		std::vector<MacdPoint> pts;
		for (int i = drawStart; i < drawEnd; i++)
		{
			if (!macdData[i].valid) continue;
			int px = x + (i - finalStartIndex) * slotWidth + slotWidth / 2;
			int pyDif = zeroY - static_cast<int>(macdData[i].dif * unitY);
			int pyDea = zeroY - static_cast<int>(macdData[i].dea * unitY);
			pts.push_back({ px, pyDif, pyDea, macdData[i].dif, macdData[i].dea });
		}
		if (pts.size() >= 2)
		{
			enum EMacdState { STATE_LONG, STATE_SHORT };
			struct Seg { int from; int to; EMacdState state; };
			std::vector<Seg> segList;
			int segStart = 0;
			EMacdState curState = (pts[0].dif >= pts[0].dea) ? STATE_LONG : STATE_SHORT;
			for (size_t i = 1; i < pts.size(); i++)
			{
				EMacdState st = (pts[i].dif >= pts[i].dea) ? STATE_LONG : STATE_SHORT;
				if (st != curState)
				{
					segList.push_back({ segStart, static_cast<int>(i), curState });
					segStart = static_cast<int>(i);
					curState = st;
				}
			}
			segList.push_back({ segStart, static_cast<int>(pts.size()) - 1, curState });

			for (const auto& seg : segList)
			{
				std::vector<CPoint> ptsDif, ptsDea;
				ptsDif.reserve(seg.to - seg.from + 1);
				ptsDea.reserve(seg.to - seg.from + 1);
				for (int i = seg.from; i <= seg.to; i++)
				{
					ptsDif.push_back(CPoint(pts[i].x, pts[i].yDif));
					ptsDea.push_back(CPoint(pts[i].x, pts[i].yDea));
				}

				if (seg.state == STATE_LONG)
				{
					CPen penMain(PS_SOLID, 2, COLOR_DARK_ORANGE);
					CPen penSub(PS_SOLID, 1, COLOR_BLACK);
					memDC.SelectObject(&penMain);
					memDC.Polyline(&ptsDif[0], static_cast<int>(ptsDif.size()));
					memDC.SelectObject(&penSub);
					memDC.Polyline(&ptsDea[0], static_cast<int>(ptsDea.size()));
				}
				else
				{
					CPen penMain(PS_SOLID, 2, COLOR_BLUE_AVG1);
					CPen penSub(PS_SOLID, 1, COLOR_BLACK);
					memDC.SelectObject(&penMain);
					memDC.Polyline(&ptsDea[0], static_cast<int>(ptsDea.size()));
					memDC.SelectObject(&penSub);
					memDC.Polyline(&ptsDif[0], static_cast<int>(ptsDif.size()));
				}
			}
		}
		else if (pts.size() == 1)
		{
			CPen penMain(PS_SOLID, 2, (pts[0].dif >= pts[0].dea) ? COLOR_DARK_ORANGE : COLOR_BLUE_AVG1);
			memDC.SelectObject(&penMain);
			memDC.SetPixel(pts[0].x, pts[0].yDif, (pts[0].dif >= pts[0].dea) ? COLOR_DARK_ORANGE : COLOR_MACD_SUB);
			memDC.SetPixel(pts[0].x, pts[0].yDea, (pts[0].dif >= pts[0].dea) ? COLOR_MACD_SUB : COLOR_BLUE_AVG1);
		}
	}

	memDC.SelectObject(pOldPen);
}

// ========== DrawCrossArrow ==========

void CIndicatorChart::DrawCrossArrow(CDC& memDC, int markX, int markY, int dotRadius, bool isGolden)
{
	COLORREF arrowColor = isGolden ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	CBrush arrowBrush(arrowColor);
	CPen arrowPen(PS_SOLID, 1, arrowColor);
	CBrush* pOldBrush = memDC.SelectObject(&arrowBrush);
	CPen* pOldPen = memDC.SelectObject(&arrowPen);
	int aw = g_data.RDPI(3);    // 箭头头部半宽
	int ah = g_data.RDPI(5);    // 箭头头部高度
	int shaft = g_data.RDPI(7); // 箭头竖线长度
	int sw = g_data.RDPI(1);    // 箭头竖线半宽
	if (isGolden)
	{
		// ↑箭头：从圆点上边缘向上绘制（箭头头+竖线杆）
		int base = markY - dotRadius;
		CPoint pts[7] = {
			CPoint(markX, base - ah - shaft),
			CPoint(markX - aw, base - shaft),
			CPoint(markX - sw, base - shaft),
			CPoint(markX - sw, base),
			CPoint(markX + sw, base),
			CPoint(markX + sw, base - shaft),
			CPoint(markX + aw, base - shaft)
		};
		memDC.Polygon(pts, 7);
	}
	else
	{
		// ↓箭头：从圆点下边缘向下绘制（箭头头+竖线杆）
		int base = markY + dotRadius;
		CPoint pts[7] = {
			CPoint(markX, base + ah + shaft),
			CPoint(markX - aw, base + shaft),
			CPoint(markX - sw, base + shaft),
			CPoint(markX - sw, base),
			CPoint(markX + sw, base),
			CPoint(markX + sw, base + shaft),
			CPoint(markX + aw, base + shaft)
		};
		memDC.Polygon(pts, 7);
	}
	memDC.SelectObject(pOldBrush);
	memDC.SelectObject(pOldPen);
}

// ========== DrawSectionGridAndTimeLabels ==========

void CIndicatorChart::DrawSectionGridAndTimeLabels(CDC& memDC, const TimelineDrawContext& ctx, int chartTop, int chartHeight, bool drawTimeLabels)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	CPen pGrid(PS_SOLID, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&pGrid);
	memDC.MoveTo(0, chartTop);
	memDC.LineTo(ctx.chartWidth, chartTop);
	memDC.MoveTo(0, chartTop + chartHeight);
	memDC.LineTo(ctx.chartWidth, chartTop + chartHeight);
	const int totalPts = static_cast<int>(timelinePoint.size());
	const int numVLines = 6;
	if (totalPts > 0)
	{
		for (int i = 0; i <= numVLines; i++)
		{
			int xPos = ctx.chartWidth * i / numVLines;
			memDC.MoveTo(xPos, chartTop);
			memDC.LineTo(xPos, chartTop + chartHeight);
		}
	}
	memDC.SelectObject(pOldPen);
	if (drawTimeLabels && totalPts > 0)
	{
		memDC.SetTextColor(COLOR_GRAY_TEXT);
		for (int i = 0; i <= numVLines; i++)
		{
			int idx = totalPts * i / numVLines;
			if (idx >= totalPts) idx = totalPts - 1;
			int xPos = ctx.chartWidth * i / numVLines;
			CString timeLabel(timelinePoint[idx].time.c_str());
			if (timeLabel.GetLength() >= 5) timeLabel = timeLabel.Left(5);
			CSize labelSize = memDC.GetTextExtent(timeLabel);
			int labelX = max(0, min(xPos - labelSize.cx / 2, ctx.chartWidth - labelSize.cx));
			memDC.TextOut(labelX, chartTop + chartHeight + g_data.RDPI(2), timeLabel);
		}
	}
}

// ========== DrawTimelineKDJSection ==========

void CIndicatorChart::DrawTimelineKDJSection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;
	const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
	// 5分钟K线用8,3,3参数，分时(1分钟)用7,3,3参数，30分钟和日K用默认9,3,3
	int kdjN = 9, kdjM1 = 3, kdjM2 = 3;
	if (hover.viewMode == UI_VIEW_MIN5_KLINE)
	{
		kdjN = 8; kdjM1 = 3; kdjM2 = 3;
	}
	else if (hover.viewMode == UI_VIEW_TIMELINE)
	{
		kdjN = 7; kdjM1 = 3; kdjM2 = 3;
	}
	auto kdjData = CStockIndicator::CalculateTimelineKDJ(fullData, kdjN, kdjM1, kdjM2);
	DrawTimelineKDJChart(memDC, 0, ctx.macdChartTop, ctx.chartWidth, ctx.macdChartHeight, timelinePoint, kdjData, ctx.startIndex, ctx.xAxisPoints);
	DrawSectionGridAndTimeLabels(memDC, ctx, ctx.macdChartTop, ctx.macdChartHeight, true);
}

// ========== DrawTimelineKDJChart ==========

void CIndicatorChart::DrawTimelineKDJChart(CDC& memDC, int x, int y, int width, int height, const std::vector<STOCK::TimelinePoint>& timelinePoint, const std::vector<KDJData>& kdjData, int startIndex /* = 0 */, int xAxisPoints /* = 0 */)
{
	if (timelinePoint.empty() || kdjData.empty())
		return;

	double minVal = 0;
	double maxVal = 100;
	int drawStart = startIndex;
	int drawEnd = startIndex + static_cast<int>(timelinePoint.size());
	if (drawEnd > static_cast<int>(kdjData.size())) drawEnd = static_cast<int>(kdjData.size());
	for (int i = drawStart; i < drawEnd; i++)
	{
		if (!kdjData[i].valid) continue;
		minVal = (std::min)(minVal, kdjData[i].j);
		maxVal = (std::max)(maxVal, kdjData[i].j);
	}
	if (minVal > 0) minVal = 0;
	if (maxVal < 100) maxVal = 100;

	const int padding = g_data.RDPI(4);
	int drawHeight = height - padding * 2;
	if (drawHeight <= 0) return;

	auto valueToY = [&](double val) {
		double ratio = (val - minVal) / (maxVal - minVal);
		return y + padding + static_cast<int>((1.0 - ratio) * drawHeight);
		};

	CPen borderPen(PS_DASH, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&borderPen);
	double borderValues[] = { 0.0, 100.0 };
	for (double v : borderValues)
	{
		int gy = valueToY(v);
		memDC.MoveTo(x, gy);
		memDC.LineTo(x + width, gy);
	}
	CPen refPen(PS_DOT, 1, COLOR_GRAY_GRID);
	memDC.SelectObject(&refPen);
	double refValues[] = { 20.0, 50.0, 80.0 };
	for (double v : refValues)
	{
		int gy = valueToY(v);
		memDC.MoveTo(x, gy);
		memDC.LineTo(x + width, gy);
	}
	memDC.SelectObject(pOldPen);

	const int xSlots = (xAxisPoints > 0) ? xAxisPoints : static_cast<int>(timelinePoint.size());
	const int totalPts = static_cast<int>(timelinePoint.size());

	CPen kPen(PS_SOLID, 1, COLOR_RED_UP);
	memDC.SelectObject(&kPen);
	bool kFirst = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(kdjData.size()); i++)
	{
		int idx = startIndex + i;
		if (!kdjData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(kdjData[idx].k);
		if (kFirst) { memDC.MoveTo(pointX, pointY); kFirst = false; }
		else memDC.LineTo(pointX, pointY);
	}

	CPen dPen(PS_SOLID, 1, RGB(0, 68, 204));
	memDC.SelectObject(&dPen);
	bool dFirst = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(kdjData.size()); i++)
	{
		int idx = startIndex + i;
		if (!kdjData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(kdjData[idx].d);
		if (dFirst) { memDC.MoveTo(pointX, pointY); dFirst = false; }
		else memDC.LineTo(pointX, pointY);
	}

	CPen jPen(PS_SOLID, 1, RGB(0, 136, 34));
	memDC.SelectObject(&jPen);
	bool jFirst = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(kdjData.size()); i++)
	{
		int idx = startIndex + i;
		if (!kdjData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(kdjData[idx].j);
		if (jFirst) { memDC.MoveTo(pointX, pointY); jFirst = false; }
		else memDC.LineTo(pointX, pointY);
	}

	memDC.SelectObject(pOldPen);

	auto crossSignals = CStockIndicator::DetectKDJCross(kdjData);
	const int dotRadius = g_data.RDPI(3);
	const int smallDotRadius = g_data.RDPI(2);
	int oldBkMode = memDC.SetBkMode(TRANSPARENT);
	int lastDrawnDir = 0; // 0=无, 1=金叉, -1=死叉

	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(crossSignals.size()); i++)
	{
		int idx = startIndex + i;
		if (crossSignals[idx] == MACDCrossSignal::None)
			continue;

		int markX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int markY = valueToY(kdjData[idx].k);

		bool isGolden = (crossSignals[idx] == MACDCrossSignal::GoldenCross || crossSignals[idx] == MACDCrossSignal::RepeatedGoldenCross);
		bool isRepeated = (crossSignals[idx] == MACDCrossSignal::RepeatedGoldenCross || crossSignals[idx] == MACDCrossSignal::RepeatedDeathCross);
		int curDir = isGolden ? 1 : -1;

		// 方向反转时始终绘制箭头；同方向重复则只画圆点
		bool drawArrow = !isRepeated || (curDir != lastDrawnDir);
		if (drawArrow)
		{
			DrawCrossArrow(memDC, markX, markY, dotRadius, isGolden);
			lastDrawnDir = curDir;
		}

		// 绘制圆点
		int r = (isRepeated && !drawArrow) ? smallDotRadius : dotRadius;
		COLORREF dotColor = isGolden ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CBrush dotBrush(dotColor);
		CPen dotPen(PS_SOLID, 1, dotColor);
		CBrush* pOldBrush = memDC.SelectObject(&dotBrush);
		CPen* pOldDotPen = memDC.SelectObject(&dotPen);
		memDC.Ellipse(markX - r, markY - r, markX + r, markY + r);

		memDC.SelectObject(pOldBrush);
		memDC.SelectObject(pOldDotPen);
	}
	memDC.SetBkMode(oldBkMode);
}

// ========== DrawTimelineWRChart ==========

void CIndicatorChart::DrawTimelineWRChart(CDC& memDC, int x, int y, int width, int height, const std::vector<STOCK::TimelinePoint>& timelinePoint, const std::vector<WRData>& wrData, int startIndex /* = 0 */, int xAxisPoints /* = 0 */)
{
	if (timelinePoint.empty() || wrData.empty())
		return;

	double minVal = 0;
	double maxVal = 100;

	const int padding = g_data.RDPI(4);
	int drawHeight = height - padding * 2;
	if (drawHeight <= 0) return;

	auto valueToY = [&](double val) {
		double ratio = (val - minVal) / (maxVal - minVal);
		return y + padding + static_cast<int>((1.0 - ratio) * drawHeight);
		};

	CPen refPen(PS_DOT, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&refPen);
	double refValues[] = { 20.0, 50.0, 80.0 };
	for (double v : refValues)
	{
		int gy = valueToY(v);
		memDC.MoveTo(x, gy);
		memDC.LineTo(x + width, gy);
	}
	memDC.SelectObject(pOldPen);

	const int xSlots = (xAxisPoints > 0) ? xAxisPoints : static_cast<int>(timelinePoint.size());
	const int totalPts = static_cast<int>(timelinePoint.size());

	CPen wr1Pen(PS_SOLID, 1, RGB(0, 68, 204));
	memDC.SelectObject(&wr1Pen);
	bool wr1First = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(wrData.size()); i++)
	{
		int idx = startIndex + i;
		if (!wrData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(wrData[idx].wr1);
		if (wr1First) { memDC.MoveTo(pointX, pointY); wr1First = false; }
		else memDC.LineTo(pointX, pointY);
	}

	CPen wr2Pen(PS_SOLID, 1, RGB(204, 34, 34));
	memDC.SelectObject(&wr2Pen);
	bool wr2First = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(wrData.size()); i++)
	{
		int idx = startIndex + i;
		if (!wrData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(wrData[idx].wr2);
		if (wr2First) { memDC.MoveTo(pointX, pointY); wr2First = false; }
		else memDC.LineTo(pointX, pointY);
	}

	memDC.SelectObject(pOldPen);
}

// ========== DrawTimelineWRSection ==========

void CIndicatorChart::DrawTimelineWRSection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;

	std::vector<WRData> wrData;
	if ((hover.viewMode == UI_VIEW_MIN5_KLINE || hover.viewMode == UI_VIEW_MIN30_KLINE) && ctx.klineData)
	{
		wrData = CStockIndicator::CalculateKLineWR(*ctx.klineData);
	}
	else if (hover.viewMode == UI_VIEW_DAY_KLINE)
	{
		if (ctx.klineData)
			wrData = CStockIndicator::CalculateKLineWR(*ctx.klineData);
	}
	else
	{
		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
		wrData = CStockIndicator::CalculateTimelineWR(fullData);
	}

	DrawTimelineWRChart(memDC, 0, ctx.macdChartTop, ctx.chartWidth, ctx.macdChartHeight, timelinePoint, wrData, ctx.startIndex, ctx.xAxisPoints);
	DrawSectionGridAndTimeLabels(memDC, ctx, ctx.macdChartTop, ctx.macdChartHeight, true);
}

// ========== DrawTimelineRSIChart ==========

void CIndicatorChart::DrawTimelineRSIChart(CDC& memDC, int x, int y, int width, int height, const std::vector<STOCK::TimelinePoint>& timelinePoint, const std::vector<RSIData>& rsiData, int startIndex /* = 0 */, int xAxisPoints /* = 0 */)
{
	if (timelinePoint.empty() || rsiData.empty())
		return;

	double minVal = 0;
	double maxVal = 100;

	const int padding = g_data.RDPI(4);
	int drawHeight = height - padding * 2;
	if (drawHeight <= 0) return;

	auto valueToY = [&](double val) {
		double ratio = (val - minVal) / (maxVal - minVal);
		return y + padding + static_cast<int>((1.0 - ratio) * drawHeight);
		};

	CPen refPen(PS_DOT, 1, COLOR_GRAY_GRID);
	CPen* pOldPen = memDC.SelectObject(&refPen);
	double refValues[] = { 30.0, 50.0, 70.0 };
	for (double v : refValues)
	{
		int gy = valueToY(v);
		memDC.MoveTo(x, gy);
		memDC.LineTo(x + width, gy);
	}
	memDC.SelectObject(pOldPen);

	const int xSlots = (xAxisPoints > 0) ? xAxisPoints : static_cast<int>(timelinePoint.size());
	const int totalPts = static_cast<int>(timelinePoint.size());

	CPen rsi1Pen(PS_SOLID, 1, RGB(0, 68, 204));
	memDC.SelectObject(&rsi1Pen);
	bool rsi1First = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(rsiData.size()); i++)
	{
		int idx = startIndex + i;
		if (!rsiData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(rsiData[idx].rsi1);
		if (rsi1First) { memDC.MoveTo(pointX, pointY); rsi1First = false; }
		else memDC.LineTo(pointX, pointY);
	}

	CPen rsi2Pen(PS_SOLID, 1, RGB(204, 34, 34));
	memDC.SelectObject(&rsi2Pen);
	bool rsi2First = true;
	for (int i = 0; i < totalPts && (startIndex + i) < static_cast<int>(rsiData.size()); i++)
	{
		int idx = startIndex + i;
		if (!rsiData[idx].valid) continue;
		int pointX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + static_cast<int>(width / static_cast<float>(xSlots) / 2);
		int pointY = valueToY(rsiData[idx].rsi2);
		if (rsi2First) { memDC.MoveTo(pointX, pointY); rsi2First = false; }
		else memDC.LineTo(pointX, pointY);
	}

	memDC.SelectObject(pOldPen);
}

// ========== DrawTimelineRSISection ==========

void CIndicatorChart::DrawTimelineRSISection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover)
{
	const auto& timelinePoint = *ctx.timelinePoint;

	std::vector<RSIData> rsiData;
	if ((hover.viewMode == UI_VIEW_MIN5_KLINE || hover.viewMode == UI_VIEW_MIN30_KLINE) && ctx.klineData)
	{
		rsiData = CStockIndicator::CalculateKLineRSI(*ctx.klineData);
	}
	else if (hover.viewMode == UI_VIEW_DAY_KLINE)
	{
		if (ctx.klineData)
			rsiData = CStockIndicator::CalculateKLineRSI(*ctx.klineData);
	}
	else
	{
		const auto& fullData = ctx.fullTimeline ? *ctx.fullTimeline : timelinePoint;
		rsiData = CStockIndicator::CalculateTimelineRSI(fullData);
	}

	DrawTimelineRSIChart(memDC, 0, ctx.macdChartTop, ctx.chartWidth, ctx.macdChartHeight, timelinePoint, rsiData, ctx.startIndex, ctx.xAxisPoints);
	DrawSectionGridAndTimeLabels(memDC, ctx, ctx.macdChartTop, ctx.macdChartHeight, true);
}

// ========== DrawVolumeChart ==========

void CIndicatorChart::DrawVolumeChart(CDC& memDC, int x, int y, int width, int height, const std::vector<STOCK::TimelinePoint>& timelinePoint, const STOCK::StockInfo* stockInfo /* = nullptr */, int startIndex /* = 0 */, int visibleCount /* = -1 */, int xAxisPoints /* = 0 */, bool isHoveringVolume /* = false */, int hoveredBarIndex /* = -1 */)
{
	if (timelinePoint.empty())
		return;

	int total = static_cast<int>(timelinePoint.size());
	int endIdx = total;
	if (visibleCount > 0)
	{
		endIdx = (std::min)(total, startIndex + visibleCount);
		startIndex = (std::max)(0, (std::min)(startIndex, total - 1));
		if (endIdx <= startIndex) endIdx = startIndex + 1;
	}
	else
	{
		startIndex = 0;
	}

	STOCK::Volume maxVolume = 0;
	for (int i = startIndex; i < endIdx; i++)
	{
		if (timelinePoint[i].volume > maxVolume)
			maxVolume = timelinePoint[i].volume;
	}

	if (maxVolume == 0)
		return;

	const int xSlots = (xAxisPoints > 0) ? xAxisPoints : static_cast<int>(timelinePoint.size());
	const int fixedGap = 1;
	int slotWidth = xSlots > 0 ? width / xSlots : 1;
	int barWidth = max(2, slotWidth - fixedGap);
	int halfSlot = slotWidth / 2;

	for (int i = startIndex; i < endIdx; i++)
	{
		const auto& item = timelinePoint[i];
		int barX = x + static_cast<int>(width / static_cast<float>(xSlots) * i) + halfSlot - barWidth / 2;

		float ratio = static_cast<float>(item.volume) / maxVolume;
		int barHeight = static_cast<int>(ratio * height);
		barHeight = max(1, barHeight);

		int barY = y + height - barHeight;

		COLORREF color = COLOR_GREEN_DOWN;
		// K线模式下openPrice>0，使用收盘价vs开盘价判断涨跌，与K线柱颜色一致
		if (item.openPrice > 0)
		{
			if (item.price >= item.openPrice)
				color = COLOR_RED_UP;
		}
		else if (i > 0)
		{
			if (item.price >= timelinePoint[i - 1].price)
				color = COLOR_RED_UP;
		}

		CBrush brush(color);
		memDC.FillRect(CRect(barX, barY, barX + barWidth, y + height), &brush);
	}

	if (isHoveringVolume && hoveredBarIndex >= 0 && hoveredBarIndex < static_cast<int>(timelinePoint.size()))
	{
		const auto& item = timelinePoint[hoveredBarIndex];
		int barX = x + static_cast<int>(width / static_cast<float>(xSlots) * hoveredBarIndex) + halfSlot - barWidth / 2;

		float ratio = static_cast<float>(timelinePoint[hoveredBarIndex].volume) / maxVolume;
		int barHeight = static_cast<int>(ratio * height);
		barHeight = max(1, barHeight);
		int barY = y + height - barHeight;

		COLORREF color = COLOR_GREEN_DOWN;
		if (item.openPrice > 0)
		{
			if (item.price >= item.openPrice)
				color = COLOR_RED_UP;
		}
		else if (hoveredBarIndex > 0)
		{
			if (timelinePoint[hoveredBarIndex].price >= timelinePoint[hoveredBarIndex - 1].price)
				color = COLOR_RED_UP;
		}

		CPen highlightPen(PS_SOLID, 2, color);
		CPen* pOldPen = memDC.SelectObject(&highlightPen);
		memDC.Rectangle(CRect(barX - 1, barY - 1, barX + barWidth + 1, y + height + 1));
		memDC.SelectObject(pOldPen);
	}
}

// ========== DrawTimelineKDJChart ==========