// ManagerDialog.cpp: 实现文件
//

#include "pch.h"
#include "Stock.h"
#include "afxdialogex.h"
#include "ManagerDialog.h"
#include "Common.h"
#include "StockFetchThread.h"
#include "OptionsDlg.h"
#include "WebDavSync.h"
#include <Windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <set>
#include <shellapi.h>
#include <ctime>

#pragma comment(lib, "gdiplus.lib")

// CManagerDialog 对话框

IMPLEMENT_DYNAMIC(CManagerDialog, CDialog)

CManagerDialog::CManagerDialog(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_MANAGER_DIALOG, pParent)
{
	m_menu_rects.resize(6);
	m_dark_brush.CreateSolidBrush(RGB(15, 23, 42));   // #0F172A
	m_card_brush.CreateSolidBrush(RGB(30, 41, 59));   // #1E293B
	m_edit_brush.CreateSolidBrush(RGB(15, 23, 42));   // #0F172A
}

CManagerDialog::~CManagerDialog()
{
}

void CManagerDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MGR_LIST, m_stock_listctrl);
	DDX_Control(pDX, IDC_POS_LIST, m_pos_listctrl);
	DDX_Control(pDX, IDC_CUSTOM_LIST, m_custom_listctrl);
	DDX_Control(pDX, IDC_MA_INPUT_EDIT, m_ma_input_edit);
	DDX_Control(pDX, IDC_MA_ADD_BTN, m_ma_add_btn);
	DDX_Control(pDX, IDC_MGR_ADD_BTN, m_mgr_add_btn);
	DDX_Control(pDX, IDC_MGR_EDIT_BTN, m_mgr_edit_btn);
	DDX_Control(pDX, IDC_MGR_DEL_BTN, m_mgr_del_btn);
	DDX_Control(pDX, IDC_MGR_MOVE_UP_BTN, m_mgr_up_btn);
	DDX_Control(pDX, IDC_MGR_MOVE_DOWN_BTN, m_mgr_down_btn);
}

BEGIN_MESSAGE_MAP(CManagerDialog, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
	ON_WM_GETMINMAXINFO()

	ON_NOTIFY(NM_CLICK, IDC_MGR_LIST, &CManagerDialog::OnListItemClick)
	ON_NOTIFY(NM_DBLCLK, IDC_MGR_LIST, &CManagerDialog::OnLbnDblclkMgrList)
	ON_NOTIFY(NM_DBLCLK, IDC_POS_LIST, &CManagerDialog::OnLbnDblclkPosList)
	ON_NOTIFY(NM_DBLCLK, IDC_CUSTOM_LIST, &CManagerDialog::OnLbnDblclkCustomList)

	ON_BN_CLICKED(IDC_MGR_ADD_BTN, &CManagerDialog::OnAddBtnClick)
	ON_BN_CLICKED(IDC_MGR_EDIT_BTN, &CManagerDialog::OnEditBtnClick)
	ON_BN_CLICKED(IDC_MGR_DEL_BTN, &CManagerDialog::OnDelBtnClick)
	ON_BN_CLICKED(IDC_MGR_MOVE_UP_BTN, &CManagerDialog::OnMoveUpBtnClick)
	ON_BN_CLICKED(IDC_MGR_MOVE_DOWN_BTN, &CManagerDialog::OnMoveDownBtnClick)
	ON_BN_CLICKED(IDC_MA_ADD_BTN, &CManagerDialog::OnMaAddBtnClick)

	ON_BN_CLICKED(IDC_FULL_DAY_CHECK, &CManagerDialog::OnClickedFullDayCheck)
	ON_BN_CLICKED(IDC_SHOW_STOCK_NAME_CHECK, &CManagerDialog::OnBnClickedShowStockNameCheck)
	ON_BN_CLICKED(IDC_COLOR_WITH_PRICE_CHECK, &CManagerDialog::OnBnClickedColorWithPriceCheck)
	ON_BN_CLICKED(IDC_SHOW_FLUCTUATION_CHECK, &CManagerDialog::OnBnClickedShowFluctuationCheck)
	ON_BN_CLICKED(IDC_USE_SOCKS5_PROXY_CHECK, &CManagerDialog::OnBnClickedUseSocks5ProxyCheck)

	ON_BN_CLICKED(IDC_WEBDAV_TEST_BTN, &CManagerDialog::OnBnClickedWebDavTestBtn)
	ON_BN_CLICKED(IDC_WEBDAV_UPLOAD_BTN, &CManagerDialog::OnBnClickedWebDavUploadBtn)
	ON_BN_CLICKED(IDC_WEBDAV_DOWNLOAD_BTN, &CManagerDialog::OnBnClickedWebDavDownloadBtn)
	ON_BN_CLICKED(IDC_WEBDAV_AUTO_SYNC_CHECK, &CManagerDialog::OnBnClickedWebDavAutoSyncCheck)
	ON_BN_CLICKED(IDC_WEBDAV_AUTO_BACKUP_CHECK, &CManagerDialog::OnBnClickedWebDavAutoBackupCheck)

	ON_BN_CLICKED(IDOK, &CManagerDialog::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CManagerDialog::OnBnClickedCancel)
END_MESSAGE_MAP()

// CManagerDialog 消息处理程序

BOOL CManagerDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	HICON hIcon = g_data.GetIcon(IDI_STOCK);
	SetIcon(hIcon, FALSE);

	// 设置窗口默认大小和最小尺寸
	int initWidth = g_data.DPI(740);
	int initHeight = g_data.DPI(510);
	m_min_size.cx = g_data.DPI(660);
	m_min_size.cy = g_data.DPI(450);

	CRect curRect;
	GetWindowRect(curRect);
	SetWindowPos(nullptr, curRect.left, curRect.top, initWidth, initHeight, SWP_NOMOVE | SWP_NOZORDER);

	m_menu_width = g_data.DPI(135);

	// 创建现代化微软雅黑统一字体
	LOGFONT lf{};
	GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(LOGFONT), &lf);
	wcscpy_s(lf.lfFaceName, L"微软雅黑");
	lf.lfHeight = -g_data.DPI(12);
	lf.lfWeight = FW_NORMAL;
	m_font.CreateFontIndirect(&lf);

	EnumChildWindows(m_hWnd, [](HWND hWnd, LPARAM lParam) -> BOOL {
		::SendMessage(hWnd, WM_SETFONT, lParam, TRUE);
		return TRUE;
	}, (LPARAM)m_font.GetSafeHandle());

	// 初始化列表深色背景与扩展属性
	DWORD dwStyle = m_stock_listctrl.GetExtendedStyle();
	dwStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;

	auto setupListDarkTheme = [dwStyle](CListCtrl& list) {
		list.SetExtendedStyle(dwStyle);
		list.SetBkColor(RGB(15, 23, 42));
		list.SetTextBkColor(RGB(15, 23, 42));
		list.SetTextColor(RGB(241, 245, 249));
	};

	setupListDarkTheme(m_stock_listctrl);
	m_stock_listctrl.InsertColumn(0, L"代码", LVCFMT_LEFT, g_data.DPI(75));
	m_stock_listctrl.InsertColumn(1, L"股票名称", LVCFMT_LEFT, g_data.DPI(90));
	m_stock_listctrl.InsertColumn(2, L"关注低价", LVCFMT_RIGHT, g_data.DPI(75));
	m_stock_listctrl.InsertColumn(3, L"关注高价", LVCFMT_RIGHT, g_data.DPI(75));
	m_stock_listctrl.InsertColumn(4, L"状态栏显示", LVCFMT_CENTER, g_data.DPI(75));

	setupListDarkTheme(m_pos_listctrl);
	m_pos_listctrl.InsertColumn(0, L"代码", LVCFMT_LEFT, g_data.DPI(70));
	m_pos_listctrl.InsertColumn(1, L"股票名称", LVCFMT_LEFT, g_data.DPI(85));
	m_pos_listctrl.InsertColumn(2, L"成本价", LVCFMT_RIGHT, g_data.DPI(65));
	m_pos_listctrl.InsertColumn(3, L"持股数", LVCFMT_RIGHT, g_data.DPI(65));
	m_pos_listctrl.InsertColumn(4, L"买入日期", LVCFMT_CENTER, g_data.DPI(80));
	m_pos_listctrl.InsertColumn(5, L"成本总额", LVCFMT_RIGHT, g_data.DPI(75));
	m_pos_listctrl.InsertColumn(6, L"当前价", LVCFMT_RIGHT, g_data.DPI(65));
	m_pos_listctrl.InsertColumn(7, L"浮动盈亏", LVCFMT_RIGHT, g_data.DPI(75));

	setupListDarkTheme(m_custom_listctrl);
	m_custom_listctrl.InsertColumn(0, L"代码", LVCFMT_LEFT, g_data.DPI(80));
	m_custom_listctrl.InsertColumn(1, L"股票名称", LVCFMT_LEFT, g_data.DPI(100));
	m_custom_listctrl.InsertColumn(2, L"关注低价", LVCFMT_RIGHT, g_data.DPI(75));
	m_custom_listctrl.InsertColumn(3, L"关注高价", LVCFMT_RIGHT, g_data.DPI(75));

	// 加载基础配置控件值
	CheckDlgButton(IDC_FULL_DAY_CHECK, m_data.m_full_day ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SHOW_STOCK_NAME_CHECK, m_data.m_show_stock_name ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_COLOR_WITH_PRICE_CHECK, m_data.m_color_with_price ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy ? BST_CHECKED : BST_UNCHECKED);
	SetDlgItemText(IDC_SOCKS5_PROXY_EDIT, m_data.m_socks5_proxy.c_str());

	CString strKlineW, strKlineH;
	strKlineW.Format(_T("%d"), static_cast<int>(m_data.m_kline_width));
	SetDlgItemText(IDC_KLINE_WIDTH_EDIT, strKlineW);
	strKlineH.Format(_T("%d"), static_cast<int>(m_data.m_kline_height));
	SetDlgItemText(IDC_KLINE_HEIGHT_EDIT, strKlineH);

	// 加载 WebDAV 云端备份控件值
	SetDlgItemText(IDC_WEBDAV_URL_EDIT, m_data.m_webdav_url.c_str());
	SetDlgItemText(IDC_WEBDAV_USER_EDIT, m_data.m_webdav_username.c_str());
	SetDlgItemText(IDC_WEBDAV_PWD_EDIT, m_data.m_webdav_password.c_str());
	SetDlgItemText(IDC_WEBDAV_DIR_EDIT, m_data.m_webdav_dir.c_str());
	CheckDlgButton(IDC_WEBDAV_AUTO_SYNC_CHECK, m_data.m_webdav_auto_sync ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_WEBDAV_AUTO_BACKUP_CHECK, m_data.m_webdav_auto_backup ? BST_CHECKED : BST_UNCHECKED);

	if (m_data.m_ma_days.empty())
		m_data.m_ma_days = { 5, 17, 60 };

	RefreshStockList();
	RefreshPositionList();
	RefreshCustomList();

	SwitchPage(PAGE_BASIC);
	return TRUE;
}

