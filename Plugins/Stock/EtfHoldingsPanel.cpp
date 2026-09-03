#include "pch.h"
#include "EtfHoldingsPanel.h"
#include "ChartColors.h"
#include "DataManager.h"
#include "StockFont.h"
#include <algorithm>

int CEtfHoldingsPanel::GetRowHeight()
{
	return g_data.RDPI(22);
}

int CEtfHoldingsPanel::GetTableHeaderHeight()
{
	return g_data.RDPI(18);
}

int CEtfHoldingsPanel::HitTest(CPoint pt, int left, int right, int height, int scrollOffset, int itemCount)
{
	const int headerHeight = g_data.RDPI(26);
	const int obTitleH = g_data.RDPI(16);
	const int tableHeaderH = GetTableHeaderHeight();
	const int topOffset = headerHeight + obTitleH;
	const int listTop = topOffset + tableHeaderH;
	const int listBottom = headerHeight + height;
	const int rowH = GetRowHeight();

	if (pt.x < left || pt.x >= right || pt.y < listTop || pt.y >= listBottom)
		return -1;

	int contentY = (pt.y - listTop) + scrollOffset;
	if (contentY < 0) return -1;
	int idx = contentY / rowH;
	if (idx >= 0 && idx < itemCount)
		return idx;
	return -1;
}

