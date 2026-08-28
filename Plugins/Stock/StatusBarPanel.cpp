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

	// 标题格式：(股票代码)股票名称：
	CString prefixTxt;
	prefixTxt.Format(_T("%s:"), realtimeData.displayName.c_str());

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

	// 计算总宽度，在整个标题栏水平居中
	CSize prefixSize = memDC.GetTextExtent(prefixTxt);
	CSize currentSize = memDC.GetTextExtent(currentTxt);
	CSize diffSize = memDC.GetTextExtent(diffTxt);
	CSize macdSize = memDC.GetTextExtent(macdTxt);
	int totalWidth = prefixSize.cx + currentSize.cx + diffSize.cx + macdSize.cx;

	int startX = (windowWidth - totalWidth) / 2;
	int centerY = headerHeight / 2;

	memDC.SetTextColor(COLOR_GRAY_TEXT);
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
		// 信号颜色：正T=红色，反T=绿色，持有=橙色，观望=灰色
		COLORREF macdColor;
		if (macdTrendSignal == _T("正T"))
			macdColor = RGB(255, 50, 50);
		else if (macdTrendSignal == _T("反T"))
			macdColor = RGB(0, 180, 0);
		else if (macdTrendSignal == _T("持有"))
			macdColor = RGB(255, 165, 0);
		else
			macdColor = RGB(128, 128, 128);
		memDC.SetTextColor(macdColor);
		memDC.TextOut(curX, centerY - macdSize.cy / 2, macdTxt);
	}
}