HBRUSH CManagerDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (nCtlColor == CTLCOLOR_STATIC)
	{
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(RGB(241, 245, 249));
		return (HBRUSH)m_dark_brush.GetSafeHandle();
	}
	else if (nCtlColor == CTLCOLOR_EDIT || nCtlColor == CTLCOLOR_LISTBOX)
	{
		pDC->SetBkMode(OPAQUE);
		pDC->SetBkColor(RGB(15, 23, 42));
		pDC->SetTextColor(RGB(248, 250, 252));
		return (HBRUSH)m_edit_brush.GetSafeHandle();
	}
	else if (nCtlColor == CTLCOLOR_BTN)
	{
		pDC->SetBkMode(TRANSPARENT);
		return (HBRUSH)m_dark_brush.GetSafeHandle();
	}
	else if (nCtlColor == CTLCOLOR_DLG)
	{
		return (HBRUSH)m_dark_brush.GetSafeHandle();
	}
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

std::wstring CManagerDialog::GetStockName(const std::wstring& code)
{
	auto stockData = g_data.GetStockData(code);
	if (stockData && !stockData->info.displayName.empty())
	{
		return stockData->info.displayName;
	}
	return L"";
}

void CManagerDialog::RefreshStockList()
{
	m_stock_listctrl.DeleteAllItems();
	for (size_t i = 0; i < m_data.m_stock_codes.size(); ++i)
	{
		const auto& code = m_data.m_stock_codes[i];
		int nItem = m_stock_listctrl.InsertItem(static_cast<int>(i), code.c_str());
		std::wstring name = GetStockName(code);
		m_stock_listctrl.SetItemText(nItem, 1, name.c_str());

		double low = g_data.GetAlertLowPrice(code);
		double high = g_data.GetAlertHighPrice(code);
		if (low > 0)
		{
			CString lowStr;
			lowStr.Format(_T("%.2f"), low);
			m_stock_listctrl.SetItemText(nItem, 2, lowStr);
		}
		if (high > 0)
		{
			CString highStr;
			highStr.Format(_T("%.2f"), high);
			m_stock_listctrl.SetItemText(nItem, 3, highStr);
		}

		if (g_data.GetShowInStatusBar(code))
		{
			m_stock_listctrl.SetItemText(nItem, 4, L"是");
		}
		else
		{
			m_stock_listctrl.SetItemText(nItem, 4, L"-");
		}
	}
}

void CManagerDialog::RefreshPositionList()
{
	m_pos_listctrl.DeleteAllItems();
	int nItem = 0;
	for (const auto& code : m_data.m_stock_codes)
	{
		double cost = g_data.GetCostPrice(code);
		double count = g_data.GetHoldingCount(code);
		std::wstring buyDate = g_data.GetBuyDate(code);
		if (cost > 0 || count > 0)
		{
			m_pos_listctrl.InsertItem(nItem, code.c_str());
			std::wstring name = GetStockName(code);
			m_pos_listctrl.SetItemText(nItem, 1, name.c_str());

			CString strCost, strCount, strDate, strTotal, strCurPrice, strProfit;
			strCost.Format(_T("%.2f"), cost);
			strCount.Format(_T("%.0f"), count);
			strDate = buyDate.c_str();

			double totalCost = cost * count;
			strTotal.Format(_T("%.2f"), totalCost);

			double curPrice = 0.0;
			auto stockData = g_data.GetStockData(code);
			if (stockData)
				curPrice = stockData->info.currentPrice;

			strCurPrice.Format(_T("%.2f"), curPrice);

			double profit = (curPrice - cost) * count;
			strProfit.Format(_T("%+.2f"), profit);

			m_pos_listctrl.SetItemText(nItem, 2, strCost);
			m_pos_listctrl.SetItemText(nItem, 3, strCount);
			m_pos_listctrl.SetItemText(nItem, 4, strDate);
			m_pos_listctrl.SetItemText(nItem, 5, strTotal);
			m_pos_listctrl.SetItemText(nItem, 6, strCurPrice);
			m_pos_listctrl.SetItemText(nItem, 7, strProfit);

			nItem++;
		}
	}
}

void CManagerDialog::RefreshCustomList()
{
	m_custom_listctrl.DeleteAllItems();
	for (size_t i = 0; i < m_data.m_custom_group_codes.size(); ++i)
	{
		const auto& code = m_data.m_custom_group_codes[i];
		int nItem = m_custom_listctrl.InsertItem(static_cast<int>(i), code.c_str());
		std::wstring name = GetStockName(code);
		m_custom_listctrl.SetItemText(nItem, 1, name.c_str());

		double low = g_data.GetAlertLowPrice(code);
		double high = g_data.GetAlertHighPrice(code);
		if (low > 0)
		{
			CString lowStr;
			lowStr.Format(_T("%.2f"), low);
			m_custom_listctrl.SetItemText(nItem, 2, lowStr);
		}
		if (high > 0)
		{
			CString highStr;
			highStr.Format(_T("%.2f"), high);
			m_custom_listctrl.SetItemText(nItem, 3, highStr);
		}
	}
}

void CManagerDialog::SwitchPage(PageIndex page)
{
	m_current_page = page;
	UpdateControlsLayout();
	Invalidate();
}

void CManagerDialog::SwitchGroupTab(GroupSubTab tab)
{
	m_current_group_tab = tab;
	UpdateControlsLayout();
	Invalidate();
}

