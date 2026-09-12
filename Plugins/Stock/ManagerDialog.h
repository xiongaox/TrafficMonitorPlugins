#pragma once
#include "afxdialogex.h"
#include "DataManager.h"
#include <vector>
#include <string>
#include <memory>

#define IDC_STOCK_SEARCH_EDIT 1090

// 平面化深色列表头（自绘，与浮动窗口暗色风格一致）
class CFlatHeaderCtrl : public CHeaderCtrl
{
	DECLARE_MESSAGE_MAP()

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC) { return TRUE; }
	afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
};

// 股票搜索结果与分组添加一体化暗色浮窗
class CSearchResultDropdown : public CWnd
{
	DECLARE_MESSAGE_MAP()
public:
	struct GroupMenuItem
	{
		int id{ 0 };
		std::wstring text;
		bool isSeparator{ false };
		bool isAction{ false };
	};

	std::vector<StockSearchResult> m_results;
	std::vector<GroupMenuItem> m_group_items;
	int m_hover_item{ -1 };
	int m_hover_btn{ -1 };
	int m_selected_stock_idx{ -1 }; // 哪个股票展开了分组菜单 (-1 为未展开)
	int m_hover_group_idx{ -1 };     // 分组列表中的 hover 项
	CRect m_edit_screen_rc;

	std::function<void(const StockSearchResult& stock, int groupId)> m_on_add_to_group;

	CSearchResultDropdown() = default;
	virtual ~CSearchResultDropdown() = default;

	BOOL CreatePopup(CWnd* pParent);
	void ShowResults(const std::vector<StockSearchResult>& results, const CRect& editScreenRc, const std::vector<GroupMenuItem>& groupItems);
	void HidePopup();

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC) { return TRUE; }
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);

	int GetResultsWidth() const;
	int GetMenuWidth() const;
	void UpdatePopupPosition();
};

// 暗色风格弹出菜单 (替代原生亮白 CMenu 弹窗)
class CDarkPopupMenu : public CWnd
{
	DECLARE_MESSAGE_MAP()
public:
	struct MenuItem
	{
		int id{ 0 };
		std::wstring text;
		bool isChecked{ false };
		bool isSeparator{ false };
		bool isDestructive{ false };
	};

	std::vector<MenuItem> m_items;
	int m_hover_idx{ -1 };
	int m_selected_id{ 0 };
	bool m_is_open{ false };

	CDarkPopupMenu() = default;
	virtual ~CDarkPopupMenu() = default;

	BOOL CreatePopup(CWnd* pParent);
	int TrackMenu(const CPoint& screenPt, const std::vector<MenuItem>& items, int minWidth = 100);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC) { return TRUE; }
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
};

// 深色主题扁平下拉框（自绘，与暗色输入框/卡片保持统一视觉设计）
class CDarkComboBox : public CComboBox
{
	DECLARE_MESSAGE_MAP()

public:
	CDarkComboBox() = default;
	virtual ~CDarkComboBox() = default;

	// 关闭态字段框高度（像素），由布局方设置，用于让闭合框体与输入框字段框等高
	int m_field_height{ 0 };

protected:
	bool m_is_hovered{ false };

	virtual void PreSubclassWindow() override;
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC) { return TRUE; }
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
	afx_msg void OnNcPaint();
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct) override;
	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct) override;
};

// CManagerDialog 对话框

class CManagerDialog : public CDialog
{
	DECLARE_DYNAMIC(CManagerDialog)

public:
	enum PageIndex
	{
		PAGE_BASIC = 0,   // 基础设置
		PAGE_INDEX = 1,   // 指数编辑
		PAGE_GROUPS = 2,  // 分组管理 (自选股/持仓/自定义)
		PAGE_MA = 3,      // 均线日配置
		PAGE_METRICS = 4, // 指标栏配置
		PAGE_WEBDAV = 5,  // 云端备份 (WebDAV)
		PAGE_API_HEALTH = 6, // 接口检测 (心跳健康监控)
		PAGE_ABOUT = 7    // 关于插件
	};

	enum GroupSubTab
	{
		TAB_WATCHLIST = 0, // 自选股
		TAB_POSITIONS = 1  // 持仓
		// >= 2: 自定义分组索引 (对应 m_data.m_custom_groups[idx - 2])
	};

	CManagerDialog(CWnd* pParent = nullptr);   // 标准构造函数
	virtual ~CManagerDialog();

	SettingData m_data;

	// ===== 内嵌子窗口模式（悬浮窗设置视图） =====
	// 加载 IDD_MANAGER_DIALOG 模板副本并剥除弹出/标题栏样式后 CreateIndirect 为 WS_CHILD
	bool CreateAsChild(CWnd* pParent);
	// 内嵌模式确定(true)/取消或ESC(false) 后由宿主悬浮窗接管收起；模态模式保持 EndDialog
	std::function<void(bool)> m_on_settings_closed;
	// 即时生效：读取全部控件值写入 g_data 并保存/热更新（原「确定」按钮的提交逻辑）
	void ApplySettings();

	// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MANAGER_DIALOG };
#endif

private:
	CSize m_min_size;		// 窗口的最小大小
	PageIndex m_current_page{ PAGE_BASIC };
	int m_current_group_tab{ 0 }; // 0:自选股, 1:持仓, >=2:各自定义分组
	int m_hover_menu{ -1 };
	int m_hover_index_card{ -1 };
	int m_hover_ma_tag_del{ -1 };
	int m_hover_ma_slot{ -1 };
	int m_hover_ma_preset{ -1 };
	int m_hover_metric_tag_del{ -1 };
	int m_hover_metric_slot{ -1 };
	int m_hover_metric_preset{ -1 };
	int m_hover_group_tab{ -1 };
	int m_hover_index_mode{ -1 };
	CRect m_index_mode_rects[3];
	CRect m_display_area_rects[5];
	int m_hover_display_area{ -1 };
	int m_index_scroll_y{ 0 };
	int m_page_scroll_y{ 0 };           // 方案B：右侧内容区隐藏式滚动偏移（滚轮驱动，不绘制滚动条）
	bool m_as_child{ false };           // 内嵌子窗口模式：作为悬浮窗“设置”视图的 WS_CHILD 子对话框
	bool m_tracking_mouse{ false };

	// ===== 暗色主题自绘状态 =====
	std::map<UINT, bool> m_checkStates; // 自绘复选框状态（控件为 BS_OWNERDRAW，勾选状态自行托管）
	std::map<UINT, CRect> m_editFieldRects; // 输入框字段矩形（控件在字段内垂直居中，整框由 OnPaint 绘制）
	CFlatHeaderCtrl m_hdr_stock;        // 三个列表的平面化表头
	CFlatHeaderCtrl m_hdr_pos;
	CFlatHeaderCtrl m_hdr_custom;

	// 控件对象
	CListCtrl m_stock_listctrl;
	CListCtrl m_pos_listctrl;
	CListCtrl m_custom_listctrl;
	CEdit m_search_edit;
	CSearchResultDropdown m_search_dropdown;
	CEdit m_ma_input_edit;
	CButton m_ma_add_btn;
	CButton m_mgr_add_btn;
	CButton m_mgr_edit_btn;
	CButton m_mgr_del_btn;
	CButton m_mgr_up_btn;
	CButton m_mgr_down_btn;
	CButton m_mgr_del_group_btn;
	CButton m_group_sort_btn;   // 分组管理页右上角「分组排序」入口
	CButton m_reset_btn;        // 基础设置页「重置所有数据」按钮
	CDarkComboBox m_display_area_combo;

	// 深色主题 GDI 资源
	CBrush m_dark_brush;
	CBrush m_card_brush;
	CBrush m_edit_brush;
	CFont m_font;
	CFont m_font_bold;
	CFont m_font_title;

	// 布局与尺寸
	int m_menu_width{ 145 };
	std::vector<CRect> m_menu_rects;
	std::vector<CRect> m_index_card_rects;
	std::vector<CRect> m_ma_tag_rects;
	std::vector<CRect> m_ma_tag_del_rects;
	std::vector<CRect> m_ma_slot_rects;   // 均线页空槽位（点击聚焦输入框）
	std::vector<CRect> m_ma_preset_rects; // 均线页常用周期快捷添加按钮
	std::vector<CRect> m_boll_vis_check_rects; // 均线页「分时图布林带显示」点击区域（固定3项：[0]=上轨、[1]=中轨、[2]=下轨）
	std::vector<CRect> m_metric_tag_rects;
	std::vector<CRect> m_metric_tag_del_rects;
	std::vector<CRect> m_metric_slot_rects;
	struct MetricCandidateItem {
		std::wstring name;
		CRect rect;
	};
	std::vector<MetricCandidateItem> m_metric_candidates;
	std::vector<CRect> m_group_tab_rects;
	std::vector<CRect> m_group_pref_radio_rects; // 分组页右下角「优先展示」单选区域 [0]=自选股 [1]=持仓分组
	CRect m_about_author_rect; // 关于页作者主页超链接区域
	CRect m_about_repo_rect;   // 关于页项目仓库超链接区域

	// 内部辅助方法
	std::wstring GetStockName(const std::wstring& code);

