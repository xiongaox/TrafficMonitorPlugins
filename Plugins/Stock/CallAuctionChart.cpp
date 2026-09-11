#include "pch.h"
#include "CallAuctionChart.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "StockFont.h"
#include <algorithm>
#include <ctime>

namespace
{
	constexpr int kAuctionStartSecond = 9 * 3600 + 15 * 60;
	constexpr int kAuctionEndSecond = 9 * 3600 + 25 * 60;

	bool IsTodayAuctionSnapshot(const STOCK::CallAuctionSnapshot& snapshot, const struct tm& today)
	{
		if (snapshot.timestamp == 0)
			return false;

		struct tm snapshotTm;
		localtime_s(&snapshotTm, &snapshot.timestamp);
		if (snapshotTm.tm_year != today.tm_year || snapshotTm.tm_yday != today.tm_yday)
			return false;

		const int second = snapshotTm.tm_hour * 3600 + snapshotTm.tm_min * 60 + snapshotTm.tm_sec;
		return second >= kAuctionStartSecond && second <= kAuctionEndSecond;
	}

	int AuctionSecond(time_t timestamp)
	{
		struct tm snapshotTm;
		localtime_s(&snapshotTm, &timestamp);
		return snapshotTm.tm_hour * 3600 + snapshotTm.tm_min * 60 + snapshotTm.tm_sec;
	}
}

