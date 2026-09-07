#pragma once
#include <afxwin.h>
#include "MarketCenterData.h"
#include <map>
#include <vector>
#include <string>

// ===== 行情中心面板（悬浮窗视图模式）=====
// 纯绘制类（非窗口）：悬浮窗在 OnPaint 里调用 Draw 把行情中心画进自身客户区，
// 与总览/分时一样是悬浮窗的一个视图，不建子窗口、不改窗口尺寸。
// 布局与交互对齐 demo/market_center_demo.html 定稿；数据来自 CMarketCenterData。
class CMarketCenterPanel
{
public:
	CMarketCenterPanel();
	~CMarketCenterPanel();

	// 数据到达消息（取数线程完成后 PostMessage 到通知窗口；悬浮窗处理它触发重绘）
	static const UINT WM_MC_DATA_UPDATED = WM_APP + 140;

	// 绘制行情中心到指定矩形（x,y,w,h 为悬浮窗客户区坐标；顶部标题条由悬浮窗自留）
	void Draw(CDC& memDC, int x, int y, int w, int h);

	// 交互（坐标均为悬浮窗客户区坐标）；返回是否需要重绘
	bool HandleMouseMove(CPoint clientPt);
	void HandleMouseLeave();
	void HandleLButtonDown(CPoint clientPt);
	void HandleMouseWheel(short zDelta, CPoint clientPt);
	bool IsCursorOverInteractive(CPoint clientPt) const;

	// 悬浮窗 1s 定时器调用：刷新时钟 + 拉取过期数据
	void OnTimerTick();
	// 数据到达（悬浮窗收到 WM_MC_DATA_UPDATED 后调用，触发重绘由悬浮窗 Invalidate 完成）
	void OnDataUpdated();
	// 设置数据到达通知窗口（悬浮窗句柄；取数线程完成后向其 PostMessage WM_MC_DATA_UPDATED）
	void SetNotifyWnd(HWND h);

	// 当前面板内容矩形（悬浮窗客户区坐标），未绘制前为空
	const CRect& ContentRect() const { return m_content_rect; }

private:
	// ===== 页面枚举（与侧栏菜单一一对应）=====
	enum McPage
	{
		PAGE_BUBBLE = 0,     // 基金气泡图
		PAGE_ETF_INFLOW = 1, // ETF申购净流入
		PAGE_MAINFLOW = 2,   // 主力资金
		PAGE_TREND = 3,      // 涨跌趋势
		PAGE_ETF_RANK = 4,   // ETF涨跌榜
		PAGE_COUNT = 5
	};

	// ===== 统计卡通用单元 =====
	struct StatCell
	{
		const wchar_t* label;
		std::wstring value;
		COLORREF color;
	};

	// ===== 气泡图布局节点 =====
	struct BubbleNode
	{
		int sectorIdx{ -1 };
		float x{ 0 }, y{ 0 }, r{ 0 };
	};

	// ===== 涨跌分布柱 =====
	struct DistBar
	{
		CRect rect;
		int bin{ 0 };
	};

	// ===== ETF涨跌榜列 =====
	struct RankCol
	{
		CRect rect;
		int key{ 0 };
		bool sortedUp{ false };
	};

	// ===== ETF净流入榜条目 =====
	struct InflowBar
	{
		CRect rect;
		int themeIdx{ -1 };
	};

	// ===== 主题聚合净流入 =====
	struct ThemeInflow
	{
		std::wstring theme;
		double inflow{ 0.0 };
	};

	void DrawAll(Gdiplus::Graphics& g, const CRect& client);
	void DrawSidebar(Gdiplus::Graphics& g, const CRect& rc);
	void DrawPage(Gdiplus::Graphics& g, const CRect& content);
	void DrawPageTitle(Gdiplus::Graphics& g, const CRect& content, const std::wstring& title, const std::wstring& sub);

	void DrawBubblePage(Gdiplus::Graphics& g, const CRect& rc);
	void DrawEtfInflowPage(Gdiplus::Graphics& g, const CRect& rc);
	void DrawThemePanel(Gdiplus::Graphics& g, const CRect& chartRc);
	void DrawMainFlowPage(Gdiplus::Graphics& g, const CRect& rc);
	void DrawTrendPage(Gdiplus::Graphics& g, const CRect& rc);
	void DrawEtfRankPage(Gdiplus::Graphics& g, const CRect& rc);

	void RebuildBubbleLayout(const CRect& chartRc);
	void BuildThemeInflow();
	std::vector<int> SortedRankList() const;

	void UpdateClock();
	void RequestData();
	void SwitchPage(McPage page);
	void RefreshSnapshots();

	CRect m_content_rect;               // 面板内容矩形（悬浮窗客户区坐标）
	CRect m_clock_rect;
	int m_clock_status{ 1 };
	std::wstring m_clock_time;

	McPage m_page{ PAGE_BUBBLE };
	CRect m_menu_item_rects[PAGE_COUNT];
	int m_hover_menu{ -1 };
	bool m_inflow_out{ false };

	// ===== 数据快照 =====
	std::vector<MC::SectorFlow> m_sectors_snapshot;
	time_t m_sectors_snapshot_time{ 0 };
	std::vector<MC::EtfQuote> m_etfs_snapshot;
	time_t m_etfs_snapshot_time{ 0 };
	long long m_etf_total{ 0 };

	// ===== 气泡图 =====
	std::vector<BubbleNode> m_bubble_nodes;
	bool m_bubble_layout_dirty{ true };
	CRect m_bubble_chart_rect;
	CRect m_bubble_detail_rect;
	int m_selected_sector{ -1 };
	int m_hover_bubble{ -1 };

	// ===== ETF净流入页 =====
	std::vector<ThemeInflow> m_theme_inflow;
	std::vector<InflowBar> m_inflow_bars;
	struct StatCardRect { CRect rect; int idx{ 0 }; };
	std::vector<StatCardRect> m_inflow_stat_rects;
	int m_hover_inflow_bar{ -1 };
	int m_hover_inflow_card{ -1 };

	// ===== 主题ETF浮层 =====
	bool m_theme_panel_open{ false };
	CRect m_theme_panel_rect;
	CRect m_theme_close_rect;
	std::wstring m_theme_panel_title;
	std::vector<int> m_theme_row_etfs;
	int m_theme_panel_scroll{ 0 };
	int m_theme_panel_scroll_max{ 0 };

	// ===== 主力资金页 =====
	std::vector<StatCardRect> m_mainflow_stat_rects;
	int m_mainflow_series_mask{ 0xF };
	int m_hover_mainflow_card{ -1 };

	// ===== 涨跌趋势页 =====
	std::vector<StatCardRect> m_trend_stat_rects;
	std::vector<DistBar> m_dist_bars;
	int m_hover_dist_bar{ -1 };

	// ===== ETF涨跌榜页 =====
	std::vector<StatCardRect> m_rank_stat_rects;
	std::vector<RankCol> m_rank_cols;
	CRect m_rank_table_rect;
	int m_rank_scroll{ 0 };
	int m_rank_scroll_max{ 0 };
	int m_rank_sort_key{ 3 };
	int m_rank_sort_dir{ -1 };
	int m_hover_rank_header{ -1 };
	int m_hover_rank_row{ -1 };
	std::vector<int> m_rank_row_etf;

	// hover
	CPoint m_mouse_pos;

	HWND m_notify_wnd{ nullptr };   // 数据到达通知窗口（悬浮窗）
	CSize m_draw_size{ 0, 0 };       // 上次绘制尺寸（变化时重排气泡布局）
};
