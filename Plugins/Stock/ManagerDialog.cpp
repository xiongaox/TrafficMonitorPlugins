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
#include "ChartColors.h"
#include <Windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <set>
#include <shellapi.h>
#include <ctime>
#include <uxtheme.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

// CManagerDialog 对话框

IMPLEMENT_DYNAMIC(CManagerDialog, CDialog)

CManagerDialog::CManagerDialog(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_MANAGER_DIALOG, pParent)
{
	m_menu_rects.resize(6);
	m_dark_brush.CreateSolidBrush(COLOR_BG_DARK);     // #12141A
	m_card_brush.CreateSolidBrush(COLOR_BG_CARD);     // #181B22
	m_edit_brush.CreateSolidBrush(RGB(13, 15, 21));   // 输入框内嵌底色（略深于卡片，形成下沉观感）
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
	ON_WM_DRAWITEM()
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

	// 列表行自绘（交替行底色/选中高亮）
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_MGR_LIST, &CManagerDialog::OnListCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_POS_LIST, &CManagerDialog::OnListCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CUSTOM_LIST, &CManagerDialog::OnListCustomDraw)

	// 输入框焦点变化时重绘自绘边框（聚焦高亮蓝）
	ON_EN_SETFOCUS(IDC_KLINE_WIDTH_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_KLINE_WIDTH_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_KLINE_HEIGHT_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_KLINE_HEIGHT_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_SOCKS5_PROXY_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_SOCKS5_PROXY_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_MA_INPUT_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_MA_INPUT_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_WEBDAV_URL_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_URL_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_WEBDAV_USER_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_USER_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_WEBDAV_PWD_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_PWD_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_SETFOCUS(IDC_WEBDAV_DIR_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_DIR_EDIT, &CManagerDialog::OnEditFocusChanged)

	ON_BN_CLICKED(IDOK, &CManagerDialog::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CManagerDialog::OnBnClickedCancel)
END_MESSAGE_MAP()

// CManagerDialog 消息处理程序

BOOL CManagerDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	HICON hIcon = g_data.GetIcon(IDI_STOCK);
	SetIcon(hIcon, FALSE);

	// 深色标题栏（Win10 1809+ / Win11），与插件暗色主题保持一致
	BOOL darkCaption = TRUE;
	if (FAILED(DwmSetWindowAttribute(GetSafeHwnd(), DWMWA_USE_IMMERSIVE_DARK_MODE, &darkCaption, sizeof(darkCaption))))
	{
		DwmSetWindowAttribute(GetSafeHwnd(), 19, &darkCaption, sizeof(darkCaption));
	}

	// 设置窗口默认大小和最小尺寸
	int initWidth = g_data.DPI(760);
	int initHeight = g_data.DPI(520);
	m_min_size.cx = g_data.DPI(680);
	m_min_size.cy = g_data.DPI(460);

	CRect curRect;
	GetWindowRect(curRect);
	SetWindowPos(nullptr, curRect.left, curRect.top, initWidth, initHeight, SWP_NOMOVE | SWP_NOZORDER);

	m_menu_width = g_data.DPI(140);

	// 创建与走势图一致的微软雅黑字阶体系 (字号升级，清晰易读)
	m_font.CreateFont(-g_data.RDPI(13), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	m_font_bold.CreateFont(-g_data.RDPI(13), 0, 0, 0, FW_BOLD, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	m_font_title.CreateFont(-g_data.RDPI(16), 0, 0, 0, FW_BOLD, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	// 全局应用清晰字体
	EnumChildWindows(m_hWnd, [](HWND hWnd, LPARAM lParam) -> BOOL {
		::SendMessage(hWnd, WM_SETFONT, lParam, TRUE);
		return TRUE;
	}, (LPARAM)m_font.GetSafeHandle());

	// ===== 输入框：去掉系统边框与主题，背景/边框全部由 OnCtlColor/OnPaint 自绘 =====
	// 注意：ModifyStyle 改样式位后默认不重算非客户区，WS_BORDER 白边会残留，
	// 必须再发一次 SWP_FRAMECHANGED 才能真正摘掉原生边框。
	const int editControlIds[] = {
		IDC_KLINE_WIDTH_EDIT, IDC_KLINE_HEIGHT_EDIT, IDC_SOCKS5_PROXY_EDIT,
		IDC_WEBDAV_URL_EDIT, IDC_WEBDAV_USER_EDIT, IDC_WEBDAV_PWD_EDIT, IDC_WEBDAV_DIR_EDIT,
		IDC_MA_INPUT_EDIT
	};
	for (int id : editControlIds)
	{
		CWnd* pWnd = GetDlgItem(id);
		if (pWnd && pWnd->GetSafeHwnd())
		{
			pWnd->ModifyStyle(WS_BORDER, 0);
			pWnd->ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
			SetWindowTheme(pWnd->GetSafeHwnd(), L"", L"");
			::SetWindowPos(pWnd->GetSafeHwnd(), nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
	}

	// ===== 复选框：改为自绘按钮，勾选状态由 m_checkStates 托管（告别原生白底方块） =====
	const int checkControlIds[] = {
		IDC_FULL_DAY_CHECK, IDC_SHOW_STOCK_NAME_CHECK, IDC_COLOR_WITH_PRICE_CHECK,
		IDC_SHOW_FLUCTUATION_CHECK, IDC_USE_SOCKS5_PROXY_CHECK,
		IDC_WEBDAV_AUTO_SYNC_CHECK, IDC_WEBDAV_AUTO_BACKUP_CHECK
	};
	for (int id : checkControlIds)
	{
		CWnd* pWnd = GetDlgItem(id);
		if (pWnd && pWnd->GetSafeHwnd())
		{
			pWnd->ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
			SetWindowTheme(pWnd->GetSafeHwnd(), L"", L"");
			pWnd->InvalidateRect(nullptr);
		}
	}

	// 启用全部按钮自绘 (BS_OWNERDRAW)，彻底告别原生白底按钮
	const int ownerDrawBtnIds[] = {
		IDOK, IDCANCEL,
		IDC_MGR_ADD_BTN, IDC_MGR_EDIT_BTN, IDC_MGR_DEL_BTN, IDC_MGR_MOVE_UP_BTN, IDC_MGR_MOVE_DOWN_BTN,
		IDC_MA_ADD_BTN,
		IDC_WEBDAV_TEST_BTN, IDC_WEBDAV_UPLOAD_BTN, IDC_WEBDAV_DOWNLOAD_BTN
	};
	for (int id : ownerDrawBtnIds)
	{
		CWnd* pBtn = GetDlgItem(id);
		if (pBtn && pBtn->GetSafeHwnd())
			pBtn->ModifyStyle(0, BS_OWNERDRAW);
	}

	// 初始化列表深色背景与扩展属性 (不使用 LVS_EX_GRIDLINES，避免刺眼白网格)
	DWORD dwStyle = LVS_EX_FULLROWSELECT;

	auto setupListDarkTheme = [dwStyle](CListCtrl& list) {
		list.SetExtendedStyle(dwStyle);
		list.SetBkColor(COLOR_BG_PANEL);
		list.SetTextBkColor(COLOR_BG_PANEL);
		list.SetTextColor(COLOR_TEXT_PRIMARY);
		HWND hHeader = list.GetHeaderCtrl()->GetSafeHwnd();
		if (hHeader)
			SetWindowTheme(hHeader, L"", L"");
	};

	setupListDarkTheme(m_stock_listctrl);
	m_stock_listctrl.InsertColumn(0, L"代码", LVCFMT_LEFT, g_data.DPI(80));
	m_stock_listctrl.InsertColumn(1, L"股票名称", LVCFMT_LEFT, g_data.DPI(100));
	m_stock_listctrl.InsertColumn(2, L"关注低价", LVCFMT_RIGHT, g_data.DPI(80));
	m_stock_listctrl.InsertColumn(3, L"关注高价", LVCFMT_RIGHT, g_data.DPI(80));
	m_stock_listctrl.InsertColumn(4, L"状态栏显示", LVCFMT_CENTER, g_data.DPI(80));

	setupListDarkTheme(m_pos_listctrl);
	m_pos_listctrl.InsertColumn(0, L"代码", LVCFMT_LEFT, g_data.DPI(75));
	m_pos_listctrl.InsertColumn(1, L"股票名称", LVCFMT_LEFT, g_data.DPI(90));
	m_pos_listctrl.InsertColumn(2, L"成本价", LVCFMT_RIGHT, g_data.DPI(70));
	m_pos_listctrl.InsertColumn(3, L"持股数", LVCFMT_RIGHT, g_data.DPI(70));
	m_pos_listctrl.InsertColumn(4, L"买入日期", LVCFMT_CENTER, g_data.DPI(85));
	m_pos_listctrl.InsertColumn(5, L"成本总额", LVCFMT_RIGHT, g_data.DPI(80));
	m_pos_listctrl.InsertColumn(6, L"当前价", LVCFMT_RIGHT, g_data.DPI(70));
	m_pos_listctrl.InsertColumn(7, L"浮动盈亏", LVCFMT_RIGHT, g_data.DPI(80));

	setupListDarkTheme(m_custom_listctrl);
	m_custom_listctrl.InsertColumn(0, L"代码", LVCFMT_LEFT, g_data.DPI(85));
	m_custom_listctrl.InsertColumn(1, L"股票名称", LVCFMT_LEFT, g_data.DPI(110));
	m_custom_listctrl.InsertColumn(2, L"关注低价", LVCFMT_RIGHT, g_data.DPI(80));
	m_custom_listctrl.InsertColumn(3, L"关注高价", LVCFMT_RIGHT, g_data.DPI(80));

	// 表头改为自绘平面化样式，列表启用双缓冲与深色滚动条，并去掉系统边框
	auto setupFlatHeader = [this](CListCtrl& list, CFlatHeaderCtrl& hdr) {
		HWND hHeader = list.GetHeaderCtrl() ? list.GetHeaderCtrl()->GetSafeHwnd() : nullptr;
		if (hHeader && hdr.GetSafeHwnd() == nullptr)
			hdr.SubclassWindow(hHeader);
		SetWindowTheme(list.GetSafeHwnd(), L"DarkMode_Explorer", nullptr);
		list.SetExtendedStyle(list.GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);
		list.ModifyStyle(WS_BORDER, 0);
		list.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		::SetWindowPos(list.GetSafeHwnd(), nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	};
	setupFlatHeader(m_stock_listctrl, m_hdr_stock);
	setupFlatHeader(m_pos_listctrl, m_hdr_pos);
	setupFlatHeader(m_custom_listctrl, m_hdr_custom);

	// 加载基础配置控件值（自绘复选框状态）
	SetCheck(IDC_FULL_DAY_CHECK, m_data.m_full_day);
	SetCheck(IDC_SHOW_STOCK_NAME_CHECK, m_data.m_show_stock_name);
	SetCheck(IDC_COLOR_WITH_PRICE_CHECK, m_data.m_color_with_price);
	SetCheck(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation);
	SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy);
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
	SetCheck(IDC_WEBDAV_AUTO_SYNC_CHECK, m_data.m_webdav_auto_sync);
	SetCheck(IDC_WEBDAV_AUTO_BACKUP_CHECK, m_data.m_webdav_auto_backup);

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
	if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN)
	{
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(COLOR_TEXT_PRIMARY);
		return (HBRUSH)m_card_brush.GetSafeHandle();
	}
	else if (nCtlColor == CTLCOLOR_EDIT)
	{
		pDC->SetBkMode(OPAQUE);
		pDC->SetBkColor(RGB(13, 15, 21));
		pDC->SetTextColor(COLOR_TEXT_PRIMARY);
		return (HBRUSH)m_edit_brush.GetSafeHandle();
	}
	else if (nCtlColor == CTLCOLOR_LISTBOX)
	{
		pDC->SetBkMode(OPAQUE);
		pDC->SetBkColor(COLOR_BG_DARK);
		pDC->SetTextColor(COLOR_TEXT_PRIMARY);
		return (HBRUSH)m_dark_brush.GetSafeHandle();
	}
	else if (nCtlColor == CTLCOLOR_DLG)
	{
		return (HBRUSH)m_dark_brush.GetSafeHandle();
	}
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CManagerDialog::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (lpDrawItemStruct->CtlType == ODT_BUTTON)
	{
		CDC dc;
		dc.Attach(lpDrawItemStruct->hDC);
		CRect r = lpDrawItemStruct->rcItem;
		UINT nID = lpDrawItemStruct->CtlID;

		CString text;
		CWnd* pBtn = GetDlgItem(nID);
		if (pBtn)
			pBtn->GetWindowText(text);

		if (IsCheckCtrl(nID))
		{
			// ===== 自绘复选框：暗色方块 + 品牌蓝勾选态，与浮动窗配色一致 =====
			bool checked = IsChecked(nID);
			bool hot = (lpDrawItemStruct->itemState & ODS_HOTLIGHT) != 0;

			int boxSize = g_data.DPI(14);
			int boxTop = r.top + (r.Height() - boxSize) / 2;
			CRect box(r.left, boxTop, r.left + boxSize, boxTop + boxSize);

			COLORREF boxBorder = checked ? COLOR_ACCENT_BLUE : (hot ? RGB(100, 116, 139) : RGB(71, 78, 94));
			dc.FillSolidRect(box, checked ? COLOR_ACCENT_BLUE : RGB(20, 22, 29));
			dc.Draw3dRect(box, boxBorder, boxBorder);

			if (checked)
			{
				CPen pen(PS_SOLID, max(1, g_data.DPI(2)), RGB(255, 255, 255));
				CPen* pOldPen = dc.SelectObject(&pen);
				dc.MoveTo(box.left + g_data.DPI(3), box.top + g_data.DPI(7));
				dc.LineTo(box.left + g_data.DPI(6), box.top + g_data.DPI(10));
				dc.LineTo(box.left + g_data.DPI(11), box.top + g_data.DPI(4));
				dc.SelectObject(pOldPen);
			}

			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(hot ? RGB(255, 255, 255) : COLOR_TEXT_PRIMARY);
			CFont* pOldFont = dc.SelectObject(&m_font);
			CRect textRect(r.left + boxSize + g_data.DPI(9), r.top, r.right, r.bottom);
			dc.DrawText(text, textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			dc.SelectObject(pOldFont);

			dc.Detach();
			return;
		}

		// ===== 普通按钮：与浮动窗一致的扁平暗色样式（直角 + 细边框 + 悬停/按下反馈） =====
		bool pressedState = (lpDrawItemStruct->itemState & ODS_SELECTED) != 0;
		bool hot = (lpDrawItemStruct->itemState & ODS_HOTLIGHT) != 0;
		DrawFlatButton(dc, r, text, IsPrimaryBtn(nID), IsDestructiveBtn(nID), hot, pressedState);

		dc.Detach();
		return;
	}
	CDialog::OnDrawItem(nIDCtl, lpDrawItemStruct);
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
	int rightTop = g_data.DPI(72);
	int rightWidth = clientRect.Width() - rightLeft - g_data.DPI(18);
	int rightBottom = clientRect.Height() - g_data.DPI(52);

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
		// 卡片 1: 行情与走势图展示（两列开关，位置与 DrawBasicPage 卡片严格对应）
		int card1Top = rightTop;
		const int chkIds[2][2] = {
			{ IDC_FULL_DAY_CHECK, IDC_SHOW_STOCK_NAME_CHECK },
			{ IDC_COLOR_WITH_PRICE_CHECK, IDC_SHOW_FLUCTUATION_CHECK }
		};
		int chkW = g_data.DPI(165);
		int chkH = g_data.DPI(22);
		for (int row = 0; row < 2; ++row)
		{
			for (int col = 0; col < 2; ++col)
			{
				CWnd* pChk = GetDlgItem(chkIds[row][col]);
				if (pChk && pChk->GetSafeHwnd())
					pChk->MoveWindow(rightLeft + g_data.DPI(18) + col * g_data.DPI(195), card1Top + g_data.DPI(40) + row * g_data.DPI(30), chkW, chkH);
			}
		}

		// 卡片 2: 走势图尺寸配置
		int card2Top = card1Top + g_data.DPI(110);
		CWnd* pKWLbl = GetDlgItem(IDC_KLINE_WIDTH_STATIC);
		CWnd* pKWEdit = GetDlgItem(IDC_KLINE_WIDTH_EDIT);
		CWnd* pKHLbl = GetDlgItem(IDC_KLINE_HEIGHT_STATIC);
		CWnd* pKHEdit = GetDlgItem(IDC_KLINE_HEIGHT_EDIT);

		if (pKWLbl && pKWLbl->GetSafeHwnd()) pKWLbl->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(42), g_data.DPI(90), g_data.DPI(20));
		if (pKWEdit && pKWEdit->GetSafeHwnd()) pKWEdit->MoveWindow(rightLeft + g_data.DPI(112), card2Top + g_data.DPI(38), g_data.DPI(80), g_data.DPI(26));
		if (pKHLbl && pKHLbl->GetSafeHwnd()) pKHLbl->MoveWindow(rightLeft + g_data.DPI(220), card2Top + g_data.DPI(42), g_data.DPI(90), g_data.DPI(20));
		if (pKHEdit && pKHEdit->GetSafeHwnd()) pKHEdit->MoveWindow(rightLeft + g_data.DPI(314), card2Top + g_data.DPI(38), g_data.DPI(80), g_data.DPI(26));

		// 卡片 3: SOCKS5 代理网络
		int card3Top = card1Top + g_data.DPI(192);
		CWnd* pProxyChk = GetDlgItem(IDC_USE_SOCKS5_PROXY_CHECK);
		CWnd* pProxyLbl = GetDlgItem(IDC_SOCKS5_PROXY_STATIC);
		CWnd* pProxyEdit = GetDlgItem(IDC_SOCKS5_PROXY_EDIT);

		if (pProxyChk && pProxyChk->GetSafeHwnd()) pProxyChk->MoveWindow(rightLeft + g_data.DPI(18), card3Top + g_data.DPI(40), g_data.DPI(150), chkH);
		if (pProxyLbl && pProxyLbl->GetSafeHwnd()) pProxyLbl->MoveWindow(rightLeft + g_data.DPI(180), card3Top + g_data.DPI(42), g_data.DPI(65), g_data.DPI(20));
		if (pProxyEdit && pProxyEdit->GetSafeHwnd()) pProxyEdit->MoveWindow(rightLeft + g_data.DPI(250), card3Top + g_data.DPI(38), min(g_data.DPI(210), rightWidth - g_data.DPI(268)), g_data.DPI(26));
	}

	// 分组管理控件布局
	bool isGroup = (m_current_page == PAGE_GROUPS);
	int listTop = rightTop + g_data.DPI(42);
	int listHeight = rightBottom - listTop - g_data.DPI(44);

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

		int btnTop = listTop + listHeight + g_data.DPI(10);
		int btnW = g_data.DPI(72);
		int btnH = g_data.DPI(26);
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
		int maInputTop = rightTop + g_data.DPI(130);
		if (m_ma_input_edit.GetSafeHwnd())
			m_ma_input_edit.MoveWindow(rightLeft + g_data.DPI(155), maInputTop, g_data.DPI(84), g_data.DPI(26));
		if (m_ma_add_btn.GetSafeHwnd())
			m_ma_add_btn.MoveWindow(rightLeft + g_data.DPI(249), maInputTop, g_data.DPI(88), g_data.DPI(26));
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
		// 卡片 1: 四行参数输入，行高 30，与 DrawWebDavPage 卡片位置一致
		int card1Top = rightTop;
		int lblW = g_data.DPI(80);
		int editW = min(g_data.DPI(330), rightWidth - lblW - g_data.DPI(46));
		int rowY0 = card1Top + g_data.DPI(40);
		int rowStep = g_data.DPI(30);

		const int wdLabelIds[] = { IDC_WEBDAV_URL_STATIC, IDC_WEBDAV_USER_STATIC, IDC_WEBDAV_PWD_STATIC, IDC_WEBDAV_DIR_STATIC };
		const int wdEditIds[] = { IDC_WEBDAV_URL_EDIT, IDC_WEBDAV_USER_EDIT, IDC_WEBDAV_PWD_EDIT, IDC_WEBDAV_DIR_EDIT };
		for (int i = 0; i < 4; ++i)
		{
			CWnd* pLbl = GetDlgItem(wdLabelIds[i]);
			CWnd* pEdit = GetDlgItem(wdEditIds[i]);
			if (pLbl && pLbl->GetSafeHwnd()) pLbl->MoveWindow(rightLeft + g_data.DPI(18), rowY0 + i * rowStep + g_data.DPI(4), lblW, g_data.DPI(20));
			if (pEdit && pEdit->GetSafeHwnd()) pEdit->MoveWindow(rightLeft + g_data.DPI(18) + lblW + g_data.DPI(10), rowY0 + i * rowStep, editW, g_data.DPI(26));
		}

		// 卡片 2: 勾选项 / 操作按钮 / 提示文字分区排布，杜绝重叠
		int card2Top = card1Top + g_data.DPI(178);
		CWnd* pSyncChk = GetDlgItem(IDC_WEBDAV_AUTO_SYNC_CHECK);
		CWnd* pBakChk = GetDlgItem(IDC_WEBDAV_AUTO_BACKUP_CHECK);
		if (pSyncChk && pSyncChk->GetSafeHwnd()) pSyncChk->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(38), g_data.DPI(300), g_data.DPI(22));
		if (pBakChk && pBakChk->GetSafeHwnd()) pBakChk->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(66), g_data.DPI(300), g_data.DPI(22));

		int wdBtnW = g_data.DPI(88);
		int wdBtnH = g_data.DPI(28);
		int wdGap = g_data.DPI(10);
		CWnd* pTestBtn = GetDlgItem(IDC_WEBDAV_TEST_BTN);
		CWnd* pUpBtn = GetDlgItem(IDC_WEBDAV_UPLOAD_BTN);
		CWnd* pDownBtn = GetDlgItem(IDC_WEBDAV_DOWNLOAD_BTN);
		if (pTestBtn && pTestBtn->GetSafeHwnd()) pTestBtn->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(96), wdBtnW, wdBtnH);
		if (pUpBtn && pUpBtn->GetSafeHwnd()) pUpBtn->MoveWindow(rightLeft + g_data.DPI(18) + (wdBtnW + wdGap), card2Top + g_data.DPI(96), wdBtnW + g_data.DPI(16), wdBtnH);
		if (pDownBtn && pDownBtn->GetSafeHwnd()) pDownBtn->MoveWindow(rightLeft + g_data.DPI(18) + (wdBtnW + wdGap) * 2 + g_data.DPI(16), card2Top + g_data.DPI(96), wdBtnW + g_data.DPI(16), wdBtnH);
	}

	// 底部确定与取消按钮
	CWnd* pOkBtn = GetDlgItem(IDOK);
	CWnd* pCancelBtn = GetDlgItem(IDCANCEL);
	int okBtnW = g_data.DPI(75);
	int okBtnH = g_data.DPI(28);
	int btnY = clientRect.Height() - g_data.DPI(40);

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

	// 整体深色底 (#12141A)
	Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 18, 20, 26));
	g.FillRectangle(&bgBrush, 0, 0, clientRect.Width(), clientRect.Height());

	// 绘制左侧导航菜单
	DrawSidebar(g, clientRect);

	// 绘制右侧内容头部
	DrawHeader(g, clientRect);

	CRect contentRect(m_menu_width + g_data.DPI(18), g_data.DPI(72), clientRect.Width() - g_data.DPI(18), clientRect.Height() - g_data.DPI(52));

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

	// 自绘输入框与列表边框（聚焦品牌蓝高亮，失焦暗灰），绘制在卡片之上
	const int borderedEditIds[] = {
		IDC_KLINE_WIDTH_EDIT, IDC_KLINE_HEIGHT_EDIT, IDC_SOCKS5_PROXY_EDIT,
		IDC_WEBDAV_URL_EDIT, IDC_WEBDAV_USER_EDIT, IDC_WEBDAV_PWD_EDIT, IDC_WEBDAV_DIR_EDIT,
		IDC_MA_INPUT_EDIT
	};
	for (int id : borderedEditIds)
		DrawControlBorder(g, id);
	DrawControlBorder(g, IDC_MGR_LIST);
	DrawControlBorder(g, IDC_POS_LIST);
	DrawControlBorder(g, IDC_CUSTOM_LIST);

	dc.BitBlt(0, 0, clientRect.Width(), clientRect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

void CManagerDialog::DrawSidebar(Gdiplus::Graphics& g, const CRect& clientRect)
{
	// 侧边栏深色底 (#14161D)
	Gdiplus::SolidBrush sideBrush(Gdiplus::Color(255, 20, 22, 29));
	g.FillRectangle(&sideBrush, 0, 0, m_menu_width, clientRect.Height());

	// 侧边栏右侧暗黑细线 (#262A36)
	Gdiplus::Pen divPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
	g.DrawLine(&divPen, m_menu_width, 0, m_menu_width, clientRect.Height());

	// 侧边栏顶部品牌标识
	Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 37, 99, 235)); // Accent Blue
	g.FillEllipse(&dotBrush, g_data.DPI(16), g_data.DPI(18), g_data.DPI(9), g_data.DPI(9));

	Gdiplus::Font titleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 241, 245, 249));
	g.DrawString(L"股票管理", -1, &titleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(g_data.DPI(30)), static_cast<Gdiplus::REAL>(g_data.DPI(13))), &titleBrush);

	const wchar_t* menuTitles[] = { L"基础设置", L"指数编辑", L"分组管理", L"均线日配置", L"云端备份", L"关于插件" };
	int menuCount = 6;
	int itemH = g_data.DPI(40);
	int itemTop = g_data.DPI(54);
	int itemPadX = g_data.DPI(8);
	int itemW = m_menu_width - (itemPadX * 2);

	Gdiplus::Font menuFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font menuActiveFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

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
			Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 28, 45, 75)); // #1C2D4B
			g.FillRectangle(&activeBg, rf);

			Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 37, 99, 235)); // Left Blue Accent
			g.FillRectangle(&barBrush, static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(r.Height()));

			Gdiplus::SolidBrush txtBrush(Gdiplus::Color(255, 255, 255, 255));
			Gdiplus::RectF textRf(static_cast<Gdiplus::REAL>(r.left + g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width() - g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.Height()));
			g.DrawString(menuTitles[i], -1, &menuActiveFont, textRf, &sf, &txtBrush);
		}
		else
		{
			if (i == m_hover_menu)
			{
				Gdiplus::SolidBrush hoverBg(Gdiplus::Color(255, 24, 27, 34));
				g.FillRectangle(&hoverBg, rf);
			}

			Gdiplus::SolidBrush txtBrush(i == m_hover_menu ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 148, 163, 184));
			Gdiplus::RectF textRf(static_cast<Gdiplus::REAL>(r.left + g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width() - g_data.DPI(14)), static_cast<Gdiplus::REAL>(r.Height()));
			g.DrawString(menuTitles[i], -1, &menuFont, textRf, &sf, &txtBrush);
		}

		itemTop += itemH + g_data.DPI(3);
	}

	// 侧边栏底部版本信息
	Gdiplus::Font verFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush verBrush(Gdiplus::Color(255, 100, 116, 139));
	g.DrawString(L"Stock Plugin v1.15", -1, &verFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(g_data.DPI(14)), static_cast<Gdiplus::REAL>(clientRect.Height() - g_data.DPI(28))), &verBrush);
}

