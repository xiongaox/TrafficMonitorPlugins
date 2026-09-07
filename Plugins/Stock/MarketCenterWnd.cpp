#include "pch.h"
#include "MarketCenterWnd.h"
#include "ChartColors.h"
#include "Common.h"
#include "Stock.h"
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

	// 漂亮的坐标轴刻度步长
	double NiceStep(double range)
	{
		double raw = range / 3.0;
		if (raw <= 0) return 1.0;
		double mag = pow(10.0, floor(log10(raw)));
		double norm = raw / mag;
		double step = norm < 1.5 ? 1 : (norm < 3.5 ? 2 : (norm < 7.5 ? 5 : 10));
		return step * mag;
	}

	std::wstring FormatAxisNum(double v, double step)
	{
		wchar_t buf[32];
		if (fabs(step) >= 1.0)
			swprintf_s(buf, L"%.0f", v);
		else
			swprintf_s(buf, L"%.1f", v);
		return buf;
	}
}

CMarketCenterWnd::CMarketCenterWnd()
{
}

CMarketCenterWnd::~CMarketCenterWnd()
{
}

void CMarketCenterWnd::PostNcDestroy()
{
	Stock::Instance().OnMarketCenterWndClosed();
	delete this;
}

BEGIN_MESSAGE_MAP(CMarketCenterWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEWHEEL()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_SETCURSOR()
	ON_WM_GETMINMAXINFO()
	ON_MESSAGE(WM_MC_DATA_UPDATED, &CMarketCenterWnd::OnDataUpdated)
END_MESSAGE_MAP()

BOOL CMarketCenterWnd::Create(CWnd* pParent)
{
	WNDCLASS wc{};
	HINSTANCE hInst = AfxGetInstanceHandle();
	if (!::GetClassInfo(hInst, L"CStockMarketCenterWnd", &wc))
	{
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ::DefWindowProc;
		wc.hInstance = hInst;
		wc.hIcon = NULL;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszClassName = L"CStockMarketCenterWnd";
		if (!AfxRegisterClass(&wc))
			return FALSE;
	}

	// 初始尺寸 1180x740（96dpi 逻辑像素，随显示器 DPI 放大）
	int w = g_data.DPI(1180), h = g_data.DPI(740);
	CRect work(0, 0, GetSystemMetrics(SM_CXFULLSCREEN), GetSystemMetrics(SM_CYFULLSCREEN));
	int x = max(0, (work.Width() - w) / 2);
	int y = max(0, (work.Height() - h) / 2);

	if (!CreateEx(WS_EX_APPWINDOW, L"CStockMarketCenterWnd", L"行情中心",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		x, y, w, h, pParent ? pParent->GetSafeHwnd() : NULL, 0))
	{
		return FALSE;
	}

	// 深色标题栏（dwmapi 动态加载，失败无害）
	HMODULE dwm = ::GetModuleHandleW(L"dwmapi.dll");
	if (dwm)
	{
		using DwmSetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		auto fn = reinterpret_cast<DwmSetAttrFn>(::GetProcAddress(dwm, "DwmSetWindowAttribute"));
		if (fn)
		{
			BOOL val = TRUE;
			fn(m_hWnd, 20, &val, sizeof(val));  // DWMWA_USE_IMMERSIVE_DARK_MODE (20)
			fn(m_hWnd, 19, &val, sizeof(val));  // 旧版本属性值 19
		}
	}

	UpdateClock();
	SetTimer(MC_REFRESH_TIMER, 1000, NULL);
	RequestData();
	return TRUE;
}

BOOL CMarketCenterWnd::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

void CMarketCenterWnd::OnDestroy()
{
	KillTimer(MC_REFRESH_TIMER);
	CWnd::OnDestroy();
}

void CMarketCenterWnd::OnSize(UINT nType, int cx, int cy)
{
	m_bubble_layout_dirty = true;   // 气泡布局随窗口尺寸重排
	CWnd::OnSize(nType, cx, cy);
}

void CMarketCenterWnd::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = g_data.DPI(860);
	lpMMI->ptMinTrackSize.y = g_data.DPI(540);
	CWnd::OnGetMinMaxInfo(lpMMI);
}

void CMarketCenterWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == MC_REFRESH_TIMER)
	{
		UpdateClock();
		RequestData();
		Invalidate(FALSE);
	}
	CWnd::OnTimer(nIDEvent);
}

void CMarketCenterWnd::UpdateClock()
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

void CMarketCenterWnd::RequestData()
{
	HWND hWnd = GetSafeHwnd();
	CMarketCenterData& mc = CMarketCenterData::Instance();
	// 常规刷新：板块/趋势/主力 120s；ETF 全量 5min（低频避免东财 WAF 频控）
	mc.RequestIfStale(CMarketCenterData::DS_SECTORS, 120, hWnd);
	mc.RequestIfStale(CMarketCenterData::DS_MAINFLOW, 120, hWnd);
	mc.RequestIfStale(CMarketCenterData::DS_TREND, 120, hWnd);
	mc.RequestIfStale(CMarketCenterData::DS_ETFS, 300, hWnd);
}

LRESULT CMarketCenterWnd::OnDataUpdated(WPARAM wParam, LPARAM lParam)
{
	Invalidate(FALSE);
	return 0;
}

void CMarketCenterWnd::SwitchPage(McPage page)
{
	if (m_page == page)
		return;
	m_page = page;
	m_theme_panel_open = false;
	m_rank_scroll = 0;
	m_hover_bubble = -1;
	m_hover_inflow_bar = -1;
	Invalidate();
}

void CMarketCenterWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);
	if (rect.IsRectEmpty())
		return;

	CDC memDC;
	CBitmap memBitmap;
	memDC.CreateCompatibleDC(&dc);
	memBitmap.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
	CBitmap* pOldBitmap = memDC.SelectObject(&memBitmap);
	memDC.FillSolidRect(rect, MC_BG);
	memDC.SetBkMode(TRANSPARENT);

	{
		Gdiplus::Graphics graphics(memDC.GetSafeHdc());
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		DrawAll(graphics, rect);
	}

	dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBitmap);
}

void CMarketCenterWnd::RefreshSnapshots()
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
	if (mc.m_etfs_time != m_etfs_snapshot_time)
	{
		m_etfs_snapshot = mc.m_etfs;
		m_etfs_snapshot_time = mc.m_etfs_time;
		BuildThemeInflow();
	}
}

void CMarketCenterWnd::BuildThemeInflow()
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

