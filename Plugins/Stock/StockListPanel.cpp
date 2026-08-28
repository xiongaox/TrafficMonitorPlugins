#include "pch.h"
#include "StockListPanel.h"
#include "ChartColors.h"
#include "Common.h"
#include "DataManager.h"
#include <Stock.h>
#include <mutex>
#include <vector>

void CStockListPanel::Draw(CDC& memDC, int x, int y, int w, int h, const std::wstring& currentStockId)
{
	// 绘制面板现代底色
	memDC.FillSolidRect(x, y, w, h, COLOR_PANEL_BG);

	// 绘制标题栏（与走势图标题栏高度一致）
	const int titleH = g_data.RDPI(18);
	memDC.FillSolidRect(x, y, w, titleH, RGB(238, 242, 246));
	memDC.SetTextColor(COLOR_BLACK);
	memDC.SetBkMode(TRANSPARENT);

	CFont titleFont;
	titleFont.CreateFont(-g_data.RDPI(11), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont* pOldBaseFont = memDC.SelectObject(&titleFont);
	memDC.TextOut(x + g_data.RDPI(6), y + g_data.RDPI(2), _T("自选列表"));
	memDC.SelectObject(pOldBaseFont);
	titleFont.DeleteObject();

	// 绘制细分隔线
	CPen linePen(PS_SOLID, 1, COLOR_DARK_GRAY_BORDER);
	CPen* pOldPen = memDC.SelectObject(&linePen);
	memDC.MoveTo(x, y + titleH);
	memDC.LineTo(x + w, y + titleH);

	// 获取所有股票列表（加锁访问，过滤掉大盘指数和港股）
	std::vector<std::wstring> stockCodes;
	{
		std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
		for (const auto& code : g_data.m_setting_data.m_stock_codes)
		{
			if (GetStockPriority(code) >= 200 && code.find(kHK) != 0)  // 只保留非指数、非港股股票
				stockCodes.push_back(code);
		}
	}

	if (stockCodes.empty())
	{
		// 没有股票时显示提示
		memDC.SetTextColor(COLOR_GRAY_TEXT);
		memDC.TextOut(x + g_data.RDPI(6), y + titleH + g_data.RDPI(10), _T("暂无股票"));
		memDC.SelectObject(pOldPen);
		return;
	}

	// 每行高度固定36像素
	const int rowHeight = g_data.RDPI(36);
	const int nameHeight = g_data.RDPI(14);
	const int codeHeight = g_data.RDPI(11);

	CFont nameFont;
	nameFont.CreateFont(-g_data.RDPI(11), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	CFont codeFont;
	codeFont.CreateFont(-g_data.RDPI(9), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));

	// 绘制股票列表
	int currentY = y + titleH + g_data.RDPI(2);
	for (const auto& code : stockCodes)
	{
		if (currentY + rowHeight > y + h)
			break;  // 超出区域

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
			// 选中项柔和背景
			memDC.FillSolidRect(x + 1, currentY, w - 2, rowHeight, COLOR_CARD_SELECTED);
			// 左侧 3px 品牌蓝聚焦指示条
			memDC.FillSolidRect(x + 1, currentY, g_data.RDPI(3), rowHeight, COLOR_ACCENT_BLUE);
		}

		// 文字垂直居中：内容总高度 = nameHeight + codeHeight
		int contentH = nameHeight + codeHeight;
		int textOffsetY = (rowHeight - contentH) / 2;

		// 绘制股票名称（上方）
		memDC.SetTextColor(isCurrent ? COLOR_ACCENT_BLUE : COLOR_BLACK);
		CFont* pOldFont = memDC.SelectObject(&nameFont);
		int textLeft = x + g_data.RDPI(6);
		CRect nameRect(textLeft, currentY + textOffsetY, x + w - g_data.RDPI(4), currentY + textOffsetY + nameHeight);
		memDC.DrawText(stockName.c_str(), static_cast<int>(stockName.length()), &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

		// 绘制股票代码与涨跌幅微标签（下方）
		memDC.SelectObject(&codeFont);
		memDC.SetTextColor(isCurrent ? RGB(70, 95, 135) : COLOR_GRAY_TEXT);
		memDC.TextOut(textLeft, currentY + textOffsetY + nameHeight, code.c_str());

		// 绘制微涨跌幅文本（若有行情）
		if (hasRealtime)
		{
			CString changeStr;
			if (diffPercent >= 0)
				changeStr.Format(_T("+%.2f%%"), diffPercent);
			else
				changeStr.Format(_T("%.2f%%"), diffPercent);

			memDC.SetTextColor(diffPercent >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN);
			CSize changeSize = memDC.GetTextExtent(changeStr);
			int changeX = x + w - changeSize.cx - g_data.RDPI(4);
			if (changeX > textLeft + g_data.RDPI(32))
			{
				memDC.TextOut(changeX, currentY + textOffsetY + nameHeight, changeStr);
			}
		}

		memDC.SelectObject(pOldFont);

		// 绘制轻微行分隔线
		currentY += rowHeight;
		memDC.MoveTo(x + g_data.RDPI(4), currentY);
		memDC.LineTo(x + w - g_data.RDPI(4), currentY);
	}

	nameFont.DeleteObject();
	codeFont.DeleteObject();
	memDC.SelectObject(pOldPen);
}