void CManagerDialog::DrawHeader(Gdiplus::Graphics& g, const CRect& clientRect)
{
	int rightLeft = m_menu_width + g_data.DPI(18);
	int headerTop = g_data.DPI(14);

	const wchar_t* titles[] = { L"基础设置", L"指数编辑", L"分组管理", L"均线日配置", L"云端备份", L"关于插件" };
	const wchar_t* subs[] = {
		L"配置全天更新、代理网络及走势图尺寸参数",
		L"点击卡片选择展示的指数，前 5 个展示在首页顶部",
		L"管理自选股票列表、持仓配置与自定义分组",
		L"自定义均线周期（最多 5 条；点标签右上角 × 删除；在下方输入添加）",
		L"基于 WebDAV 协议在多台电脑间安全备份与同步配置",
		L"TrafficMonitor 专业级股票行情监控插件"
	};

	Gdiplus::Font headFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(14)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush headBrush(Gdiplus::Color(255, 241, 245, 249));

	// 标题前的品牌蓝竖条（与卡片章节标题同一视觉语言）
	Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 37, 99, 235));
	g.FillRectangle(&barBrush, static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(headerTop + g_data.DPI(3)), static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(g_data.DPI(16)));
	g.DrawString(titles[m_current_page], -1, &headFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(10)), static_cast<Gdiplus::REAL>(headerTop)), &headBrush);

	Gdiplus::Font subFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 148, 163, 184));

	std::wstring subText = subs[m_current_page];
	if (m_current_page == PAGE_INDEX)
	{
		subText = L"点击卡片选择展示的指数，前 5 个展示在首页顶部 (已选: " + std::to_wstring(m_data.m_selected_indices.size()) + L")";
	}
	else if (m_current_page == PAGE_MA)
	{
		subText = L"最多 5 条；点标签右上角 × 删除；在下方输入添加。已选 (" + std::to_wstring(m_data.m_ma_days.size()) + L"/5)";
	}

	g.DrawString(subText.c_str(), -1, &subFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(10)), static_cast<Gdiplus::REAL>(headerTop + g_data.DPI(24))), &subBrush);

	Gdiplus::Pen divPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
	g.DrawLine(&divPen, rightLeft, g_data.DPI(56), clientRect.Width() - g_data.DPI(18), g_data.DPI(56));
}

