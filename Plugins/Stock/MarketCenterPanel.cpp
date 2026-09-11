#include "pch.h"
#include "MarketCenterPanel.h"
#include "DataManager.h"
#include "ChartColors.h"
#include "Icons/Icons.h"
#include "Common.h"
#include <algorithm>
#include <cmath>

// ===== 行情中心窗口 =====
// 布局与视觉对齐 demo/market_center_demo.html 定稿：
//   --bg #12141A  --panel #1A1D26  --card #20242F  --border #2A2F3C
//   --text #E8EAF0  --text-sub #9AA1B0  --text-dim #616878
//   --up #F6465D  --down #0ECB81  --accent #3B82F6
namespace
{
	const COLORREF MC_BG = RGB(18, 20, 26);
	const COLORREF MC_PANEL = RGB(26, 29, 38);
	const COLORREF MC_CARD = RGB(32, 36, 47);
	const COLORREF MC_BORDER = RGB(42, 47, 60);
	const COLORREF MC_TEXT = RGB(232, 234, 240);
	const COLORREF MC_TEXT_SUB = RGB(154, 161, 176);
	const COLORREF MC_TEXT_DIM = RGB(97, 104, 120);
	const COLORREF MC_UP = RGB(246, 70, 93);
	const COLORREF MC_DOWN = RGB(14, 203, 129);
	const COLORREF MC_ACCENT = RGB(59, 130, 246);
	const COLORREF MC_GRID = RGB(30, 34, 44);
	// 主力资金曲线色（demo MF_SERIES）
	const COLORREF MF_SH = RGB(77, 138, 240);
	const COLORREF MF_SZ = RGB(245, 166, 35);
	const COLORREF MF_ETF = RGB(237, 91, 196);
	const COLORREF MF_IDX = RGB(154, 161, 176);
	// 资金流向曲线色（机构/主力/大户/散户，对应图一）
	const COLORREF FLOW_INST = RGB(74, 144, 226);  // 机构（浅蓝）
	const COLORREF FLOW_MAIN = RGB(245, 166, 35);  // 主力（橙黄）
	const COLORREF FLOW_BIG  = RGB(80, 227, 194);  // 大户（水绿）
	const COLORREF FLOW_SMALL= RGB(255, 107, 107); // 散户（珊瑚粉红）

	constexpr const wchar_t* MC_FONT = L"微软雅黑";