void CMarketCenterWnd::DrawAll(Gdiplus::Graphics& g, const CRect& client)
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

	// 右上角时钟
	{
		int clockH = g_data.DPI(24);
		int clockW = g_data.DPI(120);
		m_clock_rect = CRect(content.right - g_data.DPI(16) - clockW, content.top + g_data.DPI(8),
			content.right - g_data.DPI(16), content.top + g_data.DPI(8) + clockH);
		FillCard(g, m_clock_rect);
		COLORREF dotColor = m_clock_status == 0 ? MC_UP : MC_TEXT_DIM;
		Gdiplus::SolidBrush dotBrush(Gdi(dotColor));
		float dotR = g_data.DPI(7) / 2.0f;
		float cy = m_clock_rect.top + clockH / 2.0f;
		g.FillEllipse(&dotBrush, m_clock_rect.left + g_data.DPI(9), cy - dotR, dotR * 2, dotR * 2);
		auto f10 = MkFont(10);
		DrawStr(g, m_clock_status == 0 ? L"开市" : L"休市", f10.get(), CRect(m_clock_rect.left + g_data.DPI(20), m_clock_rect.top, m_clock_rect.left + g_data.DPI(52), m_clock_rect.bottom), MC_TEXT_SUB);
		auto f11 = MkFont(11, true);
		DrawStr(g, m_clock_time, f11.get(), CRect(m_clock_rect.left + g_data.DPI(50), m_clock_rect.top, m_clock_rect.right - g_data.DPI(4), m_clock_rect.bottom), MC_TEXT);
	}
}

void CMarketCenterWnd::DrawSidebar(Gdiplus::Graphics& g, const CRect& rc)
{
	// 品牌行：圆点 + “行情中心”
	Gdiplus::SolidBrush dotBrush(Gdi(MC_ACCENT));
	g.FillEllipse(&dotBrush, Gdiplus::REAL(rc.left + g_data.DPI(14)), Gdiplus::REAL(rc.top + g_data.DPI(15)), Gdiplus::REAL(g_data.DPI(8)), Gdiplus::REAL(g_data.DPI(8)));
	auto f14 = MkFont(14, true);
	DrawStr(g, L"行情中心", f14.get(), CRect(rc.left + g_data.DPI(27), rc.top + g_data.DPI(6), rc.right, rc.top + g_data.DPI(32)), MC_TEXT);

	// 菜单项
	const wchar_t* titles[PAGE_COUNT] = { L"基金气泡图", L"ETF申购净流入", L"主力资金", L"涨跌趋势", L"ETF涨跌榜" };
	int itemH = g_data.DPI(34);
	int top = rc.top + g_data.DPI(44);
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

	// 底部提示 + 分隔线
	auto f10 = MkFont(10);
	DrawStr(g, L"数据来源：东方财富", f10.get(), CRect(rc.left + g_data.DPI(10), rc.bottom - g_data.DPI(28), rc.right - g_data.DPI(2), rc.bottom - g_data.DPI(6)), MC_TEXT_DIM);
	Gdiplus::Pen divPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&divPen, Gdiplus::REAL(rc.right), Gdiplus::REAL(rc.top), Gdiplus::REAL(rc.right), Gdiplus::REAL(rc.bottom));
}

void CMarketCenterWnd::DrawPage(Gdiplus::Graphics& g, const CRect& content)
{
	switch (m_page)
	{
	case PAGE_BUBBLE: DrawBubblePage(g, content); break;
	case PAGE_ETF_INFLOW: DrawEtfInflowPage(g, content); break;
	case PAGE_MAINFLOW: DrawMainFlowPage(g, content); break;
	case PAGE_TREND: DrawTrendPage(g, content); break;
	case PAGE_ETF_RANK: DrawEtfRankPage(g, content); break;
	}
}