void CManagerDialog::DrawBasicPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));      // #181B22
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);   // #262A36

	// 统一卡片样式：深色底 + 细边框 + 品牌蓝竖条章节标题
	auto drawCard = [&](int top, int height, const std::wstring& title) {
		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(height));
		g.FillRectangle(&cardBg, rf);
		g.DrawRectangle(&cardBorder, rf);
		DrawSectionTitle(g, rightLeft + g_data.DPI(14), top + g_data.DPI(12), title);
	};

	// 卡片位置/高度与 UpdateControlsLayout 严格对应
	drawCard(contentRect.top, g_data.DPI(100), L"行情与走势图展示");
	drawCard(contentRect.top + g_data.DPI(110), g_data.DPI(72), L"走势图高清尺寸（像素）");
	drawCard(contentRect.top + g_data.DPI(192), g_data.DPI(72), L"SOCKS5 代理网络");
}

void CManagerDialog::DrawIndexPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	const auto& presets = GetPresetIndices();
	m_index_card_rects.clear();
	m_index_card_rects.resize(presets.size());

	int cols = 3;
	int cardGapX = g_data.DPI(10);
	int cardGapY = g_data.DPI(8);
	int cardW = (contentRect.Width() - (cardGapX * (cols - 1))) / cols;
	int cardH = g_data.DPI(46);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font codeFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(9)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font rankFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(9)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

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
			Gdiplus::SolidBrush selBg(Gdiplus::Color(255, 28, 45, 75)); // #1C2D4B
			g.FillRectangle(&selBg, rf);

			Gdiplus::Pen borderPen(Gdiplus::Color(255, 37, 99, 235), 1.2f); // #2563EB
			g.DrawRectangle(&borderPen, rf);

			if (rank <= 5)
			{
				Gdiplus::SolidBrush rankBg(Gdiplus::Color(255, 37, 99, 235));
				g.FillEllipse(&rankBg, x + cardW - g_data.DPI(24), y + g_data.DPI(8), g_data.DPI(16), g_data.DPI(16));

				Gdiplus::SolidBrush rankTxtBrush(Gdiplus::Color(255, 255, 255, 255));
				Gdiplus::StringFormat sfRank;
				sfRank.SetAlignment(Gdiplus::StringAlignmentCenter);
				sfRank.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				std::wstring rankStr = std::to_wstring(rank);
				Gdiplus::RectF rankRf(static_cast<Gdiplus::REAL>(x + cardW - g_data.DPI(24)), static_cast<Gdiplus::REAL>(y + g_data.DPI(8)), static_cast<Gdiplus::REAL>(g_data.DPI(16)), static_cast<Gdiplus::REAL>(g_data.DPI(16)));
				g.DrawString(rankStr.c_str(), -1, &rankFont, rankRf, &sfRank, &rankTxtBrush);
			}

			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(presets[i].name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(7))), &nameBrush);

			Gdiplus::SolidBrush codeBrush(Gdiplus::Color(255, 147, 197, 253));
			g.DrawString(presets[i].code.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(25))), &codeBrush);
		}
		else
		{
			Gdiplus::SolidBrush unselBg(Gdiplus::Color(255, 24, 27, 34)); // #181B22
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen borderPen(static_cast<int>(i) == m_hover_index_card ? Gdiplus::Color(255, 100, 116, 139) : Gdiplus::Color(255, 38, 42, 54), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 226, 232, 240));
			g.DrawString(presets[i].name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(7))), &nameBrush);

			Gdiplus::SolidBrush codeBrush(Gdiplus::Color(255, 100, 116, 139));
			g.DrawString(presets[i].code.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y + g_data.DPI(25))), &codeBrush);
		}
	}
}