	std::unique_ptr<Gdiplus::Font> MkFont(int px, bool bold = false)
	{
		return std::make_unique<Gdiplus::Font>(MC_FONT, static_cast<Gdiplus::REAL>(g_data.DPI(px)),
			bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	}

	Gdiplus::Color Gdi(COLORREF c, BYTE a = 255)
	{
		return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
	}

	float clampf(float v, float lo, float hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	std::wstring FormatYi(double yuan, int prec = 1)
	{
		wchar_t buf[48];
		double yi = yuan / 1e8;
		swprintf_s(buf, L"%s%.*f亿", yi >= 0 ? L"+" : L"-", prec, fabs(yi));
		return buf;
	}

	std::wstring FormatPct(double pct)
	{
		wchar_t buf[32];
		swprintf_s(buf, L"%s%.2f%%", pct >= 0 ? L"+" : L"", pct);
		return buf;
	}

	std::wstring FormatInt(long long v)
	{
		wchar_t buf[32];
		swprintf_s(buf, L"%lld", v);
		return buf;
	}

	COLORREF UpDownColor(double v)
	{
		return v > 0 ? MC_UP : (v < 0 ? MC_DOWN : MC_TEXT_SUB);
	}

	void DrawStr(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font* font,
		const CRect& rc, COLORREF color, BYTE alpha = 255,
		Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear)
	{
		Gdiplus::StringFormat sf;
		sf.SetAlignment(align);
		sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		Gdiplus::SolidBrush brush(Gdi(color, alpha));
		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
			static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
		g.DrawString(text.c_str(), -1, font, rf, &sf, &brush);
	}

	void DrawStrMid(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font* font,
		const CRect& rc, COLORREF color, BYTE alpha = 255)
	{
		DrawStr(g, text, font, rc, color, alpha, Gdiplus::StringAlignmentCenter);
	}

	// 单行绘制：不换行，宽度不够时省略号截断
	void DrawStrSingle(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::Font* font,
		const CRect& rc, COLORREF color, BYTE alpha = 255,
		Gdiplus::StringAlignment align = Gdiplus::StringAlignmentCenter)
	{
		Gdiplus::StringFormat sf;
		sf.SetAlignment(align);
		sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		sf.SetFormatFlags(static_cast<Gdiplus::StringFormatFlags>(sf.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap));
		Gdiplus::SolidBrush brush(Gdi(color, alpha));
		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
			static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
		g.DrawString(text.c_str(), -1, font, rf, &sf, &brush);
	}

	CSize MeasureStr(Gdiplus::Graphics& g, const Gdiplus::Font* font, const std::wstring& text)
	{
		Gdiplus::RectF bound;
		g.MeasureString(text.c_str(), -1, font, Gdiplus::PointF(0, 0), &bound);
		return CSize(static_cast<int>(bound.Width) + 1, static_cast<int>(bound.Height) + 1);
	}

	void FillCard(Gdiplus::Graphics& g, const CRect& rc, COLORREF bg = MC_CARD, COLORREF border = MC_BORDER)
	{
		Gdiplus::SolidBrush b(Gdi(bg));
		g.FillRectangle(&b, Gdiplus::REAL(rc.left), Gdiplus::REAL(rc.top), Gdiplus::REAL(rc.Width()), Gdiplus::REAL(rc.Height()));
		Gdiplus::Pen p(Gdi(border), 1.0f);
		g.DrawRectangle(&p, Gdiplus::REAL(rc.left), Gdiplus::REAL(rc.top), Gdiplus::REAL(rc.Width()), Gdiplus::REAL(rc.Height()));
	}

	void FillRounded(Gdiplus::Graphics& g, const CRect& rc, COLORREF bg, float radius, BYTE alpha = 255)
	{
		if (rc.Width() <= 0 || rc.Height() <= 0)
			return;
		Gdiplus::SolidBrush b(Gdi(bg, alpha));
		if (radius <= 0.5f)
		{
			g.FillRectangle(&b, Gdiplus::REAL(rc.left), Gdiplus::REAL(rc.top), Gdiplus::REAL(rc.Width()), Gdiplus::REAL(rc.Height()));
			return;
		}
		Gdiplus::GraphicsPath path;
		float w = static_cast<float>(rc.Width()), h = static_cast<float>(rc.Height());
		float r = min(radius, min(w, h) / 2);
		path.AddArc(rc.left, rc.top, r * 2, r * 2, 180, 90);
		path.AddArc(rc.right - r * 2, rc.top, r * 2, r * 2, 270, 90);
		path.AddArc(rc.right - r * 2, rc.bottom - r * 2, r * 2, r * 2, 0, 90);
		path.AddArc(rc.left, rc.bottom - r * 2, r * 2, r * 2, 90, 90);
		path.CloseFigure();
		g.FillPath(&b, &path);
	}

	// 漂亮的坐标轴刻度步长（支持指定期望分段数 targetDivs）
	double NiceStep(double range, double targetDivs = 3.0)
	{
		double raw = range / max(1.0, targetDivs);
		if (raw <= 0) return 1.0;
		double mag = pow(10.0, floor(log10(raw)));
		double norm = raw / mag;
		double step;
		if (norm < 1.4) step = 1;
		else if (norm < 2.2) step = 2;
		else if (norm < 3.8) step = 2.5;
		else if (norm < 7.5) step = 5;
		else step = 10;
		return step * mag;
	}

	std::wstring FormatAxisNum(double v, double step)
	{
		wchar_t buf[32];
		if (fabs(step - floor(step)) < 1e-5)
			swprintf_s(buf, L"%.0f", v);
		else
			swprintf_s(buf, L"%.1f", v);
		return buf;
	}
}

CMarketCenterPanel::CMarketCenterPanel()
{
}

CMarketCenterPanel::~CMarketCenterPanel()
{
}

void CMarketCenterPanel::SetNotifyWnd(HWND h)
{
	m_notify_wnd = h;
}

void CMarketCenterPanel::Draw(CDC& memDC, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
		return;
	m_content_rect = CRect(x, y, x + w, y + h);
	// 尺寸变化时重排矩形图布局
	if (m_draw_size != CSize(w, h))
	{
		m_draw_size = CSize(w, h);
		m_bubble_layout_dirty = true;
	}
	Gdiplus::Graphics g(memDC.GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
	g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
	DrawAll(g, m_content_rect);
}

// 开市/休市时钟：画在悬浮窗顶部标题条内（水平居中），全部页面统一显示
// 字体直接派生宿主字体（与首页股票名标题同字号同字面），时间加粗
void CMarketCenterPanel::DrawHeaderClock(CDC& memDC, const CRect& rc)
{
	Gdiplus::Graphics g(memDC.GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
	g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

	std::wstring status = m_clock_status == 0 ? L"开市" : L"休市";
	// OnPaint 已将宿主字体选入 memDC，取其 LOGFONT 派生时钟字体（与股票名标题同字号）
	std::unique_ptr<Gdiplus::Font> fStatus, fTime;
	{
		HDC hdc = memDC.GetSafeHdc();
		LOGFONTW lf{};
		HFONT hHost = static_cast<HFONT>(::GetCurrentObject(hdc, OBJ_FONT));
		if (hHost && ::GetObjectW(hHost, sizeof(lf), &lf) == sizeof(lf))
		{
			// "开市/休市"与左侧行情中心标题同款字体：RDPI(12) 粗体（同 CreateStockFont 的 104% 密度补偿）
			LOGFONTW lfStatus = lf;
			lfStatus.lfHeight = -(g_data.RDPI(12) * 104 / 100) * g_data.GetFontScalePercent() / 100;
			lfStatus.lfWeight = FW_BOLD;
			fStatus.reset(new Gdiplus::Font(hdc, &lfStatus));
			// 时间数字：宿主字体加粗放大 1.25 倍补数字视觉字高
			LOGFONTW lfTime = lf;
			lfTime.lfWeight = FW_BOLD;
			lfTime.lfHeight = lfTime.lfHeight * 125 / 100;
			fTime.reset(new Gdiplus::Font(hdc, &lfTime));
		}
		if (!fStatus || Gdiplus::Ok != fStatus->GetLastStatus())
			fStatus = MkFont(13);
		if (!fTime || Gdiplus::Ok != fTime->GetLastStatus())
			fTime = MkFont(13, true);
	}
		CSize szStatus = MeasureStr(g, fStatus.get(), status);
		CSize szTime = MeasureStr(g, fTime.get(), m_clock_time);
		std::wstring cacheStatus;
		const auto state = CMarketCenterData::Instance().GetDataSetState(CurrentDataSet(),
			CurrentDataSet() == CMarketCenterData::DS_ETFS ? 300 : (CurrentDataSet() == CMarketCenterData::DS_SECTORS || CurrentDataSet() == CMarketCenterData::DS_MAINFLOW ? 120 : 60));
		if (state.failed) cacheStatus = L"更新失败";
		else if (state.inflight || state.queued) cacheStatus = state.hasData ? L"后台数据缓存中" : L"正在获取数据";
		else if (state.hasData) cacheStatus = L"正在使用本地数据";
		else cacheStatus = L"暂无缓存";
		CSize szCache = MeasureStr(g, fStatus.get(), cacheStatus);
		int cacheRight = rc.right - g_data.DPI(22) - g_data.DPI(4);
		int cacheLeft = max(rc.left + g_data.DPI(4), cacheRight - szCache.cx);
		DrawStrSingle(g, cacheStatus, fStatus.get(), CRect(cacheLeft, rc.top, cacheRight, rc.bottom), RGB(255, 255, 255), 128, Gdiplus::StringAlignmentFar);
	int dotD = g_data.DPI(8);
	int gap = g_data.DPI(7);
	int totalW = dotD + gap + szStatus.cx + gap + szTime.cx;
	int x0 = rc.CenterPoint().x - totalW / 2;
	float cy = static_cast<float>(rc.CenterPoint().y);
	float dotR = dotD / 2.0f;
	COLORREF dotColor = m_clock_status == 0 ? MC_UP : MC_TEXT_DIM;
	Gdiplus::SolidBrush dotBrush(Gdi(dotColor));
	g.FillEllipse(&dotBrush, static_cast<Gdiplus::REAL>(x0), cy - dotR, dotR * 2, dotR * 2);
	// 文字颜色与首页股票名标题一致（COLOR_TEXT_PRIMARY 高亮白）
	DrawStr(g, status, fStatus.get(), CRect(x0 + dotD + gap, rc.top, x0 + dotD + gap + szStatus.cx, rc.bottom), RGB(241, 245, 249));
	DrawStr(g, m_clock_time, fTime.get(), CRect(x0 + dotD + gap + szStatus.cx + gap, rc.top, rc.right, rc.bottom), RGB(241, 245, 249));
}

void CMarketCenterPanel::OnTimerTick()
{
	UpdateClock();
	RequestData();
}

void CMarketCenterPanel::OnDataUpdated()
{
	// 数据到达：悬浮窗收到 WM_MC_DATA_UPDATED 后 Invalidate 重绘
}

void CMarketCenterPanel::UpdateClock()
{
	time_t now = time(nullptr);
	struct tm localTm{};
	localtime_s(&localTm, &now);
	wchar_t buf[16];
	swprintf_s(buf, L"%02d:%02d:%02d", localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
	m_clock_time = buf;

	bool weekend = (localTm.tm_wday == 0 || localTm.tm_wday == 6);
	int mins = localTm.tm_hour * 60 + localTm.tm_min;
	bool inSession = !weekend && ((mins >= 9 * 60 + 30 && mins < 11 * 60 + 30) || (mins >= 13 * 60 && mins < 15 * 60));
	m_clock_status = inSession ? 0 : 1;
}

void CMarketCenterPanel::RequestData()
{
	HWND hWnd = m_notify_wnd;
	CMarketCenterData& mc = CMarketCenterData::Instance();
	// 只拉当前页需要的数据（懒加载）：避免一进行情中心就把 ETF 全量 14 页等全部拉完，
	// 拖慢首页（气泡图只需 2 个请求）。切页时 SwitchPage 会再触发对应数据集拉取。
	switch (m_page)
	{
		case PAGE_BUBBLE:
			mc.RequestIfStale(CMarketCenterData::DS_SECTORS, 120, hWnd, CMarketCenterData::RequestPriority::Foreground);
			if (m_sector_view_mode == 1)
				mc.RequestIfStale(CMarketCenterData::DS_SECTOR_TIMELINES, 120, hWnd, CMarketCenterData::RequestPriority::Foreground);
		break;
	case PAGE_ETF_INFLOW:
		case PAGE_ETF_RANK:
			mc.RequestIfStale(CMarketCenterData::DS_ETFS, 300, hWnd, CMarketCenterData::RequestPriority::Foreground);
		break;
		case PAGE_MONEY_FLOW:
		case PAGE_MAINFLOW:
			mc.RequestIfStale(CMarketCenterData::DS_MAINFLOW, 120, hWnd, CMarketCenterData::RequestPriority::Foreground);
		break;
		case PAGE_TREND:
			mc.RequestIfStale(CMarketCenterData::DS_TREND, 60, hWnd, CMarketCenterData::RequestPriority::Foreground);
		break;
	default:
		break;
	}
}

void CMarketCenterPanel::SwitchPage(McPage page)
{
	if (m_page == page)
		return;
	m_page = page;
	m_rank_scroll = 0;
	m_hover_bubble = -1;
	m_hover_inflow_bar = -1;
	m_treemap_mode = 0;   // 离开页面恢复红绿全部视图
	m_hover_bubble_stat = -1;
	m_hover_sector_tab = -1;
	m_hover_timeline_idx = -1;
	m_hover_timeline_sector = -1;
	m_hover_moneyflow_card = -1;
	m_hover_moneyflow_idx = -1;
	m_theme_panel_open = false;
	// 切页后拉取该页数据（懒加载；数据到达由悬浮窗 WM_MC_DATA_UPDATED 触发重绘）
	RequestData();
	// 重绘由悬浮窗在 HandleLButtonDown 后 Invalidate 完成
}

std::wstring CMarketCenterPanel::EtfCodeAt(int idx) const
{
	if (idx < 0 || idx >= static_cast<int>(m_etfs_snapshot.size()))
		return std::wstring();
	return m_etfs_snapshot[static_cast<size_t>(idx)].code;
}

CMarketCenterData::DataSet CMarketCenterPanel::CurrentDataSet() const
{
	switch (m_page)
	{
	case PAGE_BUBBLE: return (m_sector_view_mode == 1) ? CMarketCenterData::DS_SECTOR_TIMELINES : CMarketCenterData::DS_SECTORS;
	case PAGE_ETF_INFLOW:
	case PAGE_ETF_RANK: return CMarketCenterData::DS_ETFS;
	case PAGE_MONEY_FLOW:
	case PAGE_MAINFLOW: return CMarketCenterData::DS_MAINFLOW;
	case PAGE_TREND: return CMarketCenterData::DS_TREND;
	default: return CMarketCenterData::DS_SECTORS;
	}
}

std::wstring CMarketCenterPanel::StatusText(CMarketCenterData::DataSet ds, const std::wstring& loading) const
{
	if (CMarketCenterData::Instance().HasFailed(ds))
		return L"获取失败（网络或接口异常），点击此处重试";
	return loading;
}

void CMarketCenterPanel::RetryCurrentPage()
{
	CMarketCenterData::DataSet ds = CurrentDataSet();
	CMarketCenterData::Instance().Retry(ds, m_notify_wnd);
	RequestData();   // 立即重新请求
}

void CMarketCenterPanel::DrawStatus(Gdiplus::Graphics& g, const CRect& rc, CMarketCenterData::DataSet ds,
	const std::wstring& loading, const Gdiplus::Font* font)
{
	m_status_rect = rc;
	bool failed = CMarketCenterData::Instance().HasFailed(ds);
	DrawStrMid(g, StatusText(ds, loading), font, rc, failed ? RGB(240, 173, 107) : MC_TEXT_DIM);
}

void CMarketCenterPanel::RefreshSnapshots()
{
	CMarketCenterData& mc = CMarketCenterData::Instance();
	std::lock_guard<std::mutex> lock(mc.m_mutex);
	if (mc.m_sectors_time != m_sectors_snapshot_time)
	{
		m_sectors_snapshot = mc.m_sectors;
		m_sectors_snapshot_time = mc.m_sectors_time;
		m_bubble_layout_dirty = true;
		if (m_selected_sector >= static_cast<int>(m_sectors_snapshot.size()))
			m_selected_sector = -1;
	}
	if (mc.m_sector_timelines_time != m_sector_timelines_snapshot_time)
	{
		m_sector_timelines_snapshot = mc.m_sector_timelines;
		m_sector_timelines_snapshot_time = mc.m_sector_timelines_time;
	}
	if (mc.m_etfs_time != m_etfs_snapshot_time)
	{
		m_etfs_snapshot = mc.m_etfs;
		m_etf_total = mc.m_etf_total;
		m_etfs_snapshot_time = mc.m_etfs_time;
		BuildThemeInflow();
	}
}

void CMarketCenterPanel::BuildThemeInflow()
{
	// 主题聚合当日主力净流入（供 ETF申购净流入页 Top10）
	std::map<std::wstring, double> byTheme;
	for (const auto& e : m_etfs_snapshot)
		byTheme[e.theme] += e.inflow;
	m_theme_inflow.clear();
	for (auto& p : byTheme)
		m_theme_inflow.push_back({ p.first, p.second });
	std::sort(m_theme_inflow.begin(), m_theme_inflow.end(),
		[](const ThemeInflow& a, const ThemeInflow& b) { return a.inflow > b.inflow; });
}

int CMarketCenterPanel::ThemeRepresentEtf(int themeIdx) const
{
	if (themeIdx < 0 || themeIdx >= static_cast<int>(m_theme_inflow.size()))
		return -1;
	const std::wstring& theme = m_theme_inflow[static_cast<size_t>(themeIdx)].theme;
	int best = -1;
	double bestAbs = -1.0;
	for (int i = 0; i < static_cast<int>(m_etfs_snapshot.size()); i++)
	{
		const auto& e = m_etfs_snapshot[static_cast<size_t>(i)];
		if (e.theme != theme)
			continue;
		double a = fabs(e.inflow);
		if (a > bestAbs)
		{
			bestAbs = a;
			best = i;
		}
	}
	return best;
}

void CMarketCenterPanel::DrawAll(Gdiplus::Graphics& g, const CRect& client)
{
	RefreshSnapshots();

	const int sidebarW = g_data.DPI(108);
	CRect sidebar(client.left, client.top, client.left + sidebarW, client.bottom);
	CRect content(client.left + sidebarW, client.top, client.right, client.bottom);
	m_content_rect = content;

	// 侧栏底（右边界 +1px 防接缝）
	{
		Gdiplus::SolidBrush panelBrush(Gdi(MC_PANEL));
		g.FillRectangle(&panelBrush, Gdiplus::REAL(sidebar.left), Gdiplus::REAL(sidebar.top),
			Gdiplus::REAL(sidebar.Width() + 1), Gdiplus::REAL(sidebar.Height()));
	}
	DrawSidebar(g, sidebar);
	DrawPage(g, content);
}

void CMarketCenterPanel::DrawSidebar(Gdiplus::Graphics& g, const CRect& rc)
{
	// 菜单第一项直接贴住顶部标题条（无上边距）
	// 菜单项
	const wchar_t* titles[PAGE_COUNT] = { L"板块资金流", L"ETF申购净流入", L"资金流向", L"主力资金", L"涨跌趋势", L"ETF涨跌榜" };
	int itemH = g_data.DPI(34);
	int top = rc.top;
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		CRect item(rc.left, top, rc.right, top + itemH);
		m_menu_item_rects[i] = item;
		top += itemH;

		if (i == m_page)
		{
			Gdiplus::SolidBrush activeBg(Gdi(RGB(45, 62, 90)));
			g.FillRectangle(&activeBg, Gdiplus::REAL(item.left), Gdiplus::REAL(item.top), Gdiplus::REAL(item.Width()), Gdiplus::REAL(item.Height()));
			Gdiplus::SolidBrush barBrush(Gdi(MC_ACCENT));
			g.FillRectangle(&barBrush, Gdiplus::REAL(item.left), Gdiplus::REAL(item.top + g_data.DPI(6)), Gdiplus::REAL(g_data.DPI(3)), Gdiplus::REAL(item.Height() - g_data.DPI(12)));
		}
		else if (i == m_hover_menu)
		{
			Gdiplus::SolidBrush hoverBg(Gdi(MC_TEXT, 10));
			g.FillRectangle(&hoverBg, Gdiplus::REAL(item.left), Gdiplus::REAL(item.top), Gdiplus::REAL(item.Width()), Gdiplus::REAL(item.Height()));
		}

		COLORREF txt = (i == m_page || i == m_hover_menu) ? MC_TEXT : MC_TEXT_SUB;
		auto f12 = MkFont(12, i == m_page);
		DrawStr(g, titles[i], f12.get(), CRect(item.left + g_data.DPI(14), item.top, item.right - g_data.DPI(4), item.bottom), txt);
	}

	// 底部数据来源（单行式，居中）；开市/休市时钟已迁移到顶部标题条（FloatingWnd 调用 DrawHeaderClock）
	auto f10 = MkFont(10);
	DrawStrSingle(g, L"数据来源 - 东方财富", f10.get(), CRect(rc.left, rc.bottom - g_data.DPI(30), rc.right, rc.bottom - g_data.DPI(12)), MC_TEXT_DIM);
	Gdiplus::Pen divPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&divPen, Gdiplus::REAL(rc.right), Gdiplus::REAL(rc.top), Gdiplus::REAL(rc.right), Gdiplus::REAL(rc.bottom));
}

void CMarketCenterPanel::DrawPage(Gdiplus::Graphics& g, const CRect& content)
{
	switch (m_page)
	{
	case PAGE_BUBBLE: DrawBubblePage(g, content); break;
	case PAGE_ETF_INFLOW: DrawEtfInflowPage(g, content); break;
	case PAGE_MONEY_FLOW: DrawMoneyFlowPage(g, content); break;
	case PAGE_MAINFLOW: DrawMainFlowPage(g, content); break;
	case PAGE_TREND: DrawTrendPage(g, content); break;
	case PAGE_ETF_RANK: DrawEtfRankPage(g, content); break;
	}
}

void CMarketCenterPanel::DrawPageTitle(Gdiplus::Graphics& g, const CRect& content, const std::wstring& title, const std::wstring& sub)
{
	auto f13 = MkFont(13, true);
	CRect titleRc(content.left + g_data.DPI(16), content.top + g_data.DPI(16), content.right - g_data.DPI(140), content.top + g_data.DPI(38));
	DrawStr(g, title, f13.get(), titleRc, MC_TEXT);
	if (!sub.empty())
	{
		auto f10 = MkFont(10);
		CSize sz = MeasureStr(g, f10.get(), sub);
		CRect subRc(titleRc.right, titleRc.top, titleRc.right + sz.cx + g_data.DPI(8), titleRc.bottom);
		if (subRc.right <= content.right - g_data.DPI(130))
			DrawStr(g, sub, f10.get(), subRc, MC_TEXT_DIM);
	}
}

// ============ 页面1：基金气泡图（矩形树图版） ============

// squarified treemap：面积 ∝ |主力净流入|，流入列在上、流出列在下，比例接近 1:1 避免细长条
void CMarketCenterPanel::RebuildTreemapLayout(const CRect& chartRc)
{
	m_treemap_cells.clear();
	if (m_sectors_snapshot.empty() || chartRc.Width() < g_data.DPI(100) || chartRc.Height() < g_data.DPI(80))
		return;

	// 按流入/流出分两组，各自降序（squarified 要求按面积降序布局）
	std::vector<int> inIdx, outIdx;
	for (int i = 0; i < static_cast<int>(m_sectors_snapshot.size()); i++)
	{
		if (m_sectors_snapshot[static_cast<size_t>(i)].flow >= 0) inIdx.push_back(i);
		else outIdx.push_back(i);
	}
	auto byMagDesc = [&](int a, int b) {
		return fabs(m_sectors_snapshot[static_cast<size_t>(a)].flow) > fabs(m_sectors_snapshot[static_cast<size_t>(b)].flow);
	};
	std::sort(inIdx.begin(), inIdx.end(), byMagDesc);
	std::sort(outIdx.begin(), outIdx.end(), byMagDesc);

	const float W = static_cast<float>(chartRc.Width());
	const float H = static_cast<float>(chartRc.Height());
	float totalIn = 0, totalOut = 0;
	for (int i : inIdx) totalIn += static_cast<float>(fabs(m_sectors_snapshot[static_cast<size_t>(i)].flow));
	for (int i : outIdx) totalOut += static_cast<float>(fabs(m_sectors_snapshot[static_cast<size_t>(i)].flow));
	const float total = totalIn + totalOut;
	if (total <= 0)
		return;

	const float GAP = 2.0f * (static_cast<float>(g_data.GetDpi()) / 96.0f);   // 单元间缝隙
	const float inH = H * (totalIn / total);   // 上半区（流入）高度

	struct Row { float x, y, w, h; };
	// 在给定区域内按 squarified 算法铺一批板块（面积 = |flow|/total * 区域面积）
	auto layRow = [&](const std::vector<int>& idxs, float x, float y, float w, float h)
	{
		if (idxs.empty() || w < GAP || h < GAP)
			return;
		float area = w * h;
		// 区域内各板块面积（按组内面积占比归一，铺满整个区域）
		float groupSum = 0;
		for (int j : idxs)
			groupSum += static_cast<float>(fabs(m_sectors_snapshot[static_cast<size_t>(j)].flow));
		groupSum = max(groupSum, 0.001f);
		std::vector<float> a(idxs.size());
		for (size_t k = 0; k < idxs.size(); k++)
			a[k] = area * static_cast<float>(fabs(m_sectors_snapshot[static_cast<size_t>(idxs[k])].flow)) / groupSum;
		std::vector<Row> rows;
		size_t k = 0;
		float rx = x, ry = y, rw = w, rh = h;
		while (k < idxs.size())
		{
			// 经典 squarified：行沿较长边横跨，厚度沿较短边累加；贪心加入直到行内最差长宽比不再改善
			bool horiz = rw >= rh;           // horiz: 水平行，横跨宽度 rw
			float span = horiz ? rw : rh;    // 行横跨的边长（行厚 = 面积和 / span）
			float cross = horiz ? rh : rw;   // 厚度方向剩余空间
			float bestAsp = -1.0f, bestThick = 0;
			size_t bestK = k;
			float acc = 0;
			for (size_t e = k; e < idxs.size(); e++)
			{
				float next = acc + a[e];
				float th = next / max(0.001f, span);   // 行厚 = 面积和 / 横跨边
				if (th > cross)                        // 超出剩余厚度，此格放入下行
					break;
				float worst = 0;
				for (size_t m2 = k; m2 <= e; m2++)
				{
					float cw = a[m2] / max(0.001f, th);
					worst = max(worst, max(cw / max(0.001f, th), th / max(0.001f, cw)));
				}
				if (bestAsp < 0 || worst < bestAsp)
				{
					bestAsp = worst; bestK = e; bestThick = th;
					acc = next;
				}
				else break;
			}
			if (bestAsp < 0)
			{
				// 首格就放不进剩余区域（尾部小格）：整块剩余区域给这一格，兜底防死循环
				if (horiz)
				{
					rows.push_back({ rx, ry, rw, cross });
					ry += cross; rh -= cross;
				}
				else
				{
					rows.push_back({ rx, ry, cross, rh });
					rx += cross; rw -= cross;
				}
				k += 1;
				if (rw < GAP || rh < GAP)
					break;
				continue;
			}
			float th = bestThick;
			if (horiz)
			{
				// 水平行：占满 rw 宽、厚 th
				float cx = rx;
				for (size_t m2 = k; m2 <= bestK; m2++)
				{
					float cw = a[m2] / max(0.001f, th);
					rows.push_back({ cx, ry, cw, th });
					cx += cw;
				}
				ry += th; rh -= th;
			}
			else
			{
				// 垂直列：占满 rh 高、厚 th
				float cy = ry;
				for (size_t m2 = k; m2 <= bestK; m2++)
				{
					float ch = a[m2] / max(0.001f, th);
					rows.push_back({ rx, cy, th, ch });
					cy += ch;
				}
				rx += th; rw -= th;
			}
			k = bestK + 1;
			if (rw < GAP || rh < GAP)
				break;
		}
		// 写入结果（rows 顺序即布局顺序，对应 idxs 前缀）
		for (size_t m2 = 0; m2 < rows.size() && m2 < idxs.size(); m2++)
		{
			const Row& r = rows[m2];
			int si = idxs[m2];
			CRect rc(static_cast<int>(r.x + GAP / 2), static_cast<int>(r.y + GAP / 2),
				static_cast<int>(r.x + r.w - GAP / 2), static_cast<int>(r.y + r.h - GAP / 2));
			if (rc.Width() < 2 || rc.Height() < 2)
				continue;
			m_treemap_cells.push_back({ si, rc });
		}
	};

	// 铺排：模式 1/2 单色视图整区只铺一个组；模式 0 上半区流入、下半区流出
	m_treemap_cells.clear();
	if (m_treemap_mode == 1)
		layRow(inIdx, chartRc.left, chartRc.top, W, H);
	else if (m_treemap_mode == 2)
		layRow(outIdx, chartRc.left, chartRc.top, W, H);
	else
	{
		layRow(inIdx, chartRc.left, chartRc.top, W, max(GAP * 2, inH));
		layRow(outIdx, chartRc.left, chartRc.top + inH, W, H - inH);
	}

	m_bubble_layout_dirty = false;
}

void CMarketCenterPanel::DrawBubblePage(Gdiplus::Graphics& g, const CRect& rc)
{
	// 无数据：提示 + 由 RequestData 拉取
	if (m_sectors_snapshot.empty())
	{
		auto f12 = MkFont(12);
		if (CMarketCenterData::Instance().IsPremarketNoData(CMarketCenterData::DS_SECTORS))
			DrawStatus(g, rc, CMarketCenterData::DS_SECTORS, L"盘前/清算时段，暂无板块资金流数据（开盘后自动恢复）", f12.get());
		else
			DrawStatus(g, rc, CMarketCenterData::DS_SECTORS, L"正在获取行业板块资金流…", f12.get());
		return;
	}

	auto f10 = MkFont(10);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12 = MkFont(12);
	auto f12b = MkFont(12, true);
	auto f14b = MkFont(14, true);
	auto f15b = MkFont(15, true);
	auto f16b = MkFont(16, true);
	auto f20b = MkFont(20, true);

	// 统计条（grid4）：流入合计/流出合计/最强板块/最弱板块
	int maxIdx = 0, minIdx = 0;
	double totalIn = 0, totalOut = 0;
	for (int i = 0; i < static_cast<int>(m_sectors_snapshot.size()); i++)
	{
		const auto& s = m_sectors_snapshot[static_cast<size_t>(i)];
		if (s.flow > m_sectors_snapshot[static_cast<size_t>(maxIdx)].flow) maxIdx = i;
		if (s.flow < m_sectors_snapshot[static_cast<size_t>(minIdx)].flow) minIdx = i;
		if (s.flow > 0) totalIn += s.flow;
		else totalOut += s.flow;
	}
	if (m_selected_sector < 0)
		m_selected_sector = maxIdx;

	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(16), rc.right - g_data.DPI(16), rc.top + g_data.DPI(16) + g_data.DPI(50));
	FillCard(g, statsRc);
	const StatCell cells[4] = {
		{ L"流入合计", FormatYi(totalIn), MC_UP },
		{ L"流出合计", FormatYi(totalOut), MC_DOWN },
		{ L"最强板块", m_sectors_snapshot[static_cast<size_t>(maxIdx)].name + L"  " + FormatYi(m_sectors_snapshot[static_cast<size_t>(maxIdx)].flow), MC_TEXT },
		{ L"最弱板块", m_sectors_snapshot[static_cast<size_t>(minIdx)].name + L"  " + FormatYi(m_sectors_snapshot[static_cast<size_t>(minIdx)].flow), MC_TEXT },
	};
	int cellW = statsRc.Width() / 4;
	for (int i = 0; i < 4; i++)
	{
		CRect cell(statsRc.left + i * cellW, statsRc.top, statsRc.left + (i + 1) * cellW, statsRc.bottom);
		if (i > 0)
		{
			Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&sepPen, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top + g_data.DPI(10)), Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.bottom - g_data.DPI(10)));
		}
		// 流入/流出合计是树图单色视图开关：激活描边、悬停提亮
		bool statActive = (i < 2 && m_treemap_mode == i + 1);
		if (i < 2)
		{
			m_bubble_stat_rects[i] = cell;
			if (statActive)
			{
				Gdiplus::Pen actPen(Gdi(MC_ACCENT), 1.6f);
				g.DrawRectangle(&actPen, Gdiplus::REAL(cell.left + 1), Gdiplus::REAL(cell.top + 1),
					Gdiplus::REAL(cell.Width() - 2), Gdiplus::REAL(cell.Height() - 2));
			}
			else if (m_hover_bubble_stat == i)
			{
				Gdiplus::SolidBrush hovBrush(Gdi(MC_TEXT, 14));
				g.FillRectangle(&hovBrush, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top),
					Gdiplus::REAL(cell.Width()), Gdiplus::REAL(cell.Height()));
			}
		}
		DrawStrMid(g, cells[i].label, f11.get(), CRect(cell.left, cell.top + g_data.DPI(6), cell.right, cell.top + g_data.DPI(21)),
			statActive || (i < 2 && m_hover_bubble_stat == i) ? MC_TEXT : MC_TEXT_SUB);
		DrawStrMid(g, cells[i].value, f15b.get(), CRect(cell.left, cell.top + g_data.DPI(21), cell.right, cell.bottom - g_data.DPI(4)), cells[i].color);
	}

	// 主区域：矩形树图 + 右侧详情
	CRect bodyRc(statsRc.left, statsRc.bottom + g_data.DPI(8), statsRc.right, rc.bottom - g_data.DPI(16));
	const int detailW = g_data.DPI(150);
	m_bubble_detail_rect = CRect(bodyRc.right - detailW, bodyRc.top, bodyRc.right, bodyRc.bottom);
	m_bubble_chart_rect = CRect(bodyRc.left, bodyRc.top, m_bubble_detail_rect.left - g_data.DPI(8), bodyRc.bottom);

	if (m_sector_view_mode == 0)
	{
		// 树图布局（数据、尺寸或单色模式变化时重排）
		if (m_bubble_layout_dirty)
			RebuildTreemapLayout(m_bubble_chart_rect);

		// 当前选中板块不在可见集合（切了单色视图/数据刷新）时，回落到可见最大格
		{
			bool selVisible = false;
			for (const auto& c : m_treemap_cells)
				if (c.sectorIdx == m_selected_sector) { selVisible = true; break; }
			if (!selVisible && !m_treemap_cells.empty())
				m_selected_sector = m_treemap_cells.front().sectorIdx;
		}

		float maxMag = 1.0f;
		for (const auto& s : m_sectors_snapshot)
			maxMag = max(maxMag, static_cast<float>(fabs(s.flow)));

		for (const auto& cellNode : m_treemap_cells)
		{
			const auto& s = m_sectors_snapshot[static_cast<size_t>(cellNode.sectorIdx)];
			bool selected = (cellNode.sectorIdx == m_selected_sector);
			bool hovered = (cellNode.sectorIdx == m_hover_bubble);
			// 按规模做颜色插值：小格暗、大格亮
			float mag = min(1.0f, sqrtf(static_cast<float>(fabs(s.flow)) / maxMag));
			float t = 0.38f + mag * 0.62f;
			bool inGroup = (s.flow >= 0);
			// 单色视图：仅流入/仅流出时全格统一用该组颜色
			if (m_treemap_mode == 1) inGroup = true;
			else if (m_treemap_mode == 2) inGroup = false;
			float base[3], bright[3];
			if (inGroup)
			{
				base[0] = 163; base[1] = 57; base[2] = 52;
				bright[0] = 255; bright[1] = 59; bright[2] = 48;
			}
			else
			{
				base[0] = 23; base[1] = 108; base[2] = 77;
				bright[0] = 0; bright[1] = 209; bright[2] = 125;
			}
			BYTE r = static_cast<BYTE>(base[0] + (bright[0] - base[0]) * t);
			BYTE gg = static_cast<BYTE>(base[1] + (bright[1] - base[1]) * t);
			BYTE b = static_cast<BYTE>(base[2] + (bright[2] - base[2]) * t);
			Gdiplus::SolidBrush fillBrush(Gdiplus::Color(selected || hovered ? BYTE(255) : BYTE(225), r, gg, b));
			g.FillRectangle(&fillBrush, Gdiplus::REAL(cellNode.rect.left), Gdiplus::REAL(cellNode.rect.top),
				Gdiplus::REAL(cellNode.rect.Width()), Gdiplus::REAL(cellNode.rect.Height()));
			if (selected || hovered)
			{
				Gdiplus::Pen strokePen(Gdi(RGB(255, 255, 255)), selected ? 2.2f : 1.4f);
				g.DrawRectangle(&strokePen, Gdiplus::REAL(cellNode.rect.left), Gdiplus::REAL(cellNode.rect.top),
					Gdiplus::REAL(cellNode.rect.Width()), Gdiplus::REAL(cellNode.rect.Height()));
			}

			// 文字：格子足够大时两行（名称 + 净流入），够放名称时一行，太小不画
			int minSide = min(cellNode.rect.Width(), cellNode.rect.Height());
			int fontSize = static_cast<int>(clampf(minSide * 0.26f / (g_data.GetDpi() / 96.0f), 9.0f, 13.0f));
			if (cellNode.rect.Width() >= g_data.DPI(34) && cellNode.rect.Height() >= g_data.DPI(30))
			{
				auto fTxt = MkFont(fontSize, true);
				auto fVal = MkFont(max(9, fontSize - 2));
				// 名称单行、溢出省略（完整名称在右侧详情卡展示）
				DrawStrSingle(g, s.name, fTxt.get(), CRect(cellNode.rect.left, cellNode.rect.top, cellNode.rect.right, cellNode.rect.top + cellNode.rect.Height() / 2 + g_data.DPI(2)), RGB(255, 255, 255));
				DrawStrMid(g, FormatYi(s.flow), fVal.get(), CRect(cellNode.rect.left, cellNode.rect.top + cellNode.rect.Height() / 2 - g_data.DPI(2), cellNode.rect.right, cellNode.rect.bottom), RGB(255, 255, 255));
			}
			else if (cellNode.rect.Width() >= g_data.DPI(26) && minSide >= g_data.DPI(15))
			{
				auto fTxt = MkFont(fontSize, true);
				DrawStrSingle(g, s.name, fTxt.get(), cellNode.rect, RGB(255, 255, 255));
			}
		}
	}
	else
	{
		DrawSectorTimelinePage(g, m_bubble_chart_rect);
	}

	// 右侧详情栏
	FillCard(g, m_bubble_detail_rect);
	const int P = g_data.DPI(9);
	CRect dc1(m_bubble_detail_rect.left + P, m_bubble_detail_rect.top + P, m_bubble_detail_rect.right - P, m_bubble_detail_rect.bottom - P);

	// 始终计算并绘制底部的切换 Tab 与 数据时间
	CRect rcTime(dc1.left, dc1.bottom - g_data.DPI(16), dc1.right, dc1.bottom);
	int tabH = g_data.DPI(24);
	int tabBottom = rcTime.top - g_data.DPI(6);
	int tabTop = tabBottom - tabH;
	CRect tabRc(dc1.left, tabTop, dc1.right, tabBottom);
	int halfW = tabRc.Width() / 2;
	m_sector_tab_rects[0] = CRect(tabRc.left, tabRc.top, tabRc.left + halfW, tabRc.bottom);
	m_sector_tab_rects[1] = CRect(tabRc.left + halfW, tabRc.top, tabRc.right, tabRc.bottom);

	// 绘制 Tab 容器背景与边框
	Gdiplus::SolidBrush tabBgBrush(Gdi(MC_TEXT, 12));
	g.FillRectangle(&tabBgBrush, Gdiplus::REAL(tabRc.left), Gdiplus::REAL(tabRc.top),
		Gdiplus::REAL(tabRc.Width()), Gdiplus::REAL(tabRc.Height()));
	Gdiplus::Pen tabBorderPen(Gdi(MC_BORDER), 1.0f);
	g.DrawRectangle(&tabBorderPen, Gdiplus::REAL(tabRc.left), Gdiplus::REAL(tabRc.top),
		Gdiplus::REAL(tabRc.Width()), Gdiplus::REAL(tabRc.Height()));

	const wchar_t* tabLabels[2] = { L"资金树图", L"时间走向" };
	for (int i = 0; i < 2; i++)
	{
		CRect rTab = m_sector_tab_rects[i];
		bool active = (m_sector_view_mode == i);
		bool hov = (m_hover_sector_tab == i);
		if (active)
		{
			Gdiplus::SolidBrush actBrush(Gdi(MC_ACCENT));
			g.FillRectangle(&actBrush, Gdiplus::REAL(rTab.left), Gdiplus::REAL(rTab.top),
				Gdiplus::REAL(rTab.Width()), Gdiplus::REAL(rTab.Height()));
		}
		else if (hov)
		{
			Gdiplus::SolidBrush hovBrush(Gdi(MC_TEXT, 25));
			g.FillRectangle(&hovBrush, Gdiplus::REAL(rTab.left), Gdiplus::REAL(rTab.top),
				Gdiplus::REAL(rTab.Width()), Gdiplus::REAL(rTab.Height()));
		}
		DrawStrMid(g, tabLabels[i], active ? f11b.get() : f11.get(), rTab,
			active ? RGB(255, 255, 255) : (hov ? MC_TEXT : MC_TEXT_SUB));
	}

	// 绘制数据时间
	DrawStrMid(g, std::wstring(L"数据时间 ") + (m_clock_time.empty() ? L"--" : m_clock_time.substr(0, 5)), f10.get(),
		rcTime, MC_TEXT_DIM);

	if (m_selected_sector >= 0 && m_selected_sector < static_cast<int>(m_sectors_snapshot.size()))
	{
		const auto& s = m_sectors_snapshot[static_cast<size_t>(m_selected_sector)];
		// 完整板块名：标题（f12b）两行内放不下时截断加省略号
		std::wstring dispName = s.name;
		{
			int availW = dc1.Width();
			if (MeasureStr(g, f12b.get(), dispName).cx > availW * 2 - g_data.DPI(4))
			{
				std::wstring ell = L"…";
				int limit = availW * 2 - MeasureStr(g, f12b.get(), ell).cx;
				size_t n = dispName.size();
				while (n > 0 && MeasureStr(g, f12b.get(), dispName.substr(0, n)).cx > limit)
					n--;
				dispName = dispName.substr(0, n) + ell;
			}
		}
		// 名称区高度按实际行数收放，单行时顶部间距与左右一致
		int nameLines = (MeasureStr(g, f12b.get(), dispName).cx > dc1.Width()) ? 2 : 1;
		CRect rcName(dc1.left, dc1.top, dc1.right, dc1.top + g_data.DPI(19) * nameLines);
		CRect rcFlow(dc1.left, rcName.bottom + g_data.DPI(4), dc1.right, rcName.bottom + g_data.DPI(32));
		DrawStr(g, dispName, f12b.get(), rcName, MC_TEXT, 255, Gdiplus::StringAlignmentFar);
		DrawStr(g, FormatYi(s.flow), f20b.get(), rcFlow, UpDownColor(s.flow), 255, Gdiplus::StringAlignmentFar);
		// 明细行：涨跌幅/主力/超大/大/中/小
		struct Row { const wchar_t* label; std::wstring value; COLORREF color; };
		Row rows[6] = {
			{ L"涨跌幅", FormatPct(s.pct), UpDownColor(s.pct) },
			{ L"主力净流入", FormatYi(s.flow), UpDownColor(s.flow) },
			{ L"超大单", FormatYi(s.superBig), UpDownColor(s.superBig) },
			{ L"大单", FormatYi(s.big), UpDownColor(s.big) },
			{ L"中单", FormatYi(s.mid), UpDownColor(s.mid) },
			{ L"小单", FormatYi(s.smallOrder), UpDownColor(s.smallOrder) },
		};
		int rowTop = rcFlow.bottom + g_data.DPI(18);
		int rowH = g_data.DPI(27);
		for (int i = 0; i < 6; i++)
		{
			CRect rRow(dc1.left, rowTop, dc1.right, rowTop + rowH);
			rowTop += rowH;
			// 首行（涨跌幅）上方不画分隔线，其余行保留
			if (i > 0)
			{
				Gdiplus::Pen rowPen(Gdi(MC_BORDER), 1.0f);
				g.DrawLine(&rowPen, Gdiplus::REAL(rRow.left), Gdiplus::REAL(rRow.top), Gdiplus::REAL(rRow.right), Gdiplus::REAL(rRow.top));
			}
			DrawStr(g, rows[i].label, f11.get(), CRect(rRow.left, rRow.top, rRow.CenterPoint().x, rRow.bottom), MC_TEXT_SUB);
			DrawStr(g, rows[i].value, f11.get(), CRect(rRow.CenterPoint().x, rRow.top, rRow.right, rRow.bottom), rows[i].color, 255, Gdiplus::StringAlignmentFar);
		}
	}
	else
	{
		DrawStrMid(g, L"选择或悬停板块查看详情", f11.get(), CRect(dc1.left, dc1.top + g_data.DPI(60), dc1.right, tabTop - g_data.DPI(20)), MC_TEXT_DIM);
	}
}