void CManagerDialog::UpdateControlsLayout()
{
	CRect clientRect;
	GetClientRect(clientRect);
	if (clientRect.Width() <= 0 || clientRect.Height() <= 0)
		return;

	int rightLeft = m_menu_width + g_data.DPI(18);
	int rightTop = g_data.DPI(65);
	int rightWidth = clientRect.Width() - rightLeft - g_data.DPI(18);
	int rightBottom = clientRect.Height() - g_data.DPI(50);

	// 基础设置控件列表
	const int basicControlIds[] = {
		IDC_FULL_DAY_CHECK, IDC_SHOW_STOCK_NAME_CHECK, IDC_COLOR_WITH_PRICE_CHECK,
		IDC_SHOW_FLUCTUATION_CHECK, IDC_USE_SOCKS5_PROXY_CHECK,
		IDC_SOCKS5_PROXY_STATIC, IDC_SOCKS5_PROXY_EDIT,
		IDC_KLINE_WIDTH_STATIC, IDC_KLINE_WIDTH_EDIT,
		IDC_KLINE_HEIGHT_STATIC, IDC_KLINE_HEIGHT_EDIT
	};

	bool isBasic = (m_current_page == PAGE_BASIC);
	for (int id : basicControlIds)
	{
		CWnd* pWnd = GetDlgItem(id);
		if (pWnd && pWnd->GetSafeHwnd())
			pWnd->ShowWindow(isBasic ? SW_SHOW : SW_HIDE);
	}

	if (isBasic)
	{
		// 卡片 1 内部控件布局 (行情与走势展示)
		int card1Top = rightTop;
		CWnd* pChk1 = GetDlgItem(IDC_FULL_DAY_CHECK);
		CWnd* pChk2 = GetDlgItem(IDC_SHOW_STOCK_NAME_CHECK);
		CWnd* pChk3 = GetDlgItem(IDC_COLOR_WITH_PRICE_CHECK);
		CWnd* pChk4 = GetDlgItem(IDC_SHOW_FLUCTUATION_CHECK);

		int chkW = g_data.DPI(135);
		int chkH = g_data.DPI(20);
		if (pChk1 && pChk1->GetSafeHwnd()) pChk1->MoveWindow(rightLeft + g_data.DPI(16), card1Top + g_data.DPI(30), chkW, chkH);
		if (pChk2 && pChk2->GetSafeHwnd()) pChk2->MoveWindow(rightLeft + g_data.DPI(170), card1Top + g_data.DPI(30), chkW, chkH);
		if (pChk3 && pChk3->GetSafeHwnd()) pChk3->MoveWindow(rightLeft + g_data.DPI(16), card1Top + g_data.DPI(56), chkW, chkH);
		if (pChk4 && pChk4->GetSafeHwnd()) pChk4->MoveWindow(rightLeft + g_data.DPI(170), card1Top + g_data.DPI(56), chkW, chkH);

		// 卡片 2 内部控件布局 (走势图尺寸)
		int card2Top = card1Top + g_data.DPI(92);
		CWnd* pKWLbl = GetDlgItem(IDC_KLINE_WIDTH_STATIC);
		CWnd* pKWEdit = GetDlgItem(IDC_KLINE_WIDTH_EDIT);
		CWnd* pKHLbl = GetDlgItem(IDC_KLINE_HEIGHT_STATIC);
		CWnd* pKHEdit = GetDlgItem(IDC_KLINE_HEIGHT_EDIT);

		if (pKWLbl && pKWLbl->GetSafeHwnd()) pKWLbl->MoveWindow(rightLeft + g_data.DPI(16), card2Top + g_data.DPI(33), g_data.DPI(85), g_data.DPI(18));
		if (pKWEdit && pKWEdit->GetSafeHwnd()) pKWEdit->MoveWindow(rightLeft + g_data.DPI(105), card2Top + g_data.DPI(30), g_data.DPI(65), g_data.DPI(22));
		if (pKHLbl && pKHLbl->GetSafeHwnd()) pKHLbl->MoveWindow(rightLeft + g_data.DPI(195), card2Top + g_data.DPI(33), g_data.DPI(85), g_data.DPI(18));
		if (pKHEdit && pKHEdit->GetSafeHwnd()) pKHEdit->MoveWindow(rightLeft + g_data.DPI(285), card2Top + g_data.DPI(30), g_data.DPI(65), g_data.DPI(22));

		// 卡片 3 内部控件布局 (SOCKS5 代理)
		int card3Top = card2Top + g_data.DPI(72);
		CWnd* pProxyChk = GetDlgItem(IDC_USE_SOCKS5_PROXY_CHECK);
		CWnd* pProxyLbl = GetDlgItem(IDC_SOCKS5_PROXY_STATIC);
		CWnd* pProxyEdit = GetDlgItem(IDC_SOCKS5_PROXY_EDIT);

		if (pProxyChk && pProxyChk->GetSafeHwnd()) pProxyChk->MoveWindow(rightLeft + g_data.DPI(16), card3Top + g_data.DPI(31), g_data.DPI(135), g_data.DPI(20));
		if (pProxyLbl && pProxyLbl->GetSafeHwnd()) pProxyLbl->MoveWindow(rightLeft + g_data.DPI(160), card3Top + g_data.DPI(33), g_data.DPI(60), g_data.DPI(18));
		if (pProxyEdit && pProxyEdit->GetSafeHwnd()) pProxyEdit->MoveWindow(rightLeft + g_data.DPI(225), card3Top + g_data.DPI(30), min(g_data.DPI(180), rightWidth - g_data.DPI(235)), g_data.DPI(22));
	}

	// 分组管理控件布局
	bool isGroup = (m_current_page == PAGE_GROUPS);
	int listTop = rightTop + g_data.DPI(38);
	int listHeight = rightBottom - listTop - g_data.DPI(40);

	m_stock_listctrl.ShowWindow((isGroup && m_current_group_tab == TAB_WATCHLIST) ? SW_SHOW : SW_HIDE);
	m_pos_listctrl.ShowWindow((isGroup && m_current_group_tab == TAB_POSITIONS) ? SW_SHOW : SW_HIDE);
	m_custom_listctrl.ShowWindow((isGroup && m_current_group_tab == TAB_CUSTOM) ? SW_SHOW : SW_HIDE);

	if (isGroup)
	{
		CRect listRect(rightLeft, listTop, rightLeft + rightWidth, listTop + listHeight);
		if (m_current_group_tab == TAB_WATCHLIST && m_stock_listctrl.GetSafeHwnd())
			m_stock_listctrl.MoveWindow(listRect);
		else if (m_current_group_tab == TAB_POSITIONS && m_pos_listctrl.GetSafeHwnd())
			m_pos_listctrl.MoveWindow(listRect);
		else if (m_current_group_tab == TAB_CUSTOM && m_custom_listctrl.GetSafeHwnd())
			m_custom_listctrl.MoveWindow(listRect);

		int btnTop = listTop + listHeight + g_data.DPI(8);
		int btnW = g_data.DPI(65);
		int btnH = g_data.DPI(25);
		int btnGap = g_data.DPI(8);

		m_mgr_add_btn.ShowWindow(SW_SHOW);
		m_mgr_del_btn.ShowWindow(SW_SHOW);
		m_mgr_add_btn.MoveWindow(rightLeft, btnTop, btnW, btnH);
		m_mgr_add_btn.SetWindowText(m_current_group_tab == TAB_POSITIONS ? L"编辑持仓" : L"添加股票");

		m_mgr_del_btn.MoveWindow(rightLeft + btnW + btnGap, btnTop, btnW, btnH);
		m_mgr_del_btn.SetWindowText(m_current_group_tab == TAB_POSITIONS ? L"清除持仓" : L"删除股票");

		bool showOrderBtns = (m_current_group_tab != TAB_POSITIONS);
		m_mgr_edit_btn.ShowWindow(showOrderBtns ? SW_SHOW : SW_HIDE);
		m_mgr_up_btn.ShowWindow(showOrderBtns ? SW_SHOW : SW_HIDE);
		m_mgr_down_btn.ShowWindow(showOrderBtns ? SW_SHOW : SW_HIDE);

		if (showOrderBtns)
		{
			m_mgr_edit_btn.MoveWindow(rightLeft + (btnW + btnGap) * 2, btnTop, btnW, btnH);
			m_mgr_up_btn.MoveWindow(rightLeft + (btnW + btnGap) * 3, btnTop, btnW, btnH);
			m_mgr_down_btn.MoveWindow(rightLeft + (btnW + btnGap) * 4, btnTop, btnW, btnH);
		}
	}
	else
	{
		m_mgr_add_btn.ShowWindow(SW_HIDE);
		m_mgr_edit_btn.ShowWindow(SW_HIDE);
		m_mgr_del_btn.ShowWindow(SW_HIDE);
		m_mgr_up_btn.ShowWindow(SW_HIDE);
		m_mgr_down_btn.ShowWindow(SW_HIDE);
	}

	// 均线日配置控件布局
	bool isMa = (m_current_page == PAGE_MA);
	m_ma_input_edit.ShowWindow(isMa ? SW_SHOW : SW_HIDE);
	m_ma_add_btn.ShowWindow(isMa ? SW_SHOW : SW_HIDE);

	if (isMa)
	{
		int maInputTop = rightTop + g_data.DPI(120);
		if (m_ma_input_edit.GetSafeHwnd())
			m_ma_input_edit.MoveWindow(rightLeft + g_data.DPI(120), maInputTop, g_data.DPI(75), g_data.DPI(24));
		if (m_ma_add_btn.GetSafeHwnd())
			m_ma_add_btn.MoveWindow(rightLeft + g_data.DPI(205), maInputTop, g_data.DPI(75), g_data.DPI(24));
	}

	// WebDAV 云端备份控件布局
	const int webdavControlIds[] = {
		IDC_WEBDAV_URL_STATIC, IDC_WEBDAV_URL_EDIT,
		IDC_WEBDAV_USER_STATIC, IDC_WEBDAV_USER_EDIT,
		IDC_WEBDAV_PWD_STATIC, IDC_WEBDAV_PWD_EDIT,
		IDC_WEBDAV_DIR_STATIC, IDC_WEBDAV_DIR_EDIT,
		IDC_WEBDAV_AUTO_SYNC_CHECK, IDC_WEBDAV_AUTO_BACKUP_CHECK,
		IDC_WEBDAV_TEST_BTN, IDC_WEBDAV_UPLOAD_BTN, IDC_WEBDAV_DOWNLOAD_BTN
	};

	bool isWebDav = (m_current_page == PAGE_WEBDAV);
	for (int id : webdavControlIds)
	{
		CWnd* pWnd = GetDlgItem(id);
		if (pWnd && pWnd->GetSafeHwnd())
			pWnd->ShowWindow(isWebDav ? SW_SHOW : SW_HIDE);
	}

	if (isWebDav)
	{
		int wdTop = rightTop + g_data.DPI(28);
		int lblW = g_data.DPI(80);
		int editW = min(g_data.DPI(280), rightWidth - lblW - g_data.DPI(30));
		int rowH = g_data.DPI(26);

		CWnd* pUrlLbl = GetDlgItem(IDC_WEBDAV_URL_STATIC);
		CWnd* pUrlEdit = GetDlgItem(IDC_WEBDAV_URL_EDIT);
		if (pUrlLbl && pUrlLbl->GetSafeHwnd()) pUrlLbl->MoveWindow(rightLeft + g_data.DPI(16), wdTop + g_data.DPI(3), lblW, g_data.DPI(18));
		if (pUrlEdit && pUrlEdit->GetSafeHwnd()) pUrlEdit->MoveWindow(rightLeft + g_data.DPI(16) + lblW, wdTop, editW, g_data.DPI(22));
		wdTop += rowH;

		CWnd* pUsrLbl = GetDlgItem(IDC_WEBDAV_USER_STATIC);
		CWnd* pUsrEdit = GetDlgItem(IDC_WEBDAV_USER_EDIT);
		if (pUsrLbl && pUsrLbl->GetSafeHwnd()) pUsrLbl->MoveWindow(rightLeft + g_data.DPI(16), wdTop + g_data.DPI(3), lblW, g_data.DPI(18));
		if (pUsrEdit && pUsrEdit->GetSafeHwnd()) pUsrEdit->MoveWindow(rightLeft + g_data.DPI(16) + lblW, wdTop, editW, g_data.DPI(22));
		wdTop += rowH;

		CWnd* pPwdLbl = GetDlgItem(IDC_WEBDAV_PWD_STATIC);
		CWnd* pPwdEdit = GetDlgItem(IDC_WEBDAV_PWD_EDIT);
		if (pPwdLbl && pPwdLbl->GetSafeHwnd()) pPwdLbl->MoveWindow(rightLeft + g_data.DPI(16), wdTop + g_data.DPI(3), lblW, g_data.DPI(18));
		if (pPwdEdit && pPwdEdit->GetSafeHwnd()) pPwdEdit->MoveWindow(rightLeft + g_data.DPI(16) + lblW, wdTop, editW, g_data.DPI(22));
		wdTop += rowH;

		CWnd* pDirLbl = GetDlgItem(IDC_WEBDAV_DIR_STATIC);
		CWnd* pDirEdit = GetDlgItem(IDC_WEBDAV_DIR_EDIT);
		if (pDirLbl && pDirLbl->GetSafeHwnd()) pDirLbl->MoveWindow(rightLeft + g_data.DPI(16), wdTop + g_data.DPI(3), lblW, g_data.DPI(18));
		if (pDirEdit && pDirEdit->GetSafeHwnd()) pDirEdit->MoveWindow(rightLeft + g_data.DPI(16) + lblW, wdTop, editW, g_data.DPI(22));
		wdTop += rowH + g_data.DPI(16);

		CWnd* pSyncChk = GetDlgItem(IDC_WEBDAV_AUTO_SYNC_CHECK);
		CWnd* pBakChk = GetDlgItem(IDC_WEBDAV_AUTO_BACKUP_CHECK);
		if (pSyncChk && pSyncChk->GetSafeHwnd()) pSyncChk->MoveWindow(rightLeft + g_data.DPI(16), wdTop, g_data.DPI(240), g_data.DPI(20));
		wdTop += g_data.DPI(24);
		if (pBakChk && pBakChk->GetSafeHwnd()) pBakChk->MoveWindow(rightLeft + g_data.DPI(16), wdTop, g_data.DPI(240), g_data.DPI(20));
		wdTop += g_data.DPI(28);

		int wdBtnW = g_data.DPI(80);
		int wdBtnH = g_data.DPI(26);
		int wdGap = g_data.DPI(10);
		CWnd* pTestBtn = GetDlgItem(IDC_WEBDAV_TEST_BTN);
		CWnd* pUpBtn = GetDlgItem(IDC_WEBDAV_UPLOAD_BTN);
		CWnd* pDownBtn = GetDlgItem(IDC_WEBDAV_DOWNLOAD_BTN);
		if (pTestBtn && pTestBtn->GetSafeHwnd()) pTestBtn->MoveWindow(rightLeft + g_data.DPI(16), wdTop, wdBtnW, wdBtnH);
		if (pUpBtn && pUpBtn->GetSafeHwnd()) pUpBtn->MoveWindow(rightLeft + g_data.DPI(16) + wdBtnW + wdGap, wdTop, wdBtnW + g_data.DPI(15), wdBtnH);
		if (pDownBtn && pDownBtn->GetSafeHwnd()) pDownBtn->MoveWindow(rightLeft + g_data.DPI(16) + (wdBtnW + wdGap) * 2 + g_data.DPI(15), wdTop, wdBtnW + g_data.DPI(15), wdBtnH);
	}

	// 底部确定与取消按钮
	CWnd* pOkBtn = GetDlgItem(IDOK);
	CWnd* pCancelBtn = GetDlgItem(IDCANCEL);
	int okBtnW = g_data.DPI(72);
	int okBtnH = g_data.DPI(26);
	int btnY = clientRect.Height() - g_data.DPI(38);

	if (pCancelBtn && pCancelBtn->GetSafeHwnd())
		pCancelBtn->MoveWindow(clientRect.Width() - okBtnW - g_data.DPI(18), btnY, okBtnW, okBtnH);
	if (pOkBtn && pOkBtn->GetSafeHwnd())
		pOkBtn->MoveWindow(clientRect.Width() - (okBtnW * 2) - g_data.DPI(28), btnY, okBtnW, okBtnH);
}