void CEtfHoldingsPanel::Draw(CDC& memDC, int left, int right, int height,
	const STOCK::EtfHoldingsData& data, int scrollOffset, const std::wstring& currentStockId)
{
	const int headerHeight = g_data.RDPI(26);
	const int obTitleH = g_data.RDPI(16);
	const int topOffset = headerHeight + obTitleH;
	const int panelW = right - left;
	const int contentH = height - obTitleH;
	if (panelW <= 0 || contentH <= 0) return;

	// 1. 标题栏底色与面板底色
	memDC.FillSolidRect(left, headerHeight, panelW, obTitleH, COLOR_BG_HEADER);
	memDC.FillSolidRect(left, topOffset, panelW, contentH, COLOR_BG_PANEL);
	memDC.SetBkMode(TRANSPARENT);

	// 2. 表头子标题行
	const int tableHeaderH = GetTableHeaderHeight();
	CRect tableHeaderRect(left, topOffset, right, topOffset + tableHeaderH);
	memDC.FillSolidRect(tableHeaderRect, RGB(22, 26, 35));
	// 表头底边线
	memDC.FillSolidRect(left, topOffset + tableHeaderH - 1, panelW, 1, COLOR_DARK_GRAY_BORDER);

	// 字体定义
	CFont headerFont, textFont;
	CreateStockFont(headerFont, memDC, g_data.RDPI(10), FW_NORMAL);
	CreateStockFont(textFont, memDC, g_data.RDPI(11), FW_NORMAL);

	// 列宽度计算（以 panelW 为基准自适应分配）
	// 序号(20) + 名称(44) + 成交量(32) + 今涨跌(38) + 仓位(34) = 168
	const int colSeqW = g_data.RDPI(20);
	const int colVolW = g_data.RDPI(32);
	const int colChgW = g_data.RDPI(38);
	const int colPosW = g_data.RDPI(34);
	const int colNameW = max(g_data.RDPI(42), panelW - (colSeqW + colVolW + colChgW + colPosW));

	const int colSeqX = left;
	const int colNameX = colSeqX + colSeqW;
	const int colVolX = colNameX + colNameW;
	const int colChgX = colVolX + colVolW;
	const int colPosX = colChgX + colChgW;

	// 绘制表头文字
	CFont* pOldFont = memDC.SelectObject(&headerFont);
	memDC.SetTextColor(COLOR_TEXT_MUTED);

	CRect rcSeqH(colSeqX, topOffset, colSeqX + colSeqW, topOffset + tableHeaderH);
	CRect rcNameH(colNameX, topOffset, colNameX + colNameW, topOffset + tableHeaderH);
	CRect rcVolH(colVolX, topOffset, colVolX + colVolW - g_data.RDPI(2), topOffset + tableHeaderH);
	CRect rcChgH(colChgX, topOffset, colChgX + colChgW - g_data.RDPI(2), topOffset + tableHeaderH);
	CRect rcPosH(colPosX, topOffset, right - g_data.RDPI(3), topOffset + tableHeaderH);

	memDC.DrawText(_T("序"), rcSeqH, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	memDC.DrawText(_T("名称"), rcNameH, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	memDC.DrawText(_T("成交量"), rcVolH, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	memDC.DrawText(_T("今涨跌"), rcChgH, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	memDC.DrawText(_T("仓位"), rcPosH, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	// 3. 列表区域
	const int listTop = topOffset + tableHeaderH;
	const int listH = contentH - tableHeaderH;
	if (listH <= 0)
	{
		memDC.SelectObject(pOldFont);
		return;
	}

	CRect listClipRect(left, listTop, right, listTop + listH);
	CRgn clipRgn;
	clipRgn.CreateRectRgn(listClipRect.left, listClipRect.top, listClipRect.right, listClipRect.bottom);
	memDC.SelectClipRgn(&clipRgn);

	if (!data.isValid || data.items.empty())
	{
		memDC.SelectObject(&textFont);
		memDC.SetTextColor(COLOR_TEXT_DIM);
		CRect rcMsg = listClipRect;
		const wchar_t* msg = data.fetchFailed ? _T("暂无持仓数据") : _T("加载持仓数据中...");
		memDC.DrawText(msg, rcMsg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		memDC.SelectClipRgn(nullptr);
		memDC.SelectObject(pOldFont);
		return;
	}

	const int rowH = GetRowHeight();
	memDC.SelectObject(&textFont);

	for (size_t i = 0; i < data.items.size(); ++i)
	{
		int itemY = listTop - scrollOffset + static_cast<int>(i) * rowH;
		if (itemY + rowH <= listTop || itemY >= listTop + listH)
			continue; // 视口外跳过绘制

		const auto& item = data.items[i];
		CRect rowRect(left, itemY, right, itemY + rowH);

		// 当前选中的股票高亮背景
		bool isCurrent = (!currentStockId.empty() && item.fullCode == currentStockId);
		if (isCurrent)
		{
			memDC.FillSolidRect(rowRect, COLOR_CARD_SELECTED);
		}
		else if (i % 2 == 1)
		{
			// 斑马条纹浅底
			memDC.FillSolidRect(rowRect, RGB(22, 24, 32));
		}

		// 1) 序号
		CRect rcSeq(colSeqX, itemY, colSeqX + colSeqW, itemY + rowH);
		memDC.SetTextColor(COLOR_TEXT_DIM);
		CString seqStr;
		seqStr.Format(_T("%d"), item.rank > 0 ? item.rank : static_cast<int>(i + 1));
		memDC.DrawText(seqStr, rcSeq, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		// 2) 名称
		CRect rcName(colNameX, itemY, colNameX + colNameW, itemY + rowH);
		memDC.SetTextColor(isCurrent ? RGB(147, 197, 253) : COLOR_WHITE);
		memDC.DrawText(item.name.c_str(), rcName, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		// 3) 成交量 / 成交额 (如 39亿, 52亿, 8.5亿, 1200万)
		CRect rcVol(colVolX, itemY, colVolX + colVolW - g_data.RDPI(2), itemY + rowH);
		memDC.SetTextColor(COLOR_TEXT_MUTED);
		CString volStr = _T("-");
		if (item.volume > 0)
		{
			if (item.volume >= 1e8)
			{
				double inYi = item.volume / 1e8;
				if (inYi >= 10.0)
					volStr.Format(_T("%.0f亿"), inYi);
				else
					volStr.Format(_T("%.1f亿"), inYi);
			}
			else if (item.volume >= 1e4)
			{
				volStr.Format(_T("%.0f万"), item.volume / 1e4);
			}
			else
			{
				volStr.Format(_T("%.0f"), item.volume);
			}
		}
		memDC.DrawText(volStr, rcVol, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		// 4) 今涨跌 (红绿着色，如 -0.10%, +1.23%)
		CRect rcChg(colChgX, itemY, colChgX + colChgW - g_data.RDPI(2), itemY + rowH);
		COLORREF chgColor = CCommon::GetProfitLossColor(item.changePercent);
		memDC.SetTextColor(chgColor);
		CString chgStr;
		if (item.changePercent > 0.0001)
			chgStr.Format(_T("+%.2f%%"), item.changePercent);
		else
			chgStr.Format(_T("%.2f%%"), item.changePercent);
		memDC.DrawText(chgStr, rcChg, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		// 5) 仓位比例 (如 17.94%)
		CRect rcPos(colPosX, itemY, right - g_data.RDPI(3), itemY + rowH);
		memDC.SetTextColor(RGB(147, 197, 253));
		CString posStr;
		if (item.ratio > 0.0)
			posStr.Format(_T("%.2f%%"), item.ratio);
		else
			posStr = _T("-");
		memDC.DrawText(posStr, rcPos, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	memDC.SelectClipRgn(nullptr);
	memDC.SelectObject(pOldFont);
}