void CMarketCenterPanel::DrawSectorTimelinePage(Gdiplus::Graphics& g, const CRect& chartRc)
{
	FillCard(g, chartRc);

	if (m_sector_timelines_snapshot.empty())
	{
		auto f12 = MkFont(12);
		if (CMarketCenterData::Instance().IsPremarketNoData(CMarketCenterData::DS_SECTOR_TIMELINES))
			DrawStatus(g, chartRc, CMarketCenterData::DS_SECTOR_TIMELINES, L"盘前/清算时段，暂无板块资金分时走向数据", f12.get());
		else
			DrawStatus(g, chartRc, CMarketCenterData::DS_SECTOR_TIMELINES, L"正在获取板块资金时间走向…", f12.get());
		return;
	}

	auto f10 = MkFont(10);
	auto f10b = MkFont(10, true);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);

	// 15 种流入暖色系（红、橙、黄、粉、紫）
	static const COLORREF s_inflowColors[15] = {
		RGB(255, 59, 48),   // 0 鲜红
		RGB(255, 99, 71),   // 1 番茄红
		RGB(255, 140, 0),  // 2 深橙
		RGB(255, 165, 0),  // 3 橙色
		RGB(255, 193, 7),   // 4 琥珀黄
		RGB(255, 214, 0),  // 5 明黄
		RGB(255, 235, 59),  // 6 亮黄
		RGB(255, 64, 129),  // 7 亮粉红
		RGB(245, 0, 87),    // 8 艳粉
		RGB(233, 30, 99),   // 9 玫瑰红
		RGB(224, 64, 251),  // 10 荧光紫
		RGB(171, 71, 188),  // 11 丁香紫
		RGB(255, 112, 67),  // 12 珊瑚橙
		RGB(216, 67, 21),   // 13 赭红
		RGB(255, 138, 128)  // 14 浅鲑红
	};

	// 15 种流出偏绿冷色系（翠绿、亮绿、柠檬绿、绿松石、青蓝、天蓝等）
	static const COLORREF s_outflowColors[15] = {
		RGB(0, 209, 125),   // 0 翠绿
		RGB(0, 230, 118),   // 1 亮绿
		RGB(100, 221, 23),  // 2 柠檬绿
		RGB(118, 255, 3),   // 3 荧光草绿
		RGB(29, 233, 182),  // 4 绿松石
		RGB(0, 191, 165),   // 5 深水绿
		RGB(0, 229, 255),   // 6 青蓝
		RGB(0, 184, 212),   // 7 深青
		RGB(64, 196, 255),  // 8 浅天蓝
		RGB(41, 121, 255),  // 9 湛蓝
		RGB(76, 175, 80),   // 10 森林绿
		RGB(139, 195, 74),  // 11 黄绿
		RGB(38, 166, 154),  // 12 波斯绿
		RGB(128, 222, 234), // 13 浅青
		RGB(128, 216, 255)  // 14 天空蓝
	};

	// 分离流入与流出集合
	std::vector<const MC::SectorTimeline*> allInflows;
	std::vector<const MC::SectorTimeline*> allOutflows;
	for (const auto& tl : m_sector_timelines_snapshot)
	{
		if (tl.finalFlow >= 0) allInflows.push_back(&tl);
		else allOutflows.push_back(&tl);
	}

	// 筛选可见曲线（全部模式展示 15 个：8流入+7流出；单选流入/流出均各展示 Top 15）
	struct TimelineItem {
		size_t origIdx;
		const MC::SectorTimeline* pTl;
		COLORREF color;
	};
	std::vector<TimelineItem> visibleTimelines;

	if (m_treemap_mode == 1)
	{
		// 仅流入：展示净流入前 15 个板块，全部采用暖红橙色系
		size_t cnt = min(static_cast<size_t>(15), allInflows.size());
		for (size_t i = 0; i < cnt; ++i)
		{
			visibleTimelines.push_back({ i, allInflows[i], s_inflowColors[i % 15] });
		}
	}
	else if (m_treemap_mode == 2)
	{
		// 仅流出：展示净流出前 15 个板块，全部采用偏绿冷色系
		size_t cnt = min(static_cast<size_t>(15), allOutflows.size());
		for (size_t i = 0; i < cnt; ++i)
		{
			visibleTimelines.push_back({ i, allOutflows[i], s_outflowColors[i % 15] });
		}
	}
	else
	{
		// 合计（全部）：流入 8 个 + 流出 7 个（共 15 个）
		size_t inCnt = min(static_cast<size_t>(8), allInflows.size());
		size_t outCnt = min(static_cast<size_t>(7), allOutflows.size());
		if (inCnt + outCnt < 15)
		{
			while (inCnt < allInflows.size() && inCnt + outCnt < 15) inCnt++;
			while (outCnt < allOutflows.size() && inCnt + outCnt < 15) outCnt++;
		}
		for (size_t i = 0; i < inCnt; ++i)
		{
			visibleTimelines.push_back({ i, allInflows[i], s_inflowColors[i % 15] });
		}
		for (size_t i = 0; i < outCnt; ++i)
		{
			visibleTimelines.push_back({ inCnt + i, allOutflows[i], s_outflowColors[i % 15] });
		}
	}

	// 坐标区域：左侧刻度(50px)、右侧末端标签(84px)、顶部标题栏(28px)、底部时间轴(24px)
	const int padL = g_data.DPI(50);
	const int padR = g_data.DPI(84);
	const int padT = g_data.DPI(28);
	const int padB = g_data.DPI(24);
	CRect plotRc(chartRc.left + padL, chartRc.top + padT, chartRc.right - padR, chartRc.bottom - padB);
	m_sector_timeline_inner_rect = plotRc;
	if (plotRc.Width() < g_data.DPI(100) || plotRc.Height() < g_data.DPI(60))
		return;

	// 计算全局 Y 轴范围并强制包含 0 轴
	double flowLo = 0.0, flowHi = 0.0;
	bool hasVal = false;
	for (const auto& item : visibleTimelines)
	{
		for (double v : item.pTl->points)
		{
			if (isnan(v)) continue;
			flowLo = min(flowLo, v);
			flowHi = max(flowHi, v);
			hasVal = true;
		}
	}
	if (!hasVal) { flowLo = -10.0; flowHi = 10.0; }
	flowLo = min(flowLo, 0.0);
	flowHi = max(flowHi, 0.0);
	double span = flowHi - flowLo;
	if (span < 1.0) span = 1.0;
	double flowPad = max(1.0, span * 0.10);
	flowLo -= flowPad;
	flowHi += flowPad;

	auto fx = [&](int i) -> float {
		return plotRc.left + static_cast<float>(i) * plotRc.Width() / 240.0f;
	};
	auto fy = [&](double v) -> float {
		return static_cast<float>(plotRc.bottom - (v - flowLo) / (flowHi - flowLo) * plotRc.Height());
	};

	// 绘制横向网格与 Y 轴刻度（细化刻度步长，保证正负两侧均有清晰参考阶梯）
	int targetDivs = max(6, plotRc.Height() / g_data.DPI(32));
	double step = NiceStep(flowHi - flowLo, static_cast<double>(targetDivs));
	for (double tv = ceil(flowLo / step) * step; tv <= flowHi; tv += step)
	{
		float y = fy(tv);
		if (y < plotRc.top - 2 || y > plotRc.bottom + 2) continue;
		bool isZero = fabs(tv) < 1e-6;
		Gdiplus::Pen gridPen(Gdi(isZero ? RGB(90, 100, 120) : MC_GRID), isZero ? 1.5f : 1.0f);
		g.DrawLine(&gridPen, Gdiplus::REAL(plotRc.left), y, Gdiplus::REAL(plotRc.right), y);

		std::wstring lbl = (tv > 0 ? L"+" : L"") + FormatAxisNum(tv, step) + L"亿";
		CRect lblRc(chartRc.left + g_data.DPI(2), static_cast<int>(y) - g_data.DPI(8), plotRc.left - g_data.DPI(4), static_cast<int>(y) + g_data.DPI(8));
		DrawStr(g, lbl, f10.get(), lblRc, isZero ? RGB(255, 255, 255) : MC_TEXT_SUB, 255, Gdiplus::StringAlignmentFar);
	}

	// 绘制 X 轴时间刻度 09:30 10:30 11:30/13:00 14:00 15:00
	const int timeMarks[5] = { 0, 60, 120, 180, 240 };
	const wchar_t* timeLabels[5] = { L"09:30", L"10:30", L"11:30/13:00", L"14:00", L"15:00" };
	for (int k = 0; k < 5; k++)
	{
		int mi = timeMarks[k];
		float x = fx(mi);
		CRect lblRc(static_cast<int>(x) - g_data.DPI(30), plotRc.bottom + g_data.DPI(4), static_cast<int>(x) + g_data.DPI(30), plotRc.bottom + g_data.DPI(20));
		DrawStrMid(g, timeLabels[k], f10.get(), lblRc, MC_TEXT_SUB);
	}

	// 右边界竖向截断线 (15:00 时间轴终点基准线)
	Gdiplus::Pen cutPen(Gdi(RGB(85, 95, 115)), 1.2f);
	g.DrawLine(&cutPen, Gdiplus::REAL(plotRc.right), Gdiplus::REAL(plotRc.top),
		Gdiplus::REAL(plotRc.right), Gdiplus::REAL(plotRc.bottom));

	// 判断当前聚焦/悬停的曲线
	int focusedTlIdx = -1;
	float minDistY = static_cast<float>(g_data.DPI(18));
	if (m_hover_timeline_idx >= 0 && m_hover_timeline_idx <= 240 && plotRc.PtInRect(m_mouse_pos))
	{
		for (int i = 0; i < static_cast<int>(visibleTimelines.size()); i++)
		{
			const auto* pTl = visibleTimelines[static_cast<size_t>(i)].pTl;
			if (m_hover_timeline_idx < static_cast<int>(pTl->points.size()))
			{
				float cy = fy(pTl->points[static_cast<size_t>(m_hover_timeline_idx)]);
				float dist = fabs(cy - static_cast<float>(m_mouse_pos.y));
				if (dist < minDistY)
				{
					minDistY = dist;
					focusedTlIdx = i;
				}
			}
		}
	}
	if (focusedTlIdx < 0 && m_selected_sector >= 0 && m_selected_sector < static_cast<int>(m_sectors_snapshot.size()))
	{
		const auto& selCode = m_sectors_snapshot[static_cast<size_t>(m_selected_sector)].code;
		for (int i = 0; i < static_cast<int>(visibleTimelines.size()); i++)
		{
			if (visibleTimelines[static_cast<size_t>(i)].pTl->code == selCode)
			{
				focusedTlIdx = i;
				break;
			}
		}
	}

	// 记录当前悬停/聚焦板块在 m_sectors_snapshot 中的下标
	m_hover_timeline_sector = -1;
	if (focusedTlIdx >= 0 && focusedTlIdx < static_cast<int>(visibleTimelines.size()))
	{
		const std::wstring& fCode = visibleTimelines[static_cast<size_t>(focusedTlIdx)].pTl->code;
		for (int sIdx = 0; sIdx < static_cast<int>(m_sectors_snapshot.size()); ++sIdx)
		{
			if (m_sectors_snapshot[static_cast<size_t>(sIdx)].code == fCode)
			{
				m_hover_timeline_sector = sIdx;
				break;
			}
		}
	}

	// 绘制平滑曲线（启用抗锯齿）
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	for (int i = 0; i < static_cast<int>(visibleTimelines.size()); i++)
	{
		const auto& item = visibleTimelines[static_cast<size_t>(i)];
		const auto* pTl = item.pTl;
		if (pTl->points.size() < 2) continue;
		bool isFocused = (i == focusedTlIdx);
		BYTE alpha = (focusedTlIdx >= 0) ? (isFocused ? 255 : 85) : 225;
		float width = isFocused ? 2.5f : 1.6f;
		COLORREF col = item.color;

		std::vector<Gdiplus::PointF> pts;
		pts.reserve(pTl->points.size());
		for (size_t k = 0; k < pTl->points.size() && k <= 240; ++k)
		{
			pts.push_back(Gdiplus::PointF(fx(static_cast<int>(k)), fy(pTl->points[k])));
		}
		if (pts.size() >= 2)
		{
			Gdiplus::Pen pen(Gdi(col, alpha), width);
			g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
		}
	}

	// 垂直虚线十字光标
	if (m_hover_timeline_idx >= 0 && m_hover_timeline_idx <= 240 && plotRc.PtInRect(m_mouse_pos))
	{
		float curX = fx(m_hover_timeline_idx);
		Gdiplus::Pen crossPen(Gdi(RGB(180, 190, 210), 140), 1.0f);
		crossPen.SetDashStyle(Gdiplus::DashStyleDash);
		g.DrawLine(&crossPen, curX, static_cast<float>(plotRc.top), curX, static_cast<float>(plotRc.bottom));
	}

	// 顶部概要栏（当前/悬停时刻流入居首与流出居首）
	int queryIdx = (m_hover_timeline_idx >= 0 && m_hover_timeline_idx <= 240) ? m_hover_timeline_idx : 240;
	const MC::SectorTimeline* topInTl = nullptr;
	const MC::SectorTimeline* topOutTl = nullptr;
	double maxInVal = -1e9, minOutVal = 1e9;

	for (const auto& item : visibleTimelines)
	{
		const auto* pTl = item.pTl;
		if (pTl->points.empty()) continue;
		int idx = min(queryIdx, static_cast<int>(pTl->points.size()) - 1);
		double val = pTl->points[static_cast<size_t>(idx)];
		if (val > 0 && val > maxInVal) { maxInVal = val; topInTl = pTl; }
		if (val < 0 && val < minOutVal) { minOutVal = val; topOutTl = pTl; }
	}

	std::wstring headerStr;
	const auto& axis = CMarketCenterData::TimeAxis();
	std::wstring tStr = (queryIdx < static_cast<int>(axis.size())) ? axis[static_cast<size_t>(queryIdx)] : (m_clock_time.empty() ? L"--" : m_clock_time.substr(0, 5));
	std::wstring modeTitle = (m_treemap_mode == 1 ? L"净流入 Top15" : (m_treemap_mode == 2 ? L"净流出 Top15" : L"代表板块"));
	headerStr = tStr + L"  主力资金时间走向 (" + modeTitle + L")";
	if (focusedTlIdx >= 0 && focusedTlIdx < static_cast<int>(visibleTimelines.size()))
	{
		const auto* fTl = visibleTimelines[static_cast<size_t>(focusedTlIdx)].pTl;
		int idx = min(queryIdx, static_cast<int>(fTl->points.size()) - 1);
		double val = (idx >= 0) ? fTl->points[static_cast<size_t>(idx)] : fTl->finalFlow;
		headerStr += L"  |  聚焦: " + fTl->name + L" " + FormatYi(val * 1e8, 1);
	}
	else if (topInTl || topOutTl)
	{
		if (topInTl) headerStr += L"  |  流入首位: " + topInTl->name + L" " + FormatYi(maxInVal * 1e8, 1);
		if (topOutTl) headerStr += L"  |  流出首位: " + topOutTl->name + L" " + FormatYi(minOutVal * 1e8, 1);
	}
	DrawStr(g, headerStr, f11.get(), CRect(plotRc.left, chartRc.top + g_data.DPI(5), plotRc.right + padR, plotRc.top - g_data.DPI(3)), MC_TEXT);

	// 右侧末端标签（只显示板块名称，带防重叠避让算法）
	struct LabelNode {
		int tlIdx;
		float targetY;
		float y;
		std::wstring text;
		COLORREF color;
	};
	std::vector<LabelNode> nodes;
	for (int i = 0; i < static_cast<int>(visibleTimelines.size()); i++)
	{
		const auto& item = visibleTimelines[static_cast<size_t>(i)];
		const auto* pTl = item.pTl;
		double val = pTl->finalFlow;
		if (m_hover_timeline_idx >= 0 && m_hover_timeline_idx < static_cast<int>(pTl->points.size()))
			val = pTl->points[static_cast<size_t>(m_hover_timeline_idx)];
		float ty = fy(val);
		COLORREF col = item.color;
		nodes.push_back({ i, ty, ty, pTl->name, col });
	}

	std::sort(nodes.begin(), nodes.end(), [](const LabelNode& a, const LabelNode& b) {
		return a.targetY < b.targetY;
	});

	float minGap = static_cast<float>(g_data.DPI(15));
	if (!nodes.empty())
	{
		// 向下推开
		for (size_t i = 1; i < nodes.size(); ++i)
		{
			if (nodes[i].y < nodes[i - 1].y + minGap)
				nodes[i].y = nodes[i - 1].y + minGap;
		}
		// 触底回拉
		float maxBottom = static_cast<float>(plotRc.bottom);
		if (nodes.back().y > maxBottom)
		{
			nodes.back().y = maxBottom;
			for (int i = static_cast<int>(nodes.size()) - 2; i >= 0; --i)
			{
				if (nodes[static_cast<size_t>(i)].y > nodes[static_cast<size_t>(i + 1)].y - minGap)
					nodes[static_cast<size_t>(i)].y = nodes[static_cast<size_t>(i + 1)].y - minGap;
			}
		}
		// 触顶钳制
		float minTop = static_cast<float>(plotRc.top);
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			if (nodes[i].y < minTop + i * minGap)
				nodes[i].y = minTop + i * minGap;
		}
	}

	for (const auto& node : nodes)
	{
		bool isFocused = (node.tlIdx == focusedTlIdx);
		BYTE alpha = (focusedTlIdx >= 0) ? (isFocused ? 255 : 100) : 230;

		CRect lblRc(plotRc.right + g_data.DPI(5), static_cast<int>(node.y) - g_data.DPI(7),
			chartRc.right - g_data.DPI(2), static_cast<int>(node.y) + g_data.DPI(8));
		DrawStrSingle(g, node.text, isFocused ? f10b.get() : f10.get(), lblRc, node.color, alpha, Gdiplus::StringAlignmentNear);
	}
}