void CManagerDialog::DrawGroupPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	const wchar_t* groupTabs[] = { L"自选股", L"持仓", L"自定义" };
	int tabCount = 3;
	int tabW = g_data.DPI(80);
	int tabH = g_data.DPI(28);
	int tabGap = g_data.DPI(6);
	int tabTop = contentRect.top;

	m_group_tab_rects.clear();
	m_group_tab_rects.resize(tabCount);

	Gdiplus::Font tabFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font tabActiveFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

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
			Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 37, 99, 235)); // #2563EB
			g.FillRectangle(&activeBg, rf);

			Gdiplus::SolidBrush txtBrush(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(groupTabs[i], -1, &tabActiveFont, rf, &sf, &txtBrush);
		}
		else
		{
			Gdiplus::SolidBrush unselBg(i == m_hover_group_tab ? Gdiplus::Color(255, 30, 41, 59) : Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen borderPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			Gdiplus::SolidBrush txtBrush(i == m_hover_group_tab ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 148, 163, 184));
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

	Gdiplus::RectF panelRf(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top), static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(g_data.DPI(110)));
	Gdiplus::SolidBrush panelBg(Gdiplus::Color(255, 24, 27, 34));
	g.FillRectangle(&panelBg, panelRf);
	Gdiplus::Pen panelPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
	g.DrawRectangle(&panelPen, panelRf);

	DrawSectionTitle(g, contentRect.left + g_data.DPI(14), contentRect.top + g_data.DPI(12), L"当前均线周期配置");

	int tagLeft = contentRect.left + g_data.DPI(14);
	int tagTop = contentRect.top + g_data.DPI(46);
	int tagH = g_data.DPI(32);
	int tagGap = g_data.DPI(10);

	Gdiplus::Font tagFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(10.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font delFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::Color tagColors[] = {
		Gdiplus::Color(255, 245, 158, 11),  // Amber (MA5) #F59E0B
		Gdiplus::Color(255, 124, 58, 237),  // Violet (MA17) #7C3AED
		Gdiplus::Color(255, 14, 203, 129),  // Emerald (MA60) #0ECB81
		Gdiplus::Color(255, 56, 189, 248),  // Sky Blue #38BDF8
		Gdiplus::Color(255, 246, 70, 93)    // Rose Red #F6465D
	};

	for (size_t i = 0; i < m_data.m_ma_days.size(); ++i)
	{
		int day = m_data.m_ma_days[i];
		std::wstring tagText = L"MA" + std::to_wstring(day);
		int tagW = g_data.DPI(85);

		CRect tagRect(tagLeft, tagTop, tagLeft + tagW, tagTop + tagH);
		m_ma_tag_rects[i] = tagRect;

		Gdiplus::RectF tagRf(static_cast<Gdiplus::REAL>(tagLeft), static_cast<Gdiplus::REAL>(tagTop), static_cast<Gdiplus::REAL>(tagW), static_cast<Gdiplus::REAL>(tagH));
		Gdiplus::SolidBrush tagBg(tagColors[i % 5]);
		g.FillRectangle(&tagBg, tagRf);

		Gdiplus::SolidBrush tagTxtBrush(Gdiplus::Color(255, 255, 255, 255));
		g.DrawString(tagText.c_str(), -1, &tagFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(tagLeft + g_data.DPI(10)), static_cast<Gdiplus::REAL>(tagTop + g_data.DPI(6))), &tagTxtBrush);

		int delBtnX = tagLeft + tagW - g_data.DPI(22);
		int delBtnY = tagTop + g_data.DPI(7);
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

	Gdiplus::Font addPromptFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush promptBrush(Gdiplus::Color(255, 148, 163, 184));
	g.DrawString(L"输入均线天数 (1~250)：", -1, &addPromptFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top + g_data.DPI(135))), &promptBrush);
}