void CMarketCenterWnd::DrawPageTitle(Gdiplus::Graphics& g, const CRect& content, const std::wstring& title, const std::wstring& sub)
{
	auto f13 = MkFont(13, true);
	CRect titleRc(content.left + g_data.DPI(16), content.top + g_data.DPI(8), content.right - g_data.DPI(140), content.top + g_data.DPI(30));
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

// ============ 页面1：基金气泡图 ============

void CMarketCenterWnd::RebuildBubbleLayout(const CRect& chartRc)
{
	m_bubble_nodes.clear();
	if (m_sectors_snapshot.empty() || chartRc.Width() < g_data.DPI(100) || chartRc.Height() < g_data.DPI(80))
		return;

	const int n = static_cast<int>(m_sectors_snapshot.size());
	const float S = static_cast<float>(g_data.GetDpi()) / 96.0f;
	float maxMag = 1.0f;
	for (const auto& s : m_sectors_snapshot)
		maxMag = max(maxMag, static_cast<float>(fabs(s.flow)));
	const float W = static_cast<float>(chartRc.Width());
	const float H = static_cast<float>(chartRc.Height());
	const float TOP_PAD = 26 * S, BOTTOM_PAD = 10 * S, SIDE_GAP = 6 * S, COLLIDE_GAP = 5 * S;
	const float midY = H / 2;

	struct LayoutNode
	{
		float x, y, tx, ty, vx, vy, r;
	};
	std::vector<LayoutNode> nodes(static_cast<size_t>(n));

	// demo drawBubble 的确定性装箱：黄金角散布 X + 规模越大沉降越深 + 碰撞分离迭代
	auto runLayout = [&](float mR) -> int {
		auto radOf = [&](float mag) { return 15 * S + sqrtf(mag / maxMag) * (mR - 15 * S); };
		for (int i = 0; i < n; i++)
		{
			const auto& s = m_sectors_snapshot[static_cast<size_t>(i)];
			float r = radOf(static_cast<float>(fabs(s.flow)));
			float sign = s.flow >= 0 ? -1.0f : 1.0f;    // 流入悬浮轴上方，流出沉降轴下方
			float bandH = sign < 0 ? midY - TOP_PAD : H - BOTTOM_PAD - midY;
			float avail = max(0.0f, bandH - r - SIDE_GAP);
			float dist = SIDE_GAP + r + avail * (0.18f + 0.72f * powf(static_cast<float>(fabs(s.flow)) / maxMag, 0.58f));
			float frac = fmodf(i * 0.61803398875f, 1.0f);
			nodes[static_cast<size_t>(i)] = { r + 14 * S + frac * max(1.0f, W - 28 * S - r * 2), midY + sign * dist,
				r + 14 * S + frac * max(1.0f, W - 28 * S - r * 2), midY + sign * dist, 0, 0, r };
		}
		for (int tick = 0; tick < 260; tick++)
		{
			float alpha = powf(0.978f, static_cast<float>(tick));
			for (auto& nd : nodes)
			{
				nd.vx = (nd.vx + (nd.tx - nd.x) * 0.10f * alpha) * 0.62f;
				nd.vy = (nd.vy + (nd.ty - nd.y) * 0.24f * alpha) * 0.62f;
				nd.x += nd.vx;
				nd.y += nd.vy;
			}
			for (int a = 0; a < n; a++)
			{
				for (int b = a + 1; b < n; b++)
				{
					LayoutNode& na = nodes[static_cast<size_t>(a)];
					LayoutNode& nb = nodes[static_cast<size_t>(b)];
					float dx = nb.x - na.x, dy = nb.y - na.y;
					float rr = na.r + nb.r + COLLIDE_GAP;
					float d2 = dx * dx + dy * dy;
					if (d2 < rr * rr)
					{
						float d = sqrtf(d2);
						if (d < 0.01f) d = 0.01f;
						float push = (rr - d) / d * 0.5f;
						na.x -= dx * push; na.y -= dy * push;
						nb.x += dx * push; nb.y += dy * push;
					}
				}
			}
			for (auto& nd : nodes)
			{
				nd.x = clampf(nd.x, nd.r + 8 * S, W - nd.r - 8 * S);
				if (nd.y < midY)
					nd.y = clampf(nd.y, TOP_PAD + nd.r, midY - nd.r - 3 * S);
				else
					nd.y = clampf(nd.y, midY + nd.r + 3 * S, H - BOTTOM_PAD - nd.r);
			}
		}
		int residual = 0;
		for (int a = 0; a < n; a++)
		{
			for (int b = a + 1; b < n; b++)
			{
				float dx = nodes[static_cast<size_t>(b)].x - nodes[static_cast<size_t>(a)].x;
				float dy = nodes[static_cast<size_t>(b)].y - nodes[static_cast<size_t>(a)].y;
				float rr = nodes[static_cast<size_t>(a)].r + nodes[static_cast<size_t>(b)].r + COLLIDE_GAP;
				if (dx * dx + dy * dy < (rr - 1.5f * S) * (rr - 1.5f * S))
					residual++;
			}
		}
		return residual;
	};

	float mR = min(min(54.0f * S, W / 6.2f), H / 7.4f);
	for (; mR >= 24.0f * S; mR -= 4.0f * S)
	{
		int residual = runLayout(mR);
		if (residual == 0)
			break;
	}

	m_bubble_nodes.resize(static_cast<size_t>(n));
	for (int i = 0; i < n; i++)
	{
		m_bubble_nodes[static_cast<size_t>(i)].sectorIdx = i;
		m_bubble_nodes[static_cast<size_t>(i)].x = chartRc.left + nodes[static_cast<size_t>(i)].x;
		m_bubble_nodes[static_cast<size_t>(i)].y = chartRc.top + nodes[static_cast<size_t>(i)].y;
		m_bubble_nodes[static_cast<size_t>(i)].r = nodes[static_cast<size_t>(i)].r;
	}
	m_bubble_layout_dirty = false;
}

void CMarketCenterWnd::DrawBubblePage(Gdiplus::Graphics& g, const CRect& rc)
{
	DrawPageTitle(g, rc, L"公开基金气泡图", L"圆大小 = 主力净流入 · 红 = 流入 · 绿 = 流出");

	// 无数据：提示 + 由 RequestData 拉取
	if (m_sectors_snapshot.empty())
	{
		auto f12 = MkFont(12);
		DrawStrMid(g, m_sectors_snapshot_time == 0 ? L"正在获取行业板块资金流…" : L"暂无数据", f12.get(), rc, MC_TEXT_DIM);
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

	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(34), rc.right - g_data.DPI(16), rc.top + g_data.DPI(34) + g_data.DPI(54));
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
		DrawStrMid(g, cells[i].label, f11.get(), CRect(cell.left, cell.top + g_data.DPI(8), cell.right, cell.top + g_data.DPI(24)), MC_TEXT_SUB);
		DrawStrMid(g, cells[i].value, f15b.get(), CRect(cell.left, cell.top + g_data.DPI(26), cell.right, cell.bottom - g_data.DPI(6)), cells[i].color);
	}

	// 主区域：气泡图 + 右侧详情
	CRect bodyRc(statsRc.left, statsRc.bottom + g_data.DPI(8), statsRc.right, rc.bottom - g_data.DPI(10));
	const int detailW = g_data.DPI(150);
	m_bubble_detail_rect = CRect(bodyRc.right - detailW, bodyRc.top, bodyRc.right, bodyRc.bottom);
	m_bubble_chart_rect = CRect(bodyRc.left, bodyRc.top, m_bubble_detail_rect.left - g_data.DPI(8), bodyRc.bottom);

	// 中轴分隔线
	Gdiplus::Pen midPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&midPen, Gdiplus::REAL(m_bubble_chart_rect.left + g_data.DPI(12)), Gdiplus::REAL(m_bubble_chart_rect.CenterPoint().y),
		Gdiplus::REAL(m_bubble_chart_rect.right - g_data.DPI(12)), Gdiplus::REAL(m_bubble_chart_rect.CenterPoint().y));

	// 气泡布局（数据或尺寸变化时重排）
	if (m_bubble_layout_dirty)
		RebuildBubbleLayout(m_bubble_chart_rect);

	float maxMag = 1.0f;
	for (const auto& s : m_sectors_snapshot)
		maxMag = max(maxMag, static_cast<float>(fabs(s.flow)));

	for (const auto& node : m_bubble_nodes)
	{
		const auto& s = m_sectors_snapshot[static_cast<size_t>(node.sectorIdx)];
		bool selected = (node.sectorIdx == m_selected_sector);
		bool hovered = (node.sectorIdx == m_hover_bubble);
		// 按规模做颜色插值：小泡暗、大泡亮
		float mag = min(1.0f, sqrtf(static_cast<float>(fabs(s.flow)) / maxMag));
		float t = 0.38f + mag * 0.62f;
		float base[3], bright[3];
		if (s.flow >= 0)
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
		Gdiplus::SolidBrush fillBrush(Gdiplus::Color(235, r, gg, b));
		g.FillEllipse(&fillBrush, node.x - node.r, node.y - node.r, node.r * 2, node.r * 2);
		Gdiplus::Pen strokePen(Gdi(s.flow >= 0 ? RGB(255, 157, 146) : RGB(124, 240, 185)), selected || hovered ? 2.4f : 1.2f);
		g.DrawEllipse(&strokePen, node.x - node.r, node.y - node.r, node.r * 2, node.r * 2);

		// 文字：半径足够时两行（名称 + 净流入），否则仅名称
		int fontSize = static_cast<int>(clampf(node.r * 0.24f / (g_data.GetDpi() / 96.0f), 11.0f, 14.0f));
		auto fTxt = MkFont(fontSize, true);
		CRect txtRc(static_cast<int>(node.x - node.r), static_cast<int>(node.y - node.r), static_cast<int>(node.x + node.r), static_cast<int>(node.y + node.r));
		if (node.r >= 28 * (g_data.GetDpi() / 96.0f))
		{
			auto fVal = MkFont(max(10, fontSize - 2));
			DrawStrMid(g, s.name, fTxt.get(), CRect(txtRc.left, txtRc.top, txtRc.right, txtRc.top + txtRc.Height() / 2), RGB(255, 255, 255));
			DrawStrMid(g, FormatYi(s.flow), fVal.get(), CRect(txtRc.left, txtRc.top + txtRc.Height() / 2 - g_data.DPI(2), txtRc.right, txtRc.bottom + g_data.DPI(2)), RGB(255, 255, 255));
		}
		else
		{
			DrawStrMid(g, s.name, fTxt.get(), txtRc, RGB(255, 255, 255));
		}
	}

	// 右侧详情栏
	FillCard(g, m_bubble_detail_rect);
	const int P = g_data.DPI(9);
	CRect dc1(m_bubble_detail_rect.left + P, m_bubble_detail_rect.top + P, m_bubble_detail_rect.right - P, m_bubble_detail_rect.bottom - P);
	DrawStr(g, L"板块详情", f10.get(), dc1, MC_TEXT_SUB);
	CRect rcName(dc1.left, dc1.top + g_data.DPI(16), dc1.right, dc1.top + g_data.DPI(36));
	CRect rcFlow(dc1.left, rcName.bottom + g_data.DPI(2), dc1.right, rcName.bottom + g_data.DPI(26));
	if (m_selected_sector >= 0 && m_selected_sector < static_cast<int>(m_sectors_snapshot.size()))
	{
		const auto& s = m_sectors_snapshot[static_cast<size_t>(m_selected_sector)];
		DrawStr(g, s.name, f14b.get(), rcName, MC_TEXT);
		DrawStr(g, FormatYi(s.flow), f16b.get(), rcFlow, UpDownColor(s.flow));
		DrawStr(g, std::wstring(L"数据时间 ") + (m_clock_time.empty() ? L"--" : m_clock_time.substr(0, 5)), f10.get(),
			CRect(dc1.left, rcFlow.bottom, dc1.right, rcFlow.bottom + g_data.DPI(14)), MC_TEXT_DIM);
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
		int rowH = g_data.DPI(22);
		for (int i = 0; i < 6; i++)
		{
			CRect rRow(dc1.left, rowTop, dc1.right, rowTop + rowH);
			rowTop += rowH;
			Gdiplus::Pen rowPen(Gdi(MC_BORDER), 1.0f);
			g.DrawLine(&rowPen, Gdiplus::REAL(rRow.left), Gdiplus::REAL(rRow.top), Gdiplus::REAL(rRow.right), Gdiplus::REAL(rRow.top));
			DrawStr(g, rows[i].label, f10.get(), CRect(rRow.left, rRow.top, rRow.CenterPoint().x, rRow.bottom), MC_TEXT_SUB);
			DrawStr(g, rows[i].value, f10.get(), CRect(rRow.CenterPoint().x, rRow.top, rRow.right, rRow.bottom), rows[i].color, 255, Gdiplus::StringAlignmentFar);
		}
	}
	// 图例
	CRect legendRc(dc1.left, dc1.bottom - g_data.DPI(20), dc1.right, dc1.bottom);
	DrawStrMid(g, L"● 流入", f10.get(), CRect(legendRc.left, legendRc.top, legendRc.left + legendRc.Width() / 2, legendRc.bottom), MC_UP);
	DrawStrMid(g, L"● 流出", f10.get(), CRect(legendRc.left + legendRc.Width() / 2, legendRc.top, legendRc.right, legendRc.bottom), MC_DOWN);
}

