#pragma once

#include <afxwin.h>
#include <string>
#include <vector>
#include <atomic>
#include <StockDef.h>
#include <TransparentWnd.h>
#include "SignalAnalyzer.h"
#include "StockIndicator.h"
#include "ChartContext.h"
#include "StockListPanel.h"
#include "CallAuctionChart.h"
#include "ChipPeakPanel.h"
#include "EtfHoldingsPanel.h"
#include "OrderBookPanel.h"
#include "OverviewPanel.h"
#include "IndicatorChart.h"
#include "StatusBarPanel.h"
#include "KLineChart.h"
#include "TimelineChart.h"
#include "MarketCenterPanel.h"

// 定义自定义消息
#define FWND_MSG_UPDATE_STATUS (WM_USER + 100)
#define FWND_MSG_SHOW_EDIT_DLG (WM_USER + 102)
#define FWND_MSG_SHOW_ADD_DLG (WM_USER + 103)
#define FWND_MSG_SHOW_TRADE_DLG (WM_USER + 104)

// 定义时间线可见点数常量
#define TIME_LINE_VISIBLE_COUNT_1MIN 30
#define TIME_LINE_VISIBLE_COUNT_5MIN 24
#define TIME_LINE_VISIBLE_COUNT_30MIN 16
#define TIME_LINE_VISIBLE_COUNT_1DAY 20
#define TIME_LINE_VISIBLE_COUNT_STEP 10

class CFloatingWnd : public CWnd
{
public:
	CFloatingWnd();
	virtual ~CFloatingWnd();