// ============ 页面2：ETF申购净流入 ============

void CMarketCenterPanel::DrawEtfInflowPage(Gdiplus::Graphics& g, const CRect& rc)
{
	if (m_etfs_snapshot.empty())
	{
		auto f12 = MkFont(12);
		DrawStatus(g, rc, CMarketCenterData::DS_ETFS, L"正在获取ETF数据…", f12.get());
		return;
	}

	auto f10 = MkFont(10);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12 = MkFont(12);
	auto f12b = MkFont(12, true);
	auto f15b = MkFont(15, true);

	double totalIn = 0, totalOut = 0;
	long long upCnt = 0, downCnt = 0;
	for (const auto& e : m_etfs_snapshot)
	{
		if (e.inflow > 0) totalIn += e.inflow;
		else totalOut += e.inflow;
		if (e.pct > 0) upCnt++;
		else if (e.pct < 0) downCnt++;
	}

	// 顶部不再重复页面标题（统计卡已表明流入/流出模式），统计卡直接置顶
	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(16), rc.right - g_data.DPI(16), rc.top + g_data.DPI(16) + g_data.DPI(54));
	FillCard(g, statsRc);
	m_inflow_stat_rects.clear();
	const int cellW = statsRc.Width() / 4;
	struct InCell { const wchar_t* label; std::wstring value; COLORREF color; bool clickable; bool active; };
	InCell inCells[4] = {
		{ L"ETF净流入", FormatYi(totalIn), MC_UP, true, !m_inflow_out },
		{ L"ETF净流出", FormatYi(totalOut), MC_DOWN, true, m_inflow_out },
		{ L"上涨家数", FormatInt(upCnt), MC_TEXT, false, false },
		{ L"下跌家数", FormatInt(downCnt), MC_TEXT, false, false },
	};
	for (int i = 0; i < 4; i++)
	{
		CRect cell(statsRc.left + i * cellW, statsRc.top, statsRc.left + (i + 1) * cellW, statsRc.bottom);
		if (inCells[i].active)
		{
			Gdiplus::SolidBrush activeBg(Gdi(inCells[i].color, 26));
			g.FillRectangle(&activeBg, Gdiplus::REAL(cell.left + 1), Gdiplus::REAL(cell.top + 1), Gdiplus::REAL(cellW - 1), Gdiplus::REAL(cell.Height() - 2));
			Gdiplus::SolidBrush barBrush(Gdi(inCells[i].color));
			g.FillRectangle(&barBrush, Gdiplus::REAL(cell.left + 1), Gdiplus::REAL(cell.bottom - g_data.DPI(2)), Gdiplus::REAL(cellW - 2), Gdiplus::REAL(g_data.DPI(2)));
		}
		if (i > 0)
		{
			Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&sepPen, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top + g_data.DPI(10)), Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.bottom - g_data.DPI(10)));
		}
		DrawStrMid(g, inCells[i].label, f11.get(), CRect(cell.left, cell.top + g_data.DPI(8), cell.right, cell.top + g_data.DPI(24)), MC_TEXT_SUB);
		DrawStrMid(g, inCells[i].value, f15b.get(), CRect(cell.left, cell.top + g_data.DPI(26), cell.right, cell.bottom - g_data.DPI(6)), inCells[i].color);
		m_inflow_stat_rects.push_back({ cell, i });
	}

	// 标题
	CRect headingRc(statsRc.left, statsRc.bottom + g_data.DPI(6), statsRc.right, statsRc.bottom + g_data.DPI(24));
	DrawStr(g, m_inflow_out ? L"ETF 当日主力净流出 Top10（亿）" : L"ETF 当日主力净流入 Top10（亿）", f12b.get(), headingRc, MC_TEXT);

	// 横向条形图
	CRect chartRc(headingRc.left, headingRc.bottom + g_data.DPI(4), headingRc.right, rc.bottom - g_data.DPI(16));
	m_inflow_bars.clear();

	// 取 Top10：净流入榜取降序前10，净流出榜取升序前10
	std::vector<int> order;
	for (int i = 0; i < static_cast<int>(m_theme_inflow.size()); i++)
		order.push_back(i);
	std::sort(order.begin(), order.end(), [this](int a, int b) {
		return m_inflow_out ? (m_theme_inflow[static_cast<size_t>(a)].inflow < m_theme_inflow[static_cast<size_t>(b)].inflow)
			: (m_theme_inflow[static_cast<size_t>(a)].inflow > m_theme_inflow[static_cast<size_t>(b)].inflow);
		});
	if (order.size() > 10)
		order.resize(10);
	if (order.empty())
	{
		DrawStrMid(g, L"暂无主题聚合数据", f12.get(), chartRc, MC_TEXT_DIM);
		return;
	}

	double maxAbs = 0;
	for (int idx : order)
		maxAbs = max(maxAbs, fabs(m_theme_inflow[static_cast<size_t>(idx)].inflow) / 1e8);
	if (maxAbs <= 0) maxAbs = 1;

	// 名称列宽按可见最长名称自适应，文字右对齐贴条形起点（字块起点仍与标题左缘一致）
	int nameW = 0;
	for (int idx : order)
		nameW = max(nameW, MeasureStr(g, f11.get(), m_theme_inflow[static_cast<size_t>(idx)].theme + L"ETF").cx);
	nameW = min(nameW + g_data.DPI(16), g_data.DPI(120));
	const int valW = g_data.DPI(64);
	const int barH = g_data.DPI(18);
	const int slot = chartRc.Height() / static_cast<int>(order.size());
	const int plotLeft = chartRc.left + nameW;
	const int plotRight = chartRc.right - valW;
	// 单向条形图：流入/流出同一形态，全部从名称列右缘向右铺开，长度按 |值|/maxAbs 归一，仅颜色区分
	Gdiplus::Pen zeroPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&zeroPen, Gdiplus::REAL(plotLeft), Gdiplus::REAL(chartRc.top), Gdiplus::REAL(plotLeft), Gdiplus::REAL(chartRc.bottom));

	for (size_t k = 0; k < order.size(); k++)
	{
		int idx = order[k];
		const auto& th = m_theme_inflow[static_cast<size_t>(idx)];
		int yTop = chartRc.top + static_cast<int>(k) * slot + (slot - barH) / 2;
		CRect nameRc(chartRc.left, yTop - g_data.DPI(2), plotLeft - g_data.DPI(8), yTop + barH + g_data.DPI(2));
		DrawStr(g, th.theme + L"ETF", f11.get(), nameRc, MC_TEXT_SUB, 255, Gdiplus::StringAlignmentFar);
		double vYi = th.inflow / 1e8;
		int x1 = plotLeft + static_cast<int>(fabs(vYi) / maxAbs * (plotRight - plotLeft));
		CRect barRc(plotLeft, yTop, max(plotLeft + g_data.DPI(2), x1), yTop + barH);
		FillRounded(g, barRc, th.inflow >= 0 ? MC_UP : MC_DOWN, g_data.DPI(2));
		bool hovered = (static_cast<int>(k) == m_hover_inflow_bar);
		if (hovered)
		{
			Gdiplus::Pen hoverPen(Gdi(MC_TEXT, 120), 1.2f);
			g.DrawRectangle(&hoverPen, Gdiplus::REAL(barRc.left), Gdiplus::REAL(barRc.top), Gdiplus::REAL(barRc.Width()), Gdiplus::REAL(barRc.Height()));
		}

		// 数值标签：条形右端外侧
		wchar_t vbuf[32];
		swprintf_s(vbuf, L"%s%.2f", vYi >= 0 ? L"+" : L"", vYi);
		CRect valRc(barRc.right + g_data.DPI(6), yTop,
			barRc.right + g_data.DPI(6) + valW, yTop + barH);
		DrawStr(g, vbuf, f10.get(), valRc, MC_TEXT_SUB, 255, Gdiplus::StringAlignmentNear);
		// 整行命中区（整个槽位高度，避免行间死区），点击打开主题浮层
		int slotTop = chartRc.top + static_cast<int>(k) * slot;
		int slotBottom = (k == order.size() - 1) ? chartRc.bottom : (slotTop + slot);
		m_inflow_bars.push_back({ CRect(chartRc.left, slotTop, chartRc.right, slotBottom), barRc, idx });
	}

	// 主题ETF浮层
	if (m_theme_panel_open)
		DrawThemePanel(g, chartRc);
}

