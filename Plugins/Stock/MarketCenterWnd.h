#pragma once
#include <afxwin.h>
#include "MarketCenterData.h"
#include <map>
#include <vector>
#include <string>

// ===== 行情中心独立窗口 =====
// 客户区 memDC+GDI+ 全自绘：左侧菜单栏（品牌+5项）+ 右侧内容区（5个页面）+ 右上角时钟。
// 布局与交互对齐 demo/market_center_demo.html 定稿；数据来自 CMarketCenterData（后台线程抓取）。

class CMarketCenterWnd : public CWnd
{
	DECLARE_MESSAGE_MAP()

public:
	CMarketCenterWnd();
	virtual ~CMarketCenterWnd();

	BOOL Create(CWnd* pParent);
	// 以子窗口形态嵌入宿主（悬浮窗行情中心视图）：占据 rc，无标题栏、不注册到 Stock 单例
	BOOL CreateChild(CWnd* pParent, const CRect& rc);
	// 子窗口形态下右键请求宿主退出行情中心视图
	bool IsChildMode() const { return m_childMode; }

	// 子窗口右键 → 宿主退出行情中心（父窗口消息映射引用，需 public）
	static const UINT WM_MC_EXIT_REQUEST = WM_APP + 141;

	// 创建失败时由 Stock 调用以自清理（delete this）
	virtual void PostNcDestroy() override;

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
		int sectorIdx{ -1 };    // CMarketCenterData::m_sectors 下标
		float x{ 0 }, y{ 0 }, r{ 0 };
	};

	// ===== 涨跌分布柱 =====
	struct DistBar
	{
		CRect rect;
		int bin{ 0 };           // bins 下标
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
		int themeIdx{ -1 };     // m_theme_inflow 下标
	};

	// ===== 主题聚合净流入 =====
	struct ThemeInflow
	{
		std::wstring theme;
		double inflow{ 0.0 };
	};

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg LRESULT OnDataUpdated(WPARAM wParam, LPARAM lParam);

private:
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

	// 气泡图确定性装箱布局（demo drawBubble 的 C++ 移植；数据/尺寸变化时重排）
	void RebuildBubbleLayout(const CRect& chartRc);
	// 主题聚合净流入榜（ETF快照变化时重算）
	void BuildThemeInflow();
	// 涨跌榜排序结果（m_etfs_snapshot 下标序列）
	std::vector<int> SortedRankList() const;

	void UpdateClock();
	void RequestData();
	void SwitchPage(McPage page);
	// 数据快照同步（数据版本变化时拷贝到 UI 成员）
	void RefreshSnapshots();

	CRect m_clock_rect;
	CRect m_content_rect;
	int m_clock_status{ 1 };        // 0开市 1休市
	std::wstring m_clock_time;      // HH:MM:SS

	McPage m_page{ PAGE_BUBBLE };
	CRect m_menu_item_rects[PAGE_COUNT];
	int m_hover_menu{ -1 };
	bool m_inflow_out{ false };     // ETF申购净流入榜方向：false=流入榜 true=流出榜

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
	int m_mainflow_series_mask{ 0xF };  // bit0沪主力 bit1深主力 bit2ETF bit3上证
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
	bool m_tracking_mouse{ false };
	bool m_childMode{ false };      // 子窗口形态（悬浮窗内嵌视图）

	static const UINT WM_MC_DATA_UPDATED = WM_APP + 140;   // 数据到达（与 CMarketCenterData::RequestIfStale 一致）
	static const int MC_REFRESH_TIMER = 3001;
};