BOOL CManagerDialog::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

void CManagerDialog::OnPaint()
{
	CPaintDC dc(this);
	CRect clientRect;
	GetClientRect(clientRect);

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	CBitmap memBmp;
	memBmp.CreateCompatibleBitmap(&dc, clientRect.Width(), clientRect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	Gdiplus::Graphics g(memDC.GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

	// 整体背景填充 (深邃科技黑 #0B0F19)
	Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 11, 15, 25));
	g.FillRectangle(&bgBrush, 0, 0, clientRect.Width(), clientRect.Height());

	// 绘制左侧导航菜单
	DrawSidebar(g, clientRect);

	// 绘制右侧内容头部
	DrawHeader(g, clientRect);

	CRect contentRect(m_menu_width + g_data.DPI(18), g_data.DPI(65), clientRect.Width() - g_data.DPI(18), clientRect.Height() - g_data.DPI(50));

	switch (m_current_page)
	{
	case PAGE_BASIC:
		DrawBasicPage(g, contentRect);
		break;
	case PAGE_INDEX:
		DrawIndexPage(g, contentRect);
		break;
	case PAGE_GROUPS:
		DrawGroupPage(g, contentRect);
		break;
	case PAGE_MA:
		DrawMaPage(g, contentRect);
		break;
	case PAGE_WEBDAV:
		DrawWebDavPage(g, contentRect);
		break;
	case PAGE_ABOUT:
		DrawAboutPage(g, contentRect);
		break;
	default:
		break;
	}

	dc.BitBlt(0, 0, clientRect.Width(), clientRect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

void CManagerDialog::DrawSidebar(Gdiplus::Graphics& g, const CRect& clientRect)
{
	// 侧边栏背景
	Gdiplus::SolidBrush sideBrush(Gdiplus::Color(255, 17, 24, 39)); // Gray 900
	g.FillRectangle(&sideBrush, 0, 0, m_menu_width, clientRect.Height());

	// 侧边栏右侧分割线
	Gdiplus::Pen divPen(Gdiplus::Color(255, 31, 41, 55), 1.0f);
	g.DrawLine(&divPen, m_menu_width, 0, m_menu_width, clientRect.Height());

	// 侧边栏顶部品牌图标与标题
	Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 99, 102, 241)); // Indigo 500
	g.FillEllipse(&dotBrush, g_data.DPI(16), g_data.DPI(18), g_data.DPI(10), g_data.DPI(10));

	Gdiplus::Font titleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 248, 250, 252));
	g.DrawString(L"股票管理", -1, &titleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(g_data.DPI(32)), static_cast<Gdiplus::REAL>(g_data.DPI(14))), &titleBrush);

	const wchar_t* menuTitles[] = { L"基础设置", L"指数编辑", L"分组管理", L"均线日配置", L"云端备份", L"关于插件" };
	int menuCount = 6;
	int itemH = g_data.DPI(34);
	int itemTop = g_data.DPI(48);
	int itemPadX = g_data.DPI(10);
	int itemW = m_menu_width - (itemPadX * 2);

	Gdiplus::Font menuFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font menuActiveFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::StringFormat sf;
	sf.SetAlignment(Gdiplus::StringAlignmentNear);
	sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	for (int i = 0; i < menuCount; ++i)
	{
		CRect r(itemPadX, itemTop, itemPadX + itemW, itemTop + itemH);
		m_menu_rects[i] = r;

		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width()), static_cast<Gdiplus::REAL>(r.Height()));

		if (i == m_current_page)
		{
			Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 79, 70, 229)); // Vibrant Indigo
			g.FillRectangle(&activeBg, rf);

			Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 165, 180, 252));
			g.FillRectangle(&barBrush, static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(r.Height()));

			Gdiplus::SolidBrush txtBrush(Gdiplus::Color(255, 255, 255, 255));
			Gdiplus::RectF textRf(static_cast<Gdiplus::REAL>(r.left + g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width() - g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.Height()));
			g.DrawString(menuTitles[i], -1, &menuActiveFont, textRf, &sf, &txtBrush);
		}
		else
		{
			if (i == m_hover_menu)
			{
				Gdiplus::SolidBrush hoverBg(Gdiplus::Color(255, 31, 41, 55));
				g.FillRectangle(&hoverBg, rf);
			}

			Gdiplus::SolidBrush txtBrush(i == m_hover_menu ? Gdiplus::Color(255, 226, 232, 240) : Gdiplus::Color(255, 156, 163, 175));
			Gdiplus::RectF textRf(static_cast<Gdiplus::REAL>(r.left + g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width() - g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.Height()));
			g.DrawString(menuTitles[i], -1, &menuFont, textRf, &sf, &txtBrush);
		}

		itemTop += itemH + g_data.DPI(3);
	}
}

void CManagerDialog::DrawHeader(Gdiplus::Graphics& g, const CRect& clientRect)
{
	int rightLeft = m_menu_width + g_data.DPI(18);
	int headerTop = g_data.DPI(14);

	const wchar_t* titles[] = { L"基础设置", L"指数编辑", L"分组管理", L"均线日配置", L"云端备份", L"关于插件" };
	const wchar_t* subs[] = {
		L"配置全天更新、代理网络及K线走势图尺寸",
		L"点击选择展示的指数，前 5 个展示在首页顶部",
		L"管理自选股票列表、持仓配置与自定义分组",
		L"自定义均线周期（最多 5 条；点标签删除；点添加新增）",
		L"基于 WebDAV 协议在多台电脑间安全同步自选股与全部插件配置",
		L"TrafficMonitor 专业级股票行情监控插件"
	};

	Gdiplus::Font headFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush headBrush(Gdiplus::Color(255, 248, 250, 252));
	g.DrawString(titles[m_current_page], -1, &headFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(headerTop)), &headBrush);

	Gdiplus::Font subFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 148, 163, 184));

	std::wstring subText = subs[m_current_page];
	if (m_current_page == PAGE_INDEX)
	{
		subText = L"点击选择展示的指数，前 5 个展示在首页顶部 (已选: " + std::to_wstring(m_data.m_selected_indices.size()) + L")";
	}
	else if (m_current_page == PAGE_MA)
	{
		subText = L"最多 5 条；点标签删除；点添加新增。已选 (" + std::to_wstring(m_data.m_ma_days.size()) + L"/5)";
	}

	g.DrawString(subText.c_str(), -1, &subFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(headerTop + g_data.DPI(22))), &subBrush);

	Gdiplus::Pen divPen(Gdiplus::Color(255, 30, 41, 59), 1.0f);
	g.DrawLine(&divPen, rightLeft, g_data.DPI(52), clientRect.Width() - g_data.DPI(18), g_data.DPI(52));
}