void CManagerDialog::DrawWebDavPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);

	// 卡片 1: WebDAV 服务器参数（高度与 UpdateControlsLayout 的四行输入严格对应）
	int card1Top = contentRect.top;
	int card1H = g_data.DPI(168);
	Gdiplus::RectF card1Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card1Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card1H));
	g.FillRectangle(&cardBg, card1Rf);
	g.DrawRectangle(&cardBorder, card1Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card1Top + g_data.DPI(12), L"WebDAV 服务器参数");

	// 卡片 2: 同步与备份操作（勾选项/操作按钮/提示文字分区块排布，互不重叠）
	int card2Top = card1Top + g_data.DPI(178);
	int card2H = g_data.DPI(152);
	Gdiplus::RectF card2Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card2Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card2H));
	g.FillRectangle(&cardBg, card2Rf);
	g.DrawRectangle(&cardBorder, card2Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card2Top + g_data.DPI(12), L"同步与备份操作");

	Gdiplus::Font tipFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 148, 163, 184));
	int tipY = card2Top + g_data.DPI(132);

	g.DrawString(L"提示：支持坚果云、Nextcloud、Alist、群晖 NAS 等标准 WebDAV 服务。", -1, &tipFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(18)), static_cast<Gdiplus::REAL>(tipY)), &tipBrush);

	if (!m_data.m_webdav_last_sync_time.empty())
	{
		std::wstring timeStr = L"上次同步: " + m_data.m_webdav_last_sync_time;
		Gdiplus::SolidBrush succBrush(Gdiplus::Color(255, 14, 203, 129));
		Gdiplus::RectF bounds;
		g.MeasureString(timeStr.c_str(), -1, &tipFont, Gdiplus::PointF(0, 0), &bounds);
		g.DrawString(timeStr.c_str(), -1, &tipFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + rightWidth - g_data.DPI(14) - bounds.Width), static_cast<Gdiplus::REAL>(tipY)), &succBrush);
	}
}