	BOOL Create(CFont* font, CPoint pt, std::wstring stock_id);
	const std::wstring& GetStockId() const { return m_stock_id; }
	void SetStockId(const std::wstring& stockId);
	void ToggleKLineMode(); // 切换分时/日K模式
	// 行情中心内嵌视图：右键在悬浮窗内原地切换；进入时临时放大窗口，退出还原
	void ToggleMarketCenter();   // 右键切换行情中心视图模式（悬浮窗内原地切换，不建子窗口/不改尺寸）
	void HideChartButtons(bool hide);   // 行情中心视图下隐藏/恢复图表视图专属按钮
	// 鼠标移出图表区超过2秒时自动清除悬停信息卡，避免长期遮挡图表
	void CheckHoverCardAutoHide();
	// 右侧信息面板（盘口/筹码峰）当前是否可见：隐藏后宽度全部让给图表
	bool IsInfoPanelVisible(bool isIndexKLine) const;

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	LRESULT OnUpdateStatus(WPARAM wParam, LPARAM lParam);
	LRESULT OnMarketCenterDataUpdated(WPARAM wParam, LPARAM lParam);   // 行情中心数据到达，重绘
	LRESULT OnMcEtfClicked(WPARAM wParam, LPARAM lParam);              // 行情中心点击 ETF，跳转首页 K 线临时查看
	LRESULT OnCloseWindow(WPARAM wParam, LPARAM lParam);
	LRESULT OnShowEditDialog(WPARAM wParam, LPARAM lParam);
	LRESULT OnShowAddDialog(WPARAM wParam, LPARAM lParam);
	LRESULT OnShowTradeDialog(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedTimeLineBtn();
	afx_msg void OnBnClickedKLineBtn();
	afx_msg void OnBnClickedWeekKLineBtn();
	afx_msg void OnBnClickedMonthKLineBtn();
	afx_msg void OnBnClickedCloseBtn();
	afx_msg void OnBnClickedMABtn();
	afx_msg void OnBnClickedBollBtn();
	afx_msg void OnBnClickedIndicatorMACDBtn();
	afx_msg void OnBnClickedIndicatorMACDSignalBtn();
	afx_msg void OnBnClickedIndicatorKDJBtn();
	afx_msg void OnBnClickedIndicatorWRBtn();
	afx_msg void OnBnClickedIndicatorRSIBtn();
	afx_msg void OnBnClickedChipPeakBtn();
	afx_msg void OnBnClickedOrderBookBtn();
	afx_msg void OnBnClickedEtfHoldingsBtn();
	afx_msg void OnBnClickedExpandBtn();
	afx_msg void OnBnClickedToggleStockListBtn();
	afx_msg void OnBnClickedCallAuctionBtn();
	afx_msg void OnBnClickedKLineSourceBtn();

private:
	void EnsureChipPeakData();
	void EnsureEtfHoldingsData();
	void EnsureKLineData(STOCK::Period period);
	void ResetHoverState();           // 重置所有悬停状态
	void SetTimelineModeDefaults();   // 设置分时模式默认参数
	void SetDayKLineModeDefaults();   // 设置日K模式默认参数
	void SetWeekKLineModeDefaults();  // 设置周K模式默认参数
	void SetMonthKLineModeDefaults(); // 设置月K模式默认参数
	static void SafeSetWindowPos(CWnd& wnd, int x, int y, int cx, int cy);
	static void SafeShowWindow(CWnd& wnd, bool show);

	// TimelineDrawContext / KLineDrawData / LabelInfo 已移至 ChartContext.h，供各图表模块共享
	// MACDData/MACDCrossSignal/KDJData/WRData/RSIData/PeriodPoint 类型别名已移至各模块类
	// 走势图绘制已移至CTimelineChart
	// MACD/KDJ/WR/RSI/成交量绘制已移至CIndicatorChart
	// DrawHeader/DrawTimelinePositionInfo/DrawKLinePositionInfo/DrawKLineInfoPanel 已移至CStatusBarPanel
	// K线图绘制已移至CKLineChart
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	void UpdateModeButtons();
	void UpdateIndicatorButtons();
	void UpdatePeriodComboVisibility();
	void ApplySignalColors(COLORREF bollColor, COLORREF macdColor, COLORREF kdjColor, COLORREF wrColor, COLORREF rsiColor, COLORREF maColor);
	void EnsureStockListVisible();
	// 左侧列表分组：切换 / “更多分组”下拉 / 顶栏标签悬停
	void SwitchFloatingGroup(int groupTab);
	void ShowGroupDropdownMenu(const CRect& dropdownRect);
	void UpdateGroupTabHover(const CPoint& point);

	CTransparentWnd m_CTransparentWnd;
	CMarketCenterPanel m_marketCenterPanel;            // 行情中心面板（视图模式，悬浮窗 OnPaint 里绘制）
	bool m_marketCenterMode{ false };                  // 是否处于行情中心视图
	CStockListPanel m_stockListPanel;
	CCallAuctionChart m_callAuctionChart;
	CChipPeakPanel m_chipPeakPanel;
	CEtfHoldingsPanel m_etfHoldingsPanel;
	COrderBookPanel m_orderBookPanel;
	COverviewPanel m_overviewPanel;
	CIndicatorChart m_indicatorChart;
	CStatusBarPanel m_statusBarPanel;
	CKLineChart m_kLineChart;
	CTimelineChart m_timelineChart;
	CButton m_btnTimeLine;
	CButton m_btnKLine;
	CButton m_btnWeekKLine;
	CButton m_btnMonthKLine;
	CButton m_btnKLineSource;   // K线数据源状态与刷新按钮
	CButton m_btnMA;
	CButton m_btnBoll;
	CButton m_btnClose;
	CButton m_btnExpand;      // 放大按钮（隐藏副图，走势图占3/4）
	CButton m_btnToggleStockList;  // 股票列表显示/隐藏按钮
	CButton m_btnCallAuction;     // 集合竞价按钮
	CButton m_btnIndicatorCJL;  // CJL指标按钮
	CButton m_btnIndicatorMACD;  // MACD信号按钮
	CButton m_btnIndicatorKDJ;   // KDJ指标按钮
	CButton m_btnIndicatorWR;    // W&R指标按钮
	CButton m_btnIndicatorRSI;   // RSI指标按钮
	CButton m_btnChipPeak;       // 筹码峰按钮
	CButton m_btnOrderBook;      // 盘口按钮（与筹码峰按钮切换）
	CButton m_btnEtfHoldings;    // ETF持仓按钮（CC）
	CFont m_chipPeakFont;        // 筹码峰按钮小字体
	std::wstring m_stock_id;
	std::wstring m_mc_return_stock_id;  // 非空 = 正在临时查看行情中心 ETF 的 K 线，值为跳转前股票 id
	UIViewMode m_viewMode{ UI_VIEW_DAY_KLINE };  // 当前界面视图模式
	bool m_klineDataLoaded{ false };
	int m_klinePeriodDays{ 250 };
	int m_scrollOffset{ 0 };
	int m_timelineScrollOffset{ -1 };  // 分时图水平滚动偏移，-1表示需要自动滚动到末尾
	int m_timelineVisibleCount{ 30 };  // 分时图可见数据点数
	int m_timelineLastTotalPoints{ 0 };  // 上次绘制的数据点数，用于判断新数据追加时是否自动跟随
	int m_vScrollOffset{ 0 };

	// 分时图指标类型
	enum class TimelineIndicator { CJL, MACD, KDJ, WR, RSI };
	TimelineIndicator m_timelineIndicator{ TimelineIndicator::CJL };
	bool m_indicatorBtnsInitialized{ false };

	// 分时图鼠标拖动滚动
	bool m_isTimelineDragging{ false };
	CPoint m_timelineDragStartPos;
	int m_timelineDragStartOffset{ 0 };
	// K线图鼠标拖动滚动
	bool m_isKLineDragging{ false };
	CPoint m_klineDragStartPos;
	int m_klineDragStartOffset{ 0 };
	HCURSOR m_hPrevCursor{ NULL };
	volatile BOOL m_isDestroying;
	CFont* m_pfont{};
	CString loading_state_txt;

	// 悬停信息卡自动清除：鼠标持续离开图表区的时间起点（0=鼠标在图表区内或无信息卡）
	ULONG64 m_hoverCardOutsideSince{ 0 };

	// 鼠标悬停数据
	CPoint m_mousePos;
	bool m_isHoveringVolume{ false };
	int m_hoveredBarIndex{ -1 };
	STOCK::TimelinePoint m_hoveredData;
	// 悬停点的MA值及前一点MA值（用于箭头方向）
	STOCK::Price m_hoverMa1{ 0 }, m_hoverPrevMa1{ 0 };
	std::vector<STOCK::Price> m_hoverMaValues; // 悬停点各周期均线值，顺序同 SettingData::m_ma_days
	CString m_hoverTip;
	// 分时图标题栏悬停提示
	CString m_timelinePriceTitleTip;   // 走势图标题栏：现价/均价/MA5/MA17/MA60...
	CString m_timelineVolumeTitleTip;  // 量柱图标题栏：成交量/成交额
	CString m_timelineMacdTitleTip;    // MACD标题栏：DIF/DEA/MACD
	CString m_timelineKdjTitleTip;     // KDJ标题栏：K/D/J
	CString m_timelineWrTitleTip;      // WR标题栏：WR1/WR2
	CString m_timelineRsiTitleTip;     // RSI标题栏：RSI1/RSI2
	CString m_chipPeakTip;             // 筹码峰提示

	// 双击检测
	DWORD m_lastClickTime{};
	CPoint m_lastClickPos;
	std::wstring m_pendingEditStockCode;
	CString m_pendingTradeTime;
	double m_pendingTradePrice{ 0.0 };

	// 日K线鼠标悬停数据
	bool m_isHoveringKLine{ false };
	bool m_isHoveringKLineVolume{ false };
	bool m_isHoveringKDJ{ false };
	bool m_showTrendView{ false };
	bool m_showChipPeak{ false };
	bool m_showOrderBook{ false };  // 是否显示右侧买卖盘口面板（默认不选中，给图表更多空间）
	bool m_showEtfHoldings{ false }; // 是否显示右侧 ETF 持仓面板
	int m_etfHoldingsScrollOffset{ 0 }; // ETF持仓列表垂直滚动偏移
	bool m_isEtfHoldingsDragging{ false }; // ETF持仓列表是否正在拖动
	bool m_isEtfHoldingsDragMoved{ false }; // ETF持仓列表拖动是否产生了位移
	CPoint m_etfHoldingsDragStartPos;       // ETF持仓列表拖动起点
	int m_etfHoldingsDragStartOffset{ 0 };  // ETF持仓列表拖动起始偏移
	bool m_expandedMode{ false };  // 放大模式：隐藏副图，走势图3/4+成交量1/4
	bool m_showStockList{ true };  // 是否显示左侧股票列表面板
	bool m_showPositionSummaryPercent{ false };  // 持仓汇总栏是否显示盈亏百分比
	int m_activeGroupTab{ 1 };     // 左侧列表当前分组：0=自选股, 1=持仓, >=2 为自定义分组
	std::vector<FloatingGroupTab> m_groupTabs;  // 顶部分组标签布局（绘制时计算，供点击命中）
	int m_hoverGroupTab{ -1 };     // 悬停的分组标签下标（m_groupTabs 下标，-1 无）
	int m_groupListSort{ 0 };      // 左侧列表排序：0=默认顺序, 1=涨跌幅降序(涨最多在上), 2=涨跌幅升序(跌最多在上)
	int m_hoverSortArrow{ -1 };    // 悬停的分组标题排序箭头：0=▲, 1=▼, -1 无
	bool m_trackingTabHover{ false };  // 是否已申请 WM_MOUSELEAVE 跟踪
	int m_stockListScrollOffset{ 0 };  // 左侧股票列表垂直滚动偏移
	bool m_isStockListDragging{ false };  // 左侧股票列表是否正在拖动
	bool m_isStockListDragMoved{ false };  // 左侧股票列表拖动是否产生了位移
	CPoint m_stockListDragStartPos;       // 左侧股票列表拖动起点
	int m_stockListDragStartOffset{ 0 };  // 左侧股票列表拖动起始偏移
	bool m_showJZCurve{ false };  // 基金净值曲线
	bool m_showMA{ false };
	bool m_showBollBands{ true };
	volatile bool m_chartDirty{ false };      // 图表数据更新标识（走势图/K线/MACD等），由PostMessage设置
	volatile bool m_orderBookDirty{ false };  // 盘口数据更新标识（五档/成交/净比等），由共享内存回调设置
	int m_klineHoveredBarIndex{ -1 };
	CString m_klineHoverTip;
	CString m_klineVolumeHoverTip;
	CString m_klineTrendHoverTip;
	CString m_kdjHoverTip;

	// 5分钟K线图整点时间标签（X轴：centerX, "h:mm"）
	std::vector<std::pair<int, CString>> m_min5HourLabels;

	// 总览表行信息（用于双击处理）
	std::vector<OverviewRowInfo> m_overviewRows;

	// 信号颜色（由ApplySignalColors设置，供OnDrawItem使用）
	COLORREF m_bollSignalColor{ CLR_INVALID };
	COLORREF m_macdSignalColor{ CLR_INVALID };
	COLORREF m_kdjSignalColor{ CLR_INVALID };
	COLORREF m_wrSignalColor{ CLR_INVALID };
	COLORREF m_rsiSignalColor{ CLR_INVALID };
	COLORREF m_maSignalColor{ CLR_INVALID };

	// K线数据源刷新与进度状态
	std::atomic<bool> m_isKLineRefreshing{ false };
	std::atomic<int> m_klineRefreshProgress{ 0 };
	std::wstring m_klineRefreshingStockId;
};
