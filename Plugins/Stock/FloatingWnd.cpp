#include "pch.h"
#include "FloatingWnd.h"
#include <afxinet.h>
#include <memory>
#include <map>
#include "Common.h"
#include "DataManager.h"
#include <Stock.h>
#include <algorithm>
#include <set>
#include <cstdlib>
#include "OptionsDlg.h"
#include "TradeRecordDialog.h"
#include "StockFetchThread.h"
#include "SmartSignalTestDlg.h"
#include "ChartColors.h"
#include "StockListPanel.h"
#include "StockFont.h"
#include "CallAuctionChart.h"
#include "ManagerDialog.h"  // CDarkPopupMenu：顶栏“更多分组”暗色下拉菜单

// 大盘指数优先级列表与 GetStockPriority 已移至 Stock.h/Stock.cpp，供各模块共享

// 颜色常量已移至 ChartColors.h，供各图表模块共享

void DrawPricePointLabel(CDC& memDC, int pointX, int pointY, int chartLeft, int chartTop, int chartWidth, int chartHeight,
	STOCK::Price price, bool isHigh, COLORREF color)
{
	CString label = CCommon::FormatFloat(price);
	CSize labelSize = memDC.GetTextExtent(label);
	const int gap = g_data.RDPI(4);
	const int arrowGap = g_data.RDPI(10);
	const int chartRight = chartLeft + chartWidth;
	const int chartBottom = chartTop + chartHeight;

	int labelX = pointX - labelSize.cx / 2;
	int labelY = isHigh ? pointY - labelSize.cy - arrowGap : pointY + arrowGap;
	bool useSideLabel = labelX < chartLeft || labelX + labelSize.cx > chartRight;

	if (useSideLabel)
	{
		if (pointX < chartLeft + chartWidth / 2)
		{
			label.Format(_T("\u2190%s"), CCommon::FormatFloat(price));
			labelSize = memDC.GetTextExtent(label);
			labelX = pointX + gap;
		}
		else
		{
			label.Format(_T("%s\u2192"), CCommon::FormatFloat(price));
			labelSize = memDC.GetTextExtent(label);
			labelX = pointX - labelSize.cx - gap;
		}
		labelY = pointY - labelSize.cy / 2;
		labelX = max(chartLeft, min(labelX, chartRight - labelSize.cx));
		labelY = max(chartTop, min(labelY, chartBottom - labelSize.cy));
		memDC.SetTextColor(color);
		memDC.TextOut(labelX, labelY, label);
		return;
	}

	labelY = max(chartTop, min(labelY, chartBottom - labelSize.cy));
	memDC.SetTextColor(color);
	memDC.TextOut(labelX, labelY, label);

	int fromX = labelX + labelSize.cx / 2;
	int fromY = isHigh ? labelY + labelSize.cy : labelY;
	if (abs(pointY - fromY) > g_data.RDPI(2))
	{
		CPen pen(PS_SOLID, 1, color);
		CPen* pOldPen = memDC.SelectObject(&pen);
		memDC.MoveTo(fromX, fromY);
		memDC.LineTo(pointX, pointY);

		int dir = (pointY >= fromY) ? 1 : -1;
		int arrowLen = g_data.RDPI(4);
		int arrowHalf = g_data.RDPI(3);
		memDC.MoveTo(pointX, pointY);
		memDC.LineTo(pointX - arrowHalf, pointY - dir * arrowLen);
		memDC.MoveTo(pointX, pointY);
		memDC.LineTo(pointX + arrowHalf, pointY - dir * arrowLen);
		memDC.SelectObject(pOldPen);
	}
}

#define ORDER_BOOK_WIDTH          g_data.RDPI(168)     // 右侧信息面板宽度

enum {
	IDC_TIMELINE_BTN = 1001,
	IDC_KLINE_BTN = 1002,
	IDC_CLOSE_BTN = 1005,
	IDM_CLOSE_WINDOW = 1006,
	IDC_MA_BTN = 1007,
	IDC_WEEK_KLINE_BTN = 1008,
	IDC_BOLL_BTN = 1009,
	IDC_INDICATOR_MACD_BTN = 1012,
	IDC_INDICATOR_KDJ_BTN = 1013,
	IDC_MONTH_KLINE_BTN = 1014,
	IDC_INDICATOR_WR_BTN = 1015,
	IDC_INDICATOR_RSI_BTN = 1016,
	IDC_INDICATOR_MACD_SIGNAL_BTN = 1017,
	IDC_CHIP_PEAK_BTN = 1018,
	IDC_ORDER_BOOK_BTN = 1019,
	IDC_EXPAND_BTN = 1020,
	IDC_TOGGLE_STOCK_LIST_BTN = 1021,
	IDC_CALL_AUCTION_BTN = 1022,
	IDC_REFRESH_TIMER = 1023
};