void CManagerDialog::DrawAboutPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	Gdiplus::RectF panelRf(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top), static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(contentRect.Height()));
	Gdiplus::SolidBrush panelBg(Gdiplus::Color(255, 24, 27, 34));
	g.FillRectangle(&panelBg, panelRf);
	Gdiplus::Pen panelPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
	g.DrawRectangle(&panelPen, panelRf);

	int textX = contentRect.left + g_data.DPI(24);
	int textY = contentRect.top + g_data.DPI(24);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(14)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 241, 245, 249));
	g.DrawString(L"TrafficMonitor 股票行情插件 (Stock Plugin)", -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &nameBrush);

	textY += g_data.DPI(32);
	Gdiplus::Font infoFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush infoBrush(Gdiplus::Color(255, 148, 163, 184));
	g.DrawString(L"版本: v1.15   |   原作者: CListery   |   开发贡献: TrafficMonitor Community", -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &infoBrush);

	textY += g_data.DPI(30);
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
		textY += g_data.DPI(24);
	}

	textY += g_data.DPI(18);
	g.DrawString(L"项目开源主页 (点击访问)：", -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &infoBrush);

	textY += g_data.DPI(22);
	const wchar_t* url = L"https://github.com/zhongyang219/TrafficMonitorPlugins";
	Gdiplus::SolidBrush linkBrush(Gdiplus::Color(255, 56, 189, 248));
	g.DrawString(url, -1, &infoFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &linkBrush);

	m_about_link_rect = CRect(textX, textY, textX + g_data.DPI(340), textY + g_data.DPI(22));
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
	SetCheck(IDC_FULL_DAY_CHECK, !IsChecked(IDC_FULL_DAY_CHECK));
	m_data.m_full_day = IsChecked(IDC_FULL_DAY_CHECK);
}