	// ===== 方案B：右侧内容区隐藏式滚动（无滚动条，滚轮驱动） =====
	void GetScrollContentRect(CRect& contentRect) const; // 右侧内容可视区（未含滚动偏移）
	bool InScrollContent(CPoint point);                  // 点是否在右侧内容可视区内
	void SetPageScroll(int scrollY);                     // 钳制滚动偏移并联动控件布局与重绘
	int CalcPageContentHeight();                         // 当前页内容自然总高（0 = 不启用通用滚动）
	int MeasureMetricCard2Height(int rightWidth);        // 指标页候选库自然高度（与绘制排布一致）
	int ContentBottomPad() const;                        // 内容区底部留白（内嵌无按钮条时收窄，分组页仍留操作按钮行）
	void ApplyIfEmbedded();                              // 内嵌模式即时提交设置（模态模式等「确定」）

	// ===== 暗色主题自绘辅助 =====
	bool IsChecked(UINT nID) const;
	void SetCheck(UINT nID, bool checked);
	bool IsCheckCtrl(UINT nID) const;
	bool IsPrimaryBtn(UINT nID) const;
	bool IsDestructiveBtn(UINT nID) const;
	void DrawFlatButton(CDC& dc, const CRect& rect, const CString& text, bool primary, bool destructive, bool hot, bool pressed);
	void DrawControlBorder(Gdiplus::Graphics& g, UINT nID);
	void DrawSectionTitle(Gdiplus::Graphics& g, int x, int y, const std::wstring& title);
	// 单行 EDIT 不支持垂直居中：控件实际高度缩为字段高-8 并居中放置，
	// 字段整框（底色+边框）由 DrawControlBorder 绘制，文字自然居中
	void PlaceEditInField(UINT nID, const CRect& fieldRect);
	bool TryAddMaDay(int day); // 校验并添加均线周期，失败时弹出对应提示，返回是否成功
	bool TryAddMetric(const std::wstring& name); // 添加指标项（上限4项）
	void SwitchPage(PageIndex page);
	void SwitchGroupTab(int tab);
	void UpdateControlsLayout();
	void AdjustListColumns(CListCtrl& list, int tabType);
	void RefreshStockList();
	void RefreshPositionList();
	void RefreshCustomList();
	void DrawSidebar(Gdiplus::Graphics& g, const CRect& clientRect);
	void DrawHeader(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawBasicPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawIndexPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawGroupPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawMaPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawMetricPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawWebDavPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawApiHealthPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawAboutPage(Gdiplus::Graphics& g, const CRect& contentRect);

	// ===== 接口检测页控件 =====
	CButton m_api_test_btn;       // 接口检测页右上角「立即重新检测」按钮
	bool m_api_probing{ false };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持
	virtual BOOL PreTranslateMessage(MSG* pMsg) override;

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg BOOL OnNcActivate(BOOL bActive);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);

	// 搜索与列表事件
	afx_msg void OnSearchEditChange();
	afx_msg void OnListItemClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLbnDblclkMgrList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLbnDblclkPosList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLbnDblclkCustomList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnAddBtnClick();
	afx_msg void OnEditBtnClick();
	afx_msg void OnDelBtnClick();
	afx_msg void OnDelGroupBtnClick();
	afx_msg void OnMoveUpBtnClick();
	afx_msg void OnMoveDownBtnClick();
	afx_msg void OnMaAddBtnClick();
	afx_msg void OnGroupSortBtnClick();

	// 基础设置事件
	afx_msg void OnClickedFullDayCheck();
	afx_msg void OnBnClickedShowFluctuationCheck();
	afx_msg void OnBnClickedShowTodayProfitCheck();
	afx_msg void OnBnClickedUseSocks5ProxyCheck();
	afx_msg void OnBnClickedResetData();

	// 云端备份事件
	afx_msg void OnBnClickedWebDavTestBtn();
	afx_msg void OnBnClickedWebDavUploadBtn();
	afx_msg void OnBnClickedWebDavDownloadBtn();
	afx_msg void OnBnClickedWebDavAutoSyncCheck();
	afx_msg void OnBnClickedWebDavAutoBackupCheck();
	afx_msg LRESULT OnWebDavResult(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedApiTestBtn();
	afx_msg LRESULT OnApiProbeFinished(WPARAM wParam, LPARAM lParam);
	bool m_webdav_busy{ false };  // 是否有 WebDAV 操作在后台执行（此时禁用操作按钮）
	std::wstring m_webdav_restore_file; // 待恢复的云端备份文件名（在列表中选中后回填）
	std::wstring m_webdav_restore_name; // 待恢复备份的展示名（用于确认与成功提示）
	void StartWebDavAsync(int op);            // 投递 WebDAV 操作到取数线程
	void ApplyWebDavRestore(const std::string& data, const std::wstring& backupName = L""); // 将云端备份内容应用到本地配置与界面
	afx_msg void OnEditFocusChanged();
	afx_msg void OnEditFocusLost();   // 输入框失焦：内嵌模式即时提交字段值
	afx_msg void OnListCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);

	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
};