BEGIN_MESSAGE_MAP(CFloatingWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_RBUTTONDOWN()
	ON_WM_CREATE()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSELEAVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_CTLCOLOR()
	ON_WM_DRAWITEM()
	ON_MESSAGE((WM_USER + 100), OnUpdateStatus)
	ON_MESSAGE((WM_USER + 102), OnShowEditDialog)
	ON_MESSAGE((WM_USER + 103), OnShowAddDialog)
	ON_MESSAGE((WM_USER + 104), OnShowTradeDialog)
	ON_MESSAGE(IDM_CLOSE_WINDOW, OnCloseWindow)
	ON_BN_CLICKED(IDC_CALL_AUCTION_BTN, &CFloatingWnd::OnBnClickedCallAuctionBtn)
	ON_BN_CLICKED(IDC_TIMELINE_BTN, &CFloatingWnd::OnBnClickedTimeLineBtn)
	ON_BN_CLICKED(IDC_KLINE_BTN, &CFloatingWnd::OnBnClickedKLineBtn)
	ON_BN_CLICKED(IDC_WEEK_KLINE_BTN, &CFloatingWnd::OnBnClickedWeekKLineBtn)
	ON_BN_CLICKED(IDC_MONTH_KLINE_BTN, &CFloatingWnd::OnBnClickedMonthKLineBtn)
	ON_BN_CLICKED(IDC_CLOSE_BTN, &CFloatingWnd::OnBnClickedCloseBtn)
	ON_BN_CLICKED(IDC_MA_BTN, &CFloatingWnd::OnBnClickedMABtn)
	ON_BN_CLICKED(IDC_BOLL_BTN, &CFloatingWnd::OnBnClickedBollBtn)
	ON_BN_CLICKED(IDC_INDICATOR_MACD_BTN, &CFloatingWnd::OnBnClickedIndicatorMACDBtn)
	ON_BN_CLICKED(IDC_INDICATOR_MACD_SIGNAL_BTN, &CFloatingWnd::OnBnClickedIndicatorMACDSignalBtn)
	ON_BN_CLICKED(IDC_INDICATOR_KDJ_BTN, &CFloatingWnd::OnBnClickedIndicatorKDJBtn)
	ON_BN_CLICKED(IDC_INDICATOR_WR_BTN, &CFloatingWnd::OnBnClickedIndicatorWRBtn)
	ON_BN_CLICKED(IDC_INDICATOR_RSI_BTN, &CFloatingWnd::OnBnClickedIndicatorRSIBtn)
	ON_BN_CLICKED(IDC_CHIP_PEAK_BTN, &CFloatingWnd::OnBnClickedChipPeakBtn)
	ON_BN_CLICKED(IDC_ORDER_BOOK_BTN, &CFloatingWnd::OnBnClickedOrderBookBtn)
	ON_BN_CLICKED(IDC_EXPAND_BTN, &CFloatingWnd::OnBnClickedExpandBtn)
	ON_BN_CLICKED(IDC_TOGGLE_STOCK_LIST_BTN, &CFloatingWnd::OnBnClickedToggleStockListBtn)
END_MESSAGE_MAP()

int CFloatingWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	const int btnWidth = g_data.RDPI(40);
	const int btnHeight = g_data.RDPI(22);
	const int btnGap = 0;  // 按钮之间不留缝隙

	// 模式切换胶囊按钮（竞价、分时、日K、周K、月K，全自绘，在副图标题栏右侧动态定位）
	m_btnCallAuction.Create(_T("竞价"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_CALL_AUCTION_BTN);
	m_btnTimeLine.Create(_T("分时"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_TIMELINE_BTN);
	m_btnKLine.Create(_T("日K"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_KLINE_BTN);
	m_btnWeekKLine.Create(_T("周K"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_WEEK_KLINE_BTN);
	m_btnMonthKLine.Create(_T("月K"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_MONTH_KLINE_BTN);
	m_btnCallAuction.ShowWindow(SW_HIDE);
	m_btnTimeLine.ShowWindow(SW_HIDE);
	m_btnKLine.ShowWindow(SW_HIDE);
	m_btnWeekKLine.ShowWindow(SW_HIDE);
	m_btnMonthKLine.ShowWindow(SW_HIDE);

	// 右侧按钮：关闭、放大、自选折叠、筹码峰（全自绘）
	const int closeBtnWidth = g_data.RDPI(22);
	const int closeBtnHeight = g_data.RDPI(20);
	const int cx = lpCreateStruct->cx;
	CRect closeBtnRect(cx - closeBtnWidth, g_data.RDPI(2), cx, g_data.RDPI(2) + closeBtnHeight);
	m_btnClose.Create(_T("✕"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, closeBtnRect, this, IDC_CLOSE_BTN);

	const int expandBtnWidth = closeBtnWidth;
	const int expandBtnHeight = closeBtnHeight;
	CRect expandBtnRect(closeBtnRect.left - expandBtnWidth, g_data.RDPI(2), closeBtnRect.left, g_data.RDPI(2) + expandBtnHeight);
	m_btnExpand.Create(_T("□"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, expandBtnRect, this, IDC_EXPAND_BTN);

	const int toggleStockListBtnWidth = closeBtnWidth;
	const int toggleStockListBtnHeight = closeBtnHeight;
	CRect toggleStockListBtnRect(expandBtnRect.left - toggleStockListBtnWidth, g_data.RDPI(2), expandBtnRect.left, g_data.RDPI(2) + toggleStockListBtnHeight);
	m_btnToggleStockList.Create(_T("<|"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, toggleStockListBtnRect, this, IDC_TOGGLE_STOCK_LIST_BTN);

	const int rightBtnWidth = g_data.RDPI(32);

	CRect chipPeakBtnRect(toggleStockListBtnRect.left - rightBtnWidth, g_data.RDPI(2), toggleStockListBtnRect.left, g_data.RDPI(2) + btnHeight);
	m_btnChipPeak.Create(_T("CM"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, chipPeakBtnRect, this, IDC_CHIP_PEAK_BTN);

	CRect orderBookBtnRect(0, 0, rightBtnWidth, btnHeight);
	m_btnOrderBook.Create(_T("PK"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, orderBookBtnRect, this, IDC_ORDER_BOOK_BTN);

	CRect bollBtnRect(0, 0, rightBtnWidth, btnHeight);
	m_btnBoll.Create(_T("BL"), WS_CHILD | BS_OWNERDRAW, bollBtnRect, this, IDC_BOLL_BTN);
	m_btnBoll.ShowWindow(SW_HIDE);

	CRect maBtnRect(0, 0, rightBtnWidth, btnHeight);
	m_btnMA.Create(_T("MA"), WS_CHILD | BS_OWNERDRAW, maBtnRect, this, IDC_MA_BTN);
	m_btnMA.ShowWindow(SW_HIDE);

	// 副图指标切换胶囊按钮（初始隐藏，在OnPaint中定位）
	m_btnIndicatorCJL.Create(_T("VOL"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_INDICATOR_MACD_BTN);
	m_btnIndicatorMACD.Create(_T("MACD"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_INDICATOR_MACD_SIGNAL_BTN);
	m_btnIndicatorKDJ.Create(_T("KDJ"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_INDICATOR_KDJ_BTN);
	m_btnIndicatorRSI.Create(_T("RSI"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_INDICATOR_RSI_BTN);
	m_btnIndicatorWR.Create(_T("W&&R"), WS_CHILD | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, IDC_INDICATOR_WR_BTN);
	m_btnIndicatorCJL.ShowWindow(SW_HIDE);
	m_btnIndicatorMACD.ShowWindow(SW_HIDE);
	m_btnIndicatorKDJ.ShowWindow(SW_HIDE);
	m_btnIndicatorRSI.ShowWindow(SW_HIDE);
	m_btnIndicatorWR.ShowWindow(SW_HIDE);

	// 主页默认显示日K，并应用日K对应的指标和显示设置。
	SetDayKLineModeDefaults();

	UpdateModeButtons();
	UpdatePeriodComboVisibility();

	// 固定1秒定时检查：数据变化时才重绘，无变化则跳过
	SetTimer(IDC_REFRESH_TIMER, 1000, NULL);

	Invalidate();
	return 0;
}

// 处理消息
LRESULT CFloatingWnd::OnUpdateStatus(WPARAM wParam, LPARAM lParam)
{
	// wParam=0: 图表数据更新，wParam=1: 盘口数据更新
	if (wParam == 1)
		m_orderBookDirty = true;
	else
		m_chartDirty = true;
	return 0;
}

CFloatingWnd::CFloatingWnd() : m_isDestroying(FALSE), m_klineDataLoaded(false), m_viewMode(UI_VIEW_DAY_KLINE)
{
}

CFloatingWnd::~CFloatingWnd()
{
	// 标记窗口正在销毁
	m_isDestroying = TRUE;
	if (m_CTransparentWnd.GetSafeHwnd())
		m_CTransparentWnd.DestroyWindow();
}

BOOL CFloatingWnd::Create(CFont* font, CPoint pt, std::wstring stock_id)
{
	m_stock_id = stock_id;
	// 注册窗口类
	WNDCLASS wndcls;
	HINSTANCE hInst = AfxGetInstanceHandle();
	if (!(::GetClassInfo(hInst, L"CTransparentWnd", &wndcls)))
	{
		wndcls.style = CS_HREDRAW | CS_VREDRAW;  // 不使用CS_DBLCLKS，让双击也发送WM_LBUTTONDOWN
		wndcls.lpfnWndProc = ::DefWindowProc;
		wndcls.cbClsExtra = wndcls.cbWndExtra = 0;
		wndcls.hInstance = hInst;
		wndcls.hIcon = NULL;
		wndcls.hCursor = LoadCursor(NULL, IDC_ARROW);
		// wndcls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wndcls.hbrBackground = NULL; // 重要：设置为NULL
		wndcls.lpszMenuName = NULL;
		wndcls.lpszClassName = L"CTransparentWnd";
		if (!AfxRegisterClass(&wndcls))
			return FALSE;
	}

	// 设置父窗口指针
	m_CTransparentWnd.SetParent(this);

	m_pfont = font;
	// 记录主机字体信息，供 StockFont 派生字体统一字面并按主机字号等比缩放
	g_data.SetHostFont(font ? static_cast<HFONT>(font->GetSafeHandle()) : nullptr);

	// 获取包含鼠标点的显示器
	HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(MONITORINFO) };
	GetMonitorInfo(hMonitor, &mi);
	CRect screenRect = mi.rcWork; // 工作区域

	// 创建透明全屏窗口
	if (!m_CTransparentWnd.CreateEx(WS_EX_TOOLWINDOW /* | WS_EX_LAYERED */ /* | WS_EX_TRANSPARENT */,
		L"CTransparentWnd", L"", WS_POPUP | WS_VISIBLE,
		screenRect, NULL, 0, NULL))
	{
		TRACE(L"Failed to create transparent window\n");
		return FALSE;
	}

	const int WIDTH = g_data.RDPI(g_data.m_setting_data.m_kline_width);
	const int HEIGHT = g_data.RDPI(g_data.m_setting_data.m_kline_height);

	// 根据配置计算悬浮窗在屏幕工作区的位置
	int x = screenRect.right - WIDTH - 3;
	int y = screenRect.bottom - HEIGHT - 3;

	switch (g_data.m_setting_data.m_display_area)
	{
	case AREA_LEFT_TOP:
		x = screenRect.left + 3;
		y = screenRect.top + 3;
		break;
	case AREA_RIGHT_TOP:
		x = screenRect.right - WIDTH - 3;
		y = screenRect.top + 3;
		break;
	case AREA_LEFT_BOTTOM:
		x = screenRect.left + 3;
		y = screenRect.bottom - HEIGHT - 3;
		break;
	case AREA_RIGHT_BOTTOM:
		x = screenRect.right - WIDTH - 3;
		y = screenRect.bottom - HEIGHT - 3;
		break;
	case AREA_CENTER:
		x = screenRect.left + (screenRect.Width() - WIDTH) / 2;
		y = screenRect.top + (screenRect.Height() - HEIGHT) / 2;
		break;
	default:
		x = screenRect.right - WIDTH - 3;
		y = screenRect.bottom - HEIGHT - 3;
		break;
	}

	if (x + WIDTH > screenRect.right)
		x = max(screenRect.left, screenRect.right - WIDTH);
	if (x < screenRect.left)
		x = screenRect.left;
	if (y + HEIGHT > screenRect.bottom)
		y = max(screenRect.top, screenRect.bottom - HEIGHT);
	if (y < screenRect.top)
		y = screenRect.top;

	CRect rect(x, y, x + WIDTH, y + HEIGHT);

	// 创建实际的浮动窗口
	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW),
		L"", WS_POPUP | WS_VISIBLE | WS_BORDER | WS_CLIPCHILDREN,
		rect, &m_CTransparentWnd, 0))
	{
		TRACE(L"Failed to create floating window\n");
		m_CTransparentWnd.DestroyWindow();
		return FALSE;
	}

	// 确保浮动窗口在最顶层
	BringWindowToTop();
	SetForegroundWindow();

	// 设置弹出窗口半透明/暗黑质感底色
	HWND hWnd = this->m_hWnd;
	::SetWindowLongPtr(hWnd, GWL_EXSTYLE, ::GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
	::SetLayeredWindowAttributes(hWnd, 0, 248, LWA_ALPHA);

	// 设置父窗口完全透明
	m_CTransparentWnd.SetLayeredWindowAttributes(0, 0, LWA_ALPHA);
	m_CTransparentWnd.ShowWindow(SW_SHOW);

	EnsureStockListVisible();

	TRACE(L"Windows created successfully\n");
	return TRUE;
}

// ========== OnPaint ==========
void CFloatingWnd::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);

	CDC memDC;
	CBitmap memBitmap;
	memDC.CreateCompatibleDC(&dc);
	if (m_pfont)
	{
		memDC.SelectObject(m_pfont);
	}
	memBitmap.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
	CBitmap* pOldBitmap = memDC.SelectObject(&memBitmap);

	// 填充现代暗黑专业底色 (#12141A)
	memDC.FillSolidRect(rect, COLOR_BG_DARK);

	memDC.SetBkMode(TRANSPARENT);

	int x = rect.left, y = rect.top, h = rect.Height(), w = rect.Width();

	bool isIndex = (GetStockPriority(m_stock_id) < 200);
	// 大盘在K线模式下不显示盘口（所有K线模式m_viewMode>=UI_VIEW_DAY_KLINE，自动覆盖）
	bool isIndexKLine = isIndex && m_viewMode >= UI_VIEW_DAY_KLINE;

	const int stockListWidth = m_showStockList ? CStockListPanel::GetPanelWidth() : 0;  // 左侧股票列表面板宽度
	const int orderBookWidth = IsInfoPanelVisible(isIndexKLine) ? ORDER_BOOK_WIDTH : 0;
	const int chartWidth = w - orderBookWidth;
	// 左侧Y轴坐标区域宽度（所有图表统一预留）
	const int yAxisWidth = g_data.RDPI(50);

	const int headerHeight = g_data.RDPI(26);
	const int xAxisLabelHeight = g_data.RDPI(20);
	const int singleBarHeight = g_data.RDPI(20);  // 单行状态栏高度
	const int relatedBarHeight = 0;  // 移除顶部关联股票栏
	const int indexBarHeight = singleBarHeight;    // 底部系统状态栏高度（单行4个）
	const bool showPositionSummary = CStockListPanel::ClampGroupTab(m_activeGroupTab) == 1;
	const int positionSummaryHeight = showPositionSummary ? singleBarHeight : 0;

	// 统一现代双层布局：标题栏 + 主走势图(约62%) + 单一副图(约38%) + 时间标签 + 底部系统状态栏
	int chartArea = h - headerHeight - relatedBarHeight - positionSummaryHeight - xAxisLabelHeight - indexBarHeight;
	int priceChartHeight, subChartHeight;
	if (m_expandedMode)
	{
		priceChartHeight = chartArea;
		subChartHeight = 0;
	}
	else
	{
		priceChartHeight = chartArea * 62 / 100;
		subChartHeight = chartArea - priceChartHeight;
	}
	int macdChartHeight = subChartHeight;
	int kdjChartHeight = subChartHeight;
	int volumeChartHeight = subChartHeight;

	const int priceChartTop = headerHeight + relatedBarHeight + positionSummaryHeight;

	STOCK::StockInfo realtimeData;
	STOCK::ChipDistribution chipData;
	STOCK::CallAuctionData callAuctionData;
	std::vector<STOCK::TimelinePoint> timelinePoint;
	std::vector<STOCK::KLinePoint> klineData;
	{
		std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
		auto stockData = g_data.GetStockData(m_stock_id);
		if (stockData)
		{
			realtimeData = stockData->info;
			chipData = stockData->chipDistribution;
			if (m_viewMode == UI_VIEW_DAY_KLINE || m_viewMode == UI_VIEW_WEEK_KLINE || m_viewMode == UI_VIEW_MONTH_KLINE)
			{
				STOCK::KLineData* klineObj = nullptr;
				if (m_viewMode == UI_VIEW_DAY_KLINE)
					klineObj = stockData->getKLineData();
				else if (m_viewMode == UI_VIEW_WEEK_KLINE)
					klineObj = stockData->getWeekKLineData();
				else if (m_viewMode == UI_VIEW_MONTH_KLINE)
					klineObj = stockData->getMonthKLineData();

				if (klineObj)
				{
					klineData = klineObj->data;
					// 将K线数据转换为TimelinePoint格式，复用走势图与指标绘制流程
					for (const auto& kp : klineObj->data)
					{
						STOCK::TimelinePoint tp;
						// K线 day 格式为 "YYYY-MM-DD"，tp.time 取 "MM-DD" 用于常规标签，tp.fullTime 存完整日期用于悬停高亮
						if (kp.day.length() >= 10)
						{
							tp.time = kp.day.substr(5, 5);  // "MM-DD"
							tp.fullTime = kp.day;           // "YYYY-MM-DD"
						}
						else
						{
							tp.time = kp.day;
							tp.fullTime = kp.day;
						}
						tp.price = kp.close;
						tp.openPrice = kp.open;
						tp.averagePrice = kp.close;  // K线无分时均价，暂用收盘价
						tp.volume = kp.volume;
						tp.amount = static_cast<STOCK::Amount>(kp.volume) * kp.close;
						timelinePoint.push_back(tp);
					}
				}
			}
			else
			{
				auto timelineData = stockData->getTimelineData();
				if (timelineData)
				{
					timelinePoint = timelineData->data;
				}

				auto klineObj = stockData->getKLineData();
				if (klineObj)
				{
					klineData = klineObj->data;
				}
			}
			// 集合竞价数据（所有模式都需要加载，竞价模式下用于绘图，其他模式用于盘口展示）
			callAuctionData = stockData->callAuctionData;
		}
	}

	if (m_viewMode != UI_VIEW_OVERVIEW)
	{
		{
			int closeBtnW = g_data.RDPI(20);
			int closeBtnH = g_data.RDPI(18);
			int headerBtnTop = g_data.RDPI(2);
			SafeSetWindowPos(m_btnClose, w - closeBtnW, headerBtnTop, closeBtnW, closeBtnH);
			SafeSetWindowPos(m_btnExpand, w - closeBtnW * 2, headerBtnTop, closeBtnW, closeBtnH);
			SafeSetWindowPos(m_btnToggleStockList, w - closeBtnW * 3, headerBtnTop, closeBtnW, closeBtnH);
			// 筹码峰/盘口按钮定位到盘口标题栏
			int obTitleH = g_data.RDPI(16);
			int obBtnW = g_data.RDPI(34);
			int obBtnH = min(obTitleH, g_data.RDPI(16));
			int obBtnTop = headerHeight + relatedBarHeight + (obTitleH - obBtnH) / 2;
			bool showObBtns = !isIndexKLine;
			SafeSetWindowPos(m_btnChipPeak, w - obBtnW, obBtnTop, obBtnW, obBtnH);
			SafeShowWindow(m_btnChipPeak, showObBtns);
			SafeSetWindowPos(m_btnOrderBook, w - obBtnW * 2, obBtnTop, obBtnW, obBtnH);
			SafeShowWindow(m_btnOrderBook, showObBtns);
		}

		// 左侧股票列表面板（无论分时数据是否加载都绘制）
		if (m_showStockList)
		{
			// 顶栏分组标签条（自选股/持仓/自定义分组 + 更多分组下拉），记录矩形供点击命中
			const int activeGroupTab = CStockListPanel::ClampGroupTab(m_activeGroupTab);
			m_groupTabs = CStockListPanel::LayoutGroupTabs(memDC, w, headerHeight, activeGroupTab);
			CStockListPanel::DrawGroupTabs(memDC, m_groupTabs, m_hoverGroupTab);
			m_stockListPanel.Draw(memDC, 0, headerHeight + relatedBarHeight, stockListWidth, h - headerHeight - indexBarHeight - relatedBarHeight, m_stock_id, m_stockListScrollOffset, activeGroupTab);
		}

		// 持仓分组汇总行：位于主图标题栏下方，仅占图表区域，不覆盖左侧列表和右侧盘口。
		if (showPositionSummary)
		{
			double totalMarketValue = 0.0;
			double floatingProfitLoss = 0.0;
			double todayProfitLoss = 0.0;
			std::vector<std::wstring> positionCodes = CStockListPanel::GetStockListCodes(1);
			{
				std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
				for (const auto& code : positionCodes)
				{
					auto stockData = g_data.GetStockData(code);
					if (!stockData)
						continue;
					double holdingCount = g_data.GetHoldingCount(code);
					double costPrice = g_data.GetCostPrice(code);
					double currentPrice = stockData->info.currentPrice > 0 ? stockData->info.currentPrice : stockData->info.prevClosePrice;
					if (holdingCount <= 0 || currentPrice <= 0)
						continue;
					totalMarketValue += currentPrice * holdingCount;
					if (costPrice > 0)
						floatingProfitLoss += (currentPrice - costPrice) * holdingCount;
					if (stockData->info.prevClosePrice > 0)
						todayProfitLoss += (currentPrice - stockData->info.prevClosePrice) * holdingCount;
				}
			}

			// 总市值显示完整数值，不使用 FormatAmount 的“万/亿”缩写。
			CString marketText = CCommon::FormatNumber(totalMarketValue, 2);
			CString floatingText = CCommon::FormatAmount(std::abs(floatingProfitLoss));
			CString todayText = CCommon::FormatAmount(std::abs(todayProfitLoss));
			if (floatingProfitLoss > 0.0001) floatingText = _T("+") + floatingText;
			else if (floatingProfitLoss < -0.0001) floatingText = _T("-") + floatingText;
			else floatingText = _T("0.00");
			if (todayProfitLoss > 0.0001) todayText = _T("+") + todayText;
			else if (todayProfitLoss < -0.0001) todayText = _T("-") + todayText;
			else todayText = _T("0.00");

			const int summaryX = stockListWidth;
			const int summaryW = chartWidth - stockListWidth;
			const int summaryY = headerHeight + relatedBarHeight;
			memDC.FillSolidRect(summaryX, summaryY, summaryW, positionSummaryHeight, COLOR_BG_HEADER);
			memDC.FillSolidRect(summaryX, summaryY, summaryW, 1, COLOR_DARK_GRAY_BORDER);
			int textH = memDC.GetTextExtent(_T("Ay")).cy;
			int textY = summaryY + max(0, (positionSummaryHeight - textH) / 2);
			const CString labels[] = { _T("总市值: "), _T("浮动盈亏: "), _T("当日盈亏: ") };
			const CString values[] = { marketText, floatingText, todayText };
			const COLORREF valueColors[] = {
				COLOR_TEXT_PRIMARY,
				floatingProfitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN,
				todayProfitLoss >= 0 ? COLOR_RED_UP : COLOR_GREEN_DOWN
			};
			memDC.SetBkMode(TRANSPARENT);
			// 三项数据左对齐排布（固定宽松间距），盘口收起后末端不会顶到PK按钮
			int drawX = summaryX + g_data.RDPI(12);
			const int itemGap = g_data.RDPI(48);
			for (int i = 0; i < 3; ++i)
			{
				memDC.SetTextColor(COLOR_TEXT_MUTED);
				memDC.TextOut(drawX, textY, labels[i]);
				drawX += memDC.GetTextExtent(labels[i]).cx;
				memDC.SetTextColor(valueColors[i]);
				memDC.TextOut(drawX, textY, values[i]);
				drawX += memDC.GetTextExtent(values[i]).cx + itemGap;
			}
		}

		// 集合竞价模式绘制（主图+副图各占一半）
		if (m_viewMode == UI_VIEW_AUCTION)
		{
			// 竞价模式：主图价格和副图成交量各占一半
			int totalChartHeight = priceChartHeight + macdChartHeight + kdjChartHeight + volumeChartHeight;
			int halfChartHeight = totalChartHeight / 2;
			const int titleH = g_data.RDPI(16);
			int origPriceTop = priceChartTop;
			int origVolTop = priceChartTop + halfChartHeight;

			TimelineDrawContext ctx;
			ctx.chartLeft = stockListWidth + yAxisWidth;
			ctx.chartWidth = chartWidth - stockListWidth - yAxisWidth;
			ctx.windowWidth = w;
			ctx.chartHeight = h;
			ctx.priceChartTop = origPriceTop + titleH;
			ctx.priceChartHeight = halfChartHeight - titleH;
			ctx.volumeChartTop = origVolTop + titleH;
			ctx.volumeChartHeight = halfChartHeight - titleH;
			ctx.macdChartTop = origVolTop + titleH;
			ctx.macdChartHeight = halfChartHeight - titleH;
			ctx.positionY = origVolTop + halfChartHeight + g_data.RDPI(2);
			ctx.realtimeData = realtimeData;
			ctx.startIndex = 0;
			ctx.visibleCount = 0;
			ctx.klineData = &klineData;

			// Y轴范围基于集合竞价数据
			STOCK::Price visMax = callAuctionData.matchPrice;
			STOCK::Price visMin = callAuctionData.matchPrice;
			for (const auto& snap : callAuctionData.snapshots)
			{
				if (snap.matchPrice > 0)
				{
					visMax = (std::max)(visMax, snap.matchPrice);
					visMin = (std::min)(visMin, snap.matchPrice);
				}
			}
			STOCK::Price refPrice = callAuctionData.prevClosePrice > 0 ? callAuctionData.prevClosePrice : realtimeData.prevClosePrice;
			if (refPrice > 0)
			{
				if (visMax <= 0) visMax = refPrice;
				if (visMin <= 0 || visMin == visMax) visMin = refPrice;
			}
			if (visMax <= visMin)
			{
				if (refPrice > 0)
				{
					visMax = refPrice * 1.02;
					visMin = refPrice * 0.98;
				}
				else
				{
					visMax = 10.0;
					visMin = 9.8;
				}
			}

			const double DIV_COUNT = 6.0;
			const double MIN_STEP = 0.001;
			double axisMin, axisMax, niceStep;
			CStockIndicator::CalcNiceAxisRange(visMin, visMax, DIV_COUNT, MIN_STEP, axisMin, axisMax, niceStep);
			ctx.maxPrice = axisMax;
			ctx.minPrice = axisMin;
			ctx.niceStep = niceStep;
			ctx.unitY = ctx.priceChartHeight / (ctx.maxPrice - ctx.minPrice);

			std::vector<STOCK::TimelinePoint> emptyTimeline;
			ctx.timelinePoint = &emptyTimeline;
			ctx.fullTimeline = &emptyTimeline;

			memDC.SaveDC();
			memDC.OffsetViewportOrg(stockListWidth + yAxisWidth, 0);

			CTimelineChart::HoverState tlHover;
			tlHover.viewMode = m_viewMode;
			tlHover.isHoveringVolume = m_isHoveringVolume;
			tlHover.hoveredBarIndex = m_hoveredBarIndex;
			tlHover.hoveredData = m_hoveredData;
			tlHover.hoverMa1 = m_hoverMa1;
			tlHover.hoverMaValues = m_hoverMaValues;
			tlHover.hoverPrevMa1 = m_hoverPrevMa1;
			tlHover.hoverTip = m_hoverTip;
			tlHover.timelinePriceTitleTip = m_timelinePriceTitleTip;
			tlHover.timelineVolumeTitleTip = m_timelineVolumeTitleTip;
			tlHover.timelineMacdTitleTip = m_timelineMacdTitleTip;
			tlHover.timelineKdjTitleTip = m_timelineKdjTitleTip;
			tlHover.timelineWrTitleTip = m_timelineWrTitleTip;
			tlHover.timelineRsiTitleTip = m_timelineRsiTitleTip;
			tlHover.showMA = m_showMA;
			tlHover.showBollBands = m_showBollBands;
			tlHover.showTrendView = m_showTrendView;
			tlHover.showChipPeak = m_showChipPeak;
			tlHover.expandedMode = m_expandedMode;
			tlHover.klinePeriodDays = m_klinePeriodDays;
			tlHover.scrollOffset = m_scrollOffset;
			tlHover.timelineScrollOffset = m_timelineScrollOffset;
			tlHover.timelineVisibleCount = m_timelineVisibleCount;
			tlHover.timelineLastTotalPoints = m_timelineLastTotalPoints;
			tlHover.stockId = m_stock_id;
			tlHover.mousePos = m_mousePos;
			tlHover.timelineIndicator = static_cast<int>(m_timelineIndicator);

			m_timelineChart.DrawTimelineHeader(memDC, ctx, tlHover);
			m_callAuctionChart.Draw(memDC, ctx, callAuctionData);

			// 标题栏+图表内容（竞价模式只有价格图和成交量图）
			m_timelineChart.DrawPriceChartArea(memDC, ctx, origPriceTop, halfChartHeight, tlHover);
			{
				CIndicatorChart::HoverState volHover;
				volHover.isHoveringVolume = m_isHoveringVolume;
				volHover.hoveredBarIndex = m_hoveredBarIndex;
				volHover.viewMode = m_viewMode;
				volHover.timelineVolumeTitleTip = m_timelineVolumeTitleTip;
				m_indicatorChart.DrawVolumeChartArea(memDC, ctx, origVolTop, halfChartHeight, false, volHover);
			}

			memDC.RestoreDC(-1);

			// 主标题栏右侧按钮定位
			{
				int closeBtnW = g_data.RDPI(20);
				int closeBtnH = g_data.RDPI(18);
				int top = g_data.RDPI(2);
				SafeSetWindowPos(m_btnClose, w - closeBtnW, top, closeBtnW, closeBtnH);
				SafeSetWindowPos(m_btnExpand, w - closeBtnW * 2, top, closeBtnW, closeBtnH);
				SafeSetWindowPos(m_btnToggleStockList, w - closeBtnW * 3, top, closeBtnW, closeBtnH);
			}
			// 盘口按钮
			{
				int obTitleH = g_data.RDPI(16);
				int obBtnW = g_data.RDPI(34);
				int obBtnH = min(obTitleH, g_data.RDPI(16));
				int obBtnTop = headerHeight + relatedBarHeight + (obTitleH - obBtnH) / 2;
				bool showObBtns = !isIndexKLine;
				SafeSetWindowPos(m_btnChipPeak, w - obBtnW, obBtnTop, obBtnW, obBtnH);
				SafeShowWindow(m_btnChipPeak, showObBtns);
				SafeSetWindowPos(m_btnOrderBook, w - obBtnW * 2, obBtnTop, obBtnW, obBtnH);
				SafeShowWindow(m_btnOrderBook, showObBtns);
			}
			// 定位模式切换标签到副图标题栏右侧 [竞价] [分时] [日K] [周K] [月K]
			int modeTabW = g_data.RDPI(38);
			int tabY = origVolTop + g_data.RDPI(1);
			int tabH = titleH - g_data.RDPI(2);
			int tabGap = g_data.RDPI(2);
			int rightEdge = chartWidth;
			int modeTabsTotalW = 5 * modeTabW + 4 * tabGap;
			int modeStartX = rightEdge - modeTabsTotalW - g_data.RDPI(2);

			SafeSetWindowPos(m_btnCallAuction, modeStartX, tabY, modeTabW, tabH);
			SafeShowWindow(m_btnCallAuction, true);

			SafeSetWindowPos(m_btnTimeLine, modeStartX + (modeTabW + tabGap), tabY, modeTabW, tabH);
			SafeShowWindow(m_btnTimeLine, true);

			SafeSetWindowPos(m_btnKLine, modeStartX + (modeTabW + tabGap) * 2, tabY, modeTabW, tabH);
			SafeShowWindow(m_btnKLine, true);

			SafeSetWindowPos(m_btnWeekKLine, modeStartX + (modeTabW + tabGap) * 3, tabY, modeTabW, tabH);
			SafeShowWindow(m_btnWeekKLine, true);

			SafeSetWindowPos(m_btnMonthKLine, modeStartX + (modeTabW + tabGap) * 4, tabY, modeTabW, tabH);
			SafeShowWindow(m_btnMonthKLine, true);

			// 竞价模式隐藏副图指标工具按钮
			SafeShowWindow(m_btnMA, false);
			SafeShowWindow(m_btnBoll, false);
			SafeShowWindow(m_btnIndicatorMACD, false);
			SafeShowWindow(m_btnIndicatorCJL, false);
			SafeShowWindow(m_btnIndicatorKDJ, false);
			SafeShowWindow(m_btnIndicatorWR, false);
			SafeShowWindow(m_btnIndicatorRSI, false);				// 右侧盘口（竞价模式下盘口跟随PK开关）
				if (IsInfoPanelVisible(isIndexKLine))
				{
					m_orderBookPanel.Draw(memDC, chartWidth, w, h - headerHeight - indexBarHeight - relatedBarHeight, realtimeData, klineData, m_viewMode);
				}
		}
		else if (!timelinePoint.empty())
		{
			// 先基于完整分时数据计算MA，避免缩放/拖动后只用可见区间导致均线与其他APP不一致
			CStockIndicator::CalcAllRollingAvgPrices(timelinePoint);

			// 计算可见范围：m_timelineVisibleCount控制缩放，m_timelineScrollOffset控制拖动
			int totalPoints = static_cast<int>(timelinePoint.size());
			int visibleCount = min(m_timelineVisibleCount, totalPoints);
			int maxOffset = max(0, totalPoints - visibleCount);
			int prevMaxOffset = max(0, m_timelineLastTotalPoints - visibleCount);
			bool wasAtLatest = (m_timelineScrollOffset < 0 || m_timelineScrollOffset >= prevMaxOffset);

			// 首次显示或更新前就在最新位置时，数据追加后继续自动跟随末尾
			if (wasAtLatest)
				m_timelineScrollOffset = maxOffset;
			m_timelineLastTotalPoints = totalPoints;

			int startIndex = max(0, min(m_timelineScrollOffset, maxOffset));
			// 创建可见范围的子向量
			std::vector<STOCK::TimelinePoint> subTimeline(
				timelinePoint.begin() + startIndex,
				timelinePoint.begin() + startIndex + visibleCount);

			TimelineDrawContext ctx;
			ctx.chartLeft = stockListWidth + yAxisWidth;         // 左侧股票列表+Y轴留白
			ctx.chartWidth = chartWidth - stockListWidth - yAxisWidth;  // 图表宽度（不含左右价格轴）
			ctx.windowWidth = w;
			ctx.chartHeight = h;
			// 每个图表顶部预留16像素标题栏，绘图区域下移并减小高度
			const int titleH = g_data.RDPI(16);
			int origPriceTop = priceChartTop;
			int origVolTop = priceChartTop + priceChartHeight;        // 成交量区（紧贴走势图下方）
			int origIndicatorTop = origVolTop + volumeChartHeight;    // MACD指标区
			int origKdjTop = origIndicatorTop + macdChartHeight;      // KDJ指标区
			ctx.priceChartTop = origPriceTop + titleH;
			ctx.priceChartHeight = priceChartHeight - titleH;
			ctx.volumeChartTop = origVolTop + titleH;
			ctx.volumeChartHeight = volumeChartHeight - titleH;
			ctx.macdChartTop = origIndicatorTop + titleH;
			ctx.macdChartHeight = macdChartHeight - titleH;
			// 时间标签位置：KDJ图下方
			ctx.positionY = origKdjTop + kdjChartHeight + g_data.RDPI(2);
			ctx.baseFont = m_pfont;
			ctx.realtimeData = realtimeData;
			ctx.timelinePoint = &subTimeline;
			ctx.fullTimeline = &timelinePoint;  // 完整分时数据，供布林带等指标回溯
			ctx.startIndex = startIndex;
			ctx.visibleCount = visibleCount;
			ctx.xAxisPoints = (m_viewMode >= UI_VIEW_DAY_KLINE) ? 0 : m_timelineVisibleCount;  // 仅分时模式固定X轴，K线模式动态
			ctx.klineData = &klineData;

			// 使用完整数据中已计算好的MA值
			if (!subTimeline.empty())
			{
				const auto& lastPt = subTimeline.back();
				ctx.ma1 = lastPt.price;
				ctx.maValues = lastPt.maValues;
			}

			// 计算整齐Y轴范围：先根据可见数据范围计算整齐步长，再扩展为整齐边界
			// 注意：缩放/拖动后Y轴范围仅基于可见数据，不强制包含昨收价，
			// 这样缩放到局部区域时Y轴步长能正确缩小，走势线始终居中
			{
				STOCK::Price visMax = 0;
				STOCK::Price visMin = (std::numeric_limits<STOCK::Price>::max)();
				for (const auto& tp : subTimeline)
				{
					if (tp.price > 0)
					{
						visMax = (std::max)(visMax, tp.price);
						visMin = (std::min)(visMin, tp.price);
					}
					if (m_viewMode < UI_VIEW_DAY_KLINE && tp.averagePrice > 0)
					{
						visMax = (std::max)(visMax, tp.averagePrice);
						visMin = (std::min)(visMin, tp.averagePrice);
					}
				}
				// K线模式：Y轴范围需要包含K线柱的high/low
				// 分别纳入high和low，避免low=0时丢失有效的high值，也避免low=0/close=0时visMin被设为0
				if (m_viewMode >= UI_VIEW_DAY_KLINE && ctx.klineData)
				{
					const auto& klineRef = *ctx.klineData;
					for (int i = 0; i < visibleCount && (startIndex + i) < static_cast<int>(klineRef.size()); i++)
					{
						const auto& kp = klineRef[startIndex + i];
						if (kp.high > 0)
							visMax = (std::max)(visMax, kp.high);
						if (kp.low > 0)
							visMin = (std::min)(visMin, kp.low);
					}
				}

				// 开启MA均线时，Y轴范围同时包含可见区间的MA均线值，避免均线超出价格图区发生截断或异常
				if (m_showMA && !subTimeline.empty())
				{
					for (const auto& tp : subTimeline)
					{
						for (STOCK::Price maVal : tp.maValues)
						{
							if (maVal > 0)
							{
								visMax = (std::max)(visMax, maVal);
								visMin = (std::min)(visMin, maVal);
							}
						}
					}
				}

				// 开启BOLL时，Y轴范围同时包含可见区间的布林上下轨，避免窄幅区间下BOLL被映射到价格图区外
				// 分时走势类视图下，被「均线日配置」页取消勾选的轨道不再纳入扩展（日K蜡烛视图维持既有行为）
				if (m_showBollBands && !timelinePoint.empty())
				{
					bool bollCfgScope = (m_viewMode < UI_VIEW_DAY_KLINE) || m_showTrendView;
					const int N = 20;
					const int K = 2;
					const int totalCount = static_cast<int>(timelinePoint.size());
					for (int i = 0; i < visibleCount && (startIndex + i) < totalCount; i++)
					{
						int globalIdx = startIndex + i;
						if (globalIdx < N - 1)
							continue;

						double sum = 0;
						for (int j = globalIdx - N + 1; j <= globalIdx; j++)
							sum += timelinePoint[j].price;
						double ma = sum / N;

						double variance = 0;
						for (int j = globalIdx - N + 1; j <= globalIdx; j++)
						{
							double diff = timelinePoint[j].price - ma;
							variance += diff * diff;
						}
						double stddev = std::sqrt(variance / N);
						double upperBand = ma + K * stddev;
						double lowerBand = ma - K * stddev;
						if (upperBand > 0 && (!bollCfgScope || g_data.m_setting_data.m_boll_upper_visible))
							visMax = (std::max)(visMax, upperBand);
						if (lowerBand > 0 && (!bollCfgScope || g_data.m_setting_data.m_boll_lower_visible))
							visMin = (std::min)(visMin, lowerBand);
					}
				}
				// 开启基金净值曲线时，Y轴范围同时包含可见区间的IOPV值，避免净值曲线绘制到图表区外
				if (m_showJZCurve && m_viewMode < UI_VIEW_DAY_KLINE)
				{
					for (const auto& tp : subTimeline)
					{
						if (tp.iopv > 0)
						{
							visMax = (std::max)(visMax, tp.iopv);
							visMin = (std::min)(visMin, tp.iopv);
						}
					}
				}
				if (visMin == (std::numeric_limits<STOCK::Price>::max)() || visMax <= visMin)
				{
					// 数据无效，回退到涨跌停范围
					STOCK::Price priceLimit = ctx.realtimeData.priceLimit;
					visMax = ctx.realtimeData.prevClosePrice + priceLimit;
					visMin = ctx.realtimeData.prevClosePrice - priceLimit;
				}

				// Y轴固定6等分7根横线：Nice Number算法向上取整本身已提供边距，无需额外除以(DIV_COUNT-2)
				// 先把轴边界对齐到实际显示的价格刻度，再让网格线、标签、曲线共用同一组刻度值，避免标签四舍五入后与曲线位置错位
				const double DIV_COUNT = 6.0;
				const double MIN_STEP = 0.001;
				double axisMin, axisMax, niceStep;
				CStockIndicator::CalcNiceAxisRange(visMin, visMax, DIV_COUNT, MIN_STEP, axisMin, axisMax, niceStep);

				ctx.maxPrice = axisMax;
				ctx.minPrice = axisMin;
				ctx.niceStep = niceStep;
				ctx.unitY = ctx.priceChartHeight / (ctx.maxPrice - ctx.minPrice);
			}

			// 使用视口偏移让分时图所有绘制自动向右偏移 stockListWidth + yAxisWidth，实现左侧股票列表和Y轴留白
			memDC.SaveDC();
			memDC.OffsetViewportOrg(stockListWidth + yAxisWidth, 0);

			CTimelineChart::HoverState tlHover;
			tlHover.viewMode = m_viewMode;
			tlHover.isHoveringVolume = m_isHoveringVolume;
			tlHover.hoveredBarIndex = m_hoveredBarIndex;
			tlHover.hoveredData = m_hoveredData;
			tlHover.hoverMa1 = m_hoverMa1;
			tlHover.hoverMaValues = m_hoverMaValues;
			tlHover.hoverPrevMa1 = m_hoverPrevMa1;
			tlHover.hoverTip = m_hoverTip;
			tlHover.timelinePriceTitleTip = m_timelinePriceTitleTip;
			tlHover.timelineVolumeTitleTip = m_timelineVolumeTitleTip;
			tlHover.timelineMacdTitleTip = m_timelineMacdTitleTip;
			tlHover.timelineKdjTitleTip = m_timelineKdjTitleTip;
			tlHover.timelineWrTitleTip = m_timelineWrTitleTip;
			tlHover.timelineRsiTitleTip = m_timelineRsiTitleTip;
			tlHover.showMA = m_showMA;
			tlHover.showBollBands = m_showBollBands;
			tlHover.showTrendView = m_showTrendView;
			tlHover.showChipPeak = m_showChipPeak;
			tlHover.expandedMode = m_expandedMode;
			tlHover.klinePeriodDays = m_klinePeriodDays;
			tlHover.scrollOffset = m_scrollOffset;
			tlHover.timelineScrollOffset = m_timelineScrollOffset;
			tlHover.timelineVisibleCount = m_timelineVisibleCount;
			tlHover.timelineLastTotalPoints = m_timelineLastTotalPoints;
			tlHover.stockId = m_stock_id;
			tlHover.mousePos = m_mousePos;
			tlHover.timelineIndicator = static_cast<int>(m_timelineIndicator);

			m_timelineChart.DrawTimelineHeader(memDC, ctx, tlHover);
			m_timelineChart.DrawTimelineGridAndLines(memDC, ctx, tlHover);
			// 走势图区域（标题栏+图表内容）
			m_timelineChart.DrawPriceChartArea(memDC, ctx, origPriceTop, priceChartHeight, tlHover);

			// 现代双层架构：单一副图区域（成交量 / MACD / KDJ / RSI / WR）
			if (subChartHeight > 0 && !m_expandedMode)
			{
				int subChartTop = origPriceTop + priceChartHeight;
				// 百分比刻度已画入价格图内部，副图与价格图共用同一宽度。
				TimelineDrawContext indicatorCtx = ctx;
				auto indicatorType = static_cast<CIndicatorChart::TimelineIndicator>(m_timelineIndicator);
				CIndicatorChart::HoverState subHover;
				subHover.isHoveringVolume = m_isHoveringVolume;
				subHover.hoveredBarIndex = m_hoveredBarIndex;
				subHover.viewMode = m_viewMode;
				subHover.timelineVolumeTitleTip = m_timelineVolumeTitleTip;
				subHover.timelineMacdTitleTip = m_timelineMacdTitleTip;
				subHover.timelineKdjTitleTip = m_timelineKdjTitleTip;
				subHover.timelineWrTitleTip = m_timelineWrTitleTip;
				subHover.timelineRsiTitleTip = m_timelineRsiTitleTip;

				if (indicatorType == CIndicatorChart::TimelineIndicator::MACD)
				{
					m_indicatorChart.DrawMacdChartArea(memDC, indicatorCtx, subChartTop, subChartHeight, m_timelineMacdTitleTip, subHover);
				}
				else
				{
					m_indicatorChart.DrawIndicatorChartArea(memDC, indicatorCtx, subChartTop, subChartHeight, true, indicatorType, subHover);
				}
			}
			m_timelineChart.DrawTimelineHoverOverlay(memDC, ctx, tlHover);

			memDC.RestoreDC(-1);

			// 应用信号颜色到按钮背景
			ApplySignalColors(tlHover.bollSignalColor, tlHover.macdSignalColor, tlHover.kdjSignalColor, tlHover.wrSignalColor, tlHover.rsiSignalColor, tlHover.maSignalColor);

			// 定位副图指标水平胶囊切换器 [VOL] [MACD] [KDJ] [RSI] [W&R]
			if (subChartHeight > 0 && !m_expandedMode)
			{
				int subChartTop = origPriceTop + priceChartHeight;
				int tabX = stockListWidth + yAxisWidth + g_data.RDPI(4);
				int tabY = subChartTop + g_data.RDPI(1);
				int tabW = g_data.RDPI(42);
				int tabH = titleH - g_data.RDPI(2);
				int tabGap = g_data.RDPI(2);

				SafeSetWindowPos(m_btnIndicatorCJL, tabX, tabY, tabW, tabH);
				m_btnIndicatorCJL.SetWindowText(_T("VOL"));
				SafeShowWindow(m_btnIndicatorCJL, true);

				SafeSetWindowPos(m_btnIndicatorMACD, tabX + (tabW + tabGap), tabY, tabW, tabH);
				SafeShowWindow(m_btnIndicatorMACD, true);

				SafeSetWindowPos(m_btnIndicatorKDJ, tabX + (tabW + tabGap) * 2, tabY, tabW, tabH);
				SafeShowWindow(m_btnIndicatorKDJ, true);

				SafeSetWindowPos(m_btnIndicatorRSI, tabX + (tabW + tabGap) * 3, tabY, tabW, tabH);
				SafeShowWindow(m_btnIndicatorRSI, true);

				SafeSetWindowPos(m_btnIndicatorWR, tabX + (tabW + tabGap) * 4, tabY, tabW, tabH);
				SafeShowWindow(m_btnIndicatorWR, true);

				// 定位模式切换标签到副图标题栏右侧 [竞价] [分时] [日K] [周K] [月K]
				int modeTabW = g_data.RDPI(38);
				int rightEdge = chartWidth;
				int modeTabsTotalW = 5 * modeTabW + 4 * tabGap;
				int modeStartX = rightEdge - modeTabsTotalW - g_data.RDPI(2);

				SafeSetWindowPos(m_btnCallAuction, modeStartX, tabY, modeTabW, tabH);
				SafeShowWindow(m_btnCallAuction, true);

				SafeSetWindowPos(m_btnTimeLine, modeStartX + (modeTabW + tabGap), tabY, modeTabW, tabH);
				SafeShowWindow(m_btnTimeLine, true);

				SafeSetWindowPos(m_btnKLine, modeStartX + (modeTabW + tabGap) * 2, tabY, modeTabW, tabH);
				SafeShowWindow(m_btnKLine, true);

				SafeSetWindowPos(m_btnWeekKLine, modeStartX + (modeTabW + tabGap) * 3, tabY, modeTabW, tabH);
				SafeShowWindow(m_btnWeekKLine, true);

				SafeSetWindowPos(m_btnMonthKLine, modeStartX + (modeTabW + tabGap) * 4, tabY, modeTabW, tabH);
				SafeShowWindow(m_btnMonthKLine, true);
			}
			else
			{
				SafeShowWindow(m_btnIndicatorCJL, false);
				SafeShowWindow(m_btnIndicatorMACD, false);
				SafeShowWindow(m_btnIndicatorKDJ, false);
				SafeShowWindow(m_btnIndicatorRSI, false);
				SafeShowWindow(m_btnIndicatorWR, false);

				SafeShowWindow(m_btnCallAuction, false);
				SafeShowWindow(m_btnTimeLine, false);
				SafeShowWindow(m_btnKLine, false);
				SafeShowWindow(m_btnWeekKLine, false);
				SafeShowWindow(m_btnMonthKLine, false);
			}
			SafeShowWindow(m_btnMA, false);
			SafeShowWindow(m_btnBoll, false);

			// 主标题栏右侧按钮定位（关闭按钮、放大按钮、股票列表切换按钮）
			{
				int closeBtnW = g_data.RDPI(20);
				int closeBtnH = g_data.RDPI(18);
				int top = g_data.RDPI(2);
				SafeSetWindowPos(m_btnClose, w - closeBtnW, top, closeBtnW, closeBtnH);
				SafeSetWindowPos(m_btnExpand, w - closeBtnW * 2, top, closeBtnW, closeBtnH);
				SafeSetWindowPos(m_btnToggleStockList, w - closeBtnW * 3, top, closeBtnW, closeBtnH);
			}

			// 盘口标题栏右侧按钮定位（筹码峰、盘口按钮）
			{
				int obTitleH = g_data.RDPI(16);
				int obBtnW = g_data.RDPI(34);
				int obBtnH = min(obTitleH, g_data.RDPI(16));
				int obBtnTop = headerHeight + relatedBarHeight + (obTitleH - obBtnH) / 2;
				bool showObBtns = !isIndexKLine;
				SafeSetWindowPos(m_btnChipPeak, w - obBtnW, obBtnTop, obBtnW, obBtnH);
				SafeShowWindow(m_btnChipPeak, showObBtns);
				SafeSetWindowPos(m_btnOrderBook, w - obBtnW * 2, obBtnTop, obBtnW, obBtnH);
				SafeShowWindow(m_btnOrderBook, showObBtns);
			}

			// 右侧盘口高度：不减xAxisLabelHeight（那是左侧走势图的时间标签，右侧不需要）
			if (m_showChipPeak)
			{
				STOCK::StockInfo drawRealtime = realtimeData;
				STOCK::ChipDistribution drawChip = chipData;
				std::vector<STOCK::TimelinePoint> drawTimeline = timelinePoint;

				if (m_viewMode >= UI_VIEW_DAY_KLINE)
				{
					// K线模式（日K/周K/月K）：根据悬停点或当前滚动位置动态推算历史筹码分布
					int targetIdx = -1;
					if (m_hoveredBarIndex >= 0)
					{
						targetIdx = min(static_cast<int>(klineData.size()) - 1, startIndex + m_hoveredBarIndex);
					}
					else if (!klineData.empty())
					{
						targetIdx = min(static_cast<int>(klineData.size()) - 1, startIndex + visibleCount - 1);
					}

					if (targetIdx >= 0 && targetIdx < static_cast<int>(klineData.size()))
					{
						STOCK::ChipDistribution dynamicChip;
						if (CDataManager::CalculateChipDistributionForKLines(m_stock_id, klineData, targetIdx, drawRealtime.circulatingAShares, dynamicChip))
						{
							drawChip = dynamicChip;
						}
						drawRealtime.currentPrice = klineData[targetIdx].close;
						drawRealtime.prevClosePrice = targetIdx > 0 ? klineData[targetIdx - 1].close : klineData[targetIdx].open;
					}
				}
				else if (m_viewMode < UI_VIEW_DAY_KLINE)
				{
					// 分时模式：若鼠标悬停在特定分时柱，则衰减计算仅截取到该分钟
					if (m_hoveredBarIndex >= 0)
					{
						int targetIdx = min(static_cast<int>(timelinePoint.size()) - 1, startIndex + m_hoveredBarIndex);
						if (targetIdx >= 0 && targetIdx < static_cast<int>(timelinePoint.size()))
						{
							drawTimeline.assign(timelinePoint.begin(), timelinePoint.begin() + targetIdx + 1);
							drawRealtime.currentPrice = timelinePoint[targetIdx].price > 0 ? timelinePoint[targetIdx].price : timelinePoint[targetIdx].averagePrice;
						}
					}
				}

				m_chipPeakPanel.Draw(memDC, chartWidth, w, h - headerHeight - indexBarHeight - relatedBarHeight, drawRealtime, drawChip, drawTimeline, m_viewMode);
			}				else if (IsInfoPanelVisible(isIndexKLine))
					m_orderBookPanel.Draw(memDC, chartWidth, w, h - headerHeight - indexBarHeight - relatedBarHeight, realtimeData, klineData, m_viewMode);
		}
		else
		{
			CPen pMiddleLine(PS_DASHDOT, 1, COLOR_GRAY_MIDDLE);
			memDC.SelectObject(&pMiddleLine);
			memDC.SetTextColor(COLOR_GRAY_PURPLE);
			memDC.TextOut((chartWidth - memDC.GetTextExtent(loading_state_txt).cx) / 2, headerHeight + g_data.RDPI(10), loading_state_txt);
		}


		// 绘制底部系统状态栏
		{
			int bottomBarY = h - indexBarHeight;
			memDC.FillSolidRect(0, bottomBarY, w, indexBarHeight, COLOR_BG_HEADER);
			memDC.FillSolidRect(0, bottomBarY, w, 1, COLOR_DARK_GRAY_BORDER);
			memDC.SetBkMode(TRANSPARENT);
			m_statusBarPanel.DrawSystemStatusBar(memDC, w, bottomBarY, indexBarHeight);
		}
	} // end if (m_viewMode != UI_VIEW_OVERVIEW)

	if (m_viewMode == UI_VIEW_OVERVIEW)
	{
		const int headerHeight = g_data.RDPI(26);

		// 计算状态栏高度
		CSize textSize = memDC.GetTextExtent(_T("Ay"));
		const int statusBarHeight = textSize.cy + g_data.RDPI(6);

		// 计算大盘指数区域高度（从 GetStatusBarStockCodes() 获取已选大盘指数）
		std::vector<std::wstring> indexCodes = g_data.GetStatusBarStockCodes();
		if (indexCodes.empty())
		{
			indexCodes = { L"sh000001", L"sz399001", L"sz399006", L"sh000688", L"sh000300" };
		}
		std::vector<std::pair<std::wstring, STOCK::StockInfo>> indices;
		{
			std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
			for (const auto& code : indexCodes)
			{
				auto stockData = g_data.GetStockData(code);
				if (stockData && stockData->info.is_ok)
					indices.push_back({ code, stockData->info });
			}
		}
		const int indexCount = (int)indices.size();
		const int indexSectionHeight = indexCount > 0 ? g_data.RDPI(56) : 0;

		auto stockCodes = g_data.m_setting_data.m_stock_codes;
		int totalRows = (int)stockCodes.size();
		int totalTableH = headerHeight + totalRows * headerHeight;

		// 可滚动区域 = 总高度 - 表头 - 状态栏 - 指数区域
		int availableHeight = h - headerHeight - statusBarHeight - indexSectionHeight;
		int maxScrollOffset = max(0, totalTableH - availableHeight);

		// 限制滚动偏移
		if (m_vScrollOffset < 0) m_vScrollOffset = 0;
		if (m_vScrollOffset > maxScrollOffset) m_vScrollOffset = maxScrollOffset;

		// 绘制大盘指数区域
		if (indexCount > 0)
		{
			m_overviewPanel.DrawIndexSection(memDC, 0, headerHeight, w, indices);
		}

		// 绘制表格（从指数区域下方开始，间距3像素在表格外部）
		int tableTop = headerHeight + indexSectionHeight + 3;
		int tableHeight = h - tableTop - statusBarHeight;
		m_overviewPanel.DrawOverviewTable(memDC, 0, tableTop, w, tableHeight, m_vScrollOffset, h, m_overviewRows);
	}

	dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

	memDC.SelectObject(pOldBitmap);
}

// ========== MACD指标绘制 ==========
// 注：以下指标计算函数已移至 CStockIndicator 类（StockIndicator.h/cpp）：
//   CalcAllRollingAvgPrices/CalcRollingAvgPrice/CalcNiceStep/CalcNiceAxisRange/
//   CalcNiceAxisRangeSymmetric/CalculateTimelineMACD/CalculateKLineMACD/
//   DetectMACDCross/GetLatestMACDCross/DetectBuySignal/DetectSellSignal
// CFloatingWnd 仅保留绘制逻辑。

// 已移至 CIndicatorChart

// 注：CalculateTimelineWR/CalculateKLineWR 已移至 CStockIndicator 类。

// ========== RSI相对强弱指标绘制 ==========
// 注：CalculateTimelineRSI/CalculateKLineRSI 已移至 CStockIndicator 类。

// ========== K线图公共辅助函数 ==========
// 已移至 CKLineChart

// 注：CalculatePeriodHighsLows 已移至 CStockIndicator 类。

// ========== K线图绘制 ==========
// 已移至 CKLineChart

// ========== 走势图绘制 ==========
// 已移至 CKLineChart

BOOL CFloatingWnd::OnEraseBkgnd(CDC* pDC)
{
	return TRUE; // 不擦除背景
}

void CFloatingWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	// 检测双击
	DWORD currentTime = GetTickCount();
	int dx = point.x - m_lastClickPos.x;
	int dy = point.y - m_lastClickPos.y;
	bool isDoubleClick = (currentTime - m_lastClickTime < GetDoubleClickTime()) &&
		(abs(dx) < 4) && (abs(dy) < 4);
	m_lastClickTime = currentTime;
	m_lastClickPos = point;

	// 顶栏分组标签点击（切换分组 / 打开“更多分组”下拉）
	if (m_viewMode != UI_VIEW_OVERVIEW && m_showStockList && point.y < g_data.RDPI(26))
	{
		for (size_t i = 0; i < m_groupTabs.size(); ++i)
		{
			if (m_groupTabs[i].rect.PtInRect(point))
			{
				if (m_groupTabs[i].isDropdown)
					ShowGroupDropdownMenu(m_groupTabs[i].rect);
				else
					SwitchFloatingGroup(m_groupTabs[i].tabIndex);
				return;
			}
		}
	}

	// 单击：点击在按钮区域不处理（让按钮自己处理）
	const int btnBarHeight = g_data.RDPI(2) + g_data.RDPI(22);  // 按钮y起始 + 按钮高度
	if (point.y < btnBarHeight)
	{
		// 在按钮区域，不处理，让子控件处理
		return;
	}

	// 左侧股票列表区域的单击/拖动处理
	if (m_viewMode != UI_VIEW_OVERVIEW && m_showStockList)
	{
		const int stockListWidth = CStockListPanel::GetPanelWidth();
		const int headerHeight = g_data.RDPI(26);
		const int relatedBarHeight = 0;  // 移除顶部关联股票栏
		const int titleH = g_data.RDPI(18);
		const int listTop = headerHeight + relatedBarHeight + titleH;
		CRect clRect;
		GetClientRect(&clRect);
		const int listBottom = clRect.Height() - g_data.RDPI(20);

		if (point.x >= 0 && point.x < stockListWidth && point.y >= listTop && point.y < listBottom)
		{
			m_isStockListDragging = true;
			m_isStockListDragMoved = false;
			m_stockListDragStartPos = point;
			m_stockListDragStartOffset = m_stockListScrollOffset;
			SetCapture();
			return;
		}
	}


	// 底部系统状态栏指数点击切换（单行四个）
	if (m_viewMode != UI_VIEW_OVERVIEW)
	{
		CRect clRect;
		GetClientRect(&clRect);
		const int bottomBarHeight = g_data.RDPI(20);
		int bottomBarY = clRect.Height() - bottomBarHeight;
		if (point.y >= bottomBarY && point.y < clRect.Height())
		{
			std::vector<std::wstring> statusBarCodes = g_data.GetStatusBarStockCodes();
			if (statusBarCodes.empty())
			{
				statusBarCodes = { L"sh000001", L"sz399001", L"sz399006", L"sh000688", L"sh000300" };
			}
			const int COLS = static_cast<int>(statusBarCodes.size());
			if (COLS > 0)
			{
				const int colW = clRect.Width() / COLS;
				int col = point.x / max(colW, 1);
				if (col >= 0 && col < COLS)
				{
					const std::wstring& targetCode = statusBarCodes[col];
					if (targetCode != m_stock_id)
					{
						SetStockId(targetCode);
						UpdateModeButtons();
					}
					return;
				}
			}
		}
	}

	// 总览模式下的鼠标点击处理
	if (m_viewMode == UI_VIEW_OVERVIEW)
	{
		// 双击表头：弹出增加股票对话框
		const int tableHeaderHeight = g_data.RDPI(26);
		if (isDoubleClick && point.y >= tableHeaderHeight && point.y < 2 * tableHeaderHeight)
		{
			PostMessage(FWND_MSG_SHOW_ADD_DLG);
			return;
		}

		if (m_overviewRows.empty())
			return;

		for (const auto& rowInfo : m_overviewRows)
		{
			if (rowInfo.code.empty())
				continue;

			if (point.y >= rowInfo.rowY && point.y < rowInfo.rowY + rowInfo.rowH)
			{
				if (point.x >= 0 && point.x < rowInfo.nameColWidth)
				{
					// 单击名称列：切换到走势图
					m_viewMode = UI_VIEW_TIMELINE;
					m_showChipPeak = false;
					SetStockId(rowInfo.code);
					UpdateModeButtons();
					UpdatePeriodComboVisibility();
					return;
				}
				else if (rowInfo.deleteBtnStartX > 0 && point.x >= rowInfo.deleteBtnStartX && point.x <= rowInfo.deleteBtnEndX)
				{
					// 点击删除按钮：删除该股票
					auto& stockCodes = g_data.m_setting_data.m_stock_codes;
					auto it = std::find(stockCodes.begin(), stockCodes.end(), rowInfo.code);
					if (it != stockCodes.end())
					{
						stockCodes.erase(it);
						g_data.SaveConfig();

						m_vScrollOffset = 0;
						Invalidate();
						return;
					}
				}
				else if (isDoubleClick)
				{
					// 双击非名称列、非删除按钮列：延迟弹出股票编辑对话框
					m_pendingEditStockCode = rowInfo.code;
					PostMessage(FWND_MSG_SHOW_EDIT_DLG);
					return;
				}
			}
		}
	}

	// 非总览模式下的分时图双击（所有模式都支持）
	if (m_viewMode != UI_VIEW_OVERVIEW && isDoubleClick)
	{
		CRect rect;
		GetClientRect(&rect);
		bool isIndex = (GetStockPriority(m_stock_id) < 200);
		bool isIndexKLine = isIndex && m_viewMode >= UI_VIEW_DAY_KLINE;
		const int orderBookWidth = IsInfoPanelVisible(isIndexKLine) ? ORDER_BOOK_WIDTH : 0;
		const int chartWidth = rect.Width() - orderBookWidth;
		const int headerHeight = g_data.RDPI(26);
		const int relatedBarHeight = 0;  // 移除顶部关联股票栏

		const int yAxisWidth = g_data.RDPI(50);
		const int stockListWidth = m_showStockList ? CStockListPanel::GetPanelWidth() : 0;
		const int chartLeft = stockListWidth + yAxisWidth;
		const int chartRight = chartWidth;

		if (point.x >= chartLeft && point.x < chartRight && point.y >= headerHeight + relatedBarHeight)
		{
			std::vector<STOCK::TimelinePoint> timelinePoint;
			{
				std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
				auto stockData = g_data.GetStockData(m_stock_id);
				if (stockData)
				{
					if (m_viewMode == UI_VIEW_DAY_KLINE || m_viewMode == UI_VIEW_WEEK_KLINE || m_viewMode == UI_VIEW_MONTH_KLINE)
					{
						STOCK::KLineData* klineObj = nullptr;
						if (m_viewMode == UI_VIEW_DAY_KLINE)
							klineObj = stockData->getKLineData();
						else if (m_viewMode == UI_VIEW_WEEK_KLINE)
							klineObj = stockData->getWeekKLineData();
						else if (m_viewMode == UI_VIEW_MONTH_KLINE)
							klineObj = stockData->getMonthKLineData();

						if (klineObj)
						{
							for (const auto& kp : klineObj->data)
							{
								STOCK::TimelinePoint tp;
								if (kp.day.length() >= 10)
									tp.time = kp.day.substr(5, 5);
								else
									tp.time = kp.day;
								tp.fullTime = kp.day;
								tp.price = kp.close;
								tp.openPrice = kp.open;
								tp.volume = kp.volume;
								timelinePoint.push_back(tp);
							}
						}
					}
					else
					{
						auto timelineData = stockData->getTimelineData();
						if (timelineData)
						{
							timelinePoint = timelineData->data;
						}
					}
				}
			}

			if (!timelinePoint.empty())
			{
				int totalPoints = static_cast<int>(timelinePoint.size());
				int visibleCount = min(m_timelineVisibleCount, totalPoints);
				int maxOffset = max(0, totalPoints - visibleCount);
				if (m_timelineScrollOffset < 0 || m_timelineScrollOffset >= maxOffset)
					m_timelineScrollOffset = maxOffset;
				int startIndex = max(0, min(m_timelineScrollOffset, maxOffset));

				int adjX = point.x - chartLeft;
				int effectiveWidth = chartRight - chartLeft;
				int relIndex = static_cast<int>(adjX * static_cast<float>(visibleCount) / effectiveWidth);
				relIndex = max(0, min(relIndex, visibleCount - 1));
				int countX = startIndex + relIndex;
				countX = max(0, min(countX, totalPoints - 1));

				const auto& item = timelinePoint[countX];
				m_pendingTradeTime = item.time.c_str();
				m_pendingTradePrice = item.price;
				// PostMessage(FWND_MSG_SHOW_TRADE_DLG);  // 测试买卖点检测时暂时屏蔽交易记录弹窗
				CSmartSignalTestDlg::Show(m_stock_id, countX, m_viewMode, m_pendingTradePrice, m_pendingTradeTime, this);
				return;
			}
		}
	}

	// 拖动启动：在图表区域内按下左键开始拖动滚动
	{
		CRect dragRect;
		GetClientRect(&dragRect);
		bool isIdx = (GetStockPriority(m_stock_id) < 200);
		bool isIdxKLine = isIdx && m_viewMode >= UI_VIEW_DAY_KLINE;
		const int dragOrderBookWidth = IsInfoPanelVisible(isIdxKLine) ? ORDER_BOOK_WIDTH : 0;
		const int dragChartWidth = dragRect.Width() - dragOrderBookWidth;
		const int dragYAxisWidth = g_data.RDPI(50);
		const int dragStockListWidth = m_showStockList ? CStockListPanel::GetPanelWidth() : 0;
		const int dragChartLeft = dragStockListWidth + dragYAxisWidth;
		const int dragHeaderHeight = g_data.RDPI(26);  // 标题栏
		const int dragIndexBarHeight = g_data.RDPI(20);
		if (m_viewMode != UI_VIEW_OVERVIEW && point.x >= dragChartLeft && point.x < dragChartWidth && point.y >= dragHeaderHeight && point.y < dragRect.Height() - dragIndexBarHeight)
		{
			// 分时图拖动（5分钟K线模式和日K线模式也使用分时拖动逻辑）
			{
				m_isTimelineDragging = true;
				m_timelineDragStartPos = point;
				m_timelineDragStartOffset = m_timelineScrollOffset;
			}
			SetCapture();
			m_hPrevCursor = SetCursor(LoadCursor(NULL, IDC_SIZEALL));
		}
	}

	// 点击在窗口外部则关闭
	CPoint ptScreen = point;
	ClientToScreen(&ptScreen);
	CRect rcWindow;
	GetWindowRect(rcWindow);
	if (!rcWindow.PtInRect(ptScreen))
	{
		DestroyWindow();
		Stock::Instance().DestroyFloatingWnd();
	}

	CWnd::OnLButtonDown(nFlags, point);
}

void CFloatingWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_isStockListDragging)
	{
		m_isStockListDragging = false;
		ReleaseCapture();

		if (!m_isStockListDragMoved)
		{
			// 点击切换股票
			const int headerHeight = g_data.RDPI(26);
			const int relatedBarHeight = 0;
			const int titleH = g_data.RDPI(18);
			const int rowHeight = CStockListPanel::GetRowHeight();
			const int listTop = headerHeight + relatedBarHeight + titleH;

			int contentY = (point.y - listTop) + m_stockListScrollOffset;
			if (contentY >= 0)
			{
				int rowIndex = contentY / rowHeight;
				std::vector<std::wstring> stockCodes = CStockListPanel::GetStockListCodes(CStockListPanel::ClampGroupTab(m_activeGroupTab));
				if (rowIndex >= 0 && rowIndex < static_cast<int>(stockCodes.size()))
				{
					const std::wstring& clickedCode = stockCodes[rowIndex];
					if (clickedCode != m_stock_id)
					{
						SetStockId(clickedCode);
						UpdateModeButtons();
					}
				}
			}
		}
		Invalidate();
		CWnd::OnLButtonUp(nFlags, point);
		return;
	}

	bool wasDragging = m_isTimelineDragging || m_isKLineDragging;
	m_isTimelineDragging = false;
	m_isKLineDragging = false;
	if (wasDragging)
	{
		ReleaseCapture();
		if (m_hPrevCursor)
		{
			SetCursor(m_hPrevCursor);
			m_hPrevCursor = NULL;
		}
		Invalidate();
	}
	CWnd::OnLButtonUp(nFlags, point);
}

// ========== KDJ 指标绘制 ==========
// 注：CalculateKDJ/CalculateTimelineKDJ 已移至 CStockIndicator 类。

// 已移至 CIndicatorChart

// 已移至 CKLineChart

// 已移至 CTimelineChart

// 已移至 CIndicatorChart

void CFloatingWnd::OnRButtonDown(UINT nFlags, CPoint point)
{
	if (m_viewMode != UI_VIEW_OVERVIEW)
	{
		m_viewMode = UI_VIEW_OVERVIEW;
		m_showChipPeak = false;
		UpdateModeButtons();
		UpdatePeriodComboVisibility();
		Invalidate();
	}
	else
	{
		m_viewMode = UI_VIEW_TIMELINE;
		m_showChipPeak = false;
		m_showJZCurve = CCommon::IsFundCode(m_stock_id);  // 基金默认显示净值曲线
		UpdateModeButtons();
		UpdatePeriodComboVisibility();
		Invalidate();
	}
}

void CFloatingWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	m_mousePos = point;

	// 顶栏分组标签悬停跟踪（进入时申请 WM_MOUSELEAVE）
	if (!m_trackingTabHover)
	{
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, GetSafeHwnd(), 0 };
		if (TrackMouseEvent(&tme))
			m_trackingTabHover = true;
	}
	UpdateGroupTabHover(point);

	// 左侧股票列表拖动处理
	if (m_isStockListDragging)
	{
		int dy = point.y - m_stockListDragStartPos.y;
		if (abs(dy) > 3)
		{
			m_isStockListDragMoved = true;
		}

		CRect clRect;
		GetClientRect(&clRect);
		const int headerHeight = g_data.RDPI(26);
		const int relatedBarHeight = 0;
		const int indexBarHeight = g_data.RDPI(20);
		const int titleH = g_data.RDPI(18);
		int listAreaH = (clRect.Height() - headerHeight - relatedBarHeight - indexBarHeight) - titleH;
		const int rowHeight = CStockListPanel::GetRowHeight();
		std::vector<std::wstring> stockCodes = CStockListPanel::GetStockListCodes(CStockListPanel::ClampGroupTab(m_activeGroupTab));
		int totalH = static_cast<int>(stockCodes.size()) * rowHeight;
		int maxOffset = max(0, totalH - listAreaH);

		int newOffset = m_stockListDragStartOffset - dy;
		newOffset = max(0, min(newOffset, maxOffset));
		if (newOffset != m_stockListScrollOffset)
		{
			m_stockListScrollOffset = newOffset;
			Invalidate();
		}
		CWnd::OnMouseMove(nFlags, point);
		return;
	}

	// 拖动滚动处理
	if (m_isTimelineDragging || m_isKLineDragging)
	{
		int dx = 0;
		if (m_isTimelineDragging)
		{
			dx = point.x - m_timelineDragStartPos.x;
			// 计算可见范围
			std::vector<STOCK::TimelinePoint> timelinePoint;
			{
				std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
				auto stockData = g_data.GetStockData(m_stock_id);
				if (stockData)
				{
					if (m_viewMode == UI_VIEW_DAY_KLINE || m_viewMode == UI_VIEW_WEEK_KLINE || m_viewMode == UI_VIEW_MONTH_KLINE)
					{
						STOCK::KLineData* klineObj = nullptr;
						if (m_viewMode == UI_VIEW_DAY_KLINE)
							klineObj = stockData->getKLineData();
						else if (m_viewMode == UI_VIEW_WEEK_KLINE)
							klineObj = stockData->getWeekKLineData();
						else if (m_viewMode == UI_VIEW_MONTH_KLINE)
							klineObj = stockData->getMonthKLineData();

						if (klineObj)
						{
							for (const auto& kp : klineObj->data)
							{
								STOCK::TimelinePoint tp;
								if (kp.day.length() >= 10)
									tp.time = kp.day.substr(5, 5);  // "MM-DD"
								else
									tp.time = kp.day;
								tp.fullTime = kp.day;
								tp.price = kp.close;
								tp.openPrice = kp.open;
								tp.volume = kp.volume;
								timelinePoint.push_back(tp);
							}
						}
					}
					else
					{
						auto timelineData = stockData->getTimelineData();
						if (timelineData)
							timelinePoint = timelineData->data;
					}
				}
			}
			int totalPoints = static_cast<int>(timelinePoint.size());
			int visibleCount = min(m_timelineVisibleCount, totalPoints);
			int maxOffset = max(0, totalPoints - visibleCount);
			// 像素偏移转换为数据点偏移
			CRect clientRect;
			GetClientRect(&clientRect);
			int yAxisW = g_data.RDPI(50);
			int effectiveWidth = clientRect.Width() - yAxisW;
			int pointsDelta = static_cast<int>(dx * static_cast<float>(visibleCount) / effectiveWidth);
			int newOffset = m_timelineDragStartOffset - pointsDelta;
			newOffset = max(0, min(newOffset, maxOffset));
			if (newOffset != m_timelineScrollOffset)
			{
				m_timelineScrollOffset = newOffset;
				Invalidate();
			}
		}
		else if (m_isKLineDragging)
		{
			dx = point.x - m_klineDragStartPos.x;
			// 每拖动一个 barWidth + gap 像素滚动一根K线
			const int minBarWidth = 7;
			const int gap = 1;
			int deltaBars = dx / (minBarWidth + gap);
			int newOffset = m_klineDragStartOffset - deltaBars;
			if (newOffset < 0) newOffset = 0;
			if (newOffset != m_scrollOffset)
			{
				m_scrollOffset = newOffset;
				Invalidate();
			}
		}
		// 拖动期间不进行 hover 检测，直接返回
		CWnd::OnMouseMove(nFlags, point);
		return;
	}

	CRect rect;
	GetClientRect(&rect);
	bool isIndex = (GetStockPriority(m_stock_id) < 200);
	bool isIndexKLine = isIndex && m_viewMode >= UI_VIEW_DAY_KLINE;
	const int orderBookWidth = IsInfoPanelVisible(isIndexKLine) ? ORDER_BOOK_WIDTH : 0;
	const int chartWidth = rect.Width() - orderBookWidth;
	const int yAxisWidth = g_data.RDPI(50);
	const int stockListWidth = m_showStockList ? CStockListPanel::GetPanelWidth() : 0;
	const int chartLeft = stockListWidth + yAxisWidth;
	const int chartRight = chartWidth;
	const int headerHeight = g_data.RDPI(26);
	const int xAxisLabelHeight = g_data.RDPI(20);
	const int singleBarHeight = g_data.RDPI(20);
	const int relatedBarHeight = 0;  // 移除顶部关联股票栏
	const int indexBarHeight = singleBarHeight;    // 底部系统状态栏高度（单行4个）

	// 统一布局：标题栏 + 走势图(2/5) + 成交量(1/5) + MACD(1/5) + KDJ(1/5) + 时间标签 + 底部系统状态栏
	int chartArea = rect.Height() - headerHeight - relatedBarHeight - xAxisLabelHeight - indexBarHeight;
	int priceChartHeight = chartArea * 2 / 5;
	int volumeChartHeight = chartArea / 5;
	int macdChartHeight = chartArea / 5;
	int kdjChartHeight = chartArea / 5;

	m_isHoveringVolume = false;
	int prevHoveredBarIndex = m_hoveredBarIndex;
	m_hoveredBarIndex = -1;
	bool prevHoveringKLine = m_isHoveringKLine;
	bool prevHoveringKLineVolume = m_isHoveringKLineVolume;
	int prevKlineHoveredBarIndex = m_klineHoveredBarIndex;
	m_isHoveringKLine = false;
	m_isHoveringKLineVolume = false;
	m_klineHoveredBarIndex = -1;

	if ((m_viewMode == UI_VIEW_DAY_KLINE) && false)  // 日K线模式现在走分时悬停逻辑
	{
		std::vector<STOCK::KLinePoint> klineData;
		{
			std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
			auto stockData = g_data.GetStockData(m_stock_id);
			if (stockData)
			{
				auto klineObj = stockData->getKLineData();
				if (klineObj)
				{
					klineData = klineObj->data;
				}
			}
		}

		if (!klineData.empty() && point.x >= chartLeft && point.x < chartRight)
		{
			// 使用与绘制完全一致的参数计算（x从chartLeft开始，宽度为chartWidth-chartLeft）
			const int paddingY = g_data.RDPI(10);
			CKLineChart::HoverState klineHover;
			klineHover.isHoveringKLine = m_isHoveringKLine;
			klineHover.isHoveringKLineVolume = m_isHoveringKLineVolume;
			klineHover.isHoveringKDJ = m_isHoveringKDJ;
			klineHover.klineHoveredBarIndex = m_klineHoveredBarIndex;
			klineHover.klineHoverTip = m_klineHoverTip;
			klineHover.klineVolumeHoverTip = m_klineVolumeHoverTip;
			klineHover.klineTrendHoverTip = m_klineTrendHoverTip;
			klineHover.kdjHoverTip = m_kdjHoverTip;
			klineHover.showMA = m_showMA;
			klineHover.showBollBands = m_showBollBands;
			klineHover.showTrendView = m_showTrendView;
			klineHover.viewMode = m_viewMode;
			klineHover.klinePeriodDays = m_klinePeriodDays;
			klineHover.scrollOffset = m_scrollOffset;
			klineHover.stockId = m_stock_id;
			KLineDrawData drawData = m_kLineChart.PrepareKLineDrawData(chartLeft, headerHeight + paddingY, chartWidth - chartLeft, priceChartHeight - paddingY * 2, klineData, klineHover);

			if (point.y >= headerHeight && point.y < headerHeight + priceChartHeight)
			{
				// 鼠标在K线图上 - 使用与绘制一致的参数定位
				int barIndex = -1;
				int totalBars = klineData.size() - drawData.finalStartIndex;
				if (totalBars > 0 && drawData.barWidth + drawData.gap > 0)
				{
					barIndex = drawData.finalStartIndex + (point.x - drawData.x) / (drawData.barWidth + drawData.gap);
					barIndex = max(drawData.finalStartIndex, min(barIndex, (int)klineData.size() - 1));
				}

				if (barIndex >= 0)
				{
					m_isHoveringKLine = true;
					m_klineHoveredBarIndex = barIndex;

					const auto& item = klineData[barIndex];
					m_klineHoverTip.Format(_T("开:%s  收:%s  高:%s  低:%s"),
						CCommon::FormatFloat(item.open),
						CCommon::FormatFloat(item.close),
						CCommon::FormatFloat(item.high),
						CCommon::FormatFloat(item.low));

					m_klineTrendHoverTip.Format(_T("收:%s  最高:%s  最低:%s"),
						CCommon::FormatFloat(item.close),
						CCommon::FormatFloat(item.high),
						CCommon::FormatFloat(item.low));

					// 同时设置量柱提示，实现同步显示
					STOCK::Volume volumeLots = item.volume / 100;
					CString volumeStr = CCommon::FormatVolumeInt(volumeLots);
					m_klineVolumeHoverTip.Format(_T("成交量:%s"),
						volumeStr);
				}
			}
			else
			{
				// 统一布局：成交量图紧贴走势图
				int volumeY = headerHeight + priceChartHeight;
				if (point.y >= volumeY && point.y < volumeY + volumeChartHeight)
				{
					// 鼠标在量柱图上 - 使用与绘制一致的参数定位
					int barIndex = -1;
					int totalBars = klineData.size() - drawData.finalStartIndex;
					if (totalBars > 0 && drawData.barWidth + drawData.gap > 0)
					{
						barIndex = drawData.finalStartIndex + (point.x - drawData.x) / (drawData.barWidth + drawData.gap);
						barIndex = max(drawData.finalStartIndex, min(barIndex, (int)klineData.size() - 1));
					}

					if (barIndex >= 0)
					{
						m_isHoveringKLineVolume = true;
						m_klineHoveredBarIndex = barIndex;

						const auto& item = klineData[barIndex];
						STOCK::Volume volumeLots = item.volume / 100;
						CString volumeStr = CCommon::FormatVolumeInt(volumeLots);
						m_klineVolumeHoverTip.Format(_T("成交量:%s"),
							volumeStr);

						// 同时设置K线提示，实现同步显示
						m_klineHoverTip.Format(_T("开:%s  收:%s  高:%s  低:%s"),
							CCommon::FormatFloat(item.open),
							CCommon::FormatFloat(item.close),
							CCommon::FormatFloat(item.high),
							CCommon::FormatFloat(item.low));

						m_klineTrendHoverTip.Format(_T("收:%s  最高:%s  最低:%s"),
							CCommon::FormatFloat(item.close),
							CCommon::FormatFloat(item.high),
							CCommon::FormatFloat(item.low));
					}
				}
			}

			// 只在悬停状态变化时重绘图表区域，避免按钮闪烁
			bool hoverChanged = (m_isHoveringKLine != prevHoveringKLine ||
				m_isHoveringKLineVolume != prevHoveringKLineVolume ||
				m_klineHoveredBarIndex != prevKlineHoveredBarIndex);
			if (hoverChanged)
			{
				InvalidateRect(CRect(0, headerHeight, chartWidth, rect.Height()));
			}
		}
	}
	else
	{
		std::vector<STOCK::TimelinePoint> timelinePoint;
		{
			std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
			auto stockData = g_data.GetStockData(m_stock_id);
			if (stockData)
			{
				if (m_viewMode == UI_VIEW_DAY_KLINE || m_viewMode == UI_VIEW_WEEK_KLINE || m_viewMode == UI_VIEW_MONTH_KLINE)
				{
					STOCK::KLineData* klineObj = nullptr;
					if (m_viewMode == UI_VIEW_DAY_KLINE)
						klineObj = stockData->getKLineData();
					else if (m_viewMode == UI_VIEW_WEEK_KLINE)
						klineObj = stockData->getWeekKLineData();
					else if (m_viewMode == UI_VIEW_MONTH_KLINE)
						klineObj = stockData->getMonthKLineData();

					if (klineObj)
					{
						for (const auto& kp : klineObj->data)
						{
							STOCK::TimelinePoint tp;
							if (kp.day.length() >= 10)
								tp.time = kp.day.substr(5, 5);  // "MM-DD"
							else
								tp.time = kp.day;
							tp.fullTime = kp.day;
							tp.price = kp.close;
							tp.openPrice = kp.open;
							tp.averagePrice = kp.close;
							tp.volume = kp.volume;
							tp.amount = static_cast<STOCK::Amount>(kp.volume) * kp.close;
							timelinePoint.push_back(tp);
						}
					}
				}
				else
				{
					auto timelineData = stockData->getTimelineData();
					if (timelineData)
					{
						timelinePoint = timelineData->data;
					}
				}
			}
		}

		if (!timelinePoint.empty() && point.x >= chartLeft && point.x < chartRight)
		{
			// 计算可见范围（与OnPaint一致）
			int totalPoints = static_cast<int>(timelinePoint.size());
			int visibleCount = min(m_timelineVisibleCount, totalPoints);
			int maxOffset = max(0, totalPoints - visibleCount);
			// 自动跟随：如果当前在末尾或需要自动滚动
			if (m_timelineScrollOffset < 0 || m_timelineScrollOffset >= maxOffset)
				m_timelineScrollOffset = maxOffset;
			int startIndex = max(0, min(m_timelineScrollOffset, maxOffset));

			// 构建可见子向量并计算MA值（与OnPaint一致）
			CStockIndicator::CalcAllRollingAvgPrices(timelinePoint);
			auto subStart = timelinePoint.begin() + startIndex;
			auto subEnd = timelinePoint.begin() + startIndex + visibleCount;
			std::vector<STOCK::TimelinePoint> subTimeline(subStart, subEnd);

			// 鼠标坐标减去图表左边界，对应到分时图内部坐标
			int adjX = point.x - chartLeft;
			int effectiveWidth = chartRight - chartLeft;
			// 按索引比例计算鼠标对应的可见数据索引
			// 分时模式X轴基于m_timelineVisibleCount固定格数，K线模式基于实际数据点数
			int xSlotCount = (m_viewMode >= UI_VIEW_DAY_KLINE) ? visibleCount : m_timelineVisibleCount;
			int relIndex = static_cast<int>(adjX * static_cast<float>(xSlotCount) / effectiveWidth);
			relIndex = max(0, min(relIndex, visibleCount - 1));

			if (relIndex >= 0 && relIndex < static_cast<int>(subTimeline.size()))
			{
				m_isHoveringVolume = true;
				m_hoveredBarIndex = relIndex;
				m_hoveredData = subTimeline[relIndex];

				// 保存hover点的MA值
				m_hoverMa1 = m_hoveredData.price;
				m_hoverMaValues = m_hoveredData.maValues;
				// 保存前一点MA值（用于箭头方向）
				m_hoverPrevMa1 = 0;
				if (relIndex > 0)
				{
					m_hoverPrevMa1 = subTimeline[relIndex - 1].price;
				}

				CString timeStr(m_hoveredData.time.c_str());
				STOCK::Volume volumeLots = m_hoveredData.volume / 100;
				CString volumeStr = CCommon::FormatVolumeInt(volumeLots);

				double amount = static_cast<double>(m_hoveredData.volume) * m_hoveredData.price;
				CString amountStr = CCommon::FormatAmount(amount);

				m_hoverTip.Format(_T("%s %s %s"), timeStr, volumeStr, amountStr);
				// 设置量柱图标题栏悬停提示：显示鼠标指向位置的分量和分额
				m_timelineVolumeTitleTip.Format(_T("分量:%s 分额:%s"), volumeStr, amountStr);

				// 设置MACD/KDJ/W&R/RSI标题栏悬停提示
				// MACD固定显示，始终计算悬停提示（用完整数据确保EMA收敛）
				{
					int shortP = 12, longP = 26, signalP = 9;
					if (m_viewMode == UI_VIEW_TIMELINE)
					{
						shortP = 6; longP = 12; signalP = 4;
					}
					auto macdData = CStockIndicator::CalculateTimelineMACD(timelinePoint, shortP, longP, signalP);
					int globalIdx = startIndex + relIndex;
					if (globalIdx < static_cast<int>(macdData.size()) && macdData[globalIdx].valid)
					{
						auto formatMACDValue = [](double val) -> CString {
							CString s;
							double absVal = std::abs(val);
							if (absVal < 0.001 && absVal > 0)
								s.Format(_T("%.5f"), val);
							else if (absVal < 0.01)
								s.Format(_T("%.4f"), val);
							else
								s.Format(_T("%.3f"), val);
							return s;
							};
						m_timelineMacdTitleTip.Format(_T("DIF:%s DEA:%s"), formatMACDValue(macdData[globalIdx].dif), formatMACDValue(macdData[globalIdx].dea));
					}
				}
				if (m_timelineIndicator == TimelineIndicator::KDJ)
				{
					// 分时(1分钟)用7,3,3参数，日K/周K/月K用默认9,3,3
					int kdjN = 9, kdjM1 = 3, kdjM2 = 3;
					if (m_viewMode == UI_VIEW_TIMELINE)
					{
						kdjN = 7; kdjM1 = 3; kdjM2 = 3;
					}
					auto kdjData = CStockIndicator::CalculateTimelineKDJ(subTimeline, kdjN, kdjM1, kdjM2);
					if (relIndex < static_cast<int>(kdjData.size()) && kdjData[relIndex].valid)
					{
						m_timelineKdjTitleTip.Format(_T("K:%.1f D:%.1f J:%.1f"), kdjData[relIndex].k, kdjData[relIndex].d, kdjData[relIndex].j);
					}
					m_timelineWrTitleTip.Empty();
					m_timelineRsiTitleTip.Empty();
				}
				else if (m_timelineIndicator == TimelineIndicator::WR)
				{
					// WR悬停提示
					auto wrData = CStockIndicator::CalculateTimelineWR(subTimeline);
					if (relIndex < static_cast<int>(wrData.size()) && wrData[relIndex].valid)
					{
						m_timelineWrTitleTip.Format(_T("WR6:%.1f WR14:%.1f"), wrData[relIndex].wr1, wrData[relIndex].wr2);
					}
					m_timelineKdjTitleTip.Empty();
					m_timelineRsiTitleTip.Empty();
				}
				else if (m_timelineIndicator == TimelineIndicator::RSI)
				{
					auto rsiData = CStockIndicator::CalculateTimelineRSI(subTimeline);
					if (relIndex < static_cast<int>(rsiData.size()) && rsiData[relIndex].valid)
					{
						m_timelineRsiTitleTip.Format(_T("RSI6:%.1f RSI14:%.1f"), rsiData[relIndex].rsi1, rsiData[relIndex].rsi2);
					}
					m_timelineKdjTitleTip.Empty();
					m_timelineWrTitleTip.Empty();
				}
				else
				{
					m_timelineKdjTitleTip.Empty();
					m_timelineWrTitleTip.Empty();
					m_timelineRsiTitleTip.Empty();
				}
			}
			else
			{
				m_isHoveringVolume = false;
				m_hoveredBarIndex = -1;
				m_hoverTip.Empty();
				m_timelineVolumeTitleTip.Empty();
				m_timelineMacdTitleTip.Empty();
				m_timelineKdjTitleTip.Empty();
				m_timelineWrTitleTip.Empty();
				m_timelineRsiTitleTip.Empty();
				m_hoverMa1 = 0; m_hoverPrevMa1 = 0;
				m_hoverMaValues.clear();
			}

			// 只在悬停状态变化时重绘图表区域，避免按钮闪烁
			bool hoverChanged = (m_isHoveringVolume != (prevHoveredBarIndex >= 0) ||
				m_hoveredBarIndex != prevHoveredBarIndex);
			if (hoverChanged)
			{
				InvalidateRect(CRect(0, headerHeight, chartWidth, rect.Height()));
			}
		}
	}
}

void CFloatingWnd::SetStockId(const std::wstring& stockId)
{
	if (m_stock_id == stockId)
		return;
	m_stock_id = stockId;
	EnsureStockListVisible();
	// 通知获取线程切换关注股票，线程自动重置计时器并立即获取新股数据
	CStockFetchThread::Instance().SetFocusStockId(m_stock_id);
	m_timelineScrollOffset = -1;
	// 切换股票时重置可见点数为当前模式的默认值，避免旧值导致新股票数据显示异常
	if (m_viewMode == UI_VIEW_DAY_KLINE || m_viewMode == UI_VIEW_WEEK_KLINE || m_viewMode == UI_VIEW_MONTH_KLINE)
		m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1DAY;
	else
	{
		m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1MIN;
		// 分时模式下，根据新股票是否为基金自动切换净值曲线显示
		m_showJZCurve = CCommon::IsFundCode(m_stock_id);
	}
	Invalidate();
}

void CFloatingWnd::ToggleKLineMode()
{
	m_viewMode = (m_viewMode == UI_VIEW_DAY_KLINE) ? UI_VIEW_TIMELINE : UI_VIEW_DAY_KLINE;
	m_showBollBands = (m_viewMode != UI_VIEW_DAY_KLINE);
	m_btnBoll.SetWindowText(_T("BL"));
	m_scrollOffset = 0;
	m_timelineScrollOffset = -1;  // 自动滚动到末尾
	m_timelineVisibleCount = 30;  // 切回分时显示最新走势
	m_showTrendView = false;
	m_showChipPeak = (m_viewMode == UI_VIEW_DAY_KLINE);
	m_showMA = (m_viewMode == UI_VIEW_DAY_KLINE);
	m_showJZCurve = (m_viewMode == UI_VIEW_TIMELINE) && CCommon::IsFundCode(m_stock_id);  // 分时+基金默认显示净值
	ResetHoverState();
	UpdateModeButtons();
	UpdatePeriodComboVisibility();

	if (m_viewMode == UI_VIEW_DAY_KLINE)
	{
		// 不再重置m_klineDataLoaded，因为已在启动时预加载
		EnsureChipPeakData();
	}
	Invalidate();
}

void CFloatingWnd::UpdateModeButtons()
{
	if (m_btnTimeLine.GetSafeHwnd() && m_btnKLine.GetSafeHwnd())
	{
		if (m_btnCallAuction.GetSafeHwnd()) m_btnCallAuction.Invalidate();
		if (m_btnTimeLine.GetSafeHwnd()) m_btnTimeLine.Invalidate();
		if (m_btnKLine.GetSafeHwnd()) m_btnKLine.Invalidate();
		if (m_btnWeekKLine.GetSafeHwnd()) m_btnWeekKLine.Invalidate();
		if (m_btnMonthKLine.GetSafeHwnd()) m_btnMonthKLine.Invalidate();

		if (m_btnMA.GetSafeHwnd()) m_btnMA.Invalidate();
		if (m_btnBoll.GetSafeHwnd()) m_btnBoll.Invalidate();
		if (m_btnIndicatorCJL.GetSafeHwnd()) m_btnIndicatorCJL.Invalidate();
		if (m_btnIndicatorMACD.GetSafeHwnd()) m_btnIndicatorMACD.Invalidate();
		if (m_btnIndicatorKDJ.GetSafeHwnd()) m_btnIndicatorKDJ.Invalidate();
		if (m_btnIndicatorWR.GetSafeHwnd()) m_btnIndicatorWR.Invalidate();
		if (m_btnIndicatorRSI.GetSafeHwnd()) m_btnIndicatorRSI.Invalidate();
		if (m_btnChipPeak.GetSafeHwnd()) m_btnChipPeak.Invalidate();
		if (m_btnOrderBook.GetSafeHwnd()) m_btnOrderBook.Invalidate();

		m_btnExpand.SetWindowText(m_expandedMode ? _T("△") : _T("□"));
		if (m_btnExpand.GetSafeHwnd()) m_btnExpand.Invalidate();

		m_btnToggleStockList.SetWindowText(m_showStockList ? _T("|>") : _T("<|"));
		if (m_btnToggleStockList.GetSafeHwnd()) m_btnToggleStockList.Invalidate();
		SafeShowWindow(m_btnToggleStockList, m_viewMode != UI_VIEW_OVERVIEW);

		// 模式按钮在所有图表模式下显示（除总览模式和放大模式外）
		bool showModeBtns = (m_viewMode != UI_VIEW_OVERVIEW && !m_expandedMode);
		SafeShowWindow(m_btnCallAuction, showModeBtns);
		SafeShowWindow(m_btnTimeLine, showModeBtns);
		SafeShowWindow(m_btnKLine, showModeBtns);
		SafeShowWindow(m_btnWeekKLine, showModeBtns);
		SafeShowWindow(m_btnMonthKLine, showModeBtns);

		// 副图指标按钮在所有模式下显示（除总览模式、放大模式和竞价模式外）
		bool showIndicatorBtns = m_viewMode != UI_VIEW_OVERVIEW && !m_expandedMode && m_viewMode != UI_VIEW_AUCTION;
		SafeShowWindow(m_btnIndicatorCJL, showIndicatorBtns);
		SafeShowWindow(m_btnIndicatorMACD, showIndicatorBtns);
		SafeShowWindow(m_btnIndicatorKDJ, showIndicatorBtns);
		SafeShowWindow(m_btnIndicatorWR, showIndicatorBtns);
		SafeShowWindow(m_btnIndicatorRSI, showIndicatorBtns);
		// MA/BL/MACD按钮与指标按钮同区域，放大模式下也隐藏
		SafeShowWindow(m_btnMA, showIndicatorBtns);
		SafeShowWindow(m_btnBoll, showIndicatorBtns);
	}
}

void CFloatingWnd::UpdateIndicatorButtons()
{
	if (m_btnIndicatorCJL.GetSafeHwnd()) m_btnIndicatorCJL.Invalidate();
	if (m_btnIndicatorMACD.GetSafeHwnd()) m_btnIndicatorMACD.Invalidate();
	if (m_btnIndicatorKDJ.GetSafeHwnd()) m_btnIndicatorKDJ.Invalidate();
	if (m_btnIndicatorRSI.GetSafeHwnd()) m_btnIndicatorRSI.Invalidate();
	if (m_btnIndicatorWR.GetSafeHwnd()) m_btnIndicatorWR.Invalidate();
}

void CFloatingWnd::UpdatePeriodComboVisibility()
{
	// MA/BL/MACD按钮与指标按钮同区域，放大模式下也隐藏
	bool showIndicatorBtns = m_viewMode != UI_VIEW_OVERVIEW && !m_expandedMode && m_viewMode != UI_VIEW_AUCTION;
	SafeShowWindow(m_btnMA, showIndicatorBtns);
	SafeShowWindow(m_btnBoll, showIndicatorBtns);
	SafeShowWindow(m_btnChipPeak, m_viewMode != UI_VIEW_OVERVIEW);
	SafeShowWindow(m_btnOrderBook, m_viewMode != UI_VIEW_OVERVIEW);
}

BOOL CFloatingWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CRect clientRect;
	GetClientRect(&clientRect);

	// 获取真实的客户区坐标（优先使用当前实时光标位置，保证高DPI/多屏下坐标精准）
	CPoint clientPt;
	if (::GetCursorPos(&clientPt))
	{
		ScreenToClient(&clientPt);
	}
	else
	{
		clientPt = pt;
		ScreenToClient(&clientPt);
	}
	if (!clientRect.PtInRect(clientPt) && clientRect.PtInRect(m_mousePos))
	{
		clientPt = m_mousePos;
	}

	const int headerHeight = g_data.RDPI(26);
	const int relatedBarHeight = 0;
	const int indexBarHeight = g_data.RDPI(20);
	const int stockListWidth = m_showStockList ? CStockListPanel::GetPanelWidth() : 0;
	const int yAxisWidth = g_data.RDPI(50);
	const int chartLeft = stockListWidth + yAxisWidth;

	bool isIndex = (GetStockPriority(m_stock_id) < 200);
	bool isIndexKLine = isIndex && m_viewMode >= UI_VIEW_DAY_KLINE;
	const int orderBookWidth = IsInfoPanelVisible(isIndexKLine) ? ORDER_BOOK_WIDTH : 0;
	const int chartWidth = clientRect.Width() - orderBookWidth;
	const int chartRight = chartWidth;

	// 1. 如果在非总览模式且左侧股票列表显示时，鼠标在左侧面板区域（包括左侧列表与Y轴左边缘），则滚动股票列表
	if (m_viewMode != UI_VIEW_OVERVIEW && m_showStockList &&
		clientPt.x < chartLeft &&
		clientPt.y >= headerHeight + relatedBarHeight &&
		clientPt.y < clientRect.Height() - indexBarHeight)
	{
		std::vector<std::wstring> stockCodes = CStockListPanel::GetStockListCodes(CStockListPanel::ClampGroupTab(m_activeGroupTab));
		const int rowHeight = CStockListPanel::GetRowHeight();
		const int titleH = g_data.RDPI(18);
		int listAreaH = (clientRect.Height() - headerHeight - relatedBarHeight - indexBarHeight) - titleH;
		int totalH = static_cast<int>(stockCodes.size()) * rowHeight;
		int maxOffset = max(0, totalH - listAreaH);

		if (maxOffset > 0)
		{
			int newOffset = m_stockListScrollOffset;
			if (zDelta > 0)
				newOffset -= rowHeight;  // 向上滚
			else
				newOffset += rowHeight;  // 向下滚

			newOffset = max(0, min(newOffset, maxOffset));
			if (newOffset != m_stockListScrollOffset)
			{
				m_stockListScrollOffset = newOffset;
				Invalidate();
			}
		}
		return TRUE;
	}

	// 2. 只有当鼠标位于中间图表区域时，才触发分时图/K线缩放
	bool isInChart = (clientPt.x >= chartLeft && clientPt.x < chartRight &&
		clientPt.y >= headerHeight + relatedBarHeight &&
		clientPt.y < clientRect.Height() - indexBarHeight);

	if (m_viewMode != UI_VIEW_OVERVIEW && isInChart)
	{
		int minVisible;              // 最大放大倍率：与"+"按钮一致
		int maxVisible;              // 最小缩放上限：根据模式不同
		if (m_viewMode == UI_VIEW_DAY_KLINE)
		{
			minVisible = TIME_LINE_VISIBLE_COUNT_1DAY;
			maxVisible = 750;        // 日K线：最多显示约3年
		}
		else if (m_viewMode == UI_VIEW_WEEK_KLINE)
		{
			minVisible = TIME_LINE_VISIBLE_COUNT_1DAY;
			maxVisible = 500;        // 周K线：最多显示约10年
		}
		else if (m_viewMode == UI_VIEW_MONTH_KLINE)
		{
			minVisible = TIME_LINE_VISIBLE_COUNT_1DAY;
			maxVisible = 300;        // 月K线：最多显示约25年
		}
		else
		{
			minVisible = TIME_LINE_VISIBLE_COUNT_1MIN;
			maxVisible = 240;        // 分时：1天240分钟
		}
		// 获取实际数据点数
		int totalPoints = 0;
		{
			std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
			auto stockData = g_data.GetStockData(m_stock_id);
			if (stockData)
			{
				if (m_viewMode == UI_VIEW_DAY_KLINE)
				{
					auto klineObj = stockData->getKLineData();
					if (klineObj)
						totalPoints = static_cast<int>(klineObj->data.size());
				}
				else if (m_viewMode == UI_VIEW_WEEK_KLINE)
				{
					auto weekKLineObj = stockData->getWeekKLineData();
					if (weekKLineObj)
						totalPoints = static_cast<int>(weekKLineObj->data.size());
				}
				else if (m_viewMode == UI_VIEW_MONTH_KLINE)
				{
					auto monthKLineObj = stockData->getMonthKLineData();
					if (monthKLineObj)
						totalPoints = static_cast<int>(monthKLineObj->data.size());
				}
				else
				{
					auto timelineData = stockData->getTimelineData();
					if (timelineData)
						totalPoints = static_cast<int>(timelineData->data.size());
				}
			}
		}
		int effectiveMax = min(maxVisible, max(totalPoints, minVisible));
		int newCount = m_timelineVisibleCount;
		if (zDelta > 0)
		{
			// 向上滚：放大（减少可见点数，逐步缩小，最终倍率与"+"按钮一致）
			newCount = max(minVisible, m_timelineVisibleCount - TIME_LINE_VISIBLE_COUNT_STEP);
		}
		else
		{
			// 向下滚：缩小（增加可见点数）
			newCount = min(effectiveMax, m_timelineVisibleCount + TIME_LINE_VISIBLE_COUNT_STEP);
		}
		if (newCount != m_timelineVisibleCount)
		{
			int adjX = clientPt.x - chartLeft;
			int effectiveWidth = chartRight - chartLeft;

			// 鼠标在可见区域中的比例位置
			float ratio = 0.5f;
			if (effectiveWidth > 0 && adjX >= 0 && adjX < effectiveWidth)
			{
				ratio = static_cast<float>(adjX) / effectiveWidth;
			}
			else if (adjX >= effectiveWidth)
			{
				ratio = 1.0f;
			}

			// 鼠标对应的全局数据索引
			int mouseGlobalIndex = m_timelineScrollOffset + static_cast<int>(ratio * m_timelineVisibleCount);

			// 新的 scrollOffset 应使鼠标位置对应的数据索引在缩放后仍处于相同比例位置
			int newOffset = mouseGlobalIndex - static_cast<int>(ratio * newCount);
			int maxOffset = max(0, totalPoints - newCount);
			newOffset = max(0, min(newOffset, maxOffset));

			m_timelineVisibleCount = newCount;
			m_timelineScrollOffset = newOffset;
			Invalidate();
		}
		return TRUE;
	}

	if (m_viewMode != UI_VIEW_OVERVIEW)
	{
		return CWnd::OnMouseWheel(nFlags, zDelta, pt);
	}

	auto stockCodes = g_data.m_setting_data.m_stock_codes;
	int totalRows = (int)stockCodes.size();
	if (totalRows == 0)
	{
		return CWnd::OnMouseWheel(nFlags, zDelta, pt);
	}

	int totalTableH = headerHeight + totalRows * headerHeight;

	// 计算状态栏高度
	CDC* pDC = GetDC();
	if (!pDC)
	{
		return CWnd::OnMouseWheel(nFlags, zDelta, pt);
	}
	CSize textSize = pDC->GetTextExtent(_T("Ay"));
	ReleaseDC(pDC);

	const int statusBarHeight = textSize.cy + g_data.RDPI(6);

	// 可滚动区域 = 总高度 - 表头 - 状态栏
	CRect rect;
	GetClientRect(&rect);
	int availableHeight = rect.Height() - headerHeight - statusBarHeight;
	int maxScrollOffset = max(0, totalTableH - availableHeight);

	if (maxScrollOffset == 0)
	{
		return CWnd::OnMouseWheel(nFlags, zDelta, pt);
	}

	int newPos = m_vScrollOffset;

	if (zDelta > 0)
	{
		newPos -= headerHeight;
	}
	else
	{
		newPos += headerHeight;
	}

	newPos = max(0, min(newPos, maxScrollOffset));

	if (newPos != m_vScrollOffset)
	{
		m_vScrollOffset = newPos;
		Invalidate();
		return TRUE;
	}

	return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CFloatingWnd::OnBnClickedCallAuctionBtn()
{
	if (m_viewMode == UI_VIEW_AUCTION)
	{
		// 已经在竞价模式，切回分时模式
		m_viewMode = UI_VIEW_TIMELINE;
	}
	else
	{
		// 切换到竞价模式
		m_viewMode = UI_VIEW_AUCTION;
		m_showTrendView = false;
		m_showChipPeak = false;
		m_timelineScrollOffset = -1;
		m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1MIN;
		ResetHoverState();
		m_timelinePriceTitleTip.Empty();
	}
	UpdateModeButtons();
	UpdatePeriodComboVisibility();
	Invalidate();
}

void CFloatingWnd::OnBnClickedTimeLineBtn()
{
	if (m_viewMode != UI_VIEW_TIMELINE)
	{
		SetTimelineModeDefaults();
		UpdateModeButtons();
		UpdatePeriodComboVisibility();
		EnsureChipPeakData();
		Invalidate();
	}
}

void CFloatingWnd::OnBnClickedKLineBtn()
{
	if (m_viewMode != UI_VIEW_DAY_KLINE)
	{
		SetDayKLineModeDefaults();
		UpdateModeButtons();
		UpdatePeriodComboVisibility();
		EnsureChipPeakData();
		Invalidate();
	}
	else if (m_showTrendView)
	{
		m_showTrendView = false;
		Invalidate();
	}
}

void CFloatingWnd::OnBnClickedIndicatorMACDSignalBtn()
{
	m_timelineIndicator = (m_timelineIndicator == TimelineIndicator::MACD) ? TimelineIndicator::CJL : TimelineIndicator::MACD;
	m_timelineMacdTitleTip.Empty();
	m_timelineKdjTitleTip.Empty();
	m_timelineWrTitleTip.Empty();
	m_timelineRsiTitleTip.Empty();
	UpdateIndicatorButtons();
	Invalidate();
}

bool CFloatingWnd::IsInfoPanelVisible(bool isIndexKLine) const
{
	// 右侧信息面板（盘口/筹码峰）可见性：大盘K线模式无盘口；PK隐藏时整体让位给图表
	return !isIndexKLine && (m_showOrderBook || m_showChipPeak);
}

void CFloatingWnd::OnBnClickedChipPeakBtn()
{
	m_showChipPeak = !m_showChipPeak;
	UpdateModeButtons();
	EnsureChipPeakData();
	Invalidate();
}

void CFloatingWnd::OnBnClickedOrderBookBtn()
{
	if (m_showChipPeak)
	{
		// 筹码峰显示中：PK切回盘口
		m_showChipPeak = false;
		m_showOrderBook = true;
	}
	else
	{
		// 盘口显示中：整体隐藏右侧面板，宽度让给图表；已隐藏则恢复盘口
		m_showOrderBook = !m_showOrderBook;
	}
	UpdateModeButtons();
	EnsureChipPeakData();
	Invalidate();
}

void CFloatingWnd::OnBnClickedExpandBtn()
{
	m_expandedMode = !m_expandedMode;
	UpdateModeButtons();
	Invalidate();
}

void CFloatingWnd::OnBnClickedToggleStockListBtn()
{
	m_showStockList = !m_showStockList;
	if (m_showStockList)
	{
		EnsureStockListVisible();
	}
	UpdateModeButtons();
	Invalidate();
	// 强制重绘指标按钮，避免位置变化后按钮不显示
	if (!m_expandedMode)
	{
		m_btnIndicatorKDJ.Invalidate();
		m_btnIndicatorWR.Invalidate();
		m_btnIndicatorRSI.Invalidate();
	}
}

void CFloatingWnd::SafeSetWindowPos(CWnd& wnd, int x, int y, int cx, int cy)
{
	if (!wnd.GetSafeHwnd()) return;
	CRect curRect;
	wnd.GetWindowRect(&curRect);
	CWnd* parent = wnd.GetParent();
	if (parent)
	{
		parent->ScreenToClient(&curRect);
	}
	if (curRect.left != x || curRect.top != y || curRect.Width() != cx || curRect.Height() != cy)
	{
		wnd.SetWindowPos(nullptr, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
		wnd.Invalidate();
	}
}

void CFloatingWnd::SafeShowWindow(CWnd& wnd, bool show)
{
	if (!wnd.GetSafeHwnd()) return;
	bool curVisible = wnd.IsWindowVisible() != FALSE;
	if (curVisible != show)
	{
		wnd.ShowWindow(show ? SW_SHOW : SW_HIDE);
	}
}

HBRUSH CFloatingWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	(void)pWnd; (void)nCtlColor;
	pDC->SetBkColor(COLOR_BG_HEADER);
	pDC->SetTextColor(COLOR_WHITE);
	pDC->SetBkMode(TRANSPARENT);
	static CBrush s_darkBrush(COLOR_BG_HEADER);
	return s_darkBrush;
}

void CFloatingWnd::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	(void)nIDCtl;
	if (lpDrawItemStruct->CtlType != ODT_BUTTON) return;
	UINT nID = lpDrawItemStruct->CtlID;

	// 获取信号与激活状态
	COLORREF signalColor = CLR_INVALID;
	bool isActive = false;
	bool isCloseBtn = (nID == IDC_CLOSE_BTN);

	if (nID == IDC_CALL_AUCTION_BTN) { isActive = (m_viewMode == UI_VIEW_AUCTION); }
	else if (nID == IDC_TIMELINE_BTN) { isActive = (m_viewMode == UI_VIEW_TIMELINE); }
	else if (nID == IDC_KLINE_BTN) { isActive = (m_viewMode == UI_VIEW_DAY_KLINE); }
	else if (nID == IDC_WEEK_KLINE_BTN) { isActive = (m_viewMode == UI_VIEW_WEEK_KLINE); }
	else if (nID == IDC_MONTH_KLINE_BTN) { isActive = (m_viewMode == UI_VIEW_MONTH_KLINE); }
	else if (nID == IDC_CHIP_PEAK_BTN) { isActive = m_showChipPeak; }
	else if (nID == IDC_ORDER_BOOK_BTN) { isActive = !m_showChipPeak && m_showOrderBook; }
	else if (nID == IDC_EXPAND_BTN) { isActive = m_expandedMode; }
	else if (nID == IDC_TOGGLE_STOCK_LIST_BTN) { isActive = m_showStockList; }
	else if (nID == IDC_BOLL_BTN) { signalColor = m_bollSignalColor; isActive = m_showBollBands; }
	else if (nID == IDC_MA_BTN) { signalColor = m_maSignalColor; isActive = m_showMA; }
	else if (nID == IDC_INDICATOR_MACD_BTN) { isActive = (m_timelineIndicator == TimelineIndicator::CJL); }
	else if (nID == IDC_INDICATOR_MACD_SIGNAL_BTN) { signalColor = m_macdSignalColor; isActive = (m_timelineIndicator == TimelineIndicator::MACD); }
	else if (nID == IDC_INDICATOR_KDJ_BTN) { signalColor = m_kdjSignalColor; isActive = (m_timelineIndicator == TimelineIndicator::KDJ); }
	else if (nID == IDC_INDICATOR_WR_BTN) { signalColor = m_wrSignalColor; isActive = (m_timelineIndicator == TimelineIndicator::WR); }
	else if (nID == IDC_INDICATOR_RSI_BTN) { signalColor = m_rsiSignalColor; isActive = (m_timelineIndicator == TimelineIndicator::RSI); }

	CDC dc;
	dc.Attach(lpDrawItemStruct->hDC);
	CRect rect = lpDrawItemStruct->rcItem;

	bool isSelected = (lpDrawItemStruct->itemState & ODS_SELECTED) != 0;

	COLORREF bgColor;
	COLORREF textColor;
	COLORREF borderColor;

	if (isCloseBtn)
	{
		bgColor = isSelected ? RGB(185, 28, 28) : RGB(24, 27, 34);
		textColor = isSelected ? RGB(255, 255, 255) : RGB(148, 163, 184);
		borderColor = RGB(38, 42, 54);
	}
	else if (isActive)
	{
		bgColor = COLOR_ACCENT_BLUE;
		textColor = RGB(255, 255, 255);
		borderColor = COLOR_ACCENT_BLUE;
	}
	else
	{
		bgColor = RGB(24, 27, 34);
		textColor = RGB(148, 163, 184);
		borderColor = RGB(38, 42, 54);
	}

	if (isSelected && !isCloseBtn)
	{
		int r = max(0, GetRValue(bgColor) - 20);
		int g = max(0, GetGValue(bgColor) - 20);
		int b = max(0, GetBValue(bgColor) - 20);
		bgColor = RGB(r, g, b);
	}

	// 填充背景
	dc.FillSolidRect(rect, bgColor);

	// 绘制细边框
	CPen pen(PS_SOLID, 1, borderColor);
	CPen* pOldPen = dc.SelectObject(&pen);
	dc.MoveTo(rect.left, rect.bottom - 1);
	dc.LineTo(rect.left, rect.top);
	dc.LineTo(rect.right - 1, rect.top);
	dc.LineTo(rect.right - 1, rect.bottom - 1);
	dc.LineTo(rect.left, rect.bottom - 1);
	dc.SelectObject(pOldPen);

	// 若存在实时买卖信号，在右上角绘制小圆点提示（不破坏Tab选中状态）
	if (signalColor != CLR_INVALID && !isCloseBtn)
	{
		int dotSize = g_data.RDPI(3);
		int dotMargin = g_data.RDPI(2);
		CRect dotRect(rect.right - dotMargin - dotSize, rect.top + dotMargin, rect.right - dotMargin, rect.top + dotMargin + dotSize);
		dc.FillSolidRect(dotRect, signalColor);
	}


	// ===== 顶栏三个图标按钮：参考 Fluent 图标库矢量绘制（收起分组 / 折叠副图 / 关闭） =====
	// 不使用字体字符，避免字体回退导致字形缺失；用 GDI+ 抗锯齿渲染保证小尺寸下平滑。
	if (isCloseBtn || nID == IDC_EXPAND_BTN || nID == IDC_TOGGLE_STOCK_LIST_BTN)
	{
		Gdiplus::Graphics graphics(dc.GetSafeHdc());
		graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::Color iconColor(255, GetRValue(textColor), GetGValue(textColor), GetBValue(textColor));
		float penW = max(1.4f, g_data.GetDpi() * 1.4f / 96.0f);
		Gdiplus::Pen iconPen(iconColor, penW);
		iconPen.SetStartCap(Gdiplus::LineCapRound);
		iconPen.SetEndCap(Gdiplus::LineCapRound);
		iconPen.SetLineJoin(Gdiplus::LineJoinRound);
		auto P = [](float x, float y) { return Gdiplus::PointF(x, y); };
		float fx = (float)rect.left, fy = (float)rect.top;
		float fw = (float)rect.Width(), fh = (float)rect.Height();
		float cx = fx + fw / 2.0f, cy = fy + fh / 2.0f;

		if (isCloseBtn)
		{
			// Fluent "Dismiss"：圆头对角线 ✕，边距取短边的28%
			float m = min(fw, fh) * 0.28f;
			graphics.DrawLine(&iconPen, P(fx + m, fy + m), P(fx + fw - m, fy + fh - m));
			graphics.DrawLine(&iconPen, P(fx + fw - m, fy + m), P(fx + m, fy + fh - m));
		}
		else if (nID == IDC_EXPAND_BTN)
		{
			// Fluent "ChevronDown/Up"：双 V 箭头。展开状态显示朝上（点击收起副图），
			// 收起状态显示朝下（点击展开副图）
			float hw = g_data.RDPI(3) + 0.5f;
			float vh = g_data.RDPI(2) + 0.5f;
			float dir = m_expandedMode ? -1.0f : 1.0f; // -1=尖头朝上，1=尖头朝下
			Gdiplus::PointF pts[3];
			for (int k = -1; k <= 1; k += 2)
			{
				float vy = cy + k * vh / 2.0f + dir * vh / 2.0f;
				pts[0] = P(cx - hw, vy - dir * vh / 2.0f);
				pts[1] = P(cx, vy + dir * vh / 2.0f);
				pts[2] = P(cx + hw, vy - dir * vh / 2.0f);
				graphics.DrawLines(&iconPen, pts, 3);
			}
		}
		else
		{
			// Fluent "PanelLeft"：左侧面板边线 + 方向箭头。
			// 列表已显示→箭头朝右（收起分组），已隐藏→箭头朝左（展开分组）
			float barX = fx + g_data.RDPI(6);
			float top = fy + g_data.RDPI(6);
			float bottom = fy + fh - g_data.RDPI(6);
			graphics.DrawLine(&iconPen, P(barX, top), P(barX, bottom));
			float hw = g_data.RDPI(2) + 0.5f;
			float vh = g_data.RDPI(2) + 0.5f;
			float ax = cx + g_data.RDPI(2);
			float dir = m_showStockList ? 1.0f : -1.0f; // 1=朝右，-1=朝左
			Gdiplus::PointF pts[3] = {
				P(ax - dir * hw, cy - vh), P(ax + dir * hw, cy), P(ax - dir * hw, cy + vh)
			};
			graphics.DrawLines(&iconPen, pts, 3);
		}

		dc.Detach();
		return;
	}

	// 明确获取按钮文本
	CString text;
	if (nID == IDC_CALL_AUCTION_BTN) text = _T("竞价");
	else if (nID == IDC_TIMELINE_BTN) text = _T("分时");
	else if (nID == IDC_KLINE_BTN) text = _T("日K");
	else if (nID == IDC_WEEK_KLINE_BTN) text = _T("周K");
	else if (nID == IDC_MONTH_KLINE_BTN) text = _T("月K");
	else if (nID == IDC_CHIP_PEAK_BTN) text = _T("CM");
	else if (nID == IDC_ORDER_BOOK_BTN) text = _T("PK");
	else if (nID == IDC_INDICATOR_MACD_BTN) text = _T("VOL");
	else if (nID == IDC_INDICATOR_MACD_SIGNAL_BTN) text = _T("MACD");
	else if (nID == IDC_INDICATOR_KDJ_BTN) text = _T("KDJ");
	else if (nID == IDC_INDICATOR_RSI_BTN) text = _T("RSI");
	else if (nID == IDC_INDICATOR_WR_BTN) text = _T("W&R");
	else
	{
		CWnd* pBtn = CWnd::FromHandle(lpDrawItemStruct->hwndItem);
		if (pBtn) pBtn->GetWindowText(text);
	}

	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(textColor);

	CFont btnFont;
	CreateStockFont(btnFont, dc, g_data.RDPI(10), isActive ? FW_BOLD : FW_NORMAL);
	CFont* pOldFont = dc.SelectObject(&btnFont);
	dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc.SelectObject(pOldFont);
	btnFont.DeleteObject();

	dc.Detach();
}

void CFloatingWnd::ApplySignalColors(COLORREF bollColor, COLORREF macdColor, COLORREF kdjColor, COLORREF wrColor, COLORREF rsiColor, COLORREF maColor)
{
	auto updateColor = [](COLORREF& storedColor, COLORREF newColor, CButton& btn) {
		if (storedColor == newColor) return;
		storedColor = newColor;
		if (btn.GetSafeHwnd())
			btn.Invalidate();
		};
	updateColor(m_bollSignalColor, bollColor, m_btnBoll);
	updateColor(m_macdSignalColor, macdColor, m_btnIndicatorMACD);
	updateColor(m_kdjSignalColor, kdjColor, m_btnIndicatorKDJ);
	updateColor(m_wrSignalColor, wrColor, m_btnIndicatorWR);
	updateColor(m_rsiSignalColor, rsiColor, m_btnIndicatorRSI);
	updateColor(m_maSignalColor, maColor, m_btnMA);
}

void CFloatingWnd::EnsureChipPeakData()
{
	if (m_showChipPeak)
	{
		bool needRequest = true;
		{
			std::lock_guard<std::mutex> lock(Stock::Instance().m_stockDataMutex);
			auto stockData = g_data.GetStockData(m_stock_id);
			needRequest = !stockData || !stockData->chipDistribution.IsValid() || stockData->info.circulatingAShares <= 0;
		}

		if (needRequest)
		{
			// 通过 StockFetchThread 后台任务队列执行，避免创建临时线程
			std::wstring stockId = m_stock_id;
			CStockFetchThread::Instance().PostBackgroundTask([stockId]() {
				CStockFetchThread::Instance().FetchStockBasic(stockId);
				CStockFetchThread::Instance().FetchChipDistribution(stockId);
				// UI刷新由1秒定时器检查dirty标识驱动，此处仅更新数据
				});
		}
	}
}

void CFloatingWnd::ResetHoverState()
{
	m_isHoveringKLine = false;
	m_isHoveringKLineVolume = false;
	m_isHoveringVolume = false;
	m_klineHoveredBarIndex = -1;
	m_hoveredBarIndex = -1;
	m_klineHoverTip.Empty();
	m_hoverTip.Empty();
	m_klineTrendHoverTip.Empty();
	m_timelineVolumeTitleTip.Empty();
	m_timelineMacdTitleTip.Empty();
	m_timelineKdjTitleTip.Empty();
}

void CFloatingWnd::SetTimelineModeDefaults()
{
	m_viewMode = UI_VIEW_TIMELINE;
	m_showBollBands = true;
	m_btnBoll.SetWindowText(_T("BL"));
	m_showMA = false;
	m_showTrendView = false;
	m_showChipPeak = false;
	m_showJZCurve = CCommon::IsFundCode(m_stock_id);  // 基金默认显示净值曲线
	m_scrollOffset = 0;
	m_timelineScrollOffset = -1;  // 自动滚动到末尾
	m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1MIN;  // 显示最新走势
	ResetHoverState();
}

void CFloatingWnd::SetDayKLineModeDefaults()
{
	m_viewMode = UI_VIEW_DAY_KLINE;
	m_showBollBands = false;
	m_btnBoll.SetWindowText(_T("BL"));
	m_showTrendView = false;  // 日K默认显示K线图
	m_showChipPeak = false;
	m_showJZCurve = false;
	m_showMA = true;
	m_scrollOffset = 0;
	m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1DAY;  // 日K线初始缩放到最大，显示最新40根
	m_timelineScrollOffset = -1;  // 自动滚动到末尾
	ResetHoverState();
}

void CFloatingWnd::SetWeekKLineModeDefaults()
{
	m_viewMode = UI_VIEW_WEEK_KLINE;
	m_showBollBands = false;
	m_btnBoll.SetWindowText(_T("BL"));
	m_showTrendView = false;
	m_showChipPeak = false;
	m_showJZCurve = false;
	m_showMA = true;
	m_scrollOffset = 0;
	m_timelineScrollOffset = -1;  // 自动滚动到末尾
	m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1DAY;  // 周K线初始显示最新40根
	ResetHoverState();
}

void CFloatingWnd::SetMonthKLineModeDefaults()
{
	m_viewMode = UI_VIEW_MONTH_KLINE;
	m_showBollBands = false;
	m_btnBoll.SetWindowText(_T("BL"));
	m_showTrendView = false;
	m_showChipPeak = false;
	m_showJZCurve = false;
	m_showMA = true;
	m_scrollOffset = 0;
	m_timelineScrollOffset = -1;  // 自动滚动到末尾
	m_timelineVisibleCount = TIME_LINE_VISIBLE_COUNT_1DAY;  // 月K线初始显示最新40根
	ResetHoverState();
}

void CFloatingWnd::OnBnClickedMABtn()
{
	m_showMA = !m_showMA;
	if (m_showMA)
	{
		m_showBollBands = false;
		m_btnBoll.SetWindowText(_T("BL"));
	}
	if (m_btnMA.GetSafeHwnd()) m_btnMA.Invalidate();
	if (m_btnBoll.GetSafeHwnd()) m_btnBoll.Invalidate();
	Invalidate();
}

void CFloatingWnd::OnBnClickedWeekKLineBtn()
{
	if (m_viewMode != UI_VIEW_WEEK_KLINE)
	{
		// 切换到周K线模式
		SetWeekKLineModeDefaults();
	}
	else
	{
		// 退出周K线模式，回到分时模式
		SetTimelineModeDefaults();
	}
	UpdateModeButtons();
	UpdatePeriodComboVisibility();
	EnsureChipPeakData();
	Invalidate();
}

void CFloatingWnd::OnBnClickedMonthKLineBtn()
{
	if (m_viewMode != UI_VIEW_MONTH_KLINE)
	{
		// 切换到月K线模式
		SetMonthKLineModeDefaults();
	}
	else
	{
		// 退出月K线模式，回到分时模式
		SetTimelineModeDefaults();
	}
	UpdateModeButtons();
	UpdatePeriodComboVisibility();
	EnsureChipPeakData();
	Invalidate();
}

void CFloatingWnd::OnBnClickedBollBtn()
{
	// 布林带模式：点击切换显示/隐藏
	m_showBollBands = !m_showBollBands;
	if (m_showBollBands)
		m_showMA = false;
	m_btnBoll.SetWindowText(_T("BL"));
	if (m_btnBoll.GetSafeHwnd()) m_btnBoll.Invalidate();
	if (m_btnMA.GetSafeHwnd()) m_btnMA.Invalidate();
	Invalidate();
}

void CFloatingWnd::OnBnClickedIndicatorMACDBtn()
{
	m_timelineIndicator = TimelineIndicator::CJL;
	m_timelineMacdTitleTip.Empty();
	m_timelineKdjTitleTip.Empty();
	m_timelineWrTitleTip.Empty();
	m_timelineRsiTitleTip.Empty();
	UpdateIndicatorButtons();
	Invalidate();
}

void CFloatingWnd::OnBnClickedIndicatorKDJBtn()
{
	m_timelineIndicator = (m_timelineIndicator == TimelineIndicator::KDJ) ? TimelineIndicator::CJL : TimelineIndicator::KDJ;
	m_timelineMacdTitleTip.Empty();
	m_timelineKdjTitleTip.Empty();
	m_timelineWrTitleTip.Empty();
	m_timelineRsiTitleTip.Empty();
	UpdateIndicatorButtons();
	Invalidate();
}

void CFloatingWnd::OnBnClickedIndicatorWRBtn()
{
	m_timelineIndicator = (m_timelineIndicator == TimelineIndicator::WR) ? TimelineIndicator::CJL : TimelineIndicator::WR;
	m_timelineMacdTitleTip.Empty();
	m_timelineKdjTitleTip.Empty();
	m_timelineWrTitleTip.Empty();
	m_timelineRsiTitleTip.Empty();
	UpdateIndicatorButtons();
	Invalidate();
}

void CFloatingWnd::OnBnClickedIndicatorRSIBtn()
{
	m_timelineIndicator = (m_timelineIndicator == TimelineIndicator::RSI) ? TimelineIndicator::CJL : TimelineIndicator::RSI;
	m_timelineMacdTitleTip.Empty();
	m_timelineKdjTitleTip.Empty();
	m_timelineWrTitleTip.Empty();
	m_timelineRsiTitleTip.Empty();
	UpdateIndicatorButtons();
	Invalidate();
}

void CFloatingWnd::OnBnClickedCloseBtn()
{
	ReleaseCapture();
	PostMessage(IDM_CLOSE_WINDOW, 0, 0);
}

LRESULT CFloatingWnd::OnCloseWindow(WPARAM wParam, LPARAM lParam)
{
	if (GetSafeHwnd())
	{
		SetForegroundWindow();
		DestroyWindow();
	}
	return 0;
}

LRESULT CFloatingWnd::OnShowEditDialog(WPARAM wParam, LPARAM lParam)
{
	if (m_pendingEditStockCode.empty())
		return 0;

	std::wstring editCode = m_pendingEditStockCode;
	m_pendingEditStockCode.clear();

	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	COptionsDlg dlg(editCode, AfxGetMainWnd());
	GetWindowRect(&dlg.m_refWndRect);
	if (dlg.DoModal() == IDOK && !dlg.m_stock_code.IsEmpty())
	{
		auto& codes = g_data.m_setting_data.m_stock_codes;
		for (auto& code : codes)
		{
			if (code == editCode)
			{
				code = dlg.m_stock_code.GetString();
				break;
			}
		}
		Invalidate();
	}
	return 0;
}

LRESULT CFloatingWnd::OnShowAddDialog(WPARAM wParam, LPARAM lParam)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	COptionsDlg dlg(std::wstring(), AfxGetMainWnd());
	GetWindowRect(&dlg.m_refWndRect);
	if (dlg.DoModal() == IDOK && !dlg.m_stock_code.IsEmpty())
	{
		auto& codes = g_data.m_setting_data.m_stock_codes;
		codes.push_back(dlg.m_stock_code.GetString());
		g_data.SaveConfig();

		m_vScrollOffset = 0;
		Invalidate();
	}
	return 0;
}

LRESULT CFloatingWnd::OnShowTradeDialog(WPARAM wParam, LPARAM lParam)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	/* 测试买卖点检测时暂时屏蔽交易记录弹窗
	CTradeRecordDialog dlg(this);
	dlg.SetTradeInfo(m_pendingTradeTime, m_pendingTradePrice, CString(m_stock_id.c_str()));
	dlg.DoModal();
	*/
	return 0;
}

void CFloatingWnd::OnDestroy()
{
	KillTimer(IDC_REFRESH_TIMER);

	CWnd::OnDestroy();

	if (m_CTransparentWnd.GetSafeHwnd())
	{
		m_CTransparentWnd.DestroyWindow();
	}
}

void CFloatingWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == IDC_REFRESH_TIMER)
	{
		// 鼠标移出图表区超过2秒自动清除悬停信息卡，避免长期遮挡图表
		CheckHoverCardAutoHide();

		// 1秒定时检查：图表和盘口分别判断，任一有变化才重绘
		bool needRedraw = false;
		if (m_chartDirty)
		{
			m_chartDirty = false;
			needRedraw = true;
		}
		if (m_orderBookDirty)
		{
			m_orderBookDirty = false;
			needRedraw = true;
		}
		if (needRedraw)
			Invalidate();
	}
	CWnd::OnTimer(nIDEvent);
}