void CManagerDialog::DrawBasicPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 30, 41, 59));
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 51, 65, 85), 1.0f);
	Gdiplus::Font cardTitleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	// 卡片 1: 走势与行情展示
	int card1Top = contentRect.top;
	int card1H = g_data.DPI(84);
	Gdiplus::RectF card1Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card1Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card1H));
	g.FillRectangle(&cardBg, card1Rf);
	g.DrawRectangle(&cardBorder, card1Rf);

	Gdiplus::SolidBrush title1Brush(Gdiplus::Color(255, 56, 189, 248)); // Sky 400
	g.DrawString(L"📈 行情与走势图展示", -1, &cardTitleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(12)), static_cast<Gdiplus::REAL>(card1Top + g_data.DPI(8))), &title1Brush);

	// 卡片 2: 走势图尺寸配置
	int card2Top = card1Top + card1H + g_data.DPI(10);
	int card2H = g_data.DPI(60);
	Gdiplus::RectF card2Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card2Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card2H));
	g.FillRectangle(&cardBg, card2Rf);
	g.DrawRectangle(&cardBorder, card2Rf);

	Gdiplus::SolidBrush title2Brush(Gdiplus::Color(255, 192, 132, 252)); // Purple 400
	g.DrawString(L"📐 走势图高清尺寸 (像素)", -1, &cardTitleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(12)), static_cast<Gdiplus::REAL>(card2Top + g_data.DPI(8))), &title2Brush);

	// 卡片 3: 网络与代理设置
	int card3Top = card2Top + card2H + g_data.DPI(10);
	int card3H = g_data.DPI(60);
	Gdiplus::RectF card3Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card3Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card3H));
	g.FillRectangle(&cardBg, card3Rf);
	g.DrawRectangle(&cardBorder, card3Rf);

	Gdiplus::SolidBrush title3Brush(Gdiplus::Color(255, 52, 211, 153)); // Emerald 400
	g.DrawString(L"🌐 SOCKS5 代理网络", -1, &cardTitleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(12)), static_cast<Gdiplus::REAL>(card3Top + g_data.DPI(8))), &title3Brush);
}

void CManagerDialog::DrawIndexPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	const auto& presets = GetPresetIndices();
	m_index_card_rects.clear();
	m_index_card_rects.resize(presets.size());

	int cols = 3;
	int cardGapX = g_data.DPI(12);
	int cardGapY = g_data.DPI(10);
	int cardW = (contentRect.Width() - (cardGapX * (cols - 1))) / cols;
	int cardH = g_data.DPI(44);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font codeFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(8.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font rankFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(8.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	for (size_t i = 0; i < presets.size(); ++i)
	{
		int col = static_cast<int>(i % cols);
		int row = static_cast<int>(i / cols);
		int x = contentRect.left + col * (cardW + cardGapX);
		int y = contentRect.top + row * (cardH + cardGapY);

		CRect cardRect(x, y, x + cardW, y + cardH);
		m_index_card_rects[i] = cardRect;

		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y), static_cast<Gdiplus::REAL>(cardW), static_cast<Gdiplus::REAL>(cardH));

		auto it = std::find(m_data.m_selected_indices.begin(), m_data.m_selected_indices.end(), presets[i].code);
		bool isSelected = (it != m_data.m_selected_indices.end());
		int rank = isSelected ? static_cast<int>(std::distance(m_data.m_selected_indices.begin(), it) + 1) : 0;

		if (isSelected)
		{
			Gdiplus::SolidBrush selBg(Gdiplus::Color(255, 79, 70, 229));
			g.FillRectangle(&selBg, rf);

			Gdiplus::Pen borderPen(Gdiplus::Color(255, 165, 180, 252), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			if (rank <= 5)
			{
				Gdiplus::SolidBrush rankBg(Gdiplus::Color(255, 238, 242, 255));
				g.FillEllipse(&rankBg, x + cardW - g_data.DPI(22), y + g_data.DPI(8), g_data.DPI(15), g_data.DPI(15));

				Gdiplus::SolidBrush rankTxtBrush(Gdiplus::Color(255, 67, 56, 202));
				Gdiplus::StringFormat sfRank;
				sfRank.SetAlignment(Gdiplus::StringAlignmentCenter);
				sfRank.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				std::wstring rankStr = std::to_wstring(rank);
				Gdiplus::RectF rankRf(static_cast<Gdiplus::REAL>(x + cardW - g_data.DPI(22)), static_cast<Gdiplus::REAL>(y + g_data.DPI(8)), static_cast<Gdiplus::REAL>(g_data.DPI(15)), static_cast<Gdiplus::REAL>(g_data.DPI(15)));
				g.DrawString(rankStr.c_str(), -1, &rankFont, rankRf, &sfRank, &rankTxtBrush);
			}

			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(presets[i].name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(7))), &nameBrush);

			Gdiplus::SolidBrush codeBrush(Gdiplus::Color(255, 224, 231, 255));
			g.DrawString(presets[i].code.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(24))), &codeBrush);
		}
		else
		{
			Gdiplus::SolidBrush unselBg(Gdiplus::Color(255, 30, 41, 59));
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen borderPen(static_cast<int>(i) == m_hover_index_card ? Gdiplus::Color(255, 148, 163, 184) : Gdiplus::Color(255, 51, 65, 85), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 226, 232, 240));
			g.DrawString(presets[i].name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(7))), &nameBrush);

			Gdiplus::SolidBrush codeBrush(Gdiplus::Color(255, 100, 116, 139));
			g.DrawString(presets[i].code.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(24))), &codeBrush);
		}
	}
}

void CManagerDialog::DrawGroupPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	const wchar_t* groupTabs[] = { L"自选股", L"持仓", L"自定义" };
	int tabCount = 3;
	int tabW = g_data.DPI(75);
	int tabH = g_data.DPI(26);
	int tabGap = g_data.DPI(6);
	int tabTop = contentRect.top;

	m_group_tab_rects.clear();
	m_group_tab_rects.resize(tabCount);

	Gdiplus::Font tabFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font tabActiveFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::StringFormat sf;
	sf.SetAlignment(Gdiplus::StringAlignmentCenter);
	sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	for (int i = 0; i < tabCount; ++i)
	{
		int x = contentRect.left + i * (tabW + tabGap);
		CRect r(x, tabTop, x + tabW, tabTop + tabH);
		m_group_tab_rects[i] = r;

		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width()), static_cast<Gdiplus::REAL>(r.Height()));

		if (i == m_current_group_tab)
		{
			Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 79, 70, 229));
			g.FillRectangle(&activeBg, rf);

			Gdiplus::SolidBrush txtBrush(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(groupTabs[i], -1, &tabActiveFont, rf, &sf, &txtBrush);
		}
		else
		{
			Gdiplus::SolidBrush unselBg(i == m_hover_group_tab ? Gdiplus::Color(255, 51, 65, 85) : Gdiplus::Color(255, 30, 41, 59));
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen borderPen(Gdiplus::Color(255, 51, 65, 85), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			Gdiplus::SolidBrush txtBrush(Gdiplus::Color(255, 148, 163, 184));
			g.DrawString(groupTabs[i], -1, &tabFont, rf, &sf, &txtBrush);
		}
	}
}

void CManagerDialog::DrawMaPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	m_ma_tag_rects.clear();
	m_ma_tag_del_rects.clear();
	m_ma_tag_rects.resize(m_data.m_ma_days.size());
	m_ma_tag_del_rects.resize(m_data.m_ma_days.size());

	Gdiplus::RectF panelRf(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top), static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(g_data.DPI(100)));
	Gdiplus::SolidBrush panelBg(Gdiplus::Color(255, 30, 41, 59));
	g.FillRectangle(&panelBg, panelRf);
	Gdiplus::Pen panelPen(Gdiplus::Color(255, 51, 65, 85), 1.0f);
	g.DrawRectangle(&panelPen, panelRf);

	Gdiplus::Font labelFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 226, 232, 240));
	g.DrawString(L"当前均线周期：", -1, &labelFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(contentRect.left + g_data.DPI(14)), static_cast<Gdiplus::REAL>(contentRect.top + g_data.DPI(14))), &labelBrush);

	int tagLeft = contentRect.left + g_data.DPI(14);
	int tagTop = contentRect.top + g_data.DPI(42);
	int tagH = g_data.DPI(30);
	int tagGap = g_data.DPI(10);

	Gdiplus::Font tagFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font delFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(9)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::Color tagColors[] = {
		Gdiplus::Color(255, 217, 119, 6),   // Amber (MA5)
		Gdiplus::Color(255, 124, 58, 237),  // Violet (MA17)
		Gdiplus::Color(255, 16, 185, 129),  // Emerald (MA60)
		Gdiplus::Color(255, 14, 165, 233),  // Sky Blue
		Gdiplus::Color(255, 244, 63, 94)   // Rose
	};

	for (size_t i = 0; i < m_data.m_ma_days.size(); ++i)
	{
		int day = m_data.m_ma_days[i];
		std::wstring tagText = L"MA" + std::to_wstring(day);
		int tagW = g_data.DPI(80);

		CRect tagRect(tagLeft, tagTop, tagLeft + tagW, tagTop + tagH);
		m_ma_tag_rects[i] = tagRect;

		Gdiplus::RectF tagRf(static_cast<Gdiplus::REAL>(tagLeft), static_cast<Gdiplus::REAL>(tagTop), static_cast<Gdiplus::REAL>(tagW), static_cast<Gdiplus::REAL>(tagH));
		Gdiplus::SolidBrush tagBg(tagColors[i % 5]);
		g.FillRectangle(&tagBg, tagRf);

		Gdiplus::SolidBrush tagTxtBrush(Gdiplus::Color(255, 255, 255, 255));
		g.DrawString(tagText.c_str(), -1, &tagFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(tagLeft + g_data.DPI(10)), static_cast<Gdiplus::REAL>(tagTop + g_data.DPI(6))), &tagTxtBrush);

		int delBtnX = tagLeft + tagW - g_data.DPI(22);
		int delBtnY = tagTop + g_data.DPI(6);
		CRect delRect(delBtnX, delBtnY, delBtnX + g_data.DPI(16), delBtnY + g_data.DPI(16));
		m_ma_tag_del_rects[i] = delRect;

		if (static_cast<int>(i) == m_hover_ma_tag_del)
		{
			Gdiplus::SolidBrush delHoverBrush(Gdiplus::Color(255, 239, 68, 68));
			g.FillEllipse(&delHoverBrush, delBtnX, delBtnY, g_data.DPI(16), g_data.DPI(16));
		}

		Gdiplus::StringFormat sfDel;
		sfDel.SetAlignment(Gdiplus::StringAlignmentCenter);
		sfDel.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		Gdiplus::RectF delRf(static_cast<Gdiplus::REAL>(delBtnX), static_cast<Gdiplus::REAL>(delBtnY), static_cast<Gdiplus::REAL>(g_data.DPI(16)), static_cast<Gdiplus::REAL>(g_data.DPI(16)));
		g.DrawString(L"×", -1, &delFont, delRf, &sfDel, &tagTxtBrush);

		tagLeft += tagW + tagGap;
	}

	Gdiplus::Font addPromptFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush promptBrush(Gdiplus::Color(255, 148, 163, 184));
	g.DrawString(L"输入均线天数 (1~250)：", -1, &addPromptFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top + g_data.DPI(124))), &promptBrush);
}