void CManagerDialog::OnBnClickedShowStockNameCheck()
{
	SetCheck(IDC_SHOW_STOCK_NAME_CHECK, !IsChecked(IDC_SHOW_STOCK_NAME_CHECK));
	m_data.m_show_stock_name = IsChecked(IDC_SHOW_STOCK_NAME_CHECK);
}

void CManagerDialog::OnBnClickedColorWithPriceCheck()
{
	SetCheck(IDC_COLOR_WITH_PRICE_CHECK, !IsChecked(IDC_COLOR_WITH_PRICE_CHECK));
	m_data.m_color_with_price = IsChecked(IDC_COLOR_WITH_PRICE_CHECK);
}

void CManagerDialog::OnBnClickedShowFluctuationCheck()
{
	SetCheck(IDC_SHOW_FLUCTUATION_CHECK, !IsChecked(IDC_SHOW_FLUCTUATION_CHECK));
	m_data.m_show_fluctuation = IsChecked(IDC_SHOW_FLUCTUATION_CHECK);
}

void CManagerDialog::OnBnClickedUseSocks5ProxyCheck()
{
	SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, !IsChecked(IDC_USE_SOCKS5_PROXY_CHECK));
	m_data.m_use_socks5_proxy = IsChecked(IDC_USE_SOCKS5_PROXY_CHECK);
}

void CManagerDialog::OnBnClickedWebDavAutoSyncCheck()
{
	SetCheck(IDC_WEBDAV_AUTO_SYNC_CHECK, !IsChecked(IDC_WEBDAV_AUTO_SYNC_CHECK));
	m_data.m_webdav_auto_sync = IsChecked(IDC_WEBDAV_AUTO_SYNC_CHECK);
}

void CManagerDialog::OnBnClickedWebDavAutoBackupCheck()
{
	SetCheck(IDC_WEBDAV_AUTO_BACKUP_CHECK, !IsChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK));
	m_data.m_webdav_auto_backup = IsChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK);
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

		SetCheck(IDC_FULL_DAY_CHECK, m_data.m_full_day);
		SetCheck(IDC_SHOW_STOCK_NAME_CHECK, m_data.m_show_stock_name);
		SetCheck(IDC_COLOR_WITH_PRICE_CHECK, m_data.m_color_with_price);
		SetCheck(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation);
		SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy);
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
		SetCheck(IDC_WEBDAV_AUTO_SYNC_CHECK, m_data.m_webdav_auto_sync);
		SetCheck(IDC_WEBDAV_AUTO_BACKUP_CHECK, m_data.m_webdav_auto_backup);

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
	m_data.m_webdav_auto_sync = IsChecked(IDC_WEBDAV_AUTO_SYNC_CHECK);
	m_data.m_webdav_auto_backup = IsChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK);

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

// ===================== 暗色主题自绘辅助 =====================

BEGIN_MESSAGE_MAP(CFlatHeaderCtrl, CHeaderCtrl)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CFlatHeaderCtrl::OnCustomDraw)
END_MESSAGE_MAP()

// 平面化表头：深色底 + 细分隔线 + 灰色文字，与浮动窗表面体系一致
void CFlatHeaderCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMCUSTOMDRAW* pCD = reinterpret_cast<NMCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;

	if (pCD->dwDrawStage == CDDS_PREPAINT)
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
	}
	else if (pCD->dwDrawStage == CDDS_ITEMPREPAINT)
	{
		CDC dc;
		dc.Attach(pCD->hdc);
		CRect rc(pCD->rc);

		dc.FillSolidRect(rc, RGB(28, 31, 39));
		dc.FillSolidRect(CRect(rc.left, rc.bottom - 1, rc.right, rc.bottom), COLOR_DARK_GRAY_BORDER);
		dc.FillSolidRect(CRect(rc.right - 1, rc.top + 4, rc.right, rc.bottom - 4), COLOR_DARK_GRAY_BORDER);

		wchar_t buf[128] = { 0 };
		HDITEM it = { 0 };
		it.mask = HDI_TEXT;
		it.pszText = buf;
		it.cchTextMax = 128;
		if (GetItem(static_cast<int>(pCD->dwItemSpec), &it))
		{
			CFont* pFont = GetFont();
			if (pFont == nullptr || pFont->GetSafeHandle() == nullptr)
				pFont = CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT));
			CFont* pOldFont = dc.SelectObject(pFont);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(COLOR_TEXT_MUTED);
			CRect textRc = rc;
			textRc.DeflateRect(g_data.DPI(8), 0);
			CString headerText(buf);
			dc.DrawText(headerText, textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			dc.SelectObject(pOldFont);
		}

		dc.Detach();
		*pResult = CDRF_SKIPDEFAULT;
	}
}