// ============ 页面2：ETF申购净流入 ============

void CMarketCenterWnd::DrawEtfInflowPage(Gdiplus::Graphics& g, const CRect& rc)
{
	DrawPageTitle(g, rc, m_inflow_out ? L"实时ETF申购净流出" : L"实时ETF申购净流入", L"口径：ETF 场内主力资金净流入");

	if (m_etfs_snapshot.empty())
	{
		auto f12 = MkFont(12);
		DrawStrMid(g, m_etfs_snapshot_time == 0 ? L"正在获取ETF数据…" : L"暂无数据", f12.get(), rc, MC_TEXT_DIM);
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

	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(34), rc.right - g_data.DPI(16), rc.top + g_data.DPI(34) + g_data.DPI(54));
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
	CRect chartRc(headingRc.left, headingRc.bottom + g_data.DPI(4), headingRc.right, rc.bottom - g_data.DPI(10));
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

	const int nameW = g_data.DPI(110);
	const int valW = g_data.DPI(64);
	const int barH = g_data.DPI(18);
	const int slot = chartRc.Height() / static_cast<int>(order.size());
	const int plotLeft = chartRc.left + nameW;
	const int plotRight = chartRc.right - valW;
	double lo = 0, hi = 0;
	for (int idx : order)
	{
		double v = m_theme_inflow[static_cast<size_t>(idx)].inflow / 1e8;
		lo = min(lo, v);
		hi = max(hi, v);
	}
	double range = hi - lo;
	if (range <= 0) range = 1;
	// 零轴位置
	int xZero = plotLeft + static_cast<int>((0 - lo) / range * (plotRight - plotLeft));
	Gdiplus::Pen zeroPen(Gdi(MC_BORDER), 1.0f);
	g.DrawLine(&zeroPen, Gdiplus::REAL(xZero), Gdiplus::REAL(chartRc.top), Gdiplus::REAL(xZero), Gdiplus::REAL(chartRc.bottom));

	for (size_t k = 0; k < order.size(); k++)
	{
		int idx = order[k];
		const auto& th = m_theme_inflow[static_cast<size_t>(idx)];
		int yTop = chartRc.top + static_cast<int>(k) * slot + (slot - barH) / 2;
		CRect nameRc(chartRc.left, yTop - g_data.DPI(2), plotLeft - g_data.DPI(8), yTop + barH + g_data.DPI(2));
		DrawStr(g, th.theme + L"ETF", f11.get(), nameRc, MC_TEXT_SUB, 255, Gdiplus::StringAlignmentFar);

		double vYi = th.inflow / 1e8;
		int x0 = xZero, x1 = xZero;
		if (vYi >= 0)
			x1 = xZero + static_cast<int>(vYi / range * (plotRight - plotLeft));
		else
			x0 = xZero + static_cast<int>(vYi / range * (plotRight - plotLeft));
		CRect barRc(min(x0, x1), yTop, max(x0, x1), yTop + barH);
		if (barRc.Width() < g_data.DPI(2))
			barRc.right = barRc.left + g_data.DPI(2);
		FillRounded(g, barRc, th.inflow >= 0 ? MC_UP : MC_DOWN, g_data.DPI(2));
		bool hovered = (static_cast<int>(k) == m_hover_inflow_bar);
		if (hovered)
		{
			Gdiplus::Pen hoverPen(Gdi(MC_TEXT, 120), 1.2f);
			g.DrawRectangle(&hoverPen, Gdiplus::REAL(barRc.left), Gdiplus::REAL(barRc.top), Gdiplus::REAL(barRc.Width()), Gdiplus::REAL(barRc.Height()));
		}
		// 数值标签
		wchar_t vbuf[32];
		swprintf_s(vbuf, L"%s%.2f", vYi >= 0 ? L"+" : L"", vYi);
		CRect valRc(vYi >= 0 ? barRc.right + g_data.DPI(6) : barRc.left - valW - g_data.DPI(6), yTop,
			vYi >= 0 ? barRc.right + g_data.DPI(6) + valW : barRc.left - g_data.DPI(6), yTop + barH);
		DrawStr(g, vbuf, f10.get(), valRc, MC_TEXT_SUB, 255, vYi >= 0 ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentFar);
		m_inflow_bars.push_back({ barRc, idx });
	}

	// 主题ETF浮层
	if (m_theme_panel_open)
		DrawThemePanel(g, chartRc);
}