void CManagerDialog::DrawWebDavPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 30, 41, 59));
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 51, 65, 85), 1.0f);
	Gdiplus::Font cardTitleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	// 卡片 1: WebDAV 服务器参数
	int card1Top = contentRect.top;
	int card1H = g_data.DPI(135);
	Gdiplus::RectF card1Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card1Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card1H));
	g.FillRectangle(&cardBg, card1Rf);
	g.DrawRectangle(&cardBorder, card1Rf);

	Gdiplus::SolidBrush title1Brush(Gdiplus::Color(255, 96, 165, 250)); // Blue 400
	g.DrawString(L"☁️ WebDAV 服务器参数", -1, &cardTitleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(12)), static_cast<Gdiplus::REAL>(card1Top + g_data.DPI(8))), &title1Brush);

	// 卡片 2: 状态与说明
	int card2Top = card1Top + card1H + g_data.DPI(10);
	int card2H = g_data.DPI(110);
	Gdiplus::RectF card2Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card2Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card2H));
	g.FillRectangle(&cardBg, card2Rf);
	g.DrawRectangle(&cardBorder, card2Rf);

	Gdiplus::SolidBrush title2Brush(Gdiplus::Color(255, 52, 211, 153)); // Emerald 400
	g.DrawString(L"⚡ 同步与备份操作", -1, &cardTitleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(12)), static_cast<Gdiplus::REAL>(card2Top + g_data.DPI(8))), &title2Brush);

	Gdiplus::Font tipFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(8.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 148, 163, 184));
	int textX = rightLeft + g_data.DPI(16);
	int textY = card2Top + g_data.DPI(75);

	g.DrawString(L"💡 支持坚果云、Nextcloud、Alist、群晖NAS等标准 WebDAV 服务。", -1, &tipFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &tipBrush);

	if (!m_data.m_webdav_last_sync_time.empty())
	{
		textY += g_data.DPI(16);
		std::wstring timeStr = L"• 上次成功同步时间: " + m_data.m_webdav_last_sync_time;
		Gdiplus::SolidBrush succBrush(Gdiplus::Color(255, 52, 211, 153));
		g.DrawString(timeStr.c_str(), -1, &tipFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &succBrush);
	}
}

void CManagerDialog::DrawAboutPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	Gdiplus::RectF panelRf(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top), static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(contentRect.Height()));
	Gdiplus::SolidBrush panelBg(Gdiplus::Color(255, 30, 41, 59));
	g.FillRectangle(&panelBg, panelRf);
	Gdiplus::Pen panelPen(Gdiplus::Color(255, 51, 65, 85), 1.0f);
	g.DrawRectangle(&panelPen, panelRf);

	int textX = contentRect.left + g_data.DPI(24);
	int textY = contentRect.top + g_data.DPI(24);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 248, 250, 252));
	g.DrawString(L"TrafficMonitor 股票行情插件 (Stock Plugin)", -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &nameBrush);

	textY += g_data.DPI(30);
	Gdiplus::Font infoFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush infoBrush(Gdiplus::Color(255, 148, 163, 184));
	g.DrawString(L"版本: v1.15   |   原作者: CListery   |   开发贡献: TrafficMonitor Community", -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &infoBrush);

	textY += g_data.DPI(28);
	const wchar_t* features[] = {
		L"• 全天候股票/基金行情实时监测，毫秒级状态栏高频刷新",
		L"• 高清分时走势图与多周期K线（日K/周K/月K）自绘预览",
		L"• 自定义均线指标系统（MA5/MA17/MA60等）多周期叠加分析",
		L"• 自选股、持仓盈亏核算、自定义分组多维度分类管理",
		L"• WebDAV 云端备份同步与 SOCKS5 代理网络支持"
	};
	for (const auto* feat : features)
	{
		g.DrawString(feat, -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &infoBrush);
		textY += g_data.DPI(22);
	}

	textY += g_data.DPI(16);
	g.DrawString(L"项目开源主页 (点击访问)：", -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &infoBrush);

	textY += g_data.DPI(20);
	const wchar_t* url = L"https://github.com/zhongyang219/TrafficMonitorPlugins";
	Gdiplus::SolidBrush linkBrush(Gdiplus::Color(255, 96, 165, 250));
	g.DrawString(url, -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &linkBrush);

	m_about_link_rect = CRect(textX, textY, textX + g_data.DPI(320), textY + g_data.DPI(20));
}

void CManagerDialog::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_tracking_mouse)
	{
		TRACKMOUSEEVENT tme;
		tme.cbSize = sizeof(TRACKMOUSEEVENT);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		TrackMouseEvent(&tme);
		m_tracking_mouse = true;
	}

	int oldHoverMenu = m_hover_menu;
	int oldHoverCard = m_hover_index_card;
	int oldHoverMa = m_hover_ma_tag_del;
	int oldHoverTab = m_hover_group_tab;

	m_hover_menu = -1;
	for (size_t i = 0; i < m_menu_rects.size(); ++i)
	{
		if (m_menu_rects[i].PtInRect(point))
		{
			m_hover_menu = static_cast<int>(i);
			break;
		}
	}

	m_hover_index_card = -1;
	if (m_current_page == PAGE_INDEX)
	{
		for (size_t i = 0; i < m_index_card_rects.size(); ++i)
		{
			if (m_index_card_rects[i].PtInRect(point))
			{
				m_hover_index_card = static_cast<int>(i);
				break;
			}
		}
	}

	m_hover_ma_tag_del = -1;
	if (m_current_page == PAGE_MA)
	{
		for (size_t i = 0; i < m_ma_tag_del_rects.size(); ++i)
		{
			if (m_ma_tag_del_rects[i].PtInRect(point))
			{
				m_hover_ma_tag_del = static_cast<int>(i);
				break;
			}
		}
	}

	m_hover_group_tab = -1;
	if (m_current_page == PAGE_GROUPS)
	{
		for (size_t i = 0; i < m_group_tab_rects.size(); ++i)
		{
			if (m_group_tab_rects[i].PtInRect(point))
			{
				m_hover_group_tab = static_cast<int>(i);
				break;
			}
		}
	}

	if (oldHoverMenu != m_hover_menu || oldHoverCard != m_hover_index_card ||
		oldHoverMa != m_hover_ma_tag_del || oldHoverTab != m_hover_group_tab)
	{
		Invalidate();
	}

	CDialog::OnMouseMove(nFlags, point);
}

void CManagerDialog::OnMouseLeave()
{
	m_tracking_mouse = false;
	m_hover_menu = -1;
	m_hover_index_card = -1;
	m_hover_ma_tag_del = -1;
	m_hover_group_tab = -1;
	Invalidate();
	CDialog::OnMouseLeave();
}

BOOL CManagerDialog::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	CPoint pt;
	GetCursorPos(&pt);
	ScreenToClient(&pt);

	if (m_hover_menu >= 0 || m_hover_index_card >= 0 || m_hover_ma_tag_del >= 0 ||
		m_hover_group_tab >= 0 || (m_current_page == PAGE_ABOUT && m_about_link_rect.PtInRect(pt)))
	{
		SetCursor(LoadCursor(nullptr, IDC_HAND));
		return TRUE;
	}

	return CDialog::OnSetCursor(pWnd, nHitTest, message);
}

