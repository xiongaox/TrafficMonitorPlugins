#include "pch.h"
#include "OrderBookPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "SignalAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <map>

// 静态成员初始化
const COLORREF COrderBookPanel::NET_RATIO_RED_COLORS[] = {
	RGB(251, 113, 133),  // 0-30 浅柔红
	RGB(244, 63, 94),    // 30-60 现代红
	RGB(190, 18, 60)     // 60以上 强红
};
const COLORREF COrderBookPanel::NET_RATIO_GREEN_COLORS[] = {
	RGB(52, 211, 153),   // 0~30 浅薄荷绿
	RGB(16, 185, 129),   // 30~60 现代绿
	RGB(5, 150, 105)     // 60以上 强绿
};
std::map<std::wstring, double> COrderBookPanel::m_lastNetRatioMap;
std::map<std::wstring, CString> COrderBookPanel::m_lastNetRatioTrendMap;
std::map<std::wstring, std::map<int, double>> COrderBookPanel::m_lastPeriodRatioMap;
std::map<std::wstring, std::map<int, CString>> COrderBookPanel::m_lastPeriodRatioTrendMap;

void COrderBookPanel::Draw(CDC& memDC, int left, int right, int height, const STOCK::StockInfo& stockInfo,
	const std::vector<STOCK::KLinePoint>& klineData,
	UIViewMode viewMode)
{
	(void)viewMode;
	// 现代极简14行呼吸感布局：
	// 行0: 委比 + 胶囊进度条 + 百分比
	// 行1-5: 卖五 ~ 卖一 (带挂单深度条与语义红)
	// 行6: 中间行情行 (现价 与 今开)
	// 行7-11: 买一 ~ 买五 (带挂单深度条与语义绿)
	// 行12: 净比 05 与 99 统计
	// 行13: 振幅与换手率
	const int totalRows = 14;
	const int headerHeight = g_data.RDPI(26) + g_data.RDPI(20);  // 主标题栏+管理股票栏高度
	const int obTitleH = g_data.RDPI(16);       // 盘口标题栏高度
	const int topOffset = headerHeight + obTitleH;
	const int panelW = right - left;

	// 绘制盘口标题栏背景 (#181B22)
	memDC.FillSolidRect(left, headerHeight, panelW, obTitleH, COLOR_BG_HEADER);
	const int rowHeight = (height - obTitleH) / totalRows;
	const int contentH = height - obTitleH;
	const int rem = contentH % totalRows;
	const int textX = left + g_data.RDPI(6);

	// 填充内容区域深色背景 (#14161D)
	memDC.FillSolidRect(left, topOffset, panelW, contentH, COLOR_BG_PANEL);
	memDC.SetBkMode(TRANSPARENT);

	m_stockDataForAccum = g_data.GetStockData(stockInfo.code);

	LayoutContext lc;
	lc.left = left;
	lc.right = right;
	lc.height = height;
	lc.headerHeight = headerHeight;
	lc.obTitleH = obTitleH;
	lc.topOffset = topOffset;
	lc.panelW = panelW;
	lc.totalRows = totalRows;
	lc.rowHeight = rowHeight;
	lc.contentH = contentH;
	lc.rem = rem;
	lc.textX = textX;

	DWORD tickCount = GetTickCount();
	bool blinkOn = (tickCount / 500) % 2 == 0;

	DrawWeiBi(memDC, lc, stockInfo);
	DrawAskRows(memDC, lc, stockInfo, blinkOn);
	DrawMidPriceRow(memDC, lc, stockInfo);
	DrawBidRows(memDC, lc, stockInfo, blinkOn);
	DrawNetRatioRows(memDC, lc, stockInfo);
	DrawStatsRow(memDC, lc, stockInfo, klineData);
}