void CMarketCenterPanel::DrawThemePanel(Gdiplus::Graphics& g, const CRect& chartRc)
{
	auto f10 = MkFont(10);
	auto f10b = MkFont(10, true);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12b = MkFont(12, true);

	const int panelW = g_data.DPI(310);
	m_theme_panel_rect = CRect(chartRc.right - panelW, chartRc.top, chartRc.right, chartRc.bottom);
	FillCard(g, m_theme_panel_rect, MC_CARD);

	// 边框与简易顶部微光
	Gdiplus::Pen borderPen(Gdi(MC_BORDER), 1.0f);
	g.DrawRectangle(&borderPen, m_theme_panel_rect.left, m_theme_panel_rect.top,
		m_theme_panel_rect.Width(), m_theme_panel_rect.Height());
	Gdiplus::Pen topPen(Gdi(MC_TEXT, 18), 1.0f);
	g.DrawLine(&topPen, Gdiplus::REAL(m_theme_panel_rect.left), Gdiplus::REAL(m_theme_panel_rect.top),
		Gdiplus::REAL(m_theme_panel_rect.right), Gdiplus::REAL(m_theme_panel_rect.top));

	const int P = g_data.DPI(10);
	CRect headRc(m_theme_panel_rect.left + P, m_theme_panel_rect.top + P, m_theme_panel_rect.right - P, m_theme_panel_rect.top + P + g_data.DPI(20));
	m_theme_close_rect = CRect(headRc.right - g_data.DPI(18), headRc.top, headRc.right, headRc.bottom);
	DrawStr(g, m_theme_panel_title, f12b.get(), CRect(headRc.left, headRc.top, m_theme_close_rect.left - g_data.DPI(4), headRc.bottom), MC_TEXT);
	DrawStrMid(g, L"\x2715", f11.get(), m_theme_close_rect, m_hover_theme_close ? MC_TEXT : MC_TEXT_DIM);

	// 表头
	CRect headRow(headRc.left, headRc.bottom + g_data.DPI(4), headRc.right, headRc.bottom + g_data.DPI(4) + g_data.DPI(18));
	const int colW[4] = { 42, 22, 18, 18 };
	int colWidths[4];
	int totalW = headRow.Width();
	for (int i = 0; i < 3; i++)
		colWidths[i] = totalW * colW[i] / 100;
	colWidths[3] = totalW - colWidths[0] - colWidths[1] - colWidths[2];

	const wchar_t* headers[4] = { L"名称", L"代码", L"现价", L"涨跌幅" };
	int x = headRow.left;
	for (int i = 0; i < 4; i++)
	{
		CRect c(x, headRow.top, x + colWidths[i], headRow.bottom);
		DrawStr(g, headers[i], f10.get(), c, MC_TEXT_DIM, 255, i == 0 ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
		x += colWidths[i];
	}
	Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&sepPen, Gdiplus::REAL(headRow.left), Gdiplus::REAL(headRow.bottom), Gdiplus::REAL(headRow.right), Gdiplus::REAL(headRow.bottom));

	// 数据行（可滚动）
	const int rowH = g_data.DPI(23);
	m_theme_row_h = rowH;
	CRect listRc(headRow.left, headRow.bottom + g_data.DPI(2), headRow.right, m_theme_panel_rect.bottom - P);
	m_theme_list_rect = listRc;
	int maxVisible = max(1, listRc.Height() / rowH);
	m_theme_panel_scroll_max = max(0, static_cast<int>(m_theme_row_etfs.size()) - maxVisible);
	m_theme_panel_scroll = min(m_theme_panel_scroll, m_theme_panel_scroll_max);

	g.SetClip(Gdiplus::Rect(listRc.left, listRc.top, listRc.Width(), listRc.Height()));
	for (int drawIdx = 0; drawIdx < maxVisible; drawIdx++)
	{
		int itemIdx = m_theme_panel_scroll + drawIdx;
		if (itemIdx >= static_cast<int>(m_theme_row_etfs.size()))
			break;

		int etfIdx = m_theme_row_etfs[static_cast<size_t>(itemIdx)];
		const auto& e = m_etfs_snapshot[static_cast<size_t>(etfIdx)];
		CRect rRow(listRc.left, listRc.top + drawIdx * rowH, listRc.right, listRc.top + (drawIdx + 1) * rowH);

		// 行 hover 高亮背景
		if (m_hover_theme_row == itemIdx)
		{
			FillRounded(g, rRow, MC_TEXT, 0, 14);
		}

		wchar_t priceBuf[24], pctBuf[24];
		swprintf_s(priceBuf, L"%.3f", e.price);
		swprintf_s(pctBuf, L"%s%.2f%%", e.pct >= 0 ? L"+" : L"", e.pct);

		// 股票名称：单行绘制，宽度不够时溢出隐藏截断，坚决不换行
		CRect nameRc(rRow.left, rRow.top, rRow.left + colWidths[0] - g_data.DPI(4), rRow.bottom);
		DrawStrSingle(g, e.name, f11.get(), nameRc, MC_TEXT, 255, Gdiplus::StringAlignmentNear);
		DrawStrMid(g, e.code, f10.get(), CRect(rRow.left + colWidths[0], rRow.top, rRow.left + colWidths[0] + colWidths[1], rRow.bottom), MC_TEXT_SUB);
		DrawStrMid(g, priceBuf, f10.get(), CRect(rRow.left + colWidths[0] + colWidths[1], rRow.top, rRow.left + colWidths[0] + colWidths[1] + colWidths[2], rRow.bottom), MC_TEXT_SUB);
		DrawStrMid(g, pctBuf, f10b.get(), CRect(rRow.left + colWidths[0] + colWidths[1] + colWidths[2], rRow.top, rRow.right, rRow.bottom), UpDownColor(e.pct));
	}
	g.ResetClip();
}

// ============ 页面3：资金流向（多维度资金博弈分时，对应图一） ============

void CMarketCenterPanel::DrawMoneyFlowPage(Gdiplus::Graphics& g, const CRect& rc)
{
	DrawPageTitle(g, rc, L"资金流向", L"全市场大单/超大单博弈分时（沪深合计）");

	auto f10 = MkFont(10);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12b = MkFont(12, true);
	auto f15b = MkFont(15, true);

	CMarketCenterData& mc = CMarketCenterData::Instance();
	const int AXIS_N = 241;

	// 缓存机制：若后台数据时间戳变动或未初始化，才执行加锁与数据聚合
	if (mc.m_fflow_time != m_moneyflow_cache.dataTime || !m_moneyflow_cache.hasData)
	{
		std::vector<MC::FflowMinute> sh, sz;
		MC::MoneyFlowLeader leaderInst, leaderMain;
		time_t curTime = 0;
		{
			std::lock_guard<std::mutex> lock(mc.m_mutex);
			sh = mc.m_fflow_sh;
			sz = mc.m_fflow_sz;
			leaderInst = mc.m_leader_inst;
			leaderMain = mc.m_leader_main;
			curTime = mc.m_fflow_time;
		}

		m_moneyflow_cache.inst.assign(static_cast<size_t>(AXIS_N), NAN);
		m_moneyflow_cache.main.assign(static_cast<size_t>(AXIS_N), NAN);
		m_moneyflow_cache.big.assign(static_cast<size_t>(AXIS_N), NAN);
		m_moneyflow_cache.smallOrder.assign(static_cast<size_t>(AXIS_N), NAN);

		auto putAdd = [&](const MC::FflowMinute& f) {
			int i = CMarketCenterData::TimeIndex(f.time);
			if (i >= 0 && i < AXIS_N)
			{
				size_t si = static_cast<size_t>(i);
				m_moneyflow_cache.inst[si] = (isnan(m_moneyflow_cache.inst[si]) ? 0.0 : m_moneyflow_cache.inst[si]) + f.superBig / 1e8;
				m_moneyflow_cache.main[si] = (isnan(m_moneyflow_cache.main[si]) ? 0.0 : m_moneyflow_cache.main[si]) + f.main / 1e8;
				m_moneyflow_cache.big[si] = (isnan(m_moneyflow_cache.big[si]) ? 0.0 : m_moneyflow_cache.big[si]) + f.big / 1e8;
				m_moneyflow_cache.smallOrder[si] = (isnan(m_moneyflow_cache.smallOrder[si]) ? 0.0 : m_moneyflow_cache.smallOrder[si]) + f.smallOrder / 1e8;
			}
		};
		for (auto& f : sh) putAdd(f);
		for (auto& f : sz) putAdd(f);

		auto lastOf = [](const std::vector<double>& arr) -> double {
			for (int i = static_cast<int>(arr.size()) - 1; i >= 0; i--)
				if (!isnan(arr[static_cast<size_t>(i)]))
					return arr[static_cast<size_t>(i)];
			return NAN;
		};
		m_moneyflow_cache.instNow = lastOf(m_moneyflow_cache.inst);
		m_moneyflow_cache.mainNow = lastOf(m_moneyflow_cache.main);
		m_moneyflow_cache.bigNow = lastOf(m_moneyflow_cache.big);
		m_moneyflow_cache.smallOrderNow = lastOf(m_moneyflow_cache.smallOrder);
		m_moneyflow_cache.leaderInst = leaderInst;
		m_moneyflow_cache.leaderMain = leaderMain;
		m_moneyflow_cache.dataTime = curTime;
		m_moneyflow_cache.hasData = (!isnan(m_moneyflow_cache.instNow) || !isnan(m_moneyflow_cache.mainNow) ||
			!isnan(m_moneyflow_cache.bigNow) || !isnan(m_moneyflow_cache.smallOrderNow));
	}

	const auto& instArr = m_moneyflow_cache.inst;
	const auto& mainArr = m_moneyflow_cache.main;
	const auto& bigArr = m_moneyflow_cache.big;
	const auto& smallArr = m_moneyflow_cache.smallOrder;
	double instNow = m_moneyflow_cache.instNow;
	double mainNow = m_moneyflow_cache.mainNow;
	double bigNow = m_moneyflow_cache.bigNow;
	double smallNow = m_moneyflow_cache.smallOrderNow;
	bool hasAny = m_moneyflow_cache.hasData;

	if (!hasAny)
	{
		auto f12 = MkFont(12);
		DrawStatus(g, rc, CMarketCenterData::DS_MAINFLOW, L"正在获取全市场资金流向分时…", f12.get());
	}

	// 顶部统计卡兼图例（4列：机构、主力、大户、散户，可点击开关曲线）
	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(16), rc.right - g_data.DPI(16), rc.top + g_data.DPI(16) + g_data.DPI(54));
	FillCard(g, statsRc);
	m_moneyflow_stat_rects.clear();

	struct FlowCell { const wchar_t* label; COLORREF color; double value; };
	FlowCell flowCells[4] = {
		{ L"机构", FLOW_INST, instNow },
		{ L"主力", FLOW_MAIN, mainNow },
		{ L"大户", FLOW_BIG, bigNow },
		{ L"散户", FLOW_SMALL, smallNow },
	};

	const int cellW = statsRc.Width() / 4;
	for (int i = 0; i < 4; i++)
	{
		CRect cell(statsRc.left + i * cellW, statsRc.top, statsRc.left + (i + 1) * cellW, statsRc.bottom);
		bool off = (m_moneyflow_series_mask & (1 << i)) == 0;
		BYTE alpha = off ? 96 : 255;
		if (m_hover_moneyflow_card == i)
		{
			Gdiplus::SolidBrush hoverBg(Gdi(MC_TEXT, 10));
			g.FillRectangle(&hoverBg, Gdiplus::REAL(cell.left + 1), Gdiplus::REAL(cell.top + 1), Gdiplus::REAL(cellW - 2), Gdiplus::REAL(cell.Height() - 2));
		}
		if (i > 0)
		{
			Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&sepPen, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top + g_data.DPI(10)), Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.bottom - g_data.DPI(10)));
		}

		// 色块 + 标签
		CRect lblRc = cell;
		lblRc.top += g_data.DPI(8);
		lblRc.bottom = lblRc.top + g_data.DPI(16);
		int swatchW = g_data.DPI(14), swatchH = g_data.DPI(4);
		std::wstring label = flowCells[i].label;
		auto fLbl = MkFont(11);
		CSize lblSize = MeasureStr(g, fLbl.get(), label);
		int totalW = swatchW + g_data.DPI(5) + lblSize.cx;
		int startX = cell.CenterPoint().x - totalW / 2;
		Gdiplus::SolidBrush swBrush(Gdi(flowCells[i].color, off ? static_cast<BYTE>(96) : static_cast<BYTE>(255)));
		g.FillRectangle(&swBrush, Gdiplus::REAL(startX), Gdiplus::REAL(lblRc.top + (lblRc.Height() - swatchH) / 2), Gdiplus::REAL(swatchW), Gdiplus::REAL(swatchH));
		DrawStr(g, label, fLbl.get(), CRect(startX + swatchW + g_data.DPI(5), lblRc.top, cell.right, lblRc.bottom), MC_TEXT_SUB, alpha);

		// 数值（带单位“亿”，正红负绿）
		std::wstring valStr = isnan(flowCells[i].value) ? L"--" : FormatYi(flowCells[i].value * 1e8, 2);
		COLORREF valColor = UpDownColor(flowCells[i].value);
		DrawStrMid(g, valStr, f15b.get(), CRect(cell.left, cell.top + g_data.DPI(26), cell.right, cell.bottom - g_data.DPI(6)), valColor, alpha);
		m_moneyflow_stat_rects.push_back({ cell, i });
	}

	if (!hasAny)
		return;

	// 底部领头股票条：预留空间高 24px
	const int leaderH = g_data.DPI(24);
	CRect leaderRc(statsRc.left, rc.bottom - leaderH - g_data.DPI(10), statsRc.right, rc.bottom - g_data.DPI(10));

	// 中间图表区域
	CRect chartRc(statsRc.left, statsRc.bottom + g_data.DPI(8), statsRc.right, leaderRc.top - g_data.DPI(4));
	const int padL = g_data.DPI(2), padR = g_data.DPI(2), padT = g_data.DPI(18), padB = g_data.DPI(22);
	CRect plotRc(chartRc.left + padL, chartRc.top + padT, chartRc.right - padR, chartRc.bottom - padB);
	m_moneyflow_plot_rect = plotRc; // 供 HandleMouseMove 做精准 Hover 判断
	if (plotRc.Width() < g_data.DPI(100) || plotRc.Height() < g_data.DPI(60))
		return;

	// 单 Y 轴范围：包含可见曲线并包含 0 轴
	double flowLo = 0, flowHi = 0;
	bool flowAny = false;
	auto mergeFlow = [&](const std::vector<double>& arr) {
		for (double v : arr)
		{
			if (isnan(v)) continue;
			flowLo = min(flowLo, v);
			flowHi = max(flowHi, v);
			flowAny = true;
		}
	};
	if (m_moneyflow_series_mask & 1) mergeFlow(instArr);
	if (m_moneyflow_series_mask & 2) mergeFlow(mainArr);
	if (m_moneyflow_series_mask & 4) mergeFlow(bigArr);
	if (m_moneyflow_series_mask & 8) mergeFlow(smallArr);
	if (!flowAny) { flowLo = -10; flowHi = 10; }

	// 确保 0 轴在坐标系内
	flowLo = min(flowLo, 0.0);
	flowHi = max(flowHi, 0.0);
	double flowPad = max(1.0, (flowHi - flowLo) * 0.08);
	flowLo -= flowPad; flowHi += flowPad;

	auto fx = [&](int i) -> float { return plotRc.left + static_cast<float>(i) * plotRc.Width() / (AXIS_N - 1); };
	auto fyFlow = [&](double v) -> float { return static_cast<float>(plotRc.bottom - (v - flowLo) / (flowHi - flowLo) * plotRc.Height()); };

	// 网格 + 左轴刻度 + 强化0轴
	{
		double step = NiceStep(flowHi - flowLo);
		auto f10 = MkFont(10);
		for (double tv = ceil(flowLo / step) * step; tv <= flowHi; tv += step)
		{
			float y = fyFlow(tv);
			bool isZero = fabs(tv) < 1e-6;
			Gdiplus::Pen gridPen(Gdi(isZero ? RGB(70, 78, 96) : MC_GRID), isZero ? 1.5f : 1.0f);
			g.DrawLine(&gridPen, Gdiplus::REAL(plotRc.left), y, Gdiplus::REAL(plotRc.right), y);

			// 左侧刻度数值
			std::wstring lbl = FormatAxisNum(tv, step) + L"亿";
			CRect lblRc(plotRc.left + g_data.DPI(4), static_cast<int>(y) - g_data.DPI(8), plotRc.left + g_data.DPI(60), static_cast<int>(y) + g_data.DPI(8));
			{
				Gdiplus::SolidBrush bgBrush(Gdi(MC_BG, 190));
				g.FillRectangle(&bgBrush, Gdiplus::REAL(lblRc.left - g_data.DPI(2)), Gdiplus::REAL(lblRc.top), Gdiplus::REAL(lblRc.Width() + g_data.DPI(4)), Gdiplus::REAL(lblRc.Height()));
			}
			DrawStr(g, lbl, f10.get(), lblRc, isZero ? MC_TEXT : MC_TEXT_SUB);
		}
	}

	// X 轴时间标签 09:30 10:30 13:00 14:00 15:00
	{
		auto f10 = MkFont(10);
		const auto& axis = CMarketCenterData::TimeAxis();
		const int marks[5] = { 0, 60, 120, 180, 240 };
		for (int k = 0; k < 5; k++)
		{
			int mi = marks[k];
			if (mi >= static_cast<int>(axis.size()))
				continue;
			CRect lblRc(static_cast<int>(fx(mi)) - g_data.DPI(24), plotRc.bottom + g_data.DPI(4), static_cast<int>(fx(mi)) + g_data.DPI(24), plotRc.bottom + g_data.DPI(20));
			DrawStrMid(g, axis[static_cast<size_t>(mi)], f10.get(), lblRc, MC_TEXT_SUB);
		}
	}

	// 4条折线
	auto drawSeries = [&](const std::vector<double>& arr, COLORREF color, float width) {
		Gdiplus::Pen pen(Gdi(color), width);
		std::vector<Gdiplus::PointF> pts;
		for (int i = 0; i < AXIS_N; i++)
		{
			double v = arr[static_cast<size_t>(i)];
			if (isnan(v))
				continue;
			pts.push_back(Gdiplus::PointF(fx(i), fyFlow(v)));
		}
		if (pts.size() >= 2)
			g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
	};

	if (m_moneyflow_series_mask & 8) drawSeries(smallArr, FLOW_SMALL, 1.8f); // 散户
	if (m_moneyflow_series_mask & 4) drawSeries(bigArr, FLOW_BIG, 1.8f);     // 大户
	if (m_moneyflow_series_mask & 1) drawSeries(instArr, FLOW_INST, 1.8f);   // 机构
	if (m_moneyflow_series_mask & 2) drawSeries(mainArr, FLOW_MAIN, 2.0f);   // 主力（稍粗突出）

	// 鼠标悬停十字光标（Crosshair）：根据防抖后的 m_hover_moneyflow_idx 极速绘制
	if (m_hover_moneyflow_idx >= 0 && m_hover_moneyflow_idx < AXIS_N)
	{
		int hoverIdx = m_hover_moneyflow_idx;
		float curX = fx(hoverIdx);
		Gdiplus::Pen crossPen(Gdi(RGB(180, 190, 210), 120), 1.0f);
		crossPen.SetDashStyle(Gdiplus::DashStyleDash);
		g.DrawLine(&crossPen, curX, static_cast<float>(plotRc.top), curX, static_cast<float>(plotRc.bottom));

		// 收集该点的4个值在顶部浮层/提示显示
		const auto& axis = CMarketCenterData::TimeAxis();
		if (hoverIdx < static_cast<int>(axis.size()))
		{
			std::wstring tStr = axis[static_cast<size_t>(hoverIdx)];
			double iVal = (hoverIdx < static_cast<int>(instArr.size())) ? instArr[static_cast<size_t>(hoverIdx)] : NAN;
			double mVal = (hoverIdx < static_cast<int>(mainArr.size())) ? mainArr[static_cast<size_t>(hoverIdx)] : NAN;
			double bVal = (hoverIdx < static_cast<int>(bigArr.size())) ? bigArr[static_cast<size_t>(hoverIdx)] : NAN;
			double sVal = (hoverIdx < static_cast<int>(smallArr.size())) ? smallArr[static_cast<size_t>(hoverIdx)] : NAN;
			std::wstring tip = tStr + L" | 机构:" + (isnan(iVal) ? L"--" : FormatYi(iVal * 1e8, 2))
				+ L" 主力:" + (isnan(mVal) ? L"--" : FormatYi(mVal * 1e8, 2))
				+ L" 大户:" + (isnan(bVal) ? L"--" : FormatYi(bVal * 1e8, 2))
				+ L" 散户:" + (isnan(sVal) ? L"--" : FormatYi(sVal * 1e8, 2));

			auto fTip = MkFont(11);
			CSize szTip = MeasureStr(g, fTip.get(), tip);
			int tipW = szTip.cx + g_data.DPI(16);
			int tipH = g_data.DPI(20);
			int tipX = min(max(static_cast<int>(curX) - tipW / 2, plotRc.left), plotRc.right - tipW);
			int tipY = plotRc.top + g_data.DPI(2);
			CRect tipRc(tipX, tipY, tipX + tipW, tipY + tipH);
			Gdiplus::SolidBrush tipBg(Gdi(RGB(15, 17, 23), 220));
			g.FillRectangle(&tipBg, Gdiplus::REAL(tipRc.left), Gdiplus::REAL(tipRc.top), Gdiplus::REAL(tipRc.Width()), Gdiplus::REAL(tipRc.Height()));
			Gdiplus::Pen tipBorder(Gdi(MC_BORDER), 1.0f);
			g.DrawRectangle(&tipBorder, Gdiplus::REAL(tipRc.left), Gdiplus::REAL(tipRc.top), Gdiplus::REAL(tipRc.Width()), Gdiplus::REAL(tipRc.Height()));
			DrawStrMid(g, tip, fTip.get(), tipRc, MC_TEXT);
		}
	}

	// 底部领头股票条绘制（机构领头 + 主力领头）
	{
		int midX = leaderRc.left + leaderRc.Width() / 2;
		CRect instLeadRc(leaderRc.left, leaderRc.top, midX - g_data.DPI(10), leaderRc.bottom);
		CRect mainLeadRc(midX + g_data.DPI(10), leaderRc.top, leaderRc.right, leaderRc.bottom);

		// 机构领头：中际旭创 57.10亿
		std::wstring instLeadStr = L"机构领头：";
		if (m_moneyflow_cache.leaderInst.name.empty()) instLeadStr += L"--";
		else instLeadStr += m_moneyflow_cache.leaderInst.name + L"  " + FormatYi(m_moneyflow_cache.leaderInst.flow, 2);

		// 主力领头：杭电股份 3.30亿
		std::wstring mainLeadStr = L"主力领头：";
		if (m_moneyflow_cache.leaderMain.name.empty()) mainLeadStr += L"--";
		else mainLeadStr += m_moneyflow_cache.leaderMain.name + L"  " + FormatYi(m_moneyflow_cache.leaderMain.flow, 2);

		auto f11bLead = MkFont(11, true);
		DrawStr(g, instLeadStr, f11bLead.get(), instLeadRc, FLOW_INST);
		DrawStr(g, mainLeadStr, f11bLead.get(), mainLeadRc, FLOW_MAIN, 255, Gdiplus::StringAlignmentFar);
	}
}