void CManagerDialog::OnLButtonDown(UINT nFlags, CPoint point)
{
	for (size_t i = 0; i < m_menu_rects.size(); ++i)
	{
		if (m_menu_rects[i].PtInRect(point))
		{
			SwitchPage(static_cast<PageIndex>(i));
			return;
		}
	}

	if (m_current_page == PAGE_INDEX)
	{
		const auto& presets = GetPresetIndices();
		for (size_t i = 0; i < m_index_card_rects.size() && i < presets.size(); ++i)
		{
			if (m_index_card_rects[i].PtInRect(point))
			{
				const auto& code = presets[i].code;
				auto it = std::find(m_data.m_selected_indices.begin(), m_data.m_selected_indices.end(), code);
				if (it != m_data.m_selected_indices.end())
				{
					if (m_data.m_selected_indices.size() > 1)
						m_data.m_selected_indices.erase(it);
				}
				else
				{
					m_data.m_selected_indices.push_back(code);
				}
				Invalidate();
				return;
			}
		}
	}

	if (m_current_page == PAGE_GROUPS)
	{
		for (size_t i = 0; i < m_group_tab_rects.size(); ++i)
		{
			if (m_group_tab_rects[i].PtInRect(point))
			{
				SwitchGroupTab(static_cast<GroupSubTab>(i));
				return;
			}
		}
	}

	if (m_current_page == PAGE_MA)
	{
		for (size_t i = 0; i < m_ma_tag_del_rects.size(); ++i)
		{
			if (m_ma_tag_del_rects[i].PtInRect(point))
			{
				if (m_data.m_ma_days.size() > 1)
				{
					m_data.m_ma_days.erase(m_data.m_ma_days.begin() + i);
					Invalidate();
				}
				else
				{
					MessageBox(L"至少保留 1 个均线周期！", L"提示", MB_ICONINFORMATION);
				}
				return;
			}
		}
	}

	if (m_current_page == PAGE_ABOUT && m_about_link_rect.PtInRect(point))
	{
		ShellExecute(nullptr, L"open", L"https://github.com/zhongyang219/TrafficMonitorPlugins", nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	CDialog::OnLButtonDown(nFlags, point);
}

void CManagerDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	UpdateControlsLayout();
	Invalidate();
}

void CManagerDialog::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = m_min_size.cx;
	lpMMI->ptMinTrackSize.y = m_min_size.cy;
	CDialog::OnGetMinMaxInfo(lpMMI);
}

void CManagerDialog::OnListItemClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
}

void CManagerDialog::OnLbnDblclkMgrList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
	if (index >= 0 && index < static_cast<int>(m_data.m_stock_codes.size()))
	{
		COptionsDlg dlg(m_data.m_stock_codes[index], this);
		if (dlg.DoModal() == IDOK && !dlg.m_stock_code.IsEmpty())
		{
			m_data.m_stock_codes[index] = dlg.m_stock_code.GetString();
			RefreshStockList();
			RefreshPositionList();
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnLbnDblclkPosList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_pos_listctrl.GetNextItem(-1, LVNI_SELECTED);
	if (index >= 0)
	{
		CString code = m_pos_listctrl.GetItemText(index, 0);
		COptionsDlg dlg(code.GetString(), this);
		if (dlg.DoModal() == IDOK)
		{
			RefreshPositionList();
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnLbnDblclkCustomList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
	if (index >= 0 && index < static_cast<int>(m_data.m_custom_group_codes.size()))
	{
		COptionsDlg dlg(m_data.m_custom_group_codes[index], this);
		if (dlg.DoModal() == IDOK && !dlg.m_stock_code.IsEmpty())
		{
			m_data.m_custom_group_codes[index] = dlg.m_stock_code.GetString();
			RefreshCustomList();
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnAddBtnClick()
{
	if (m_current_group_tab == TAB_WATCHLIST)
	{
		if (m_data.m_stock_codes.size() >= Stock_ITEM_MAX)
		{
			MessageBox(g_data.StringRes(IDS_STOCK_NUM_LIMIT_WARNING), g_data.StringRes(IDS_PLUGIN_NAME), MB_ICONWARNING | MB_OK);
			return;
		}
		COptionsDlg dlg(std::wstring(), this);
		if (dlg.DoModal() == IDOK)
		{
			std::wstring stock_code = dlg.m_stock_code.GetString();
			if (!stock_code.empty())
			{
				if (std::find(m_data.m_stock_codes.begin(), m_data.m_stock_codes.end(), stock_code) == m_data.m_stock_codes.end())
				{
					m_data.m_stock_codes.push_back(stock_code);
					RefreshStockList();
				}
			}
		}
	}
	else if (m_current_group_tab == TAB_POSITIONS)
	{
		int curSel = m_pos_listctrl.GetNextItem(-1, LVNI_SELECTED);
		std::wstring code;
		if (curSel >= 0)
			code = m_pos_listctrl.GetItemText(curSel, 0).GetString();
		else if (!m_data.m_stock_codes.empty())
			code = m_data.m_stock_codes[0];

		COptionsDlg dlg(code, this);
		if (dlg.DoModal() == IDOK)
		{
			RefreshPositionList();
		}
	}
	else if (m_current_group_tab == TAB_CUSTOM)
	{
		COptionsDlg dlg(std::wstring(), this);
		if (dlg.DoModal() == IDOK)
		{
			std::wstring stock_code = dlg.m_stock_code.GetString();
			if (!stock_code.empty())
			{
				if (std::find(m_data.m_custom_group_codes.begin(), m_data.m_custom_group_codes.end(), stock_code) == m_data.m_custom_group_codes.end())
				{
					m_data.m_custom_group_codes.push_back(stock_code);
					RefreshCustomList();
				}
			}
		}
	}
}

void CManagerDialog::OnEditBtnClick()
{
	if (m_current_group_tab == TAB_WATCHLIST)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()))
		{
			COptionsDlg dlg(m_data.m_stock_codes[curSel], this);
			if (dlg.DoModal() == IDOK && !dlg.m_stock_code.IsEmpty())
			{
				m_data.m_stock_codes[curSel] = dlg.m_stock_code.GetString();
				RefreshStockList();
				RefreshPositionList();
			}
		}
	}
	else if (m_current_group_tab == TAB_CUSTOM)
	{
		int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_custom_group_codes.size()))
		{
			COptionsDlg dlg(m_data.m_custom_group_codes[curSel], this);
			if (dlg.DoModal() == IDOK && !dlg.m_stock_code.IsEmpty())
			{
				m_data.m_custom_group_codes[curSel] = dlg.m_stock_code.GetString();
				RefreshCustomList();
			}
		}
	}
}

void CManagerDialog::OnDelBtnClick()
{
	if (m_current_group_tab == TAB_WATCHLIST)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()))
		{
			m_data.m_stock_codes.erase(m_data.m_stock_codes.begin() + curSel);
			RefreshStockList();
			RefreshPositionList();
		}
	}
	else if (m_current_group_tab == TAB_POSITIONS)
	{
		int curSel = m_pos_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0)
		{
			std::wstring code = m_pos_listctrl.GetItemText(curSel, 0).GetString();
			g_data.SetPosition(code, 0.0, 0.0, L"");
			RefreshPositionList();
		}
	}
	else if (m_current_group_tab == TAB_CUSTOM)
	{
		int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_custom_group_codes.size()))
		{
			m_data.m_custom_group_codes.erase(m_data.m_custom_group_codes.begin() + curSel);
			RefreshCustomList();
		}
	}
}