// ============================================================================
// 绘制委比（行0）
// ============================================================================
void COrderBookPanel::DrawWeiBi(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	const int MAX_LEVEL = STOCK::StockInfo::MAX_LEVEL;
	STOCK::Volume bidTotal = 0;
	STOCK::Volume askTotal = 0;
	for (int i = 0; i < MAX_LEVEL; i++)
	{
		bidTotal += stockInfo.bidLevels[i].volume / 100;
		askTotal += stockInfo.askLevels[i].volume / 100;
	}

	double wbRatio = 0.0;
	if (bidTotal + askTotal > 0)
	{
		wbRatio = (double)(bidTotal - askTotal) / (bidTotal + askTotal) * 100;
	}

	int rowY = lc.RowY(0);
	int rowH = lc.RowH(0);
	int textY = rowY + max(0, (rowH - memDC.GetTextExtent(_T("Ay")).cy) / 2);

	// 1. 左侧：委比标签
	memDC.SetTextColor(COLOR_TEXT_DIM);
	memDC.TextOut(lc.textX, textY, _T("委比"));
	int labelW = memDC.GetTextExtent(_T("委比 ")).cx;

	// 2. 右侧：百分比文本
	CString wbTxt;
	if (wbRatio > 0) wbTxt.Format(_T("+%.2f%%"), wbRatio);
	else if (wbRatio < 0) wbTxt.Format(_T("%.2f%%"), wbRatio);
	else wbTxt = _T("0.00%");
	CSize valSize = memDC.GetTextExtent(wbTxt);
	int valX = lc.right - valSize.cx - g_data.RDPI(6);
	memDC.SetTextColor(wbRatio > 0 ? COLOR_RED_UP : (wbRatio < 0 ? COLOR_GREEN_DOWN : COLOR_TEXT_MUTED));
	memDC.TextOut(valX, textY, wbTxt);

	// 3. 中间：胶囊进度条
	int barX = lc.textX + labelW + g_data.RDPI(2);
	int barW = valX - barX - g_data.RDPI(4);
	if (barW > g_data.RDPI(10))
	{
		int barH = max(g_data.RDPI(4), rowH / 4);
		int barY = rowY + (rowH - barH) / 2;
		DrawRatioBar(memDC, barX, barY, barW, barH, wbRatio);
	}
}

// ============================================================================
// 绘制卖盘行（卖五~卖一，行1-5）
// ============================================================================
void COrderBookPanel::DrawAskRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, bool blinkOn)
{
	std::vector<OrderBookRow> priceRows;
	priceRows.reserve(5);

	for (int idx = 4; idx >= 0; --idx)
	{
		STOCK::Price price = stockInfo.askLevels[idx].price;
		STOCK::Volume delta = GetOrderDeltaLots(price);
		priceRows.push_back(BuildAskRow(stockInfo, idx, delta));
	}

	DrawPriceRows(memDC, lc, priceRows, 1, blinkOn);
}

// ============================================================================
// 绘制中间行情行：现价与今开（行6）
// ============================================================================
void COrderBookPanel::DrawMidPriceRow(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	int y = lc.RowY(6);
	int h = lc.RowH(6);
	int totalW = lc.right - lc.left;

	// 行背景与上下细微边框
	memDC.FillSolidRect(lc.left, y, totalW, h, COLOR_BG_HEADER);
	memDC.FillSolidRect(lc.left, y, totalW, 1, COLOR_DARK_GRAY_BORDER);
	memDC.FillSolidRect(lc.left, y + h - 1, totalW, 1, COLOR_DARK_GRAY_BORDER);

	int textY = y + max(0, (h - memDC.GetTextExtent(_T("Ay")).cy) / 2);

	// 现价
	CString curPriceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(stockInfo.currentPrice) : CCommon::FormatFloat(stockInfo.currentPrice);
	COLORREF curColor = (stockInfo.currentPrice >= stockInfo.prevClosePrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	memDC.SetTextColor(COLOR_TEXT_DIM);
	memDC.TextOut(lc.textX, textY, _T("现价: "));
	int curX = lc.textX + memDC.GetTextExtent(_T("现价: ")).cx;
	memDC.SetTextColor(curColor);
	memDC.TextOut(curX, textY, curPriceStr);

	// 今开
	CString openPriceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(stockInfo.openPrice) : CCommon::FormatFloat(stockInfo.openPrice);
	COLORREF openColor = (stockInfo.openPrice >= stockInfo.prevClosePrice) ? COLOR_RED_UP : COLOR_GREEN_DOWN;
	CString openLabel = _T("今开: ");
	CSize openValSize = memDC.GetTextExtent(openPriceStr);
	CSize openLblSize = memDC.GetTextExtent(openLabel);
	int openX = lc.right - openValSize.cx - openLblSize.cx - g_data.RDPI(6);
	memDC.SetTextColor(COLOR_TEXT_DIM);
	memDC.TextOut(openX, textY, openLabel);
	memDC.SetTextColor(openColor);
	memDC.TextOut(openX + openLblSize.cx, textY, openPriceStr);
}

// ============================================================================
// 绘制买盘行（买一~买五，行7-11）
// ============================================================================
void COrderBookPanel::DrawBidRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo, bool blinkOn)
{
	std::vector<OrderBookRow> bottomRows;
	bottomRows.reserve(5);

	for (int i = 0; i < 5; i++)
	{
		STOCK::Price price = stockInfo.bidLevels[i].price;
		STOCK::Volume delta = GetOrderDeltaLots(price);
		bottomRows.push_back(BuildBidRow(stockInfo, i, delta));
	}

	DrawPriceRows(memDC, lc, bottomRows, 7, blinkOn);
}

