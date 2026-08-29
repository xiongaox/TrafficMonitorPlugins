#include "pch.h"
#include "StockItem.h"
#include "DataManager.h"
#include "Stock.h"
#include "Common.h"
#include <algorithm>
#include "FloatingWnd.h"
#undef min
#undef max

const wchar_t* StockItem::GetItemName() const
{
	auto data = g_data.GetStockData(stock_id);
	if (data->info.is_ok)
	{
		if (data)
		{
			m_item_name = data->info.displayName;
		}
		else
		{
			m_item_name = g_data.StringRes(IDS_PLUGIN_ITEM_NAME).GetString();
			m_item_name += std::to_wstring(index);
		}
	}
	else
	{
		m_item_name = stock_id + L" " + g_data.StringRes(IDS_LOAD_FAIL).GetString();
	}
	return m_item_name.c_str();
}

const wchar_t* StockItem::GetItemId() const
{
	m_item_id = L"qL0KmmYi";
	m_item_id += std::to_wstring(index);
	return m_item_id.c_str();
}

const wchar_t* StockItem::GetItemLableText() const
{
	return L"";
}

const wchar_t* StockItem::GetItemValueText() const
{
	return L"";
}

bool StockItem::IsCustomDraw() const
{
	return true;
}
int StockItem::GetItemWidthEx(void* hDC) const
{
	CDC* pDC = CDC::FromHandle((HDC)hDC);
	auto data = g_data.GetStockData(stock_id);
	if (!data || !data->info.is_ok)
	{
		return pDC->GetTextExtent(_T("股票: 0.00 +0.00%")).cx;
	}

	int width = 0;

	// 1. 股票名称
	if (g_data.m_setting_data.m_show_stock_name)
	{
		CString stock_name = data->info.GetStockShortName();
		stock_name += _T(": ");
		width += pDC->GetTextExtent(stock_name).cx;
	}

	// 2. 价格
	CString strPrice = data->info.displayPrice.c_str();
	int textW = pDC->GetTextExtent(strPrice).cx;
	int priceWidth = textW > 33 ? textW : 33;
	width += priceWidth;

	// 3. 价格与涨跌幅间隔 (3px) + 涨跌幅百分比
	float fluctuation_percent = 0.0f;
	if (data->info.prevClosePrice != 0)
	{
		fluctuation_percent = (data->info.currentPrice - data->info.prevClosePrice) / data->info.prevClosePrice * 100;
	}

	CString strDiff;
	if (fluctuation_percent >= 0)
		strDiff.Format(_T("+%s"), data->info.displayFluctuation.c_str());
	else
		strDiff.Format(_T("-%s"), data->info.displayFluctuation.c_str());

	width += 3 + pDC->GetTextExtent(strDiff).cx;

	// 4. 当天持仓收益 或 涨跌额
	double holdingCount = g_data.GetHoldingCount(stock_id);
	bool showTodayProfit = g_data.m_setting_data.m_show_today_profit && (holdingCount > 0.0001);

	if (showTodayProfit)
	{
		double curPrice = (data->info.currentPrice > 0.0001 ? data->info.currentPrice : data->info.prevClosePrice);
		double todayProfit = (curPrice - data->info.prevClosePrice) * holdingCount;
		CString strProfit;
		if (todayProfit > 0.0001)
			strProfit.Format(_T("【+%s】"), CCommon::FormatAmount(todayProfit).GetString());
		else if (todayProfit < -0.0001)
			strProfit.Format(_T("【-%s】"), CCommon::FormatAmount(-todayProfit).GetString());
		else
			strProfit = _T("【0.00】");

		width += pDC->GetTextExtent(strProfit).cx;
	}
	else if (g_data.m_setting_data.m_show_fluctuation)
	{
		width += pDC->GetTextExtent(data->info.displayFluctuationDiff.c_str()).cx;
	}

	return width + 4;
}

void StockItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
	// 绘图句柄
	CDC* pDC = CDC::FromHandle((HDC)hDC);

	// 矩形区域
	auto data = g_data.GetStockData(stock_id);
	CRect rect(CPoint(x, y), CSize(w, h));

	// 文本颜色
	COLORREF color_default;
	if (dark_mode)
		color_default = RGB(255, 255, 255);
	else
		color_default = RGB(0, 0, 0);

	CRect rect_value{ rect };
	if (data->info.is_ok && g_data.m_setting_data.m_show_stock_name)
	{
		// 绘制名称
		pDC->SetTextColor(color_default);
		//CString stock_name(data->info.displayName.c_str(), 2); //CString stock_name{data->info.displayName.c_str()};
		CString stock_name = data->info.GetStockShortName();
		stock_name += _T(": ");
		CRect rect_name{ rect };
		rect_name.right = rect_name.left + pDC->GetTextExtent(stock_name).cx;
		pDC->DrawText(stock_name, rect_name, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		rect_value.left = rect_name.right;
	}

	// 计算涨跌幅百分比
	float fluctuation_percent = 0.0f;
	if (data->info.prevClosePrice != 0)
	{
		fluctuation_percent = (data->info.currentPrice - data->info.prevClosePrice) / data->info.prevClosePrice * 100;
	}

	// 根据涨跌幅幅度设置颜色
	COLORREF price_color = CCommon::GetProfitLossColor(fluctuation_percent);

	// 绘制价格（左对齐）
	pDC->SetTextColor(price_color);
	CString strPrice = data->info.displayPrice.c_str();
	CRect rect_price = rect_value;
	int textW = pDC->GetTextExtent(strPrice).cx;
	int priceWidth = textW > 33 ? textW : 33;
	rect_price.right = rect_price.left + priceWidth;
	pDC->DrawText(strPrice, rect_price, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	// 设置涨跌幅/涨跌额文本颜色
	if (g_data.m_setting_data.m_color_with_price)
	{
		pDC->SetTextColor(price_color);
	}
	else
	{
		pDC->SetTextColor(color_default);
	}

	// 绘制涨跌幅百分比（始终显示）
	CString strDiff;
	if (fluctuation_percent >= 0)
		strDiff.Format(_T("+%s"), data->info.displayFluctuation.c_str());
	else
		strDiff.Format(_T("-%s"), data->info.displayFluctuation.c_str());

	CRect rect_diff = rect_value;
	rect_diff.left = rect_price.right + 3; // 价格结束位置后留间隔
	rect_diff.right = rect_diff.left + pDC->GetTextExtent(strDiff).cx;
	pDC->DrawText(strDiff, rect_diff, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	// 检查是否显示当天持仓收益（已开启开关且填写了持仓股数）
	double holdingCount = g_data.GetHoldingCount(stock_id);
	bool showTodayProfit = g_data.m_setting_data.m_show_today_profit && (holdingCount > 0.0001);

	if (showTodayProfit)
	{
		double curPrice = (data->info.currentPrice > 0.0001 ? data->info.currentPrice : data->info.prevClosePrice);
		double todayProfit = (curPrice - data->info.prevClosePrice) * holdingCount;
		CString strProfit;
		if (todayProfit > 0.0001)
			strProfit.Format(_T("【+%s】"), CCommon::FormatAmount(todayProfit).GetString());
		else if (todayProfit < -0.0001)
			strProfit.Format(_T("【-%s】"), CCommon::FormatAmount(-todayProfit).GetString());
		else
			strProfit = _T("【0.00】");

		CRect rect_profit{ rect_value };
		rect_profit.left = rect_diff.right;
		pDC->DrawText(strProfit, rect_profit, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	else if (g_data.m_setting_data.m_show_fluctuation)
	{
		// 绘制涨跌额（未开启当天持仓收益或未填写持仓时，若勾选了显示涨跌幅则显示涨跌额）
		CRect rect_fluctuation{ rect_value };
		rect_fluctuation.left = rect_diff.right; // 紧接在涨跌幅后面
		pDC->DrawText(data->info.displayFluctuationDiff.c_str(), rect_fluctuation, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
}

const wchar_t* StockItem::GetItemValueSampleText() const
{
	//    if (g_data.m_setting_data.m_show_stock_name)
	//    {
	//        return L"--------: 0000000.00 +00.00%";
	//    }
	//    else
	//    {
	//        return L"0000000.00 +00.00%";
	//    }
	return L"";
}

int StockItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
	CWnd* pWnd = CWnd::FromHandle((HWND)hWnd);
	LogX(L"OnMouseEvent: %d\n", type);
	switch (type)
	{
	case IPluginItem::MT_RCLICKED:
		Stock::Instance().ShowContextMenu(pWnd);
		return 1;

	case IPluginItem::MT_LCLICKED:
	{
		// 点击时使用股票自身代码打开悬浮窗
		if (stock_id.find(kSZ) == 0 || stock_id.find(kBJ) == 0 || stock_id.find(kSH) == 0)
		{
			CPoint ptScreen = CPoint(x, y);
			Stock::Instance().ShowFloatingWnd(hWnd, ptScreen, stock_id);
			return 1;
		}
		else
		{
			MessageBox((HWND)hWnd, g_data.StringRes(IDS_UNSUPPORT_SHOW_KLINE_STOCK_TIP), g_data.StringRes(IDS_PLUGIN_NAME), MB_ICONINFORMATION | MB_OK);
		}
	}
	default:
		break;
	}
	return 0;
}