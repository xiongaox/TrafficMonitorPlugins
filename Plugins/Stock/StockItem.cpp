#include "pch.h"
#include "StockItem.h"
#include "DataManager.h"
#include "Stock.h"
#include "Common.h"
#include <algorithm>
#include "FloatingWnd.h"
#undef min
#undef max

static const int STOCK_ITEM_GAP = 4; // 股票之间的间隔像素

const wchar_t* StockItem::GetItemName() const
{
	m_item_name = g_data.StringRes(IDS_PLUGIN_ITEM_NAME).GetString();
	return m_item_name.c_str();
}

const wchar_t* StockItem::GetItemId() const
{
	// 固定单一id：主程序按id字符串持久化显示设置中的勾选状态，
	// 只要有这一个条目，勾选一次后永久生效，股票的增减由表格中的√控制。
	// 沿用旧版20槽位机制的 qL0KmmYi0：老用户配置里已勾选该id，
	// 升级到聚合单条目后显示设置中的勾选状态不会丢失
	m_item_id = L"qL0KmmYi0";
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

int StockItem::IsDoubleLineExclusive() const
{
	// 主程序"两行排列"模式下，普通显示项会两两配对、各拿半高区域；
	// 声明独占双行后本条目独占一列、拿到整窗高度，
	// DrawItem 里按可用高度检测行数（2行一列、列优先堆叠股票，与CPU/显卡的上下排布一致）
	return 1;
}

int StockItem::GetSingleStockWidth(CDC* pDC, const std::wstring& code) const
{
	auto data = g_data.GetStockData(code);
	if (!data || !data->info.is_ok)
	{
		return pDC->GetTextExtent(_T("股票: 0.00 +0.00%")).cx + STOCK_ITEM_GAP;
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
	double holdingCount = g_data.GetHoldingCount(code);
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

	return width + STOCK_ITEM_GAP;
}

int StockItem::BuildLayout(CDC* pDC, int rows) const
{
	// 所有勾选"状态栏显示"的股票（自选股/持仓/自定义分组）排在同一个条目内
	std::vector<std::wstring> codes = g_data.GetRegisteredStockCodes();
	m_layout_codes = codes;
	m_layout_offsets.clear();
	m_layout_widths.clear();

	if (rows < 1)
		rows = 1;

	if (codes.empty())
		return 0;

	size_t n = codes.size();
	std::vector<int> widths(n);
	for (size_t i = 0; i < n; ++i)
		widths[i] = GetSingleStockWidth(pDC, codes[i]);

	int total = 0;
	if (rows <= 1)
	{
		// 单行：横向依次排列
		for (size_t i = 0; i < n; ++i)
		{
			m_layout_offsets.push_back(total);
			m_layout_widths.push_back(widths[i]);
			total += widths[i];
		}
	}
	else
	{
		// 列优先：col = i / rows, row = i % rows；每列宽度取列内股票宽度最大值
		for (size_t col = 0; col * rows < n; ++col)
		{
			int colWidth = 0;
			for (size_t r = 0; r < static_cast<size_t>(rows); ++r)
			{
				size_t idx = col * rows + r;
				if (idx < n && widths[idx] > colWidth)
					colWidth = widths[idx];
			}
			for (size_t r = 0; r < static_cast<size_t>(rows); ++r)
			{
				size_t idx = col * rows + r;
				if (idx < n)
				{
					m_layout_offsets.push_back(total);
					m_layout_widths.push_back(colWidth);
				}
			}
			total += colWidth;
		}
	}
	return total;
}

// 定位主程序的任务栏窗口：插件运行在主程序进程内，
// 任务栏窗口就是 Shell_TrayWnd 下属于本进程的可见对话框子窗口。
// 注意：不要改用 ITrafficMonitor::GetTaskbarWindowHwnd —— 仓库头文件与
// 主程序发行版的虚表布局未必一致，实测 1.8.6 上调用直接崩溃
static HWND FindHostTaskbarWindow()
{
	HWND hTray = ::FindWindowW(L"Shell_TrayWnd", nullptr);
	if (hTray == nullptr)
		return nullptr;
	struct FindCtx
	{
		DWORD pid;
		HWND found;
	};
	FindCtx ctx{ ::GetCurrentProcessId(), nullptr };
	auto enumProc = [](HWND hWnd, LPARAM lParam) -> BOOL
	{
		FindCtx* p = reinterpret_cast<FindCtx*>(lParam);
		DWORD pid = 0;
		::GetWindowThreadProcessId(hWnd, &pid);
		if (pid != p->pid)
			return TRUE;
		wchar_t cls[64] = { 0 };
		::GetClassNameW(hWnd, cls, 64);
		if (wcscmp(cls, L"#32770") == 0 && ::IsWindowVisible(hWnd))
		{
			p->found = hWnd;
			return FALSE;
		}
		return TRUE;
	};
	::EnumChildWindows(hTray, enumProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.found;
}

int StockItem::GetItemWidthEx(void* hDC) const
{
	CDC* pDC = CDC::FromHandle((HDC)hDC);

	// 行数：绘制过以后用绘制检测到的行数；尚未绘制过（启动首帧）时默认按双行独占预估（2行），
	// 若能探测到宿主任务栏窗口则根据窗口高度校验，避免首帧按单行测宽导致启动瞬间多出一整列空白
	int rows = m_last_rows;
	if (!m_rows_detected)
	{
		rows = 2;
		HWND hTaskbarDlg = FindHostTaskbarWindow();
		if (hTaskbarDlg != nullptr)
		{
			CRect rc;
			::GetClientRect(hTaskbarDlg, &rc);
			int textH = pDC->GetTextExtent(_T("0")).cy;
			if (textH > 0 && rc.Height() * 5 < textH * 8)
				rows = 1;
		}
	}
	int width = BuildLayout(pDC, rows);
	return width;
}

void StockItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
	CDC* pDC = CDC::FromHandle((HDC)hDC);

	// 记录条目的客户区起点，供 OnMouseEvent 命中换算（主程序传来的鼠标坐标是窗口客户区坐标）
	m_last_draw_x = x;
	m_last_draw_y = y;

	// 按可用高度判断行数：两行排列模式下（独占双行）条目高度约为2倍行高，
	// 排成2行一列、列优先堆叠；水平排列模式条目高度约为1.3倍行高，退化为横向单行
	int textH = pDC->GetTextExtent(_T("0")).cy;
	int rows = (textH > 0 && h * 5 >= textH * 8) ? 2 : 1;
	m_last_rows = rows;
	m_rows_detected = true;

	int total_width = BuildLayout(pDC, rows);
	if (m_layout_codes.empty())
		return;

	int rowH = h / rows;
	m_last_row_h = rowH;

	for (size_t i = 0; i < m_layout_codes.size(); ++i)
	{
		int cellY = static_cast<int>(i % rows) * rowH;
		DrawSingleStock(pDC, m_layout_codes[i], x + m_layout_offsets[i], y + cellY, rowH, dark_mode);
	}
}

int StockItem::DrawSingleStock(CDC* pDC, const std::wstring& code, int x, int y, int h, bool dark_mode) const
{
	CRect rect(CPoint(x, y), CSize(0, h));

	// 文本颜色
	COLORREF color_default;
	if (dark_mode)
		color_default = RGB(255, 255, 255);
	else
		color_default = RGB(0, 0, 0);

	auto data = g_data.GetStockData(code);

	CRect rect_value{ rect };
	if (data && data->info.is_ok && g_data.m_setting_data.m_show_stock_name)
	{
		// 绘制名称
		pDC->SetTextColor(color_default);
		CString stock_name = data->info.GetStockShortName();
		stock_name += _T(": ");
		CRect rect_name{ rect };
		rect_name.right = rect_name.left + pDC->GetTextExtent(stock_name).cx;
		pDC->DrawText(stock_name, rect_name, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		rect_value.left = rect_name.right;
	}

	// 计算涨跌幅百分比
	float fluctuation_percent = 0.0f;
	if (data && data->info.is_ok && data->info.prevClosePrice != 0)
	{
		fluctuation_percent = (data->info.currentPrice - data->info.prevClosePrice) / data->info.prevClosePrice * 100;
	}

	// 根据涨跌幅幅度设置颜色
	COLORREF price_color = CCommon::GetProfitLossColor(fluctuation_percent);

	// 绘制价格（左对齐）
	pDC->SetTextColor(price_color);
	CString strPrice = (data && data->info.is_ok) ? data->info.displayPrice.c_str() : _T("--");
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
		strDiff.Format(_T("+%s"), (data && data->info.is_ok) ? data->info.displayFluctuation.c_str() : L"--");
	else
		strDiff.Format(_T("-%s"), (data && data->info.is_ok) ? data->info.displayFluctuation.c_str() : L"--");

	CRect rect_diff = rect_value;
	rect_diff.left = rect_price.right + 3; // 价格结束位置后留间隔
	rect_diff.right = rect_diff.left + pDC->GetTextExtent(strDiff).cx;
	pDC->DrawText(strDiff, rect_diff, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	// 检查是否显示当天持仓收益（已开启开关且填写了持仓股数）
	double holdingCount = g_data.GetHoldingCount(code);
	bool showTodayProfit = g_data.m_setting_data.m_show_today_profit && (holdingCount > 0.0001);

	if (showTodayProfit && data && data->info.is_ok)
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
		rect_profit.right = rect_profit.left + pDC->GetTextExtent(strProfit).cx;
		pDC->DrawText(strProfit, rect_profit, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	else if (g_data.m_setting_data.m_show_fluctuation && data && data->info.is_ok)
	{
		// 绘制涨跌额（未开启当天持仓收益或未填写持仓时，若勾选了显示涨跌幅则显示涨跌额）
		CRect rect_fluctuation{ rect_value };
		rect_fluctuation.left = rect_diff.right; // 紧接在涨跌幅后面
		rect_fluctuation.right = rect_fluctuation.left + pDC->GetTextExtent(data->info.displayFluctuationDiff.c_str()).cx;
		pDC->DrawText(data->info.displayFluctuationDiff.c_str(), rect_fluctuation, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	return GetSingleStockWidth(pDC, code);
}

const wchar_t* StockItem::GetItemValueSampleText() const
{
	return L"";
}

int StockItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
	CWnd* pWnd = CWnd::FromHandle((HWND)hWnd);
	switch (type)
	{
	case IPluginItem::MT_RCLICKED:
		Stock::Instance().ShowContextMenu(pWnd);
		return 1;

	case IPluginItem::MT_LCLICKED:
	{
		// 主程序传来的是任务栏窗口客户区坐标，减去条目绘制起点得到条目内偏移，
		// 再按最近一次布局缓存定位被点击的股票（x定位列，y定位行）
		int local_x = x - m_last_draw_x;
		int local_y = y - m_last_draw_y;
		int rows = (m_last_rows >= 1) ? m_last_rows : 1;
		std::wstring hit_code;
		for (size_t i = 0; i < m_layout_codes.size(); ++i)
		{
			int cellY = static_cast<int>(i % rows) * m_last_row_h;
			if (local_x >= m_layout_offsets[i] && local_x < m_layout_offsets[i] + m_layout_widths[i] &&
				(m_last_row_h <= 0 || (local_y >= cellY && local_y < cellY + m_last_row_h)))
			{
				hit_code = m_layout_codes[i];
				break;
			}
		}
		if (hit_code.empty())
			return 0;

		// 点击A股股票时打开对应悬浮窗
		if (hit_code.find(kSZ) == 0 || hit_code.find(kBJ) == 0 || hit_code.find(kSH) == 0)
		{
			CPoint ptScreen = CPoint(x, y);
			Stock::Instance().ShowFloatingWnd(hWnd, ptScreen, hit_code);
			return 1;
		}
		else
		{
			MessageBox((HWND)hWnd, g_data.StringRes(IDS_UNSUPPORT_SHOW_KLINE_STOCK_TIP), g_data.StringRes(IDS_PLUGIN_NAME), MB_ICONINFORMATION | MB_OK);
			return 1;
		}
	}
	default:
		break;
	}
	return 0;
}