void CMarketCenterWnd::DrawThemePanel(Gdiplus::Graphics& g, const CRect& chartRc)
{
	auto f10 = MkFont(10);
	auto f10b = MkFont(10, true);
	auto f11 = MkFont(11);
	auto f11b = MkFont(11, true);
	auto f12b = MkFont(12, true);

	const int panelW = g_data.DPI(300);
	m_theme_panel_rect = CRect(chartRc.right - panelW, chartRc.top, chartRc.right, chartRc.bottom);
	FillCard(g, m_theme_panel_rect, MC_CARD);
	// 简易阴影：顶部一条高亮边
	Gdiplus::Pen topPen(Gdi(MC_TEXT, 18), 1.0f);
	g.DrawLine(&topPen, Gdiplus::REAL(m_theme_panel_rect.left), Gdiplus::REAL(m_theme_panel_rect.top), Gdiplus::REAL(m_theme_panel_rect.right), Gdiplus::REAL(m_theme_panel_rect.top));

	const int P = g_data.DPI(10);
	CRect headRc(m_theme_panel_rect.left + P, m_theme_panel_rect.top + P, m_theme_panel_rect.right - P, m_theme_panel_rect.top + P + g_data.DPI(20));
	DrawStr(g, m_theme_panel_title, f12b.get(), headRc, MC_TEXT);
	m_theme_close_rect = CRect(headRc.right - g_data.DPI(16), headRc.top, headRc.right, headRc.bottom);
	DrawStrMid(g, L"✕", f11.get(), m_theme_close_rect, m_theme_close_rect.PtInRect(m_mouse_pos) ? MC_TEXT : MC_TEXT_DIM);

	// 表头
	CRect headRow(headRc.left, headRc.bottom + g_data.DPI(4), headRc.right, headRc.bottom + g_data.DPI(4) + g_data.DPI(18));
	const int colW[4] = { 40, 24, 18, 18 };
	int colWidths[4];
	int totalW = headRow.Width();
	for (int i = 0; i < 4; i++)
		colWidths[i] = totalW * colW[i] / 100;
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
	const int rowH = g_data.DPI(22);
	CRect listRc(headRow.left, headRow.bottom + g_data.DPI(2), headRow.right, m_theme_panel_rect.bottom - P);
	int maxVisible = max(1, listRc.Height() / rowH);
	m_theme_panel_scroll_max = max(0, static_cast<int>(m_theme_row_etfs.size()) - maxVisible);
	m_theme_panel_scroll = min(m_theme_panel_scroll, m_theme_panel_scroll_max);
	g.SetClip(Gdiplus::Rect(Gdiplus::REAL(listRc.left), Gdiplus::REAL(listRc.top), Gdiplus::REAL(listRc.Width()), Gdiplus::REAL(listRc.Height())));
	int row = 0;
	for (int etfIdx : m_theme_row_etfs)
	{
		int drawIdx = row - m_theme_panel_scroll;
		row++;
		if (drawIdx < 0) continue;
		if (drawIdx >= maxVisible) break;
		const auto& e = m_etfs_snapshot[static_cast<size_t>(etfIdx)];
		CRect rRow(listRc.left, listRc.top + drawIdx * rowH, listRc.right, listRc.top + (drawIdx + 1) * rowH);
		wchar_t priceBuf[24], pctBuf[24];
		swprintf_s(priceBuf, L"%.3f", e.price);
		swprintf_s(pctBuf, L"%s%.2f%%", e.pct >= 0 ? L"+" : L"", e.pct);
		DrawStr(g, e.name, f11.get(), CRect(rRow.left, rRow.top, rRow.left + colWidths[0] - g_data.DPI(4), rRow.bottom), MC_TEXT);
		DrawStrMid(g, e.code, f10.get(), CRect(rRow.left + colWidths[0], rRow.top, rRow.left + colWidths[0] + colWidths[1], rRow.bottom), MC_TEXT_SUB);
		DrawStrMid(g, priceBuf, f10.get(), CRect(rRow.left + colWidths[0] + colWidths[1], rRow.top, rRow.left + colWidths[0] + colWidths[1] + colWidths[2], rRow.bottom), MC_TEXT_SUB);
		DrawStrMid(g, pctBuf, f10b.get(), CRect(rRow.left + colWidths[0] + colWidths[1] + colWidths[2], rRow.top, rRow.right, rRow.bottom), UpDownColor(e.pct));
	}
	g.ResetClip();
}

// ============ 页面3：主力资金 ============

