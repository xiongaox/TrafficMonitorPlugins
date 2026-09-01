#include "pch.h"
#include "StockListPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include "StockFont.h"
#include <Stock.h>
#include <mutex>
#include <vector>

int CStockListPanel::GetGroupTabCount()
{
	return 2 + static_cast<int>(g_data.m_setting_data.m_custom_groups.size());
}

int CStockListPanel::ClampGroupTab(int groupTab)
{
	if (groupTab < 0)
		return 0;
	int count = GetGroupTabCount();
	return groupTab >= count ? count - 1 : groupTab;
}

std::wstring CStockListPanel::GetGroupTabName(int groupTab)
{
	groupTab = ClampGroupTab(groupTab);
	if (groupTab == 0)
		return L"自选股";
	if (groupTab == 1)
		return L"持仓";
	return g_data.m_setting_data.m_custom_groups[groupTab - 2].name;
}

std::vector<std::wstring> CStockListPanel::GetStockListCodes()
{
	return GetStockListCodes(0);
}

std::vector<std::wstring> CStockListPanel::GetStockListCodes(int groupTab)
{
	groupTab = ClampGroupTab(groupTab);
	std::vector<std::wstring> stockCodes;
	std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
	const std::vector<std::wstring>* src = nullptr;
	if (groupTab == 0)
		src = &g_data.m_setting_data.m_stock_codes;
	else if (groupTab == 1)
		src = &g_data.m_setting_data.m_position_codes;
	else
		src = &g_data.m_setting_data.m_custom_groups[groupTab - 2].codes;
	for (const auto& code : *src)
	{
		if (GetStockPriority(code) >= 200 && code.find(kHK) != 0)  // 只保留非指数、非港股股票
			stockCodes.push_back(code);
	}
	return stockCodes;
}

std::vector<FloatingGroupTab> CStockListPanel::LayoutGroupTabs(CDC& memDC, int windowWidth, int headerHeight, int activeTab)
{
	std::vector<FloatingGroupTab> tabs;

	const auto& customGroups = g_data.m_setting_data.m_custom_groups;
	int tabCount = ClampGroupTab(activeTab);

	// 三个固定标签位：自选股 / 持仓 / 第一个自定义分组；其余自定义分组折叠进“更多分组”
	tabs.push_back({ L"自选股", 0, tabCount == 0, false, CRect(0, 0, 0, 0) });
	tabs.push_back({ L"持仓", 1, tabCount == 1, false, CRect(0, 0, 0, 0) });
	if (!customGroups.empty())
		tabs.push_back({ customGroups[0].name, 2, tabCount == 2, false, CRect(0, 0, 0, 0) });
	if (customGroups.size() >= 2)
	{
		std::wstring dropText = L"更多分组 ▾";
		if (tabCount >= 3 && (tabCount - 2) < static_cast<int>(customGroups.size()))
			dropText = customGroups[tabCount - 2].name + L" ▾";
		tabs.push_back({ dropText, -1, tabCount >= 3, true, CRect(0, 0, 0, 0) });
	}

	// 布局：在标题栏内垂直居中，右侧预留窗口 40% 给居中的股票标题
	CFont font;
	CreateStockFont(font, memDC, g_data.RDPI(9), FW_SEMIBOLD);
	HGDIOBJ oldFont = memDC.SelectObject(&font);

	const int tabH = g_data.RDPI(18);
	const int tabTop = max(1, (headerHeight - tabH) / 2);
	const int gap = g_data.RDPI(3);
	const int padX = g_data.RDPI(8);
	const int maxTabW = g_data.RDPI(88);
	const int stripLeft = g_data.RDPI(5);
	const int maxStripRight = max(stripLeft + g_data.RDPI(60), min(g_data.RDPI(255), windowWidth * 6 / 10));

	int curX = stripLeft;
	for (auto& tab : tabs)
	{
		CString text(tab.name.c_str());
		int textW = memDC.GetTextExtent(text).cx;
		int tabW = min(maxTabW, textW + padX * 2);
		// 超出标签条可用宽度时截断，避免盖住居中的股票标题
		if (curX + tabW > maxStripRight)
			tabW = max(g_data.RDPI(40), maxStripRight - curX);
		tab.rect = CRect(curX, tabTop, curX + tabW, tabTop + tabH);
		curX += tabW + gap;
		if (curX >= maxStripRight)
			break;
	}

	memDC.SelectObject(oldFont);
	font.DeleteObject();
	return tabs;
}