void CFloatingWnd::CheckHoverCardAutoHide()
{
	if (m_hoveredBarIndex < 0)
	{
		m_hoverCardOutsideSince = 0;
		return;
	}

	CPoint cursorPt;
	if (!GetCursorPos(&cursorPt))
		return;

	// 图表区 = 整个窗口剔除左侧分组列表面板与右侧买卖盘面板
	CRect clientRect;
	GetClientRect(&clientRect);
	ClientToScreen(&clientRect);
	const int stockListWidth = m_showStockList ? CStockListPanel::GetPanelWidth() : 0;
	bool isIndex = (GetStockPriority(m_stock_id) < 200);
	bool isIndexKLine = isIndex && m_viewMode >= UI_VIEW_DAY_KLINE;
	const int orderBookWidth = IsInfoPanelVisible(isIndexKLine) ? ORDER_BOOK_WIDTH : 0;
	CRect chartRect(clientRect.left + stockListWidth, clientRect.top,
		clientRect.right - orderBookWidth, clientRect.bottom);

	if (chartRect.PtInRect(cursorPt))
	{
		m_hoverCardOutsideSince = 0;
		return;
	}
	ULONGLONG now = GetTickCount64();
	if (m_hoverCardOutsideSince == 0)
	{
		m_hoverCardOutsideSince = now;
	}
	else if (now - m_hoverCardOutsideSince >= 2000)
	{
		m_hoverCardOutsideSince = 0;
		ResetHoverState();
		Invalidate();
	}
}