// ============ 页面4：主力资金 ============

void CMarketCenterPanel::DrawMainFlowPage(Gdiplus::Graphics& g, const CRect& rc)
{
	DrawPageTitle(g, rc, L"实时主力资金", L"沪深两市大盘资金流");

	auto f10 = MkFont(10);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12b = MkFont(12, true);
	auto f15b = MkFont(15, true);

	// 拷贝数据（锁内浅拷贝）
	std::vector<MC::FflowMinute> sh, sz;
	std::vector<MC::IndexTrendPoint> idx;
	std::vector<MC::EtfFlowSample> etfCurve;
	double etfSumNow = 0;
	bool etfHasSnap = false;
	{
		CMarketCenterData& mc = CMarketCenterData::Instance();
		std::lock_guard<std::mutex> lock(mc.m_mutex);
		sh = mc.m_fflow_sh;
		sz = mc.m_fflow_sz;
		idx = mc.m_index_trend;
		etfCurve = mc.m_etf_flow_curve;
		for (auto& e : mc.m_etfs)
		{
			etfSumNow += e.inflow;
			etfHasSnap = true;
		}
	}

	const int AXIS_N = 241;
	std::vector<double> shArr(static_cast<size_t>(AXIS_N), NAN), szArr(static_cast<size_t>(AXIS_N), NAN),
		etfArr(static_cast<size_t>(AXIS_N), NAN), idxArr(static_cast<size_t>(AXIS_N), NAN);
	auto put = [](std::vector<double>& arr, const std::wstring& t, double v) {
		int i = CMarketCenterData::TimeIndex(t);
		if (i >= 0 && i < static_cast<int>(arr.size()))
			arr[static_cast<size_t>(i)] = v;
	};
	for (auto& f : sh) put(shArr, f.time, f.main / 1e8);
	for (auto& f : sz) put(szArr, f.time, f.main / 1e8);
	for (auto& f : etfCurve) put(etfArr, f.time, f.inflow / 1e8);
	for (auto& p : idx) put(idxArr, p.time, p.price);

	auto lastOf = [](const std::vector<double>& arr) -> double {
		for (int i = static_cast<int>(arr.size()) - 1; i >= 0; i--)
			if (!isnan(arr[static_cast<size_t>(i)]))
				return arr[static_cast<size_t>(i)];
		return NAN;
		};
	double shNow = lastOf(shArr), szNow = lastOf(szArr), etfNow = lastOf(etfArr), idxNow = lastOf(idxArr);
	// ETF 曲线开窗才自积累、开窗前/盘外无点，图例回退到当前 ETF 快照合计，避免显示"--"
	if (isnan(etfNow) && etfHasSnap)
		etfNow = etfSumNow / 1e8;
	bool hasAny = (!isnan(shNow) || !isnan(szNow) || !isnan(idxNow));
	if (!hasAny)
	{
		auto f12 = MkFont(12);
		DrawStatus(g, rc, CMarketCenterData::DS_MAINFLOW, L"正在获取沪深主力资金分时…", f12.get());
		// 统计条仍然绘制（无数据状态）
		shArr.clear();
	}

	// 统计卡兼图例（可点击开关曲线）
	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(16), rc.right - g_data.DPI(16), rc.top + g_data.DPI(16) + g_data.DPI(54));
	FillCard(g, statsRc);
	m_mainflow_stat_rects.clear();
	struct MfCell { const wchar_t* label; COLORREF color; double value; bool isIndex; };
	MfCell mfCells[4] = {
		{ L"主力净流入(沪)", MF_SH, shNow, false },
		{ L"主力净流入(深)", MF_SZ, szNow, false },
		{ L"ETF净流入", MF_ETF, etfNow, false },
		{ L"上证指数", MF_IDX, idxNow, true },
	};
	const int cellW = statsRc.Width() / 4;
	for (int i = 0; i < 4; i++)
	{
		CRect cell(statsRc.left + i * cellW, statsRc.top, statsRc.left + (i + 1) * cellW, statsRc.bottom);
		bool off = (m_mainflow_series_mask & (1 << i)) == 0;
		BYTE alpha = off ? 96 : 255;
		if (m_hover_mainflow_card == i)
		{
			Gdiplus::SolidBrush hoverBg(Gdi(MC_TEXT, 10));
			g.FillRectangle(&hoverBg, Gdiplus::REAL(cell.left + 1), Gdiplus::REAL(cell.top + 1), Gdiplus::REAL(cellW - 2), Gdiplus::REAL(cell.Height() - 2));
		}
		if (i > 0)
		{
			Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&sepPen, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top + g_data.DPI(10)), Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.bottom - g_data.DPI(10)));
		}
		// 色块 + 标签
		CRect lblRc = cell;
		lblRc.top += g_data.DPI(8);
		lblRc.bottom = lblRc.top + g_data.DPI(16);
		int swatchW = g_data.DPI(14), swatchH = g_data.DPI(4);
		std::wstring label = mfCells[i].label;
		auto fLbl = MkFont(11);
		CSize lblSize = MeasureStr(g, fLbl.get(), label);
		int totalW = swatchW + g_data.DPI(5) + lblSize.cx;
		int startX = cell.CenterPoint().x - totalW / 2;
		Gdiplus::SolidBrush swBrush(Gdi(mfCells[i].color, off ? static_cast<BYTE>(96) : static_cast<BYTE>(255)));
		g.FillRectangle(&swBrush, Gdiplus::REAL(startX), Gdiplus::REAL(lblRc.top + (lblRc.Height() - swatchH) / 2), Gdiplus::REAL(swatchW), Gdiplus::REAL(swatchH));
		DrawStr(g, label, fLbl.get(), CRect(startX + swatchW + g_data.DPI(5), lblRc.top, cell.right, lblRc.bottom), MC_TEXT_SUB, alpha);
		// 数值
		std::wstring valStr = isnan(mfCells[i].value) ? L"--" : (mfCells[i].isIndex ? FormatAxisNum(mfCells[i].value, 1) : FormatYi(mfCells[i].value * 1e8, 2));
		COLORREF valColor = mfCells[i].isIndex ? MC_TEXT : UpDownColor(mfCells[i].value);
		DrawStrMid(g, valStr, f15b.get(), CRect(cell.left, cell.top + g_data.DPI(26), cell.right, cell.bottom - g_data.DPI(6)), valColor, alpha);
		m_mainflow_stat_rects.push_back({ cell, i });
	}

	if (!hasAny)
		return;

	// 双轴多曲线图
	CRect chartRc(statsRc.left, statsRc.bottom + g_data.DPI(8), statsRc.right, rc.bottom - g_data.DPI(16));
	const int padL = g_data.DPI(2), padR = g_data.DPI(2), padT = g_data.DPI(20), padB = g_data.DPI(24);
	CRect plotRc(chartRc.left + padL, chartRc.top + padT, chartRc.right - padR, chartRc.bottom - padB);
	if (plotRc.Width() < g_data.DPI(100) || plotRc.Height() < g_data.DPI(60))
		return;

	// 范围：左轴=净流入亿（可见流系列），右轴=上证指数
	double flowLo = 0, flowHi = 0;
	bool flowAny = false;
	auto mergeFlow = [&](const std::vector<double>& arr) {
		for (double v : arr)
		{
			if (isnan(v)) continue;
			flowLo = min(flowLo, v);
			flowHi = max(flowHi, v);
			flowAny = true;
		}
		};
	if (m_mainflow_series_mask & 1) mergeFlow(shArr);
	if (m_mainflow_series_mask & 2) mergeFlow(szArr);
	if (m_mainflow_series_mask & 4) mergeFlow(etfArr);
	if (!flowAny) { flowLo = -10; flowHi = 10; }
	double flowPad = max(1.0, (flowHi - flowLo) * 0.08);
	flowLo -= flowPad; flowHi += flowPad;

	double idxLo = 1e18, idxHi = -1e18;
	bool idxAny = false;
	for (double v : idxArr)
	{
		if (isnan(v)) continue;
		idxLo = min(idxLo, v);
		idxHi = max(idxHi, v);
		idxAny = true;
	}
	if (idxAny)
	{
		double idxPad = max(1.0, (idxHi - idxLo) * 0.5);
		idxLo -= idxPad;
		idxHi += idxPad;
	}

	auto fx = [&](int i) -> float { return plotRc.left + static_cast<float>(i) * plotRc.Width() / (AXIS_N - 1); };
	auto fyFlow = [&](double v) -> float { return static_cast<float>(plotRc.bottom - (v - flowLo) / (flowHi - flowLo) * plotRc.Height()); };
	auto fyIdx = [&](double v) -> float { return static_cast<float>(plotRc.bottom - (v - idxLo) / (idxHi - idxLo) * plotRc.Height()); };

	// 网格 + 左轴刻度（画在图内）
	{
		double step = NiceStep(flowHi - flowLo);
		auto f10 = MkFont(10);
		for (double tv = ceil(flowLo / step) * step; tv <= flowHi; tv += step)
		{
			float y = fyFlow(tv);
			Gdiplus::Pen gridPen(Gdi(MC_GRID), 1.0f);
			g.DrawLine(&gridPen, Gdiplus::REAL(plotRc.left), y, Gdiplus::REAL(plotRc.right), y);
			// 图内左侧标签：底色块 + 文本
			std::wstring lbl = FormatAxisNum(tv, step) + L"亿";
			CRect lblRc(plotRc.left + g_data.DPI(4), static_cast<int>(y) - g_data.DPI(8), plotRc.left + g_data.DPI(60), static_cast<int>(y) + g_data.DPI(8));
			{
				Gdiplus::SolidBrush bgBrush(Gdi(MC_BG, 190));
				g.FillRectangle(&bgBrush, Gdiplus::REAL(lblRc.left - g_data.DPI(2)), Gdiplus::REAL(lblRc.top), Gdiplus::REAL(lblRc.Width() + g_data.DPI(4)), Gdiplus::REAL(lblRc.Height()));
			}
			DrawStr(g, lbl, f10.get(), lblRc, MC_TEXT_SUB);
		}
		// 右轴（指数）图内标签
		if (idxAny)
		{
			double istep = NiceStep(idxHi - idxLo);
			for (double tv = ceil(idxLo / istep) * istep; tv <= idxHi; tv += istep)
			{
				float y = fyIdx(tv);
				std::wstring lbl = FormatAxisNum(tv, istep);
				CRect lblRc(plotRc.right - g_data.DPI(56), static_cast<int>(y) - g_data.DPI(8), plotRc.right - g_data.DPI(4), static_cast<int>(y) + g_data.DPI(8));
				{
				Gdiplus::SolidBrush bgBrush(Gdi(MC_BG, 190));
				g.FillRectangle(&bgBrush, Gdiplus::REAL(lblRc.left - g_data.DPI(2)), Gdiplus::REAL(lblRc.top), Gdiplus::REAL(lblRc.Width() + g_data.DPI(4)), Gdiplus::REAL(lblRc.Height()));
			}
				DrawStr(g, lbl, f10.get(), lblRc, MC_TEXT_SUB, 255, Gdiplus::StringAlignmentFar);
			}
		}
	}

	// X 轴时间标签 09:30 10:30 11:30/13:00 14:00 15:00
	{
		auto f10 = MkFont(10);
		const auto& axis = CMarketCenterData::TimeAxis();
		const int marks[5] = { 0, 60, 120, 180, 240 };
		for (int k = 0; k < 5; k++)
		{
			int mi = marks[k];
			if (mi >= static_cast<int>(axis.size()))
				continue;
			CRect lblRc(static_cast<int>(fx(mi)) - g_data.DPI(24), plotRc.bottom + g_data.DPI(4), static_cast<int>(fx(mi)) + g_data.DPI(24), plotRc.bottom + g_data.DPI(20));
			DrawStrMid(g, axis[static_cast<size_t>(mi)], f10.get(), lblRc, MC_TEXT_SUB);
		}
	}

	// 曲线：缺失分钟直接跳过、前后直连（整日分时数据缺点是数据源零星缺失，断段只会更难看）
	auto drawSeries = [&](const std::vector<double>& arr, COLORREF color, float width, bool useIdxAxis) {
		Gdiplus::Pen pen(Gdi(color), width);
		std::vector<Gdiplus::PointF> pts;
		for (int i = 0; i < AXIS_N; i++)
		{
			double v = arr[static_cast<size_t>(i)];
			if (isnan(v))
				continue;
			float y = useIdxAxis ? fyIdx(v) : fyFlow(v);
			pts.push_back(Gdiplus::PointF(fx(i), y));
		}
		if (pts.size() >= 2)
			g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
		};
	if (m_mainflow_series_mask & 8 && idxAny) drawSeries(idxArr, MF_IDX, 1.2f, true);
	if (m_mainflow_series_mask & 1) drawSeries(shArr, MF_SH, 1.8f, false);
	if (m_mainflow_series_mask & 2) drawSeries(szArr, MF_SZ, 1.8f, false);
	if (m_mainflow_series_mask & 4) drawSeries(etfArr, MF_ETF, 1.6f, false);
}

// ============ 页面4：涨跌趋势 ============

