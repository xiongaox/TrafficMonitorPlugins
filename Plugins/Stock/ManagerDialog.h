#pragma once
#include "afxdialogex.h"
#include "DataManager.h"
#include <vector>
#include <string>
#include <memory>

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
		PAGE_WEBDAV = 4,  // 云端备份 (WebDAV)
		PAGE_ABOUT = 5    // 关于插件
	};

	enum GroupSubTab
	{
		TAB_WATCHLIST = 0, // 自选股
		TAB_POSITIONS = 1, // 持仓
		TAB_CUSTOM = 2     // 自定义
	};

	CManagerDialog(CWnd* pParent = nullptr);   // 标准构造函数
	virtual ~CManagerDialog();

	SettingData m_data;

	// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MANAGER_DIALOG };
#endif

private:
	CSize m_min_size;		// 窗口的最小大小
	PageIndex m_current_page{ PAGE_BASIC };
	GroupSubTab m_current_group_tab{ TAB_WATCHLIST };
	int m_hover_menu{ -1 };
	int m_hover_index_card{ -1 };
	int m_hover_ma_tag_del{ -1 };
	int m_hover_group_tab{ -1 };
	bool m_tracking_mouse{ false };

	// 控件对象
	CListCtrl m_stock_listctrl;
	CListCtrl m_pos_listctrl;
	CListCtrl m_custom_listctrl;
	CEdit m_ma_input_edit;
	CButton m_ma_add_btn;
	CButton m_mgr_add_btn;
	CButton m_mgr_edit_btn;
	CButton m_mgr_del_btn;
	CButton m_mgr_up_btn;
	CButton m_mgr_down_btn;

	// 布局与尺寸
	int m_menu_width{ 140 };
	std::vector<CRect> m_menu_rects;
	std::vector<CRect> m_index_card_rects;
	std::vector<CRect> m_ma_tag_rects;
	std::vector<CRect> m_ma_tag_del_rects;
	std::vector<CRect> m_group_tab_rects;
	CRect m_about_link_rect;

	// 内部辅助方法
	std::wstring GetStockName(const std::wstring& code);
	void SwitchPage(PageIndex page);
	void SwitchGroupTab(GroupSubTab tab);
	void UpdateControlsLayout();
	void RefreshStockList();
	void RefreshPositionList();
	void RefreshCustomList();
	void DrawSidebar(Gdiplus::Graphics& g, const CRect& clientRect);
	void DrawHeader(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawIndexPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawGroupPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawMaPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawWebDavPage(Gdiplus::Graphics& g, const CRect& contentRect);
	void DrawAboutPage(Gdiplus::Graphics& g, const CRect& contentRect);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);

	// 列表与按钮事件
	afx_msg void OnListItemClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLbnDblclkMgrList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLbnDblclkPosList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLbnDblclkCustomList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnAddBtnClick();
	afx_msg void OnEditBtnClick();
	afx_msg void OnDelBtnClick();
	afx_msg void OnMoveUpBtnClick();
	afx_msg void OnMoveDownBtnClick();
	afx_msg void OnMaAddBtnClick();

	// 基础设置事件
	afx_msg void OnClickedFullDayCheck();
	afx_msg void OnBnClickedShowStockNameCheck();
	afx_msg void OnBnClickedColorWithPriceCheck();
	afx_msg void OnBnClickedShowFluctuationCheck();
	afx_msg void OnBnClickedUseSocks5ProxyCheck();

	// 云端备份事件
	afx_msg void OnBnClickedWebDavTestBtn();
	afx_msg void OnBnClickedWebDavUploadBtn();
	afx_msg void OnBnClickedWebDavDownloadBtn();
	afx_msg void OnBnClickedWebDavAutoSyncCheck();
	afx_msg void OnBnClickedWebDavAutoBackupCheck();

	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
};