// ============================================================================
// 绘制净比汇总（行12）
// ============================================================================
void COrderBookPanel::DrawNetRatioRows(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo)
{
	int y = lc.RowY(12);
	int h = lc.RowH(12);
	int textY = y + max(0, (h - memDC.GetTextExtent(_T("Ay")).cy) / 2);

	auto stockDataPtr = g_data.GetStockData(stockInfo.code);
	STOCK::Volume diff05 = 0;
	double ratio05 = 0;
	bool has05 = stockDataPtr && stockDataPtr->GetInnerOuterNetDiff(5, diff05, ratio05);

	STOCK::Volume diff99 = 0;
	double ratio99 = 0;
	bool has99 = stockDataPtr && stockDataPtr->GetInnerOuterNetDiff(99, diff99, ratio99);

	// 净比 05
	memDC.SetTextColor(COLOR_TEXT_DIM);
	memDC.TextOut(lc.textX, textY, _T("净比 05: "));
	int n05X = lc.textX + memDC.GetTextExtent(_T("净比 05: ")).cx;
	if (has05)
	{
		COLORREF net05Color = diff05 >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CString diffStr = CCommon::FormatVolumeInt(std::abs(diff05) / 100.0);
		CString n05Val = CString(diff05 >= 0 ? _T("+") : _T("-")) + diffStr + _T("万");
		memDC.SetTextColor(net05Color);
		memDC.TextOut(n05X, textY, n05Val);
	}
	else
	{
		memDC.SetTextColor(COLOR_TEXT_MUTED);
		memDC.TextOut(n05X, textY, _T("--"));
	}

	// 净比 99
	if (has99)
	{
		COLORREF net99Color = diff99 >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN;
		CString diffStr = CCommon::FormatVolumeInt(std::abs(diff99) / 100.0);
		CString n99Val = CString(diff99 >= 0 ? _T("+") : _T("-")) + diffStr + _T("万");
		CString full99 = _T("99: ") + n99Val;
		CSize full99Size = memDC.GetTextExtent(full99);
		int n99X = lc.right - full99Size.cx - g_data.RDPI(6);
		memDC.SetTextColor(COLOR_TEXT_DIM);
		memDC.TextOut(n99X, textY, _T("99: "));
		int n99ValX = n99X + memDC.GetTextExtent(_T("99: ")).cx;
		memDC.SetTextColor(net99Color);
		memDC.TextOut(n99ValX, textY, n99Val);
	}
}

// ============================================================================
// 绘制振幅与换手率（行13）
// ============================================================================
void COrderBookPanel::DrawStatsRow(CDC& memDC, const LayoutContext& lc, const STOCK::StockInfo& stockInfo,
	const std::vector<STOCK::KLinePoint>& klineData)
{
	(void)klineData;
	int y = lc.RowY(13);
	int h = lc.RowH(13);
	int textY = y + max(0, (h - memDC.GetTextExtent(_T("Ay")).cy) / 2);

	// 振幅
	double amplitude = 0.0;
	if (stockInfo.prevClosePrice > 0 && stockInfo.highPrice > 0 && stockInfo.lowPrice > 0)
	{
		amplitude = (stockInfo.highPrice - stockInfo.lowPrice) / stockInfo.prevClosePrice * 100.0;
	}
	CString ampStr;
	ampStr.Format(_T("振幅: %.2f%%"), amplitude);
	memDC.SetTextColor(COLOR_TEXT_DIM);
	memDC.TextOut(lc.textX, textY, ampStr);

	// 换手率
	CString toStr;
	toStr.Format(_T("换手: %.2f%%"), stockInfo.turnoverRate);
	CSize toSize = memDC.GetTextExtent(toStr);
	int toX = lc.right - toSize.cx - g_data.RDPI(6);
	memDC.SetTextColor(COLOR_TEXT_DIM);
	memDC.TextOut(toX, textY, toStr);
}