bool CManagerDialog::IsChecked(UINT nID) const
{
	auto it = m_checkStates.find(nID);
	return it != m_checkStates.end() && it->second;
}

void CManagerDialog::SetCheck(UINT nID, bool checked)
{
	m_checkStates[nID] = checked;
	CWnd* pWnd = GetDlgItem(nID);
	if (pWnd && pWnd->GetSafeHwnd())
		pWnd->InvalidateRect(nullptr);
}

bool CManagerDialog::IsCheckCtrl(UINT nID) const
{
	switch (nID)
	{
	case IDC_FULL_DAY_CHECK:
	case IDC_SHOW_STOCK_NAME_CHECK:
	case IDC_COLOR_WITH_PRICE_CHECK:
	case IDC_SHOW_FLUCTUATION_CHECK:
	case IDC_USE_SOCKS5_PROXY_CHECK:
	case IDC_WEBDAV_AUTO_SYNC_CHECK:
	case IDC_WEBDAV_AUTO_BACKUP_CHECK:
		return true;
	default:
		return false;
	}
}

bool CManagerDialog::IsPrimaryBtn(UINT nID) const
{
	return nID == IDOK || nID == IDC_MA_ADD_BTN;
}

bool CManagerDialog::IsDestructiveBtn(UINT nID) const
{
	return nID == IDC_MGR_DEL_BTN;
}

// 与浮动窗按钮同款：直角 + 1px 细边框 + 悬停/按下反馈；主操作品牌蓝，删除操作警示红
void CManagerDialog::DrawFlatButton(CDC& dc, const CRect& r, const CString& text, bool primary, bool destructive, bool hot, bool pressed)
{
	COLORREF bgCol, borderCol, textCol;
	if (primary)
	{
		bgCol = COLOR_ACCENT_BLUE;
		borderCol = COLOR_ACCENT_BLUE;
		textCol = RGB(255, 255, 255);
		if (hot) bgCol = RGB(59, 130, 246);
	}
	else if (destructive)
	{
		bgCol = RGB(24, 27, 34);
		borderCol = COLOR_DARK_GRAY_BORDER;
		textCol = COLOR_RED_UP;
		if (hot)
		{
			bgCol = RGB(48, 25, 33);
			borderCol = RGB(88, 42, 55);
		}
	}
	else
	{
		bgCol = RGB(24, 27, 34);
		borderCol = COLOR_DARK_GRAY_BORDER;
		textCol = COLOR_TEXT_PRIMARY;
		if (hot)
		{
			bgCol = RGB(30, 41, 59);
			borderCol = RGB(56, 62, 78);
		}
	}

	if (pressed)
	{
		bgCol = RGB(max(0, GetRValue(bgCol) - 20), max(0, GetGValue(bgCol) - 20), max(0, GetBValue(bgCol) - 20));
	}

	dc.FillSolidRect(r, bgCol);
	dc.Draw3dRect(r, borderCol, borderCol);

	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(textCol);
	CFont* pOldFont = dc.SelectObject(primary ? &m_font_bold : &m_font);
	CRect textRect(r); // DrawText 需要 LPRECT，传入可写副本
	dc.DrawText(text, textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc.SelectObject(pOldFont);
}

// 输入框/列表的自绘边框：聚焦品牌蓝，失焦暗灰；仅绘制当前可见控件
void CManagerDialog::DrawControlBorder(Gdiplus::Graphics& g, UINT nID)
{
	CWnd* pWnd = GetDlgItem(nID);
	if (!pWnd || !pWnd->GetSafeHwnd() || !pWnd->IsWindowVisible())
		return;

	bool isList = (nID == IDC_MGR_LIST || nID == IDC_POS_LIST || nID == IDC_CUSTOM_LIST);
	bool focused = false;
	if (!isList)
	{
		CWnd* pFocus = GetFocus();
		focused = (pFocus && pFocus->GetSafeHwnd() == pWnd->GetSafeHwnd());
	}

	CRect rc;
	pWnd->GetWindowRect(&rc);
	ScreenToClient(&rc);
	rc.InflateRect(1, 1);

	Gdiplus::Pen pen(focused ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 52, 58, 72), 1.0f);
	g.DrawRectangle(&pen, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
		static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
}

// 章节标题：品牌蓝竖条 + 白色加粗文字（页头与卡片统一视觉语言）
void CManagerDialog::DrawSectionTitle(Gdiplus::Graphics& g, int x, int y, const std::wstring& title)
{
	Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 37, 99, 235));
	g.FillRectangle(&barBrush, static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + g_data.DPI(2)),
		static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(g_data.DPI(12)));

	Gdiplus::Font titleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 241, 245, 249));
	g.DrawString(title.c_str(), -1, &titleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y - g_data.DPI(1))), &titleBrush);
}

// 列表行自绘：交替行底色 + 选中项深蓝高亮（与浮动窗选中色一致）
void CManagerDialog::OnListCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVCUSTOMDRAW* pLV = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;

	switch (pLV->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;
	case CDDS_ITEMPREPAINT:
	{
		int row = static_cast<int>(pLV->nmcd.dwItemSpec);
		bool selected = (pLV->nmcd.uItemState & CDIS_SELECTED) != 0;
		if (selected)
		{
			pLV->clrTextBk = COLOR_CARD_SELECTED;
			pLV->clrText = RGB(255, 255, 255);
		}
		else
		{
			pLV->clrTextBk = (row % 2) ? RGB(22, 25, 32) : RGB(20, 22, 29);
			pLV->clrText = COLOR_TEXT_PRIMARY;
		}
		*pResult = CDRF_NOTIFYSUBITEMDRAW;
		break;
	}
	default:
		break;
	}
}

// 输入框焦点变化 → 重绘自绘边框
void CManagerDialog::OnEditFocusChanged()
{
	Invalidate(FALSE);
}