void CCallAuctionChart::Draw(CDC& memDC, const TimelineDrawContext& ctx, const STOCK::CallAuctionData& callAuctionData, const std::wstring& stockId)
{
	const auto& allSnapshots = callAuctionData.snapshots;
	time_t now = time(nullptr);
	struct tm today;
	localtime_s(&today, &now);
	std::vector<const STOCK::CallAuctionSnapshot*> snapshots;
	snapshots.reserve(allSnapshots.size());
	for (const auto& snapshot : allSnapshots)
	{
		if (IsTodayAuctionSnapshot(snapshot, today))
			snapshots.push_back(&snapshot);
	}
	const int totalPoints = static_cast<int>(snapshots.size());

	if (snapshots.empty())
	{
		CString titleText;
		CString subText1;
		CString subText2;

		SYSTEMTIME systemNow;
		GetLocalTime(&systemNow);
		bool isWeekend = (systemNow.wDayOfWeek == 0 || systemNow.wDayOfWeek == 6);
		int minutes = systemNow.wHour * 60 + systemNow.wMinute;

		if (!stockId.empty() && !CCommon::IsAGStockCode(stockId))
		{
			titleText = _T("该标的暂无集合竞价走势");
			subText1 = _T("仅沪深京 A 股与场内 ETF 提供早盘集合竞价申报数据");
			subText2 = _T("港股、美股、贵金属等标的暂不支持该功能");
		}
		else if (isWeekend)
		{
			titleText = _T("周末休市中");
			subText1 = _T("早盘集合竞价时间为交易日 09:15 - 09:25");
			subText2 = _T("下个交易日早盘软件运行中将自动为自选与持仓标的采集竞价走势");
		}
		else if (minutes < 9 * 60 + 15)
		{
			titleText = _T("等待早盘集合竞价 (09:15 - 09:25)");
			subText1 = _T("进入 09:15 竞价时段后将自动实时记录申报走势与撮合价格");
			subText2 = _T("请确保该标的已在【自选股】或【持仓】列表中");
		}
		else if (minutes >= 9 * 60 + 15 && minutes < 9 * 60 + 30)
		{
			titleText = _T("正在尝试采集早盘集合竞价数据…");
			subText1 = _T("09:15-09:30 正在轮询竞价行情，09:15-09:25 的快照将绘制为走势");
			subText2 = _T("请确认标的已加入【自选】或【持仓】列表");
		}
		else
		{
			titleText = _T("今天没有可显示的早盘竞价记录");
			subText1 = _T("请在下个交易日 09:15 前加入【自选】或【持仓】，并保持软件运行");
			subText2 = _T("竞价记录只在本次运行中缓存，开市后无法补取");
		}

		CFont titleFont, subFont;
		CreateStockFont(titleFont, memDC, g_data.RDPI(13), FW_SEMIBOLD);
		CreateStockFont(subFont, memDC, g_data.RDPI(10), FW_NORMAL);
		int centerY = ctx.priceChartTop + ctx.priceChartHeight / 2;

		CFont* pOldFont = memDC.SelectObject(&titleFont);
		CSize szTitle = memDC.GetTextExtent(titleText);
		memDC.SelectObject(&subFont);
		CSize szSub1 = memDC.GetTextExtent(subText1);
		CSize szSub2 = memDC.GetTextExtent(subText2);

		const int lineGap1 = g_data.RDPI(10);
		const int lineGap2 = g_data.RDPI(6);
		int totalH = szTitle.cy + lineGap1 + szSub1.cy + lineGap2 + szSub2.cy;
		int startY = centerY - totalH / 2;

		memDC.SelectObject(&titleFont);
		memDC.SetTextColor(COLOR_TEXT_PRIMARY);
		memDC.TextOut(max(g_data.RDPI(8), (ctx.chartWidth - szTitle.cx) / 2), startY, titleText);

		memDC.SelectObject(&subFont);
		memDC.SetTextColor(COLOR_TEXT_MUTED);
		int y1 = startY + szTitle.cy + lineGap1;
		memDC.TextOut(max(g_data.RDPI(8), (ctx.chartWidth - szSub1.cx) / 2), y1, subText1);

		memDC.SetTextColor(COLOR_TEXT_DIM);
		int y2 = y1 + szSub1.cy + lineGap2;
		memDC.TextOut(max(g_data.RDPI(8), (ctx.chartWidth - szSub2.cx) / 2), y2, subText2);

		memDC.SelectObject(pOldFont);
		return;
	}

	const int startMinute = 9 * 60 + 15;
	const int endMinute = 9 * 60 + 25;
	const int totalMinutes = endMinute - startMinute;
	const int totalSeconds = kAuctionEndSecond - kAuctionStartSecond;
	auto timeToX = [&](time_t timestamp) -> int {
		double ratio = static_cast<double>(AuctionSecond(timestamp) - kAuctionStartSecond) / totalSeconds;
		return static_cast<int>(ratio * ctx.chartWidth);
	};

	{
		CPen gridPen(PS_SOLID, 1, COLOR_GRAY_GRID);
		CPen* pOldPen = memDC.SelectObject(&gridPen);
		for (int minute = startMinute; minute <= endMinute; minute += 5)
		{
			int xPos = static_cast<int>(static_cast<double>(minute - startMinute) / totalMinutes * ctx.chartWidth);
			memDC.MoveTo(xPos, ctx.priceChartTop);
			memDC.LineTo(xPos, ctx.volumeChartTop + ctx.volumeChartHeight);
		}
		memDC.SelectObject(pOldPen);
	}

	if (callAuctionData.prevClosePrice > 0 && ctx.unitY > 0)
	{
		STOCK::Price prevClose = callAuctionData.prevClosePrice;
		int prevCloseY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((prevClose - ctx.minPrice) * ctx.unitY);
		CPen dashPen(PS_DASH, 1, RGB(128, 128, 128));
		CPen* pOldPen = memDC.SelectObject(&dashPen);
		memDC.MoveTo(0, prevCloseY);
		memDC.LineTo(ctx.chartWidth, prevCloseY);
		memDC.SelectObject(pOldPen);

		CString prevCloseLabel;
		prevCloseLabel.Format(_T("昨收 %.2f"), prevClose);
		memDC.SetTextColor(RGB(128, 128, 128));
		memDC.TextOut(2, prevCloseY - memDC.GetTextExtent(prevCloseLabel).cy - 1, prevCloseLabel);
	}

	if (callAuctionData.limitUpPrice > 0 && ctx.unitY > 0)
	{
		int limitUpY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((callAuctionData.limitUpPrice - ctx.minPrice) * ctx.unitY);
		CPen dashPen(PS_DOT, 1, RGB(200, 0, 200));
		CPen* pOldPen = memDC.SelectObject(&dashPen);
		memDC.MoveTo(0, limitUpY);
		memDC.LineTo(ctx.chartWidth, limitUpY);
		memDC.SelectObject(pOldPen);
	}
	if (callAuctionData.limitDownPrice > 0 && ctx.unitY > 0)
	{
		int limitDownY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((callAuctionData.limitDownPrice - ctx.minPrice) * ctx.unitY);
		CPen dashPen(PS_DOT, 1, RGB(0, 128, 0));
		CPen* pOldPen = memDC.SelectObject(&dashPen);
		memDC.MoveTo(0, limitDownY);
		memDC.LineTo(ctx.chartWidth, limitDownY);
		memDC.SelectObject(pOldPen);
	}

	const auto& lastSnapshot = *snapshots.back();
	if (ctx.unitY > 0)
	{
		CPen pricePen(PS_SOLID, 2, RGB(0, 0, 180));
		CPen* pOldPen = memDC.SelectObject(&pricePen);
		bool firstPoint = true;
		int prevX = 0, prevY = 0;
		for (const auto* snapshot : snapshots)
		{
			if (snapshot->matchPrice <= 0)
				continue;
			int x = timeToX(snapshot->timestamp);
			int y = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((snapshot->matchPrice - ctx.minPrice) * ctx.unitY);
			if (firstPoint)
			{
				prevX = x;
				prevY = y;
				firstPoint = false;
			}
			else
			{
				memDC.MoveTo(prevX, prevY);
				memDC.LineTo(x, y);
				prevX = x;
				prevY = y;
			}
		}
		memDC.SelectObject(pOldPen);

		if (lastSnapshot.matchPrice > 0)
		{
			int lastY = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((lastSnapshot.matchPrice - ctx.minPrice) * ctx.unitY);
			CString priceLabel = CCommon::FormatFloat(lastSnapshot.matchPrice);
			memDC.SetTextColor(lastSnapshot.matchPrice >= callAuctionData.prevClosePrice ? COLOR_RED_UP : COLOR_GREEN_DOWN);
			CSize labelSize = memDC.GetTextExtent(priceLabel);
			memDC.TextOut(ctx.chartWidth + 2 - labelSize.cx, lastY - labelSize.cy / 2, priceLabel);
		}
	}

	{
		CString infoText;
		if (lastSnapshot.matchPrice > 0)
		{
			double changePercent = callAuctionData.prevClosePrice > 0 ?
				(lastSnapshot.matchPrice - callAuctionData.prevClosePrice) / callAuctionData.prevClosePrice * 100 : 0;
			CString priceStr = CCommon::FormatFloat(lastSnapshot.matchPrice);
			CString changeStr = CCommon::FormatSignedValue(changePercent, _T("%.2f"));
			infoText.Format(_T("撮合价 %s  %s%%"), priceStr, changeStr);
			if (callAuctionData.isReplay)
				infoText += _T("  测试回放");
			memDC.SetTextColor(lastSnapshot.matchPrice >= callAuctionData.prevClosePrice ? COLOR_RED_UP : COLOR_GREEN_DOWN);
		}
		else
		{
			infoText = _T("暂无撮合价");
			memDC.SetTextColor(COLOR_GRAY_TEXT);
		}
		memDC.TextOut(g_data.RDPI(4), ctx.priceChartTop + g_data.RDPI(2), infoText);
	}

	if (ctx.maxPrice > 0 && ctx.minPrice >= 0 && ctx.maxPrice > ctx.minPrice && ctx.niceStep > 0)
	{
		memDC.SetTextColor(COLOR_GRAY_TEXT);
		double priceRange = ctx.maxPrice - ctx.minPrice;
		int labelCount = static_cast<int>(round(priceRange / ctx.niceStep));
		for (int i = 0; i <= labelCount; i++)
		{
			double p = round((ctx.minPrice + i * ctx.niceStep) * 1000.0) / 1000.0;
			int y = ctx.priceChartTop + ctx.priceChartHeight - static_cast<int>((p - ctx.minPrice) * ctx.unitY);
			CString label = CCommon::FormatFloat(p);
			CSize sz = memDC.GetTextExtent(label);
			memDC.TextOut(-sz.cx - g_data.RDPI(4), y - sz.cy / 2, label);
		}
	}

	{
		STOCK::Volume maxAddVol = 0;
		STOCK::Volume maxUnmatchVol = 0;
		for (const auto* snapshot : snapshots)
		{
			maxAddVol = (std::max)(maxAddVol, snapshot->addVol);
			maxUnmatchVol = (std::max)(maxUnmatchVol, (std::max)(snapshot->unmatchBidVol, snapshot->unmatchAskVol));
		}
		if (maxAddVol <= 0) maxAddVol = 1;
		if (maxUnmatchVol <= 0) maxUnmatchVol = 1;

		int volHalfHeight = ctx.volumeChartHeight / 2;
		int volMidY = ctx.volumeChartTop + volHalfHeight;
		CPen midPen(PS_SOLID, 1, COLOR_GRAY_MIDDLE);
		CPen* pOldPen = memDC.SelectObject(&midPen);
		memDC.MoveTo(0, volMidY);
		memDC.LineTo(ctx.chartWidth, volMidY);
		memDC.SelectObject(pOldPen);

		int actualBarWidth = max(1, static_cast<int>(static_cast<double>(ctx.chartWidth) * 3 / totalSeconds) - 1);
			for (const auto* snapshot : snapshots)
			{
				int x = (std::max)(0, (std::min)(ctx.chartWidth - actualBarWidth, timeToX(snapshot->timestamp) - actualBarWidth / 2));

				if (snapshot->addVol > 0)
				{
					int barHeight = static_cast<int>(static_cast<double>(snapshot->addVol) / maxAddVol * volHalfHeight * 0.9);
					barHeight = max(1, barHeight);
					memDC.FillSolidRect(x, volMidY, actualBarWidth, barHeight, COLOR_TEXT_MUTED);
				}
				if (snapshot->unmatchBidVol > 0)
				{
					int barHeight = static_cast<int>(static_cast<double>(snapshot->unmatchBidVol) / maxUnmatchVol * volHalfHeight * 0.9);
					barHeight = max(1, barHeight);
					int halfW = max(1, actualBarWidth / 2);
					memDC.FillSolidRect(x, volMidY - barHeight, halfW, barHeight, COLOR_GREEN_DOWN);
				}
				if (snapshot->unmatchAskVol > 0)
				{
					int barHeight = static_cast<int>(static_cast<double>(snapshot->unmatchAskVol) / maxUnmatchVol * volHalfHeight * 0.9);
					barHeight = max(1, barHeight);
					int halfW = max(1, actualBarWidth / 2);
					memDC.FillSolidRect(x + halfW, volMidY - barHeight, halfW, barHeight, COLOR_RED_UP);
				}
			}

			struct LegendItem
			{
				CString label;
				CString value;
				COLORREF color;
			};
			const CString deltaValue = CCommon::FormatVolume(static_cast<double>(lastSnapshot.addVol)) + _T("股");
			const CString cumulativeValue = CCommon::FormatVolume(static_cast<double>(lastSnapshot.matchVolume)) + _T("股");
			const CString bidValue = CCommon::FormatVolume(static_cast<double>(lastSnapshot.unmatchBidVol)) + _T("股");
			const CString askValue = CCommon::FormatVolume(static_cast<double>(lastSnapshot.unmatchAskVol)) + _T("股");
			const int headerTop = ctx.volumeChartTop - g_data.RDPI(16);
			const int headerHeight = g_data.RDPI(16);
			const int padding = g_data.RDPI(4);
			const int swatchSize = (std::max)(g_data.RDPI(4), g_data.RDPI(6));
			const int swatchGap = g_data.RDPI(3);
			const int labelGap = g_data.RDPI(2);
			const int itemGap = g_data.RDPI(8);
			auto drawLegend = [&](const std::vector<LegendItem>& items) {
				int totalWidth = 0;
				for (size_t i = 0; i < items.size(); ++i)
				{
					totalWidth += swatchSize + swatchGap + memDC.GetTextExtent(items[i].label).cx + labelGap + memDC.GetTextExtent(items[i].value).cx;
					if (i + 1 < items.size())
						totalWidth += itemGap;
				}
				if (totalWidth > ctx.chartWidth - padding * 2)
					return false;

				int oldBkMode = memDC.SetBkMode(TRANSPARENT);
				int x = padding;
				for (size_t i = 0; i < items.size(); ++i)
				{
					int swatchY = headerTop + (headerHeight - swatchSize) / 2;
					memDC.FillSolidRect(x, swatchY, swatchSize, swatchSize, items[i].color);
					x += swatchSize + swatchGap;
					memDC.SetTextColor(COLOR_TEXT_MUTED);
					memDC.TextOut(x, headerTop + (headerHeight - memDC.GetTextExtent(items[i].label).cy) / 2, items[i].label);
					x += memDC.GetTextExtent(items[i].label).cx + labelGap;
					memDC.SetTextColor(items[i].color);
					memDC.TextOut(x, headerTop + (headerHeight - memDC.GetTextExtent(items[i].value).cy) / 2, items[i].value);
					x += memDC.GetTextExtent(items[i].value).cx + itemGap;
				}
				memDC.SetBkMode(oldBkMode);
				return true;
			};

			if (!drawLegend({
				{ _T("Δ撮合"), deltaValue, COLOR_TEXT_MUTED },
				{ _T("累计"), cumulativeValue, COLOR_TEXT_MUTED },
				{ _T("估买"), bidValue, COLOR_GREEN_DOWN },
				{ _T("估卖"), askValue, COLOR_RED_UP }
				}))
			{
				if (!drawLegend({
					{ _T("Δ撮合"), deltaValue, COLOR_TEXT_MUTED },
					{ _T("估买"), bidValue, COLOR_GREEN_DOWN },
					{ _T("估卖"), askValue, COLOR_RED_UP }
					}))
				{
					if (!drawLegend({
						{ _T("撮"), deltaValue, COLOR_TEXT_MUTED },
						{ _T("买"), bidValue, COLOR_GREEN_DOWN },
						{ _T("卖"), askValue, COLOR_RED_UP }
						}))
					{
						drawLegend({
							{ _T("买"), bidValue, COLOR_GREEN_DOWN },
							{ _T("卖"), askValue, COLOR_RED_UP }
							});
					}
				}
			}
	}

	{
		memDC.SetTextColor(COLOR_GRAY_TEXT);
		for (int minute = startMinute; minute <= endMinute; minute += 5)
		{
			int xPos = static_cast<int>(static_cast<double>(minute - startMinute) / totalMinutes * ctx.chartWidth);
			int hour = minute / 60;
			int min = minute % 60;
			CString timeLabel;
			timeLabel.Format(_T("%02d:%02d"), hour, min);
			CSize labelSize = memDC.GetTextExtent(timeLabel);
			int labelX = max(0, min(xPos - labelSize.cx / 2, ctx.chartWidth - labelSize.cx));
			memDC.TextOut(labelX, ctx.positionY, timeLabel);
		}
	}
}