void CFloatingWnd::EnsureStockListVisible()
{
	if (!m_showStockList || !GetSafeHwnd())
		return;

	CRect clientRect;
	GetClientRect(&clientRect);
	const int headerHeight = g_data.RDPI(26);
	const int relatedBarHeight = 0;
	const int indexBarHeight = g_data.RDPI(20);
	const int titleH = g_data.RDPI(18);
	const int rowHeight = CStockListPanel::GetRowHeight();
	int listAreaH = clientRect.Height() - headerHeight - relatedBarHeight - indexBarHeight - titleH;
	if (listAreaH <= 0)
		return;

	std::vector<std::wstring> stockCodes = CStockListPanel::GetStockListCodes(CStockListPanel::ClampGroupTab(m_activeGroupTab));
	int totalH = static_cast<int>(stockCodes.size()) * rowHeight;
	int maxOffset = max(0, totalH - listAreaH);

	auto it = std::find(stockCodes.begin(), stockCodes.end(), m_stock_id);
	if (it != stockCodes.end())
	{
		int idx = static_cast<int>(std::distance(stockCodes.begin(), it));
		int itemTop = idx * rowHeight;
		int itemBottom = itemTop + rowHeight;
		if (itemTop < m_stockListScrollOffset)
		{
			m_stockListScrollOffset = itemTop;
		}
		else if (itemBottom > m_stockListScrollOffset + listAreaH)
		{
			m_stockListScrollOffset = itemBottom - listAreaH;
		}
		m_stockListScrollOffset = max(0, min(m_stockListScrollOffset, maxOffset));
	}
	else
	{
		m_stockListScrollOffset = max(0, min(m_stockListScrollOffset, maxOffset));
	}
}