void CMarketCenterPanel::DrawTrendPage(Gdiplus::Graphics& g, const CRect& rc)
{
	DrawPageTitle(g, rc, L"涨跌趋势", L"全市场涨跌家数与沪深成交额");

	auto f9 = MkFont(9);
	auto f10 = MkFont(10);
	auto f10b = MkFont(10, true);
	auto f11 = MkFont(11);
	auto f12b = MkFont(12, true);
	auto f15b = MkFont(15, true);

	MC::UpDownDist dist;
	double turnoverToday = 0, turnoverYday = 0;
	{
		CMarketCenterData& mc = CMarketCenterData::Instance();
		std::lock_guard<std::mutex> lock(mc.m_mutex);
		dist = mc.m_dist;
		turnoverToday = mc.m_turnover_today;
		turnoverYday = mc.m_turnover_yesterday;
	}

	long long upCnt = dist.UpCount(), downCnt = dist.DownCount(), flatCnt = dist.FlatCount();

	// 9格统计卡（涨跌5格 + 成交量4格）
	CRect blockRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(16), rc.right - g_data.DPI(16), rc.top + g_data.DPI(16) + g_data.DPI(58));
	FillCard(g, blockRc);
	m_trend_stat_rects.clear();
	// 交易中按当日进度外推；收盘后显示最终全天成交额，避免把最终值伪装成预测。
	const auto& axis = CMarketCenterData::TimeAxis();
	time_t now = time(nullptr);
	struct tm localTm{};
	localtime_s(&localTm, &now);
	wchar_t nowBuf[8];
	swprintf_s(nowBuf, L"%02d:%02d", localTm.tm_hour, localTm.tm_min);
	int nowIdx = CMarketCenterData::TimeIndex(nowBuf);
	double forecast = 0;
	bool forecastOk = false;
	if (nowIdx > 10 && turnoverToday > 0 && m_clock_status == 0)
	{
		forecast = turnoverToday / ((nowIdx + 1.0) / axis.size());
		forecastOk = true;
	}
	const bool finalTurnover = turnoverToday > 0 && m_clock_status != 0;
	double delta = turnoverToday - turnoverYday;

	struct TrendCell { const wchar_t* label; std::wstring value; COLORREF color; };
	wchar_t numBuf[32];
	swprintf_s(numBuf, L"%.0f亿", turnoverToday / 1e8);
	std::wstring todayStr = numBuf;
	swprintf_s(numBuf, L"%.0f亿", turnoverYday / 1e8);
	std::wstring ydayStr = numBuf;
	swprintf_s(numBuf, L"%s%.0f亿", delta >= 0 ? L"+" : L"-", fabs(delta) / 1e8);
	std::wstring deltaStr = numBuf;
	TrendCell tCells[9] = {
		{ L"上涨", FormatInt(upCnt), MC_UP },
		{ L"平盘", FormatInt(flatCnt), MC_TEXT_SUB },
		{ L"下跌", FormatInt(downCnt), MC_DOWN },
		{ L"涨停", FormatInt(dist.zt), MC_UP },
		{ L"跌停", FormatInt(dist.dt), MC_DOWN },
		{ L"当日成交额", todayStr, MC_TEXT },
		{ L"昨日成交", ydayStr, MC_TEXT },
		{ L"较昨日全天", deltaStr, UpDownColor(delta) },
		{ finalTurnover ? L"全天成交" : L"预测全天",
			forecastOk ? (swprintf_s(numBuf, L"%.0f亿", forecast / 1e8), numBuf) : (finalTurnover ? todayStr : std::wstring(L"--")),
			finalTurnover ? COLOR_GOLDEN : MC_TEXT },
	};
	// 列宽按内容加权：大数列（成交额类）多占，避免右侧大数挤压左侧涨跌家数格
	int colW9[9];
	{
		int totalW = 0;
		CSize szTmp;
		for (int i = 0; i < 9; i++)
		{
			auto fProbe = MkFont(15, true);
			szTmp = MeasureStr(g, fProbe.get(), tCells[i].value);
			int wv = max(szTmp.cx, MeasureStr(g, f10.get(), tCells[i].label).cx);
			colW9[i] = max(wv + g_data.DPI(16), g_data.DPI(56));
			totalW += colW9[i];
		}
		int avail = blockRc.Width();
		if (totalW > 0)
			for (int i = 0; i < 9; i++)
				colW9[i] = colW9[i] * avail / totalW;   // 按比例拉伸到满宽
	}
	int accX = blockRc.left;
	for (int i = 0; i < 9; i++)
	{
		CRect cell(accX, blockRc.top, accX + colW9[i], blockRc.bottom);
		accX += colW9[i];
		if (i > 0)
		{
			Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&sepPen, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top + g_data.DPI(10)), Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.bottom - g_data.DPI(10)));
		}
		DrawStrMid(g, tCells[i].label, f10.get(), CRect(cell.left, cell.top + g_data.DPI(8), cell.right, cell.top + g_data.DPI(22)), MC_TEXT_SUB);
		DrawStrMid(g, tCells[i].value, f15b.get(), CRect(cell.left, cell.top + g_data.DPI(26), cell.right, cell.bottom - g_data.DPI(6)), tCells[i].color);
		m_trend_stat_rects.push_back({ cell, i });
	}

	if (dist.buckets.empty())
	{
		auto f12 = MkFont(12);
		DrawStatus(g, CRect(rc.left, blockRc.bottom, rc.right, rc.bottom), CMarketCenterData::DS_TREND, L"正在获取涨跌分布…", f12.get());
		return;
	}

	// ===== 涨跌分布（13桶，撑满下半区；涨跌家数分时已移除——数据源缺失频发）=====
	CRect bodyRc(blockRc.left, blockRc.bottom + g_data.DPI(8), blockRc.right, rc.bottom - g_data.DPI(16));
	CRect distRc(bodyRc.left, bodyRc.top, bodyRc.right, bodyRc.bottom);

	DrawStr(g, L"涨跌分布", f12b.get(), CRect(distRc.left + g_data.DPI(6), distRc.top, distRc.left + g_data.DPI(100), distRc.top + g_data.DPI(18)), MC_TEXT);
	CRect distPlot(distRc.left + g_data.DPI(44), distRc.top + g_data.DPI(24), distRc.right - g_data.DPI(8), distRc.bottom - g_data.DPI(22));

	auto bucketSum = [&dist](int lo, int hi) -> long long {
		long long n = 0;
		for (auto& p : dist.buckets)
			if (p.first >= lo && p.first <= hi)
				n += p.second;
		return n;
		};
	long long zt = dist.zt, dt = dist.dt;
	long long ge10 = max(0LL, bucketSum(10, INT_MAX) - zt);
	long long le_10 = max(0LL, bucketSum(INT_MIN, -10) - dt);
	struct DistBin { const wchar_t* label; long long count; int kind; /*0 up 1 flat 2 down*/ };
	DistBin bins[13] = {
		{ L"涨停", zt, 0 },
		{ L">10%", ge10, 0 },
		{ L"7~10%", bucketSum(7, 9), 0 },
		{ L"5~7%", bucketSum(5, 6), 0 },
		{ L"3~5%", bucketSum(3, 4), 0 },
		{ L"0~3%", bucketSum(1, 2), 0 },
		{ L"平盘", dist.FlatCount(), 1 },
		{ L"0~-3", bucketSum(-2, -1), 2 },
		{ L"-3~-5", bucketSum(-4, -3), 2 },
		{ L"-5~-7", bucketSum(-6, -5), 2 },
		{ L"-7~-10", bucketSum(-9, -7), 2 },
		{ L"<-10%", le_10, 2 },
		{ L"跌停", dt, 2 },
	};
	long long maxCnt = 1;
	for (auto& b : bins)
		maxCnt = max(maxCnt, b.count);
	// 左轴刻度
	{
		double hiV = static_cast<double>(maxCnt);
		double step = NiceStep(hiV);
		for (double tv = 0; tv <= hiV; tv += step)
		{
			float y = distPlot.bottom - static_cast<float>(tv / hiV * distPlot.Height());
			Gdiplus::Pen gridPen(Gdi(MC_GRID), 1.0f);
			g.DrawLine(&gridPen, Gdiplus::REAL(distPlot.left), y, Gdiplus::REAL(distPlot.right), y);
			DrawStr(g, FormatAxisNum(tv, step), f10.get(), CRect(distRc.left + g_data.DPI(2), static_cast<int>(y) - g_data.DPI(8), distPlot.left - g_data.DPI(4), static_cast<int>(y) + g_data.DPI(8)), MC_TEXT_SUB, 255, Gdiplus::StringAlignmentFar);
		}
	}
	m_dist_bars.clear();
	const int nBins = 13;
	int slotW = distPlot.Width() / nBins;
	int barW = slotW * 6 / 10;
	for (int i = 0; i < nBins; i++)
	{
		int cx = distPlot.left + i * slotW + slotW / 2;
		int barH = static_cast<int>(static_cast<double>(bins[i].count) / maxCnt * distPlot.Height());
		CRect barRc(cx - barW / 2, distPlot.bottom - barH, cx + barW / 2, distPlot.bottom);
		COLORREF c = bins[i].kind == 0 ? MC_UP : (bins[i].kind == 1 ? MC_TEXT_DIM : MC_DOWN);
		if (static_cast<int>(i) == m_hover_dist_bar)
			FillRounded(g, CRect(barRc.left - g_data.DPI(1), barRc.top - g_data.DPI(1), barRc.right + g_data.DPI(1), barRc.bottom), RGB(60, 66, 84), 0);
		FillRounded(g, barRc, c, 0);
		DrawStrMid(g, FormatInt(bins[i].count), f9.get(), CRect(barRc.left - g_data.DPI(8), barRc.top - g_data.DPI(14), barRc.right + g_data.DPI(8), barRc.top - g_data.DPI(2)), MC_TEXT_SUB);
		DrawStrMid(g, bins[i].label, f9.get(), CRect(cx - slotW / 2, distPlot.bottom + g_data.DPI(4), cx + slotW / 2, distPlot.bottom + g_data.DPI(18)), MC_TEXT_SUB);
		m_dist_bars.push_back({ barRc, i });
	}
}

// ============ 页面5：ETF涨跌榜 ============

std::vector<int> CMarketCenterPanel::SortedRankList() const
{
	std::vector<int> order;
	for (int i = 0; i < static_cast<int>(m_etfs_snapshot.size()); i++)
		order.push_back(i);
	auto keyLess = [this](int a, int b) -> bool {
		const auto& ea = m_etfs_snapshot[static_cast<size_t>(a)];
		const auto& eb = m_etfs_snapshot[static_cast<size_t>(b)];
		switch (m_rank_sort_key)
		{
		case 0: return ea.name < eb.name;
		case 1: return ea.theme < eb.theme;
		case 2: return ea.price < eb.price;
		case 3: return ea.pct < eb.pct;
		case 4: return ea.amount < eb.amount;
		case 5: return ea.inflow < eb.inflow;
		default: return a < b;
		}
		};
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		bool less = keyLess(a, b);
		if (less) return m_rank_sort_dir < 0;
		if (keyLess(b, a)) return m_rank_sort_dir > 0;
		return a < b;   // 稳定
		});
	return order;
}

void CMarketCenterPanel::DrawEtfRankPage(Gdiplus::Graphics& g, const CRect& rc)
{
	DrawPageTitle(g, rc, L"ETF涨跌榜", L"按涨跌幅排序（列头点击排序）");

	auto f9 = MkFont(9);
	auto f10 = MkFont(10);
	auto f10b = MkFont(10, true);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12b = MkFont(12, true);
	auto f14b = MkFont(14, true);

	if (m_etfs_snapshot.empty())
	{
		auto f12 = MkFont(12);
		DrawStatus(g, rc, CMarketCenterData::DS_ETFS, L"正在获取ETF数据…", f12.get());
		return;
	}

	// 六格工具条
	double upCnt = 0, downCnt = 0, sumPct = 0, sumAmount = 0, sumInflow = 0;
	for (const auto& e : m_etfs_snapshot)
	{
		if (e.pct > 0) upCnt++;
		else if (e.pct < 0) downCnt++;
		sumPct += e.pct;
		sumAmount += e.amount;
		sumInflow += e.inflow;
	}
	double avgPct = m_etfs_snapshot.empty() ? 0 : sumPct / m_etfs_snapshot.size();
	long long etfTotal = m_etf_total > 0 ? m_etf_total : static_cast<long long>(m_etfs_snapshot.size());

	CRect toolbarRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(16), rc.right - g_data.DPI(16), rc.top + g_data.DPI(16) + g_data.DPI(52));
	FillCard(g, toolbarRc);
	m_rank_stat_rects.clear();
	wchar_t buf[48];
	swprintf_s(buf, L"%.0f亿", sumAmount / 1e8);
	std::wstring amountStr = buf;
	swprintf_s(buf, L"%s%.1f亿", sumInflow >= 0 ? L"+" : L"-", fabs(sumInflow) / 1e8);
	std::wstring inflowStr = buf;
	swprintf_s(buf, L"%s%.2f%%", avgPct >= 0 ? L"+" : L"", avgPct);
	std::wstring avgStr = buf;
	struct RankTool { const wchar_t* label; std::wstring value; COLORREF color; };
	RankTool tools[6] = {
		{ L"上涨", FormatInt(static_cast<long long>(upCnt)), MC_UP },
		{ L"下跌", FormatInt(static_cast<long long>(downCnt)), MC_DOWN },
		{ L"平均涨跌", avgStr, UpDownColor(avgPct) },
		{ L"合计成交", amountStr, MC_TEXT },
		{ L"主力净流入", inflowStr, UpDownColor(sumInflow) },
		{ L"ETF总数", FormatInt(etfTotal), MC_TEXT },
	};
	const int cellW6 = toolbarRc.Width() / 6;
	for (int i = 0; i < 6; i++)
	{
		CRect cell(toolbarRc.left + i * cellW6, toolbarRc.top, toolbarRc.left + (i + 1) * cellW6, toolbarRc.bottom);
		if (i > 0)
		{
			Gdiplus::Pen sepPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&sepPen, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top + g_data.DPI(8)), Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.bottom - g_data.DPI(8)));
		}
		DrawStrMid(g, tools[i].label, f11.get(), CRect(cell.left, cell.top + g_data.DPI(7), cell.right, cell.top + g_data.DPI(21)), MC_TEXT_SUB);
		DrawStrMid(g, tools[i].value, f14b.get(), CRect(cell.left, cell.top + g_data.DPI(24), cell.right, cell.bottom - g_data.DPI(5)), tools[i].color);
		m_rank_stat_rects.push_back({ cell, i });
	}

	// 表格
	CRect tableRc(toolbarRc.left, toolbarRc.bottom + g_data.DPI(8), toolbarRc.right, rc.bottom - g_data.DPI(16));
	m_rank_table_rect = tableRc;
	const int headerH = g_data.DPI(28);
	const int rowH = g_data.DPI(28);
	CRect headerRc(tableRc.left, tableRc.top, tableRc.right, tableRc.top + headerH);
	CRect listRc(tableRc.left, headerRc.bottom, tableRc.right, tableRc.bottom);

	// 列布局：名称弹性，代码/行业/现价/涨跌幅/成交额/主力净流入 固定宽度
	int fixed[6] = { 56, 84, 72, 96, 90, 104 };   // 代码/行业/现价/涨跌幅/成交额/主力净流入 (96dpi)
	int fixedSum = 0;
	for (int w : fixed) fixedSum += w;
	float S = static_cast<float>(g_data.GetDpi()) / 96.0f;
	int nameW = max(g_data.DPI(150), tableRc.Width() - static_cast<int>(fixedSum * S));
	int colX[7];
	colX[0] = tableRc.left + g_data.DPI(8);
	colX[1] = colX[0] + nameW;
	for (int i = 2; i <= 6; i++)
		colX[i] = colX[i - 1] + static_cast<int>(fixed[i - 2] * S);
	int colEnd = tableRc.right - g_data.DPI(6);

	// 表头 key：0=名称 1=行业 2=现价 3=涨跌幅 4=成交额 5=主力净流入；代码列(-1)不参与排序
	const wchar_t* headers[7] = { L"名称", L"代码", L"行业", L"现价", L"涨跌幅", L"成交额(亿)", L"主力净流入(亿)" };
	m_rank_cols.clear();
	for (int i = 0; i < 7; i++)
	{
		CRect colRc(colX[i], headerRc.top, i == 0 ? colX[1] : (i == 6 ? colEnd : colX[i + 1]), headerRc.bottom);
		RankCol rc2;
		rc2.rect = colRc;
		rc2.key = (i == 0 ? 0 : (i >= 2 ? i - 1 : -1));
		rc2.sortedUp = (rc2.key >= 0 && m_rank_sort_key == rc2.key && m_rank_sort_dir > 0);
		m_rank_cols.push_back(rc2);
		// 表头背景 + hover
		if (m_hover_rank_header == i)
			FillRounded(g, colRc, MC_TEXT, 0, 12);
			CRect lblRc = colRc;
			const bool isSorted = rc2.key >= 0 && m_rank_sort_key == rc2.key;
			if (isSorted)
				lblRc.right -= g_data.DPI(12);
			const COLORREF headerColor = isSorted ? MC_ACCENT : MC_TEXT_DIM;
			DrawStr(g, headers[i], f11.get(), lblRc, headerColor, 255, i == 0 ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
			if (isSorted)
			{
				const int iconSize = g_data.DPI(12);
				Icons::Draw(g, m_rank_sort_dir > 0 ? Icons::Id::ChevronUp : Icons::Id::ChevronDown,
					Gdiplus::RectF(static_cast<Gdiplus::REAL>(colRc.right - iconSize - g_data.DPI(3)),
						static_cast<Gdiplus::REAL>(colRc.top + (colRc.Height() - iconSize) / 2),
						static_cast<Gdiplus::REAL>(iconSize), static_cast<Gdiplus::REAL>(iconSize)), headerColor);
			}
	}
	Gdiplus::Pen headPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&headPen, Gdiplus::REAL(tableRc.left), Gdiplus::REAL(headerRc.bottom - 1), Gdiplus::REAL(tableRc.right), Gdiplus::REAL(headerRc.bottom - 1));
	// 外框
	{
		Gdiplus::Pen framePen(Gdi(MC_BORDER), 1.0f);
		g.DrawRectangle(&framePen, Gdiplus::REAL(tableRc.left), Gdiplus::REAL(tableRc.top), Gdiplus::REAL(tableRc.Width()), Gdiplus::REAL(tableRc.Height()));
	}

	// 行
	std::vector<int> order = SortedRankList();
	int viewH = listRc.Height();
	int totalH = static_cast<int>(order.size()) * rowH;
	m_rank_scroll_max = max(0, totalH - viewH);
	m_rank_scroll = min(m_rank_scroll, m_rank_scroll_max);
	int firstRow = m_rank_scroll / rowH;
	int visibleRows = viewH / rowH + 1;

	m_rank_row_etf.clear();
	g.SetClip(Gdiplus::Rect(Gdiplus::REAL(listRc.left), Gdiplus::REAL(listRc.top), Gdiplus::REAL(listRc.Width()), Gdiplus::REAL(listRc.Height())));
	for (int r = firstRow; r < static_cast<int>(order.size()) && r < firstRow + visibleRows; r++)
	{
		int etfIdx = order[static_cast<size_t>(r)];
		const auto& e = m_etfs_snapshot[static_cast<size_t>(etfIdx)];
		CRect rowRc(listRc.left, listRc.top + r * rowH - m_rank_scroll, listRc.right, listRc.top + (r + 1) * rowH - m_rank_scroll);
		bool hover = (r == m_hover_rank_row);
		if (hover)
			FillRounded(g, rowRc, MC_TEXT, 0, 10);
		m_rank_row_etf.push_back(etfIdx);

		// 名称 / 代码（独立列，字号与表格其他列一致）
		DrawStr(g, e.name, f11b.get(), CRect(colX[0], rowRc.top, colX[1] - g_data.DPI(6), rowRc.bottom), MC_TEXT);
		DrawStrMid(g, e.code, f11.get(), CRect(colX[1], rowRc.top, colX[2], rowRc.bottom), MC_TEXT_SUB);
		// 行业（纯文字，与表格其他列一致）
		DrawStrMid(g, e.theme, f11.get(), CRect(colX[2], rowRc.top, colX[3], rowRc.bottom), MC_TEXT_SUB);
		// 现价
		swprintf_s(buf, L"%.3f", e.price);
		DrawStrMid(g, buf, f11.get(), CRect(colX[3], rowRc.top, colX[4], rowRc.bottom), MC_TEXT);
		// 涨跌幅 pill（宽度贴文字，列内居中）
		{
			CSize szPct = MeasureStr(g, f11.get(), FormatPct(e.pct));
			int pillW = min(szPct.cx + g_data.DPI(12), colX[5] - colX[4]);
			int cxMid = (colX[4] + colX[5]) / 2;
			CRect pillRc(cxMid - pillW / 2, rowRc.top + g_data.DPI(5), cxMid + pillW / 2, rowRc.bottom - g_data.DPI(5));
			FillRounded(g, pillRc, e.pct >= 0 ? MC_UP : MC_DOWN, static_cast<float>(pillRc.Height() / 2), 36);
			DrawStrMid(g, FormatPct(e.pct), f11.get(), pillRc, e.pct >= 0 ? RGB(255, 173, 183) : RGB(148, 240, 200));
		}
		// 成交额
		swprintf_s(buf, L"%.1f", e.amount / 1e8);
		DrawStrMid(g, buf, f11.get(), CRect(colX[5], rowRc.top, colX[6], rowRc.bottom), MC_TEXT);
		// 主力净流入
		swprintf_s(buf, L"%s%.2f", e.inflow >= 0 ? L"+" : L"-", fabs(e.inflow) / 1e8);
		DrawStrMid(g, buf, f11.get(), CRect(colX[6], rowRc.top, colEnd, rowRc.bottom), UpDownColor(e.inflow));
	}
	g.ResetClip();
}

// ============ 交互 ============

