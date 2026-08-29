#include "pch.h"
#include "StockListPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <Stock.h>
#include <mutex>
#include <vector>

std::vector<std::wstring> CStockListPanel::GetStockListCodes()
{
	std::vector<std::wstring> stockCodes;
	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	for (const auto& code : g_data.m_setting_data.m_stock_codes)
	{
		if (GetStockPriority(code) >= 200 && code.find(kHK) != 0)  // 只保留非指数、非港股股票
			stockCodes.push_back(code);
	}
	return stockCodes;
}

void CStockListPanel::Draw(CDC& memDC, int x, int y, int w, int h, const std::wstring& currentStockId, int scrollOffset)
{
	// 绘制面板现代深色底 (#14161D)
	memDC.FillSolidRect(x, y, w, h, COLOR_BG_PANEL);

	// 绘制标题栏（与走势图标题栏高度一致，#181B22）
	const int titleH = g_data.RDPI(18);
	memDC.FillSolidRect(x, y, w, titleH, COLOR_BG_HEADER);
	memDC.SetTextColor(COLOR_TEXT_MUTED);
	memDC.SetBkMode(TRANSPARENT);

	CFont titleFont;
	titleFont.CreateFont(-g_data.RDPI(11), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont* pOldBaseFont = memDC.SelectObject(&titleFont);
	memDC.TextOut(x + g_data.RDPI(6), y + g_data.RDPI(2), _T("自选列表"));
	memDC.SelectObject(pOldBaseFont);
	titleFont.DeleteObject();

	// 绘制细暗黑分隔线
	CPen linePen(PS_SOLID, 1, COLOR_DARK_GRAY_BORDER);
	CPen* pOldPen = memDC.SelectObject(&linePen);
	memDC.MoveTo(x, y + titleH);
	memDC.LineTo(x + w, y + titleH);

	std::vector<std::wstring> stockCodes = GetStockListCodes();

	if (stockCodes.empty())
	{
		// 没有股票时显示提示
		memDC.SetTextColor(COLOR_TEXT_DIM);
		memDC.TextOut(x + g_data.RDPI(6), y + titleH + g_data.RDPI(10), _T("暂无股票"));
		memDC.SelectObject(pOldPen);
		return;
	}

	// 每行高度固定36像素
	const int rowHeight = g_data.RDPI(36);
	const int nameHeight = g_data.RDPI(14);
	const int codeHeight = g_data.RDPI(11);

	int listTop = y + titleH;
	int listAreaH = h - titleH;
	int totalContentH = static_cast<int>(stockCodes.size()) * rowHeight;
	int maxScrollOffset = max(0, totalContentH - listAreaH);
	int effectiveScrollOffset = max(0, min(scrollOffset, maxScrollOffset));

	CFont nameFont;
	nameFont.CreateFont(-g_data.RDPI(11), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	CFont codeFont;
	codeFont.CreateFont(-g_data.RDPI(9), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));

	// 裁剪区域：防止滚动时文字超出列表区域或覆盖标题栏
	CRect listClipRect(x, listTop + 1, x + w, y + h);
	int savedDC = memDC.SaveDC();
	memDC.IntersectClipRect(&listClipRect);

	// 绘制股票列表
	for (size_t i = 0; i < stockCodes.size(); ++i)
	{
		const auto& code = stockCodes[i];
		int currentY = listTop - effectiveScrollOffset + static_cast<int>(i) * rowHeight;

		// 超出可视区域跳过绘制
		if (currentY + rowHeight <= listTop || currentY >= y + h)
			continue;

		// 获取股票名称与行情
		std::wstring stockName = code;  // 默认使用代码作为名称
		double diffPercent = 0.0;
		bool hasRealtime = false;
		auto stockData = g_data.GetStockData(code);
		if (stockData)
		{
			if (!stockData->info.displayName.empty())
				stockName = stockData->info.displayName;
			if (stockData->info.is_ok)
			{
				diffPercent = stockData->info.GetChangePercent();
				hasRealtime = true;
			}
		}

		// 高亮当前股票卡片
		bool isCurrent = (code == currentStockId);
		if (isCurrent)
		{
			// 选中项暗黑高亮背景
			memDC.FillSolidRect(x + 1, currentY, w - 2, rowHeight, COLOR_CARD_SELECTED);
			// 左侧 3px 品牌蓝聚焦指示条
			memDC.FillSolidRect(x + 1, currentY, g_data.RDPI(3), rowHeight, COLOR_ACCENT_BLUE);
		}

		// 文字垂直居中：内容总高度 = nameHeight + codeHeight
		int contentH = nameHeight + codeHeight;
		int textOffsetY = (rowHeight - contentH) / 2;

		// 绘制股票名称（上方，高亮白色）
		memDC.SetTextColor(isCurrent ? RGB(255, 255, 255) : COLOR_WHITE);
		CFont* pOldFont = memDC.SelectObject(&nameFont);
		int textLeft = x + g_data.RDPI(6);
		int textRight = x + w - (maxScrollOffset > 0 ? g_data.RDPI(7) : g_data.RDPI(4));
		CRect nameRect(textLeft, currentY + textOffsetY, textRight, currentY + textOffsetY + nameHeight);
		memDC.DrawText(stockName.c_str(), static_cast<int>(stockName.length()), &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		// 绘制股票代码（下方，暗灰）
		memDC.SelectObject(&codeFont);
		memDC.SetTextColor(isCurrent ? RGB(147, 197, 253) : COLOR_GRAY_TEXT);
		memDC.TextOut(textLeft, currentY + textOffsetY + nameHeight, code.c_str());

		// 绘制微涨跌幅徽章（若有行情）
		if (hasRealtime)
		{
			CString changeStr;
			if (diffPercent >= 0)
				changeStr.Format(_T("+%.2f%%"), diffPercent);
			else
				changeStr.Format(_T("%.2f%%"), diffPercent);

			CSize changeSize = memDC.GetTextExtent(changeStr);
			int changeX = textRight - changeSize.cx - g_data.RDPI(2);
			if (changeX > textLeft + g_data.RDPI(30))
			{
				COLORREF badgeBg = (diffPercent >= 0) ? COLOR_BG_RED : COLOR_BG_GREEN;
				CRect badgeRect(changeX - g_data.RDPI(2), currentY + textOffsetY + nameHeight - g_data.RDPI(1),
					changeX + changeSize.cx + g_data.RDPI(2), currentY + textOffsetY + nameHeight + changeSize.cy);
				memDC.FillSolidRect(&badgeRect, badgeBg);
				memDC.SetTextColor(RGB(255, 255, 255));
				memDC.TextOut(changeX, currentY + textOffsetY + nameHeight, changeStr);
			}
		}

		memDC.SelectObject(pOldFont);

		// 绘制暗黑行分隔线
		memDC.MoveTo(x + g_data.RDPI(4), currentY + rowHeight);
		memDC.LineTo(x + w - g_data.RDPI(4), currentY + rowHeight);
	}

	memDC.RestoreDC(savedDC);

	// 绘制右侧极简滚动条指示器（仅在超出高度时显示）
	if (maxScrollOffset > 0 && listAreaH > g_data.RDPI(20))
	{
		int scrollBarW = g_data.RDPI(2);
		int scrollBarX = x + w - scrollBarW - g_data.RDPI(1);
		int trackY = listTop + g_data.RDPI(2);
		int trackH = listAreaH - g_data.RDPI(4);
		int minThumbH = g_data.RDPI(16);
		int thumbH = max(minThumbH, trackH * listAreaH / totalContentH);
		thumbH = min(thumbH, trackH);
		int thumbY = trackY + (trackH - thumbH) * effectiveScrollOffset / maxScrollOffset;

		CRect thumbRect(scrollBarX, thumbY, scrollBarX + scrollBarW, thumbY + thumbH);
		memDC.FillSolidRect(&thumbRect, RGB(80, 85, 105));
	}

	nameFont.DeleteObject();
	codeFont.DeleteObject();
	memDC.SelectObject(pOldPen);
}