void CManagerDialog::OnMoveUpBtnClick()
{
	if (m_current_group_tab == TAB_WATCHLIST)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel > 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()))
		{
			std::swap(m_data.m_stock_codes[curSel - 1], m_data.m_stock_codes[curSel]);
			RefreshStockList();
			m_stock_listctrl.SetItemState(curSel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
	else if (m_current_group_tab == TAB_CUSTOM)
	{
		int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel > 0 && curSel < static_cast<int>(m_data.m_custom_group_codes.size()))
		{
			std::swap(m_data.m_custom_group_codes[curSel - 1], m_data.m_custom_group_codes[curSel]);
			RefreshCustomList();
			m_custom_listctrl.SetItemState(curSel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
}

void CManagerDialog::OnMoveDownBtnClick()
{
	if (m_current_group_tab == TAB_WATCHLIST)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()) - 1)
		{
			std::swap(m_data.m_stock_codes[curSel], m_data.m_stock_codes[curSel + 1]);
			RefreshStockList();
			m_stock_listctrl.SetItemState(curSel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
	else if (m_current_group_tab == TAB_CUSTOM)
	{
		int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_custom_group_codes.size()) - 1)
		{
			std::swap(m_data.m_custom_group_codes[curSel], m_data.m_custom_group_codes[curSel + 1]);
			RefreshCustomList();
			m_custom_listctrl.SetItemState(curSel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
}

void CManagerDialog::OnMaAddBtnClick()
{
	CString valStr;
	m_ma_input_edit.GetWindowText(valStr);
	int val = _ttoi(valStr);
	if (val < 1 || val > 250)
	{
		MessageBox(L"均线周期请输入 1 到 250 之间的整数！", L"提示", MB_ICONWARNING);
		return;
	}

	if (m_data.m_ma_days.size() >= 5)
	{
		MessageBox(L"均线周期最多配置 5 条！请先删除已有周期后再添加。", L"提示", MB_ICONINFORMATION);
		return;
	}

	if (std::find(m_data.m_ma_days.begin(), m_data.m_ma_days.end(), val) != m_data.m_ma_days.end())
	{
		MessageBox(L"该均线周期已存在！", L"提示", MB_ICONINFORMATION);
		return;
	}

	m_data.m_ma_days.push_back(val);
	std::sort(m_data.m_ma_days.begin(), m_data.m_ma_days.end());
	m_ma_input_edit.SetWindowText(L"");
	Invalidate();
}

void CManagerDialog::OnClickedFullDayCheck()
{
	m_data.m_full_day = (IsDlgButtonChecked(IDC_FULL_DAY_CHECK) != 0);
}

void CManagerDialog::OnBnClickedShowStockNameCheck()
{
	m_data.m_show_stock_name = (IsDlgButtonChecked(IDC_SHOW_STOCK_NAME_CHECK) != 0);
}

void CManagerDialog::OnBnClickedColorWithPriceCheck()
{
	m_data.m_color_with_price = (IsDlgButtonChecked(IDC_COLOR_WITH_PRICE_CHECK) != 0);
}

void CManagerDialog::OnBnClickedShowFluctuationCheck()
{
	m_data.m_show_fluctuation = (IsDlgButtonChecked(IDC_SHOW_FLUCTUATION_CHECK) != 0);
}

void CManagerDialog::OnBnClickedUseSocks5ProxyCheck()
{
	m_data.m_use_socks5_proxy = (IsDlgButtonChecked(IDC_USE_SOCKS5_PROXY_CHECK) != 0);
}

void CManagerDialog::OnBnClickedWebDavAutoSyncCheck()
{
	m_data.m_webdav_auto_sync = (IsDlgButtonChecked(IDC_WEBDAV_AUTO_SYNC_CHECK) != 0);
}

void CManagerDialog::OnBnClickedWebDavAutoBackupCheck()
{
	m_data.m_webdav_auto_backup = (IsDlgButtonChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK) != 0);
}

void CManagerDialog::OnBnClickedWebDavTestBtn()
{
	CString urlStr, userStr, pwdStr, dirStr;
	GetDlgItemText(IDC_WEBDAV_URL_EDIT, urlStr);
	GetDlgItemText(IDC_WEBDAV_USER_EDIT, userStr);
	GetDlgItemText(IDC_WEBDAV_PWD_EDIT, pwdStr);
	GetDlgItemText(IDC_WEBDAV_DIR_EDIT, dirStr);

	SettingData testData = m_data;
	testData.m_webdav_url = urlStr.GetString();
	testData.m_webdav_username = userStr.GetString();
	testData.m_webdav_password = pwdStr.GetString();
	testData.m_webdav_dir = dirStr.GetString();

	std::wstring errMsg;
	CWaitCursor wait;
	if (CWebDavSync::TestConnection(testData, errMsg))
	{
		MessageBox(L"WebDAV 云端服务器连接与认证成功！", L"连接成功", MB_ICONINFORMATION | MB_OK);
	}
	else
	{
		MessageBox((L"WebDAV 连接失败：\n" + errMsg).c_str(), L"连接失败", MB_ICONERROR | MB_OK);
	}
}

void CManagerDialog::OnBnClickedWebDavUploadBtn()
{
	CString urlStr, userStr, pwdStr, dirStr;
	GetDlgItemText(IDC_WEBDAV_URL_EDIT, urlStr);
	GetDlgItemText(IDC_WEBDAV_USER_EDIT, userStr);
	GetDlgItemText(IDC_WEBDAV_PWD_EDIT, pwdStr);
	GetDlgItemText(IDC_WEBDAV_DIR_EDIT, dirStr);

	m_data.m_webdav_url = urlStr.GetString();
	m_data.m_webdav_username = userStr.GetString();
	m_data.m_webdav_password = pwdStr.GetString();
	m_data.m_webdav_dir = dirStr.GetString();

	std::wstring errMsg;
	CWaitCursor wait;
	if (CWebDavSync::UploadBackup(m_data, errMsg))
	{
		time_t now = time(nullptr);
		tm t;
		localtime_s(&t, &now);
		wchar_t timeBuf[64];
		wcsftime(timeBuf, 64, L"%Y-%m-%d %H:%M:%S", &t);
		m_data.m_webdav_last_sync_time = timeBuf;

		g_data.m_setting_data = m_data;
		g_data.SaveConfig();

		Invalidate();
		MessageBox(L"已成功将全部配置与自选股备份至 WebDAV 云端！", L"备份成功", MB_ICONINFORMATION | MB_OK);
	}
	else
	{
		MessageBox((L"上传备份失败：\n" + errMsg).c_str(), L"备份失败", MB_ICONERROR | MB_OK);
	}
}

void CManagerDialog::OnBnClickedWebDavDownloadBtn()
{
	if (MessageBox(L"从云端恢复将覆盖本地当前的股票列表与全部配置，是否继续？", L"确认恢复", MB_ICONQUESTION | MB_YESNO) != IDYES)
		return;

	CString urlStr, userStr, pwdStr, dirStr;
	GetDlgItemText(IDC_WEBDAV_URL_EDIT, urlStr);
	GetDlgItemText(IDC_WEBDAV_USER_EDIT, userStr);
	GetDlgItemText(IDC_WEBDAV_PWD_EDIT, pwdStr);
	GetDlgItemText(IDC_WEBDAV_DIR_EDIT, dirStr);

	m_data.m_webdav_url = urlStr.GetString();
	m_data.m_webdav_username = userStr.GetString();
	m_data.m_webdav_password = pwdStr.GetString();
	m_data.m_webdav_dir = dirStr.GetString();

	std::wstring errMsg;
	CWaitCursor wait;
	if (CWebDavSync::DownloadBackup(m_data, errMsg))
	{
		m_data = g_data.m_setting_data;

		CheckDlgButton(IDC_FULL_DAY_CHECK, m_data.m_full_day ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_SHOW_STOCK_NAME_CHECK, m_data.m_show_stock_name ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_COLOR_WITH_PRICE_CHECK, m_data.m_color_with_price ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy ? BST_CHECKED : BST_UNCHECKED);
		SetDlgItemText(IDC_SOCKS5_PROXY_EDIT, m_data.m_socks5_proxy.c_str());

		CString strKlineW, strKlineH;
		strKlineW.Format(_T("%d"), static_cast<int>(m_data.m_kline_width));
		SetDlgItemText(IDC_KLINE_WIDTH_EDIT, strKlineW);
		strKlineH.Format(_T("%d"), static_cast<int>(m_data.m_kline_height));
		SetDlgItemText(IDC_KLINE_HEIGHT_EDIT, strKlineH);

		SetDlgItemText(IDC_WEBDAV_URL_EDIT, m_data.m_webdav_url.c_str());
		SetDlgItemText(IDC_WEBDAV_USER_EDIT, m_data.m_webdav_username.c_str());
		SetDlgItemText(IDC_WEBDAV_PWD_EDIT, m_data.m_webdav_password.c_str());
		SetDlgItemText(IDC_WEBDAV_DIR_EDIT, m_data.m_webdav_dir.c_str());
		CheckDlgButton(IDC_WEBDAV_AUTO_SYNC_CHECK, m_data.m_webdav_auto_sync ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(IDC_WEBDAV_AUTO_BACKUP_CHECK, m_data.m_webdav_auto_backup ? BST_CHECKED : BST_UNCHECKED);

		RefreshStockList();
		RefreshPositionList();
		RefreshCustomList();
		Invalidate();

		MessageBox(L"已成功从 WebDAV 云端恢复最新配置并加载！", L"恢复成功", MB_ICONINFORMATION | MB_OK);
	}
	else
	{
		MessageBox((L"从云端恢复失败：\n" + errMsg).c_str(), L"恢复失败", MB_ICONERROR | MB_OK);
	}
}

void CManagerDialog::OnBnClickedOk()
{
	bool stock_code_changed{ g_data.m_setting_data.m_stock_codes != m_data.m_stock_codes };

	CString value;
	GetDlgItemText(IDC_KLINE_WIDTH_EDIT, value);
	int kw = _ttoi(value);
	if (kw > 0)
		m_data.m_kline_width = kw;

	GetDlgItemText(IDC_KLINE_HEIGHT_EDIT, value);
	int kh = _ttoi(value);
	if (kh > 0)
		m_data.m_kline_height = kh;

	CString proxy_addr;
	GetDlgItemText(IDC_SOCKS5_PROXY_EDIT, proxy_addr);
	m_data.m_socks5_proxy = proxy_addr.GetString();

	CString urlStr, userStr, pwdStr, dirStr;
	GetDlgItemText(IDC_WEBDAV_URL_EDIT, urlStr);
	GetDlgItemText(IDC_WEBDAV_USER_EDIT, userStr);
	GetDlgItemText(IDC_WEBDAV_PWD_EDIT, pwdStr);
	GetDlgItemText(IDC_WEBDAV_DIR_EDIT, dirStr);
	m_data.m_webdav_url = urlStr.GetString();
	m_data.m_webdav_username = userStr.GetString();
	m_data.m_webdav_password = pwdStr.GetString();
	m_data.m_webdav_dir = dirStr.GetString();
	m_data.m_webdav_auto_sync = (IsDlgButtonChecked(IDC_WEBDAV_AUTO_SYNC_CHECK) != 0);
	m_data.m_webdav_auto_backup = (IsDlgButtonChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK) != 0);

	g_data.m_setting_data = m_data;
	g_data.SaveConfig();

	if (m_data.m_webdav_auto_backup && !m_data.m_webdav_url.empty())
	{
		SettingData curData = m_data;
		CStockFetchThread::Instance().PostBackgroundTask([curData]() {
			std::wstring err;
			CWebDavSync::UploadBackup(curData, err);
		});
	}

	if (stock_code_changed)
	{
		std::set<std::wstring> old_codes(g_data.m_setting_data.m_stock_codes.begin(), g_data.m_setting_data.m_stock_codes.end());
		std::vector<std::wstring> new_codes;
		for (const auto& code : m_data.m_stock_codes)
		{
			if (old_codes.find(code) == old_codes.end())
				new_codes.push_back(code);
		}

		Stock::Instance().SendStockInfoRequest();

		if (!new_codes.empty())
		{
			CStockFetchThread::Instance().PostBackgroundTask([new_codes]() {
				for (const auto& code : new_codes)
				{
					CStockFetchThread::Instance().FetchDayKLine(code, 750);
				}
			});
		}
	}
	else
	{
		Stock::Instance().SendStockInfoRequest();
	}

	CDialog::OnOK();
}

void CManagerDialog::OnBnClickedCancel()
{
	CDialog::OnCancel();
}