int CStockListPanel::GetRowHeight()
{
	return g_data.RDPI(48);
}

int CStockListPanel::GetPanelWidth()
{
	return g_data.RDPI(114);
}

void CStockListPanel::DrawGroupTabs(CDC& memDC, const std::vector<FloatingGroupTab>& tabs, int hoverIdx)
{
	CFont font;
	CreateStockFont(font, memDC, g_data.RDPI(9), FW_SEMIBOLD);
	CFont* pOldFont = memDC.SelectObject(&font);
	int oldBk = memDC.SetBkMode(TRANSPARENT);

	for (size_t i = 0; i < tabs.size(); ++i)
	{
		const auto& tab = tabs[i];
		const CRect& r = tab.rect;
		if (r.IsRectEmpty())
			continue;

		if (tab.isActive)
		{
			// 激活标签：品牌蓝实底 + 白字（与分组管理页激活标签一致）
			memDC.FillSolidRect(r, COLOR_ACCENT_BLUE);
			memDC.SetTextColor(RGB(255, 255, 255));
		}
		else
		{
			// 未激活标签：卡片底色 + 暗边框，悬停提亮
			bool hovered = (static_cast<int>(i) == hoverIdx);
			memDC.FillSolidRect(r, hovered ? RGB(30, 41, 59) : COLOR_BG_CARD);
			CBrush borderBrush(hovered ? RGB(59, 130, 246) : COLOR_DARK_GRAY_BORDER);
			memDC.FrameRect(r, &borderBrush);
			memDC.SetTextColor(hovered ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);
		}

		CString text(tab.name.c_str());
		CRect textRect = r;
		textRect.DeflateRect(g_data.RDPI(6), 0);
		memDC.DrawText(text, textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}

	memDC.SetBkMode(oldBk);
	memDC.SelectObject(pOldFont);
	font.DeleteObject();
}

void CStockListPanel::Draw(CDC& memDC, int x, int y, int w, int h, const std::wstring& currentStockId, int scrollOffset, int groupTab)
{
	// 绘制面板现代深色底 (#14161D)
	memDC.FillSolidRect(x, y, w, h, COLOR_BG_PANEL);

	// 绘制标题栏（与走势图标题栏高度一致，#181B22）
	const int titleH = g_data.RDPI(18);
	memDC.FillSolidRect(x, y, w, titleH, COLOR_BG_HEADER);
	memDC.SetTextColor(COLOR_TEXT_MUTED);
	memDC.SetBkMode(TRANSPARENT);

	std::wstring groupTitle = GetGroupTabName(groupTab);

	CFont titleFont;
	CreateStockFont(titleFont, memDC, g_data.RDPI(11), FW_SEMIBOLD);
	CFont* pOldBaseFont = memDC.SelectObject(&titleFont);
	memDC.TextOut(x + g_data.RDPI(6), y + g_data.RDPI(2), groupTitle.c_str());
	memDC.SelectObject(pOldBaseFont);
	titleFont.DeleteObject();

	// 绘制细暗黑分隔线
	CPen linePen(PS_SOLID, 1, COLOR_DARK_GRAY_BORDER);
	CPen* pOldPen = memDC.SelectObject(&linePen);
	memDC.MoveTo(x, y + titleH);
	memDC.LineTo(x + w, y + titleH);

	std::vector<std::wstring> stockCodes = GetStockListCodes(groupTab);

	if (stockCodes.empty())
	{
		// 没有股票时显示提示
		memDC.SetTextColor(COLOR_TEXT_DIM);
		memDC.TextOut(x + g_data.RDPI(6), y + titleH + g_data.RDPI(10), _T("暂无股票"));
		memDC.SelectObject(pOldPen);
		return;
	}

	const int rowHeight = GetRowHeight();
	const int nameHeight = g_data.RDPI(14);
	const int codeHeight = g_data.RDPI(12);
	const int lineGap = g_data.RDPI(4);
	const int cardPadX = g_data.RDPI(3);
	const int cardPadY = g_data.RDPI(4);
	const int innerPadX = g_data.RDPI(6);
	const int innerPadY = g_data.RDPI(5);

	int listTop = y + titleH;
	int listAreaH = h - titleH;
	int totalContentH = static_cast<int>(stockCodes.size()) * rowHeight;
	int maxScrollOffset = max(0, totalContentH - listAreaH);
	int effectiveScrollOffset = max(0, min(scrollOffset, maxScrollOffset));

	CFont nameFont;
	CreateStockFont(nameFont, memDC, g_data.RDPI(11), FW_NORMAL);

	CFont codeFont;
	CreateStockFont(codeFont, memDC, g_data.RDPI(9), FW_NORMAL);

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
		std::wstring stockName = CCommon::GetPureCode(code);
		std::wstring displayCode = CCommon::GetPureCode(code);
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

		CRect cardRect(
			x + cardPadX,
			currentY + cardPadY,
			x + w - cardPadX,
			currentY + rowHeight - cardPadY);

		bool isCurrent = (code == currentStockId);
		memDC.FillSolidRect(&cardRect, isCurrent ? COLOR_CARD_SELECTED : COLOR_BG_CARD);
		if (isCurrent)
			memDC.FillSolidRect(cardRect.left, cardRect.top, g_data.RDPI(3), cardRect.Height(), COLOR_ACCENT_BLUE);

		int textLeft = cardRect.left + innerPadX;
		int textRight = cardRect.right - innerPadX;
		int nameTop = cardRect.top + innerPadY;
		int codeTop = nameTop + nameHeight + lineGap;

		memDC.SetTextColor(isCurrent ? RGB(255, 255, 255) : COLOR_WHITE);
		CFont* pOldFont = memDC.SelectObject(&nameFont);
		CRect nameRect(textLeft, nameTop, textRight, nameTop + nameHeight);
		memDC.DrawText(stockName.c_str(), static_cast<int>(stockName.length()), &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		memDC.SelectObject(&codeFont);
		memDC.SetTextColor(isCurrent ? RGB(147, 197, 253) : COLOR_GRAY_TEXT);

		CRect codeRect(textLeft, codeTop, textRight, codeTop + codeHeight);
		if (hasRealtime)
		{
			CString changeStr;
			if (diffPercent >= 0)
				changeStr.Format(_T("+%.2f%%"), diffPercent);
			else
				changeStr.Format(_T("%.2f%%"), diffPercent);

			CSize changeSize = memDC.GetTextExtent(changeStr);
			const int badgePadX = g_data.RDPI(3);
			int badgeW = changeSize.cx + badgePadX * 2;
			int badgeRight = textRight;
			int badgeLeft = max(textLeft, badgeRight - badgeW);
			CRect badgeRect(
				badgeLeft,
				codeTop - g_data.RDPI(1),
				badgeRight,
				codeTop + codeHeight + g_data.RDPI(1));
			memDC.FillSolidRect(&badgeRect, (diffPercent >= 0) ? COLOR_BG_RED : COLOR_BG_GREEN);
			memDC.SetTextColor(RGB(255, 255, 255));
			CRect changeRect(badgeLeft, codeTop, badgeRight, codeTop + codeHeight);
			memDC.DrawText(changeStr, changeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

			int codeRight = badgeLeft - g_data.RDPI(4);
			if (codeRight > textLeft + g_data.RDPI(18))
				codeRect.right = codeRight;

			memDC.SetTextColor(isCurrent ? RGB(147, 197, 253) : COLOR_GRAY_TEXT);
		}

		memDC.DrawText(displayCode.c_str(), static_cast<int>(displayCode.length()), &codeRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		memDC.SelectObject(pOldFont);
	}

	memDC.RestoreDC(savedDC);

	nameFont.DeleteObject();
	codeFont.DeleteObject();
	memDC.SelectObject(pOldPen);
}