void CMarketCenterWnd::DrawMainFlowPage(Gdiplus::Graphics& g, const CRect& rc)
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
	{
		CMarketCenterData& mc = CMarketCenterData::Instance();
		std::lock_guard<std::mutex> lock(mc.m_mutex);
		sh = mc.m_fflow_sh;
		sz = mc.m_fflow_sz;
		idx = mc.m_index_trend;
		etfCurve = mc.m_etf_flow_curve;
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
	bool hasAny = (!isnan(shNow) || !isnan(szNow) || !isnan(idxNow));
	if (!hasAny)
	{
		auto f12 = MkFont(12);
		DrawStrMid(g, L"正在获取沪深主力资金分时…", f12.get(), rc, MC_TEXT_DIM);
		// 统计条仍然绘制（无数据状态）
		shArr.clear();
	}

	// 统计卡兼图例（可点击开关曲线）
	CRect statsRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(34), rc.right - g_data.DPI(16), rc.top + g_data.DPI(34) + g_data.DPI(54));
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
	CRect chartRc(statsRc.left, statsRc.bottom + g_data.DPI(8), statsRc.right, rc.bottom - g_data.DPI(8));
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

	// 曲线
	auto drawSeries = [&](const std::vector<double>& arr, COLORREF color, float width, bool useIdxAxis) {
		Gdiplus::Pen pen(Gdi(color), width);
		std::vector<Gdiplus::PointF> pts;
		for (int i = 0; i < AXIS_N; i++)
		{
			double v = arr[static_cast<size_t>(i)];
			if (isnan(v))
			{
				if (pts.size() >= 2)
					g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
				pts.clear();
				continue;
			}
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

void CMarketCenterWnd::DrawTrendPage(Gdiplus::Graphics& g, const CRect& rc)
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
	std::vector<MC::TrendSample> curve;
	{
		CMarketCenterData& mc = CMarketCenterData::Instance();
		std::lock_guard<std::mutex> lock(mc.m_mutex);
		dist = mc.m_dist;
		turnoverToday = mc.m_turnover_today;
		turnoverYday = mc.m_turnover_yesterday;
		curve = mc.m_trend_curve;
	}

	long long upCnt = dist.UpCount(), downCnt = dist.DownCount(), flatCnt = dist.FlatCount();

	// 9格统计卡（涨跌5格 + 成交量4格）
	CRect blockRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(34), rc.right - g_data.DPI(16), rc.top + g_data.DPI(34) + g_data.DPI(58));
	FillCard(g, blockRc);
	m_trend_stat_rects.clear();
	// 当日进度 → 预测全天
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
		{ L"预测全天", forecastOk ? (swprintf_s(numBuf, L"%.0f亿", forecast / 1e8), numBuf) : std::wstring(L"--"), MC_TEXT },
	};
	const int cellW9 = blockRc.Width() / 9;
	for (int i = 0; i < 9; i++)
	{
		CRect cell(blockRc.left + i * cellW9, blockRc.top, blockRc.left + (i + 1) * cellW9, blockRc.bottom);
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
		DrawStrMid(g, L"正在获取涨跌分布…", f12.get(), CRect(rc.left, blockRc.bottom, rc.right, rc.bottom), MC_TEXT_DIM);
		return;
	}

	// ===== 涨跌分布（13桶）=====
	CRect bodyRc(blockRc.left, blockRc.bottom + g_data.DPI(8), blockRc.right, rc.bottom - g_data.DPI(8));
	int chartH = (bodyRc.Height() - g_data.DPI(8)) * 100 / 215;   // flex 1 : 1.15
	CRect distRc(bodyRc.left, bodyRc.top, bodyRc.right, bodyRc.top + chartH);
	CRect lineRc(bodyRc.left, distRc.bottom + g_data.DPI(8), bodyRc.right, bodyRc.bottom);

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

	// ===== 涨跌家数分时 =====
	DrawStr(g, L"涨跌家数分时", f12b.get(), CRect(lineRc.left + g_data.DPI(6), lineRc.top, lineRc.left + g_data.DPI(110), lineRc.top + g_data.DPI(18)), MC_TEXT);
	// 图例（右上）
	DrawStr(g, L"上涨家数", f10.get(), CRect(lineRc.right - g_data.DPI(150), lineRc.top + g_data.DPI(2), lineRc.right - g_data.DPI(96), lineRc.top + g_data.DPI(18)), MC_UP);
	DrawStr(g, L"下跌家数", f10.get(), CRect(lineRc.right - g_data.DPI(90), lineRc.top + g_data.DPI(2), lineRc.right - g_data.DPI(36), lineRc.top + g_data.DPI(18)), MC_DOWN);

	CRect linePlot(lineRc.left + g_data.DPI(44), lineRc.top + g_data.DPI(24), lineRc.right - g_data.DPI(8), lineRc.bottom - g_data.DPI(22));
	const int AXIS_N = 241;
	if (!curve.empty() && linePlot.Width() > g_data.DPI(100))
	{
		std::vector<double> upArr(static_cast<size_t>(AXIS_N), NAN), downArr(static_cast<size_t>(AXIS_N), NAN);
		for (auto& s : curve)
		{
			int i = CMarketCenterData::TimeIndex(s.time);
			if (i >= 0)
			{
				upArr[static_cast<size_t>(i)] = static_cast<double>(s.up);
				downArr[static_cast<size_t>(i)] = static_cast<double>(s.down);
			}
		}
		double maxV = 1;
		for (double v : upArr) if (!isnan(v)) maxV = max(maxV, v);
		for (double v : downArr) if (!isnan(v)) maxV = max(maxV, v);
		maxV *= 1.08;
		auto fy = [&](double v) -> float { return static_cast<float>(linePlot.bottom - v / maxV * linePlot.Height()); };
		auto fx = [&](int i) -> float { return linePlot.left + static_cast<float>(i) * linePlot.Width() / (AXIS_N - 1); };
		// 网格
		{
			double step = NiceStep(maxV);
			for (double tv = 0; tv <= maxV; tv += step)
			{
				float y = fy(tv);
				Gdiplus::Pen gridPen(Gdi(MC_GRID), 1.0f);
				g.DrawLine(&gridPen, Gdiplus::REAL(linePlot.left), y, Gdiplus::REAL(linePlot.right), y);
				DrawStr(g, FormatAxisNum(tv, step), f10.get(), CRect(lineRc.left + g_data.DPI(2), static_cast<int>(y) - g_data.DPI(8), linePlot.left - g_data.DPI(4), static_cast<int>(y) + g_data.DPI(8)), MC_TEXT_SUB, 255, Gdiplus::StringAlignmentFar);
			}
			const auto& axisL = CMarketCenterData::TimeAxis();
			const int marks[5] = { 0, 60, 120, 180, 240 };
			for (int k = 0; k < 5; k++)
			{
				int mi = marks[k];
				if (mi >= static_cast<int>(axisL.size()))
					continue;
				DrawStrMid(g, axisL[static_cast<size_t>(mi)], f10.get(), CRect(static_cast<int>(fx(mi)) - g_data.DPI(24), linePlot.bottom + g_data.DPI(4), static_cast<int>(fx(mi)) + g_data.DPI(24), linePlot.bottom + g_data.DPI(20)), MC_TEXT_SUB);
			}
		}
		auto drawLine = [&](const std::vector<double>& arr, COLORREF color) {
			Gdiplus::Pen pen(Gdi(color), 1.6f);
			std::vector<Gdiplus::PointF> pts;
			for (int i = 0; i < AXIS_N; i++)
			{
				double v = arr[static_cast<size_t>(i)];
				if (isnan(v))
				{
					if (pts.size() >= 2)
						g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
					pts.clear();
					continue;
				}
				pts.push_back(Gdiplus::PointF(fx(i), fy(v)));
			}
			if (pts.size() >= 2)
				g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
			};
		drawLine(upArr, MC_UP);
		drawLine(downArr, MC_DOWN);
	}
	else
	{
		DrawStrMid(g, L"开窗后按分钟自积累（约1分钟出一个点）", f10.get(), linePlot, MC_TEXT_DIM);
	}
}

