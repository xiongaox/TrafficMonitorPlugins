#include "pch.h"
#include "StatusBarPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <Stock.h>
#include <algorithm>
#include <map>
#include <ctime>
#include <limits>

void CStatusBarPanel::DrawHeader(CDC& memDC, const STOCK::StockInfo& realtimeData, int windowWidth, int headerHeight, const CString& macdTrendSignal)
{
	double diff = realtimeData.GetChangeAmount();
	double diffPercent = realtimeData.GetChangePercent();
	COLORREF diffColor = diff >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;

	// 标题格式：股票名称
	CString prefixTxt;
	if (!realtimeData.displayName.empty())
		prefixTxt.Format(_T("%s "), realtimeData.displayName.c_str());
	else
		prefixTxt = _T("股票行情 ");

	CString currentTxt = realtimeData.IsETF() ? CCommon::FormatETFPrice(realtimeData.currentPrice) : CCommon::FormatFloat(realtimeData.currentPrice);
	CString diffTxt;
	if (diff >= 0)
		diffTxt.Format(_T(" +%.2f%%"), diffPercent);
	else
		diffTxt.Format(_T(" %.2f%%"), diffPercent);

	// MACD趋势信号标签
	CString macdTxt;
	if (!macdTrendSignal.IsEmpty())
		macdTxt.Format(_T(" [%s]"), macdTrendSignal.GetString());

	CFont headerFont;
	headerFont.CreateFont(-g_data.RDPI(13), 0, 0, 0, FW_BOLD, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont* pOldFont = memDC.SelectObject(&headerFont);

	// 计算总宽度，在整个标题栏水平居中
	CSize prefixSize = memDC.GetTextExtent(prefixTxt);
	CSize currentSize = memDC.GetTextExtent(currentTxt);
	CSize diffSize = memDC.GetTextExtent(diffTxt);
	CSize macdSize = memDC.GetTextExtent(macdTxt);
	int totalWidth = prefixSize.cx + currentSize.cx + diffSize.cx + macdSize.cx;

	int startX = (windowWidth - totalWidth) / 2;
	int centerY = headerHeight / 2;

	memDC.SetTextColor(COLOR_BLACK);
	memDC.TextOut(startX, centerY - prefixSize.cy / 2, prefixTxt);

	int curX = startX + prefixSize.cx;
	memDC.SetTextColor(diffColor);
	memDC.TextOut(curX, centerY - currentSize.cy / 2, currentTxt);

	curX += currentSize.cx;
	memDC.TextOut(curX, centerY - diffSize.cy / 2, diffTxt);

	// 绘制MACD趋势信号
	if (!macdTxt.IsEmpty())
	{
		curX += diffSize.cx;
		// 信号颜色：正T=红色，反T=绿色，持有=橙色，观望=次要灰色
		COLORREF macdColor;
		if (macdTrendSignal == _T("正T"))
			macdColor = COLOR_RED_UP;
		else if (macdTrendSignal == _T("反T"))
			macdColor = COLOR_GREEN_DOWN;
		else if (macdTrendSignal == _T("持有"))
			macdColor = COLOR_GOLDEN;
		else
			macdColor = COLOR_GRAY_TEXT;
		memDC.SetTextColor(macdColor);
		memDC.TextOut(curX, centerY - macdSize.cy / 2, macdTxt);
	}

	memDC.SelectObject(pOldFont);
	headerFont.DeleteObject();
}

void CStatusBarPanel::DrawRelatedStockBar(CDC& memDC, int w, int topBarY, int singleBarHeight, const std::wstring& stockId, int viewMode)
{
	// 绘制关联股票/指数栏深色背景 (#15171E)
	memDC.FillSolidRect(0, topBarY, w, singleBarHeight, COLOR_BG_SUBHEADER);
	memDC.FillSolidRect(0, topBarY + singleBarHeight - 1, w, 1, COLOR_DARK_GRAY_BORDER);

	const int GAP = 4;

	std::vector<std::wstring> relatedCodes = g_data.GetRelatedStocks(stockId);
	bool isRelatedMode = !relatedCodes.empty();
	if (relatedCodes.empty())
	{
		// 没有关联股票时使用默认指数：上证指数、中证银行、恒生科技
		relatedCodes = { L"sh000001", L"sz399986", L"rt_hkHSTECH" };
	}
	const int relatedCount = static_cast<int>(relatedCodes.size());

	// 从已计算好的均幅数据中获取（由UpdateRelatedStocksAvgDiff在行情更新时计算）
	bool showAvgDiff = isRelatedMode && relatedCount >= 1;
	double avgDiffPercent = 0.0;
	double minAvgDiff = 0.0;
	double maxAvgDiff = 0.0;
	CString minAvgValueStr, avgValueStr, maxAvgValueStr, trendArrowStr;
	if (showAvgDiff)
	{
		auto avgData = g_data.GetAvgDiffData(stockId);
		minAvgDiff = avgData.minVal;
		maxAvgDiff = avgData.maxVal;
		avgDiffPercent = avgData.currentVal;
		// 均幅数据全为0说明尚未计算，不显示均幅区域
		if (minAvgDiff == 0.0 && maxAvgDiff == 0.0 && avgDiffPercent == 0.0)
			showAvgDiff = false;
	}
	if (showAvgDiff)
	{
		if (minAvgDiff >= 0)
			minAvgValueStr.Format(_T("+%.2f"), minAvgDiff);
		else
			minAvgValueStr.Format(_T("%.2f"), minAvgDiff);

		if (avgDiffPercent >= 0)
			avgValueStr.Format(_T("+%.2f"), avgDiffPercent);
		else
			avgValueStr.Format(_T("%.2f"), avgDiffPercent);

		if (maxAvgDiff >= 0)
			maxAvgValueStr.Format(_T("+%.2f"), maxAvgDiff);
		else
			maxAvgValueStr.Format(_T("%.2f"), maxAvgDiff);

		// 计算趋势箭头：分时界面用1分钟趋势，5分钟界面用5分钟趋势
		RegResult trend = (viewMode < UI_VIEW_MIN5_KLINE)
			? g_data.Get1MinAvgTrend(stockId)
			: g_data.Get5MinAvgTrend(stockId);
		trendArrowStr = _T("|"); // 默认竖线
		if (trend.valid)
		{
			if (trend.r2 >= 0.1 || std::abs(trend.slope) >= 0.001)
			{
				// 箭头强度：低1个、中2个、高3个
				int arrowCount = 1;
				if (trend.r2 >= 0.7)
					arrowCount = 3;
				else if (trend.r2 >= 0.55)
					arrowCount = 2;
				if (trend.slope > 0)
					trendArrowStr = CString(_T('↑'), arrowCount);
				else
					trendArrowStr = CString(_T('↓'), arrowCount);
			}
		}
	}

	// 动态计算字体大小：先测量实际文字宽度，再逐步缩小直到适合
	CFont* pOldFont = nullptr;
	CFont dynFont;
	CFont avgFont; // 右侧均幅区域固定字体
	{
		const int minFont = 8;
		const int maxFont = 14;

		// 右侧均幅区域始终使用最大字号
		if (showAvgDiff)
		{
			avgFont.CreateFont(-g_data.RDPI(maxFont), 0, 0, 0, FW_NORMAL, 0, 0, 0,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
		}

		if (!isRelatedMode)
		{
			// 非关联模式直接用最大字号
			dynFont.CreateFont(-g_data.RDPI(maxFont), 0, 0, 0, FW_NORMAL, 0, 0, 0,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
			pOldFont = memDC.SelectObject(&dynFont);
		}
		else
		{
			// 关联模式：用实际测量法，从最大字号开始尝试，逐步缩小直到文字总宽度适合
			int availableWidth = w - GAP * 2;
			if (showAvgDiff)
				availableWidth -= (g_data.RDPI(120) + GAP * 2);

			int fontSize = maxFont;
			while (fontSize >= minFont)
			{
				CFont testFont;
				testFont.CreateFont(-g_data.RDPI(fontSize), 0, 0, 0, FW_NORMAL, 0, 0, 0,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
				CFont* pPrev = memDC.SelectObject(&testFont);

				int totalWidth = 0;
				for (int i = 0; i < relatedCount; i++)
				{
					auto stockData = g_data.GetStockData(relatedCodes[i]);
					if (stockData && stockData->info.is_ok)
					{
						CString nameStr = stockData->info.GetStockListName() + _T(":");
						CString changeStr = _T("+00.00%");
						totalWidth += memDC.GetTextExtent(nameStr).cx + memDC.GetTextExtent(changeStr).cx + GAP * 2;
					}
					else
					{
						CString nameStr = CString(relatedCodes[i].c_str()) + _T(" --");
						totalWidth += memDC.GetTextExtent(nameStr).cx + GAP;
					}
				}

				memDC.SelectObject(pPrev);
				testFont.DeleteObject();

				if (totalWidth <= availableWidth)
					break; // 当前字号适合

				fontSize--;
			}

			if (fontSize < minFont) fontSize = minFont;

			dynFont.CreateFont(-g_data.RDPI(fontSize), 0, 0, 0, FW_NORMAL, 0, 0, 0,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
			pOldFont = memDC.SelectObject(&dynFont);
		}
	}

	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	if (isRelatedMode)
	{
		// 关联模式：流式布局，每只股票紧凑排列
		int textX = GAP;
		for (int i = 0; i < relatedCount; i++)
		{
			auto stockData = g_data.GetStockData(relatedCodes[i]);

			if (stockData && stockData->info.is_ok)
			{
				const auto& info = stockData->info;
				double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
				double diff = displayPrice - info.prevClosePrice;
				double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;

				CString nameStr = info.GetStockListName() + _T(":");
				CString changeStr;
				if (diff >= 0)
					changeStr.Format(_T("+%.2f%%"), diffPercent);
				else
					changeStr.Format(_T("%.2f%%"), diffPercent);

				memDC.SetTextColor(COLOR_TEXT_MUTED);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;

				memDC.SetTextColor(diffPercent >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), changeStr);
				textX += memDC.GetTextExtent(changeStr).cx + GAP;
			}
			else
			{
				CString nameStr = CString(relatedCodes[i].c_str()) + _T(" --");
				memDC.SetTextColor(COLOR_TEXT_DIM);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;
			}
		}

		// 右侧预留120像素显示：最小值 均值 最大值
		if (showAvgDiff)
		{
			int avgAreaWidth = g_data.RDPI(120);
			int avgAreaX = w - avgAreaWidth - GAP;
			int avgAreaH = singleBarHeight;
			int avgAreaY = topBarY;

			static const COLORREF AVG_RED_COLORS[] = {
				RGB(190, 24, 60),
				RGB(159, 18, 57),
				RGB(136, 19, 55)
			};
			static const COLORREF AVG_GREEN_COLORS[] = {
				RGB(6, 95, 70),
				RGB(6, 78, 59),
				RGB(4, 47, 46)
			};

			double range = maxAvgDiff - minAvgDiff;
			int redIdx, greenIdx;
			if (range == 0)
			{
				redIdx = 0;
				greenIdx = 2;
			}
			else
			{
				double posRatio = (avgDiffPercent - minAvgDiff) / range;
				posRatio = max(0.0, min(1.0, posRatio));
				redIdx = static_cast<int>(posRatio * 2 + 0.5);
				greenIdx = 2 - redIdx;
				redIdx = max(0, min(2, redIdx));
				greenIdx = max(0, min(2, greenIdx));
			}
			COLORREF redColor = AVG_RED_COLORS[redIdx];
			COLORREF greenColor = AVG_GREEN_COLORS[greenIdx];

			if (range == 0 || avgDiffPercent <= minAvgDiff)
			{
				memDC.FillSolidRect(avgAreaX, avgAreaY, avgAreaWidth, avgAreaH, greenColor);
			}
			else if (avgDiffPercent >= maxAvgDiff)
			{
				memDC.FillSolidRect(avgAreaX, avgAreaY, avgAreaWidth, avgAreaH, redColor);
			}
			else
			{
				double redRatio = (avgDiffPercent - minAvgDiff) / range;
				int redWidth = static_cast<int>(avgAreaWidth * redRatio);
				int greenWidth = avgAreaWidth - redWidth;
				if (avgDiffPercent >= 0)
				{
					memDC.FillSolidRect(avgAreaX, avgAreaY, redWidth, avgAreaH, redColor);
					memDC.FillSolidRect(avgAreaX + redWidth, avgAreaY, greenWidth, avgAreaH, greenColor);
				}
				else
				{
					memDC.FillSolidRect(avgAreaX, avgAreaY, greenWidth, avgAreaH, greenColor);
					memDC.FillSolidRect(avgAreaX + greenWidth, avgAreaY, redWidth, avgAreaH, redColor);
				}
			}

			CFont* pPrevFont = memDC.SelectObject(&avgFont);
			int thirdWidth = avgAreaWidth / 3;

			if (!trendArrowStr.IsEmpty())
			{
				TCHAR firstChar = trendArrowStr.GetAt(0);
				if (firstChar == _T('↑'))
					memDC.SetTextColor(COLOR_RED_UP);
				else if (firstChar == _T('↓'))
					memDC.SetTextColor(COLOR_GREEN_DOWN);
				else
					memDC.SetTextColor(COLOR_TEXT_MUTED);
				int trendX = avgAreaX - GAP - memDC.GetTextExtent(trendArrowStr).cx;
				memDC.TextOut(trendX, avgAreaY + g_data.RDPI(2), trendArrowStr);
			}

			memDC.SetTextColor(RGB(255, 255, 255));
			int minX = avgAreaX + (thirdWidth - memDC.GetTextExtent(minAvgValueStr).cx) / 2;
			memDC.TextOut(minX, avgAreaY + g_data.RDPI(2), minAvgValueStr);

			int avgX = avgAreaX + thirdWidth + (thirdWidth - memDC.GetTextExtent(avgValueStr).cx) / 2;
			memDC.TextOut(avgX, avgAreaY + g_data.RDPI(2), avgValueStr);

			int maxX = avgAreaX + thirdWidth * 2 + (thirdWidth - memDC.GetTextExtent(maxAvgValueStr).cx) / 2;
			memDC.TextOut(maxX, avgAreaY + g_data.RDPI(2), maxAvgValueStr);

			memDC.SelectObject(pPrevFont);
		}
	}
	else
	{
		// 非关联模式（默认指数）：等分列宽布局
		const int colWidth = w / max(relatedCount, 1);
		for (int i = 0; i < relatedCount; i++)
		{
			auto stockData = g_data.GetStockData(relatedCodes[i]);
			int colX = i * colWidth;
			int textX = colX + GAP;

			if (stockData && stockData->info.is_ok)
			{
				const auto& info = stockData->info;
				double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
				double diff = displayPrice - info.prevClosePrice;
				double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;

				CString nameStr = info.GetStockListName();
				CString priceStr;
				priceStr.Format(_T("%.2f"), displayPrice);
				CString changeStr;
				if (diff >= 0)
					changeStr.Format(_T("+%.2f%%"), diffPercent);
				else
					changeStr.Format(_T("%.2f%%"), diffPercent);

				memDC.SetTextColor(COLOR_TEXT_MUTED);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;

				memDC.SetTextColor(diffPercent >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), priceStr);
				textX += memDC.GetTextExtent(priceStr).cx + GAP;

				memDC.SetTextColor(diffPercent >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), changeStr);
			}
			else
			{
				CString nameStr = (relatedCodes[i] == L"sh000001") ? _T("上证:") : ((relatedCodes[i] == L"sz399986") ? _T("中证银行:") : ((relatedCodes[i] == L"rt_hkHSTECH") ? _T("恒生科技:") : CString(relatedCodes[i].c_str()) + _T(":")));
				memDC.SetTextColor(COLOR_TEXT_DIM);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr + _T(" --"));
			}
		}
	}

	if (pOldFont)
		memDC.SelectObject(pOldFont);
	dynFont.DeleteObject();
	avgFont.DeleteObject();
}

static CString GetIndexDisplayName(const std::wstring& code, const CString& defaultName)
{
	if (code == L"sh000001") return _T("上证");
	if (code == L"sz399001") return _T("深证");
	if (code == L"sz399006") return _T("创业板指");
	if (code == L"sh000688") return _T("科创50");
	if (code == L"sh000300") return _T("沪深300");
	if (code == L"sz399303" || code == L"si932000" || code == L"sh932000") return _T("中证2000");
	return defaultName;
}

void CStatusBarPanel::DrawSystemStatusBar(CDC& memDC, int w, int bottomBarY, int singleBarHeight)
{
	// 绘制底部系统状态栏深色背景 (#161820)
	memDC.FillSolidRect(0, bottomBarY, w, singleBarHeight, COLOR_BG_FOOTER);
	memDC.FillSolidRect(0, bottomBarY, w, 1, COLOR_DARK_GRAY_BORDER);

	const int GAP = g_data.RDPI(3);

	std::vector<std::wstring> statusBarCodes = g_data.GetStatusBarStockCodes();
	if (statusBarCodes.empty())
	{
		// 默认指数：上证，深证，创业板指，科创50，沪深300，中证2000
		statusBarCodes = { L"sh000001", L"sz399001", L"sz399006", L"sh000688", L"sh000300", L"sz399303" };
	}
	const int sbCount = static_cast<int>(statusBarCodes.size());
	const int colWidth = w / max(sbCount, 1);

	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	for (int i = 0; i < sbCount; i++)
	{
		auto stockData = g_data.GetStockData(statusBarCodes[i]);
		int colX = i * colWidth;
		int textX = colX + GAP;
		CString defaultName = (stockData && !stockData->info.displayName.empty()) ? stockData->info.GetStockListName() : statusBarCodes[i].c_str();
		CString nameStr = GetIndexDisplayName(statusBarCodes[i], defaultName);

		if (stockData && stockData->info.is_ok && (stockData->info.currentPrice > 0 || stockData->info.prevClosePrice > 0))
		{
			const auto& info = stockData->info;
			double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
			double diff = displayPrice - info.prevClosePrice;
			double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;

			CString priceStr;
			priceStr.Format(_T("%.2f"), displayPrice);
			CString changeStr;
			if (diff >= 0)
				changeStr.Format(_T("+%.2f%%"), diffPercent);
			else
				changeStr.Format(_T("%.2f%%"), diffPercent);

			memDC.SetTextColor(COLOR_TEXT_MUTED);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), nameStr);
			textX += memDC.GetTextExtent(nameStr).cx + GAP;

			memDC.SetTextColor(diffPercent >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), priceStr);
			textX += memDC.GetTextExtent(priceStr).cx + GAP;

			memDC.SetTextColor(diffPercent >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), changeStr);
		}
		else
		{
			memDC.SetTextColor(COLOR_TEXT_DIM);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), nameStr + _T(" --"));
		}
	}
}

