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
	// 点击某只 ETF（wParam = m_etfs_snapshot 下标）：悬浮窗跳转首页 K 线临时查看
	static const UINT WM_MC_ETF_CLICKED = WM_APP + 141;

	// 绘制行情中心到指定矩形（x,y,w,h 为悬浮窗客户区坐标；顶部标题条由悬浮窗自留）
	void Draw(CDC& memDC, int x, int y, int w, int h);
	// 在悬浮窗顶部标题条内绘制开市/休市时钟（由 FloatingWnd 在行情中心模式下调用）
	void DrawHeaderClock(CDC& memDC, const CRect& rc);

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
	// 快照下标转 ETF 六位代码（越界返回空串；供悬浮窗处理 WM_MC_ETF_CLICKED）
	std::wstring EtfCodeAt(int idx) const;

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

	// ===== 板块资金流矩形图布局单元 =====
	struct TreeCell
	{
		int sectorIdx{ -1 };
		CRect rect;   // 悬浮窗客户区坐标
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
		CRect rect;      // 整行命中区（名称列 + 条形 + 数值）
		CRect barRect;   // 条形本身（hover 高亮框）
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
	void DrawMainFlowPage(Gdiplus::Graphics& g, const CRect& rc);
	void DrawTrendPage(Gdiplus::Graphics& g, const CRect& rc);
	void DrawEtfRankPage(Gdiplus::Graphics& g, const CRect& rc);

	void RebuildTreemapLayout(const CRect& chartRc);
	void BuildThemeInflow();
	// 主题的代表 ETF 快照下标（主题内 |主力净流入| 最大者；无效返回 -1）
	int ThemeRepresentEtf(int themeIdx) const;
	std::vector<int> SortedRankList() const;

	void UpdateClock();
	void RequestData();
	void SwitchPage(McPage page);
	void RefreshSnapshots();

	// 数据集状态文案：加载中 / 获取失败（点击重试）
	std::wstring StatusText(CMarketCenterData::DataSet ds, const std::wstring& loading) const;
	// 绘制状态文案（加载中/失败），并设置可点击重试区域
	void DrawStatus(Gdiplus::Graphics& g, const CRect& rc, CMarketCenterData::DataSet ds,
		const std::wstring& loading, const Gdiplus::Font* font);
	// 当前页对应的数据集
	CMarketCenterData::DataSet CurrentDataSet() const;
	// 用户点击"获取失败"文案时重试当前页
	void RetryCurrentPage();

	CRect m_content_rect;               // 面板内容矩形（悬浮窗客户区坐标）
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
	std::vector<TreeCell> m_treemap_cells;
	bool m_bubble_layout_dirty{ true };
	CRect m_bubble_chart_rect;
	CRect m_bubble_detail_rect;
	int m_selected_sector{ -1 };
	int m_hover_bubble{ -1 };
	CRect m_bubble_stat_rects[2];   // 流入/流出合计卡片（点击切换树图单色视图）
	int m_treemap_mode{ 0 };        // 0=全部红绿 1=仅流入 2=仅流出
	int m_hover_bubble_stat{ -1 };

	// ===== ETF净流入页 =====
	std::vector<ThemeInflow> m_theme_inflow;
	std::vector<InflowBar> m_inflow_bars;
	struct StatCardRect { CRect rect; int idx{ 0 }; };
	std::vector<StatCardRect> m_inflow_stat_rects;
	int m_hover_inflow_bar{ -1 };
	int m_hover_inflow_card{ -1 };

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

	CRect m_status_rect;   // "获取失败，点击重试"文案的可点击区域（Draw 时设置）

	HWND m_notify_wnd{ nullptr };   // 数据到达通知窗口（悬浮窗）
	CSize m_draw_size{ 0, 0 };       // 上次绘制尺寸（变化时重排气泡布局）
};