void CFloatingWnd::SwitchFloatingGroup(int groupTab)
{
	groupTab = CStockListPanel::ClampGroupTab(groupTab);
	if (m_activeGroupTab == groupTab)
		return;
	m_activeGroupTab = groupTab;
	m_stockListScrollOffset = 0;
	Invalidate();
}

void CFloatingWnd::ShowGroupDropdownMenu(const CRect& dropdownRect)
{
	const auto& customGroups = g_data.m_setting_data.m_custom_groups;
	if (customGroups.size() < 2)
		return;

	// 下拉列出第一个标签位装不下的自定义分组（自选股/持仓/自定义#1 在标签条上）
	std::vector<CDarkPopupMenu::MenuItem> menuItems;
	for (size_t k = 1; k < customGroups.size(); ++k)
	{
		menuItems.push_back({
			static_cast<int>(1000 + k),
			customGroups[k].name,
			m_activeGroupTab == static_cast<int>(k + 2),
			false,
			false
			});
	}

	CPoint pt(dropdownRect.left, dropdownRect.bottom + g_data.RDPI(2));
	ClientToScreen(&pt);

	CDarkPopupMenu menu;
	menu.CreatePopup(this);
	int cmd = menu.TrackMenu(pt, menuItems, dropdownRect.Width());
	if (cmd >= 1001 && cmd < 1000 + static_cast<int>(customGroups.size()))
		SwitchFloatingGroup(cmd - 1000 + 2);
}

void CFloatingWnd::UpdateGroupTabHover(const CPoint& point)
{
	int hover = -1;
	if (m_showStockList && m_viewMode != UI_VIEW_OVERVIEW && point.y < g_data.RDPI(26))
	{
		for (size_t i = 0; i < m_groupTabs.size(); ++i)
		{
			if (m_groupTabs[i].rect.PtInRect(point))
			{
				hover = static_cast<int>(i);
				break;
			}
		}
	}
	if (hover != m_hoverGroupTab)
	{
		m_hoverGroupTab = hover;
		Invalidate();
	}
}

void CFloatingWnd::OnMouseLeave()
{
	m_trackingTabHover = false;
	UpdateGroupTabHover(CPoint(-1, -1));
	CWnd::OnMouseLeave();
}