// ============================================================================
// 辅助函数
// ============================================================================

void COrderBookPanel::DrawOrderBookRowText(CDC& memDC, const OrderBookRow& row, int x, int y, int rowWidth, bool blinkOff)
{
	COLORREF textColor;
	if (row.darkBackground && !(row.blink && blinkOff))
		textColor = RGB(255, 255, 255);
	else
		textColor = row.textColor;
	memDC.SetTextColor(textColor);

	// 粗体字体
	CFont* pOldFont = nullptr;
	CFont boldFont;
	if (row.bold)
	{
		pOldFont = memDC.GetCurrentFont();
		LOGFONT lf;
		pOldFont->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
		memDC.SelectObject(&boldFont);
	}

	memDC.TextOut(x, y, row.text);

	if (row.bold && pOldFont)
		memDC.SelectObject(pOldFont);
	if (!row.drawSmallSuffix || row.smallSuffix.IsEmpty())
	{
		// 只有右对齐后缀
		if (!row.rightAlignSuffix.IsEmpty())
		{
			CFont* oldFont = memDC.GetCurrentFont();
			LOGFONT lf;
			oldFont->GetLogFont(&lf);
			lf.lfHeight = lf.lfHeight * 3 / 4;
			CFont smallFont;
			smallFont.CreateFontIndirect(&lf);
			memDC.SelectObject(&smallFont);
			memDC.SetTextColor(row.rightAlignSuffixColor);
			int suffixW = memDC.GetTextExtent(row.rightAlignSuffix).cx;
			memDC.TextOut(x + rowWidth - suffixW - g_data.RDPI(4), y + g_data.RDPI(1), row.rightAlignSuffix);
			memDC.SelectObject(oldFont);
		}
		return;
	}

	int suffixX = x + memDC.GetTextExtent(row.text).cx;
	CFont* oldFont = memDC.GetCurrentFont();
	LOGFONT lf;
	oldFont->GetLogFont(&lf);
	lf.lfHeight = lf.lfHeight * 3 / 4;
	CFont smallFont;
	smallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&smallFont);
	memDC.SetTextColor(row.darkBackground ? RGB(255, 255, 200) : textColor);
	memDC.TextOut(suffixX, y + g_data.RDPI(1), row.smallSuffix);
	// 右对齐绘制累计成交量和瞬时变化量
	// 布局：[...smallSuffix] [rightAlignSuffix] [cumVolSuffix] 右边距
	int rightEdge = x + rowWidth - g_data.RDPI(4);
	if (!row.cumVolSuffix.IsEmpty())
	{
		memDC.SetTextColor(row.cumVolSuffixColor);
		int cumVolW = memDC.GetTextExtent(row.cumVolSuffix).cx;
		memDC.TextOut(rightEdge - cumVolW, y + g_data.RDPI(1), row.cumVolSuffix);
		rightEdge -= cumVolW;
	}
	if (!row.rightAlignSuffix.IsEmpty())
	{
		memDC.SetTextColor(row.rightAlignSuffixColor);
		int raSuffixW = memDC.GetTextExtent(row.rightAlignSuffix).cx;
		memDC.TextOut(rightEdge - raSuffixW, y + g_data.RDPI(1), row.rightAlignSuffix);
	}
	memDC.SelectObject(oldFont);
}