bool CMarketCenterPanel::HandleMouseMove(CPoint point)
{
	m_mouse_pos = point;

	bool changed = false;
	int newHoverMenu = -1;
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		if (m_menu_item_rects[i].PtInRect(point))
		{
			newHoverMenu = i;
			break;
		}
	}
	if (newHoverMenu != m_hover_menu)
	{
		m_hover_menu = newHoverMenu;
		changed = true;
	}

	// 页面内 hover（命中缓存矩形）
	if (point.x >= m_content_rect.left)
	{
		switch (m_page)
		{
		case PAGE_BUBBLE:
		{
			int hovStat = -1;
			for (int i = 0; i < 2; i++)
			{
				if (!m_bubble_stat_rects[i].IsRectEmpty() && m_bubble_stat_rects[i].PtInRect(point))
				{
					hovStat = i;
					break;
				}
			}
			if (hovStat != m_hover_bubble_stat)
			{
				m_hover_bubble_stat = hovStat;
				changed = true;
			}

			int hovTab = -1;
			for (int i = 0; i < 2; i++)
			{
				if (!m_sector_tab_rects[i].IsRectEmpty() && m_sector_tab_rects[i].PtInRect(point))
				{
					hovTab = i;
					break;
				}
			}
			if (hovTab != m_hover_sector_tab)
			{
				m_hover_sector_tab = hovTab;
				changed = true;
			}

			if (m_sector_view_mode == 0)
			{
				int hov = -1;
				for (int i = 0; i < static_cast<int>(m_treemap_cells.size()); i++)
				{
					if (m_treemap_cells[static_cast<size_t>(i)].rect.PtInRect(point))
					{
						hov = m_treemap_cells[static_cast<size_t>(i)].sectorIdx;
						break;
					}
				}
				if (hov != m_hover_bubble)
				{
					m_hover_bubble = hov;
					changed = true;
				}
			}
			else
			{
				int hovIdx = -1;
				if (!m_sector_timeline_inner_rect.IsRectEmpty() && m_sector_timeline_inner_rect.PtInRect(point))
				{
					float mouseX = static_cast<float>(point.x);
					hovIdx = static_cast<int>(round((mouseX - m_sector_timeline_inner_rect.left) * 240.0f / m_sector_timeline_inner_rect.Width()));
					if (hovIdx < 0) hovIdx = 0;
					if (hovIdx > 240) hovIdx = 240;
				}
				if (hovIdx != m_hover_timeline_idx)
				{
					m_hover_timeline_idx = hovIdx;
					changed = true;
				}
				if (m_bubble_chart_rect.PtInRect(point))
				{
					changed = true;
				}
			}
			break;
		}
		case PAGE_ETF_INFLOW:
		{
			int hovBar = -1, hovCard = -1;
			int hovThemeRow = -1;
			bool hovThemeClose = false;
			if (m_theme_panel_open && m_theme_panel_rect.PtInRect(point))
			{
				hovThemeClose = (m_theme_close_rect.PtInRect(point) != FALSE);
				if (!hovThemeClose && m_theme_list_rect.PtInRect(point) && m_theme_row_h > 0 && !m_theme_row_etfs.empty())
				{
					int relY = point.y - m_theme_list_rect.top;
					if (relY >= 0)
					{
						int slot = relY / m_theme_row_h;
						int itemIdx = m_theme_panel_scroll + slot;
						if (itemIdx >= 0 && itemIdx < static_cast<int>(m_theme_row_etfs.size()))
							hovThemeRow = itemIdx;
					}
				}
			}
			else
			{
				for (int i = 0; i < static_cast<int>(m_inflow_stat_rects.size()); i++)
					if (i < 2 && m_inflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
						hovCard = i;
				for (int i = 0; i < static_cast<int>(m_inflow_bars.size()); i++)
					if (m_inflow_bars[static_cast<size_t>(i)].rect.PtInRect(point))
						hovBar = i;
			}
			if (hovBar != m_hover_inflow_bar)
			{
				m_hover_inflow_bar = hovBar;
				changed = true;
			}
			if (hovCard != m_hover_inflow_card)
			{
				m_hover_inflow_card = hovCard;
				changed = true;
			}
			if (hovThemeRow != m_hover_theme_row)
			{
				m_hover_theme_row = hovThemeRow;
				changed = true;
			}
			if (hovThemeClose != m_hover_theme_close)
			{
				m_hover_theme_close = hovThemeClose;
				changed = true;
			}
			break;
		}
		case PAGE_MONEY_FLOW:
		{
			int hov = -1;
			for (int i = 0; i < static_cast<int>(m_moneyflow_stat_rects.size()); i++)
				if (m_moneyflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
					hov = i;
			if (hov != m_hover_moneyflow_card)
			{
				m_hover_moneyflow_card = hov;
				changed = true;
			}
			int hovIdx = -1;
			if (m_moneyflow_plot_rect.PtInRect(point) && m_moneyflow_plot_rect.Width() > 0)
			{
				float mouseX = static_cast<float>(point.x);
				hovIdx = static_cast<int>(round((mouseX - m_moneyflow_plot_rect.left) * 240.0f / m_moneyflow_plot_rect.Width()));
				if (hovIdx < 0) hovIdx = 0;
				if (hovIdx > 240) hovIdx = 240;
			}
			if (hovIdx != m_hover_moneyflow_idx)
			{
				m_hover_moneyflow_idx = hovIdx;
				changed = true;
			}
			break;
		}
		case PAGE_MAINFLOW:
		{
			int hov = -1;
			for (int i = 0; i < static_cast<int>(m_mainflow_stat_rects.size()); i++)
				if (m_mainflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
					hov = i;
			if (hov != m_hover_mainflow_card)
			{
				m_hover_mainflow_card = hov;
				changed = true;
			}
			break;
		}
		case PAGE_TREND:
		{
			int hov = -1;
			for (int i = 0; i < static_cast<int>(m_dist_bars.size()); i++)
				if (m_dist_bars[static_cast<size_t>(i)].rect.PtInRect(point))
					hov = i;
			if (hov != m_hover_dist_bar)
			{
				m_hover_dist_bar = hov;
				changed = true;
			}
			break;
		}
		case PAGE_ETF_RANK:
		{
			int hovHeader = -1;
			for (int i = 0; i < static_cast<int>(m_rank_cols.size()); i++)
				if (m_rank_cols[static_cast<size_t>(i)].rect.PtInRect(point))
					hovHeader = i;
			if (hovHeader != m_hover_rank_header)
			{
				m_hover_rank_header = hovHeader;
				changed = true;
			}
			int hovRow = -1;
			if (hovHeader < 0 && m_rank_table_rect.PtInRect(point) && !m_rank_row_etf.empty())
			{
				const int rowH = g_data.DPI(28);
				int headerH = g_data.DPI(28);
				// m_rank_row_etf 按可见槽位顺序存放，先算可见槽位再加 firstRow 得绘制时的绝对行号
				int relY = point.y - m_rank_table_rect.top - headerH;
				if (relY >= 0)
				{
					int slot = relY / rowH;
					if (slot < static_cast<int>(m_rank_row_etf.size()))
						hovRow = m_rank_scroll / rowH + slot;
				}
			}
			if (hovRow != m_hover_rank_row)
			{
				m_hover_rank_row = hovRow;
				changed = true;
			}
			break;
		}
		}
	}

	return changed;
}

void CMarketCenterPanel::HandleMouseLeave()
{
	if (m_hover_menu != -1 || m_hover_bubble != -1 || m_hover_inflow_bar != -1 ||
		m_hover_moneyflow_card != -1 || m_hover_moneyflow_idx != -1 || m_hover_mainflow_card != -1 || m_hover_inflow_card != -1 || m_hover_dist_bar != -1 ||
		m_hover_rank_header != -1 || m_hover_rank_row != -1 || m_hover_theme_row != -1 || m_hover_theme_close)
	{
		m_hover_menu = -1;
		m_hover_bubble = -1;
		m_hover_inflow_bar = -1;
		m_hover_moneyflow_card = -1;
		m_hover_moneyflow_idx = -1;
		m_hover_mainflow_card = -1;
		m_hover_inflow_card = -1;
		m_hover_dist_bar = -1;
		m_hover_rank_header = -1;
		m_hover_rank_row = -1;
		m_hover_theme_row = -1;
		m_hover_theme_close = false;
	}
}

void CMarketCenterPanel::HandleLButtonDown(CPoint point)
{
	// 点击"获取失败，点击重试"文案 → 重试当前页
	if (!m_status_rect.IsRectEmpty() && m_status_rect.PtInRect(point) &&
		CMarketCenterData::Instance().HasFailed(CurrentDataSet()))
	{
		RetryCurrentPage();
		return;
	}

	// 菜单切换
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		if (m_menu_item_rects[i].PtInRect(point))
		{
			SwitchPage(static_cast<McPage>(i));
			return;
		}
	}

	if (point.x < m_content_rect.left)   // 侧栏点击由菜单项矩形处理；时钟已在侧栏内，不在此拦截
	{
		return;
	}

	switch (m_page)
	{
	case PAGE_BUBBLE:
	{
		// 点击板块视图切换Tab [资金树图] [时间走向]
		for (int i = 0; i < 2; i++)
		{
			if (!m_sector_tab_rects[i].IsRectEmpty() && m_sector_tab_rects[i].PtInRect(point))
			{
				if (m_sector_view_mode != i)
				{
					m_sector_view_mode = i;
					RequestData();
				}
				return;
			}
		}

		// 点击流入/流出合计卡片 → 切换单色/过滤视图（再点同一个恢复红绿全部）
		for (int i = 0; i < 2; i++)
		{
			if (!m_bubble_stat_rects[i].IsRectEmpty() && m_bubble_stat_rects[i].PtInRect(point))
			{
				m_treemap_mode = (m_treemap_mode == i + 1) ? 0 : i + 1;
				m_bubble_layout_dirty = true;
				return;
			}
		}

		if (m_sector_view_mode == 0)
		{
			for (const auto& cellNode : m_treemap_cells)
			{
				if (cellNode.rect.PtInRect(point))
				{
					m_selected_sector = cellNode.sectorIdx;
					return;
				}
			}
		}
		else
		{
			if (m_bubble_chart_rect.PtInRect(point) && m_hover_timeline_sector >= 0)
			{
				m_selected_sector = m_hover_timeline_sector;
				return;
			}
		}
		break;
	}
	case PAGE_ETF_INFLOW:
	{
		if (m_theme_panel_open)
		{
			if (m_theme_close_rect.PtInRect(point))
			{
				m_theme_panel_open = false;
				m_hover_theme_row = -1;
				m_hover_theme_close = false;
				return;
			}
			if (m_theme_panel_rect.PtInRect(point))
			{
				// 点击浮层列表中的具体 ETF：跳转至日 K 线
				if (m_theme_list_rect.PtInRect(point) && m_theme_row_h > 0 && !m_theme_row_etfs.empty())
				{
					int relY = point.y - m_theme_list_rect.top;
					if (relY >= 0)
					{
						int slot = relY / m_theme_row_h;
						int itemIdx = m_theme_panel_scroll + slot;
						if (itemIdx >= 0 && itemIdx < static_cast<int>(m_theme_row_etfs.size()))
						{
							int etfIdx = m_theme_row_etfs[static_cast<size_t>(itemIdx)];
							if (etfIdx >= 0 && m_notify_wnd)
								::PostMessage(m_notify_wnd, WM_MC_ETF_CLICKED, static_cast<WPARAM>(etfIdx), 0);
							return;
						}
					}
				}
				return; // 拦截浮层内的其他点击（如标题/表头），防止穿透到底部条形
			}
		}
		for (int i = 0; i < 2 && i < static_cast<int>(m_inflow_stat_rects.size()); i++)
		{
			if (m_inflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
			{
				bool out = (i == 1);
				if (out != m_inflow_out)
				{
					m_inflow_out = out;
					m_theme_panel_open = false;
				}
				return;
			}
		}
		for (const auto& bar : m_inflow_bars)
		{
			if (bar.rect.PtInRect(point))
			{
				// 打开该主题的全部 ETF 列表浮层
				if (bar.themeIdx >= 0 && bar.themeIdx < static_cast<int>(m_theme_inflow.size()))
				{
					const std::wstring& theme = m_theme_inflow[static_cast<size_t>(bar.themeIdx)].theme;
					m_theme_row_etfs.clear();
					for (int i = 0; i < static_cast<int>(m_etfs_snapshot.size()); i++)
						if (m_etfs_snapshot[static_cast<size_t>(i)].theme == theme)
							m_theme_row_etfs.push_back(i);
					if (!m_theme_row_etfs.empty())
					{
						// 按涨跌幅降序排列
						std::sort(m_theme_row_etfs.begin(), m_theme_row_etfs.end(), [this](int a, int b) {
							return m_etfs_snapshot[static_cast<size_t>(a)].pct > m_etfs_snapshot[static_cast<size_t>(b)].pct;
						});
						m_theme_panel_title = theme + L" · 共" + std::to_wstring(m_theme_row_etfs.size()) + L"只";
						m_theme_panel_scroll = 0;
						m_hover_theme_row = -1;
						m_hover_theme_close = false;
						m_theme_panel_open = true;
					}
				}
				return;
			}
		}
		// 点击浮层外部空白区：关闭浮层
		if (m_theme_panel_open)
		{
			m_theme_panel_open = false;
			m_hover_theme_row = -1;
			m_hover_theme_close = false;
		}
		break;
	}
	case PAGE_MONEY_FLOW:
	{
		for (int i = 0; i < static_cast<int>(m_moneyflow_stat_rects.size()); i++)
		{
			if (m_moneyflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
			{
				m_moneyflow_series_mask ^= (1 << i);
				if (m_moneyflow_series_mask == 0)
					m_moneyflow_series_mask = 0xF;
				return;
			}
		}
		break;
	}
	case PAGE_MAINFLOW:
	{
		for (int i = 0; i < static_cast<int>(m_mainflow_stat_rects.size()); i++)
		{
			if (m_mainflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
			{
				m_mainflow_series_mask ^= (1 << i);
				if (m_mainflow_series_mask == 0)
					m_mainflow_series_mask = 0xF;
				return;
			}
		}
		break;
	}
	case PAGE_ETF_RANK:
	{
		for (int i = 0; i < static_cast<int>(m_rank_cols.size()); i++)
		{
			if (m_rank_cols[static_cast<size_t>(i)].rect.PtInRect(point))
			{
				int key = m_rank_cols[static_cast<size_t>(i)].key;
				if (key < 0)
					return;   // 代码列不支持排序
				if (m_rank_sort_key == key)
					m_rank_sort_dir = -m_rank_sort_dir;
				else
				{
					m_rank_sort_key = key;
					m_rank_sort_dir = (key == 3) ? -1 : 1;    // 涨跌幅默认降序，其余升序
				}
				m_rank_scroll = 0;
				m_hover_rank_row = -1;
				return;
			}
		}
		// 点击数据行 → 通知悬浮窗跳转该 ETF 的 K 线（m_rank_row_etf 按可见槽位顺序存放：
		// 第 0 项即当前滚到的第一行，点击坐标只除以行高得槽位，不再叠加 m_rank_scroll）
		if (m_rank_table_rect.PtInRect(point) && !m_rank_row_etf.empty())
		{
			const int rowH = g_data.DPI(28);
			int headerH = g_data.DPI(28);
			int relY = point.y - m_rank_table_rect.top - headerH;
			if (relY >= 0)
			{
				int slot = relY / rowH;
				if (slot < static_cast<int>(m_rank_row_etf.size()) && m_notify_wnd)
					::PostMessage(m_notify_wnd, WM_MC_ETF_CLICKED, static_cast<WPARAM>(m_rank_row_etf[static_cast<size_t>(slot)]), 0);
			}
		}
		break;
	}
	default:
		break;
	}
}

void CMarketCenterPanel::HandleMouseWheel(short zDelta, CPoint point)
{
	bool scrolled = false;
	if (m_page == PAGE_ETF_INFLOW && m_theme_panel_open && m_theme_panel_rect.PtInRect(point))
	{
		int old = m_theme_panel_scroll;
		m_theme_panel_scroll = max(0, m_theme_panel_scroll - (zDelta / 120) * 3);
		m_theme_panel_scroll = min(m_theme_panel_scroll, m_theme_panel_scroll_max);
		scrolled = (old != m_theme_panel_scroll);
	}
	else if (m_page == PAGE_ETF_RANK && m_rank_table_rect.PtInRect(point))
	{
		int old = m_rank_scroll;
		m_rank_scroll = max(0, m_rank_scroll - (zDelta / 120) * g_data.DPI(28) * 3);   // 每格滚动3行
		scrolled = (old != m_rank_scroll);
	}
	(void)scrolled;
}

bool CMarketCenterPanel::IsCursorOverInteractive(CPoint point) const
{
	// "获取失败，点击重试"文案
	if (!m_status_rect.IsRectEmpty() && m_status_rect.PtInRect(point) &&
		CMarketCenterData::Instance().HasFailed(CurrentDataSet()))
		return true;

	bool hand = false;
	for (int i = 0; i < PAGE_COUNT && !hand; i++)
		hand = m_menu_item_rects[i].PtInRect(point) != FALSE;
	if (!hand)
	{
		switch (m_page)
		{
		case PAGE_BUBBLE:
			for (int i = 0; i < 2; i++)
				if (!m_bubble_stat_rects[i].IsRectEmpty() && m_bubble_stat_rects[i].PtInRect(point)) { hand = true; break; }
			if (!hand)
				for (int i = 0; i < 2; i++)
					if (!m_sector_tab_rects[i].IsRectEmpty() && m_sector_tab_rects[i].PtInRect(point)) { hand = true; break; }
			if (!hand && m_sector_view_mode == 0)
				for (const auto& cellNode : m_treemap_cells)
					if (cellNode.rect.PtInRect(point)) { hand = true; break; }
			if (!hand && m_sector_view_mode == 1 && m_bubble_chart_rect.PtInRect(point))
				hand = true;
			break;
		case PAGE_ETF_INFLOW:
			if (m_theme_panel_open && m_theme_panel_rect.PtInRect(point))
			{
				hand = m_theme_close_rect.PtInRect(point) ||
					(m_theme_list_rect.PtInRect(point) && m_hover_theme_row >= 0);
			}
			else
			{
				hand = (m_inflow_stat_rects.size() > 1 && (m_inflow_stat_rects[0].rect.PtInRect(point) || m_inflow_stat_rects[1].rect.PtInRect(point)));
				if (!hand)
					for (const auto& bar : m_inflow_bars)
						if (bar.rect.PtInRect(point)) { hand = true; break; }
			}
			break;
		case PAGE_MAINFLOW:
			for (const auto& c : m_mainflow_stat_rects)
				if (c.rect.PtInRect(point)) { hand = true; break; }
			break;
		case PAGE_ETF_RANK:
			for (const auto& c : m_rank_cols)
				if (c.rect.PtInRect(point)) { hand = true; break; }
			if (!hand && !m_rank_table_rect.IsRectEmpty() && point.y >= m_rank_table_rect.top + g_data.DPI(28) && m_rank_table_rect.PtInRect(point))
				hand = true;   // 数据行区域可点击
			break;
		default:
			break;
		}
	}
	return hand;
}