void CStatusBarPanel::DrawRelatedStockBar(CDC& memDC, int w, int topBarY, int singleBarHeight, const std::wstring& stockId, int viewMode)
{
	const int GAP = 2;

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
			// 右侧预留120像素不参与均分，仅左侧区域用于字体计算
			// 趋势箭头在120像素区域左侧，也需要预留空间
			int trendArrowWidth = 0;
			if (showAvgDiff && !trendArrowStr.IsEmpty())
			{
				// 用最大字号测量趋势箭头宽度（趋势箭头使用avgFont固定字号）
				CFont tmpAvgFont;
				tmpAvgFont.CreateFont(-g_data.RDPI(maxFont), 0, 0, 0, FW_NORMAL, 0, 0, 0,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
				CFont* pOldTmpFont = memDC.SelectObject(&tmpAvgFont);
				trendArrowWidth = memDC.GetTextExtent(trendArrowStr).cx + GAP;
				memDC.SelectObject(pOldTmpFont);
				tmpAvgFont.DeleteObject();
			}
			int availableWidth = w - (showAvgDiff ? g_data.RDPI(120) : 0) - trendArrowWidth;

			std::lock_guard<std::mutex> lockMeasure(Stock::Instance().m_stockDataMutex);

			// 预先构建所有文本字符串
			struct StockText { CString nameStr; CString changeStr; bool valid; };
			std::vector<StockText> stockTexts(relatedCount);
			for (int i = 0; i < relatedCount; i++)
			{
				auto stockData = g_data.GetStockData(relatedCodes[i]);
				if (stockData && stockData->info.is_ok)
				{
					const auto& info = stockData->info;
					double displayPrice = info.currentPrice > 0 ? info.currentPrice : info.prevClosePrice;
					double diff = displayPrice - info.prevClosePrice;
					double diffPercent = info.prevClosePrice != 0 ? (diff / info.prevClosePrice) * 100 : 0;
					stockTexts[i].nameStr = info.GetStockListName() + _T(":");
					if (diff >= 0)
						stockTexts[i].changeStr.Format(_T("+%.2f%%"), diffPercent);
					else
						stockTexts[i].changeStr.Format(_T("%.2f%%"), diffPercent);
					stockTexts[i].valid = true;
				}
				else
				{
					stockTexts[i].nameStr = CString(relatedCodes[i].c_str()) + _T(" --");
					stockTexts[i].valid = false;
				}
			}

			int fontSize = maxFont;

			while (fontSize >= minFont)
			{
				// 创建临时字体测量
				CFont tmpFont;
				tmpFont.CreateFont(-g_data.RDPI(fontSize), 0, 0, 0, FW_NORMAL, 0, 0, 0,
					DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
				CFont* pOldTmp = memDC.SelectObject(&tmpFont);

				// 计算所有股票文字总宽度（已包含每个元素后的GAP间隔）
				int totalWidth = GAP; // 左侧起始边距
				for (int i = 0; i < relatedCount; i++)
				{
					totalWidth += memDC.GetTextExtent(stockTexts[i].nameStr).cx + GAP;
					if (stockTexts[i].valid)
						totalWidth += memDC.GetTextExtent(stockTexts[i].changeStr).cx + GAP;
				}

				memDC.SelectObject(pOldTmp);
				tmpFont.DeleteObject();

				// 检查是否适合左侧可用区域（去掉预留120像素）
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

				memDC.SetTextColor(COLOR_BLACK);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;

				memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), changeStr);
				textX += memDC.GetTextExtent(changeStr).cx + GAP;
			}
			else
			{
				CString nameStr = CString(relatedCodes[i].c_str()) + _T(" --");
				memDC.SetTextColor(COLOR_GRAY_PURPLE);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;
			}
		}

		// 右侧预留120像素显示：最小值 均值 最大值（固定字体大小，红绿背景）
		if (showAvgDiff)
		{
			int avgAreaWidth = g_data.RDPI(120);
			int avgAreaX = w - avgAreaWidth - GAP;
			int avgAreaH = singleBarHeight;
			int avgAreaY = topBarY;

			// 红绿颜色分3档，由浅到深
			static const COLORREF AVG_RED_COLORS[] = {
				RGB(240, 40, 40),   // 浅红
				RGB(180, 50, 50),   // 中红
				RGB(130, 20, 40)    // 深红
			};
			static const COLORREF AVG_GREEN_COLORS[] = {
				RGB(40, 240, 40),   // 浅绿
				RGB(50, 180, 50),   // 中绿
				RGB(20, 130, 40)    // 深绿
			};

			// 红绿颜色深度由均值在区间中的位置决定
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

			// 绘制红绿背景
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
				double ratio = (avgDiffPercent - minAvgDiff) / range;
				int redWidth = static_cast<int>(ratio * avgAreaWidth);
				redWidth = max(0, min(redWidth, avgAreaWidth));
				int greenWidth = avgAreaWidth - redWidth;
				if (redWidth >= greenWidth)
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

			// 切换到固定字体绘制均幅区域
			CFont* pPrevFont = memDC.SelectObject(&avgFont);

			// 120像素区域三等分
			int thirdWidth = avgAreaWidth / 3;

			// 趋势指示器（120像素左侧，间隔2像素，无背景）
			if (!trendArrowStr.IsEmpty())
			{
				TCHAR firstChar = trendArrowStr.GetAt(0);
				if (firstChar == _T('↑'))
					memDC.SetTextColor(COLOR_RED_UP);
				else if (firstChar == _T('↓'))
					memDC.SetTextColor(COLOR_GREEN_DOWN);
				else
					memDC.SetTextColor(RGB(0, 0, 0));
				int trendX = avgAreaX - GAP - memDC.GetTextExtent(trendArrowStr).cx;
				memDC.TextOut(trendX, avgAreaY + g_data.RDPI(2), trendArrowStr);
			}

			// 最小值（白色文字）
			memDC.SetTextColor(RGB(255, 255, 255));
			int minX = avgAreaX + (thirdWidth - memDC.GetTextExtent(minAvgValueStr).cx) / 2;
			memDC.TextOut(minX, avgAreaY + g_data.RDPI(2), minAvgValueStr);

			// 均值（白色文字）
			int avgX = avgAreaX + thirdWidth + (thirdWidth - memDC.GetTextExtent(avgValueStr).cx) / 2;
			memDC.TextOut(avgX, avgAreaY + g_data.RDPI(2), avgValueStr);

			// 最大值（白色文字）
			int maxX = avgAreaX + thirdWidth * 2 + (thirdWidth - memDC.GetTextExtent(maxAvgValueStr).cx) / 2;
			memDC.TextOut(maxX, avgAreaY + g_data.RDPI(2), maxAvgValueStr);

			// 恢复之前的字体
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

				memDC.SetTextColor(COLOR_BLACK);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr);
				textX += memDC.GetTextExtent(nameStr).cx + GAP;

				memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), priceStr);
				textX += memDC.GetTextExtent(priceStr).cx + GAP;

				memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), changeStr);
			}
			else
			{
				CString nameStr = CString(relatedCodes[i].c_str());
				memDC.SetTextColor(COLOR_GRAY_PURPLE);
				memDC.TextOut(textX, topBarY + g_data.RDPI(2), nameStr + _T(" --"));
			}
		}
	}

	// 恢复原始字体
	if (pOldFont != nullptr)
	{
		memDC.SelectObject(pOldFont);
		dynFont.DeleteObject();
	}
}

void CStatusBarPanel::DrawSystemStatusBar(CDC& memDC, int w, int bottomBarY, int singleBarHeight)
{
	const int GAP = 2;

	std::vector<std::wstring> statusBarCodes = g_data.GetStatusBarStockCodes();
	if (statusBarCodes.empty())
	{
		// 没有配置时使用默认指数：上证指数、中证银行、恒生科技
		statusBarCodes = { L"sh000001", L"sz399986", L"rt_hkHSTECH" };
	}
	const int sbCount = static_cast<int>(statusBarCodes.size());
	// 下行状态栏使用等分列宽布局
	const int colWidth = w / max(sbCount, 1);

	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	for (int i = 0; i < sbCount; i++)
	{
		auto stockData = g_data.GetStockData(statusBarCodes[i]);
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

			memDC.SetTextColor(COLOR_BLACK);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), nameStr);
			textX += memDC.GetTextExtent(nameStr).cx + GAP;

			memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), priceStr);
			textX += memDC.GetTextExtent(priceStr).cx + GAP;

			memDC.SetTextColor(CCommon::GetProfitLossColor(diffPercent));
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), changeStr);
		}
		else
		{
			CString nameStr = CString(statusBarCodes[i].c_str());
			memDC.SetTextColor(COLOR_GRAY_PURPLE);
			memDC.TextOut(textX, bottomBarY + g_data.RDPI(2), nameStr + _T(" --"));
		}
	}
}