// ============ 页面5：ETF涨跌榜 ============

std::vector<int> CMarketCenterWnd::SortedRankList() const
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

void CMarketCenterWnd::DrawEtfRankPage(Gdiplus::Graphics& g, const CRect& rc)
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
		DrawStrMid(g, m_etfs_snapshot_time == 0 ? L"正在获取ETF数据…" : L"暂无数据", f12.get(), rc, MC_TEXT_DIM);
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

	CRect toolbarRc(rc.left + g_data.DPI(16), rc.top + g_data.DPI(34), rc.right - g_data.DPI(16), rc.top + g_data.DPI(34) + g_data.DPI(52));
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
	CRect tableRc(toolbarRc.left, toolbarRc.bottom + g_data.DPI(8), toolbarRc.right, rc.bottom - g_data.DPI(10));
	m_rank_table_rect = tableRc;
	const int headerH = g_data.DPI(28);
	const int rowH = g_data.DPI(28);
	CRect headerRc(tableRc.left, tableRc.top, tableRc.right, tableRc.top + headerH);
	CRect listRc(tableRc.left, headerRc.bottom, tableRc.right, tableRc.bottom);

	// 列布局：名称/代码 左对齐弹性，其余居中
	int fixed[5] = { 90, 70, 80, 96, 110 };   // 行业/现价/涨跌幅/成交额/主力净流入 (96dpi)
	int fixedSum = 0;
	for (int w : fixed) fixedSum += w;
	float S = static_cast<float>(g_data.GetDpi()) / 96.0f;
	int nameW = max(g_data.DPI(180), tableRc.Width() - static_cast<int>(fixedSum * S));
	int colX[6];
	colX[0] = tableRc.left + g_data.DPI(8);
	colX[1] = colX[0] + nameW;
	for (int i = 2; i <= 5; i++)
		colX[i] = colX[i - 1] + static_cast<int>(fixed[i - 2] * S);
	int colEnd = tableRc.right - g_data.DPI(6);

	const wchar_t* headers[6] = { L"名称 / 代码", L"行业", L"现价", L"涨跌幅", L"成交额(亿)", L"主力净流入(亿)" };
	m_rank_cols.clear();
	for (int i = 0; i < 6; i++)
	{
		CRect colRc(colX[i], headerRc.top, i == 0 ? colX[1] : (i == 5 ? colEnd : colX[i + 1]), headerRc.bottom);
		RankCol rc2;
		rc2.rect = colRc;
		rc2.key = i;
		rc2.sortedUp = (m_rank_sort_key == i && m_rank_sort_dir > 0);
		m_rank_cols.push_back(rc2);
		// 表头背景 + hover
		if (m_hover_rank_header == i)
			FillRounded(g, colRc, MC_TEXT, 0, 12);
		CRect lblRc = colRc;
		std::wstring label = headers[i];
		if (m_rank_sort_key == i)
			label += (m_rank_sort_dir > 0 ? L" ▲" : L" ▼");
		DrawStr(g, label, f11.get(), lblRc, m_rank_sort_key == i ? MC_ACCENT : MC_TEXT_DIM, 255, i == 0 ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
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

		// 名称 + 代码
		DrawStr(g, e.name, f11b.get(), CRect(colX[0], rowRc.top, colX[0] + nameW - g_data.DPI(70), rowRc.bottom), MC_TEXT);
		DrawStr(g, e.code, f10.get(), CRect(colX[0] + nameW - g_data.DPI(66), rowRc.top, colX[0] + nameW - g_data.DPI(4), rowRc.bottom), MC_TEXT_DIM);
		// 行业
		CRect themeRc(colX[1] + g_data.DPI(4), rowRc.top + g_data.DPI(5), colX[2] - g_data.DPI(4), rowRc.bottom - g_data.DPI(5));
		FillRounded(g, themeRc, MC_TEXT, static_cast<float>(themeRc.Height() / 2), 12);
		{
			Gdiplus::Pen tagPen(Gdi(MC_BORDER), 1.0f);
			g.DrawRectangle(&tagPen, Gdiplus::REAL(themeRc.left), Gdiplus::REAL(themeRc.top), Gdiplus::REAL(themeRc.Width()), Gdiplus::REAL(themeRc.Height()));
		}
		DrawStrMid(g, e.theme, f9.get(), themeRc, MC_TEXT_SUB);
		// 现价
		swprintf_s(buf, L"%.3f", e.price);
		DrawStrMid(g, buf, f11.get(), CRect(colX[2], rowRc.top, colX[3], rowRc.bottom), MC_TEXT);
		// 涨跌幅 pill
		CRect pillRc(colX[3] + g_data.DPI(8), rowRc.top + g_data.DPI(4), colX[4] - g_data.DPI(8), rowRc.bottom - g_data.DPI(4));
		FillRounded(g, pillRc, e.pct >= 0 ? MC_UP : MC_DOWN, static_cast<float>(pillRc.Height() / 2), 36);
		DrawStrMid(g, FormatPct(e.pct), f10b.get(), pillRc, e.pct >= 0 ? RGB(255, 173, 183) : RGB(148, 240, 200));
		// 成交额
		swprintf_s(buf, L"%.1f", e.amount / 1e8);
		DrawStrMid(g, buf, f11.get(), CRect(colX[4], rowRc.top, colX[5], rowRc.bottom), MC_TEXT);
		// 主力净流入
		swprintf_s(buf, L"%s%.2f", e.inflow >= 0 ? L"+" : L"-", fabs(e.inflow) / 1e8);
		DrawStrMid(g, buf, f11.get(), CRect(colX[5], rowRc.top, colEnd, rowRc.bottom), UpDownColor(e.inflow));
	}
	g.ResetClip();
}

// ============ 交互 ============

void CMarketCenterWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking_mouse)
	{
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, GetSafeHwnd(), 0 };
		if (TrackMouseEvent(&tme))
			m_tracking_mouse = true;
	}
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
			int hov = -1;
			for (int i = 0; i < static_cast<int>(m_bubble_nodes.size()); i++)
			{
				const auto& nd = m_bubble_nodes[static_cast<size_t>(i)];
				CRect rc(static_cast<int>(nd.x - nd.r), static_cast<int>(nd.y - nd.r), static_cast<int>(nd.x + nd.r), static_cast<int>(nd.y + nd.r));
				if (rc.PtInRect(point))
				{
					hov = nd.sectorIdx;
					break;
				}
			}
			if (hov != m_hover_bubble)
			{
				m_hover_bubble = hov;
				changed = true;
			}
			break;
		}
		case PAGE_ETF_INFLOW:
		{
			int hovBar = -1, hovCard = -1;
			for (int i = 0; i < static_cast<int>(m_inflow_stat_rects.size()); i++)
				if (i < 2 && m_inflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
					hovCard = i;
			for (int i = 0; i < static_cast<int>(m_inflow_bars.size()); i++)
				if (m_inflow_bars[static_cast<size_t>(i)].rect.PtInRect(point))
					hovBar = i;
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
				int relY = point.y - m_rank_table_rect.top - headerH + m_rank_scroll;
				if (relY >= 0)
				{
					int rowIdx = relY / rowH;
					if (rowIdx >= 0 && rowIdx < static_cast<int>(m_rank_row_etf.size()))
						hovRow = m_rank_scroll / rowH + rowIdx;
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

	if (changed)
		Invalidate(FALSE);
	CWnd::OnMouseMove(nFlags, point);
}

void CMarketCenterWnd::OnMouseLeave()
{
	m_tracking_mouse = false;
	if (m_hover_menu != -1 || m_hover_bubble != -1 || m_hover_inflow_bar != -1 ||
		m_hover_mainflow_card != -1 || m_hover_inflow_card != -1 || m_hover_dist_bar != -1 ||
		m_hover_rank_header != -1 || m_hover_rank_row != -1)
	{
		m_hover_menu = -1;
		m_hover_bubble = -1;
		m_hover_inflow_bar = -1;
		m_hover_mainflow_card = -1;
		m_hover_inflow_card = -1;
		m_hover_dist_bar = -1;
		m_hover_rank_header = -1;
		m_hover_rank_row = -1;
		Invalidate(FALSE);
	}
}

void CMarketCenterWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	// 菜单切换
	for (int i = 0; i < PAGE_COUNT; i++)
	{
		if (m_menu_item_rects[i].PtInRect(point))
		{
			SwitchPage(static_cast<McPage>(i));
			return;
		}
	}

	if (point.x < m_content_rect.left || m_clock_rect.PtInRect(point))
	{
		CWnd::OnLButtonDown(nFlags, point);
		return;
	}

	switch (m_page)
	{
	case PAGE_BUBBLE:
	{
		for (const auto& nd : m_bubble_nodes)
		{
			CRect rc(static_cast<int>(nd.x - nd.r), static_cast<int>(nd.y - nd.r), static_cast<int>(nd.x + nd.r), static_cast<int>(nd.y + nd.r));
			if (rc.PtInRect(point))
			{
				m_selected_sector = nd.sectorIdx;
				Invalidate(FALSE);
				return;
			}
		}
		break;
	}
	case PAGE_ETF_INFLOW:
	{
		if (m_theme_panel_open && m_theme_close_rect.PtInRect(point))
		{
			m_theme_panel_open = false;
			Invalidate(FALSE);
			return;
		}
		for (int i = 0; i < 2 && i < static_cast<int>(m_inflow_stat_rects.size()); i++)
		{
			if (m_inflow_stat_rects[static_cast<size_t>(i)].rect.PtInRect(point))
			{
				bool out = (i == 1);
				if (out != m_inflow_out)
				{
					m_inflow_out = out;
					Invalidate(FALSE);
				}
				return;
			}
		}
		for (const auto& bar : m_inflow_bars)
		{
			if (bar.rect.PtInRect(point))
			{
				// 打开主题浮层
				if (bar.themeIdx >= 0 && bar.themeIdx < static_cast<int>(m_theme_inflow.size()))
				{
					const std::wstring& theme = m_theme_inflow[static_cast<size_t>(bar.themeIdx)].theme;
					m_theme_row_etfs.clear();
					for (int i = 0; i < static_cast<int>(m_etfs_snapshot.size()); i++)
						if (m_etfs_snapshot[static_cast<size_t>(i)].theme == theme)
							m_theme_row_etfs.push_back(i);
					if (!m_theme_row_etfs.empty())
					{
						m_theme_panel_title = theme + L" · 共" + std::to_wstring(m_theme_row_etfs.size()) + L"只";
						m_theme_panel_scroll = 0;
						m_theme_panel_open = true;
						Invalidate(FALSE);
					}
				}
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
				Invalidate(FALSE);
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
				if (m_rank_sort_key == i)
					m_rank_sort_dir = -m_rank_sort_dir;
				else
				{
					m_rank_sort_key = i;
					m_rank_sort_dir = (i == 3) ? -1 : 1;    // 涨跌幅默认降序，其余升序
				}
				m_rank_scroll = 0;
				m_hover_rank_row = -1;
				Invalidate(FALSE);
				return;
			}
		}
		break;
	}
	default:
		break;
	}

	CWnd::OnLButtonDown(nFlags, point);
}

BOOL CMarketCenterWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CPoint point = pt;
	ScreenToClient(&point);
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
	if (scrolled)
		Invalidate(FALSE);
	return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

BOOL CMarketCenterWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT)
	{
		CPoint pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);
		bool hand = false;
		for (int i = 0; i < PAGE_COUNT && !hand; i++)
			hand = m_menu_item_rects[i].PtInRect(pt) != FALSE;
		if (!hand)
		{
			switch (m_page)
			{
			case PAGE_BUBBLE:
				for (const auto& nd : m_bubble_nodes)
				{
					CRect rc(static_cast<int>(nd.x - nd.r), static_cast<int>(nd.y - nd.r), static_cast<int>(nd.x + nd.r), static_cast<int>(nd.y + nd.r));
					if (rc.PtInRect(pt)) { hand = true; break; }
				}
				break;
			case PAGE_ETF_INFLOW:
				hand = (m_theme_panel_open && m_theme_close_rect.PtInRect(pt)) ||
					(m_inflow_stat_rects.size() > 1 && (m_inflow_stat_rects[0].rect.PtInRect(pt) || m_inflow_stat_rects[1].rect.PtInRect(pt)));
				for (const auto& bar : m_inflow_bars)
					if (bar.rect.PtInRect(pt)) { hand = true; break; }
				break;
			case PAGE_MAINFLOW:
				for (const auto& c : m_mainflow_stat_rects)
					if (c.rect.PtInRect(pt)) { hand = true; break; }
				break;
			case PAGE_ETF_RANK:
				for (const auto& c : m_rank_cols)
					if (c.rect.PtInRect(pt)) { hand = true; break; }
				break;
			default:
				break;
			}
		}
		if (hand)
		{
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
	}
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}