void COrderBookPanel::DrawRatioBar(CDC& memDC, int x, int y, int w, int h, double ratio)
{
	if (w <= 0)
		return;
	COLORREF redColor = NET_RATIO_RED_COLORS[GetNetRatioColorIndex(ratio)];
	COLORREF greenColor = NET_RATIO_GREEN_COLORS[GetNetRatioColorIndex(ratio)];
	int midX = x + w / 2;
	int halfW = w / 2;
	int fillW = static_cast<int>(std::sqrt(std::abs(ratio) / 100.0) * halfW);
	fillW = min(fillW, halfW);
	memDC.FillSolidRect(x, y, w, h, RGB(28, 32, 42));
	int dominantW = min(w, halfW + fillW);
	if (ratio > 0)
	{
		memDC.FillSolidRect(x, y, dominantW, h, redColor);
		memDC.FillSolidRect(x + dominantW, y, w - dominantW, h, greenColor);
	}
	else if (ratio < 0)
	{
		memDC.FillSolidRect(x, y, dominantW, h, greenColor);
		memDC.FillSolidRect(x + dominantW, y, w - dominantW, h, redColor);
	}
	memDC.FillSolidRect(midX - 1, y, 2, h, COLOR_DARK_GRAY_BORDER);
	CPen borderPen(PS_SOLID, 1, COLOR_DARK_GRAY_BORDER);
	CPen* oldPen = memDC.SelectObject(&borderPen);
	CBrush* oldBrush = static_cast<CBrush*>(memDC.SelectStockObject(NULL_BRUSH));
	memDC.Rectangle(x, y, x + w, y + h);
	memDC.SelectObject(oldBrush);
	memDC.SelectObject(oldPen);
}

void COrderBookPanel::DrawNetRatioBarText(CDC& memDC, int x, int y, int w, int h, const CString& ratioText, const CString& diffText)
{
	CFont* oldFont = memDC.GetCurrentFont();
	LOGFONT lf;
	oldFont->GetLogFont(&lf);
	lf.lfHeight = lf.lfHeight * 27 / 32;
	CFont smallFont;
	smallFont.CreateFontIndirect(&lf);
	memDC.SelectObject(&smallFont);
	memDC.SetTextColor(RGB(255, 255, 255));
	int vCenter = max(0, (h - memDC.GetTextExtent(ratioText).cy) / 2);
	memDC.TextOut(x + g_data.RDPI(3), y + vCenter, ratioText);
	CSize diffSize = memDC.GetTextExtent(diffText);
	memDC.TextOut(x + w - diffSize.cx - g_data.RDPI(3), y + vCenter, diffText);
	memDC.SelectObject(oldFont);
}

int COrderBookPanel::GetNetRatioColorIndex(double ratio)
{
	double absRatioValue = std::abs(ratio);
	if (absRatioValue <= 30) return 0;
	if (absRatioValue <= 60) return 1;
	return 2;
}

STOCK::Volume COrderBookPanel::GetOrderDeltaLots(STOCK::Price price)
{
	if (!m_stockDataForAccum || price <= 0)
		return 0;
	auto it = m_stockDataForAccum->orderPriceAccumMap.find(price);
	if (it == m_stockDataForAccum->orderPriceAccumMap.end())
		return 0;
	return it->second.deltaVolume / 100;
}

STOCK::Volume COrderBookPanel::GetOrderBookCumVol(STOCK::Price price) const
{
	if (!m_stockDataForAccum || price <= 0)
		return 0;
	auto it = m_stockDataForAccum->orderBookCumVolMap.find(price);
	if (it == m_stockDataForAccum->orderBookCumVolMap.end())
		return 0;
	return it->second.cumVolume;
}

CString COrderBookPanel::CalcNetRatioTrend(double ratio, double previousRatio)
{
	double absRatio = std::abs(ratio);
	double previousAbsRatio = std::abs(previousRatio);
	if (absRatio > previousAbsRatio)
		return _T("↑");
	else if (absRatio < previousAbsRatio)
		return _T("↓");
	return _T("");
}

COrderBookPanel::OrderBookRow COrderBookPanel::BuildAskRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const
{
	STOCK::Price price = stockInfo.askLevels[idx].price;
	STOCK::Volume volume = stockInfo.askLevels[idx].volume / 100;
	CString volumeStr = CCommon::FormatVolumeInt(volume);
	CString priceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(price) : CCommon::FormatFloat(price);

	OrderBookRow row;
	row.level = idx + 1;
	row.price = price;
	row.volume = volume;
	row.isAsk = true;
	row.priceStr = priceStr;
	row.volumeStr = volumeStr;
	row.textColor = COLOR_RED_UP;
	row.bold = (stockInfo.highPrice > 0 && price > 0 && price == stockInfo.highPrice);
	// 卖一当前价高亮
	row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
	row.backgroundColor = COLOR_DEPTH_SELL_HL;
	if (idx == 0 && row.fillBackground && volume <= 10000)
		row.blink = true;

	return row;
}

COrderBookPanel::OrderBookRow COrderBookPanel::BuildBidRow(const STOCK::StockInfo& stockInfo, int idx, STOCK::Volume delta) const
{
	STOCK::Price price = stockInfo.bidLevels[idx].price;
	STOCK::Volume volume = stockInfo.bidLevels[idx].volume / 100;
	CString volumeStr = CCommon::FormatVolumeInt(volume);
	CString priceStr = stockInfo.IsETF() ? CCommon::FormatETFPrice(price) : CCommon::FormatFloat(price);

	OrderBookRow row;
	row.level = idx + 1;
	row.price = price;
	row.volume = volume;
	row.isAsk = false;
	row.priceStr = priceStr;
	row.volumeStr = volumeStr;
	row.textColor = COLOR_GREEN_DOWN;
	row.bold = (stockInfo.lowPrice > 0 && price > 0 && price == stockInfo.lowPrice);
	// 买一当前价高亮
	row.fillBackground = (stockInfo.currentPrice > 0 && price > 0 && stockInfo.currentPrice == price);
	row.backgroundColor = COLOR_DEPTH_BUY_HL;
	if (idx == 0 && row.fillBackground && volume <= 10000)
		row.blink = true;

	return row;
}

void COrderBookPanel::DrawPriceRows(CDC& memDC, const LayoutContext& lc, const std::vector<OrderBookRow>& rows, int startRow, bool blinkOn)
{
	STOCK::Volume maxVol = 1;
	for (const auto& r : rows)
	{
		if (r.volume > maxVol)
			maxVol = r.volume;
	}

	int totalW = lc.right - lc.left;

	for (int i = 0; i < static_cast<int>(rows.size()); i++)
	{
		int y = lc.RowY(startRow + i);
		int h = lc.RowH(startRow + i);

		// 1. 绘制基础行背景 (暗黑面板底色)
		memDC.FillSolidRect(lc.left, y, totalW, h, COLOR_BG_PANEL);

		// 2. 绘制挂单量深度条（Depth Bar 从右往左按挂单量比例绘制）
		if (rows[i].volume > 0 && maxVol > 0)
		{
			int depthW = static_cast<int>((double)rows[i].volume / maxVol * (totalW * 0.60));
			depthW = max(g_data.RDPI(2), min(totalW, depthW));
			COLORREF depthColor = rows[i].isAsk ? COLOR_DEPTH_SELL_BG : COLOR_DEPTH_BUY_BG;
			memDC.FillSolidRect(lc.right - depthW, y + 1, depthW, h - 2, depthColor);
		}

		if (rows[i].fillBackground)
		{
			if (!(rows[i].blink && !blinkOn))
				memDC.FillSolidRect(lc.left, y, totalW, h, rows[i].backgroundColor);
		}

		int textY = y + max(0, (h - memDC.GetTextExtent(_T("Ay")).cy) / 2);

		// 3. 绘制三列布局：档位 | 价格 | 挂单量
		// 列1：档位标签（卖5 / 买1）
		CString labelTxt = rows[i].isAsk ? (CString(_T("卖")) + std::to_wstring(rows[i].level).c_str()) : (CString(_T("买")) + std::to_wstring(rows[i].level).c_str());
		memDC.SetTextColor(COLOR_TEXT_DIM);
		memDC.TextOut(lc.textX, textY, labelTxt);

		// 列2：价格
		int priceX = lc.textX + g_data.RDPI(28);
		memDC.SetTextColor(rows[i].textColor);
		memDC.TextOut(priceX, textY, rows[i].priceStr);

		// 列3：挂单量（右对齐）
		CSize volSize = memDC.GetTextExtent(rows[i].volumeStr);
		int volX = lc.right - volSize.cx - g_data.RDPI(6);
		memDC.SetTextColor(COLOR_TEXT_MUTED);
		memDC.TextOut(volX, textY, rows[i].volumeStr);
	}
}