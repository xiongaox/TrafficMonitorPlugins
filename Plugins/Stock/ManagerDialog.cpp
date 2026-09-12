// ManagerDialog.cpp: 实现文件
//

#include "pch.h"
#include "Stock.h"
#include "Version.h"
#include "afxdialogex.h"
#include "ManagerDialog.h"
#include "FloatingWnd.h"
#include "Common.h"
#include "StockFetchThread.h"
#include "OptionsDlg.h"
#include "WebDavSync.h"
#include "ApiHealthManager.h"
#include "ChartColors.h"
#include "Icons/Icons.h"
#include <Windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <set>
#include <shellapi.h>
#include <ctime>
#include <uxtheme.h>
#include <dwmapi.h>
#include <fstream>
#include <thread>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

// ===== 均线日配置页布局常量（单位: DPI 逻辑像素）=====
// DrawMaPage（GDI+ 绘制卡片）与 UpdateControlsLayout（MoveWindow 摆控件）共用，
// 卡片高度/字段位置改动必须以这里的常量为准，两处天然保持同步。
namespace
{
	// ===== 均线日配置页布局常量（紧凑版：内嵌 480 高窗口一屏容纳，DrawMaPage 与
	// UpdateControlsLayout 严格共用，改动须两处同步） =====
	const int MA_CARD1_H = 86;   // 卡片1「当前均线周期」高度（标题 + 单行周期标签）
	const int MA_CARD_GAP = 10;  // 卡片间距
	const int MA_CARD2_H = 80;   // 卡片2「添加均线周期」高度（标题 + 输入行）
	const int MA_CARD3_H = 78;   // 卡片3「快捷添加常用周期」高度（标题 + 单行预设按钮）
	const int MA_CARD4_H = 74;   // 卡片4「分时图布林带显示」高度（标题 + 单行三复选框）
	const int MA_FIELD_Y = 40;   // 卡片2 输入框字段上缘（相对卡片）
	const int MA_FIELD_X = 150;  // 卡片2 输入框字段左缘（相对卡片）
	const int MA_FIELD_W = 120;  // 卡片2 输入框字段宽
	const int MA_FIELD_H = 28;   // 卡片2 输入框/按钮高
	const int MA_ADDBTN_W = 84;  // 卡片2「添加周期」按钮宽
	const int MA_PRESET_MAX = 5; // 均线周期上限
	const int kMaPresetDays[] = { 5, 10, 20, 30, 60, 120, 250 }; // 快捷添加候选周期
}

// ===== 云端备份异步操作 =====
// WebDAV 网络请求必须走取数线程：插件模块状态下宿主主线程没有 CWinThread，
// MFC 等待光标/网络层在此线程会空指针崩溃（实测 mfc140u.dll 访问违例闪退）。
// 操作完成后经 WM_APP_WEBDAV_RESULT 回到 UI 线程弹结果与刷新界面。
namespace
{
	enum WebDavOp
	{
		WEBDAV_OP_TEST = 0,
		WEBDAV_OP_UPLOAD = 1,
		WEBDAV_OP_DOWNLOAD = 2,
		WEBDAV_OP_LIST = 3,    // PROPFIND 拉取云端历史备份列表
		WEBDAV_OP_RESTORE = 4  // 下载用户选中的某一份历史备份
	};
	const UINT WM_APP_WEBDAV_RESULT = WM_APP + 130;
	const UINT WM_APP_API_PROBE_FINISHED = WM_APP + 131;
	const UINT WM_APP_SEARCH_RESULT_READY = WM_APP + 132;
	const UINT IDC_API_TEST_BTN = 1197;

	struct WebDavAsyncResult
	{
		int op{ WEBDAV_OP_TEST };
		bool ok{ false };
		std::wstring errMsg;
		std::string downloadedData;                 // 仅 WEBDAV_OP_RESTORE 使用
		std::vector<WebDavBackupEntry> backups;     // 仅 WEBDAV_OP_LIST 使用
		std::wstring remoteFile;                    // 仅 WEBDAV_OP_RESTORE：要下载的备份文件名
	};
}

// 简易深色输入弹窗（用于新建/重命名分组）
class CSimpleInputDialog : public CDialog
{
public:
	CString m_title;
	CString m_prompt;
	CString m_value;
	CEdit m_edit;
	CStatic m_label;
	CButton m_btnOk;
	CButton m_btnCancel;
	CFont m_font;
	CBrush m_dark_brush;
	CBrush m_edit_brush;

	CSimpleInputDialog(const CString& title, const CString& prompt, const CString& defVal = _T(""), CWnd* pParent = nullptr)
		: CDialog(), m_title(title), m_prompt(prompt), m_value(defVal)
	{
	}

	INT_PTR DoModal(CWnd* pParent = nullptr)
	{
		BYTE buffer[512] = { 0 };
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buffer;
		pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
		pDlg->dwExtendedStyle = 0;
		pDlg->cdit = 0;
		pDlg->x = 0;
		pDlg->y = 0;
		pDlg->cx = 220;
		pDlg->cy = 75;

		InitModalIndirect(pDlg, pParent);
		return CDialog::DoModal();
	}

	virtual BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(m_title);

		BOOL darkCaption = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption, sizeof(darkCaption));

		m_font.CreatePointFont(90, _T("微软雅黑"));
		SetFont(&m_font);

		m_dark_brush.CreateSolidBrush(RGB(24, 27, 34));
		m_edit_brush.CreateSolidBrush(RGB(13, 15, 21));

		CRect cr;
		GetClientRect(&cr);

		m_label.Create(m_prompt, WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(g_data.DPI(18), g_data.DPI(12), cr.right - g_data.DPI(18), g_data.DPI(30)), this);
		m_label.SetFont(&m_font);

		m_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(g_data.DPI(18) + g_data.DPI(6), g_data.DPI(32) + g_data.DPI(3), cr.right - g_data.DPI(18) - g_data.DPI(6), g_data.DPI(32) + g_data.DPI(21)), this, 1001);
		m_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		::SetWindowTheme(m_edit.GetSafeHwnd(), L"", L"");
		m_edit.SetFont(&m_font);
		m_edit.SetWindowText(m_value);
		m_edit.SetFocus();
		m_edit.SetSel(0, -1);

		int btnW = g_data.DPI(62);
		int btnH = g_data.DPI(24);
		int btnY = cr.bottom - btnH - g_data.DPI(10);

		m_btnOk.Create(_T("确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW, CRect(cr.right - btnW * 2 - g_data.DPI(18), btnY, cr.right - btnW - g_data.DPI(18), btnY + btnH), this, IDOK);
		m_btnOk.SetFont(&m_font);

		m_btnCancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(cr.right - btnW - g_data.DPI(10), btnY, cr.right - g_data.DPI(10), btnY + btnH), this, IDCANCEL);
		m_btnCancel.SetFont(&m_font);

		return FALSE;
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_PAINT)
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

			Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&bgBrush, 0, 0, clientRect.Width(), clientRect.Height());

			if (m_edit.GetSafeHwnd())
			{
				CRect editRc(g_data.DPI(18), g_data.DPI(32), clientRect.right - g_data.DPI(18), g_data.DPI(56));
				
				Gdiplus::SolidBrush editBg(Gdiplus::Color(255, 13, 15, 21));
				g.FillRectangle(&editBg, editRc.left, editRc.top, editRc.Width(), editRc.Height());
				
				CWnd* pFocus = GetFocus();
				bool focused = (pFocus && pFocus->GetSafeHwnd() == m_edit.GetSafeHwnd());
				Gdiplus::Pen pen(focused ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 52, 58, 72), 1.0f);
				g.DrawRectangle(&pen, editRc.left, editRc.top, editRc.Width() - 1, editRc.Height() - 1);
			}

			dc.BitBlt(0, 0, clientRect.Width(), clientRect.Height(), &memDC, 0, 0, SRCCOPY);
			memDC.SelectObject(pOldBmp);
			return 0;
		}
		else if (message == WM_ERASEBKGND)
		{
			return TRUE;
		}
		else if (message == WM_CTLCOLOREDIT)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(255, 255, 255));
			::SetBkColor(hdc, RGB(13, 15, 21));
			return (LRESULT)(HBRUSH)m_edit_brush.GetSafeHandle();
		}
		else if (message == WM_COMMAND)
		{
			WORD wNotifyCode = HIWORD(wParam);
			if (wNotifyCode == EN_SETFOCUS || wNotifyCode == EN_KILLFOCUS)
			{
				InvalidateRect(nullptr, FALSE);
			}
		}
		else if (message == WM_CTLCOLORSTATIC)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(226, 232, 240));
			::SetBkColor(hdc, RGB(24, 27, 34));
			return (LRESULT)(HBRUSH)m_dark_brush.GetSafeHandle();
		}
		else if (message == WM_DRAWITEM)
		{
			LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
			if (pDI->CtlType == ODT_BUTTON)
			{
				CDC dc;
				dc.Attach(pDI->hDC);
				CRect rect = pDI->rcItem;
				UINT state = pDI->itemState;
				CString text;
				if (pDI->CtlID == IDOK) text = _T("确定");
				else text = _T("取消");

				COLORREF bgColor = (pDI->CtlID == IDOK) ? RGB(37, 99, 235) : RGB(30, 35, 46);
				if (state & ODS_SELECTED) bgColor = (pDI->CtlID == IDOK) ? RGB(29, 78, 216) : RGB(20, 25, 35);
				
				dc.FillSolidRect(rect, bgColor);
				
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(255, 255, 255));
				CFont* pOldFont = dc.SelectObject(&m_font);
				dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(pOldFont);
				dc.Detach();
				return TRUE;
			}
		}
		return CDialog::WindowProc(message, wParam, lParam);
	}

	virtual void OnOK() override
	{
		if (m_edit.GetSafeHwnd())
		{
			m_edit.GetWindowText(m_value);
			m_value.Trim();
		}
		CDialog::OnOK();
	}
};

// ===== 自定义分组排序对话框（拖动行或上下移按钮调整顺序；自选股/持仓固定不参与） =====
class CGroupSortDlg : public CDialog
{
public:
	std::vector<CustomGroup> m_groups;   // 传入并输出排序结果
	CGroupSortDlg(const std::vector<CustomGroup>& groups, CWnd* pParent = nullptr)
		: CDialog(), m_groups(groups), m_sel(-1), m_dragging(false)
	{
		m_dark_brush.CreateSolidBrush(RGB(24, 27, 34));   // #181B22
	}

	INT_PTR DoModal(CWnd* pParent = nullptr)
	{
		BYTE buffer[512] = { 0 };
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buffer;
		pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
		pDlg->dwExtendedStyle = 0;
		pDlg->cdit = 0;
		pDlg->x = 0;
		pDlg->y = 0;
		pDlg->cx = 220;
		pDlg->cy = 210;
		InitModalIndirect(pDlg, pParent);
		return CDialog::DoModal();
	}

protected:
	CFont m_font;
	CBrush m_dark_brush;
	CButton m_btn_move_up;
	CButton m_btn_move_down;
	int m_sel;            // 当前选中行
	bool m_dragging;      // 拖动中

	CRect ListRect() const
	{
		CRect cr;
		const_cast<CGroupSortDlg*>(this)->GetClientRect(&cr);
		return CRect(g_data.DPI(18), g_data.DPI(46), cr.right - g_data.DPI(18), cr.bottom - g_data.DPI(56));
	}

	int RowHeight() const
	{
		CRect lr = ListRect();
		int n = static_cast<int>(m_groups.size());
		if (n <= 0) return g_data.DPI(34);
		return min(g_data.DPI(38), lr.Height() / n);
	}

	void MoveGroup(int from, int to)
	{
		int n = static_cast<int>(m_groups.size());
		if (from < 0 || from >= n || to < 0 || to >= n || from == to)
			return;
		CustomGroup g = m_groups[from];
		m_groups.erase(m_groups.begin() + from);
		m_groups.insert(m_groups.begin() + to, g);
		m_sel = to;
		InvalidateRect(nullptr, FALSE);
	}

	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(L"调整分组排序");

		BOOL darkCaption = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption, sizeof(darkCaption));

		m_font.CreatePointFont(90, _T("微软雅黑"));
		SetFont(&m_font);

		// 按分组数量自适应高度并重新居中
		CRect cr;
		GetClientRect(&cr);
		int n = max(1, static_cast<int>(m_groups.size()));
		int rowH = min(g_data.DPI(38), max(g_data.DPI(30), g_data.DPI(320) / n));
		int needH = g_data.DPI(46) + n * rowH + g_data.DPI(64);
		CRect wr;
		GetWindowRect(&wr);
		int addH = needH - cr.Height();
		::SetWindowPos(GetSafeHwnd(), nullptr, wr.left, max(10, wr.top - addH / 2), wr.Width(), cr.Height() + addH, SWP_NOZORDER | SWP_NOACTIVATE);
		GetClientRect(&cr);

		int btnW = g_data.DPI(60);
		int btnH = g_data.DPI(26);
		int btnY = cr.bottom - btnH - g_data.DPI(12);
		m_btn_move_up.Create(_T("上移"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(g_data.DPI(18), btnY, g_data.DPI(18) + btnW, btnY + btnH), this, 1101);
		m_btn_move_up.SetFont(&m_font);
		m_btn_move_down.Create(_T("下移"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(g_data.DPI(18) + btnW + g_data.DPI(8), btnY, g_data.DPI(18) + btnW * 2 + g_data.DPI(8), btnY + btnH), this, 1102);
		m_btn_move_down.SetFont(&m_font);

		m_btn_ok.Create(_T("确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW, CRect(cr.right - btnW * 2 - g_data.DPI(18), btnY, cr.right - btnW - g_data.DPI(18), btnY + btnH), this, IDOK);
		m_btn_ok.SetFont(&m_font);
		m_btn_cancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(cr.right - btnW - g_data.DPI(18), btnY, cr.right - g_data.DPI(18), btnY + btnH), this, IDCANCEL);
		m_btn_cancel.SetFont(&m_font);

		m_sel = m_groups.empty() ? -1 : 0;
		return FALSE;
	}

	LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_PAINT)
		{
			CPaintDC dc(this);
			CRect cr;
			GetClientRect(&cr);

			CDC memDC;
			memDC.CreateCompatibleDC(&dc);
			CBitmap memBmp;
			memBmp.CreateCompatibleBitmap(&dc, cr.Width(), cr.Height());
			CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

			Gdiplus::Graphics g(memDC.GetSafeHdc());
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
			Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&bgBrush, 0, 0, cr.Width(), cr.Height());

			// 顶部提示
			Gdiplus::Font tipFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 148, 163, 184));
			g.DrawString(L"拖动行或使用按钮调整自定义分组顺序（自选股/持仓固定）", -1, &tipFont,
				Gdiplus::PointF(static_cast<Gdiplus::REAL>(g_data.DPI(18)), static_cast<Gdiplus::REAL>(g_data.DPI(14))), &tipBrush);

			// 分组行（文字用 GDI DrawText(DT_VCENTER) 垂直居中，与 DrawMaPage 同一居中路径）
			CRect lr = ListRect();
			int rowH = RowHeight();

			// GDI 文字工具
			auto drawGdiText = [&g](const CRect& rc, const CString& text, CFont& font, COLORREF col, UINT fmt) {
				HDC hdc = g.GetHDC();
				CDC* pDC = CDC::FromHandle(hdc);
				int oldBk = pDC->SetBkMode(TRANSPARENT);
				COLORREF oldCol = pDC->SetTextColor(col);
				CFont* pOld = pDC->SelectObject(&font);
				CRect r(rc);
				pDC->DrawText(text, r, fmt | DT_SINGLELINE | DT_NOPREFIX);
				pDC->SelectObject(pOld);
				pDC->SetTextColor(oldCol);
				pDC->SetBkMode(oldBk);
				g.ReleaseHDC(hdc);
			};
			CFont rowFont;
			rowFont.CreateFont(-g_data.DPI(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

			for (size_t i = 0; i < m_groups.size(); ++i)
			{
				int y = lr.top + static_cast<int>(i) * rowH;
				CRect rowRc(lr.left, y + g_data.DPI(3), lr.right, y + rowH - g_data.DPI(3));
				bool selected = (static_cast<int>(i) == m_sel);

				Gdiplus::SolidBrush rowBg(selected ? Gdiplus::Color(255, 28, 45, 75) : Gdiplus::Color(255, 20, 22, 29));
				g.FillRectangle(&rowBg, rowRc.left, rowRc.top, rowRc.Width(), rowRc.Height());
				Gdiplus::Pen rowPen(selected ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 38, 42, 54), selected ? 1.2f : 1.0f);
				g.DrawRectangle(&rowPen, rowRc.left, rowRc.top, rowRc.Width(), rowRc.Height());

				// 序号 + 名称（含股票数），垂直居中
				wchar_t num[8];
				swprintf_s(num, L"%zu", i + 1);
				COLORREF numCol = RGB(100, 116, 139);
				CRect numRc(rowRc.left + g_data.DPI(10), rowRc.top, rowRc.left + g_data.DPI(34), rowRc.bottom);
				drawGdiText(numRc, num, rowFont, numCol, DT_LEFT | DT_VCENTER);

				COLORREF nameCol = selected ? RGB(255, 255, 255) : RGB(226, 232, 240);
				std::wstring text = m_groups[i].name + L"（" + std::to_wstring(m_groups[i].codes.size()) + L"）";
				CRect nameRc(rowRc.left + g_data.DPI(40), rowRc.top, rowRc.right - g_data.DPI(10), rowRc.bottom);
				drawGdiText(nameRc, text.c_str(), rowFont, nameCol, DT_LEFT | DT_VCENTER);
			}

			dc.BitBlt(0, 0, cr.Width(), cr.Height(), &memDC, 0, 0, SRCCOPY);
			memDC.SelectObject(pOldBmp);
			return 0;
		}
		else if (message == WM_ERASEBKGND)
		{
			return TRUE;
		}
		else if (message == WM_LBUTTONDOWN)
		{
			CPoint pt(lParam);
			CRect lr = ListRect();
			int rowH = RowHeight();
			if (pt.x >= lr.left && pt.x <= lr.right && pt.y >= lr.top && pt.y < lr.bottom && !m_groups.empty())
			{
				int idx = (pt.y - lr.top) / rowH;
				if (idx >= 0 && idx < static_cast<int>(m_groups.size()))
				{
					m_sel = idx;
					m_dragging = true;
					SetCapture();
					InvalidateRect(nullptr, FALSE);
				}
			}
			return 0;
		}
		else if (message == WM_MOUSEMOVE)
		{
			if (m_dragging && (wParam & MK_LBUTTON))
			{
				CPoint pt(lParam);
				CRect lr = ListRect();
				int rowH = RowHeight();
				int idx = (pt.y - lr.top) / rowH;
				idx = max(0, min(idx, static_cast<int>(m_groups.size()) - 1));
				if (idx != m_sel)
					MoveGroup(m_sel, idx);
			}
			return 0;
		}
		else if (message == WM_LBUTTONUP)
		{
			if (m_dragging)
			{
				m_dragging = false;
				if (GetCapture() == this)
					ReleaseCapture();
			}
			return 0;
		}
		else if (message == WM_COMMAND)
		{
			WORD id = LOWORD(wParam);
			if (HIWORD(wParam) == BN_CLICKED)
			{
				if (id == 1101)
				{
					MoveGroup(m_sel, m_sel - 1);
					return 0;
				}
				if (id == 1102)
				{
					MoveGroup(m_sel, m_sel + 1);
					return 0;
				}
			}
		}
		else if (message == WM_CTLCOLORSTATIC)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(226, 232, 240));
			::SetBkColor(hdc, RGB(24, 27, 34));
			return (LRESULT)(HBRUSH)m_dark_brush.GetSafeHandle();
		}
		else if (message == WM_DRAWITEM)
		{
			LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
			if (pDI->CtlType == ODT_BUTTON)
			{
				CDC dc;
				dc.Attach(pDI->hDC);
				CRect rect = pDI->rcItem;
				CString text;
				if (pDI->CtlID == 1101) text = _T("上移");
				else if (pDI->CtlID == 1102) text = _T("下移");
				else if (pDI->CtlID == IDOK) text = _T("确定");
				else text = _T("取消");

				// 上移/下移超出可用范围时灰显
				bool disabled = (pDI->CtlID == 1101 && m_sel <= 0) ||
					(pDI->CtlID == 1102 && (m_sel < 0 || m_sel >= static_cast<int>(m_groups.size()) - 1));

				COLORREF bgColor = (pDI->CtlID == IDOK) ? RGB(37, 99, 235) : RGB(30, 35, 46);
				if (pDI->itemState & ODS_SELECTED) bgColor = (pDI->CtlID == IDOK) ? RGB(29, 78, 216) : RGB(20, 25, 35);

				dc.FillSolidRect(rect, bgColor);
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(disabled ? RGB(87, 96, 116) : RGB(255, 255, 255));
				CFont* pOldFont = dc.SelectObject(&m_font);
				dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(pOldFont);
				dc.Detach();
				return TRUE;
			}
		}
		return CDialog::WindowProc(message, wParam, lParam);
	}

private:
	CButton m_btn_ok;
	CButton m_btn_cancel;
};

// 暗色主题通用确认弹窗 (替代原生 MessageBox)
class CDarkConfirmDialog : public CDialog
{
public:
	CString m_title;
	CString m_prompt;
	bool m_is_destructive{ true };
	CFont m_font;
	CFont m_font_bold;
	CBrush m_dark_brush;
	CStatic m_label;
	CButton m_btnOk;
	CButton m_btnCancel;

	CDarkConfirmDialog(const CString& title, const CString& prompt, CWnd* pParent = nullptr, bool isDestructive = true)
		: CDialog(), m_title(title), m_prompt(prompt), m_is_destructive(isDestructive)
	{
		m_dark_brush.CreateSolidBrush(RGB(24, 27, 34)); // #181B22
	}

	INT_PTR DoModal(CWnd* pParent = nullptr)
	{
		BYTE buffer[512] = { 0 };
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buffer;
		pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
		pDlg->dwExtendedStyle = 0;
		pDlg->cdit = 0;
		pDlg->x = 0;
		pDlg->y = 0;
		pDlg->cx = 180;
		pDlg->cy = 75;

		InitModalIndirect(pDlg, pParent);
		return CDialog::DoModal();
	}

	virtual BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(m_title);

		BOOL darkCaption = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption, sizeof(darkCaption));

		m_font.CreatePointFont(100, _T("微软雅黑"));
		m_font_bold.CreatePointFont(100, _T("微软雅黑"));
		SetFont(&m_font);

		CRect cr;
		GetClientRect(&cr);

		int marginX = g_data.DPI(18);
		m_label.Create(m_prompt, WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(marginX, g_data.DPI(16), cr.right - marginX, g_data.DPI(40)), this);
		m_label.SetFont(&m_font);

		int btnW = g_data.DPI(62);
		int btnH = g_data.DPI(26);
		int btnY = cr.bottom - btnH - g_data.DPI(12);

		m_btnOk.Create(_T("确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW, CRect(cr.right - btnW * 2 - g_data.DPI(18), btnY, cr.right - btnW - g_data.DPI(18), btnY + btnH), this, IDOK);
		m_btnOk.SetFont(&m_font_bold);

		m_btnCancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(cr.right - btnW - g_data.DPI(10), btnY, cr.right - g_data.DPI(10), btnY + btnH), this, IDCANCEL);
		m_btnCancel.SetFont(&m_font);

		return TRUE;
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_PAINT)
		{
			CPaintDC dc(this);
			CRect clientRect;
			GetClientRect(clientRect);

			Gdiplus::Graphics g(dc.GetSafeHdc());
			Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&bgBrush, 0, 0, clientRect.Width(), clientRect.Height());
			return 0;
		}
		else if (message == WM_ERASEBKGND)
		{
			return TRUE;
		}
		else if (message == WM_CTLCOLORSTATIC)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(226, 232, 240));
			::SetBkColor(hdc, RGB(24, 27, 34));
			return (LRESULT)(HBRUSH)m_dark_brush.GetSafeHandle();
		}
		else if (message == WM_DRAWITEM)
		{
			LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
			if (pDI->CtlType == ODT_BUTTON)
			{
				CDC dc;
				dc.Attach(pDI->hDC);
				CRect rect = pDI->rcItem;
				UINT state = pDI->itemState;
				CString text = (pDI->CtlID == IDOK) ? _T("确定") : _T("取消");

				bool isOk = (pDI->CtlID == IDOK);
				COLORREF bgColor;
				if (isOk)
				{
					if (m_is_destructive)
						bgColor = (state & ODS_SELECTED) ? RGB(185, 28, 28) : RGB(220, 38, 38);
					else
						bgColor = (state & ODS_SELECTED) ? RGB(29, 78, 216) : RGB(37, 99, 235);
				}
				else
				{
					bgColor = (state & ODS_SELECTED) ? RGB(20, 25, 35) : RGB(30, 35, 46);
				}

				dc.FillSolidRect(rect, bgColor);

				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(255, 255, 255));
				CFont* pOldFont = dc.SelectObject(isOk ? &m_font_bold : &m_font);
				dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(pOldFont);
				dc.Detach();
				return TRUE;
			}
		}
		return CDialog::WindowProc(message, wParam, lParam);
	}
};

// 暗色主题云端备份选择弹窗：列出云端历史备份，选中一份后返回 IDOK
class CBackupListDialog : public CDialog
{
public:
	std::vector<WebDavBackupEntry> m_entries;
	std::wstring m_selectedFile; // EndDialog(IDOK) 时有效：选中的远端文件名
	std::wstring m_selectedName; // 选中的展示文本（备份时间）
	CFont m_font;
	CBrush m_dark_brush;
	CBrush m_list_brush;
	CStatic m_label;
	CListCtrl m_list;
	CFlatHeaderCtrl m_hdr; // 复用主界面的自绘扁平深色表头
	CButton m_btnOk;
	CButton m_btnCancel;

	enum { IDC_BACKUP_LIST = 2100 };

	CBackupListDialog(const std::vector<WebDavBackupEntry>& entries, CWnd* pParent = nullptr)
		: CDialog(), m_entries(entries)
	{
		m_dark_brush.CreateSolidBrush(RGB(24, 27, 34));
		m_list_brush.CreateSolidBrush(RGB(13, 15, 21));
	}

	INT_PTR DoModal(CWnd* pParent = nullptr)
	{
		BYTE buffer[512] = { 0 };
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buffer;
		// 必须带 WS_VISIBLE：宿主取数线程持续投递行情消息时模态循环长时间无空闲，
		// 依赖 MLF_SHOWONIDLE 延迟显示会导致对话框一直不可见地模态挂着
		pDlg->style = WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
		pDlg->dwExtendedStyle = 0;
		pDlg->cdit = 0;
		pDlg->x = 0;
		pDlg->y = 0;
		pDlg->cx = 260;
		pDlg->cy = 160;

		InitModalIndirect(pDlg, pParent);
		return CDialog::DoModal();
	}

	virtual BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(L"选择要恢复的云端备份");

		BOOL darkCaption = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption, sizeof(darkCaption));

		m_font.CreatePointFont(90, _T("微软雅黑"));
		SetFont(&m_font);

		// 模板为固定 DLU 尺寸，高 DPI 下按缩放重设窗口，保证子控件布局充足
		CRect rw, rc;
		GetWindowRect(&rw);
		GetClientRect(&rc);
		int frameW = rw.Width() - rc.Width();
		int frameH = rw.Height() - rc.Height();
		SetWindowPos(nullptr, 0, 0, g_data.DPI(340) + frameW, g_data.DPI(230) + frameH, SWP_NOMOVE | SWP_NOZORDER);

		GetClientRect(&rc);
		int marginX = g_data.DPI(16);

		wchar_t tip[96]{};
		swprintf_s(tip, L"云端共有 %d 份备份，请选择要恢复到本地的一份：", static_cast<int>(m_entries.size()));
		m_label.Create(tip, WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(marginX, g_data.DPI(12), rc.right - marginX, g_data.DPI(28)), this);
		m_label.SetFont(&m_font);

		CRect listRect(marginX, g_data.DPI(34), rc.right - marginX, rc.bottom - g_data.DPI(46));
		m_list.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
			listRect, this, IDC_BACKUP_LIST);
		m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
		m_list.SetFont(&m_font);
		m_list.SetBkColor(RGB(13, 15, 21));
		m_list.SetTextBkColor(RGB(13, 15, 21));
		m_list.SetTextColor(RGB(226, 232, 240));
		SetWindowTheme(m_list.GetSafeHwnd(), L"DarkMode_Explorer", nullptr);
		m_list.ModifyStyle(WS_BORDER, 0);
		m_list.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		// 表头换为主界面同款自绘扁平深色样式，替代系统白色表头
		HWND hHeader = m_list.GetHeaderCtrl() ? m_list.GetHeaderCtrl()->GetSafeHwnd() : nullptr;
		if (hHeader && m_hdr.GetSafeHwnd() == nullptr)
			m_hdr.SubclassWindow(hHeader);

		// 列宽自适应：先窄占位避免灌条目时挤出横向滚动条，
		// 条目灌完出现纵向滚动条后，再按实际客户区（已扣滚动条）定宽
		int sizeW = g_data.DPI(95);
		m_list.InsertColumn(0, L"备份时间", LVCFMT_LEFT, listRect.Width() - sizeW - g_data.DPI(40));
		m_list.InsertColumn(1, L"大小", LVCFMT_RIGHT, sizeW);

		for (size_t i = 0; i < m_entries.size(); ++i)
		{
			int idx = m_list.InsertItem(static_cast<int>(i), m_entries[i].displayName.c_str());
			if (idx >= 0)
			{
				wchar_t sizeBuf[32]{};
				unsigned long long n = m_entries[i].sizeBytes;
				if (n >= 1024ULL * 1024ULL)
					swprintf_s(sizeBuf, L"%.1f MB", n / (1024.0 * 1024.0));
				else if (n >= 1024ULL)
					swprintf_s(sizeBuf, L"%.1f KB", n / 1024.0);
				else
					swprintf_s(sizeBuf, L"%llu B", n);
				m_list.SetItemText(idx, 1, sizeBuf);
				m_list.SetItemData(idx, i);
			}
		}
		// 按实际客户区（已扣除纵向滚动条）自适应列宽，任何份数下都不出横向滚动条
		CRect listClient;
		m_list.GetClientRect(&listClient);
		m_list.SetColumnWidth(0, listClient.Width() - sizeW - g_data.DPI(2));
		m_list.SetColumnWidth(1, sizeW);

		// 默认选中最新一份
		m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

		int btnW = g_data.DPI(62);
		int btnH = g_data.DPI(26);
		int btnY = rc.bottom - btnH - g_data.DPI(12);
		m_btnOk.Create(_T("恢复"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
			CRect(rc.right - btnW * 2 - g_data.DPI(16), btnY, rc.right - btnW - g_data.DPI(16), btnY + btnH), this, IDOK);
		m_btnOk.SetFont(&m_font);
		m_btnCancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
			CRect(rc.right - btnW - g_data.DPI(8), btnY, rc.right - g_data.DPI(8), btnY + btnH), this, IDCANCEL);
		m_btnCancel.SetFont(&m_font);

		m_list.SetFocus();
		// 显式显示窗口：宿主取数线程消息流密集时模态循环的空闲显示（MLF_SHOWONIDLE）
		// 可能长期不触发，会出现不可见却模态挂起的对话框
		ShowWindow(SW_SHOWNORMAL);
		return FALSE;
	}

	virtual void OnOK() override
	{
		// 回车/默认按钮与「恢复」按钮统一走选择逻辑
		OnRestore();
	}

	void OnRestore()
	{
		int idx = m_list.GetNextItem(-1, LVNI_SELECTED);
		if (idx < 0)
		{
			MessageBox(L"请先在列表中选择一份备份", L"提示", MB_ICONWARNING | MB_OK);
			return;
		}
		const WebDavBackupEntry& entry = m_entries[static_cast<size_t>(m_list.GetItemData(idx))];
		m_selectedFile = entry.fileName;
		m_selectedName = entry.displayName;
		EndDialog(IDOK);
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_PAINT)
		{
			CPaintDC dc(this);
			CRect clientRect;
			GetClientRect(clientRect);
			Gdiplus::Graphics g(dc.GetSafeHdc());
			Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&bgBrush, 0, 0, clientRect.Width(), clientRect.Height());
			return 0;
		}
		else if (message == WM_ERASEBKGND)
		{
			return TRUE;
		}
		else if (message == WM_CTLCOLORSTATIC)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(226, 232, 240));
			::SetBkColor(hdc, RGB(24, 27, 34));
			return (LRESULT)(HBRUSH)m_dark_brush.GetSafeHandle();
		}
		else if (message == WM_COMMAND)
		{
			// 无消息映射的对话框：按钮点击在这里分发（「恢复」= IDOK，取消 = IDCANCEL）
			if (HIWORD(wParam) == BN_CLICKED && lParam != 0)
			{
				if (LOWORD(wParam) == IDOK)
				{
					OnRestore();
					return 0;
				}
				if (LOWORD(wParam) == IDCANCEL)
				{
					EndDialog(IDCANCEL);
					return 0;
				}
			}
		}
		else if (message == WM_NOTIFY)
		{
			if ((int)wParam == IDC_BACKUP_LIST)
			{
				NMHDR* pNMHDR = (NMHDR*)lParam;
				if (pNMHDR->code == NM_DBLCLK)
				{
					OnRestore();
					return 0;
				}
			}
		}
		else if (message == WM_DRAWITEM)
		{
			LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
			if (pDI->CtlType == ODT_BUTTON)
			{
				CDC dc;
				dc.Attach(pDI->hDC);
				CRect rect = pDI->rcItem;
				UINT state = pDI->itemState;
				CString text = (pDI->CtlID == IDOK) ? _T("恢复") : _T("取消");

				bool isOk = (pDI->CtlID == IDOK);
				COLORREF bgColor;
				if (isOk)
					bgColor = (state & ODS_SELECTED) ? RGB(29, 78, 216) : RGB(37, 99, 235);
				else
					bgColor = (state & ODS_SELECTED) ? RGB(20, 25, 35) : RGB(30, 35, 46);

				dc.FillSolidRect(rect, bgColor);
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(255, 255, 255));
				CFont* pOldFont = dc.SelectObject(&m_font);
				dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(pOldFont);
				dc.Detach();
				return TRUE;
			}
		}
		return CDialog::WindowProc(message, wParam, lParam);
	}
};

// 暗色主题持仓编辑弹窗
class CDarkPositionInputDlg : public CDialog
{
public:
	std::wstring m_exchange;
	std::wstring m_code;
	std::wstring m_name;
	std::wstring m_full_code;
	double m_cost_price{ 0.0 };
	double m_holding_count{ 0.0 };

	CEdit m_cost_edit;
	CEdit m_count_edit;
	CButton m_btn_ok;
	CButton m_btn_cancel;

	CFont m_font;
	CFont m_font_bold;
	CBrush m_bg_brush;
	CBrush m_edit_brush;

	CDarkPositionInputDlg(const std::wstring& fullCode, const std::wstring& name = L"", const std::wstring& exch = L"", CWnd* pParent = nullptr)
		: CDialog(), m_full_code(fullCode), m_name(name), m_exchange(exch)
	{
		if (m_exchange.empty())
			m_exchange = CCommon::GetExchangeName(fullCode);
		m_code = CCommon::GetPureCode(fullCode);
		if (m_name.empty())
		{
			auto stockData = g_data.GetStockData(fullCode);
			if (stockData && !stockData->info.displayName.empty())
				m_name = stockData->info.displayName;
			else
				m_name = m_code;
		}
		m_cost_price = g_data.GetCostPrice(fullCode);
		m_holding_count = g_data.GetHoldingCount(fullCode);
	}

	INT_PTR DoModal(CWnd* pParent = nullptr)
	{
		BYTE buffer[512] = { 0 };
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buffer;
		pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
		pDlg->dwExtendedStyle = 0;
		pDlg->cdit = 0;
		pDlg->x = 0;
		pDlg->y = 0;
		pDlg->cx = 200;
		pDlg->cy = 140;

		InitModalIndirect(pDlg, pParent);
		return CDialog::DoModal();
	}

	virtual BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(L"设置持仓信息");

		BOOL darkCaption = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption, sizeof(darkCaption));

		m_font.CreatePointFont(100, _T("微软雅黑"));
		m_font_bold.CreatePointFont(105, _T("微软雅黑"));
		SetFont(&m_font);

		m_bg_brush.CreateSolidBrush(RGB(18, 20, 26));      // #12141A
		m_edit_brush.CreateSolidBrush(RGB(13, 15, 21));

		CRect cr;
		GetClientRect(&cr);

		int marginX = g_data.DPI(16);
		int editH = g_data.DPI(26);

		// 成本价输入框
		int costY = g_data.DPI(64);
		int editBoxH = g_data.DPI(18);
		int editOffset = g_data.DPI(4);
		int editBorderLeft = marginX + g_data.DPI(80);
		int editInnerLeft = editBorderLeft + g_data.DPI(6);
		int editInnerRight = cr.right - marginX - g_data.DPI(6);

		m_cost_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			CRect(editInnerLeft, costY + editOffset, editInnerRight, costY + editOffset + editBoxH), this, 1001);
		m_cost_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		::SetWindowTheme(m_cost_edit.GetSafeHwnd(), L"", L"");
		m_cost_edit.SetFont(&m_font);
		if (m_cost_price > 0)
		{
			CString s;
			s.Format(_T("%.3f"), m_cost_price);
			m_cost_edit.SetWindowText(s);
		}
		m_cost_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"输入成本价(元)");

		// 持股数输入框
		int countY = costY + editH + g_data.DPI(12);
		m_count_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			CRect(editInnerLeft, countY + editOffset, editInnerRight, countY + editOffset + editBoxH), this, 1002);
		m_count_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		::SetWindowTheme(m_count_edit.GetSafeHwnd(), L"", L"");
		m_count_edit.SetFont(&m_font);
		if (m_holding_count > 0)
		{
			CString s;
			s.Format(_T("%d"), static_cast<int>(m_holding_count));
			m_count_edit.SetWindowText(s);
		}
		m_count_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"输入持股数量");

		// 底部按钮
		int btnW = g_data.DPI(70);
		int btnH = g_data.DPI(26);
		int btnY = cr.bottom - btnH - g_data.DPI(12);

		m_btn_ok.Create(_T("确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
			CRect(cr.right - btnW * 2 - marginX - g_data.DPI(10), btnY, cr.right - btnW - marginX - g_data.DPI(10), btnY + btnH), this, IDOK);
		m_btn_ok.SetFont(&m_font_bold);

		m_btn_cancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
			CRect(cr.right - btnW - marginX, btnY, cr.right - marginX, btnY + btnH), this, IDCANCEL);
		m_btn_cancel.SetFont(&m_font);

		m_cost_edit.SetFocus();
		m_cost_edit.SetSel(0, -1);

		return FALSE;
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_ERASEBKGND)
		{
			return TRUE;
		}
		else if (message == WM_CTLCOLOREDIT)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(255, 255, 255));
			::SetBkColor(hdc, RGB(13, 15, 21));
			return (LRESULT)(HBRUSH)m_edit_brush;
		}
		else if (message == WM_COMMAND)
		{
			WORD wNotifyCode = HIWORD(wParam);
			if (wNotifyCode == EN_SETFOCUS || wNotifyCode == EN_KILLFOCUS)
			{
				InvalidateRect(nullptr, FALSE);
			}
		}
		else if (message == WM_CTLCOLORSTATIC)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(226, 232, 240));
			::SetBkColor(hdc, RGB(18, 20, 26));
			return (LRESULT)(HBRUSH)m_bg_brush;
		}
		else if (message == WM_DRAWITEM)
		{
			LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
			if (pDI->CtlType == ODT_BUTTON)
			{
				CDC dc;
				dc.Attach(pDI->hDC);
				CRect rect = pDI->rcItem;
				UINT state = pDI->itemState;
				CString text;
				if (pDI->CtlID == IDOK) text = _T("确定");
				else text = _T("取消");

				COLORREF bgColor = (pDI->CtlID == IDOK) ? RGB(37, 99, 235) : RGB(30, 35, 46);
				if (state & ODS_SELECTED) bgColor = (pDI->CtlID == IDOK) ? RGB(29, 78, 216) : RGB(20, 25, 35);
				
				dc.FillSolidRect(rect, bgColor);
				
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(255, 255, 255));
				CFont* pFont = (pDI->CtlID == IDOK) ? &m_font_bold : &m_font;
				CFont* pOldFont = dc.SelectObject(pFont);
				dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(pOldFont);
				dc.Detach();
				return TRUE;
			}
		}
		else if (message == WM_PAINT)
		{
			CPaintDC dc(this);
			CRect rc;
			GetClientRect(rc);

			CDC memDC;
			memDC.CreateCompatibleDC(&dc);
			CBitmap memBmp;
			memBmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
			CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

			Gdiplus::Graphics g(memDC.GetSafeHdc());
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

			// 1. 底色
			Gdiplus::SolidBrush bg(Gdiplus::Color(255, 18, 20, 26));
			g.FillRectangle(&bg, 0, 0, rc.Width(), rc.Height());

			int marginX = g_data.DPI(16);

			// 2. 股票信息展示卡片
			int cardTop = g_data.DPI(12);
			int cardBottom = cardTop + g_data.DPI(34);
			CRect cardRc(marginX, cardTop, rc.right - marginX, cardBottom);
			Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&cardBg, cardRc.left, cardRc.top, cardRc.Width(), cardRc.Height());
			Gdiplus::Pen cardBorder(Gdiplus::Color(255, 42, 48, 63), 1.0f);
			g.DrawRectangle(&cardBorder, cardRc.left, cardRc.top, cardRc.Width(), cardRc.Height());

			// 交易所 Badge
			int badgeW = g_data.DPI(50);
			int badgeH = g_data.DPI(18);
			int badgeX = cardRc.left + g_data.DPI(12);
			int badgeY = cardRc.top + g_data.DPI(8);
			Gdiplus::RectF badgeRf(static_cast<Gdiplus::REAL>(badgeX), static_cast<Gdiplus::REAL>(badgeY), static_cast<Gdiplus::REAL>(badgeW), static_cast<Gdiplus::REAL>(badgeH));
			Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 37, 99, 235));
			g.FillRectangle(&badgeBg, badgeRf);

			Gdiplus::Font badgeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::StringFormat sfCenter;
			sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
			sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			Gdiplus::SolidBrush whiteTxt(Gdiplus::Color(255, 255, 255, 255));
			// GDI+ 行框居中含雅黑 descent 空白区，汉字视觉偏上，文字矩形下移补偿
			Gdiplus::RectF badgeTxtRf = badgeRf;
			badgeTxtRf.Y += static_cast<Gdiplus::REAL>(g_data.DPI(1));
			g.DrawString(m_exchange.c_str(), -1, &badgeFont, badgeTxtRf, &sfCenter, &whiteTxt);

			// 代码
			int codeX = badgeX + badgeW + g_data.DPI(8);
			Gdiplus::Font codeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush codeTxt(Gdiplus::Color(255, 148, 163, 184));
			g.DrawString(m_code.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(codeX), static_cast<Gdiplus::REAL>(badgeY + g_data.DPI(1))), &codeTxt);

			// 股票名称 (大号加粗白色，紧跟在代码后面)
			int nameX = codeX + g_data.DPI(50);
			Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			g.DrawString(m_name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(nameX), static_cast<Gdiplus::REAL>(badgeY + 1)), &whiteTxt);

			// 3. 标签文字 (成本价 / 持股数)
			Gdiplus::Font labelFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 203, 213, 225));

			int costY = g_data.DPI(64);
			g.DrawString(L"成本价 (元):", -1, &labelFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(marginX), static_cast<Gdiplus::REAL>(costY + g_data.DPI(5))), &labelBrush);

			int countY = costY + g_data.DPI(26) + g_data.DPI(12);
			g.DrawString(L"持股数 (股):", -1, &labelFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(marginX), static_cast<Gdiplus::REAL>(countY + g_data.DPI(5))), &labelBrush);

			// Draw edit borders
			auto drawEdit = [&](CWnd& edit, int y) {
				if (!edit.GetSafeHwnd()) return;
				
				int editBorderLeft = marginX + g_data.DPI(80);
				CRect editRc(editBorderLeft, y, rc.right - marginX, y + g_data.DPI(26));
				
				Gdiplus::SolidBrush editBg(Gdiplus::Color(255, 13, 15, 21));
				g.FillRectangle(&editBg, editRc.left, editRc.top, editRc.Width(), editRc.Height());

				CWnd* pFocus = GetFocus();
				bool focused = (pFocus && pFocus->GetSafeHwnd() == edit.GetSafeHwnd());
				Gdiplus::Pen pen(focused ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 52, 58, 72), 1.0f);
				g.DrawRectangle(&pen, editRc.left, editRc.top, editRc.Width() - 1, editRc.Height() - 1);
			};
			drawEdit(m_cost_edit, costY);
			drawEdit(m_count_edit, countY);

			dc.BitBlt(0, 0, rc.Width(), rc.Height(), &memDC, 0, 0, SRCCOPY);
			memDC.SelectObject(pOldBmp);
			return 0;
		}

		return CDialog::WindowProc(message, wParam, lParam);
	}

	virtual void OnOK() override
	{
		if (m_cost_edit.GetSafeHwnd())
		{
			CString strCost;
			m_cost_edit.GetWindowText(strCost);
			strCost.Trim();
			m_cost_price = _ttof(strCost);
		}
		if (m_count_edit.GetSafeHwnd())
		{
			CString strCount;
			m_count_edit.GetWindowText(strCount);
			strCount.Trim();
			m_holding_count = _ttof(strCount);
		}
		CDialog::OnOK();
	}
};

// 暗色主题股票关注价格编辑弹窗
class CDarkStockAlertInputDlg : public CDialog
{
public:
	std::wstring m_exchange;
	std::wstring m_code;
	std::wstring m_name;
	std::wstring m_full_code;
	double m_low_price{ 0.0 };
	double m_high_price{ 0.0 };

	CEdit m_low_edit;
	CEdit m_high_edit;
	CButton m_btn_ok;
	CButton m_btn_cancel;

	CFont m_font;
	CFont m_font_bold;
	CBrush m_bg_brush;
	CBrush m_edit_brush;

	CDarkStockAlertInputDlg(const std::wstring& fullCode, const std::wstring& name = L"", const std::wstring& exch = L"", CWnd* pParent = nullptr)
		: CDialog(), m_full_code(fullCode), m_name(name), m_exchange(exch)
	{
		if (m_exchange.empty())
			m_exchange = CCommon::GetExchangeName(fullCode);
		m_code = CCommon::GetPureCode(fullCode);
		if (m_name.empty())
		{
			auto stockData = g_data.GetStockData(fullCode);
			if (stockData && !stockData->info.displayName.empty())
				m_name = stockData->info.displayName;
			else
				m_name = m_code;
		}
		m_low_price = g_data.GetAlertLowPrice(fullCode);
		m_high_price = g_data.GetAlertHighPrice(fullCode);
	}

	INT_PTR DoModal(CWnd* pParent = nullptr)
	{
		BYTE buffer[512] = { 0 };
		DLGTEMPLATE* pDlg = (DLGTEMPLATE*)buffer;
		pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
		pDlg->dwExtendedStyle = 0;
		pDlg->cdit = 0;
		pDlg->x = 0;
		pDlg->y = 0;
		pDlg->cx = 215;
		pDlg->cy = 140;

		InitModalIndirect(pDlg, pParent);
		return CDialog::DoModal();
	}

	virtual BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		SetWindowText(L"编辑股票");

		BOOL darkCaption = TRUE;
		::DwmSetWindowAttribute(GetSafeHwnd(), 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkCaption, sizeof(darkCaption));

		m_font.CreatePointFont(100, _T("微软雅黑"));
		m_font_bold.CreatePointFont(105, _T("微软雅黑"));
		SetFont(&m_font);

		m_bg_brush.CreateSolidBrush(RGB(18, 20, 26));      // #12141A
		m_edit_brush.CreateSolidBrush(RGB(13, 15, 21));

		CRect cr;
		GetClientRect(&cr);

		int marginX = g_data.DPI(16);
		int editH = g_data.DPI(26);

		// 关注低价输入框
		int lowY = g_data.DPI(64);
		int editBoxH = g_data.DPI(18);
		int editOffset = g_data.DPI(4);
		int editBorderLeft = marginX + g_data.DPI(92);
		int editInnerLeft = editBorderLeft + g_data.DPI(6);
		int editInnerRight = cr.right - marginX - g_data.DPI(6);

		m_low_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			CRect(editInnerLeft, lowY + editOffset, editInnerRight, lowY + editOffset + editBoxH), this, 1001);
		m_low_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		::SetWindowTheme(m_low_edit.GetSafeHwnd(), L"", L"");
		m_low_edit.SetFont(&m_font);
		if (m_low_price > 0)
		{
			CString s;
			s.Format(_T("%.2f"), m_low_price);
			m_low_edit.SetWindowText(s);
		}
		m_low_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"输入关注低价(元)");

		// 关注高价输入框
		int highY = lowY + editH + g_data.DPI(12);
		m_high_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			CRect(editInnerLeft, highY + editOffset, editInnerRight, highY + editOffset + editBoxH), this, 1002);
		m_high_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
		::SetWindowTheme(m_high_edit.GetSafeHwnd(), L"", L"");
		m_high_edit.SetFont(&m_font);
		if (m_high_price > 0)
		{
			CString s;
			s.Format(_T("%.2f"), m_high_price);
			m_high_edit.SetWindowText(s);
		}
		m_high_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"输入关注高价(元)");

		// 底部按钮
		int btnW = g_data.DPI(70);
		int btnH = g_data.DPI(26);
		int btnY = cr.bottom - btnH - g_data.DPI(12);

		m_btn_ok.Create(_T("确定"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
			CRect(cr.right - btnW * 2 - marginX - g_data.DPI(10), btnY, cr.right - btnW - marginX - g_data.DPI(10), btnY + btnH), this, IDOK);
		m_btn_ok.SetFont(&m_font_bold);

		m_btn_cancel.Create(_T("取消"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
			CRect(cr.right - btnW - marginX, btnY, cr.right - marginX, btnY + btnH), this, IDCANCEL);
		m_btn_cancel.SetFont(&m_font);

		m_low_edit.SetFocus();
		m_low_edit.SetSel(0, -1);

		return FALSE;
	}

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
	{
		if (message == WM_ERASEBKGND)
		{
			return TRUE;
		}
		else if (message == WM_CTLCOLOREDIT)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(255, 255, 255));
			::SetBkColor(hdc, RGB(13, 15, 21));
			return (LRESULT)(HBRUSH)m_edit_brush;
		}
		else if (message == WM_COMMAND)
		{
			WORD wNotifyCode = HIWORD(wParam);
			if (wNotifyCode == EN_SETFOCUS || wNotifyCode == EN_KILLFOCUS)
			{
				InvalidateRect(nullptr, FALSE);
			}
		}
		else if (message == WM_CTLCOLORSTATIC)
		{
			HDC hdc = (HDC)wParam;
			::SetTextColor(hdc, RGB(226, 232, 240));
			::SetBkColor(hdc, RGB(18, 20, 26));
			return (LRESULT)(HBRUSH)m_bg_brush;
		}
		else if (message == WM_DRAWITEM)
		{
			LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;
			if (pDI->CtlType == ODT_BUTTON)
			{
				CDC dc;
				dc.Attach(pDI->hDC);
				CRect rect = pDI->rcItem;
				UINT state = pDI->itemState;
				CString text = (pDI->CtlID == IDOK) ? _T("确定") : _T("取消");

				COLORREF bgColor = (pDI->CtlID == IDOK) ? RGB(37, 99, 235) : RGB(30, 35, 46);
				if (state & ODS_SELECTED) bgColor = (pDI->CtlID == IDOK) ? RGB(29, 78, 216) : RGB(20, 25, 35);
				
				dc.FillSolidRect(rect, bgColor);
				
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(255, 255, 255));
				CFont* pFont = (pDI->CtlID == IDOK) ? &m_font_bold : &m_font;
				CFont* pOldFont = dc.SelectObject(pFont);
				dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(pOldFont);
				dc.Detach();
				return TRUE;
			}
		}
		else if (message == WM_PAINT)
		{
			CPaintDC dc(this);
			CRect rc;
			GetClientRect(rc);

			CDC memDC;
			memDC.CreateCompatibleDC(&dc);
			CBitmap memBmp;
			memBmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
			CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

			Gdiplus::Graphics g(memDC.GetSafeHdc());
			g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
			g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

			// 1. 底色
			Gdiplus::SolidBrush bg(Gdiplus::Color(255, 18, 20, 26));
			g.FillRectangle(&bg, 0, 0, rc.Width(), rc.Height());

			int marginX = g_data.DPI(16);

			// 2. 股票信息展示卡片
			int cardTop = g_data.DPI(12);
			int cardBottom = cardTop + g_data.DPI(34);
			CRect cardRc(marginX, cardTop, rc.right - marginX, cardBottom);
			Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&cardBg, cardRc.left, cardRc.top, cardRc.Width(), cardRc.Height());
			Gdiplus::Pen cardBorder(Gdiplus::Color(255, 42, 48, 63), 1.0f);
			g.DrawRectangle(&cardBorder, cardRc.left, cardRc.top, cardRc.Width(), cardRc.Height());

			// 交易所 Badge
			int badgeW = g_data.DPI(50);
			int badgeH = g_data.DPI(18);
			int badgeX = cardRc.left + g_data.DPI(12);
			int badgeY = cardRc.top + g_data.DPI(8);
			Gdiplus::RectF badgeRf(static_cast<Gdiplus::REAL>(badgeX), static_cast<Gdiplus::REAL>(badgeY), static_cast<Gdiplus::REAL>(badgeW), static_cast<Gdiplus::REAL>(badgeH));
			Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 37, 99, 235));
			g.FillRectangle(&badgeBg, badgeRf);

			Gdiplus::Font badgeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::StringFormat sfCenter;
			sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
			sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
			Gdiplus::SolidBrush whiteTxt(Gdiplus::Color(255, 255, 255, 255));
			// GDI+ 行框居中含雅黑 descent 空白区，汉字视觉偏上，文字矩形下移补偿
			Gdiplus::RectF badgeTxtRf = badgeRf;
			badgeTxtRf.Y += static_cast<Gdiplus::REAL>(g_data.DPI(1));
			g.DrawString(m_exchange.c_str(), -1, &badgeFont, badgeTxtRf, &sfCenter, &whiteTxt);

			// 代码
			int codeX = badgeX + badgeW + g_data.DPI(8);
			Gdiplus::Font codeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush codeTxt(Gdiplus::Color(255, 148, 163, 184));
			g.DrawString(m_code.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(codeX), static_cast<Gdiplus::REAL>(badgeY + g_data.DPI(1))), &codeTxt);

			// 股票名称
			int nameX = codeX + g_data.DPI(50);
			Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			g.DrawString(m_name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(nameX), static_cast<Gdiplus::REAL>(badgeY + 1)), &whiteTxt);

			// 3. 标签文字 (关注低价 / 关注高价)
			Gdiplus::Font labelFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 203, 213, 225));

			int lowY = g_data.DPI(64);
			g.DrawString(L"关注低价 (元):", -1, &labelFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(marginX), static_cast<Gdiplus::REAL>(lowY + g_data.DPI(5))), &labelBrush);

			int highY = lowY + g_data.DPI(26) + g_data.DPI(12);
			g.DrawString(L"关注高价 (元):", -1, &labelFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(marginX), static_cast<Gdiplus::REAL>(highY + g_data.DPI(5))), &labelBrush);

			// Draw edit borders
			auto drawEdit = [&](CWnd& edit, int y) {
				if (!edit.GetSafeHwnd()) return;
				
				int editBorderLeft = marginX + g_data.DPI(92);
				CRect editRc(editBorderLeft, y, rc.right - marginX, y + g_data.DPI(26));
				
				Gdiplus::SolidBrush editBg(Gdiplus::Color(255, 13, 15, 21));
				g.FillRectangle(&editBg, editRc.left, editRc.top, editRc.Width(), editRc.Height());

				CWnd* pFocus = GetFocus();
				bool focused = (pFocus && pFocus->GetSafeHwnd() == edit.GetSafeHwnd());
				Gdiplus::Pen pen(focused ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 52, 58, 72), 1.0f);
				g.DrawRectangle(&pen, editRc.left, editRc.top, editRc.Width() - 1, editRc.Height() - 1);
			};
			drawEdit(m_low_edit, lowY);
			drawEdit(m_high_edit, highY);

			dc.BitBlt(0, 0, rc.Width(), rc.Height(), &memDC, 0, 0, SRCCOPY);
			memDC.SelectObject(pOldBmp);
			return 0;
		}

		return CDialog::WindowProc(message, wParam, lParam);
	}

	virtual void OnOK() override
	{
		CString strLow, strHigh;
		if (m_low_edit.GetSafeHwnd())
			m_low_edit.GetWindowText(strLow);
		if (m_high_edit.GetSafeHwnd())
			m_high_edit.GetWindowText(strHigh);

		strLow.Trim();
		strHigh.Trim();

		m_low_price = strLow.IsEmpty() ? 0.0 : _ttof(strLow);
		m_high_price = strHigh.IsEmpty() ? 0.0 : _ttof(strHigh);

		CDialog::OnOK();
	}
};

// CManagerDialog 对话框

IMPLEMENT_DYNAMIC(CManagerDialog, CDialog)

CManagerDialog::CManagerDialog(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_MANAGER_DIALOG, pParent)
{
	m_menu_rects.resize(8);
	m_dark_brush.CreateSolidBrush(COLOR_BG_DARK);     // #12141A
	m_card_brush.CreateSolidBrush(COLOR_BG_CARD);     // #181B22
	m_edit_brush.CreateSolidBrush(RGB(13, 15, 21));   // 输入框内嵌底色（略深于卡片，形成下沉观感）
}

CManagerDialog::~CManagerDialog()
{
}

// 以 WS_CHILD 方式内嵌为宿主窗口的子对话框（悬浮窗“设置”视图）：
// 加载 IDD_MANAGER_DIALOG 模板副本，剥除弹出/标题栏/可缩放边框属性后 CreateIndirect，
// 全程不弹出独立窗口、无系统边框白边，铺满宿主后即形成“行情中心式”原地设置视图。
bool CManagerDialog::CreateAsChild(CWnd* pParent)
{
	if (pParent == nullptr || pParent->GetSafeHwnd() == nullptr)
		return false;

	HINSTANCE hInst = AfxGetInstanceHandle();
	HRSRC hRes = ::FindResource(hInst, MAKEINTRESOURCE(IDD_MANAGER_DIALOG), RT_DIALOG);
	if (hRes == nullptr)
		return false;
	HGLOBAL hResData = ::LoadResource(hInst, hRes);
	if (hResData == nullptr)
		return false;
	const DLGTEMPLATE* pSrc = static_cast<const DLGTEMPLATE*>(::LockResource(hResData));
	const DWORD tplSize = ::SizeofResource(hInst, hRes);
	if (pSrc == nullptr || tplSize < sizeof(DLGTEMPLATE))
		return false;

	// 资源段只读，复制到可写内存后再改样式
	HGLOBAL hCopy = ::GlobalAlloc(GMEM_MOVEABLE, tplSize);
	if (hCopy == nullptr)
		return false;
	DLGTEMPLATE* pTpl = static_cast<DLGTEMPLATE*>(::GlobalLock(hCopy));
	if (pTpl == nullptr)
	{
		::GlobalFree(hCopy);
		return false;
	}
	memcpy(pTpl, pSrc, tplSize);

	// 定位 style / exStyle 字段：DIALOGEX 资源在内存中是 DLGTEMPLATEEX 布局
	// （dlgVer=1 + signature=0xFFFF 开头，style 位于偏移 12），与旧版 DLGTEMPLATE
	// （style 位于偏移 0）不同。直接改 DLGTEMPLATE::style 会破坏 EX 模板签名，
	// 导致 CreateDialogIndirect 报“不支持尝试执行的操作”。
	DWORD* pStyle = nullptr;
	DWORD* pExStyle = nullptr;
	if (tplSize >= 16 && reinterpret_cast<WORD*>(pTpl)[0] == 1 && reinterpret_cast<WORD*>(pTpl)[1] == 0xFFFF)
	{
		pExStyle = reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(pTpl) + 8);
		pStyle = reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(pTpl) + 12);
	}
	else
	{
		pStyle = &pTpl->style;
		pExStyle = &pTpl->dwExtendedStyle;
	}
	*pStyle &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | DS_MODALFRAME);
	*pStyle |= (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	// 必须在 CreateIndirect 之前置位：OnInitDialog 会在创建过程中同步执行
	m_as_child = true;
	BOOL ok = FALSE;
	try
	{
		ok = CreateIndirect(pTpl, pParent);
	}
	catch (CException* e)
	{
		// 兜底：创建失败（模板异常/控件创建异常）时记录原因并回退，
		// 由调用方恢复图表按钮，避免设置视图停留在黑屏状态
		wchar_t msg[256] = { 0 };
		e->GetErrorMessage(msg, 255);
		CCommon::WriteLog((std::wstring(L"[SettingsView] CreateIndirect failed: ") + msg).c_str(), g_data.m_log_path.c_str());
		e->Delete();
		ok = FALSE;
	}

	::GlobalUnlock(hCopy);
	::GlobalFree(hCopy);
	return ok != FALSE;
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
	DDX_Control(pDX, IDC_DISPLAY_AREA_COMBO, m_display_area_combo);
}

BEGIN_MESSAGE_MAP(CManagerDialog, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_DRAWITEM()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_ACTIVATE()
	ON_WM_NCACTIVATE()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
	ON_WM_GETMINMAXINFO()

	ON_EN_CHANGE(IDC_STOCK_SEARCH_EDIT, &CManagerDialog::OnSearchEditChange)
	ON_NOTIFY(NM_CLICK, IDC_MGR_LIST, &CManagerDialog::OnListItemClick)
	ON_NOTIFY(NM_CLICK, IDC_POS_LIST, &CManagerDialog::OnListItemClick)
	ON_NOTIFY(NM_CLICK, IDC_CUSTOM_LIST, &CManagerDialog::OnListItemClick)
	ON_NOTIFY(NM_DBLCLK, IDC_MGR_LIST, &CManagerDialog::OnLbnDblclkMgrList)
	ON_NOTIFY(NM_DBLCLK, IDC_POS_LIST, &CManagerDialog::OnLbnDblclkPosList)
	ON_NOTIFY(NM_DBLCLK, IDC_CUSTOM_LIST, &CManagerDialog::OnLbnDblclkCustomList)

	ON_BN_CLICKED(IDC_MGR_ADD_BTN, &CManagerDialog::OnAddBtnClick)
	ON_BN_CLICKED(IDC_MGR_EDIT_BTN, &CManagerDialog::OnEditBtnClick)
	ON_BN_CLICKED(IDC_MGR_DEL_BTN, &CManagerDialog::OnDelBtnClick)
	ON_BN_CLICKED(1199, &CManagerDialog::OnDelGroupBtnClick)
	ON_BN_CLICKED(IDC_MGR_MOVE_UP_BTN, &CManagerDialog::OnMoveUpBtnClick)
	ON_BN_CLICKED(IDC_MGR_MOVE_DOWN_BTN, &CManagerDialog::OnMoveDownBtnClick)
	ON_BN_CLICKED(IDC_MA_ADD_BTN, &CManagerDialog::OnMaAddBtnClick)
	ON_BN_CLICKED(1198, &CManagerDialog::OnGroupSortBtnClick)

	ON_BN_CLICKED(IDC_FULL_DAY_CHECK, &CManagerDialog::OnClickedFullDayCheck)
	ON_BN_CLICKED(IDC_SHOW_FLUCTUATION_CHECK, &CManagerDialog::OnBnClickedShowFluctuationCheck)
	ON_BN_CLICKED(IDC_SHOW_TODAY_PROFIT_CHECK, &CManagerDialog::OnBnClickedShowTodayProfitCheck)
	ON_BN_CLICKED(IDC_USE_SOCKS5_PROXY_CHECK, &CManagerDialog::OnBnClickedUseSocks5ProxyCheck)
	ON_BN_CLICKED(IDC_RESET_DATA_BTN, &CManagerDialog::OnBnClickedResetData)

	ON_BN_CLICKED(IDC_WEBDAV_TEST_BTN, &CManagerDialog::OnBnClickedWebDavTestBtn)
	ON_BN_CLICKED(IDC_WEBDAV_UPLOAD_BTN, &CManagerDialog::OnBnClickedWebDavUploadBtn)
	ON_BN_CLICKED(IDC_WEBDAV_DOWNLOAD_BTN, &CManagerDialog::OnBnClickedWebDavDownloadBtn)
	ON_BN_CLICKED(IDC_WEBDAV_AUTO_SYNC_CHECK, &CManagerDialog::OnBnClickedWebDavAutoSyncCheck)
	ON_BN_CLICKED(IDC_WEBDAV_AUTO_BACKUP_CHECK, &CManagerDialog::OnBnClickedWebDavAutoBackupCheck)
	ON_MESSAGE(WM_APP_WEBDAV_RESULT, &CManagerDialog::OnWebDavResult)
	ON_BN_CLICKED(IDC_API_TEST_BTN, &CManagerDialog::OnBnClickedApiTestBtn)
	ON_MESSAGE(WM_APP_API_PROBE_FINISHED, &CManagerDialog::OnApiProbeFinished)
	ON_MESSAGE(WM_APP_SEARCH_RESULT_READY, &CManagerDialog::OnSearchResultReady)

	// 列表行自绘（交替行底色/选中高亮）
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_MGR_LIST, &CManagerDialog::OnListCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_POS_LIST, &CManagerDialog::OnListCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CUSTOM_LIST, &CManagerDialog::OnListCustomDraw)

	// 输入框焦点变化时重绘自绘边框（聚焦高亮蓝）
	ON_EN_SETFOCUS(IDC_STOCK_SEARCH_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_STOCK_SEARCH_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_KLINE_WIDTH_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_KLINE_WIDTH_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_KLINE_HEIGHT_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_KLINE_HEIGHT_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_SOCKS5_PROXY_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_SOCKS5_PROXY_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_MA_INPUT_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_MA_INPUT_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_WEBDAV_URL_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_URL_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_WEBDAV_USER_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_USER_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_WEBDAV_PWD_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_PWD_EDIT, &CManagerDialog::OnEditFocusLost)
	ON_EN_SETFOCUS(IDC_WEBDAV_DIR_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_WEBDAV_DIR_EDIT, &CManagerDialog::OnEditFocusLost)

	ON_WM_MOUSEWHEEL()

	ON_BN_CLICKED(IDOK, &CManagerDialog::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CManagerDialog::OnBnClickedCancel)
END_MESSAGE_MAP()

// CManagerDialog 消息处理程序

BOOL CManagerDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	HICON hIcon = g_data.GetIcon(IDI_STOCK);
	SetIcon(hIcon, FALSE);

	// 深色标题栏（Win10 1809+ / Win11），与插件暗色主题保持一致（内嵌子窗口无标题栏，跳过）
	if (!m_as_child)
	{
		BOOL darkCaption = TRUE;
		if (FAILED(DwmSetWindowAttribute(GetSafeHwnd(), DWMWA_USE_IMMERSIVE_DARK_MODE, &darkCaption, sizeof(darkCaption))))
		{
			DwmSetWindowAttribute(GetSafeHwnd(), 19, &darkCaption, sizeof(darkCaption));
		}
	}

	// 开启 WS_CLIPCHILDREN，确保父窗口双缓冲 BitBlt 绝对不冲刷/覆盖任何子控件（如确定/取消按钮），从底层消除控件白光闪烁
	ModifyStyle(0, WS_CLIPCHILDREN);

	// 设置窗口默认大小和最小尺寸
	// 高度需容纳均线日配置页 4 张卡片（116+118+104+132 + 3*14 = 512 DPI + 头部/按钮区）
	// 内嵌子窗口模式：尺寸由宿主悬浮窗 MoveWindow 决定，内容区以隐藏式滚动适配
	if (!m_as_child)
	{
		int initWidth = g_data.DPI(800);
		int initHeight = g_data.DPI(680);
		m_min_size.cx = g_data.DPI(720);
		m_min_size.cy = g_data.DPI(640);

		CRect curRect;
		GetWindowRect(curRect);
		SetWindowPos(nullptr, curRect.left, curRect.top, initWidth, initHeight, SWP_NOMOVE | SWP_NOZORDER);
	}

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
		IDC_FULL_DAY_CHECK, IDC_SHOW_FLUCTUATION_CHECK, IDC_SHOW_TODAY_PROFIT_CHECK,
		IDC_USE_SOCKS5_PROXY_CHECK,
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
		IDC_WEBDAV_TEST_BTN, IDC_WEBDAV_UPLOAD_BTN, IDC_WEBDAV_DOWNLOAD_BTN,
		1197, 1198, 1199
	};
	for (int id : ownerDrawBtnIds)
	{
		CWnd* pBtn = GetDlgItem(id);
		if (pBtn && pBtn->GetSafeHwnd())
		{
			pBtn->ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
			SetWindowTheme(pBtn->GetSafeHwnd(), L"", L"");
		}
	}

	// 初始化搜索输入框与下拉结果弹窗
	m_search_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(0, 0, 0, 0), this, IDC_STOCK_SEARCH_EDIT);
	m_search_edit.SetFont(&m_font);
	m_search_edit.ModifyStyle(WS_BORDER, 0);
	m_search_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
	m_search_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索股票/代码/拼音...");

	m_mgr_del_group_btn.Create(_T("删除分组"), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, 1199);
	m_mgr_del_group_btn.SetFont(&m_font);

	// 分组管理页右上角「分组排序」入口（自选股/持仓顺序固定，仅自定义分组可调）
	m_group_sort_btn.Create(_T("分组排序"), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, 1198);
	m_group_sort_btn.SetFont(&m_font);

	// 接口检测页右上角「立即重新检测」入口
	m_api_test_btn.Create(_T("立即重新检测"), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, 1197);
	m_api_test_btn.SetFont(&m_font);

	m_search_dropdown.CreatePopup(this);

	m_search_dropdown.m_on_add_to_group = [this](const StockSearchResult& stock, int sel) {
		if (sel == 3001)
		{
			if (std::find(m_data.m_stock_codes.begin(), m_data.m_stock_codes.end(), stock.fullCode) == m_data.m_stock_codes.end())
			{
				m_data.m_stock_codes.push_back(stock.fullCode);
				RefreshStockList();
				g_data.m_setting_data.m_stock_codes = m_data.m_stock_codes;
				g_data.SaveConfig();
			}
		}
		else if (sel == 3002)
		{
			// 持仓分组独立维护，不同步到自选股
			CDarkPositionInputDlg dlg(stock.fullCode, stock.name, stock.exchange, this);
			if (dlg.DoModal(this) == IDOK)
			{
				g_data.SetPosition(stock.fullCode, dlg.m_cost_price, dlg.m_holding_count);
				if (std::find(m_data.m_position_codes.begin(), m_data.m_position_codes.end(), stock.fullCode) == m_data.m_position_codes.end())
					m_data.m_position_codes.push_back(stock.fullCode);
				g_data.m_setting_data.m_stock_codes = m_data.m_stock_codes;
				g_data.m_setting_data.m_position_codes = m_data.m_position_codes;
				g_data.SaveConfig();
				RefreshStockList();
				RefreshPositionList();
				SwitchGroupTab(1); // 自动切换至持仓 Tab
			}
		}
		else if (sel >= 3010 && sel < static_cast<int>(3010 + m_data.m_custom_groups.size()))
		{
			size_t groupIdx = sel - 3010;
			auto& codes = m_data.m_custom_groups[groupIdx].codes;
			if (std::find(codes.begin(), codes.end(), stock.fullCode) == codes.end())
			{
				codes.push_back(stock.fullCode);
				RefreshCustomList();
				g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
				g_data.SaveConfig();
			}
		}
		else if (sel == 3003)
		{
			CString defName;
			defName.Format(L"分组%d", static_cast<int>(m_data.m_custom_groups.size() + 1));
			CSimpleInputDialog inputDlg(L"新建分组", L"请输入新分组名称：", defName, this);
			if (inputDlg.DoModal() == IDOK && !inputDlg.m_value.IsEmpty())
			{
				CustomGroup newGrp;
				newGrp.name = inputDlg.m_value.GetString();
				newGrp.codes.push_back(stock.fullCode);
				m_data.m_custom_groups.push_back(newGrp);
				SwitchGroupTab(static_cast<int>(m_data.m_custom_groups.size()) + 1);
				g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
				g_data.SaveConfig();
			}
		}

		if (sel > 0)
		{
			m_search_edit.SetWindowText(L"");
			m_search_dropdown.HidePopup();
		}
	};

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

	auto setupListColumns = [](CListCtrl& list, const std::vector<std::tuple<std::wstring, int, int>>& cols) {
		for (int i = 0; i < static_cast<int>(cols.size()); ++i)
		{
			list.InsertColumn(i, std::get<0>(cols[i]).c_str(), std::get<1>(cols[i]), std::get<2>(cols[i]));
			LVCOLUMN lvc = { 0 };
			lvc.mask = LVCF_FMT;
			lvc.fmt = std::get<1>(cols[i]);
			list.SetColumn(i, &lvc);
		}
	};

	setupListDarkTheme(m_stock_listctrl);
	setupListColumns(m_stock_listctrl, {
		{ L"交易所", LVCFMT_CENTER, g_data.DPI(65) },
		{ L"代码", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"名称", LVCFMT_LEFT, g_data.DPI(130) },
		{ L"关注低价", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"关注高价", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"状态栏显示", LVCFMT_CENTER, g_data.DPI(75) }
	});

	setupListDarkTheme(m_pos_listctrl);
	setupListColumns(m_pos_listctrl, {
		{ L"交易所", LVCFMT_CENTER, g_data.DPI(65) },
		{ L"代码", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"股票名称", LVCFMT_LEFT, g_data.DPI(130) },
		{ L"成本价", LVCFMT_CENTER, g_data.DPI(80) },
		{ L"持股数", LVCFMT_CENTER, g_data.DPI(80) },
		{ L"状态栏显示", LVCFMT_CENTER, g_data.DPI(75) }
	});

	setupListDarkTheme(m_custom_listctrl);
	setupListColumns(m_custom_listctrl, {
		{ L"交易所", LVCFMT_CENTER, g_data.DPI(65) },
		{ L"代码", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"名称", LVCFMT_LEFT, g_data.DPI(130) },
		{ L"关注低价", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"关注高价", LVCFMT_CENTER, g_data.DPI(75) },
		{ L"状态栏显示", LVCFMT_CENTER, g_data.DPI(75) }
	});

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
	SetCheck(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation);
	SetCheck(IDC_SHOW_TODAY_PROFIT_CHECK, m_data.m_show_today_profit);
	SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy);
	SetDlgItemText(IDC_SOCKS5_PROXY_EDIT, m_data.m_socks5_proxy.c_str());

	CString strKlineW, strKlineH;
	strKlineW.Format(_T("%d"), static_cast<int>(m_data.m_kline_width));
	SetDlgItemText(IDC_KLINE_WIDTH_EDIT, strKlineW);
	strKlineH.Format(_T("%d"), static_cast<int>(m_data.m_kline_height));
	SetDlgItemText(IDC_KLINE_HEIGHT_EDIT, strKlineH);

	m_display_area_combo.ResetContent();
	m_display_area_combo.AddString(L"左上角");
	m_display_area_combo.AddString(L"右上角");
	m_display_area_combo.AddString(L"左下角");
	m_display_area_combo.AddString(L"右下角");
	m_display_area_combo.AddString(L"居中");
	m_display_area_combo.SetItemHeight(-1, g_data.DPI(26));
	m_display_area_combo.SetItemHeight(0, g_data.DPI(26)); 
	int selArea = m_data.m_display_area;
	if (selArea < AREA_LEFT_TOP || selArea > AREA_CENTER)
		selArea = AREA_RIGHT_BOTTOM;
	m_display_area_combo.SetCurSel(selArea);
	::SetWindowTheme(m_display_area_combo.GetSafeHwnd(), L"", L"");

	// 加载 WebDAV 云端备份控件值
	SetDlgItemText(IDC_WEBDAV_URL_EDIT, m_data.m_webdav_url.c_str());
	SetDlgItemText(IDC_WEBDAV_USER_EDIT, m_data.m_webdav_username.c_str());
	SetDlgItemText(IDC_WEBDAV_PWD_EDIT, m_data.m_webdav_password.c_str());
	SetDlgItemText(IDC_WEBDAV_DIR_EDIT, m_data.m_webdav_dir.c_str());
	SetCheck(IDC_WEBDAV_AUTO_SYNC_CHECK, m_data.m_webdav_auto_sync);
	SetCheck(IDC_WEBDAV_AUTO_BACKUP_CHECK, m_data.m_webdav_auto_backup);

	if (m_data.m_ma_days.empty())
		m_data.m_ma_days = { 5, 20, 60 };

	if (m_data.m_header_metrics.empty())
		m_data.m_header_metrics = { L"总市值", L"成交额", L"成交量", L"量比" };

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
					Gdiplus::Graphics graphics(dc.GetSafeHdc());
					graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
					const float inset = static_cast<float>(g_data.DPI(2));
					Icons::Draw(graphics, Icons::Id::Check,
						Gdiplus::RectF(static_cast<Gdiplus::REAL>(box.left) + inset, static_cast<Gdiplus::REAL>(box.top) + inset,
							static_cast<Gdiplus::REAL>(box.Width()) - inset * 2.0f, static_cast<Gdiplus::REAL>(box.Height()) - inset * 2.0f),
						RGB(255, 255, 255));
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
	for (const auto& preset : GetPresetIndices())
	{
		if (preset.code == code)
			return preset.name;
	}
	return code;
}

void CManagerDialog::RefreshStockList()
{
	m_stock_listctrl.DeleteAllItems();
	for (size_t i = 0; i < m_data.m_stock_codes.size(); ++i)
	{
		const auto& code = m_data.m_stock_codes[i];
		std::wstring exch = CCommon::GetExchangeName(code);
		std::wstring pureCode = CCommon::GetPureCode(code);
		std::wstring name = GetStockName(code);

		int nItem = m_stock_listctrl.InsertItem(static_cast<int>(i), exch.c_str());
		m_stock_listctrl.SetItemText(nItem, 1, pureCode.c_str());
		m_stock_listctrl.SetItemText(nItem, 2, name.c_str());

		double low = g_data.GetAlertLowPrice(code);
		double high = g_data.GetAlertHighPrice(code);
		if (low > 0)
		{
			CString lowStr;
			lowStr.Format(_T("%.2f"), low);
			m_stock_listctrl.SetItemText(nItem, 3, lowStr);
		}
		if (high > 0)
		{
			CString highStr;
			highStr.Format(_T("%.2f"), high);
			m_stock_listctrl.SetItemText(nItem, 4, highStr);
		}

		if (g_data.GetShowInStatusBar(code))
		{
			m_stock_listctrl.SetItemText(nItem, 5, L"√");
		}
		else
		{
			m_stock_listctrl.SetItemText(nItem, 5, L"");
		}
	}
	AdjustListColumns(m_stock_listctrl, 0);
}

void CManagerDialog::RefreshPositionList()
{
	m_pos_listctrl.DeleteAllItems();
	int nItem = 0;
	// 持仓分组独立列表，列表成员即持仓成员（不再依赖自选股）
	for (size_t i = 0; i < m_data.m_position_codes.size(); ++i)
	{
		const auto& code = m_data.m_position_codes[i];
		double cost = g_data.GetCostPrice(code);
		double count = g_data.GetHoldingCount(code);

		std::wstring exch = CCommon::GetExchangeName(code);
		std::wstring pureCode = CCommon::GetPureCode(code);
		std::wstring name = GetStockName(code);

		m_pos_listctrl.InsertItem(nItem, exch.c_str());
		m_pos_listctrl.SetItemData(nItem, i);

		m_pos_listctrl.SetItemText(nItem, 1, pureCode.c_str());
		m_pos_listctrl.SetItemText(nItem, 2, name.c_str());

		CString strCost, strCount;
		strCost.Format(_T("%.2f"), cost);
		strCount.Format(_T("%.0f"), count);

		m_pos_listctrl.SetItemText(nItem, 3, strCost);
		m_pos_listctrl.SetItemText(nItem, 4, strCount);

		if (g_data.GetShowInStatusBar(code))
		{
			m_pos_listctrl.SetItemText(nItem, 5, L"√");
		}
		else
		{
			m_pos_listctrl.SetItemText(nItem, 5, L"");
		}

		nItem++;
	}
	AdjustListColumns(m_pos_listctrl, 1);
}

void CManagerDialog::RefreshCustomList()
{
	m_custom_listctrl.DeleteAllItems();
	size_t groupIdx = (m_current_group_tab >= 2) ? static_cast<size_t>(m_current_group_tab - 2) : 0;
	if (groupIdx < m_data.m_custom_groups.size())
	{
		const auto& codes = m_data.m_custom_groups[groupIdx].codes;
		for (size_t i = 0; i < codes.size(); ++i)
		{
			const auto& code = codes[i];
			std::wstring exch = CCommon::GetExchangeName(code);
			std::wstring pureCode = CCommon::GetPureCode(code);
			std::wstring name = GetStockName(code);

			int nItem = m_custom_listctrl.InsertItem(static_cast<int>(i), exch.c_str());
			m_custom_listctrl.SetItemText(nItem, 1, pureCode.c_str());
			m_custom_listctrl.SetItemText(nItem, 2, name.c_str());

			double low = g_data.GetAlertLowPrice(code);
			double high = g_data.GetAlertHighPrice(code);
			if (low > 0)
			{
				CString lowStr;
				lowStr.Format(_T("%.2f"), low);
				m_custom_listctrl.SetItemText(nItem, 3, lowStr);
			}
			if (high > 0)
			{
				CString highStr;
				highStr.Format(_T("%.2f"), high);
				m_custom_listctrl.SetItemText(nItem, 4, highStr);
			}

			if (g_data.GetShowInStatusBar(code))
			{
				m_custom_listctrl.SetItemText(nItem, 5, L"√");
			}
			else
			{
				m_custom_listctrl.SetItemText(nItem, 5, L"");
			}
		}
	}
	AdjustListColumns(m_custom_listctrl, 2);
}

void CManagerDialog::AdjustListColumns(CListCtrl& list, int tabType)
{
	if (!list.GetSafeHwnd()) return;
	CRect clientRc;
	list.GetClientRect(&clientRc);
	int totalW = clientRc.Width();
	if (totalW <= 0) return;

	if (tabType == 1) // 持仓 (6 列: 交易所, 代码, 股票名称, 成本价, 持股数, 状态栏显示)
	{
		int w0 = max(g_data.DPI(55), totalW * 12 / 100);  // 交易所
		int w1 = max(g_data.DPI(70), totalW * 14 / 100);  // 代码
		int w2 = max(g_data.DPI(110), totalW * 30 / 100); // 股票名称
		int w3 = max(g_data.DPI(65), totalW * 14 / 100);  // 成本价
		int w4 = max(g_data.DPI(65), totalW * 14 / 100);  // 持股数
		int w5 = max(g_data.DPI(70), totalW - (w0 + w1 + w2 + w3 + w4)); // 状态栏显示
		if (w5 < g_data.DPI(50)) w5 = g_data.DPI(50);

		list.SetColumnWidth(0, w0);
		list.SetColumnWidth(1, w1);
		list.SetColumnWidth(2, w2);
		list.SetColumnWidth(3, w3);
		list.SetColumnWidth(4, w4);
		list.SetColumnWidth(5, w5);
	}
	else // 自选股 / 自定义分组 (6 列)
	{
		int w0 = max(g_data.DPI(55), totalW * 14 / 100);
		int w1 = max(g_data.DPI(70), totalW * 16 / 100);
		int w2 = max(g_data.DPI(100), totalW * 28 / 100);
		int w3 = max(g_data.DPI(65), totalW * 14 / 100);
		int w4 = max(g_data.DPI(65), totalW * 14 / 100);
		int w5 = max(g_data.DPI(70), totalW - (w0 + w1 + w2 + w3 + w4));
		if (w5 < g_data.DPI(50)) w5 = g_data.DPI(50);

		list.SetColumnWidth(0, w0);
		list.SetColumnWidth(1, w1);
		list.SetColumnWidth(2, w2);
		list.SetColumnWidth(3, w3);
		list.SetColumnWidth(4, w4);
		list.SetColumnWidth(5, w5);
	}

	// 末列吸收累计误差：回读各列实际生效宽度，把与客户区的差值全部补给
	// 最后一列，确保「状态栏显示」右缘精确贴合列表右边框（消除末列后空隙）
	int applied = 0;
	for (int i = 0; i < 5; ++i)
		applied += list.GetColumnWidth(i);
	int diff = totalW - applied - list.GetColumnWidth(5);
	if (diff != 0)
		list.SetColumnWidth(5, max(g_data.DPI(50), list.GetColumnWidth(5) + diff));
}

void CManagerDialog::SwitchPage(PageIndex page)
{
	if (m_search_dropdown.GetSafeHwnd())
		m_search_dropdown.HidePopup();
	m_current_page = page;
	if (m_current_page == PAGE_GROUPS)
	{
		m_current_group_tab = 0; // 进入分组管理时，默认切到自选股
	}
	m_index_scroll_y = 0;
	m_page_scroll_y = 0;
	UpdateControlsLayout();
	Invalidate();
}

void CManagerDialog::SwitchGroupTab(int tab)
{
	if (m_search_dropdown.GetSafeHwnd())
		m_search_dropdown.HidePopup();
	m_current_group_tab = tab;
	UpdateControlsLayout();
	if (m_current_group_tab == 0)
		RefreshStockList();
	else if (m_current_group_tab == 1)
		RefreshPositionList();
	else
		RefreshCustomList();
	Invalidate();
}

// 指标候选库分组定义（DrawMetricPage 绘制与 MeasureMetricCard2Height 量高共用，保证两处排布一致）
struct MetricGroupDef {
	const wchar_t* groupName;
	std::vector<const wchar_t*> items;
};
static const MetricGroupDef* MetricCandidateGroups(int* count)
{
	static const MetricGroupDef groups[] = {
		{ L"行情量价", { L"总市值", L"成交额", L"成交量", L"量比", L"换手率", L"委比", L"振幅", L"今开", L"昨收", L"最高", L"最低", L"涨停", L"跌停", L"盘后量", L"盘后额" } },
		{ L"估值股本", { L"流通值", L"市盈率(动)", L"市盈率(TTM)", L"市盈率(静)", L"市净率", L"股息率(TTM)", L"总股本", L"流通股" } },
		{ L"ETF与基金", { L"溢价率", L"IOPV净值", L"基金规模" } },
		{ L"财务与区间", { L"每股收益", L"每股净资产", L"52周最高", L"52周最低" } }
	};
	if (count != nullptr)
		*count = _countof(groups);
	return groups;
}

// ===== 方案B：右侧内容区隐藏式滚动（无滚动条，滚轮驱动） =====

// 右侧内容可视区矩形（页头分隔线下方 ~ 底部按钮上方），未含滚动偏移
void CManagerDialog::GetScrollContentRect(CRect& contentRect) const
{
	CRect clientRect;
	GetClientRect(clientRect);
	contentRect = CRect(m_menu_width + g_data.DPI(18), g_data.DPI(72),
		clientRect.Width() - g_data.DPI(18), clientRect.Height() - ContentBottomPad());
}

bool CManagerDialog::InScrollContent(CPoint point)
{
	CRect contentRect;
	GetScrollContentRect(contentRect);
	return contentRect.PtInRect(point) != FALSE;
}

// 钳制滚动偏移到 [0, maxScroll] 并联动原生控件布局与重绘
void CManagerDialog::SetPageScroll(int scrollY)
{
	CRect contentRect;
	GetScrollContentRect(contentRect);
	const int maxScroll = max(0, CalcPageContentHeight() - contentRect.Height());
	const int v = max(0, min(scrollY, maxScroll));
	if (v == m_page_scroll_y)
		return;
	m_page_scroll_y = v;
	UpdateControlsLayout();
	Invalidate();
}

// 当前页虚拟内容自然总高（像素）。返回 0 表示该页不启用通用隐藏式滚动：
// 指数页有自己的滚轮滚动；分组页列表自适应可视区；接口检测/关于页按可视区铺满
int CManagerDialog::CalcPageContentHeight()
{
	switch (m_current_page)
	{
	case PAGE_BASIC:
		// 卡片1(100) + 卡片2(110) → 卡片3透明度(72) → 卡片4SOCKS5(72) → 卡片5重置(76)，加底边距与滚动余量
		return g_data.DPI(394 + 76) + g_data.DPI(40);
	case PAGE_MA:
		return g_data.DPI(MA_CARD1_H + MA_CARD_GAP + MA_CARD2_H + MA_CARD_GAP + MA_CARD3_H + MA_CARD_GAP + MA_CARD4_H) + g_data.DPI(10);
	case PAGE_WEBDAV:
		// 卡片1高206 + 间距10（卡片2顶216）+ 卡片2高160，加底边距
		// （总高与内嵌 480 窗口可视区 394px 对齐，正常情况不出现无意义微滚动）
		return g_data.DPI(216 + 160) + g_data.DPI(10);
	case PAGE_METRICS:
	{
		CRect clientRect;
		GetClientRect(clientRect);
		const int rightWidth = clientRect.Width() - (m_menu_width + g_data.DPI(18)) - g_data.DPI(18);
		// 卡片1高86 + 间距10 + 候选库自然高度
		return g_data.DPI(86 + 10) + MeasureMetricCard2Height(rightWidth) + g_data.DPI(8);
	}
	case PAGE_ABOUT:
		return g_data.DPI(1100);
	default:
		return 0;
	}
}

// 与 DrawMetricPage「候选指标库」完全一致的换行量高（组标题 22 + 芯片行 32+8）
int CManagerDialog::MeasureMetricCard2Height(int rightWidth)
{
	int candGroupCount = 0;
	const MetricGroupDef* candGroups = MetricCandidateGroups(&candGroupCount);

	const int chipH = g_data.DPI(26);
	const int chipGap = g_data.DPI(6);
	const int rowGap = g_data.DPI(6);
	const int leftMargin = g_data.DPI(18);
	const int rightBound = rightWidth - g_data.DPI(18);

	CClientDC dc(this);
	CFont preFont;
	preFont.CreateFont(-g_data.DPI(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont* pOldFont = dc.SelectObject(&preFont);

	int curY = g_data.DPI(38);
	for (int gi = 0; gi < candGroupCount; ++gi)
	{
		curY += g_data.DPI(20);
		int curX = leftMargin;
		for (const wchar_t* item : candGroups[gi].items)
		{
			bool added = std::find(m_data.m_header_metrics.begin(), m_data.m_header_metrics.end(), item) != m_data.m_header_metrics.end();
			CString itemText;
			itemText.Format(L"%s%s", item, added ? L" ✓" : L"");
			const int textW = dc.GetTextExtent(itemText).cx;
			const int chipW = g_data.DPI(20) + textW;
			if (curX + chipW > rightBound)
			{
				curX = leftMargin;
				curY += chipH + rowGap;
			}
			curX += chipW + chipGap;
		}
		// 组间距只在组与组之间追加（最后一组后不追加，避免量高虚增导致无意义微滚动）
		if (gi + 1 < candGroupCount)
			curY += chipH + g_data.DPI(8);
	}

	dc.SelectObject(pOldFont);
	return curY + g_data.DPI(8);
}

// 内容区底部留白：独立弹窗保留「确定/取消」按钮条；内嵌模式无按钮条，
// 分组管理页底部操作按钮行（添加/删除/上移/下移 + 优先展示单选）由
// rightBottom 统一驱动布局，留白归零让列表拉满、按钮行贴近窗口底边
int CManagerDialog::ContentBottomPad() const
{
	if (!m_as_child)
		return g_data.DPI(52);
	return (m_current_page == PAGE_GROUPS) ? 0 : g_data.DPI(14);
}

// 内嵌模式即时提交设置（设置项一改立即生效）；模态模式等待「确定」统一提交
void CManagerDialog::ApplyIfEmbedded()
{
	if (m_as_child)
		ApplySettings();
}

void CManagerDialog::ApplyOpacity(int opacityPercent)
{
	int pct = opacityPercent;
	if (pct < 30) pct = 30; else if (pct > 100) pct = 100;
	m_data.m_window_opacity = pct;
	BYTE alpha = static_cast<BYTE>((pct * 255 + 50) / 100);

	// 1. 通过 Stock 单例通知悬浮窗
	CFloatingWnd* pFloat = Stock::Instance().GetFloatingWnd();
	if (pFloat != nullptr && ::IsWindow(pFloat->GetSafeHwnd()))
	{
		pFloat->UpdateOpacity(pct);
	}

	// 2. 通过系统 API 直接作用于宿主/顶层窗口句柄，双重保障即时生效
	HWND hParent = ::GetParent(m_hWnd);
	if (hParent != NULL && ::IsWindow(hParent))
	{
		::SetWindowLongPtr(hParent, GWL_EXSTYLE, ::GetWindowLongPtr(hParent, GWL_EXSTYLE) | WS_EX_LAYERED);
		::SetLayeredWindowAttributes(hParent, 0, alpha, LWA_ALPHA);
		::RedrawWindow(hParent, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	}
	else if (m_hWnd != NULL && ::IsWindow(m_hWnd))
	{
		::SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, ::GetWindowLongPtr(m_hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
		::SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
		::RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	}

	// 3. 同步写回共享配置
	g_data.m_setting_data.m_window_opacity = pct;
}

void CManagerDialog::UpdateControlsLayout()
{
	CRect clientRect;
	GetClientRect(clientRect);
	if (clientRect.Width() <= 0 || clientRect.Height() <= 0)
		return;

	int rightLeft = m_menu_width + g_data.DPI(18);
	// 方案B：右侧内容区隐藏式滚动 —— 所有内容控件随 m_page_scroll_y 整体平移
	int rightTop = g_data.DPI(72) - m_page_scroll_y;
	int rightWidth = clientRect.Width() - rightLeft - g_data.DPI(18);
	int rightBottom = clientRect.Height() - ContentBottomPad();

	// 基础设置控件列表
	const int basicControlIds[] = {
		IDC_FULL_DAY_CHECK, IDC_SHOW_FLUCTUATION_CHECK, IDC_SHOW_TODAY_PROFIT_CHECK,
		IDC_USE_SOCKS5_PROXY_CHECK,
		IDC_SOCKS5_PROXY_STATIC, IDC_SOCKS5_PROXY_EDIT,
		IDC_KLINE_WIDTH_STATIC, IDC_KLINE_WIDTH_EDIT,
		IDC_KLINE_HEIGHT_STATIC, IDC_KLINE_HEIGHT_EDIT,
		IDC_DISPLAY_AREA_STATIC, IDC_DISPLAY_AREA_COMBO
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
		// 卡片 1: 行情与走势图展示（位置与 DrawBasicPage 卡片严格对应）
		int card1Top = rightTop;
		int chkH = g_data.DPI(22);
		CWnd* pFullDay = GetDlgItem(IDC_FULL_DAY_CHECK);
		if (pFullDay && pFullDay->GetSafeHwnd())
			pFullDay->MoveWindow(rightLeft + g_data.DPI(18), card1Top + g_data.DPI(40), g_data.DPI(165), chkH);

		CWnd* pShowFluc = GetDlgItem(IDC_SHOW_FLUCTUATION_CHECK);
		if (pShowFluc && pShowFluc->GetSafeHwnd())
			pShowFluc->MoveWindow(rightLeft + g_data.DPI(18) + g_data.DPI(195), card1Top + g_data.DPI(40), g_data.DPI(165), chkH);

		CWnd* pTodayProfit = GetDlgItem(IDC_SHOW_TODAY_PROFIT_CHECK);
		if (pTodayProfit && pTodayProfit->GetSafeHwnd())
			pTodayProfit->MoveWindow(rightLeft + g_data.DPI(18), card1Top + g_data.DPI(70), g_data.DPI(115), chkH);

		// 卡片 2: 走势图尺寸与显示位置配置
		int card2Top = card1Top + g_data.DPI(110);
		CWnd* pKWLbl = GetDlgItem(IDC_KLINE_WIDTH_STATIC);
		CWnd* pKHLbl = GetDlgItem(IDC_KLINE_HEIGHT_STATIC);
		CWnd* pPosLbl = GetDlgItem(IDC_DISPLAY_AREA_STATIC);

		int rowTop = card2Top + g_data.DPI(38);
		int rowH = g_data.DPI(26);
		int lblH = g_data.DPI(20);
		int lblY = rowTop + (rowH - lblH) / 2;

		if (pKWLbl && pKWLbl->GetSafeHwnd()) pKWLbl->MoveWindow(rightLeft + g_data.DPI(18), lblY, g_data.DPI(65), lblH);
		PlaceEditInField(IDC_KLINE_WIDTH_EDIT, CRect(rightLeft + g_data.DPI(85), rowTop, rightLeft + g_data.DPI(145), rowTop + rowH));

		if (pKHLbl && pKHLbl->GetSafeHwnd()) pKHLbl->MoveWindow(rightLeft + g_data.DPI(160), lblY, g_data.DPI(65), lblH);
		PlaceEditInField(IDC_KLINE_HEIGHT_EDIT, CRect(rightLeft + g_data.DPI(227), rowTop, rightLeft + g_data.DPI(287), rowTop + rowH));

		int row2Top = card2Top + g_data.DPI(72);
		int lbl2Y = row2Top + (rowH - lblH) / 2;
		if (pPosLbl && pPosLbl->GetSafeHwnd()) pPosLbl->MoveWindow(rightLeft + g_data.DPI(18), lbl2Y, g_data.DPI(65), lblH);
		if (m_display_area_combo.GetSafeHwnd())
		{
			// 保留原生下拉框用于兼容已有序列化逻辑，界面改由下方自绘按钮呈现。
			m_display_area_combo.ShowWindow(SW_HIDE);
		}

		// 卡片 3: 背景透明度调节（自绘，无原生子控件）

		// 卡片 4: SOCKS5 代理网络
		int card4Top = card1Top + g_data.DPI(312);
		CWnd* pProxyChk = GetDlgItem(IDC_USE_SOCKS5_PROXY_CHECK);
		CWnd* pProxyLbl = GetDlgItem(IDC_SOCKS5_PROXY_STATIC);

		int row4Top = card4Top + g_data.DPI(38);
		int lbl4Y = row4Top + (rowH - lblH) / 2;

		if (pProxyChk && pProxyChk->GetSafeHwnd()) pProxyChk->MoveWindow(rightLeft + g_data.DPI(18), lbl4Y, g_data.DPI(135), lblH);
		if (pProxyLbl && pProxyLbl->GetSafeHwnd()) pProxyLbl->MoveWindow(rightLeft + g_data.DPI(160), lbl4Y, g_data.DPI(65), lblH);
		PlaceEditInField(IDC_SOCKS5_PROXY_EDIT, CRect(rightLeft + g_data.DPI(227), row4Top, rightLeft + g_data.DPI(227) + min(g_data.DPI(220), rightWidth - g_data.DPI(245)), row4Top + rowH));
	}

	// 分组管理控件布局
	bool isGroup = (m_current_page == PAGE_GROUPS);
	int listTop = rightTop + g_data.DPI(42);
	int listHeight = rightBottom - listTop - g_data.DPI(44);

	// 「分组排序」按钮：分组管理页头部右上角（红框位置），其他页面隐藏
	// 内嵌子窗口模式下父窗口顶栏按钮（关闭/设置）浮在 y2..22，页头按钮下移避开
	if (m_group_sort_btn.GetSafeHwnd())
	{
		int sortW = g_data.DPI(78);
		int sortTop = m_as_child ? g_data.DPI(26) : g_data.DPI(14);
		m_group_sort_btn.MoveWindow(rightLeft + rightWidth - sortW, sortTop, sortW, g_data.DPI(28));
		m_group_sort_btn.ShowWindow(isGroup ? SW_SHOW : SW_HIDE);
	}

	if (isGroup)
	{
		int searchW = min(g_data.DPI(150), rightWidth / 4);
		int searchH = g_data.DPI(28);
		int searchX = rightLeft + rightWidth - searchW;
		int searchY = rightTop;
		PlaceEditInField(IDC_STOCK_SEARCH_EDIT, CRect(searchX, searchY, searchX + searchW, searchY + searchH));
		if (m_search_edit.GetSafeHwnd())
			m_search_edit.ShowWindow(SW_SHOW);
	}
	else
	{
		if (m_search_edit.GetSafeHwnd())
			m_search_edit.ShowWindow(SW_HIDE);
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.ShowWindow(SW_HIDE);
	}

	m_stock_listctrl.ShowWindow((isGroup && m_current_group_tab == 0) ? SW_SHOW : SW_HIDE);
	m_pos_listctrl.ShowWindow((isGroup && m_current_group_tab == 1) ? SW_SHOW : SW_HIDE);
	m_custom_listctrl.ShowWindow((isGroup && m_current_group_tab >= 2) ? SW_SHOW : SW_HIDE);

	if (isGroup)
	{
		CRect listRect(rightLeft, listTop, rightLeft + rightWidth, listTop + listHeight);
		if (m_current_group_tab == 0 && m_stock_listctrl.GetSafeHwnd())
		{
			m_stock_listctrl.MoveWindow(listRect);
			AdjustListColumns(m_stock_listctrl, 0);
		}
		else if (m_current_group_tab == 1 && m_pos_listctrl.GetSafeHwnd())
		{
			m_pos_listctrl.MoveWindow(listRect);
			AdjustListColumns(m_pos_listctrl, 1);
		}
		else if (m_current_group_tab >= 2 && m_custom_listctrl.GetSafeHwnd())
		{
			m_custom_listctrl.MoveWindow(listRect);
			AdjustListColumns(m_custom_listctrl, 2);
		}

		int btnTop = listTop + listHeight + g_data.DPI(10);
		int btnW = g_data.DPI(72);
		int btnH = g_data.DPI(26);
		int btnGap = g_data.DPI(8);

		// 所有分组都不显示「添加股票」；持仓保留「编辑持仓」，并同样支持上下移动。
		bool isPositionTab = (m_current_group_tab == 1);
		bool showPositionEdit = isPositionTab;
		bool showItemEdit = !isPositionTab;
		bool showOrderBtns = true;
		int nextBtnX = rightLeft;

		m_mgr_add_btn.ShowWindow(showPositionEdit ? SW_SHOW : SW_HIDE);
		if (showPositionEdit)
		{
			m_mgr_add_btn.MoveWindow(nextBtnX, btnTop, btnW, btnH);
			m_mgr_add_btn.SetWindowText(L"编辑持仓");
			nextBtnX += btnW + btnGap;
		}

		m_mgr_del_btn.ShowWindow(SW_SHOW);
		m_mgr_del_btn.MoveWindow(nextBtnX, btnTop, btnW, btnH);
		m_mgr_del_btn.SetWindowText(isPositionTab ? L"清除持仓" : L"删除股票");
		nextBtnX += btnW + btnGap;

		m_mgr_edit_btn.ShowWindow(showItemEdit ? SW_SHOW : SW_HIDE);
		if (showItemEdit)
		{
			m_mgr_edit_btn.MoveWindow(nextBtnX, btnTop, btnW, btnH);
			nextBtnX += btnW + btnGap;
		}

		m_mgr_up_btn.ShowWindow(showOrderBtns ? SW_SHOW : SW_HIDE);
		m_mgr_up_btn.MoveWindow(nextBtnX, btnTop, btnW, btnH);
		nextBtnX += btnW + btnGap;
		m_mgr_down_btn.ShowWindow(showOrderBtns ? SW_SHOW : SW_HIDE);
		m_mgr_down_btn.MoveWindow(nextBtnX, btnTop, btnW, btnH);
		nextBtnX += btnW + btnGap;

		if (m_mgr_del_group_btn.GetSafeHwnd())
		{
			if (m_current_group_tab >= 2)
			{
				m_mgr_del_group_btn.ShowWindow(SW_SHOW);
				m_mgr_del_group_btn.MoveWindow(nextBtnX, btnTop, btnW, btnH);
			}
			else
			{
				m_mgr_del_group_btn.ShowWindow(SW_HIDE);
			}
		}
	}
	else
	{
		m_mgr_add_btn.ShowWindow(SW_HIDE);
		m_mgr_edit_btn.ShowWindow(SW_HIDE);
		m_mgr_del_btn.ShowWindow(SW_HIDE);
		m_mgr_up_btn.ShowWindow(SW_HIDE);
		m_mgr_down_btn.ShowWindow(SW_HIDE);
		if (m_mgr_del_group_btn.GetSafeHwnd())
			m_mgr_del_group_btn.ShowWindow(SW_HIDE);
	}

	// 均线日配置控件布局（卡片位置与 DrawMaPage 的 MA_* 常量严格对应）
	bool isMa = (m_current_page == PAGE_MA);
	m_ma_input_edit.ShowWindow(isMa ? SW_SHOW : SW_HIDE);
	m_ma_add_btn.ShowWindow(isMa ? SW_SHOW : SW_HIDE);

	if (isMa)
	{
		int card2Top = rightTop + g_data.DPI(MA_CARD1_H + MA_CARD_GAP);
		int fieldTop = card2Top + g_data.DPI(MA_FIELD_Y);
		int fieldRight = rightLeft + g_data.DPI(MA_FIELD_X) + g_data.DPI(MA_FIELD_W);
		PlaceEditInField(IDC_MA_INPUT_EDIT, CRect(fieldRight - g_data.DPI(MA_FIELD_W), fieldTop, fieldRight, fieldTop + g_data.DPI(MA_FIELD_H)));
		if (m_ma_add_btn.GetSafeHwnd())
			m_ma_add_btn.MoveWindow(fieldRight + g_data.DPI(12), fieldTop, g_data.DPI(MA_ADDBTN_W), g_data.DPI(MA_FIELD_H));
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
		// 卡片 1: 四行参数输入，行距 40（输入框高 26 + 14 间距），与 DrawWebDavPage 卡片位置一致
		int card1Top = rightTop;
		int lblW = g_data.DPI(80);
		int editW = min(g_data.DPI(330), rightWidth - lblW - g_data.DPI(46));
		int rowY0 = card1Top + g_data.DPI(44);
		int rowStep = g_data.DPI(40);

		const int wdLabelIds[] = { IDC_WEBDAV_URL_STATIC, IDC_WEBDAV_USER_STATIC, IDC_WEBDAV_PWD_STATIC, IDC_WEBDAV_DIR_STATIC };
		const int wdEditIds[] = { IDC_WEBDAV_URL_EDIT, IDC_WEBDAV_USER_EDIT, IDC_WEBDAV_PWD_EDIT, IDC_WEBDAV_DIR_EDIT };
		for (int i = 0; i < 4; ++i)
		{
			CWnd* pLbl = GetDlgItem(wdLabelIds[i]);
			if (pLbl && pLbl->GetSafeHwnd()) pLbl->MoveWindow(rightLeft + g_data.DPI(18), rowY0 + i * rowStep + g_data.DPI(4), lblW, g_data.DPI(20));
			PlaceEditInField(wdEditIds[i], CRect(CPoint(rightLeft + g_data.DPI(18) + lblW + g_data.DPI(10), rowY0 + i * rowStep), CSize(editW, g_data.DPI(26))));
		}

		// 卡片 2: 勾选项 / 操作按钮 / 提示文字分区排布，杜绝重叠
		// （卡片1 高 206 + 卡片间距 10，与 DrawWebDavPage 严格对应）
		int card2Top = card1Top + g_data.DPI(216);
		CWnd* pSyncChk = GetDlgItem(IDC_WEBDAV_AUTO_SYNC_CHECK);
		CWnd* pBakChk = GetDlgItem(IDC_WEBDAV_AUTO_BACKUP_CHECK);
		if (pSyncChk && pSyncChk->GetSafeHwnd()) pSyncChk->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(40), g_data.DPI(300), g_data.DPI(22));
		if (pBakChk && pBakChk->GetSafeHwnd()) pBakChk->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(68), g_data.DPI(300), g_data.DPI(22));

		int wdBtnW = g_data.DPI(88);
		int wdBtnH = g_data.DPI(28);
		int wdGap = g_data.DPI(10);
		CWnd* pTestBtn = GetDlgItem(IDC_WEBDAV_TEST_BTN);
		CWnd* pUpBtn = GetDlgItem(IDC_WEBDAV_UPLOAD_BTN);
		CWnd* pDownBtn = GetDlgItem(IDC_WEBDAV_DOWNLOAD_BTN);
		if (pTestBtn && pTestBtn->GetSafeHwnd()) pTestBtn->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(100), wdBtnW, wdBtnH);
		if (pUpBtn && pUpBtn->GetSafeHwnd()) pUpBtn->MoveWindow(rightLeft + g_data.DPI(18) + (wdBtnW + wdGap), card2Top + g_data.DPI(100), wdBtnW + g_data.DPI(16), wdBtnH);
		if (pDownBtn && pDownBtn->GetSafeHwnd()) pDownBtn->MoveWindow(rightLeft + g_data.DPI(18) + (wdBtnW + wdGap) * 2 + g_data.DPI(16), card2Top + g_data.DPI(100), wdBtnW + g_data.DPI(16), wdBtnH);
	}

	// 「立即重新检测」按钮：接口检测页头部右上角，其他页面隐藏
	// 内嵌子窗口模式下父窗口顶栏按钮（关闭/设置）浮在 y2..22，页头按钮下移避开
	bool isApiHealth = (m_current_page == PAGE_API_HEALTH);
	if (m_api_test_btn.GetSafeHwnd())
	{
		int testW = g_data.DPI(108);
		int testTop = m_as_child ? g_data.DPI(26) : g_data.DPI(14);
		m_api_test_btn.MoveWindow(rightLeft + rightWidth - testW, testTop, testW, g_data.DPI(28));
		m_api_test_btn.ShowWindow(isApiHealth ? SW_SHOW : SW_HIDE);
	}

	// 方案B：隐藏式滚动 —— 原生控件随内容平移后，滚出内容可视区的自动隐藏
	// （GDI+ 绘制由 OnPaint 的裁剪区收口；原生控件无法被父窗口裁剪，只能按可视带显隐）
	{
		const CRect visibleBand(g_data.DPI(72), g_data.DPI(72), clientRect.Width(), clientRect.Height() - g_data.DPI(52));
		auto hideIfOutOfBand = [this, &visibleBand](UINT id) {
			CWnd* pWnd = GetDlgItem(id);
			if (pWnd == nullptr || pWnd->GetSafeHwnd() == nullptr || !pWnd->IsWindowVisible())
				return;
			CRect rc;
			pWnd->GetWindowRect(&rc);
			ScreenToClient(&rc);
			if (rc.top < visibleBand.top - 2 || rc.bottom > visibleBand.bottom + 2)
				pWnd->ShowWindow(SW_HIDE);
		};
		if (m_current_page == PAGE_BASIC)
		{
			for (int id : basicControlIds)
				hideIfOutOfBand(id);
		}
		else if (m_current_page == PAGE_MA)
		{
			hideIfOutOfBand(IDC_MA_INPUT_EDIT);
			hideIfOutOfBand(IDC_MA_ADD_BTN);
		}
		else if (m_current_page == PAGE_WEBDAV)
		{
			for (int id : webdavControlIds)
				hideIfOutOfBand(id);
		}
	}

	// 底部确定与取消按钮（内嵌模式设置即时生效，无确定/取消，按钮条整体隐藏）
	CWnd* pOkBtn = GetDlgItem(IDOK);
	CWnd* pCancelBtn = GetDlgItem(IDCANCEL);
	if (m_as_child)
	{
		if (pOkBtn && pOkBtn->GetSafeHwnd())
			pOkBtn->ShowWindow(SW_HIDE);
		if (pCancelBtn && pCancelBtn->GetSafeHwnd())
			pCancelBtn->ShowWindow(SW_HIDE);
		return;
	}

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

	CRect contentRect(m_menu_width + g_data.DPI(18), g_data.DPI(72), clientRect.Width() - g_data.DPI(18), clientRect.Height() - ContentBottomPad());

	// 方案B：隐藏式滚动 —— 内容整体上移 m_page_scroll_y 并裁剪在内容可视区内（不绘制滚动条）
	CRect drawRect = contentRect;
	const bool scrolled = (m_page_scroll_y > 0) || (CalcPageContentHeight() > contentRect.Height());
	if (scrolled)
	{
		drawRect.top = contentRect.top - m_page_scroll_y;
		drawRect.bottom = max(drawRect.bottom, drawRect.top + CalcPageContentHeight());
		g.SetClip(Gdiplus::RectF(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top),
			static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(contentRect.Height())));
	}

	switch (m_current_page)
	{
	case PAGE_BASIC:
		DrawBasicPage(g, drawRect);
		break;
	case PAGE_INDEX:
		DrawIndexPage(g, drawRect);
		break;
	case PAGE_GROUPS:
		DrawGroupPage(g, drawRect);
		break;
	case PAGE_MA:
		DrawMaPage(g, drawRect);
		break;
	case PAGE_METRICS:
		DrawMetricPage(g, drawRect);
		break;
	case PAGE_WEBDAV:
		DrawWebDavPage(g, drawRect);
		break;
	case PAGE_API_HEALTH:
		DrawApiHealthPage(g, drawRect);
		break;
	case PAGE_ABOUT:
		DrawAboutPage(g, drawRect);
		break;
	default:
		break;
	}

	// 自绘输入框与列表边框（聚焦品牌蓝高亮，失焦暗灰），绘制在卡片之上
	const int borderedEditIds[] = {
		IDC_STOCK_SEARCH_EDIT,
		IDC_KLINE_WIDTH_EDIT, IDC_KLINE_HEIGHT_EDIT, IDC_SOCKS5_PROXY_EDIT,
		IDC_WEBDAV_URL_EDIT, IDC_WEBDAV_USER_EDIT, IDC_WEBDAV_PWD_EDIT, IDC_WEBDAV_DIR_EDIT,
		IDC_MA_INPUT_EDIT
	};
	for (int id : borderedEditIds)
		DrawControlBorder(g, id);
	DrawControlBorder(g, IDC_MGR_LIST);
	DrawControlBorder(g, IDC_POS_LIST);
	DrawControlBorder(g, IDC_CUSTOM_LIST);

	if (scrolled)
		g.ResetClip();

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

	const wchar_t* menuTitles[] = { L"基础设置", L"指数编辑", L"分组管理", L"均线日配置", L"指标栏配置", L"云端备份", L"接口检测", L"关于插件" };
	int menuCount = 8;
	int itemH = g_data.DPI(40);
	int itemTop = g_data.DPI(16);
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
	g.DrawString(L"Stock Plugin v" STOCK_VERSION_STR, -1, &verFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(g_data.DPI(14)), static_cast<Gdiplus::REAL>(clientRect.Height() - g_data.DPI(28))), &verBrush);
}

// 计算与 DrawString 文字墨迹垂直居中的标题竖条 top。
// GDI+ DrawString 的 y 是行框顶，字形墨迹相对它有明显偏移（雅黑粗体约 0.22em），
// 直接按行框摆放竖条会导致蓝条与标题上下错位；这里用路径实测墨迹范围后取中心对齐。
static Gdiplus::REAL CalcTitleBarTop(const Gdiplus::Font& font, const std::wstring& title, int textTop, int barHeight)
{
	// 兜底：按雅黑字形墨迹中心约在 0.71em 处估算
	Gdiplus::REAL fallback = static_cast<Gdiplus::REAL>(textTop) + font.GetSize() * 0.71f - barHeight / 2.0f;
	if (title.empty())
		return fallback;

	Gdiplus::FontFamily family;
	if (font.GetFamily(&family) != Gdiplus::Ok)
		return fallback;

	Gdiplus::GraphicsPath path;
	if (path.AddString(title.c_str(), -1, &family, font.GetStyle(), font.GetSize(),
		Gdiplus::PointF(0.0f, 0.0f), Gdiplus::StringFormat::GenericDefault()) != Gdiplus::Ok)
		return fallback;

	Gdiplus::RectF ink;
	if (path.GetBounds(&ink) != Gdiplus::Ok || ink.Height <= 0.0f)
		return fallback;

	Gdiplus::REAL inkCenter = static_cast<Gdiplus::REAL>(textTop) + ink.Y + ink.Height / 2.0f;
	return inkCenter - barHeight / 2.0f;
}

void CManagerDialog::DrawHeader(Gdiplus::Graphics& g, const CRect& clientRect)
{
	int rightLeft = m_menu_width + g_data.DPI(18);
	int headerTop = g_data.DPI(14);

	const wchar_t* titles[] = { L"基础设置", L"指数编辑", L"分组管理", L"均线日配置", L"指标栏配置", L"云端备份", L"接口检测", L"关于插件" };
	const wchar_t* subs[] = {
		L"配置全天更新、代理网络及走势图尺寸参数",
		L"点击卡片选择展示的指数，前 5 个展示在首页顶部",
		L"管理自选股票列表、持仓配置与自定义分组",
		L"自定义均线周期（最多 5 条；点标签右上角 × 删除；在下方输入添加）",
		L"自定义股票图表顶部指标栏显示内容（最多允许显示 4 项）",
		L"基于 WebDAV 协议在多台电脑间安全备份与同步配置",
		L"实时监测各行情源与数据接口连通状态、延迟及历史心跳",
		L"TrafficMonitor 专业级股票行情监控插件"
	};

	Gdiplus::Font headFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(14)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush headBrush(Gdiplus::Color(255, 241, 245, 249));

	// 标题前的品牌蓝竖条（与卡片章节标题同一视觉语言）：按文字墨迹垂直居中
	Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 37, 99, 235));
	Gdiplus::REAL headBarTop = CalcTitleBarTop(headFont, titles[m_current_page], headerTop, g_data.DPI(16));
	g.FillRectangle(&barBrush, static_cast<Gdiplus::REAL>(rightLeft), headBarTop, static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(g_data.DPI(16)));
	g.DrawString(titles[m_current_page], -1, &headFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(10)), static_cast<Gdiplus::REAL>(headerTop)), &headBrush);

	Gdiplus::Font subFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 148, 163, 184));

	std::wstring subText = subs[m_current_page];
	if (m_current_page == PAGE_INDEX)
	{
		subText = L"点击卡片选择展示的指数，将在主窗口底部状态栏及首页展示 (已选: " + std::to_wstring(m_data.m_selected_indices.size()) + L")";

		const wchar_t* modes[] = { L"全显", L"数字", L"百分比" };
		int modeBtnW = g_data.DPI(56);
		int modeBtnH = g_data.DPI(26);
		int modeTotalW = modeBtnW * 3;
		int modeRight = clientRect.Width() - g_data.DPI(18);
		int modeLeft = modeRight - modeTotalW;
		int modeTop = headerTop + g_data.DPI(2);

		// 分段开关外框底
		Gdiplus::SolidBrush barBg(Gdiplus::Color(255, 24, 27, 34)); // #181B22
		Gdiplus::Pen barBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);
		Gdiplus::RectF barRf(static_cast<Gdiplus::REAL>(modeLeft), static_cast<Gdiplus::REAL>(modeTop), static_cast<Gdiplus::REAL>(modeTotalW), static_cast<Gdiplus::REAL>(modeBtnH));
		g.FillRectangle(&barBg, barRf);
		g.DrawRectangle(&barBorder, barRf);

		Gdiplus::Font modeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::Font modeBoldFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

		Gdiplus::StringFormat sfCenter(Gdiplus::StringFormat::GenericTypographic());
		sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
		sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		sfCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);

		for (int i = 0; i < 3; ++i)
		{
			int bx = modeLeft + i * modeBtnW;
			CRect btnRect(bx, modeTop, bx + modeBtnW, modeTop + modeBtnH);
			m_index_mode_rects[i] = btnRect;

			Gdiplus::RectF btnRf(static_cast<Gdiplus::REAL>(bx), static_cast<Gdiplus::REAL>(modeTop), static_cast<Gdiplus::REAL>(modeBtnW), static_cast<Gdiplus::REAL>(modeBtnH));

			bool isActive = (m_data.m_index_display_mode == i);
			if (isActive)
			{
				Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 37, 99, 235)); // #2563EB
				g.FillRectangle(&activeBg, btnRf);

				Gdiplus::SolidBrush activeTxt(Gdiplus::Color(255, 255, 255, 255));
				g.DrawString(modes[i], -1, &modeBoldFont, btnRf, &sfCenter, &activeTxt);
			}
			else
			{
				if (m_hover_index_mode == i)
				{
					Gdiplus::SolidBrush hoverBg(Gdiplus::Color(255, 38, 42, 54));
					g.FillRectangle(&hoverBg, btnRf);
				}

				if (i > 0 && m_data.m_index_display_mode != (i - 1) && !isActive)
				{
					Gdiplus::Pen sepPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
					g.DrawLine(&sepPen, bx, modeTop + g_data.DPI(4), bx, modeTop + modeBtnH - g_data.DPI(4));
				}

				Gdiplus::SolidBrush inactiveTxt(Gdiplus::Color(255, 148, 163, 184));
				g.DrawString(modes[i], -1, &modeFont, btnRf, &sfCenter, &inactiveTxt);
			}
		}
	}
	else
	{
		for (int i = 0; i < 3; ++i)
			m_index_mode_rects[i].SetRectEmpty();

		if (m_current_page == PAGE_MA)
		{
			subText = L"自定义 K 线图叠加的均线周期，最多 5 条 (1~250)";
		}
		else if (m_current_page == PAGE_METRICS)
		{
			subText = L"自定义股票图表顶部指标栏显示内容，最多允许显示 4 项";
		}
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
	int card1Top = contentRect.top;
	drawCard(card1Top, g_data.DPI(100), L"行情与走势图展示");
	drawCard(contentRect.top + g_data.DPI(110), g_data.DPI(110), L"走势图尺寸与显示位置");
	drawCard(contentRect.top + g_data.DPI(230), g_data.DPI(72), L"背景透明度调节");
	drawCard(contentRect.top + g_data.DPI(312), g_data.DPI(72), L"SOCKS5 代理网络");
	drawCard(contentRect.top + g_data.DPI(394), g_data.DPI(76), L"数据重置");

	// 显示位置按钮与第二行控件对齐：左侧标签后平铺五个固定尺寸选项。
	const wchar_t* displayAreas[] = { L"左上角", L"右上角", L"左下角", L"右下角", L"居中" };
	const int buttonW = g_data.DPI(66);
	const int buttonH = g_data.DPI(26);
	const int buttonGap = g_data.DPI(8);
	const int buttonLeft = rightLeft + g_data.DPI(85);
	const int buttonTop = contentRect.top + g_data.DPI(182);

	Gdiplus::Font areaFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font areaBoldFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::StringFormat areaFormat(Gdiplus::StringFormat::GenericTypographic());
	areaFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
	areaFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	areaFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);

	for (int i = 0; i < 5; ++i)
	{
		int x = buttonLeft + i * (buttonW + buttonGap);
		CRect buttonRect(x, buttonTop, x + buttonW, buttonTop + buttonH);
		m_display_area_rects[i] = buttonRect;
		Gdiplus::RectF buttonRf(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(buttonTop),
			static_cast<Gdiplus::REAL>(buttonW), static_cast<Gdiplus::REAL>(buttonH));

		const bool isSelected = (m_data.m_display_area == i);
		const bool isHovered = (m_hover_display_area == i);
		Gdiplus::SolidBrush buttonBg(isSelected ? Gdiplus::Color(255, 37, 99, 235) :
			(isHovered ? Gdiplus::Color(255, 30, 41, 59) : Gdiplus::Color(255, 13, 15, 21)));
		Gdiplus::Pen buttonBorder(isSelected || isHovered ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 38, 42, 54), 1.0f);
		Gdiplus::SolidBrush buttonText(isSelected || isHovered ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 148, 163, 184));

		g.FillRectangle(&buttonBg, buttonRf);
		g.DrawRectangle(&buttonBorder, buttonRf);
		g.DrawString(displayAreas[i], -1, isSelected ? &areaBoldFont : &areaFont, buttonRf, &areaFormat, &buttonText);
	}

	// 绘制「当天持仓收益」说明文案
	Gdiplus::Font tipFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 148, 163, 184)); // #94A3B8
	g.DrawString(L"（填写持仓后显示当天收益，未填写仍显示涨跌幅）", -1, &tipFont,
		Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(135)), static_cast<Gdiplus::REAL>(card1Top + g_data.DPI(72))), &tipBrush);

	// 绘制「背景透明度调节」卡片控件：标签 + 数值徽章 + 滑块 + 5个预设按钮
	int card3Top = contentRect.top + g_data.DPI(230);
	int opRowTop = card3Top + g_data.DPI(36);
	int opRowH = g_data.DPI(26);

	// 标签 "透明度:"
	Gdiplus::Font opLabelFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush opLabelBrush(Gdiplus::Color(255, 148, 163, 184));
	g.DrawString(L"透明度:", -1, &opLabelFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(18)), static_cast<Gdiplus::REAL>(opRowTop + g_data.DPI(5))), &opLabelBrush);

	// 当前数值徽章 [ 97% ]
	int badgeX = rightLeft + g_data.DPI(68);
	int badgeW = g_data.DPI(42);
	int badgeH = opRowH;
	Gdiplus::RectF badgeRf(static_cast<Gdiplus::REAL>(badgeX), static_cast<Gdiplus::REAL>(opRowTop), static_cast<Gdiplus::REAL>(badgeW), static_cast<Gdiplus::REAL>(badgeH));
	Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 20, 24, 33));
	Gdiplus::Pen badgeBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);
	g.FillRectangle(&badgeBg, badgeRf);
	g.DrawRectangle(&badgeBorder, badgeRf);

	CString opValStr;
	opValStr.Format(L"%d%%", m_data.m_window_opacity);
	Gdiplus::Font badgeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush badgeText(Gdiplus::Color(255, 56, 189, 248)); // #38BDF8
	g.DrawString(opValStr, -1, &badgeFont, badgeRf, &areaFormat, &badgeText);

	// 滑块轨道与滑块
	int sliderLeft = badgeX + badgeW + g_data.DPI(12);
	int sliderW = g_data.DPI(135);
	int sliderH = opRowH;
	m_opacity_slider_rect = CRect(sliderLeft, opRowTop, sliderLeft + sliderW, opRowTop + sliderH);

	int trackY = opRowTop + sliderH / 2;
	int trackH = g_data.DPI(4);
	int curPct = m_data.m_window_opacity;
	if (curPct < 30) curPct = 30; else if (curPct > 100) curPct = 100;
	int thumbX = sliderLeft + (curPct - 30) * sliderW / 70;

	// 轨道底色 (深灰)
	Gdiplus::SolidBrush trackBg(Gdiplus::Color(255, 38, 42, 54));
	g.FillRectangle(&trackBg, static_cast<Gdiplus::REAL>(sliderLeft), static_cast<Gdiplus::REAL>(trackY - trackH / 2),
		static_cast<Gdiplus::REAL>(sliderW), static_cast<Gdiplus::REAL>(trackH));

	// 已选进度 (高亮蓝)
	if (thumbX > sliderLeft)
	{
		Gdiplus::SolidBrush trackActive(Gdiplus::Color(255, 37, 99, 235));
		g.FillRectangle(&trackActive, static_cast<Gdiplus::REAL>(sliderLeft), static_cast<Gdiplus::REAL>(trackY - trackH / 2),
			static_cast<Gdiplus::REAL>(thumbX - sliderLeft), static_cast<Gdiplus::REAL>(trackH));
	}

	// 滑块圆点
	int thumbR = g_data.DPI(6);
	Gdiplus::RectF thumbRf(static_cast<Gdiplus::REAL>(thumbX - thumbR), static_cast<Gdiplus::REAL>(trackY - thumbR),
		static_cast<Gdiplus::REAL>(thumbR * 2), static_cast<Gdiplus::REAL>(thumbR * 2));
	Gdiplus::SolidBrush thumbBg(Gdiplus::Color(255, 255, 255, 255));
	Gdiplus::Pen thumbBorder(m_hover_opacity_slider || m_is_dragging_opacity ? Gdiplus::Color(255, 96, 165, 250) : Gdiplus::Color(255, 37, 99, 235), 2.0f);
	g.FillEllipse(&thumbBg, thumbRf);
	g.DrawEllipse(&thumbBorder, thumbRf);

	// 5 个快捷预设按钮: 100%, 90%, 80%, 70%, 60%
	const int opPresets[5] = { 100, 90, 80, 70, 60 };
	const wchar_t* opPresetNames[5] = { L"100%", L"90%", L"80%", L"70%", L"60%" };
	int presetBtnW = g_data.DPI(46);
	int presetBtnH = opRowH;
	int presetGap = g_data.DPI(6);
	int presetStartLeft = sliderLeft + sliderW + g_data.DPI(14);

	for (int i = 0; i < 5; ++i)
	{
		int px = presetStartLeft + i * (presetBtnW + presetGap);
		CRect prc(px, opRowTop, px + presetBtnW, opRowTop + presetBtnH);
		m_opacity_presets_rects[i] = prc;
		Gdiplus::RectF prf(static_cast<Gdiplus::REAL>(px), static_cast<Gdiplus::REAL>(opRowTop),
			static_cast<Gdiplus::REAL>(presetBtnW), static_cast<Gdiplus::REAL>(presetBtnH));

		const bool isSel = (m_data.m_window_opacity == opPresets[i]);
		const bool isHov = (m_hover_opacity_preset == i);
		Gdiplus::SolidBrush pBg(isSel ? Gdiplus::Color(255, 37, 99, 235) :
			(isHov ? Gdiplus::Color(255, 30, 41, 59) : Gdiplus::Color(255, 13, 15, 21)));
		Gdiplus::Pen pBorder(isSel || isHov ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 38, 42, 54), 1.0f);
		Gdiplus::SolidBrush pText(isSel || isHov ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 148, 163, 184));

		g.FillRectangle(&pBg, prf);
		g.DrawRectangle(&pBorder, prf);
		g.DrawString(opPresetNames[i], -1, isSel ? &areaBoldFont : &areaFont, prf, &areaFormat, &pText);
	}

	// 绘制「数据重置」卡片控件：深色警示扁平按钮 + 说明文案
	int resetCardTop = contentRect.top + g_data.DPI(394);
	int resetBtnW = g_data.DPI(110);
	int resetBtnH = g_data.DPI(26);
	int resetBtnLeft = rightLeft + g_data.DPI(18);
	int resetBtnTop = resetCardTop + g_data.DPI(36);
	m_reset_btn_rect = CRect(resetBtnLeft, resetBtnTop, resetBtnLeft + resetBtnW, resetBtnTop + resetBtnH);

	Gdiplus::RectF resetRf(static_cast<Gdiplus::REAL>(resetBtnLeft), static_cast<Gdiplus::REAL>(resetBtnTop),
		static_cast<Gdiplus::REAL>(resetBtnW), static_cast<Gdiplus::REAL>(resetBtnH));

	const bool isResetHovered = m_hover_reset_btn;
	Gdiplus::SolidBrush resetBg(isResetHovered ? Gdiplus::Color(255, 48, 25, 33) : Gdiplus::Color(255, 24, 27, 34));
	Gdiplus::Pen resetBorder(isResetHovered ? Gdiplus::Color(255, 180, 50, 65) : Gdiplus::Color(255, 56, 62, 78), 1.0f);
	Gdiplus::SolidBrush resetText(isResetHovered ? Gdiplus::Color(255, 255, 100, 100) : Gdiplus::Color(255, 239, 68, 68));

	g.FillRectangle(&resetBg, resetRf);
	g.DrawRectangle(&resetBorder, resetRf);

	Gdiplus::Font resetFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::StringFormat resetFormat(Gdiplus::StringFormat::GenericTypographic());
	resetFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
	resetFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	resetFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);
	g.DrawString(L"重置所有数据", -1, &resetFont, resetRf, &resetFormat, &resetText);

	// 绘制「数据重置」说明文案（紧随按钮右侧，垂直居中对齐）
	g.DrawString(L"（清空所有自选股、持仓记录及本地数据库，恢复初始默认配置）", -1, &tipFont,
		Gdiplus::PointF(static_cast<Gdiplus::REAL>(resetBtnLeft + resetBtnW + g_data.DPI(12)), static_cast<Gdiplus::REAL>(resetBtnTop + g_data.DPI(6))), &tipBrush);
}

namespace
{
	std::wstring FormatIndexCodeDisplay(const std::wstring& code)
	{
		if (code.rfind(L"rt_hk", 0) == 0)
		{
			return L"HK · " + code.substr(5);
		}
		if (code.rfind(L"hk", 0) == 0)
		{
			return L"HK · " + code.substr(2);
		}
		// 东财 secid 形态（118.AUTD / 101.GC00Y 等）：市场号映射可读前缀
		{
			size_t dot = code.find(L'.');
			if (dot != std::wstring::npos && dot > 0)
			{
				bool allDigit = true;
				for (size_t i = 0; i < dot; i++)
				{
					if (!iswdigit(code[i]))
					{
						allDigit = false;
						break;
					}
				}
				if (allDigit)
				{
					std::wstring mkt = code.substr(0, dot);
					std::wstring label = (mkt == L"118") ? L"SGE" : (mkt == L"101") ? L"COMEX"
						: (mkt == L"107") ? L"US" : (mkt == L"116" || mkt == L"123") ? L"HK" : mkt;
					return label + L" · " + code.substr(dot + 1);
				}
			}
		}
		if (code.size() >= 2)
		{
			std::wstring prefix = code.substr(0, 2);
			for (auto& ch : prefix)
				ch = static_cast<wchar_t>(towupper(ch));
			std::wstring suffix = code.substr(2);
			return prefix + L" · " + suffix;
		}
		return code;
	}
}

void CManagerDialog::DrawIndexPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	const auto& presets = GetPresetIndices();
	m_index_card_rects.clear();
	m_index_card_rects.resize(presets.size());

	int cardGapX = g_data.DPI(12);
	int cardGapY = g_data.DPI(10);
	int minCardW = g_data.DPI(185);
	int cols = max(2, (contentRect.Width() + cardGapX) / (minCardW + cardGapX));
	int cardW = (contentRect.Width() - (cardGapX * (cols - 1))) / cols;
	int cardH = g_data.DPI(52);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font codeFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font rankFont(L"Segoe UI", static_cast<Gdiplus::REAL>(g_data.DPI(10.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::StringFormat sfRank(Gdiplus::StringFormat::GenericTypographic());
	sfRank.SetAlignment(Gdiplus::StringAlignmentCenter);
	sfRank.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sfRank.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);

	g.SetClip(Gdiplus::RectF(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top), static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(contentRect.Height())));

	for (size_t i = 0; i < presets.size(); ++i)
	{
		int col = static_cast<int>(i % cols);
		int row = static_cast<int>(i / cols);
		int x = contentRect.left + col * (cardW + cardGapX);
		int y = contentRect.top + row * (cardH + cardGapY) - m_index_scroll_y;

		CRect cardRect(x, y, x + cardW, y + cardH);
		m_index_card_rects[i] = cardRect;

		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y), static_cast<Gdiplus::REAL>(cardW), static_cast<Gdiplus::REAL>(cardH));

		auto it = std::find(m_data.m_selected_indices.begin(), m_data.m_selected_indices.end(), presets[i].code);
		bool isSelected = (it != m_data.m_selected_indices.end());
		int rank = isSelected ? static_cast<int>(std::distance(m_data.m_selected_indices.begin(), it) + 1) : 0;

		std::wstring displayCode = FormatIndexCodeDisplay(presets[i].code);

		if (isSelected)
		{
			Gdiplus::SolidBrush selBg(Gdiplus::Color(255, 28, 45, 75)); // #1C2D4B
			g.FillRectangle(&selBg, rf);

			Gdiplus::Pen borderPen(Gdiplus::Color(255, 37, 99, 235), 1.2f); // #2563EB
			g.DrawRectangle(&borderPen, rf);

			if (rank > 0)
			{
				int badgeSize = g_data.DPI(20);
				int badgeX = x + cardW - badgeSize - g_data.DPI(12);
				int badgeY = y + (cardH - badgeSize) / 2;

				Gdiplus::SolidBrush rankBg(Gdiplus::Color(255, 37, 99, 235));
				g.FillEllipse(&rankBg, badgeX, badgeY, badgeSize, badgeSize);

				Gdiplus::SolidBrush rankTxtBrush(Gdiplus::Color(255, 255, 255, 255));
				std::wstring rankStr = std::to_wstring(rank);
				// 微调 Y 偏移 0.5px 以抵消数字无下延伸部分的视觉下沉，实现完美居中
				Gdiplus::RectF rankRf(
					static_cast<Gdiplus::REAL>(badgeX),
					static_cast<Gdiplus::REAL>(badgeY) - static_cast<Gdiplus::REAL>(g_data.DPI(0.5f)),
					static_cast<Gdiplus::REAL>(badgeSize),
					static_cast<Gdiplus::REAL>(badgeSize)
				);
				g.DrawString(rankStr.c_str(), -1, &rankFont, rankRf, &sfRank, &rankTxtBrush);
			}

			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(presets[i].name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(12)), static_cast<Gdiplus::REAL>(y + g_data.DPI(8))), &nameBrush);

			Gdiplus::SolidBrush codeBrush(Gdiplus::Color(255, 147, 197, 253));
			g.DrawString(displayCode.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(12)), static_cast<Gdiplus::REAL>(y + g_data.DPI(28))), &codeBrush);
		}
		else
		{
			Gdiplus::SolidBrush unselBg(Gdiplus::Color(255, 24, 27, 34)); // #181B22
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen borderPen(static_cast<int>(i) == m_hover_index_card ? Gdiplus::Color(255, 100, 116, 139) : Gdiplus::Color(255, 38, 42, 54), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 226, 232, 240));
			g.DrawString(presets[i].name.c_str(), -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(12)), static_cast<Gdiplus::REAL>(y + g_data.DPI(8))), &nameBrush);

			Gdiplus::SolidBrush codeBrush(Gdiplus::Color(255, 100, 116, 139));
			g.DrawString(displayCode.c_str(), -1, &codeFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(12)), static_cast<Gdiplus::REAL>(y + g_data.DPI(28))), &codeBrush);
		}
	}

	g.ResetClip();
}

struct GroupTabItem
{
	std::wstring text;
	int targetTab; // 0=自选股, 1=持仓, 2..=custom group, -1=+新增分组, -2=dropdown
	bool isActive;
	bool isAddBtn;
	bool isDropdown;
};

static std::vector<GroupTabItem> BuildGroupTabItems(const std::vector<CustomGroup>& customGroups, int currentGroupTab)
{
	std::vector<GroupTabItem> tabs;
	tabs.push_back({ L"自选股", 0, currentGroupTab == 0, false, false });
	tabs.push_back({ L"持仓", 1, currentGroupTab == 1, false, false });

	size_t customCount = customGroups.size();
	if (customCount <= 2)
	{
		for (size_t i = 0; i < customCount; ++i)
		{
			int tabIdx = static_cast<int>(i + 2);
			tabs.push_back({ customGroups[i].name, tabIdx, currentGroupTab == tabIdx, false, false });
		}
	}
	else
	{
		tabs.push_back({ customGroups[0].name, 2, currentGroupTab == 2, false, false });
		tabs.push_back({ customGroups[1].name, 3, currentGroupTab == 3, false, false });

		std::wstring dropText = L"更多分组 ▾";
		if (currentGroupTab >= 4 && (currentGroupTab - 2) < static_cast<int>(customCount))
		{
			dropText = customGroups[currentGroupTab - 2].name + L" ▾";
		}
		tabs.push_back({ dropText, -2, currentGroupTab >= 4, false, true });
	}

	tabs.push_back({ L"+ 新增分组", -1, false, true, false });
	return tabs;
}

void CManagerDialog::DrawGroupPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	std::vector<GroupTabItem> tabs = BuildGroupTabItems(m_data.m_custom_groups, m_current_group_tab);

	int tabCount = static_cast<int>(tabs.size());
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

	int curX = contentRect.left;
	int searchW = min(g_data.DPI(150), contentRect.Width() / 4);
	int maxTabRight = contentRect.right - searchW - g_data.DPI(10);

	for (int i = 0; i < tabCount; ++i)
	{
		Gdiplus::RectF boundBox;
		g.MeasureString(tabs[i].text.c_str(), -1, &tabActiveFont, Gdiplus::PointF(0, 0), &sf, &boundBox);
		int tabW = max(g_data.DPI(65), static_cast<int>(boundBox.Width) + g_data.DPI(20));

		if (curX + tabW > maxTabRight && i < tabCount - 1 && curX > contentRect.left)
		{
			tabW = max(g_data.DPI(50), maxTabRight - curX - tabGap);
		}

		CRect r(curX, tabTop, curX + tabW, tabTop + tabH);
		m_group_tab_rects[i] = r;
		curX += tabW + tabGap;

		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.Width()), static_cast<Gdiplus::REAL>(r.Height()));

		if (tabs[i].isActive)
		{
			Gdiplus::SolidBrush activeBg(Gdiplus::Color(255, 37, 99, 235)); // #2563EB
			g.FillRectangle(&activeBg, rf);

			Gdiplus::SolidBrush txtBrush(Gdiplus::Color(255, 255, 255, 255));
			g.DrawString(tabs[i].text.c_str(), -1, &tabActiveFont, rf, &sf, &txtBrush);
		}
		else if (tabs[i].isAddBtn)
		{
			Gdiplus::SolidBrush unselBg(i == m_hover_group_tab ? Gdiplus::Color(255, 30, 41, 59) : Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen dashPen(i == m_hover_group_tab ? Gdiplus::Color(255, 59, 130, 246) : Gdiplus::Color(255, 51, 65, 85), 1.0f);
			dashPen.SetDashStyle(Gdiplus::DashStyleDash);
			g.DrawRectangle(&dashPen, rf);

			Gdiplus::SolidBrush txtBrush(i == m_hover_group_tab ? Gdiplus::Color(255, 96, 165, 250) : Gdiplus::Color(255, 148, 163, 184));
			g.DrawString(tabs[i].text.c_str(), -1, &tabFont, rf, &sf, &txtBrush);
		}
		else
		{
			Gdiplus::SolidBrush unselBg(i == m_hover_group_tab ? Gdiplus::Color(255, 30, 41, 59) : Gdiplus::Color(255, 24, 27, 34));
			g.FillRectangle(&unselBg, rf);

			Gdiplus::Pen borderPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
			g.DrawRectangle(&borderPen, rf);

			Gdiplus::SolidBrush txtBrush(i == m_hover_group_tab ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 148, 163, 184));
			g.DrawString(tabs[i].text.c_str(), -1, &tabFont, rf, &sf, &txtBrush);
		}
	}

	// ===== 右下角「优先展示」单选框（悬浮窗列表默认分组）：优先展示：○自选股 ○持仓 =====
	m_group_pref_radio_rects.clear();

	Gdiplus::Font radioFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	// 行居中 + 不裁剪：layout rect 宽度给足（GDI+ 按测量宽裁尾字），文字垂直居中与圆圈对齐
	Gdiplus::StringFormat sfRadio;
	sfRadio.SetAlignment(Gdiplus::StringAlignmentNear);
	sfRadio.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sfRadio.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);

	Gdiplus::StringFormat sfMeasure(Gdiplus::StringFormat::GenericTypographic());
	sfMeasure.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip | Gdiplus::StringFormatFlagsNoWrap);
	auto measureRadioText = [&g, &sfMeasure, &radioFont](const wchar_t* text) -> int {
		Gdiplus::RectF bb;
		g.MeasureString(text, -1, &radioFont, Gdiplus::PointF(0, 0), &sfMeasure, &bb);
		return static_cast<int>(bb.Width);
	};

	const wchar_t* radioLabels[2] = { L"自选股", L"持仓" };
	// 与左侧按钮行（删除股票/编辑/上移/下移）垂直居中：UpdateControlsLayout 里
	// btnTop = rightBottom - DPI(34)、btnH = DPI(26)，行中心 = contentRect.bottom - DPI(21)
	int radioCy = contentRect.bottom - g_data.DPI(21);
	int radioHalf = g_data.DPI(14);                       // 项绘制/点击半高
	int radioD = g_data.DPI(13);                          // 圆圈直径
	int radioTextPad = g_data.DPI(6);                     // 圆圈与文字间距
	int labelOptGap = g_data.DPI(10);                     // 「优先展示：」与第一项间距
	int optGap = g_data.DPI(18);                          // 两选项间距

	int labelW = measureRadioText(L"优先展示：");
	int optTextW[2] = { measureRadioText(radioLabels[0]), measureRadioText(radioLabels[1]) };
	int totalW = labelW + labelOptGap
		+ (radioD + radioTextPad + optTextW[0]) + optGap
		+ (radioD + radioTextPad + optTextW[1]);
	int optX = contentRect.right - totalW;

	// 标签「优先展示：」（+1px 抵消 GDI+ 行居中偏上）
	Gdiplus::RectF labelRf(static_cast<Gdiplus::REAL>(optX), static_cast<Gdiplus::REAL>(radioCy - radioHalf + g_data.DPI(1)),
		static_cast<Gdiplus::REAL>(labelW + g_data.DPI(10)), static_cast<Gdiplus::REAL>(radioHalf * 2));
	Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 148, 163, 184));
	g.DrawString(L"优先展示：", -1, &radioFont, labelRf, &sfRadio, &labelBrush);

	// 选项从左到右：[0]=自选股 [1]=持仓（与 m_group_default_tab 取值对应）
	optX += labelW + labelOptGap;
	for (int k = 0; k < 2; ++k)
	{
		int itemW = radioD + radioTextPad + optTextW[k];
		CRect itemRc(optX, radioCy - radioHalf, optX + itemW + g_data.DPI(6), radioCy + radioHalf);
		m_group_pref_radio_rects.push_back(itemRc);

		// 圆圈：选中蓝底内白点，未选中深底描边（与勾选框配色一致）
		int cx = optX + radioD / 2;
		Gdiplus::RectF circleRf(static_cast<Gdiplus::REAL>(cx - radioD / 2), static_cast<Gdiplus::REAL>(radioCy - radioD / 2),
			static_cast<Gdiplus::REAL>(radioD), static_cast<Gdiplus::REAL>(radioD));
		if (m_data.m_group_default_tab == k)
		{
			Gdiplus::SolidBrush selBg(Gdiplus::Color(255, 37, 99, 235));
			g.FillEllipse(&selBg, circleRf);
			int dotD = radioD / 3;
			Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 255, 255, 255));
			g.FillEllipse(&dotBrush, static_cast<Gdiplus::REAL>(cx - dotD / 2), static_cast<Gdiplus::REAL>(radioCy - dotD / 2),
				static_cast<Gdiplus::REAL>(dotD), static_cast<Gdiplus::REAL>(dotD));
		}
		else
		{
			Gdiplus::SolidBrush unselBg(Gdiplus::Color(255, 13, 15, 21));
			g.FillEllipse(&unselBg, circleRf);
			Gdiplus::Pen circlePen(Gdiplus::Color(255, 71, 85, 105), 1.0f);
			g.DrawEllipse(&circlePen, circleRf);
		}

		Gdiplus::RectF textRf(static_cast<Gdiplus::REAL>(optX + radioD + radioTextPad), static_cast<Gdiplus::REAL>(radioCy - radioHalf + g_data.DPI(1)),
			static_cast<Gdiplus::REAL>(optTextW[k] + g_data.DPI(14)), static_cast<Gdiplus::REAL>(radioHalf * 2));
		Gdiplus::SolidBrush txtBrush(m_data.m_group_default_tab == k ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 148, 163, 184));
		g.DrawString(radioLabels[k], -1, &radioFont, textRf, &sfRadio, &txtBrush);

		optX += itemW + optGap;
	}
}

// 均线日配置页：三卡片布局（当前周期 / 添加周期 / 快捷添加）。
// 卡片高度与内部字段位置全部来自文件头的 MA_* 常量，与 UpdateControlsLayout 严格对应。
// 标签/按钮均为直角矩形，文字用 GDI DrawText(DT_VCENTER) 居中，与其它页面的按钮视觉一致。
void CManagerDialog::DrawMaPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	m_ma_tag_rects.clear();
	m_ma_tag_del_rects.clear();
	m_ma_tag_rects.resize(m_data.m_ma_days.size());
	m_ma_tag_del_rects.resize(m_data.m_ma_days.size());
	m_ma_slot_rects.clear();
	m_ma_preset_rects.clear();
	m_boll_vis_check_rects.clear();

	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);

	// GDI 文字工具：与 DrawFlatButton 相同的 DrawText(DT_VCENTER) 居中方式
	auto drawGdiText = [&g](const CRect& rc, const CString& text, CFont& font, COLORREF col, UINT fmt) {
		HDC hdc = g.GetHDC();
		CDC* pDC = CDC::FromHandle(hdc);
		int oldBk = pDC->SetBkMode(TRANSPARENT);
		COLORREF oldCol = pDC->SetTextColor(col);
		CFont* pOld = pDC->SelectObject(&font);
		CRect r(rc);
		pDC->DrawText(text, r, fmt | DT_SINGLELINE | DT_NOPREFIX);
		pDC->SelectObject(pOld);
		pDC->SetTextColor(oldCol);
		pDC->SetBkMode(oldBk);
		g.ReleaseHDC(hdc);
	};

	CFont chipFont;   chipFont.CreateFont(-g_data.DPI(13), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont delFont;    delFont.CreateFont(-g_data.DPI(11), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont plusFont;   plusFont.CreateFont(-g_data.DPI(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont badgeFont;  badgeFont.CreateFont(-g_data.DPI(9), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont preFont;    preFont.CreateFont(-g_data.DPI(11), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont lblFont;    lblFont.CreateFont(-g_data.DPI(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	// ===== 卡片 1: 当前均线周期 =====
	int card1Top = contentRect.top;
	Gdiplus::RectF card1Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card1Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(g_data.DPI(MA_CARD1_H)));
	g.FillRectangle(&cardBg, card1Rf);
	g.DrawRectangle(&cardBorder, card1Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card1Top + g_data.DPI(14), L"当前均线周期");

	// 右上角「已选 n/5」计数徽章（配满时转为警示琥珀色）
	{
		CString cntText;
		cntText.Format(L"已选 %d / %d", static_cast<int>(m_data.m_ma_days.size()), MA_PRESET_MAX);
		int badgeW = g_data.DPI(64);
		int badgeH = g_data.DPI(20);
		CRect badgeRect(rightLeft + rightWidth - g_data.DPI(16) - badgeW, card1Top + g_data.DPI(10),
			rightLeft + rightWidth - g_data.DPI(16), card1Top + g_data.DPI(10) + badgeH);
		Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 13, 15, 21));
		g.FillRectangle(&badgeBg, Gdiplus::RectF(static_cast<Gdiplus::REAL>(badgeRect.left), static_cast<Gdiplus::REAL>(badgeRect.top),
			static_cast<Gdiplus::REAL>(badgeRect.Width()), static_cast<Gdiplus::REAL>(badgeRect.Height())));
		g.DrawRectangle(&cardBorder, Gdiplus::RectF(static_cast<Gdiplus::REAL>(badgeRect.left), static_cast<Gdiplus::REAL>(badgeRect.top),
			static_cast<Gdiplus::REAL>(badgeRect.Width()), static_cast<Gdiplus::REAL>(badgeRect.Height())));

		bool full = (static_cast<int>(m_data.m_ma_days.size()) >= MA_PRESET_MAX);
		drawGdiText(badgeRect, cntText, badgeFont, full ? RGB(245, 158, 11) : RGB(148, 163, 184), DT_CENTER | DT_VCENTER);
	}

	// 周期标签行：直角色块，宽度按文字自适应，右侧方形 × 删除区
	int tagLeft = rightLeft + g_data.DPI(18);
	int tagTop = card1Top + g_data.DPI(40);
	int tagH = g_data.DPI(32);
	int tagGap = g_data.DPI(10);

	Gdiplus::Color tagColors[5];
	for (int k = 0; k < 5; k++)
	{
		COLORREF c = MaIndexColor(k);
		tagColors[k] = Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
	}

	for (size_t i = 0; i < m_data.m_ma_days.size(); ++i)
	{
		int day = m_data.m_ma_days[i];
		CString tagText;
		tagText.Format(L"MA%d", day);

		// 与绘制同源的 GDI 测宽，保证标签宽度与文字一致
		int textW = 0;
		{
			HDC hdc = g.GetHDC();
			CDC* pDC = CDC::FromHandle(hdc);
			CFont* pOld = pDC->SelectObject(&chipFont);
			textW = pDC->GetTextExtent(tagText).cx;
			pDC->SelectObject(pOld);
			g.ReleaseHDC(hdc);
		}
		int tagW = g_data.DPI(12) + textW + g_data.DPI(4) + g_data.DPI(16) + g_data.DPI(6);

		CRect tagRect(tagLeft, tagTop, tagLeft + tagW, tagTop + tagH);
		m_ma_tag_rects[i] = tagRect;

		Gdiplus::RectF tagRf(static_cast<Gdiplus::REAL>(tagLeft), static_cast<Gdiplus::REAL>(tagTop),
			static_cast<Gdiplus::REAL>(tagW), static_cast<Gdiplus::REAL>(tagH));
		Gdiplus::SolidBrush tagBg(tagColors[i % 5]);
		g.FillRectangle(&tagBg, tagRf);

		// 右侧 × 删除区：紧跟文字留 4px，距色块右缘留 6px，悬停深红底
		int delCx = tagLeft + tagW - g_data.DPI(14);
		int delCy = tagTop + tagH / 2;
		int delR = g_data.DPI(7);
		CRect delRect(delCx - delR - g_data.DPI(2), delCy - delR - g_data.DPI(2),
			delCx + delR + g_data.DPI(2), delCy + delR + g_data.DPI(2));
		m_ma_tag_del_rects[i] = delRect;

			if (static_cast<int>(i) == m_hover_ma_tag_del)
			{
				Gdiplus::SolidBrush delHoverBrush(Gdiplus::Color(255, 140, 20, 35));
				g.FillRectangle(&delHoverBrush, delRect.left, delRect.top, delRect.Width(), delRect.Height());
			}
			Icons::Draw(g, Icons::Id::X,
				Gdiplus::RectF(static_cast<Gdiplus::REAL>(delCx - delR), static_cast<Gdiplus::REAL>(delCy - delR),
					static_cast<Gdiplus::REAL>(delR * 2), static_cast<Gdiplus::REAL>(delR * 2)), RGB(255, 255, 255));

		drawGdiText(CRect(tagLeft + g_data.DPI(12), tagTop, delRect.left - g_data.DPI(2), tagTop + tagH),
			tagText, chipFont, RGB(255, 255, 255), DT_LEFT | DT_VCENTER);

		tagLeft += tagW + tagGap;
	}

	// 空槽位：虚线直角框 + “+”，提示剩余容量，点击聚焦输入框
	if (static_cast<int>(m_data.m_ma_days.size()) < MA_PRESET_MAX)
	{
		int slotW = g_data.DPI(54);
		int slotIdx = 0;
		for (int s = static_cast<int>(m_data.m_ma_days.size()); s < MA_PRESET_MAX; ++s, ++slotIdx)
		{
			CRect slotRect(tagLeft, tagTop, tagLeft + slotW, tagTop + tagH);
			m_ma_slot_rects.push_back(slotRect);

			bool slotHover = (slotIdx == m_hover_ma_slot);
			Gdiplus::Pen slotPen(slotHover ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 58, 65, 82), 1.0f);
			slotPen.SetDashStyle(Gdiplus::DashStyleDash);
			g.DrawRectangle(&slotPen, static_cast<Gdiplus::REAL>(tagLeft), static_cast<Gdiplus::REAL>(tagTop),
				static_cast<Gdiplus::REAL>(slotW), static_cast<Gdiplus::REAL>(tagH));

			Icons::Draw(g, Icons::Id::Plus,
					Gdiplus::RectF(static_cast<Gdiplus::REAL>(slotRect.left + g_data.DPI(10)), static_cast<Gdiplus::REAL>(slotRect.top + g_data.DPI(6)),
						static_cast<Gdiplus::REAL>(slotRect.Width() - g_data.DPI(20)), static_cast<Gdiplus::REAL>(slotRect.Height() - g_data.DPI(12))),
					slotHover ? RGB(96, 165, 250) : RGB(75, 85, 99));

			tagLeft += slotW + tagGap;
		}
	}

	// ===== 卡片 2: 添加均线周期 =====
	int card2Top = card1Top + g_data.DPI(MA_CARD1_H + MA_CARD_GAP);
	Gdiplus::RectF card2Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card2Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(g_data.DPI(MA_CARD2_H)));
	g.FillRectangle(&cardBg, card2Rf);
	g.DrawRectangle(&cardBorder, card2Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card2Top + g_data.DPI(14), L"添加均线周期");

	int fieldTop = card2Top + g_data.DPI(MA_FIELD_Y);
	drawGdiText(CRect(rightLeft + g_data.DPI(18), fieldTop, rightLeft + g_data.DPI(MA_FIELD_X) - g_data.DPI(10), fieldTop + g_data.DPI(MA_FIELD_H)),
		L"均线天数 (1~250)：", lblFont, RGB(148, 163, 184), DT_LEFT | DT_VCENTER);

	// ===== 卡片 3: 快捷添加常用周期 =====
	int card3Top = card1Top + g_data.DPI(MA_CARD1_H + MA_CARD_GAP + MA_CARD2_H + MA_CARD_GAP);
	Gdiplus::RectF card3Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card3Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(g_data.DPI(MA_CARD3_H)));
	g.FillRectangle(&cardBg, card3Rf);
	g.DrawRectangle(&cardBorder, card3Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card3Top + g_data.DPI(14), L"快捷添加常用周期");

	int preTop = card3Top + g_data.DPI(38);
	int preH = g_data.DPI(26);
	int preGap = g_data.DPI(6);
	int preLeft = rightLeft + g_data.DPI(18);

	for (int preIdx = 0; preIdx < static_cast<int>(_countof(kMaPresetDays)); ++preIdx)
	{
		int day = kMaPresetDays[preIdx];
		bool added = std::find(m_data.m_ma_days.begin(), m_data.m_ma_days.end(), day) != m_data.m_ma_days.end();
		CString preText;
		preText.Format(L"MA%d%s", day, added ? L" ✓" : L"");

		int textW = 0;
		{
			HDC hdc = g.GetHDC();
			CDC* pDC = CDC::FromHandle(hdc);
			CFont* pOld = pDC->SelectObject(&preFont);
			textW = pDC->GetTextExtent(preText).cx;
			pDC->SelectObject(pOld);
			g.ReleaseHDC(hdc);
		}
		int preW = g_data.DPI(16) + textW;

		CRect preRect(preLeft, preTop, preLeft + preW, preTop + preH);
		m_ma_preset_rects.push_back(preRect);

		Gdiplus::RectF preRf(static_cast<Gdiplus::REAL>(preLeft), static_cast<Gdiplus::REAL>(preTop),
			static_cast<Gdiplus::REAL>(preW), static_cast<Gdiplus::REAL>(preH));

		bool hot = (!added && preIdx == m_hover_ma_preset);
		if (hot)
		{
			Gdiplus::SolidBrush preHotBg(Gdiplus::Color(255, 30, 41, 59));
			g.FillRectangle(&preHotBg, preRf);
			Gdiplus::Pen preHotPen(Gdiplus::Color(255, 37, 99, 235), 1.0f);
			g.DrawRectangle(&preHotPen, preRf);
			drawGdiText(preRect, preText, preFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER);
		}
		else
		{
			Gdiplus::SolidBrush preBg(Gdiplus::Color(255, 13, 15, 21));
			g.FillRectangle(&preBg, preRf);
			g.DrawRectangle(&cardBorder, preRf);
			drawGdiText(preRect, preText, preFont, added ? RGB(110, 120, 138) : RGB(148, 163, 184), DT_CENTER | DT_VCENTER);
		}

		preLeft += preW + preGap;
	}

	// ===== 卡片 4: 分时图布林带显示 =====
	// 单行三复选框（上轨红/中轨蓝/下轨绿），色块与分时图布林虚线颜色一一对应；不设总开关。
	int card4Top = card1Top + g_data.DPI(MA_CARD1_H + MA_CARD_GAP + MA_CARD2_H + MA_CARD_GAP + MA_CARD3_H + MA_CARD_GAP);
	Gdiplus::RectF card4Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card4Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(g_data.DPI(MA_CARD4_H)));
	g.FillRectangle(&cardBg, card4Rf);
	g.DrawRectangle(&cardBorder, card4Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card4Top + g_data.DPI(14), L"分时图布林带显示");

	CFont chkFont;    chkFont.CreateFont(-g_data.DPI(12), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));

	// 直角复选框：选中蓝底白勾，未选中深底描边
	auto drawVisCheckBox = [&](int cx, int cy, bool checked) {
		int half = g_data.DPI(7);
		CRect rc(cx - half, cy - half, cx + half, cy + half);
		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
			static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
		if (checked)
		{
			Gdiplus::SolidBrush bg(Gdiplus::Color(255, 37, 99, 235));
			g.FillRectangle(&bg, rf);
			Icons::Draw(g, Icons::Id::Check,
					Gdiplus::RectF(static_cast<Gdiplus::REAL>(rc.left + g_data.DPI(2)), static_cast<Gdiplus::REAL>(rc.top + g_data.DPI(2)),
						static_cast<Gdiplus::REAL>(rc.Width() - g_data.DPI(4)), static_cast<Gdiplus::REAL>(rc.Height() - g_data.DPI(4))), RGB(255, 255, 255));
		}
		else
		{
			Gdiplus::SolidBrush bg(Gdiplus::Color(255, 13, 15, 21));
			g.FillRectangle(&bg, rf);
			g.DrawRectangle(&cardBorder, rf);
		}
	};

	// 单行三复选框：色块颜色与 TimelineChart 绘制 Pen RGB 一致（上轨红/中轨蓝/下轨绿）
	struct BollVisItem
	{
		const wchar_t* label;
		COLORREF color;
		bool visible;
	};
	const BollVisItem bollItems[3] = {
		{ L"上轨", RGB(248, 113, 113), m_data.m_boll_upper_visible },
		{ L"中轨", RGB(96, 165, 250), m_data.m_boll_mid_visible },
		{ L"下轨", RGB(52, 211, 153), m_data.m_boll_lower_visible },
	};

	int rowH = g_data.DPI(26);
	int cy = card4Top + g_data.DPI(52);
	int curX = rightLeft + g_data.DPI(24);
	int itemGap = g_data.DPI(14);

	for (int k = 0; k < 3; ++k)
	{
		CString txt;
		txt.Format(L"%s", bollItems[k].label);
		int textW = 0;
		{
			HDC hdc = g.GetHDC();
			CDC* pDC = CDC::FromHandle(hdc);
			CFont* pOld = pDC->SelectObject(&chkFont);
			textW = pDC->GetTextExtent(txt).cx;
			pDC->SelectObject(pOld);
			g.ReleaseHDC(hdc);
		}

		int itemW = g_data.DPI(22) + g_data.DPI(6) + g_data.DPI(12) + g_data.DPI(6) + textW;
		CRect item(curX, cy - rowH / 2, curX + itemW, cy + rowH / 2);
		m_boll_vis_check_rects.push_back(item);

		drawVisCheckBox(curX + g_data.DPI(10), cy, bollItems[k].visible);

		Gdiplus::SolidBrush swatch(Gdiplus::Color(255, GetRValue(bollItems[k].color), GetGValue(bollItems[k].color), GetBValue(bollItems[k].color)));
		int sw = g_data.DPI(12);
		int swx = curX + g_data.DPI(22) + g_data.DPI(6);
		g.FillRectangle(&swatch, static_cast<Gdiplus::REAL>(swx), static_cast<Gdiplus::REAL>(cy - sw / 2),
			static_cast<Gdiplus::REAL>(sw), static_cast<Gdiplus::REAL>(sw));

		COLORREF txtCol = bollItems[k].visible ? RGB(241, 245, 249) : RGB(148, 163, 184);
		drawGdiText(CRect(swx + sw + g_data.DPI(6), cy - rowH / 2, curX + itemW, cy + rowH / 2),
			txt, chkFont, txtCol, DT_LEFT | DT_VCENTER);

		curX += itemW + itemGap;
	}
}

void CManagerDialog::DrawMetricPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);

	m_metric_tag_rects.clear();
	m_metric_tag_del_rects.clear();
	m_metric_tag_rects.resize(m_data.m_header_metrics.size());
	m_metric_tag_del_rects.resize(m_data.m_header_metrics.size());
	m_metric_slot_rects.clear();
	m_metric_candidates.clear();

	CFont chipFont;   chipFont.CreateFont(-g_data.DPI(12), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont delFont;    delFont.CreateFont(-g_data.DPI(11), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont plusFont;   plusFont.CreateFont(-g_data.DPI(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont badgeFont;  badgeFont.CreateFont(-g_data.DPI(9), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont preFont;    preFont.CreateFont(-g_data.DPI(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont grpFont;    grpFont.CreateFont(-g_data.DPI(10), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));

	auto drawGdiText = [&](const CRect& rc, const CString& txt, CFont& fnt, COLORREF col, UINT fmt) {
		HDC hdc = g.GetHDC();
		CDC* pDC = CDC::FromHandle(hdc);
		CFont* pOld = pDC->SelectObject(&fnt);
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(col);
		CRect tempRc = rc;
		pDC->DrawText(txt, &tempRc, fmt | DT_SINGLELINE);
		pDC->SelectObject(pOld);
		g.ReleaseHDC(hdc);
	};

	// ===== 卡片 1: 当前显示指标 =====
	int card1Top = contentRect.top;
	int card1H = g_data.DPI(86);
	Gdiplus::RectF card1Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card1Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card1H));
	g.FillRectangle(&cardBg, card1Rf);
	g.DrawRectangle(&cardBorder, card1Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card1Top + g_data.DPI(14), L"当前显示指标");

	// 右上角 "已选 N / 4" 徽标
	{
		CString cntText;
		cntText.Format(L"已选 %d / 4", static_cast<int>(m_data.m_header_metrics.size()));
		int badgeW = g_data.DPI(64);
		int badgeH = g_data.DPI(20);
		CRect badgeRect(rightLeft + rightWidth - g_data.DPI(16) - badgeW, card1Top + g_data.DPI(10),
			rightLeft + rightWidth - g_data.DPI(16), card1Top + g_data.DPI(10) + badgeH);
		Gdiplus::SolidBrush badgeBg(Gdiplus::Color(255, 13, 15, 21));
		g.FillRectangle(&badgeBg, Gdiplus::RectF(static_cast<Gdiplus::REAL>(badgeRect.left), static_cast<Gdiplus::REAL>(badgeRect.top),
			static_cast<Gdiplus::REAL>(badgeRect.Width()), static_cast<Gdiplus::REAL>(badgeRect.Height())));
		g.DrawRectangle(&cardBorder, Gdiplus::RectF(static_cast<Gdiplus::REAL>(badgeRect.left), static_cast<Gdiplus::REAL>(badgeRect.top),
			static_cast<Gdiplus::REAL>(badgeRect.Width()), static_cast<Gdiplus::REAL>(badgeRect.Height())));

		bool full = (static_cast<int>(m_data.m_header_metrics.size()) >= 4);
		drawGdiText(badgeRect, cntText, badgeFont, full ? RGB(245, 158, 11) : RGB(148, 163, 184), DT_CENTER | DT_VCENTER);
	}

	int tagLeft = rightLeft + g_data.DPI(18);
	int tagTop = card1Top + g_data.DPI(40);
	int tagH = g_data.DPI(32);
	int tagGap = g_data.DPI(10);

	Gdiplus::Color tagColors[4];
	for (int k = 0; k < 4; k++)
	{
		COLORREF c = MaIndexColor(k);
		tagColors[k] = Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
	}

	for (size_t i = 0; i < m_data.m_header_metrics.size(); ++i)
	{
		const std::wstring& name = m_data.m_header_metrics[i];
		CString tagText(name.c_str());

		int textW = 0;
		{
			HDC hdc = g.GetHDC();
			CDC* pDC = CDC::FromHandle(hdc);
			CFont* pOld = pDC->SelectObject(&chipFont);
			textW = pDC->GetTextExtent(tagText).cx;
			pDC->SelectObject(pOld);
			g.ReleaseHDC(hdc);
		}
		int tagW = g_data.DPI(12) + textW + g_data.DPI(4) + g_data.DPI(16) + g_data.DPI(6);

		CRect tagRect(tagLeft, tagTop, tagLeft + tagW, tagTop + tagH);
		m_metric_tag_rects[i] = tagRect;

		Gdiplus::RectF tagRf(static_cast<Gdiplus::REAL>(tagLeft), static_cast<Gdiplus::REAL>(tagTop),
			static_cast<Gdiplus::REAL>(tagW), static_cast<Gdiplus::REAL>(tagH));
		Gdiplus::SolidBrush tagBg(tagColors[i % 4]);
		g.FillRectangle(&tagBg, tagRf);

		// 右侧 × 删除区
		int delCx = tagLeft + tagW - g_data.DPI(14);
		int delCy = tagTop + tagH / 2;
		int delR = g_data.DPI(7);
		CRect delRect(delCx - delR - g_data.DPI(2), delCy - delR - g_data.DPI(2),
			delCx + delR + g_data.DPI(2), delCy + delR + g_data.DPI(2));
		m_metric_tag_del_rects[i] = delRect;

			if (static_cast<int>(i) == m_hover_metric_tag_del)
			{
				Gdiplus::SolidBrush delHoverBrush(Gdiplus::Color(255, 140, 20, 35));
				g.FillRectangle(&delHoverBrush, delRect.left, delRect.top, delRect.Width(), delRect.Height());
			}
			Icons::Draw(g, Icons::Id::X,
				Gdiplus::RectF(static_cast<Gdiplus::REAL>(delCx - delR), static_cast<Gdiplus::REAL>(delCy - delR),
					static_cast<Gdiplus::REAL>(delR * 2), static_cast<Gdiplus::REAL>(delR * 2)), RGB(255, 255, 255));

		drawGdiText(CRect(tagLeft + g_data.DPI(12), tagTop, delRect.left - g_data.DPI(2), tagTop + tagH),
			tagText, chipFont, RGB(255, 255, 255), DT_LEFT | DT_VCENTER);

		tagLeft += tagW + tagGap;
	}

	// 空槽位：虚线直角框 + “+”，提示剩余容量
	if (static_cast<int>(m_data.m_header_metrics.size()) < 4)
	{
		int slotW = g_data.DPI(54);
		int slotIdx = 0;
		for (int s = static_cast<int>(m_data.m_header_metrics.size()); s < 4; ++s, ++slotIdx)
		{
			CRect slotRect(tagLeft, tagTop, tagLeft + slotW, tagTop + tagH);
			m_metric_slot_rects.push_back(slotRect);

			bool slotHover = (slotIdx == m_hover_metric_slot);
			Gdiplus::Pen slotPen(slotHover ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 58, 65, 82), 1.0f);
			slotPen.SetDashStyle(Gdiplus::DashStyleDash);
			g.DrawRectangle(&slotPen, static_cast<Gdiplus::REAL>(tagLeft), static_cast<Gdiplus::REAL>(tagTop),
				static_cast<Gdiplus::REAL>(slotW), static_cast<Gdiplus::REAL>(tagH));

			Icons::Draw(g, Icons::Id::Plus,
					Gdiplus::RectF(static_cast<Gdiplus::REAL>(slotRect.left + g_data.DPI(10)), static_cast<Gdiplus::REAL>(slotRect.top + g_data.DPI(6)),
						static_cast<Gdiplus::REAL>(slotRect.Width() - g_data.DPI(20)), static_cast<Gdiplus::REAL>(slotRect.Height() - g_data.DPI(12))),
					slotHover ? RGB(96, 165, 250) : RGB(75, 85, 99));

			tagLeft += slotW + tagGap;
		}
	}

	// ===== 卡片 2: 候选指标库 =====
	// 自然高度取「按宽度换行排布的实际高度」与「撑满可视区」的较大值：
	// 大窗口保持原有的撑满观感；开启隐藏式滚动后随内容自然收口
	int card2Top = card1Top + card1H + g_data.DPI(10);
	int card2H = max(contentRect.bottom - card2Top, MeasureMetricCard2Height(rightWidth));
	Gdiplus::RectF card2Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card2Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card2H));
	g.FillRectangle(&cardBg, card2Rf);
	g.DrawRectangle(&cardBorder, card2Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card2Top + g_data.DPI(14), L"候选指标库");

	int candGroupCount = 0;
	const MetricGroupDef* candGroups = MetricCandidateGroups(&candGroupCount);

	int curY = card2Top + g_data.DPI(38);
	int chipH = g_data.DPI(26);
	int chipGap = g_data.DPI(6);
	int rowGap = g_data.DPI(6);
	int rightBound = rightLeft + rightWidth - g_data.DPI(18);

	for (int gi = 0; gi < candGroupCount; ++gi)
	{
		const MetricGroupDef& grp = candGroups[gi];
		CString grpTitle;
		grpTitle.Format(L"● %s", grp.groupName);
		drawGdiText(CRect(rightLeft + g_data.DPI(18), curY, rightBound, curY + g_data.DPI(18)),
			grpTitle, grpFont, RGB(96, 165, 250), DT_LEFT | DT_VCENTER);
		curY += g_data.DPI(20);

		int curX = rightLeft + g_data.DPI(18);
		for (const wchar_t* item : grp.items)
		{
			bool added = std::find(m_data.m_header_metrics.begin(), m_data.m_header_metrics.end(), item) != m_data.m_header_metrics.end();
			CString itemText;
			itemText.Format(L"%s%s", item, added ? L" ✓" : L"");

			int textW = 0;
			{
				HDC hdc = g.GetHDC();
				CDC* pDC = CDC::FromHandle(hdc);
				CFont* pOld = pDC->SelectObject(&preFont);
				textW = pDC->GetTextExtent(itemText).cx;
				pDC->SelectObject(pOld);
				g.ReleaseHDC(hdc);
			}
			int chipW = g_data.DPI(20) + textW;
			if (curX + chipW > rightBound)
			{
				curX = rightLeft + g_data.DPI(18);
				curY += chipH + rowGap;
			}

			CRect chipRect(curX, curY, curX + chipW, curY + chipH);
			int candIdx = static_cast<int>(m_metric_candidates.size());
			m_metric_candidates.push_back({ item, chipRect });

			Gdiplus::RectF chipRf(static_cast<Gdiplus::REAL>(curX), static_cast<Gdiplus::REAL>(curY),
				static_cast<Gdiplus::REAL>(chipW), static_cast<Gdiplus::REAL>(chipH));

			bool hot = (candIdx == m_hover_metric_preset);
			if (added)
			{
				Gdiplus::SolidBrush selBg(Gdiplus::Color(255, 28, 45, 75)); // #1C2D4B
				g.FillRectangle(&selBg, chipRf);
				Gdiplus::Pen selPen(Gdiplus::Color(255, 37, 99, 235), 1.0f);
				g.DrawRectangle(&selPen, chipRf);
				drawGdiText(chipRect, itemText, preFont, RGB(147, 197, 253), DT_CENTER | DT_VCENTER);
			}
			else if (hot)
			{
				Gdiplus::SolidBrush hotBg(Gdiplus::Color(255, 30, 41, 59));
				g.FillRectangle(&hotBg, chipRf);
				Gdiplus::Pen hotPen(Gdiplus::Color(255, 37, 99, 235), 1.0f);
				g.DrawRectangle(&hotPen, chipRf);
				drawGdiText(chipRect, itemText, preFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER);
			}
			else
			{
				Gdiplus::SolidBrush normalBg(Gdiplus::Color(255, 13, 15, 21));
				g.FillRectangle(&normalBg, chipRf);
				g.DrawRectangle(&cardBorder, chipRf);
				drawGdiText(chipRect, itemText, preFont, RGB(148, 163, 184), DT_CENTER | DT_VCENTER);
			}

			curX += chipW + chipGap;
		}
		curY += chipH + g_data.DPI(8);
	}
}

void CManagerDialog::DrawWebDavPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int rightLeft = contentRect.left;
	int rightWidth = contentRect.Width();

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));
	Gdiplus::Pen cardBorder(Gdiplus::Color(255, 38, 42, 54), 1.0f);

	// 卡片 1: WebDAV 服务器参数（高度与 UpdateControlsLayout 的四行输入严格对应）
	int card1Top = contentRect.top;
	int card1H = g_data.DPI(206);
	Gdiplus::RectF card1Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card1Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card1H));
	g.FillRectangle(&cardBg, card1Rf);
	g.DrawRectangle(&cardBorder, card1Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card1Top + g_data.DPI(12), L"WebDAV 服务器参数");

	// 卡片 2: 同步与备份操作（勾选项/操作按钮/提示文字分区块排布，互不重叠）
	int card2Top = card1Top + g_data.DPI(216);
	int card2H = g_data.DPI(160);
	Gdiplus::RectF card2Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card2Top), static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(card2H));
	g.FillRectangle(&cardBg, card2Rf);
	g.DrawRectangle(&cardBorder, card2Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card2Top + g_data.DPI(12), L"同步与备份操作");

	// 提示文字与基础设置页说明文案同字号（12px）
	Gdiplus::Font tipFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 148, 163, 184));
	int tipY = card2Top + g_data.DPI(140);

	g.DrawString(L"提示：每次备份以时间戳独立存档（云端保留最近 30 份），恢复时可在历史备份列表中任选一份。", -1, &tipFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(18)), static_cast<Gdiplus::REAL>(tipY)), &tipBrush);

	// 上次同步时间放在卡片 2 标题行右端，避免与左侧提示文字挤在同一行
	if (!m_data.m_webdav_last_sync_time.empty())
	{
		std::wstring timeStr = L"上次同步: " + m_data.m_webdav_last_sync_time;
		Gdiplus::SolidBrush succBrush(Gdiplus::Color(255, 14, 203, 129));
		Gdiplus::RectF bounds;
		g.MeasureString(timeStr.c_str(), -1, &tipFont, Gdiplus::PointF(0, 0), &bounds);
		g.DrawString(timeStr.c_str(), -1, &tipFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + rightWidth - g_data.DPI(18) - bounds.Width), static_cast<Gdiplus::REAL>(card2Top + g_data.DPI(12))), &succBrush);
	}
}

void CManagerDialog::DrawApiHealthPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	auto sources = CApiHealthManager::Instance().GetSnapshot();
	if (sources.empty())
		return;

	int srcCount = static_cast<int>(sources.size());
	int gap = g_data.DPI(10);
	int totalH = contentRect.Height();
	int cardH = (totalH - (srcCount - 1) * gap) / srcCount;
	int cardW = contentRect.Width();

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font roleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font statFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font logFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

	Gdiplus::SolidBrush cardBg(Gdiplus::Color(255, 24, 27, 34));       // #181B22
	Gdiplus::Pen cardPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);        // #262A36
	Gdiplus::SolidBrush textWhite(Gdiplus::Color(255, 241, 245, 249));
	Gdiplus::SolidBrush textMuted(Gdiplus::Color(255, 156, 172, 192));
	Gdiplus::SolidBrush textSub(Gdiplus::Color(255, 100, 116, 139));

	// 心跳条颜色
	Gdiplus::SolidBrush brOk(Gdiplus::Color(255, 16, 185, 129));        // 翠绿 #10B981
	Gdiplus::SolidBrush brWarn(Gdiplus::Color(255, 245, 158, 11));      // 暖黄 #F59E0B
	Gdiplus::SolidBrush brFail(Gdiplus::Color(255, 239, 68, 68));       // 赤红 #EF4444
	Gdiplus::SolidBrush brEmpty(Gdiplus::Color(255, 37, 41, 54));       // 空槽 #252936

	Gdiplus::StringFormat sfNear;
	sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
	sfNear.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	Gdiplus::StringFormat sfFar;
	sfFar.SetAlignment(Gdiplus::StringAlignmentFar);
	sfFar.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	Gdiplus::StringFormat sfCenter;
	sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
	sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	Gdiplus::StringFormat sfLog;
	sfLog.SetAlignment(Gdiplus::StringAlignmentNear);
	sfLog.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sfLog.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
	sfLog.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

	int curY = contentRect.top;

	for (int sIdx = 0; sIdx < srcCount; ++sIdx)
	{
		const auto& src = sources[sIdx];
		CRect cardRect(contentRect.left, curY, contentRect.left + cardW, curY + cardH);
		Gdiplus::RectF cardRf(static_cast<Gdiplus::REAL>(cardRect.left), static_cast<Gdiplus::REAL>(cardRect.top),
			static_cast<Gdiplus::REAL>(cardRect.Width()), static_cast<Gdiplus::REAL>(cardRect.Height()));

		// 1. 卡片背景与边框
		g.FillRectangle(&cardBg, cardRf);
		g.DrawRectangle(&cardPen, cardRf);

		// 左侧重点色边条（正常显示翠绿，异常显示红色，警告显示黄色）
		Gdiplus::Color accentColor(255, 16, 185, 129);
		if (src.history.empty() || src.history.back().level == LEVEL_FAIL)
			accentColor = Gdiplus::Color(255, 239, 68, 68);
		else if (src.history.back().level == LEVEL_WARN)
			accentColor = Gdiplus::Color(255, 245, 158, 11);
		else
			accentColor = Gdiplus::Color(255, 16, 185, 129);

		Gdiplus::SolidBrush accentBrush(accentColor);
		g.FillRectangle(&accentBrush, static_cast<Gdiplus::REAL>(cardRect.left), static_cast<Gdiplus::REAL>(cardRect.top),
			static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(cardRect.Height()));

		// 卡片内部元素垂直排版：行高收紧 + 行距随卡片高度自适应，
		// 第三行（职责说明/最近采样）钳制在卡片底边内，杜绝小卡片时文字出框
		int padX = g_data.DPI(16);
		int row1H = g_data.DPI(20); // 第一行：标题 + 统计 + 状态胶囊
		int barH  = g_data.DPI(14); // 第二行：心跳条
		int row3H = g_data.DPI(18); // 第三行：职责说明 + 最近采样日志
		int contentTotalH = row1H + barH + row3H;
		int gapY = max(g_data.DPI(2), (cardH - contentTotalH) / 4);

		int row1Y = cardRect.top + gapY;
		int barY  = row1Y + row1H + gapY;
		int row3Y = min(barY + barH + gapY, cardRect.bottom - row3H - g_data.DPI(2));

		// 2. 第一行：接口名 + 右侧统计数据 + 状态胶囊
		int textX = cardRect.left + padX;
		int rightBlockX = cardRect.right - padX;

		// 状态文字：无容器，纯彩色文字右对齐（与统计同字号同基线）
		Gdiplus::Color tagTxtCol;
		std::wstring tagText;
		HeartbeatLevel curLvl = src.history.empty() ? LEVEL_OK : src.history.back().level;
		if (curLvl == LEVEL_OK)
		{
			tagTxtCol = Gdiplus::Color(255, 52, 211, 153);
			tagText = L"● 运行正常";
		}
		else if (curLvl == LEVEL_WARN)
		{
			tagTxtCol = Gdiplus::Color(255, 251, 191, 36);
			tagText = L"● 响应偏慢";
		}
		else
		{
			tagTxtCol = Gdiplus::Color(255, 248, 113, 113);
			tagText = L"● 受限/异常";
		}

		int statusW = 0;
		{
			Gdiplus::RectF bb;
			g.MeasureString(tagText.c_str(), -1, &statFont, Gdiplus::PointF(0, 0), &sfNear, &bb);
			statusW = static_cast<int>(bb.Width) + g_data.DPI(4);
		}
		Gdiplus::RectF statusRf(static_cast<Gdiplus::REAL>(rightBlockX - statusW), static_cast<Gdiplus::REAL>(row1Y),
			static_cast<Gdiplus::REAL>(statusW), static_cast<Gdiplus::REAL>(row1H));
		Gdiplus::SolidBrush tagTxt(tagTxtCol);
		g.DrawString(tagText.c_str(), -1, &statFont, statusRf, &sfFar, &tagTxt);

		// 统计数据：成功率与平均延迟（紧贴状态文字左侧）
		double successRate = (src.totalRequests > 0) ? (double)src.successRequests * 100.0 / src.totalRequests : 100.0;
		wchar_t statBuf[64];
		swprintf_s(statBuf, L"可用率: %.1f%%   延迟: %dms", successRate, max(1, src.lastLatencyMs));
		int statW = g_data.DPI(180);
		int statX = rightBlockX - statusW - g_data.DPI(12) - statW;
		Gdiplus::RectF statRf(static_cast<Gdiplus::REAL>(statX), static_cast<Gdiplus::REAL>(row1Y),
			static_cast<Gdiplus::REAL>(statW), static_cast<Gdiplus::REAL>(row1H));
		g.DrawString(statBuf, -1, &statFont, statRf, &sfFar, &textMuted);

		// 标题（动态宽度，直达统计区域左侧，绝不重叠）
		Gdiplus::RectF nameRf(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(row1Y),
			static_cast<Gdiplus::REAL>(statX - textX - g_data.DPI(8)), static_cast<Gdiplus::REAL>(row1H));
		g.DrawString(src.name.c_str(), -1, &nameFont, nameRf, &sfNear, &textWhite);

		// 3. 第二行：心跳条 (Uptime Heartbeat Bar)
		int barGap = g_data.DPI(3);
		int totalSlots = CApiHealthManager::MAX_HISTORY_POINTS; // 38
		int barAreaW = cardW - padX * 2;
		int barW = max(g_data.DPI(6), (barAreaW - (totalSlots - 1) * barGap) / totalSlots);
		// 末位格子吸收整除余数：心跳条右缘与右上角状态文字右缘精确对齐
		int lastBarW = barAreaW - (totalSlots - 1) * (barW + barGap);
		if (lastBarW < barW)
			lastBarW = barW;   // 极窄卡片被 DPI(6) 下限钳制时的兜底

		int startX = cardRect.left + padX;
		int histCount = static_cast<int>(src.history.size());
		int emptySlots = max(0, totalSlots - histCount);

		for (int slot = 0; slot < totalSlots; ++slot)
		{
			int bw = (slot == totalSlots - 1) ? lastBarW : barW;
			int bx = startX + slot * (barW + barGap);
			CRect rBar(bx, barY, bx + bw, barY + barH);
			Gdiplus::RectF rBarF(static_cast<Gdiplus::REAL>(bx), static_cast<Gdiplus::REAL>(barY),
				static_cast<Gdiplus::REAL>(bw), static_cast<Gdiplus::REAL>(barH));

			if (slot < emptySlots)
			{
				g.FillRectangle(&brEmpty, rBarF);
			}
			else
			{
				int hIdx = slot - emptySlots;
				const auto& pt = src.history[hIdx];
				if (pt.level == LEVEL_OK)
					g.FillRectangle(&brOk, rBarF);
				else if (pt.level == LEVEL_WARN)
					g.FillRectangle(&brWarn, rBarF);
				else
					g.FillRectangle(&brFail, rBarF);
			}
		}

		// 4. 第三行：职责说明与最近采样状态（字号提升至 11px，清晰易读）
		std::wstring logText = L"【" + src.role + L"】 最近采样: ";
		if (src.lastActiveTime > 0)
		{
			tm ltm;
			localtime_s(&ltm, &src.lastActiveTime);
			wchar_t timeBuf[32];
			swprintf_s(timeBuf, L"%02d:%02d:%02d · ", ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
			logText += timeBuf;
		}
		logText += src.lastStatusMsg.empty() ? L"正常" : src.lastStatusMsg;

		Gdiplus::SolidBrush logBrush(src.isWarning ? Gdiplus::Color(255, 248, 113, 113) : Gdiplus::Color(255, 203, 213, 225));
		Gdiplus::RectF logRf(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(row3Y),
			static_cast<Gdiplus::REAL>(cardW - padX * 2), static_cast<Gdiplus::REAL>(row3H));
		g.DrawString(logText.c_str(), -1, &logFont, logRf, &sfLog, &logBrush);

		curY += cardH + gap;
	}
}

void CManagerDialog::OnBnClickedApiTestBtn()
{
	if (m_api_probing)
		return;

	m_api_probing = true;
	if (m_api_test_btn.GetSafeHwnd())
	{
		m_api_test_btn.SetWindowText(L"检测中...");
		m_api_test_btn.EnableWindow(FALSE);
	}

	HWND hWnd = m_hWnd;
	CApiHealthManager::Instance().TriggerActiveProbeAsync([hWnd]() {
		if (::IsWindow(hWnd))
		{
			::PostMessage(hWnd, WM_APP_API_PROBE_FINISHED, 0, 0);
		}
	});
}

LRESULT CManagerDialog::OnApiProbeFinished(WPARAM, LPARAM)
{
	m_api_probing = false;
	if (m_api_test_btn.GetSafeHwnd())
	{
		m_api_test_btn.SetWindowText(L"立即重新检测");
		m_api_test_btn.EnableWindow(TRUE);
	}
	Invalidate(FALSE);
	return 0;
}

void CManagerDialog::DrawAboutPage(Gdiplus::Graphics& g, const CRect& contentRect)
{
	int panelH = max(contentRect.Height(), CalcPageContentHeight());
	Gdiplus::RectF panelRf(static_cast<Gdiplus::REAL>(contentRect.left), static_cast<Gdiplus::REAL>(contentRect.top), static_cast<Gdiplus::REAL>(contentRect.Width()), static_cast<Gdiplus::REAL>(panelH));
	Gdiplus::SolidBrush panelBg(Gdiplus::Color(255, 24, 27, 34));
	g.FillRectangle(&panelBg, panelRf);
	Gdiplus::Pen panelPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
	g.DrawRectangle(&panelPen, panelRf);

	int textX = contentRect.left + g_data.DPI(24);
	int textY = contentRect.top + g_data.DPI(22);
	int rightX = contentRect.right - g_data.DPI(24);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(15)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 241, 245, 249));
	g.DrawString(L"TrafficMonitor 股票行情插件 (Stock Plugin)", -1, &nameFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &nameBrush);

	textY += g_data.DPI(28);
	Gdiplus::Font verFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush verBrush(Gdiplus::Color(255, 148, 163, 184));
	Gdiplus::SolidBrush linkBrush(Gdiplus::Color(255, 56, 189, 248));

	Gdiplus::StringFormat strFmt(Gdiplus::StringFormat::GenericTypographic());
	Gdiplus::PointF curPt(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY));
	Gdiplus::RectF boundRect;

	// 1. "版本：v" STOCK_VERSION_STR "   |   作者："
	const wchar_t* partVer = L"版本：v" STOCK_VERSION_STR L"   |   作者：";
	g.DrawString(partVer, -1, &verFont, curPt, &strFmt, &verBrush);
	g.MeasureString(partVer, -1, &verFont, curPt, &strFmt, &boundRect);
	curPt.X += boundRect.Width;

	// 2. "xiongaox" (点击跳转主页)
	const wchar_t* partAuthor = L"xiongaox";
	g.DrawString(partAuthor, -1, &verFont, curPt, &strFmt, &linkBrush);
	g.MeasureString(partAuthor, -1, &verFont, curPt, &strFmt, &boundRect);
	m_about_author_rect = CRect(static_cast<int>(curPt.X), textY - g_data.DPI(2),
		static_cast<int>(curPt.X + boundRect.Width), textY + g_data.DPI(18));
	curPt.X += boundRect.Width;

	// 3. "   |   项目地址："
	const wchar_t* partMid = L"   |   项目地址：";
	g.DrawString(partMid, -1, &verFont, curPt, &strFmt, &verBrush);
	g.MeasureString(partMid, -1, &verFont, curPt, &strFmt, &boundRect);
	curPt.X += boundRect.Width;

	// 4. "点击跳转" (点击跳转仓库)
	const wchar_t* partRepo = L"点击跳转";
	g.DrawString(partRepo, -1, &verFont, curPt, &strFmt, &linkBrush);
	g.MeasureString(partRepo, -1, &verFont, curPt, &strFmt, &boundRect);
	m_about_repo_rect = CRect(static_cast<int>(curPt.X), textY - g_data.DPI(2),
		static_cast<int>(curPt.X + boundRect.Width), textY + g_data.DPI(18));

	textY += g_data.DPI(24);
	Gdiplus::Font dateFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(13)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush dateBrush(Gdiplus::Color(255, 248, 250, 252));

	Gdiplus::Font logFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush logBrush(Gdiplus::Color(255, 203, 213, 225));

	Gdiplus::Pen sepPen(Gdiplus::Color(255, 42, 47, 60), 1.0f);

	struct LogGroup {
		const wchar_t* date;
		const wchar_t* const* items;
		int count;
	};

	const wchar_t* items_0912[] = {
		L"•  【新增】 悬浮窗内嵌设置视图，彻底废弃旧版独立大弹窗，支持无边框平滑滚动与配置即时生效",
		L"•  【优化】 右键快捷菜单精炼简化，仅保留一键快速刷新股票行情",
		L"•  【优化】 分时走势曲线铺满边缘自绘，重构集合竞价 62:38 黄金分割比例与盘前走势回放",
		L"•  【优化】 开源仓库与关于页面重命名为 StockPlusPlus，更新项目主页跳转与远程地址",
		L"•  【修复】 彻底解决顶部状态栏指标多语言 UTF-8 BOM 乱码问题，增强配置文件读写兼容性"
	};
	const wchar_t* items_0911[] = {
		L"•  【新增】 行情中心增加资金流向全景监控页、板块分时走势图与领涨股看板",
		L"•  【新增】 支持全市场港股 (HK)、美股 (US) 行情、分时图、K线及自选分组拉取",
		L"•  【新增】 K线数据源即时切换按钮，支持带进度条的手动强制刷新与状态反馈",
		L"•  【新增】 行情中心集成一键隐私模式遮罩，支持敏感资产与金额脱敏显示",
		L"•  【优化】 全市场总成交额纳入北交所成交统计，全面统一列表排序三角矢量图标",
		L"•  【修复】 修复美股分时数据拉取及东财成交量解析，修复搜索下拉列表换行问题"
	};
	const wchar_t* items_0910[] = {
		L"•  【新增】 图表标题栏增加数据缓存状态指示，延后行情中心预热加速启动响应",
		L"•  【优化】 隔离行情中心缓存与网络请求，优化前台可见数据优先级与预加载限流",
		L"•  【优化】 新安装首次运行图表默认尺寸优化设定为 800x480 黄金分辨率",
		L"•  【修复】 消除行情中心调度器并发死锁隐患，强化东财K线写入前有效性校验",
		L"•  【修复】 修复上海黄金交易所 (SGE) 现货金价名称显示与图表缓存水合问题"
	};
	const wchar_t* items_0909[] = {
		L"•  【新增】 指数编辑支持添加上海黄金交易所金价指标，分组管理增加默认标签页单选",
		L"•  【新增】 行情中心 ETF 榜单点击直达对应日K线走势，支持右键一键快速返回",
		L"•  【优化】 首次运行自动预置精选自选股清单并持久化状态栏注册项",
		L"•  【优化】 收盘后保持展示全天最终成交额，优化持仓汇总居中与紧凑列表行高"
	};
	const wchar_t* items_0903[] = {
		L"•  【新增】 设置界面新增接口检测 (API Health) 诊断页与 ETF 重仓持股面板",
		L"•  【新增】 持仓汇总栏增加个股当日盈亏列与金额/比例一键切换模式",
		L"•  【优化】 顶部状态栏指标配置重构为扁平自绘按钮组，支持零盈亏中性橙色提示"
	};
	const wchar_t* items_0831[] = {
		L"•  【重大】 Stock 股票行情插件全面升级重构为 v2.0 架构，开启现代暗黑视觉体系",
		L"•  【新增】 WebDAV 云端备份与历史备份选择器，支持云端自动备份与多端同步",
		L"•  【新增】 股票代码全局拼音/代码联想搜索与多自定义分组管理",
		L"•  【优化】 全面重构设置管理器为卡片化容器与无边框扁平自绘控件",
		L"•  【优化】 动态精确计算任务栏项目渲染宽度，彻底消除右侧多余空白"
	};

	LogGroup groups[] = {
		{ L"2026-09-12 (v2.0.6)", items_0912, _countof(items_0912) },
		{ L"2026-09-11 (v2.0.4)", items_0911, _countof(items_0911) },
		{ L"2026-09-10 (v2.0.3)", items_0910, _countof(items_0910) },
		{ L"2026-09-09 (v2.0.2)", items_0909, _countof(items_0909) },
		{ L"2026-09-03 (v2.0.1)", items_0903, _countof(items_0903) },
		{ L"2026-08-31 (v2.0.0)", items_0831, _countof(items_0831) }
	};

	for (size_t gIdx = 0; gIdx < _countof(groups); ++gIdx)
	{
		const auto& grp = groups[gIdx];
		if (gIdx > 0)
		{
			textY += g_data.DPI(12);
			g.DrawLine(&sepPen, textX, textY, rightX, textY);
			textY += g_data.DPI(14);
		}

		g.DrawString(grp.date, -1, &dateFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &dateBrush);
		textY += g_data.DPI(24);

		for (int it = 0; it < grp.count; ++it)
		{
			g.DrawString(grp.items[it], -1, &logFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(textX), static_cast<Gdiplus::REAL>(textY)), &logBrush);
			textY += g_data.DPI(22);
		}
	}

	textY += g_data.DPI(24);
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
	int oldHoverMaSlot = m_hover_ma_slot;
	int oldHoverMaPreset = m_hover_ma_preset;
	int oldHoverTab = m_hover_group_tab;
	int oldHoverMode = m_hover_index_mode;
	int oldHoverDisplayArea = m_hover_display_area;
	int oldHoverOpacityPreset = m_hover_opacity_preset;
	bool oldHoverOpacitySlider = m_hover_opacity_slider;
	bool oldHoverReset = m_hover_reset_btn;

	// 透明度滑块拖动
	if (m_is_dragging_opacity)
	{
		int trackLeft = m_opacity_slider_rect.left;
		int trackW = m_opacity_slider_rect.Width();
		if (trackW > 0)
		{
			int pct = 30 + (point.x - trackLeft) * 70 / trackW;
			if (pct < 30) pct = 30; else if (pct > 100) pct = 100;
			if (m_data.m_window_opacity != pct)
			{
				ApplyOpacity(pct);
				Invalidate(FALSE);
			}
		}
	}

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
	m_hover_index_mode = -1;
	m_hover_display_area = -1;
	m_hover_opacity_preset = -1;
	m_hover_opacity_slider = false;
	m_hover_reset_btn = false;
	if (m_current_page == PAGE_INDEX)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (m_index_mode_rects[i].PtInRect(point))
			{
				m_hover_index_mode = i;
				break;
			}
		}

		for (size_t i = 0; i < m_index_card_rects.size(); ++i)
		{
			if (m_index_card_rects[i].PtInRect(point))
			{
				m_hover_index_card = static_cast<int>(i);
				break;
			}
		}
	}
	else if (m_current_page == PAGE_BASIC)
	{
		if (m_reset_btn_rect.PtInRect(point))
		{
			m_hover_reset_btn = true;
		}

		if (m_opacity_slider_rect.PtInRect(point))
		{
			m_hover_opacity_slider = true;
		}

		for (int i = 0; i < 5; ++i)
		{
			if (m_opacity_presets_rects[i].PtInRect(point))
			{
				m_hover_opacity_preset = i;
				break;
			}
		}

		if (InScrollContent(point))
		{
			for (int i = 0; i < 5; ++i)
			{
				if (m_display_area_rects[i].PtInRect(point))
				{
					m_hover_display_area = i;
					break;
				}
			}
		}
	}

	m_hover_ma_tag_del = -1;
	m_hover_ma_slot = -1;
	m_hover_ma_preset = -1;
	if (m_current_page == PAGE_MA && InScrollContent(point))
	{
		for (size_t i = 0; i < m_ma_tag_del_rects.size(); ++i)
		{
			if (m_ma_tag_del_rects[i].PtInRect(point))
			{
				m_hover_ma_tag_del = static_cast<int>(i);
				break;
			}
		}

		for (size_t i = 0; i < m_ma_slot_rects.size(); ++i)
		{
			if (m_ma_slot_rects[i].PtInRect(point))
			{
				m_hover_ma_slot = static_cast<int>(i);
				break;
			}
		}

		for (size_t i = 0; i < m_ma_preset_rects.size(); ++i)
		{
			if (m_ma_preset_rects[i].PtInRect(point))
			{
				m_hover_ma_preset = static_cast<int>(i);
				break;
			}
		}
	}

	int oldHoverMetricDel = m_hover_metric_tag_del;
	int oldHoverMetricSlot = m_hover_metric_slot;
	int oldHoverMetricPreset = m_hover_metric_preset;
	m_hover_metric_tag_del = -1;
	m_hover_metric_slot = -1;
	m_hover_metric_preset = -1;
	if (m_current_page == PAGE_METRICS && InScrollContent(point))
	{
		for (size_t i = 0; i < m_metric_tag_del_rects.size(); ++i)
		{
			if (m_metric_tag_del_rects[i].PtInRect(point))
			{
				m_hover_metric_tag_del = static_cast<int>(i);
				break;
			}
		}

		for (size_t i = 0; i < m_metric_slot_rects.size(); ++i)
		{
			if (m_metric_slot_rects[i].PtInRect(point))
			{
				m_hover_metric_slot = static_cast<int>(i);
				break;
			}
		}

		for (size_t i = 0; i < m_metric_candidates.size(); ++i)
		{
			if (m_metric_candidates[i].rect.PtInRect(point))
			{
				m_hover_metric_preset = static_cast<int>(i);
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
		oldHoverMa != m_hover_ma_tag_del || oldHoverMaSlot != m_hover_ma_slot ||
		oldHoverMaPreset != m_hover_ma_preset ||
		oldHoverMetricDel != m_hover_metric_tag_del || oldHoverMetricSlot != m_hover_metric_slot ||
		oldHoverMetricPreset != m_hover_metric_preset ||
		oldHoverTab != m_hover_group_tab || oldHoverMode != m_hover_index_mode ||
		oldHoverDisplayArea != m_hover_display_area ||
		oldHoverOpacityPreset != m_hover_opacity_preset ||
		oldHoverOpacitySlider != m_hover_opacity_slider ||
		oldHoverReset != m_hover_reset_btn)
	{
		Invalidate(FALSE);
	}

	CDialog::OnMouseMove(nFlags, point);
}

void CManagerDialog::OnMouseLeave()
{
	m_tracking_mouse = false;
	m_hover_menu = -1;
	m_hover_index_card = -1;
	m_hover_ma_tag_del = -1;
	m_hover_ma_slot = -1;
	m_hover_ma_preset = -1;
	m_hover_metric_tag_del = -1;
	m_hover_metric_slot = -1;
	m_hover_metric_preset = -1;
	m_hover_group_tab = -1;
	m_hover_index_mode = -1;
	m_hover_display_area = -1;
	m_hover_opacity_preset = -1;
	m_hover_opacity_slider = false;
	m_hover_reset_btn = false;
	Invalidate(FALSE);
	CDialog::OnMouseLeave();
}

BOOL CManagerDialog::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	CPoint pt;
	GetCursorPos(&pt);
	ScreenToClient(&pt);

	if (m_hover_menu >= 0 || m_hover_index_card >= 0 || m_hover_ma_tag_del >= 0 ||
		m_hover_ma_slot >= 0 || m_hover_ma_preset >= 0 ||
		m_hover_metric_tag_del >= 0 || m_hover_metric_slot >= 0 || m_hover_metric_preset >= 0 ||
		m_hover_group_tab >= 0 || m_hover_index_mode >= 0 || m_hover_display_area >= 0 ||
		m_hover_opacity_preset >= 0 || m_hover_opacity_slider || m_is_dragging_opacity ||
		m_hover_reset_btn ||
		(m_current_page == PAGE_ABOUT && InScrollContent(pt) &&
			(m_about_author_rect.PtInRect(pt) || m_about_repo_rect.PtInRect(pt))))
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

	if (m_current_page == PAGE_BASIC)
	{
		if (m_reset_btn_rect.PtInRect(point))
		{
			OnBnClickedResetData();
			return;
		}

		for (int i = 0; i < 5; ++i)
		{
			if (m_opacity_presets_rects[i].PtInRect(point))
			{
				const int opPresets[5] = { 100, 90, 80, 70, 60 };
				ApplyOpacity(opPresets[i]);
				ApplyIfEmbedded();
				Invalidate(FALSE);
				return;
			}
		}

		if (m_opacity_slider_rect.PtInRect(point))
		{
			m_is_dragging_opacity = true;
			SetCapture();
			int trackLeft = m_opacity_slider_rect.left;
			int trackW = m_opacity_slider_rect.Width();
			if (trackW > 0)
			{
				int pct = 30 + (point.x - trackLeft) * 70 / trackW;
				if (pct < 30) pct = 30; else if (pct > 100) pct = 100;
				ApplyOpacity(pct);
				Invalidate(FALSE);
			}
			return;
		}

		if (InScrollContent(point))
		{
			for (int i = 0; i < 5; ++i)
			{
				if (m_display_area_rects[i].PtInRect(point))
				{
					m_data.m_display_area = i;
					if (m_display_area_combo.GetSafeHwnd())
						m_display_area_combo.SetCurSel(i);
					ApplyIfEmbedded();
					Invalidate(FALSE);
					return;
				}
			}
		}
	}

	if (m_current_page == PAGE_INDEX)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (m_index_mode_rects[i].PtInRect(point))
			{
				m_data.m_index_display_mode = i;
				Invalidate();
				return;
			}
		}

		CRect clientRect;
		GetClientRect(clientRect);
		CRect contentRect(m_menu_width + g_data.DPI(18), g_data.DPI(72), clientRect.Width() - g_data.DPI(18), clientRect.Height() - g_data.DPI(52));

		const auto& presets = GetPresetIndices();
		for (size_t i = 0; i < m_index_card_rects.size() && i < presets.size(); ++i)
		{
			if (m_index_card_rects[i].PtInRect(point) && contentRect.PtInRect(point))
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
				ApplyIfEmbedded();
				Invalidate();
				return;
			}
		}
	}

	if (m_current_page == PAGE_GROUPS)
	{
		// 右下角「优先展示」单选框：[0]=自选股 [1]=持仓分组；内嵌模式即时生效
		for (size_t i = 0; i < m_group_pref_radio_rects.size() && i < 2; ++i)
		{
			if (m_group_pref_radio_rects[i].PtInRect(point))
			{
				m_data.m_group_default_tab = static_cast<int>(i);
				ApplyIfEmbedded();
				Invalidate();
				return;
			}
		}

		std::vector<GroupTabItem> tabs = BuildGroupTabItems(m_data.m_custom_groups, m_current_group_tab);
		for (size_t i = 0; i < m_group_tab_rects.size() && i < tabs.size(); ++i)
		{
			if (m_group_tab_rects[i].PtInRect(point))
			{
				if (tabs[i].isAddBtn)
				{
					// 点击了 "+ 新增分组"
					CString defName;
					defName.Format(L"分组%d", static_cast<int>(m_data.m_custom_groups.size() + 1));
					CSimpleInputDialog inputDlg(L"新增分组", L"请输入新分组名称：", defName, this);
					if (inputDlg.DoModal() == IDOK && !inputDlg.m_value.IsEmpty())
					{
						CustomGroup newGrp;
						newGrp.name = inputDlg.m_value.GetString();
						m_data.m_custom_groups.push_back(newGrp);
						SwitchGroupTab(static_cast<int>(m_data.m_custom_groups.size()) + 1);
						g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
						g_data.SaveConfig();
						Invalidate();
					}
					return;
				}
				else if (tabs[i].isDropdown)
				{
					// 点击了 "更多分组 ▾"
					std::vector<CDarkPopupMenu::MenuItem> menuItems;
					for (size_t k = 2; k < m_data.m_custom_groups.size(); ++k)
					{
						menuItems.push_back({
							static_cast<int>(1000 + k),
							m_data.m_custom_groups[k].name,
							(m_current_group_tab == static_cast<int>(k + 2)),
							false,
							false
						});
					}

					CRect tabRc = m_group_tab_rects[i];
					CPoint pt(tabRc.left, tabRc.bottom + g_data.DPI(2));
					ClientToScreen(&pt);

					CDarkPopupMenu menu;
					menu.CreatePopup(this);
					int cmd = menu.TrackMenu(pt, menuItems, tabRc.Width());
					if (cmd >= 1000 && cmd < 1000 + static_cast<int>(m_data.m_custom_groups.size()))
					{
						int selectedGroup = cmd - 1000;
						SwitchGroupTab(selectedGroup + 2);
						Invalidate();
					}
					return;
				}
				else
				{
					SwitchGroupTab(tabs[i].targetTab);
					return;
				}
			}
		}
	}

	if (m_current_page == PAGE_MA && InScrollContent(point))
	{
		for (size_t i = 0; i < m_ma_tag_del_rects.size(); ++i)
		{
			if (m_ma_tag_del_rects[i].PtInRect(point))
			{
				if (m_data.m_ma_days.size() > 1)
				{
					m_data.m_ma_days.erase(m_data.m_ma_days.begin() + i);
					ApplyIfEmbedded();
					Invalidate();
				}
				else
				{
					MessageBox(L"至少保留 1 个均线周期！", L"提示", MB_ICONINFORMATION);
				}
				return;
			}
		}

		// 空槽位：把焦点交给天数输入框，方便连续录入
		for (size_t i = 0; i < m_ma_slot_rects.size(); ++i)
		{
			if (m_ma_slot_rects[i].PtInRect(point))
			{
				m_ma_input_edit.SetFocus();
				return;
			}
		}

		// 快捷添加候选周期
		for (size_t i = 0; i < m_ma_preset_rects.size() && i < _countof(kMaPresetDays); ++i)
		{
			if (m_ma_preset_rects[i].PtInRect(point))
			{
				if (TryAddMaDay(kMaPresetDays[i]))
				{
					ApplyIfEmbedded();
					Invalidate();
				}
				return;
			}
		}

		// 分时图布林带显隐：[0]=上轨、[1]=中轨、[2]=下轨；内嵌模式即时生效
		for (size_t i = 0; i < m_boll_vis_check_rects.size(); ++i)
		{
			if (m_boll_vis_check_rects[i].PtInRect(point))
			{
				if (i == 0)
					m_data.m_boll_upper_visible = !m_data.m_boll_upper_visible;
				else if (i == 1)
					m_data.m_boll_mid_visible = !m_data.m_boll_mid_visible;
				else if (i == 2)
					m_data.m_boll_lower_visible = !m_data.m_boll_lower_visible;
				ApplyIfEmbedded();
				Invalidate();
				return;
			}
		}
	}

	if (m_current_page == PAGE_METRICS && InScrollContent(point))
	{
		// 点击已选指标上的 × 删除
		for (size_t i = 0; i < m_metric_tag_del_rects.size(); ++i)
		{
			if (m_metric_tag_del_rects[i].PtInRect(point))
			{
				if (m_data.m_header_metrics.size() > 1)
				{
					m_data.m_header_metrics.erase(m_data.m_header_metrics.begin() + i);
					ApplyIfEmbedded();
					Invalidate();
				}
				else
				{
					MessageBox(L"至少保留 1 个指标项！", L"提示", MB_ICONINFORMATION);
				}
				return;
			}
		}

		// 点击候选指标
		for (size_t i = 0; i < m_metric_candidates.size(); ++i)
		{
			if (m_metric_candidates[i].rect.PtInRect(point))
			{
				const std::wstring& name = m_metric_candidates[i].name;
				auto it = std::find(m_data.m_header_metrics.begin(), m_data.m_header_metrics.end(), name);
				if (it != m_data.m_header_metrics.end())
				{
					if (m_data.m_header_metrics.size() > 1)
					{
						m_data.m_header_metrics.erase(it);
						ApplyIfEmbedded();
						Invalidate();
					}
					else
					{
						MessageBox(L"至少保留 1 个指标项！", L"提示", MB_ICONINFORMATION);
					}
				}
				else
				{
					if (TryAddMetric(name))
					{
						ApplyIfEmbedded();
						Invalidate();
					}
				}
				return;
			}
		}
	}

	if (m_current_page == PAGE_ABOUT && InScrollContent(point))
	{
		if (m_about_author_rect.PtInRect(point))
		{
			ShellExecute(nullptr, L"open", L"https://github.com/xiongaox", nullptr, nullptr, SW_SHOWNORMAL);
			return;
		}
		if (m_about_repo_rect.PtInRect(point))
		{
			ShellExecute(nullptr, L"open", L"https://github.com/xiongaox/StockPlusPlus", nullptr, nullptr, SW_SHOWNORMAL);
			return;
		}
	}

	// 点击输入框字段上下留白区时，把焦点交给对应的编辑控件
	for (const auto& kv : m_editFieldRects)
	{
		CWnd* pEdit = GetDlgItem(kv.first);
		if (pEdit && pEdit->GetSafeHwnd() && pEdit->IsWindowVisible() && kv.second.PtInRect(point))
		{
			pEdit->SetFocus();
			return;
		}
	}

	CDialog::OnLButtonDown(nFlags, point);
}

void CManagerDialog::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_is_dragging_opacity)
	{
		m_is_dragging_opacity = false;
		ReleaseCapture();
		ApplyIfEmbedded();
		Invalidate(FALSE);
		return;
	}
	CDialog::OnLButtonUp(nFlags, point);
}

void CManagerDialog::OnRButtonUp(UINT nFlags, CPoint point)
{
	if (m_current_page == PAGE_GROUPS)
	{
		std::vector<GroupTabItem> tabs = BuildGroupTabItems(m_data.m_custom_groups, m_current_group_tab);
		for (size_t i = 0; i < m_group_tab_rects.size() && i < tabs.size(); ++i)
		{
			if (m_group_tab_rects[i].PtInRect(point))
			{
				int groupIdx = -1;
				if (tabs[i].targetTab >= 2)
				{
					groupIdx = tabs[i].targetTab - 2;
				}
				else if (tabs[i].isDropdown && m_current_group_tab >= 4)
				{
					groupIdx = m_current_group_tab - 2;
				}

				if (groupIdx >= 0 && groupIdx < static_cast<int>(m_data.m_custom_groups.size()))
				{
					std::vector<CDarkPopupMenu::MenuItem> menuItems;
					menuItems.push_back({ 101, L"重命名分组", false, false, false });
					menuItems.push_back({ 102, L"删除分组", false, false, true });

					CPoint screenPt = point;
					ClientToScreen(&screenPt);

					CDarkPopupMenu menu;
					menu.CreatePopup(this);
					int cmd = menu.TrackMenu(screenPt, menuItems, g_data.DPI(110));
					if (cmd == 101)
					{
						CSimpleInputDialog inputDlg(L"重命名分组", L"请输入新的分组名称：", m_data.m_custom_groups[groupIdx].name.c_str(), this);
						if (inputDlg.DoModal() == IDOK && !inputDlg.m_value.IsEmpty())
						{
							m_data.m_custom_groups[groupIdx].name = inputDlg.m_value.GetString();
							g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
							g_data.SaveConfig();
							Invalidate();
						}
					}
					else if (cmd == 102)
					{
						CString prompt;
						prompt.Format(L"确定要删除分组 [%s] 吗？", m_data.m_custom_groups[groupIdx].name.c_str());
						CDarkConfirmDialog confirmDlg(L"确认删除", prompt, this, true);
						if (confirmDlg.DoModal() == IDOK)
						{
							m_data.m_custom_groups.erase(m_data.m_custom_groups.begin() + groupIdx);
							g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
							g_data.SaveConfig();
							SwitchGroupTab(0);
							Invalidate();
						}
					}
					return;
				}
			}
		}
	}

	CDialog::OnRButtonUp(nFlags, point);
}

void CManagerDialog::OnSearchEditChange()
{
	if (!m_search_edit.GetSafeHwnd()) return;
	CString query;
	m_search_edit.GetWindowText(query);
	query.Trim();

	if (query.IsEmpty())
	{
		++m_search_seq;
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.HidePopup();
		return;
	}

	uint32_t seq = ++m_search_seq;
	HWND hWnd = m_hWnd;
	std::wstring qStr = query.GetString();

	// 异步检索股票，带120ms防抖，避免每次输入字符阻塞UI主线程发起HTTP请求
	std::thread([hWnd, seq, qStr]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(120));
		auto* pResults = new std::vector<StockSearchResult>(CCommon::SearchStock(qStr));
		if (!::IsWindow(hWnd) || !::PostMessage(hWnd, WM_APP_SEARCH_RESULT_READY, static_cast<WPARAM>(seq), reinterpret_cast<LPARAM>(pResults)))
		{
			delete pResults;
		}
	}).detach();
}

LRESULT CManagerDialog::OnSearchResultReady(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<std::vector<StockSearchResult>> pResults(reinterpret_cast<std::vector<StockSearchResult>*>(lParam));
	if (wParam != m_search_seq.load() || !pResults)
		return 0;

	if (pResults->empty())
	{
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.HidePopup();
		return 0;
	}

	std::vector<CSearchResultDropdown::GroupMenuItem> groupItems;
	groupItems.push_back({ 3001, L"添加到: 自选股", false, false });
	groupItems.push_back({ 3002, L"添加到: 持仓", false, false });
	for (size_t i = 0; i < m_data.m_custom_groups.size(); ++i)
	{
		CString itemText;
		itemText.Format(L"添加到: %s", m_data.m_custom_groups[i].name.c_str());
		groupItems.push_back({ static_cast<int>(3010 + i), itemText.GetString(), false, false });
	}
	groupItems.push_back({ 0, L"", true, false });
	groupItems.push_back({ 3003, L"+ 新建分组并添加...", false, true });

	CRect editRc;
	m_search_edit.GetWindowRect(&editRc);
	m_search_dropdown.ShowResults(*pResults, editRc, groupItems);
	return 0;
}

BOOL CManagerDialog::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (m_current_page == PAGE_INDEX)
	{
		const auto& presets = GetPresetIndices();
		CRect clientRect;
		GetClientRect(clientRect);
		int contentWidth = clientRect.Width() - m_menu_width - g_data.DPI(36);
		int cardGapX = g_data.DPI(12);
		int cardGapY = g_data.DPI(10);
		int minCardW = g_data.DPI(185);
		int cols = max(2, (contentWidth + cardGapX) / (minCardW + cardGapX));
		int rows = static_cast<int>((presets.size() + cols - 1) / cols);
		int cardH = g_data.DPI(52);
		int totalH = rows * (cardH + cardGapY);

		int availableH = clientRect.Height() - g_data.DPI(72) - g_data.DPI(52);
		int maxScroll = max(0, totalH - availableH);

		if (maxScroll > 0)
		{
			int oldScroll = m_index_scroll_y;
			if (zDelta > 0)
				m_index_scroll_y -= g_data.DPI(36);
			else
				m_index_scroll_y += g_data.DPI(36);

			if (m_index_scroll_y < 0) m_index_scroll_y = 0;
			if (m_index_scroll_y > maxScroll) m_index_scroll_y = maxScroll;

			if (oldScroll != m_index_scroll_y)
			{
				Invalidate();
				return TRUE;
			}
		}
	}

	// 方案B：基础设置/均线/指标/云端备份/关于插件页 —— 右侧内容区隐藏式滚动（无滚动条）
	if (m_current_page == PAGE_BASIC || m_current_page == PAGE_MA ||
		m_current_page == PAGE_METRICS || m_current_page == PAGE_WEBDAV ||
		m_current_page == PAGE_ABOUT)
	{
		CPoint clientPt = pt;
		ScreenToClient(&clientPt);
		CRect contentRect;
		GetScrollContentRect(contentRect);
		const int maxScroll = CalcPageContentHeight() - contentRect.Height();
		if (maxScroll > 0 && contentRect.PtInRect(clientPt))
		{
			SetPageScroll(m_page_scroll_y + (zDelta > 0 ? -g_data.DPI(46) : g_data.DPI(46)));
			return TRUE;
		}
	}
	return CDialog::OnMouseWheel(nFlags, zDelta, pt);
}

BOOL CManagerDialog::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)
	{
		if (m_search_dropdown.GetSafeHwnd() && m_search_dropdown.IsWindowVisible())
		{
			m_search_dropdown.HidePopup();
			return TRUE;
		}
		// 内嵌子窗口模式：ESC = 提交未保存的字段并收起设置视图（设置即时生效，无丢弃语义）
		if (m_as_child && m_on_settings_closed)
		{
			ApplyIfEmbedded();
			m_on_settings_closed(true);
			return TRUE;
		}
	}

	// 均线天数输入框内按回车 = 添加周期，而不是触发「确定」关闭对话框
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN)
	{
		if (m_ma_input_edit.GetSafeHwnd() && m_ma_input_edit.IsWindowVisible())
		{
			CWnd* pFocus = GetFocus();
			if (pFocus && pFocus->GetSafeHwnd() == m_ma_input_edit.GetSafeHwnd())
			{
				OnMaAddBtnClick();
				return TRUE;
			}
		}
	}

	if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_RBUTTONDOWN ||
		pMsg->message == WM_NCLBUTTONDOWN || pMsg->message == WM_NCRBUTTONDOWN)
	{
		CPoint pt = pMsg->pt;
		if (m_search_dropdown.GetSafeHwnd() && m_search_dropdown.IsWindowVisible())
		{
			CRect dropRc, editRc;
			m_search_dropdown.GetWindowRect(&dropRc);
			m_search_edit.GetWindowRect(&editRc);
			if (!dropRc.PtInRect(pt) && !editRc.PtInRect(pt))
			{
				m_search_dropdown.HidePopup();
			}
		}
	}

	return CDialog::PreTranslateMessage(pMsg);
}

void CManagerDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	if (m_search_dropdown.GetSafeHwnd())
		m_search_dropdown.HidePopup();
	UpdateControlsLayout();
	Invalidate();
}

void CManagerDialog::OnMove(int x, int y)
{
	CDialog::OnMove(x, y);
	if (m_search_dropdown.GetSafeHwnd())
		m_search_dropdown.HidePopup();
}

void CManagerDialog::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CDialog::OnActivate(nState, pWndOther, bMinimized);
	if (nState == WA_INACTIVE)
	{
		if (pWndOther && (pWndOther->GetSafeHwnd() == m_search_dropdown.GetSafeHwnd() ||
			pWndOther->GetSafeHwnd() == m_search_edit.GetSafeHwnd()))
		{
			return;
		}
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.HidePopup();
	}
}

BOOL CManagerDialog::OnNcActivate(BOOL bActive)
{
	if (!bActive)
	{
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.HidePopup();
	}
	return CDialog::OnNcActivate(bActive);
}

void CManagerDialog::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = m_min_size.cx;
	lpMMI->ptMinTrackSize.y = m_min_size.cy;
	CDialog::OnGetMinMaxInfo(lpMMI);
}

void CManagerDialog::OnListItemClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	if (pNMItemActivate && pNMItemActivate->iItem >= 0)
	{
		int nItem = pNMItemActivate->iItem;
		int nSubItem = pNMItemActivate->iSubItem;

		// 检查是否点击了 "状态栏显示" 列 (第 5 列)
		if (nSubItem == 5)
		{
			if (m_current_group_tab == 0) // 自选股
			{
				if (nItem < static_cast<int>(m_data.m_stock_codes.size()))
				{
					const auto& code = m_data.m_stock_codes[nItem];
					bool cur = g_data.GetShowInStatusBar(code);
					g_data.SetShowInStatusBar(code, !cur);
					m_stock_listctrl.SetItemText(nItem, 5, (!cur) ? L"√" : L"");
				}
			}
			else if (m_current_group_tab == 1) // 持仓
			{
				DWORD_PTR codeIdx = m_pos_listctrl.GetItemData(nItem);
				if (codeIdx < m_data.m_position_codes.size())
				{
					const auto& code = m_data.m_position_codes[codeIdx];
					bool cur = g_data.GetShowInStatusBar(code);
					g_data.SetShowInStatusBar(code, !cur);
					m_pos_listctrl.SetItemText(nItem, 5, (!cur) ? L"√" : L"");
				}
			}
			else if (m_current_group_tab >= 2) // 自定义分组
			{
				size_t groupIdx = static_cast<size_t>(m_current_group_tab - 2);
				if (groupIdx < m_data.m_custom_groups.size())
				{
					auto& codes = m_data.m_custom_groups[groupIdx].codes;
					if (nItem < static_cast<int>(codes.size()))
					{
						const auto& code = codes[nItem];
						bool cur = g_data.GetShowInStatusBar(code);
						g_data.SetShowInStatusBar(code, !cur);
						m_custom_listctrl.SetItemText(nItem, 5, (!cur) ? L"√" : L"");
					}
				}
			}
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnLbnDblclkMgrList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
	if (index >= 0 && index < static_cast<int>(m_data.m_stock_codes.size()))
	{
		const auto& code = m_data.m_stock_codes[index];
		CDarkStockAlertInputDlg dlg(code, L"", L"", this);
		if (dlg.DoModal(this) == IDOK)
		{
			g_data.SetAlertPrice(code, dlg.m_low_price, dlg.m_high_price);
			g_data.SaveConfig();
			RefreshStockList();
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnLbnDblclkPosList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_pos_listctrl.GetNextItem(-1, LVNI_SELECTED);
	if (index >= 0)
	{
		DWORD_PTR codeIdx = m_pos_listctrl.GetItemData(index);
		if (codeIdx < m_data.m_position_codes.size())
		{
			const auto& code = m_data.m_position_codes[codeIdx];
			CDarkPositionInputDlg dlg(code, L"", L"", this);
			if (dlg.DoModal(this) == IDOK)
			{
				g_data.SetPosition(code, dlg.m_cost_price, dlg.m_holding_count);
				g_data.m_setting_data.m_position_codes = m_data.m_position_codes;
				g_data.SaveConfig();
				RefreshPositionList();
			}
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnLbnDblclkCustomList(NMHDR* pNMHDR, LRESULT* pResult)
{
	int index = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
	size_t groupIdx = (m_current_group_tab >= 2) ? static_cast<size_t>(m_current_group_tab - 2) : 0;
	if (groupIdx < m_data.m_custom_groups.size())
	{
		auto& codes = m_data.m_custom_groups[groupIdx].codes;
		if (index >= 0 && index < static_cast<int>(codes.size()))
		{
			const auto& code = codes[index];
			CDarkStockAlertInputDlg dlg(code, L"", L"", this);
			if (dlg.DoModal(this) == IDOK)
			{
				g_data.SetAlertPrice(code, dlg.m_low_price, dlg.m_high_price);
				g_data.SaveConfig();
				RefreshCustomList();
			}
		}
	}
	*pResult = 0;
}

void CManagerDialog::OnAddBtnClick()
{
	if (m_current_group_tab == 1)
	{
		// 编辑持仓：优先编辑选中行；否则依次取持仓列表、自选列表中的第一个代码作为候选
		int curSel = m_pos_listctrl.GetNextItem(-1, LVNI_SELECTED);
		std::wstring code;
		if (curSel >= 0)
		{
			DWORD_PTR codeIdx = m_pos_listctrl.GetItemData(curSel);
			if (codeIdx < m_data.m_position_codes.size())
				code = m_data.m_position_codes[codeIdx];
		}
		else if (!m_data.m_position_codes.empty())
		{
			code = m_data.m_position_codes[0];
		}
		else if (!m_data.m_stock_codes.empty())
		{
			code = m_data.m_stock_codes[0];
		}

		if (!code.empty())
		{
			CDarkPositionInputDlg dlg(code, L"", L"", this);
			if (dlg.DoModal(this) == IDOK)
			{
				g_data.SetPosition(code, dlg.m_cost_price, dlg.m_holding_count);
				if (std::find(m_data.m_position_codes.begin(), m_data.m_position_codes.end(), code) == m_data.m_position_codes.end())
					m_data.m_position_codes.push_back(code);
				g_data.m_setting_data.m_position_codes = m_data.m_position_codes;
				g_data.SaveConfig();
				RefreshPositionList();
			}
		}
	}
	else
	{
		if (m_search_edit.GetSafeHwnd())
		{
			m_search_edit.SetFocus();
			m_search_edit.SetSel(0, -1);
		}
	}
}

void CManagerDialog::OnEditBtnClick()
{
	if (m_current_group_tab == 0)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()))
		{
			const auto& code = m_data.m_stock_codes[curSel];
			CDarkStockAlertInputDlg dlg(code, L"", L"", this);
			if (dlg.DoModal(this) == IDOK)
			{
				g_data.SetAlertPrice(code, dlg.m_low_price, dlg.m_high_price);
				g_data.SaveConfig();
				RefreshStockList();
			}
		}
	}
	else if (m_current_group_tab >= 2)
	{
		size_t groupIdx = static_cast<size_t>(m_current_group_tab - 2);
		if (groupIdx < m_data.m_custom_groups.size())
		{
			auto& codes = m_data.m_custom_groups[groupIdx].codes;
			int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
			if (curSel >= 0 && curSel < static_cast<int>(codes.size()))
			{
				const auto& code = codes[curSel];
				CDarkStockAlertInputDlg dlg(code, L"", L"", this);
				if (dlg.DoModal(this) == IDOK)
				{
					g_data.SetAlertPrice(code, dlg.m_low_price, dlg.m_high_price);
					g_data.SaveConfig();
					RefreshCustomList();
				}
			}
		}
	}
}

void CManagerDialog::OnDelBtnClick()
{
	if (m_current_group_tab == 0)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()))
		{
			m_data.m_stock_codes.erase(m_data.m_stock_codes.begin() + curSel);
			RefreshStockList();
			RefreshPositionList();
		}
	}
	else if (m_current_group_tab == 1)
	{
		// 清除持仓：清零持仓数据并从独立持仓列表移除（不影响自选股）
		int curSel = m_pos_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0)
		{
			DWORD_PTR codeIdx = m_pos_listctrl.GetItemData(curSel);
			if (codeIdx < m_data.m_position_codes.size())
			{
				const auto& code = m_data.m_position_codes[codeIdx];
				g_data.SetPosition(code, 0.0, 0.0, L"");
				m_data.m_position_codes.erase(std::remove(m_data.m_position_codes.begin(), m_data.m_position_codes.end(), code), m_data.m_position_codes.end());
				g_data.m_setting_data.m_position_codes = m_data.m_position_codes;
				g_data.SaveConfig();
				RefreshPositionList();
			}
		}
	}
	else if (m_current_group_tab >= 2)
	{
		size_t groupIdx = static_cast<size_t>(m_current_group_tab - 2);
		if (groupIdx < m_data.m_custom_groups.size())
		{
			auto& codes = m_data.m_custom_groups[groupIdx].codes;
			int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
			if (curSel >= 0 && curSel < static_cast<int>(codes.size()))
			{
				codes.erase(codes.begin() + curSel);
				RefreshCustomList();
			}
		}
	}
}

void CManagerDialog::OnDelGroupBtnClick()
{
	if (m_current_group_tab >= 2 && (m_current_group_tab - 2) < static_cast<int>(m_data.m_custom_groups.size()))
	{
		size_t groupIdx = static_cast<size_t>(m_current_group_tab - 2);
		CString prompt;
		prompt.Format(L"确定要删除分组 [%s] 吗？", m_data.m_custom_groups[groupIdx].name.c_str());
		CDarkConfirmDialog confirmDlg(L"确认删除", prompt, this, true);
		if (confirmDlg.DoModal() == IDOK)
		{
			m_data.m_custom_groups.erase(m_data.m_custom_groups.begin() + groupIdx);
			g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
			g_data.SaveConfig();
			SwitchGroupTab(0);
			Invalidate();
		}
	}
}

void CManagerDialog::OnMoveUpBtnClick()
{
	if (m_current_group_tab == 0)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel > 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()))
		{
			std::swap(m_data.m_stock_codes[curSel - 1], m_data.m_stock_codes[curSel]);
			RefreshStockList();
			m_stock_listctrl.SetItemState(curSel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
	else if (m_current_group_tab >= 2)
	{
		size_t groupIdx = static_cast<size_t>(m_current_group_tab - 2);
		if (groupIdx < m_data.m_custom_groups.size())
		{
			auto& codes = m_data.m_custom_groups[groupIdx].codes;
			int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
			if (curSel > 0 && curSel < static_cast<int>(codes.size()))
			{
				std::swap(codes[curSel - 1], codes[curSel]);
				RefreshCustomList();
				m_custom_listctrl.SetItemState(curSel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
		}
	}
}

void CManagerDialog::OnMoveDownBtnClick()
{
	if (m_current_group_tab == 0)
	{
		int curSel = m_stock_listctrl.GetNextItem(-1, LVNI_SELECTED);
		if (curSel >= 0 && curSel < static_cast<int>(m_data.m_stock_codes.size()) - 1)
		{
			std::swap(m_data.m_stock_codes[curSel], m_data.m_stock_codes[curSel + 1]);
			RefreshStockList();
			m_stock_listctrl.SetItemState(curSel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	}
	else if (m_current_group_tab >= 2)
	{
		size_t groupIdx = static_cast<size_t>(m_current_group_tab - 2);
		if (groupIdx < m_data.m_custom_groups.size())
		{
			auto& codes = m_data.m_custom_groups[groupIdx].codes;
			int curSel = m_custom_listctrl.GetNextItem(-1, LVNI_SELECTED);
			if (curSel >= 0 && curSel < static_cast<int>(codes.size()) - 1)
			{
				std::swap(codes[curSel], codes[curSel + 1]);
				RefreshCustomList();
				m_custom_listctrl.SetItemState(curSel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
		}
	}
}

void CManagerDialog::OnGroupSortBtnClick()
{
	if (m_data.m_custom_groups.empty())
	{
		MessageBox(L"暂无自定义分组", L"提示", MB_ICONINFORMATION | MB_OK);
		return;
	}

	// 记录当前选中的分组名，排序后让标签跟随它的新位置
	std::wstring currentName;
	if (m_current_group_tab >= 2 && (m_current_group_tab - 2) < static_cast<int>(m_data.m_custom_groups.size()))
		currentName = m_data.m_custom_groups[m_current_group_tab - 2].name;

	CGroupSortDlg dlg(m_data.m_custom_groups, this);
	if (dlg.DoModal() != IDOK)
		return;

	m_data.m_custom_groups = dlg.m_groups;
	g_data.m_setting_data.m_custom_groups = m_data.m_custom_groups;
	g_data.SaveConfig();

	if (!currentName.empty())
	{
		for (size_t i = 0; i < m_data.m_custom_groups.size(); ++i)
		{
			if (m_data.m_custom_groups[i].name == currentName)
			{
				m_current_group_tab = static_cast<int>(i) + 2;
				break;
			}
		}
	}
	RefreshCustomList();
	Invalidate();
}

void CManagerDialog::OnMaAddBtnClick()
{
	CString valStr;
	m_ma_input_edit.GetWindowText(valStr);
	if (TryAddMaDay(_ttoi(valStr)))
	{
		m_ma_input_edit.SetWindowText(L"");
		m_ma_input_edit.SetFocus();
		ApplyIfEmbedded();
		Invalidate();
	}
}

// 校验并添加均线周期；失败时弹出与原逻辑一致的提示，返回是否添加成功
bool CManagerDialog::TryAddMaDay(int val)
{
	if (val < 1 || val > 250)
	{
		MessageBox(L"均线周期请输入 1 到 250 之间的整数！", L"提示", MB_ICONWARNING);
		return false;
	}

	if (static_cast<int>(m_data.m_ma_days.size()) >= MA_PRESET_MAX)
	{
		MessageBox(L"均线周期最多配置 5 条！请先删除已有周期后再添加。", L"提示", MB_ICONINFORMATION);
		return false;
	}

	if (std::find(m_data.m_ma_days.begin(), m_data.m_ma_days.end(), val) != m_data.m_ma_days.end())
	{
		MessageBox(L"该均线周期已存在！", L"提示", MB_ICONINFORMATION);
		return false;
	}

	m_data.m_ma_days.push_back(val);
	std::sort(m_data.m_ma_days.begin(), m_data.m_ma_days.end());
	return true;
}

// 添加指标项（上限 4 项）
bool CManagerDialog::TryAddMetric(const std::wstring& name)
{
	if (m_data.m_header_metrics.size() >= 4)
	{
		MessageBox(L"顶部指标栏最多配置 4 项！请先点击已选标签上的「×」删除不需要的指标。", L"提示", MB_ICONINFORMATION);
		return false;
	}

	if (std::find(m_data.m_header_metrics.begin(), m_data.m_header_metrics.end(), name) != m_data.m_header_metrics.end())
	{
		MessageBox(L"该指标已在显示列表中！", L"提示", MB_ICONINFORMATION);
		return false;
	}

	m_data.m_header_metrics.push_back(name);
	return true;
}

void CManagerDialog::OnClickedFullDayCheck()
{
	SetCheck(IDC_FULL_DAY_CHECK, !IsChecked(IDC_FULL_DAY_CHECK));
	m_data.m_full_day = IsChecked(IDC_FULL_DAY_CHECK);
	ApplyIfEmbedded();
}

void CManagerDialog::OnBnClickedShowTodayProfitCheck()
{
	SetCheck(IDC_SHOW_TODAY_PROFIT_CHECK, !IsChecked(IDC_SHOW_TODAY_PROFIT_CHECK));
	m_data.m_show_today_profit = IsChecked(IDC_SHOW_TODAY_PROFIT_CHECK);
	ApplyIfEmbedded();
}

void CManagerDialog::OnBnClickedShowFluctuationCheck()
{
	SetCheck(IDC_SHOW_FLUCTUATION_CHECK, !IsChecked(IDC_SHOW_FLUCTUATION_CHECK));
	m_data.m_show_fluctuation = IsChecked(IDC_SHOW_FLUCTUATION_CHECK);
	ApplyIfEmbedded();
}

void CManagerDialog::OnBnClickedUseSocks5ProxyCheck()
{
	SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, !IsChecked(IDC_USE_SOCKS5_PROXY_CHECK));
	m_data.m_use_socks5_proxy = IsChecked(IDC_USE_SOCKS5_PROXY_CHECK);
	ApplyIfEmbedded();
}

void CManagerDialog::OnBnClickedResetData()
{
	CString msg = _T("确定要重置所有数据并清空用户数据吗？\n\n")
		_T("此操作将恢复所有插件配置至初始默认状态，并彻底清空所有自选股、持仓记录、自定义分组与本地缓存数据库。\n\n")
		_T("该操作不可撤销，是否继续？");
	if (MessageBox(msg, _T("警告 - 重置所有数据"), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
		return;

	// 1. 调用底层重置逻辑（清库、删ini、写默认配置）
	g_data.ResetToDefault();
	Stock::Instance().SendStockInfoRequest();

	// 2. 同步更新当前对话框内部数据
	m_data = g_data.m_setting_data;

	// 3. 刷新基础设置页的复选框与输入框
	SetCheck(IDC_FULL_DAY_CHECK, m_data.m_full_day);
	SetCheck(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation);
	SetCheck(IDC_SHOW_TODAY_PROFIT_CHECK, m_data.m_show_today_profit);
	SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy);
	SetDlgItemText(IDC_SOCKS5_PROXY_EDIT, m_data.m_socks5_proxy.c_str());

	CString strKlineW, strKlineH;
	strKlineW.Format(_T("%d"), static_cast<int>(m_data.m_kline_width));
	SetDlgItemText(IDC_KLINE_WIDTH_EDIT, strKlineW);
	strKlineH.Format(_T("%d"), static_cast<int>(m_data.m_kline_height));
	SetDlgItemText(IDC_KLINE_HEIGHT_EDIT, strKlineH);

	int selArea = m_data.m_display_area;
	if (selArea < AREA_LEFT_TOP || selArea > AREA_CENTER)
		selArea = AREA_RIGHT_BOTTOM;
	m_display_area_combo.SetCurSel(selArea);

	// 4. 刷新云端备份页面控件
	SetDlgItemText(IDC_WEBDAV_URL_EDIT, m_data.m_webdav_url.c_str());
	SetDlgItemText(IDC_WEBDAV_USER_EDIT, m_data.m_webdav_username.c_str());
	SetDlgItemText(IDC_WEBDAV_PWD_EDIT, m_data.m_webdav_password.c_str());
	SetDlgItemText(IDC_WEBDAV_DIR_EDIT, m_data.m_webdav_dir.c_str());
	SetCheck(IDC_WEBDAV_AUTO_SYNC_CHECK, m_data.m_webdav_auto_sync);
	SetCheck(IDC_WEBDAV_AUTO_BACKUP_CHECK, m_data.m_webdav_auto_backup);

	// 5. 刷新各列表并默认切回自选股 Tab
	m_current_group_tab = 0;
	RefreshStockList();
	RefreshPositionList();
	RefreshCustomList();
	ApplyIfEmbedded();

	// 6. 若处于悬浮窗内嵌模式，同步通知宿主悬浮窗全量热重置
	CFloatingWnd* pParentFloat = Stock::Instance().GetFloatingWnd();
	if (pParentFloat != nullptr && ::IsWindow(pParentFloat->GetSafeHwnd()))
	{
		pParentFloat->OnDataReset();
	}
	ApplyOpacity(97);

	// 7. 重绘并弹窗提示
	Invalidate();
	MessageBox(_T("所有数据已成功重置为默认状态！"), _T("提示"), MB_ICONINFORMATION | MB_OK);
}

void CManagerDialog::OnBnClickedWebDavAutoSyncCheck()
{
	SetCheck(IDC_WEBDAV_AUTO_SYNC_CHECK, !IsChecked(IDC_WEBDAV_AUTO_SYNC_CHECK));
	m_data.m_webdav_auto_sync = IsChecked(IDC_WEBDAV_AUTO_SYNC_CHECK);
	ApplyIfEmbedded();
}

void CManagerDialog::OnBnClickedWebDavAutoBackupCheck()
{
	SetCheck(IDC_WEBDAV_AUTO_BACKUP_CHECK, !IsChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK));
	m_data.m_webdav_auto_backup = IsChecked(IDC_WEBDAV_AUTO_BACKUP_CHECK);
	ApplyIfEmbedded();
}

void CManagerDialog::StartWebDavAsync(int op)
{
	if (m_webdav_busy)
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

	if (m_data.m_webdav_url.empty())
	{
		MessageBox(L"请先填写 WebDAV 服务器地址", L"提示", MB_ICONWARNING | MB_OK);
		return;
	}

	// 上传前把对话框当前值固化到本地 ini，保证备份内容与界面一致
	if (op == WEBDAV_OP_UPLOAD)
	{
		g_data.m_setting_data = m_data;
		g_data.SaveConfig();
	}

	auto* result = new WebDavAsyncResult();
	result->op = op;
	result->remoteFile = m_webdav_restore_file; // 仅 WEBDAV_OP_RESTORE 使用
	HWND hWnd = GetSafeHwnd();
	SettingData data = m_data;

	std::thread([hWnd, result, data]() {
		AFX_MANAGE_STATE(AfxGetStaticModuleState());
		switch (result->op)
		{
		case WEBDAV_OP_TEST:
			result->ok = CWebDavSync::TestConnection(data, result->errMsg);
			break;
		case WEBDAV_OP_UPLOAD:
			result->ok = CWebDavSync::UploadBackup(data, result->errMsg);
			break;
		case WEBDAV_OP_LIST:
			result->ok = CWebDavSync::ListBackups(data, result->backups, result->errMsg);
			break;
		case WEBDAV_OP_RESTORE:
			result->ok = CWebDavSync::DownloadBackupData(data, result->remoteFile, result->downloadedData, result->errMsg);
			break;
		default:
			break;
		}
		if (!::PostMessage(hWnd, WM_APP_WEBDAV_RESULT, 0, (LPARAM)result))
			delete result; // 对话框已关闭，结果无人接收
	}).detach();

	// 后台执行期间禁用操作按钮并显示进行中状态
	m_webdav_busy = true;
	const UINT btnIds[] = { IDC_WEBDAV_TEST_BTN, IDC_WEBDAV_UPLOAD_BTN, IDC_WEBDAV_DOWNLOAD_BTN };
	for (UINT id : btnIds)
	{
		CWnd* pBtn = GetDlgItem(id);
		if (pBtn && pBtn->GetSafeHwnd())
			pBtn->EnableWindow(FALSE);
	}
	UINT targetBtn = IDC_WEBDAV_TEST_BTN;
	const wchar_t* busyText = L"连接中...";
	if (op == WEBDAV_OP_UPLOAD)
	{
		targetBtn = IDC_WEBDAV_UPLOAD_BTN;
		busyText = L"上传中...";
	}
	else if (op == WEBDAV_OP_LIST)
	{
		targetBtn = IDC_WEBDAV_DOWNLOAD_BTN;
		busyText = L"获取列表...";
	}
	else if (op == WEBDAV_OP_RESTORE)
	{
		targetBtn = IDC_WEBDAV_DOWNLOAD_BTN;
		busyText = L"恢复中...";
	}
	CWnd* pOpBtn = GetDlgItem(targetBtn);
	if (pOpBtn && pOpBtn->GetSafeHwnd())
		pOpBtn->SetWindowText(busyText);
}

LRESULT CManagerDialog::OnWebDavResult(WPARAM, LPARAM lParam)
{
	std::unique_ptr<WebDavAsyncResult> result(reinterpret_cast<WebDavAsyncResult*>(lParam));
	if (!result)
		return 0;

	m_webdav_busy = false;
	const UINT btnIds[] = { IDC_WEBDAV_TEST_BTN, IDC_WEBDAV_UPLOAD_BTN, IDC_WEBDAV_DOWNLOAD_BTN };
	for (UINT id : btnIds)
	{
		CWnd* pBtn = GetDlgItem(id);
		if (pBtn && pBtn->GetSafeHwnd())
			pBtn->EnableWindow(TRUE);
	}
	SetDlgItemText(IDC_WEBDAV_TEST_BTN, L"测试连接");
	SetDlgItemText(IDC_WEBDAV_UPLOAD_BTN, L"立即上传备份");
	SetDlgItemText(IDC_WEBDAV_DOWNLOAD_BTN, L"从云端恢复");
	Invalidate();

	switch (result->op)
	{
	case WEBDAV_OP_TEST:
		if (result->ok)
			MessageBox(L"WebDAV 云端服务器连接与认证成功！", L"连接成功", MB_ICONINFORMATION | MB_OK);
		else
			MessageBox((L"WebDAV 连接失败：\n" + result->errMsg).c_str(), L"连接失败", MB_ICONERROR | MB_OK);
		break;

	case WEBDAV_OP_UPLOAD:
		if (result->ok)
		{
			time_t now = time(nullptr);
			tm t{};
			localtime_s(&t, &now);
			wchar_t timeBuf[64]{};
			wcsftime(timeBuf, 64, L"%Y-%m-%d %H:%M:%S", &t);
			m_data.m_webdav_last_sync_time = timeBuf;

			g_data.m_setting_data = m_data;
			g_data.SaveConfig();

			Invalidate();
			MessageBox(L"已成功将全部配置与自选股备份至 WebDAV 云端（本次以时间戳独立存档）！", L"备份成功", MB_ICONINFORMATION | MB_OK);
		}
		else
		{
			MessageBox((L"上传备份失败：\n" + result->errMsg).c_str(), L"备份失败", MB_ICONERROR | MB_OK);
		}
		break;

	case WEBDAV_OP_LIST:
		if (!result->ok)
		{
			MessageBox((L"获取云端备份列表失败：\n" + result->errMsg).c_str(), L"获取失败", MB_ICONERROR | MB_OK);
			break;
		}
		if (result->backups.empty())
		{
			MessageBox(L"云端暂无历史备份，请先点击「立即上传备份」。", L"云端备份列表为空", MB_ICONINFORMATION | MB_OK);
			break;
		}

		// 弹出备份选择列表，选中并确认覆盖后再下载应用
		{
			CBackupListDialog dlg(result->backups, this);
			if (dlg.DoModal(this) == IDOK && !dlg.m_selectedFile.empty())
			{
				CString confirmMsg;
				confirmMsg.Format(_T("已选择备份：%s\n恢复将覆盖本地当前的股票列表与全部配置，是否继续？"),
					dlg.m_selectedName.c_str());
				if (MessageBox(confirmMsg, L"确认恢复", MB_ICONQUESTION | MB_YESNO) != IDYES)
					break;
				m_webdav_restore_file = dlg.m_selectedFile;
				m_webdav_restore_name = dlg.m_selectedName;
				StartWebDavAsync(WEBDAV_OP_RESTORE);
			}
		}
		break;

	case WEBDAV_OP_RESTORE:
		if (result->ok)
			ApplyWebDavRestore(result->downloadedData, m_webdav_restore_name);
		else
			MessageBox((L"从云端恢复失败：\n" + result->errMsg).c_str(), L"恢复失败", MB_ICONERROR | MB_OK);
		m_webdav_restore_file.clear();
		m_webdav_restore_name.clear();
		break;
	}
	return 0;
}

void CManagerDialog::ApplyWebDavRestore(const std::string& data, const std::wstring& backupName)
{
	// 将云端备份内容写入本地 INI 并重载配置
	std::wstring configPath = g_data.GetConfigPath();
	std::ofstream outFile(configPath, std::ios::binary | std::ios::trunc);
	if (!outFile.is_open())
	{
		MessageBox((L"无法写入本地配置文件: " + configPath).c_str(), L"恢复失败", MB_ICONERROR | MB_OK);
		return;
	}
	outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
	outFile.close();

	g_data.LoadConfig(L"");
	Stock::Instance().SendStockInfoRequest();

	m_data = g_data.m_setting_data;

	SetCheck(IDC_FULL_DAY_CHECK, m_data.m_full_day);
	SetCheck(IDC_SHOW_FLUCTUATION_CHECK, m_data.m_show_fluctuation);
	SetCheck(IDC_SHOW_TODAY_PROFIT_CHECK, m_data.m_show_today_profit);
	SetCheck(IDC_USE_SOCKS5_PROXY_CHECK, m_data.m_use_socks5_proxy);
	SetDlgItemText(IDC_SOCKS5_PROXY_EDIT, m_data.m_socks5_proxy.c_str());

	CString strKlineW, strKlineH;
	strKlineW.Format(_T("%d"), static_cast<int>(m_data.m_kline_width));
	SetDlgItemText(IDC_KLINE_WIDTH_EDIT, strKlineW);
	strKlineH.Format(_T("%d"), static_cast<int>(m_data.m_kline_height));
	SetDlgItemText(IDC_KLINE_HEIGHT_EDIT, strKlineH);

	int selArea = m_data.m_display_area;
	if (selArea < AREA_LEFT_TOP || selArea > AREA_CENTER)
		selArea = AREA_RIGHT_BOTTOM;
	m_display_area_combo.SetCurSel(selArea);

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

	CString okMsg;
	if (backupName.empty())
		okMsg = L"已成功从 WebDAV 云端恢复配置并加载！";
	else
		okMsg.Format(_T("已成功恢复 %s 的云端备份并加载！"), backupName.c_str());
	MessageBox(okMsg, L"恢复成功", MB_ICONINFORMATION | MB_OK);
}

void CManagerDialog::OnBnClickedWebDavTestBtn()
{
	// 网络操作在取数线程异步执行，避免阻塞 UI（宿主主线程上等待光标等
	// MFC 设施会因插件模块状态缺失直接崩溃，详见文件头说明）
	StartWebDavAsync(WEBDAV_OP_TEST);
}

void CManagerDialog::OnBnClickedWebDavUploadBtn()
{
	StartWebDavAsync(WEBDAV_OP_UPLOAD);
}

void CManagerDialog::OnBnClickedWebDavDownloadBtn()
{
	// 先拉取云端历史备份列表，用户在弹出的列表中选择要恢复的一份，
	// 选中并确认覆盖后才下载应用（见 OnWebDavResult 的 WEBDAV_OP_LIST 分支）
	StartWebDavAsync(WEBDAV_OP_LIST);
}

// 提交全部设置：读取控件值 → 写回 g_data → 保存 INI → 热更新
// 「确定」按钮与内嵌模式的即时生效（ApplyIfEmbedded）共用此入口
void CManagerDialog::ApplySettings()
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

	int selArea = m_display_area_combo.GetCurSel();
	if (selArea >= AREA_LEFT_TOP && selArea <= AREA_CENTER)
		m_data.m_display_area = selArea;

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
		std::thread([curData]() {
			AFX_MANAGE_STATE(AfxGetStaticModuleState());
			std::wstring err;
			CWebDavSync::UploadBackup(curData, err);
		}).detach();
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

	Stock::Instance().NotifyFloatingWndUpdate();
	Stock::Instance().NotifyFloatingWndOrderBookUpdate();

	ApplyOpacity(m_data.m_window_opacity);
}

void CManagerDialog::OnBnClickedOk()
{
	ApplySettings();

	// 内嵌子窗口模式：配置已写回 g_data，由宿主悬浮窗收起设置视图（不走模态 EndDialog）
	if (m_as_child && m_on_settings_closed)
	{
		m_on_settings_closed(true);
		return;
	}
	CDialog::OnOK();
}

void CManagerDialog::OnBnClickedCancel()
{
	// 内嵌子窗口模式：设置已即时生效，收起视图即可（无「取消」丢弃语义）
	if (m_as_child && m_on_settings_closed)
	{
		m_on_settings_closed(true);
		return;
	}
	CDialog::OnCancel();
}

// ===================== 暗色主题自绘辅助 =====================

BEGIN_MESSAGE_MAP(CFlatHeaderCtrl, CHeaderCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, &CFlatHeaderCtrl::OnCustomDraw)
END_MESSAGE_MAP()

void CFlatHeaderCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect clientRect;
	GetClientRect(&clientRect);

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	CBitmap memBmp;
	memBmp.CreateCompatibleBitmap(&dc, clientRect.Width(), clientRect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	// 填充全表头深色底 (#1C1F27)，彻底消除右侧白色占位
	memDC.FillSolidRect(&clientRect, RGB(28, 31, 39));
	memDC.FillSolidRect(CRect(clientRect.left, clientRect.bottom - 1, clientRect.right, clientRect.bottom), COLOR_DARK_GRAY_BORDER);

	int itemCount = GetItemCount();
	CFont* pFont = GetFont();
	if (pFont == nullptr || pFont->GetSafeHandle() == nullptr)
		pFont = CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT));
	CFont* pOldFont = memDC.SelectObject(pFont);
	memDC.SetBkMode(TRANSPARENT);
	memDC.SetTextColor(COLOR_TEXT_MUTED);

	for (int i = 0; i < itemCount; ++i)
	{
		CRect itemRect;
		GetItemRect(i, &itemRect);

		// 右侧细分割线
		memDC.FillSolidRect(CRect(itemRect.right - 1, itemRect.top + g_data.DPI(4), itemRect.right, itemRect.bottom - g_data.DPI(4)), COLOR_DARK_GRAY_BORDER);

		wchar_t buf[128] = { 0 };
		HDITEM it = { 0 };
		it.mask = HDI_TEXT | HDI_FORMAT;
		it.pszText = buf;
		it.cchTextMax = 128;
		if (GetItem(i, &it))
		{
			CRect textRc = itemRect;
			textRc.DeflateRect(g_data.DPI(6), 0);

			UINT dtFlags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
			if ((it.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)
				dtFlags |= DT_RIGHT;
			else if ((it.fmt & HDF_JUSTIFYMASK) == HDF_CENTER)
				dtFlags |= DT_CENTER;
			else
				dtFlags |= DT_LEFT;

			memDC.DrawText(buf, -1, &textRc, dtFlags);
		}
	}

	memDC.SelectObject(pOldFont);
	dc.BitBlt(0, 0, clientRect.Width(), clientRect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

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
		it.mask = HDI_TEXT | HDI_FORMAT;
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
			textRc.DeflateRect(g_data.DPI(6), 0);

			UINT dtFlags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
			if ((it.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)
				dtFlags |= DT_RIGHT;
			else if ((it.fmt & HDF_JUSTIFYMASK) == HDF_CENTER)
				dtFlags |= DT_CENTER;
			else
				dtFlags |= DT_LEFT;

			CString headerText(buf);
			dc.DrawText(headerText, textRc, dtFlags);
			dc.SelectObject(pOldFont);
		}

		dc.Detach();
		*pResult = CDRF_SKIPDEFAULT;
	}
}

// ===================== CDarkPopupMenu 暗色风格弹出菜单 =====================

BEGIN_MESSAGE_MAP(CDarkPopupMenu, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONUP()
	ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

BOOL CDarkPopupMenu::CreatePopup(CWnd* pParent)
{
	CString className = AfxRegisterWndClass(CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW | CS_SAVEBITS, ::LoadCursor(nullptr, IDC_ARROW), (HBRUSH)::GetStockObject(BLACK_BRUSH), nullptr);
	return CreateEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, className, _T("DarkPopupMenu"), WS_POPUP, CRect(0, 0, 0, 0), pParent, 0);
}

int CDarkPopupMenu::TrackMenu(const CPoint& screenPt, const std::vector<MenuItem>& items, int minWidth)
{
	m_items = items;
	m_selected_id = 0;
	m_hover_idx = -1;
	m_is_open = true;

	if (m_items.empty() || !GetSafeHwnd())
		return 0;

	CDC* pDC = GetDC();
	Gdiplus::Graphics g(pDC->GetSafeHdc());
	Gdiplus::Font font(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

	int maxTextW = 0;
	for (const auto& item : m_items)
	{
		if (!item.isSeparator)
		{
			Gdiplus::RectF bounds;
			g.MeasureString(item.text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
			maxTextW = max(maxTextW, static_cast<int>(bounds.Width));
		}
	}
	ReleaseDC(pDC);

	int padX = g_data.DPI(32);
	int menuW = max(minWidth, maxTextW + padX + g_data.DPI(16));

	int itemH = g_data.DPI(28);
	int sepH = g_data.DPI(7);
	int padY = g_data.DPI(4);

	int totalH = padY * 2;
	for (const auto& item : m_items)
	{
		totalH += item.isSeparator ? sepH : itemH;
	}

	int screenW = GetSystemMetrics(SM_CXSCREEN);
	int screenH = GetSystemMetrics(SM_CYSCREEN);
	int x = screenPt.x;
	int y = screenPt.y;
	if (x + menuW > screenW) x = screenW - menuW - g_data.DPI(4);
	if (y + totalH > screenH) y = screenPt.y - totalH;
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	SetWindowPos(&wndTopMost, x, y, menuW, totalH, SWP_SHOWWINDOW);
	SetCapture();

	MSG msg;
	while (m_is_open && ::GetMessage(&msg, nullptr, 0, 0))
	{
		if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
		{
			// 只有点击落在菜单窗口客户区内才交给 OnLButtonUp 按 hover 项处理，
			// 其余情况（点击其他窗口，或 SetCapture 捕获到的菜单外点击）视为在
			// 菜单外，关闭菜单。（不能用 msg.pt 判断：PostMessage 合成消息的 pt 不可靠）
			bool insidePopup = false;
			if (msg.hwnd == GetSafeHwnd())
			{
				CPoint pt((DWORD)msg.lParam);
				CRect rcClient;
				GetClientRect(&rcClient);
				insidePopup = rcClient.PtInRect(pt);
			}
			if (!insidePopup)
			{
				m_is_open = false;
				break;
			}
		}
		else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
		{
			m_is_open = false;
			break;
		}

		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}

	if (GetCapture() == this)
		ReleaseCapture();

	ShowWindow(SW_HIDE);
	DestroyWindow();
	return m_selected_id;
}

void CDarkPopupMenu::OnPaint()
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

	Gdiplus::SolidBrush bg(Gdiplus::Color(255, 24, 27, 34)); // #181B22
	g.FillRectangle(&bg, 0, 0, clientRect.Width(), clientRect.Height());
	Gdiplus::Pen border(Gdiplus::Color(255, 42, 48, 63), 1.0f); // #2A303F
	g.DrawRectangle(&border, 0, 0, clientRect.Width() - 1, clientRect.Height() - 1);

	int padY = g_data.DPI(4);
	int curY = padY;
	int itemH = g_data.DPI(28);
	int sepH = g_data.DPI(7);

	Gdiplus::Font font(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font checkFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::StringFormat sf;
	sf.SetAlignment(Gdiplus::StringAlignmentNear);
	sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	for (size_t i = 0; i < m_items.size(); ++i)
	{
		const auto& item = m_items[i];
		if (item.isSeparator)
		{
			int sepLineY = curY + sepH / 2;
			Gdiplus::Pen sepPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
			g.DrawLine(&sepPen, g_data.DPI(8), sepLineY, clientRect.Width() - g_data.DPI(8), sepLineY);
			curY += sepH;
		}
		else
		{
			CRect itemRc(g_data.DPI(3), curY, clientRect.Width() - g_data.DPI(3), curY + itemH);
			bool isHover = (static_cast<int>(i) == m_hover_idx);

			if (isHover)
			{
				Gdiplus::SolidBrush hoverBg(Gdiplus::Color(255, 37, 99, 235)); // #2563EB
				g.FillRectangle(&hoverBg, itemRc.left, itemRc.top, itemRc.Width(), itemRc.Height());
			}

			// 勾选标识 ✓
			if (item.isChecked)
			{
				Gdiplus::SolidBrush checkBrush(isHover ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 59, 130, 246));
				Gdiplus::RectF checkRf(static_cast<Gdiplus::REAL>(itemRc.left + g_data.DPI(4)), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(g_data.DPI(16)), static_cast<Gdiplus::REAL>(itemRc.Height()));
				Gdiplus::StringFormat checkSf;
				checkSf.SetAlignment(Gdiplus::StringAlignmentCenter);
				checkSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
				Icons::Draw(g, Icons::Id::Check, checkRf, isHover ? RGB(255, 255, 255) : RGB(59, 130, 246));
			}

			// 文本
			Gdiplus::Color textCol = isHover ? Gdiplus::Color(255, 255, 255, 255) :
				(item.isDestructive ? Gdiplus::Color(255, 239, 68, 68) : Gdiplus::Color(255, 226, 232, 240));
			Gdiplus::SolidBrush textBrush(textCol);

			int textLeft = itemRc.left + g_data.DPI(22);
			Gdiplus::RectF textRf(static_cast<Gdiplus::REAL>(textLeft), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(itemRc.right - textLeft - g_data.DPI(6)), static_cast<Gdiplus::REAL>(itemRc.Height()));
			g.DrawString(item.text.c_str(), -1, &font, textRf, &sf, &textBrush);

			curY += itemH;
		}
	}

	dc.BitBlt(0, 0, clientRect.Width(), clientRect.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

void CDarkPopupMenu::OnMouseMove(UINT nFlags, CPoint point)
{
	int padY = g_data.DPI(4);
	int curY = padY;
	int itemH = g_data.DPI(28);
	int sepH = g_data.DPI(7);

	int oldHover = m_hover_idx;
	m_hover_idx = -1;

	for (size_t i = 0; i < m_items.size(); ++i)
	{
		if (m_items[i].isSeparator)
		{
			curY += sepH;
		}
		else
		{
			CRect itemRc(0, curY, 10000, curY + itemH);
			if (point.y >= itemRc.top && point.y < itemRc.bottom)
			{
				m_hover_idx = static_cast<int>(i);
				break;
			}
			curY += itemH;
		}
	}

	if (oldHover != m_hover_idx)
		Invalidate();

	CWnd::OnMouseMove(nFlags, point);
}

void CDarkPopupMenu::OnMouseLeave()
{
	m_hover_idx = -1;
	Invalidate();
	CWnd::OnMouseLeave();
}

void CDarkPopupMenu::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_hover_idx >= 0 && m_hover_idx < static_cast<int>(m_items.size()))
	{
		if (!m_items[m_hover_idx].isSeparator)
		{
			m_selected_id = m_items[m_hover_idx].id;
			m_is_open = false;
			return;
		}
	}
	CWnd::OnLButtonUp(nFlags, point);
}

void CDarkPopupMenu::OnKillFocus(CWnd* pNewWnd)
{
	m_is_open = false;
	CWnd::OnKillFocus(pNewWnd);
}

// ===================== CSearchResultDropdown 搜索结果与分组添加一体化暗色浮窗 =====================

BEGIN_MESSAGE_MAP(CSearchResultDropdown, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

BOOL CSearchResultDropdown::CreatePopup(CWnd* pParent)
{
	CString className = AfxRegisterWndClass(CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), (HBRUSH)::GetStockObject(BLACK_BRUSH), nullptr);
	return CreateEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"", WS_POPUP, CRect(0, 0, 0, 0), pParent, 0);
}

int CSearchResultDropdown::GetResultsWidth() const
{
	return g_data.DPI(225);
}

int CSearchResultDropdown::GetMenuWidth() const
{
	return g_data.DPI(155);
}

void CSearchResultDropdown::ShowResults(const std::vector<StockSearchResult>& results, const CRect& editScreenRc, const std::vector<GroupMenuItem>& groupItems)
{
	m_results = results;
	m_edit_screen_rc = editScreenRc;
	m_group_items = groupItems;
	m_hover_item = -1;
	m_hover_btn = -1;
	m_hover_group_idx = -1;
	m_selected_stock_idx = -1;

	if (m_results.empty())
	{
		HidePopup();
		return;
	}

	UpdatePopupPosition();
}

void CSearchResultDropdown::HidePopup()
{
	m_selected_stock_idx = -1;
	m_hover_item = -1;
	m_hover_btn = -1;
	m_hover_group_idx = -1;
	if (GetSafeHwnd())
		ShowWindow(SW_HIDE);
}

void CSearchResultDropdown::UpdatePopupPosition()
{
	if (m_results.empty() || !GetSafeHwnd())
	{
		ShowWindow(SW_HIDE);
		return;
	}

	int resW = GetResultsWidth();
	int menuW = GetMenuWidth();
	int totalW = (m_selected_stock_idx >= 0) ? (resW + menuW) : resW;

	int maxItems = min(static_cast<int>(m_results.size()), 8);
	int itemH = g_data.DPI(34);
	int searchH = maxItems * itemH + 2;

	int menuH = 0;
	if (m_selected_stock_idx >= 0)
	{
		menuH = static_cast<int>(m_group_items.size()) * g_data.DPI(30) + g_data.DPI(28);
	}
	int totalH = max(searchH, menuH);

	int screenW = GetSystemMetrics(SM_CXSCREEN);
	int screenH = GetSystemMetrics(SM_CYSCREEN);

	int dropX = m_edit_screen_rc.right - resW;
	if (m_selected_stock_idx >= 0)
	{
		if (dropX + totalW > screenW - 10)
		{
			dropX = screenW - totalW - 10;
		}
	}
	if (dropX < 10) dropX = 10;

	int dropY = m_edit_screen_rc.bottom + 2;
	if (dropY + totalH > screenH - 10)
	{
		dropY = m_edit_screen_rc.top - totalH - 2;
	}
	if (dropY < 10) dropY = 10;

	SetWindowPos(&CWnd::wndTopMost, dropX, dropY, totalW, totalH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
	Invalidate();
}

void CSearchResultDropdown::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() <= 0 || rc.Height() <= 0) return;

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	CBitmap memBmp;
	memBmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	Gdiplus::Graphics g(memDC.GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

	// 1. 全局深色底与外边框
	Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 20, 22, 29));
	g.FillRectangle(&bgBrush, 0, 0, rc.Width(), rc.Height());

	Gdiplus::Pen borderPen(Gdiplus::Color(255, 59, 130, 246), 1.0f);
	g.DrawRectangle(&borderPen, 0.5f, 0.5f, static_cast<Gdiplus::REAL>(rc.Width() - 1), static_cast<Gdiplus::REAL>(rc.Height() - 1));

	int resW = (m_selected_stock_idx >= 0) ? GetResultsWidth() : rc.Width();
	int itemH = g_data.DPI(34);

	Gdiplus::Font nameFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::Font codeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font badgeFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(8.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::Font plusFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

	Gdiplus::StringFormat sfNear;
	sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
	sfNear.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sfNear.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
	sfNear.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

	Gdiplus::StringFormat sfCenter;
	sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
	sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sfCenter.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

	// 2. 绘制左侧搜索结果项
	int maxItems = min(static_cast<int>(m_results.size()), 8);
	for (int i = 0; i < maxItems; ++i)
	{
		int itemY = i * itemH + 1;
		CRect itemRc(1, itemY, resW - 1, itemY + itemH);

		bool isSelected = (m_selected_stock_idx == i);
		bool isHover = (m_hover_item == i);

		if (isSelected)
		{
			Gdiplus::SolidBrush selBrush(Gdiplus::Color(255, 37, 99, 235));
			g.FillRectangle(&selBrush, static_cast<Gdiplus::REAL>(itemRc.left), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(itemRc.Width()), static_cast<Gdiplus::REAL>(itemRc.Height()));
		}
		else if (isHover)
		{
			Gdiplus::SolidBrush hoverBrush(Gdiplus::Color(255, 30, 41, 59));
			g.FillRectangle(&hoverBrush, static_cast<Gdiplus::REAL>(itemRc.left), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(itemRc.Width()), static_cast<Gdiplus::REAL>(itemRc.Height()));
		}

		// 分隔线
		if (i > 0)
		{
			Gdiplus::Pen divPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
			g.DrawLine(&divPen, static_cast<Gdiplus::REAL>(itemRc.left + g_data.DPI(6)), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(itemRc.right - g_data.DPI(6)), static_cast<Gdiplus::REAL>(itemRc.top));
		}

		const auto& stock = m_results[i];

		// 交易所徽标
		std::wstring exch = stock.exchange;
		if (exch.empty()) exch = CCommon::GetExchangeName(stock.fullCode);
		std::wstring pureCode = stock.code;
		if (pureCode.empty()) pureCode = CCommon::GetPureCode(stock.fullCode);

		int badgeW = g_data.DPI(38);
		int badgeH = g_data.DPI(16);
		int badgeX = itemRc.left + g_data.DPI(6);
		int badgeY = itemRc.top + (itemRc.Height() - badgeH) / 2;
		Gdiplus::RectF badgeRf(static_cast<Gdiplus::REAL>(badgeX), static_cast<Gdiplus::REAL>(badgeY), static_cast<Gdiplus::REAL>(badgeW), static_cast<Gdiplus::REAL>(badgeH));

		Gdiplus::SolidBrush badgeBg(isSelected ? Gdiplus::Color(255, 30, 41, 59) : Gdiplus::Color(255, 37, 99, 235));
		g.FillRectangle(&badgeBg, badgeRf);

		Gdiplus::SolidBrush badgeTxt(Gdiplus::Color(255, 255, 255, 255));
		// GDI+ 行框居中含雅黑 descent 空白区，汉字视觉偏上，文字矩形下移补偿
		Gdiplus::RectF badgeTxtRf = badgeRf;
		badgeTxtRf.Y += static_cast<Gdiplus::REAL>(g_data.DPI(1));
		g.DrawString(exch.c_str(), -1, &badgeFont, badgeTxtRf, &sfCenter, &badgeTxt);

		// 代码
		int codeX = badgeX + badgeW + g_data.DPI(5);
		int codeW = g_data.DPI(46);
		Gdiplus::RectF codeRf(static_cast<Gdiplus::REAL>(codeX), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(codeW), static_cast<Gdiplus::REAL>(itemRc.Height()));
		Gdiplus::SolidBrush codeTxt(isSelected ? Gdiplus::Color(255, 226, 232, 240) : Gdiplus::Color(255, 148, 163, 184));
		g.DrawString(pureCode.c_str(), -1, &codeFont, codeRf, &sfNear, &codeTxt);

		// 加号按钮 [+]
		int btnSize = g_data.DPI(20);
		int btnX = itemRc.right - btnSize - g_data.DPI(6);
		int btnY = itemRc.top + (itemRc.Height() - btnSize) / 2;
		Gdiplus::RectF btnRf(static_cast<Gdiplus::REAL>(btnX), static_cast<Gdiplus::REAL>(btnY), static_cast<Gdiplus::REAL>(btnSize), static_cast<Gdiplus::REAL>(btnSize));

		bool isBtnHover = (m_hover_btn == i) || isSelected;
		Gdiplus::SolidBrush plusBg(isBtnHover ? (isSelected ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 37, 99, 235)) : Gdiplus::Color(255, 30, 41, 59));
		g.FillRectangle(&plusBg, btnRf);

		Gdiplus::Pen plusBorder(isBtnHover ? Gdiplus::Color(255, 96, 165, 250) : Gdiplus::Color(255, 71, 85, 105), 1.0f);
		g.DrawRectangle(&plusBorder, btnRf);

		Gdiplus::SolidBrush plusTxt(isBtnHover ? (isSelected ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 255, 255, 255)) : Gdiplus::Color(255, 148, 163, 184));
		Icons::Draw(g, isSelected ? Icons::Id::Check : Icons::Id::Plus, btnRf,
			isBtnHover ? (isSelected ? RGB(37, 99, 235) : RGB(255, 255, 255)) : RGB(148, 163, 184));

		// 名称
		int nameX = codeX + codeW + g_data.DPI(5);
		int nameW = max(10, btnX - nameX - g_data.DPI(4));
		Gdiplus::RectF nameRf(static_cast<Gdiplus::REAL>(nameX), static_cast<Gdiplus::REAL>(itemRc.top), static_cast<Gdiplus::REAL>(nameW), static_cast<Gdiplus::REAL>(itemRc.Height()));
		Gdiplus::SolidBrush nameTxt(Gdiplus::Color(255, 255, 255, 255));
		g.DrawString(stock.name.c_str(), -1, &nameFont, nameRf, &sfNear, &nameTxt);
	}

	// 3. 绘制右侧分组选择面板
	if (m_selected_stock_idx >= 0)
	{
		// 垂直分割线
		Gdiplus::Pen vSepPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
		g.DrawLine(&vSepPen, static_cast<Gdiplus::REAL>(resW), 0.0f, static_cast<Gdiplus::REAL>(resW), static_cast<Gdiplus::REAL>(rc.Height()));

		// 右侧背景
		Gdiplus::SolidBrush menuBg(Gdiplus::Color(255, 16, 18, 24));
		g.FillRectangle(&menuBg, static_cast<Gdiplus::REAL>(resW + 1), 1.0f, static_cast<Gdiplus::REAL>(rc.Width() - resW - 2), static_cast<Gdiplus::REAL>(rc.Height() - 2));

		int curY = g_data.DPI(6);
		// 标题
		Gdiplus::Font titleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(8.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 148, 163, 184));
		g.DrawString(L"添加到目标分组：", -1, &titleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(resW + g_data.DPI(10)), static_cast<Gdiplus::REAL>(curY)), &titleBrush);

		curY += g_data.DPI(18);

		int grpH = g_data.DPI(28);
		int grpSepH = g_data.DPI(7);

		Gdiplus::Font grpFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		Gdiplus::Font actFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(9.5)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

		for (size_t j = 0; j < m_group_items.size(); ++j)
		{
			const auto& it = m_group_items[j];
			if (it.isSeparator)
			{
				Gdiplus::Pen sepPen(Gdiplus::Color(255, 38, 42, 54), 1.0f);
				int lineY = curY + grpSepH / 2;
				g.DrawLine(&sepPen, static_cast<Gdiplus::REAL>(resW + g_data.DPI(8)), static_cast<Gdiplus::REAL>(lineY),
					static_cast<Gdiplus::REAL>(rc.right - g_data.DPI(8)), static_cast<Gdiplus::REAL>(lineY));
				curY += grpSepH;
			}
			else
			{
				CRect itemRc(resW + 1, curY, rc.right - 1, curY + grpH);
				bool isHover = (m_hover_group_idx == static_cast<int>(j));

				if (isHover)
				{
					Gdiplus::SolidBrush hoverBrush(Gdiplus::Color(255, 30, 41, 59));
					g.FillRectangle(&hoverBrush, static_cast<Gdiplus::REAL>(itemRc.left), static_cast<Gdiplus::REAL>(itemRc.top),
						static_cast<Gdiplus::REAL>(itemRc.Width()), static_cast<Gdiplus::REAL>(itemRc.Height()));
				}

				if (it.isAction)
				{
					Gdiplus::RectF txtRf(static_cast<Gdiplus::REAL>(itemRc.left + g_data.DPI(10)), static_cast<Gdiplus::REAL>(itemRc.top),
						static_cast<Gdiplus::REAL>(itemRc.Width() - g_data.DPI(14)), static_cast<Gdiplus::REAL>(itemRc.Height()));
					Gdiplus::SolidBrush actionTxt(isHover ? Gdiplus::Color(255, 147, 197, 253) : Gdiplus::Color(255, 96, 165, 250));
					g.DrawString(it.text.c_str(), -1, &actFont, txtRf, &sfNear, &actionTxt);
				}
				else
				{
					int dotSize = g_data.DPI(5);
					int dotX = itemRc.left + g_data.DPI(10);
					int dotY = itemRc.top + (itemRc.Height() - dotSize) / 2;
					Gdiplus::SolidBrush dotBrush(isHover ? Gdiplus::Color(255, 96, 165, 250) : Gdiplus::Color(255, 71, 85, 105));
					g.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);

					int txtLeft = dotX + dotSize + g_data.DPI(6);
					Gdiplus::RectF txtRf(static_cast<Gdiplus::REAL>(txtLeft), static_cast<Gdiplus::REAL>(itemRc.top),
						static_cast<Gdiplus::REAL>(itemRc.right - txtLeft - g_data.DPI(4)), static_cast<Gdiplus::REAL>(itemRc.Height()));
					Gdiplus::SolidBrush txtBrush(isHover ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 226, 232, 240));
					g.DrawString(it.text.c_str(), -1, &grpFont, txtRf, &sfNear, &txtBrush);
				}

				curY += grpH;
			}
		}
	}

	dc.BitBlt(0, 0, rc.Width(), rc.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

void CSearchResultDropdown::OnMouseMove(UINT nFlags, CPoint point)
{
	int resW = (m_selected_stock_idx >= 0) ? GetResultsWidth() : 99999;
	int oldHover = m_hover_item;
	int oldBtnHover = m_hover_btn;
	int oldGroupHover = m_hover_group_idx;

	if (point.x < resW)
	{
		int itemH = g_data.DPI(34);
		m_hover_item = point.y / itemH;
		if (m_hover_item < 0 || m_hover_item >= static_cast<int>(m_results.size()) || m_hover_item >= 8)
			m_hover_item = -1;

		int btnSize = g_data.DPI(20);
		int btnX = resW - btnSize - g_data.DPI(6);
		if (m_hover_item >= 0 && point.x >= btnX - g_data.DPI(4) && point.x <= resW)
			m_hover_btn = m_hover_item;
		else
			m_hover_btn = -1;

		m_hover_group_idx = -1;
	}
	else
	{
		m_hover_item = -1;
		m_hover_btn = -1;
		m_hover_group_idx = -1;

		int grpH = g_data.DPI(28);
		int grpSepH = g_data.DPI(7);
		int curY = g_data.DPI(24);

		for (size_t j = 0; j < m_group_items.size(); ++j)
		{
			if (m_group_items[j].isSeparator)
			{
				curY += grpSepH;
			}
			else
			{
				if (point.y >= curY && point.y < curY + grpH)
				{
					m_hover_group_idx = static_cast<int>(j);
					break;
				}
				curY += grpH;
			}
		}
	}

	if (m_hover_item != oldHover || m_hover_btn != oldBtnHover || m_hover_group_idx != oldGroupHover)
	{
		Invalidate();
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, GetSafeHwnd(), 0 };
		TrackMouseEvent(&tme);
	}
	CWnd::OnMouseMove(nFlags, point);
}

void CSearchResultDropdown::OnMouseLeave()
{
	m_hover_item = -1;
	m_hover_btn = -1;
	m_hover_group_idx = -1;
	Invalidate();
	CWnd::OnMouseLeave();
}

void CSearchResultDropdown::OnLButtonDown(UINT nFlags, CPoint point)
{
	int resW = (m_selected_stock_idx >= 0) ? GetResultsWidth() : 99999;

	if (point.x < resW)
	{
		int itemH = g_data.DPI(34);
		int clickedItem = point.y / itemH;
		if (clickedItem >= 0 && clickedItem < static_cast<int>(m_results.size()) && clickedItem < 8)
		{
			if (m_selected_stock_idx == clickedItem)
			{
				m_selected_stock_idx = -1; // 再次点击折叠
			}
			else
			{
				m_selected_stock_idx = clickedItem; // 展开分组选择面板
			}
			UpdatePopupPosition();
			return;
		}
	}
	else if (m_selected_stock_idx >= 0 && m_selected_stock_idx < static_cast<int>(m_results.size()))
	{
		int grpH = g_data.DPI(28);
		int grpSepH = g_data.DPI(7);
		int curY = g_data.DPI(24);

		for (size_t j = 0; j < m_group_items.size(); ++j)
		{
			if (m_group_items[j].isSeparator)
			{
				curY += grpSepH;
			}
			else
			{
				if (point.y >= curY && point.y < curY + grpH)
				{
					int selId = m_group_items[j].id;
					auto stock = m_results[m_selected_stock_idx];
					if (m_on_add_to_group)
					{
						m_on_add_to_group(stock, selId);
					}
					return;
				}
				curY += grpH;
			}
		}
	}

	CWnd::OnLButtonDown(nFlags, point);
}

void CSearchResultDropdown::OnLButtonUp(UINT nFlags, CPoint point)
{
	CWnd::OnLButtonUp(nFlags, point);
}

// ===================== CDarkComboBox 暗色自绘下拉框 =====================

BEGIN_MESSAGE_MAP(CDarkComboBox, CComboBox)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_WM_NCCALCSIZE()
	ON_WM_NCPAINT()
END_MESSAGE_MAP()

void CDarkComboBox::PreSubclassWindow()
{
	CComboBox::PreSubclassWindow();
	ModifyStyle(WS_BORDER, 0);
	ModifyStyleEx(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE, 0);
	::SetWindowTheme(GetSafeHwnd(), L"", L"");
}

void CDarkComboBox::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
	// 彻底消除 Windows ComboBox 原生 3D 边框预留边距，让 client 区域填满窗口
}

void CDarkComboBox::OnNcPaint()
{
}

void CDarkComboBox::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);

	// 只有闭合态需要绘制：字段区域高度 = 布局方设定的字段高（m_field_height），
	// 避免把整窗高度（含下拉列表预留区）画成一个高大的框体而下坠
	int fieldH = (m_field_height > 0) ? m_field_height : GetItemHeight(-1);
	if (fieldH > 0 && fieldH < rc.Height())
		rc.bottom = rc.top + fieldH;

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	CBitmap memBmp;
	memBmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	Gdiplus::Graphics g(memDC.GetSafeHdc());
	g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

	bool focused = (::GetFocus() == GetSafeHwnd());
	bool hovered = m_is_hovered;

	// 1. 背景底色 (#0D0F15，与单行输入框底色严格一致)
	Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 13, 15, 21));
	Gdiplus::RectF bgRect(0.0f, 0.0f, static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
	g.FillRectangle(&bgBrush, bgRect);

	// 2. 边框 (聚焦品牌蓝高亮，悬停亮灰，失焦暗灰)
	Gdiplus::Color borderColor = focused ? Gdiplus::Color(255, 37, 99, 235) : (hovered ? Gdiplus::Color(255, 75, 85, 105) : Gdiplus::Color(255, 52, 58, 72));
	Gdiplus::Pen borderPen(borderColor, 1.0f);
	g.DrawRectangle(&borderPen, 0.5f, 0.5f, static_cast<Gdiplus::REAL>(rc.Width() - 1), static_cast<Gdiplus::REAL>(rc.Height() - 1));

	// 3. 绘制选中项文字
	CString text;
	int curSel = GetCurSel();
	if (curSel != CB_ERR)
		GetLBText(curSel, text);
	else
		GetWindowText(text);

	Gdiplus::Font font(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(11.5)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 241, 245, 249));
	Gdiplus::StringFormat sf;
	sf.SetAlignment(Gdiplus::StringAlignmentNear);
	sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
	sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

	int textMargin = g_data.DPI(8);
	int arrowAreaWidth = g_data.DPI(22);
	Gdiplus::RectF textRect(static_cast<Gdiplus::REAL>(textMargin), 0.0f,
		static_cast<Gdiplus::REAL>(rc.Width() - textMargin - arrowAreaWidth), static_cast<Gdiplus::REAL>(rc.Height()));
	g.DrawString(text.GetString(), -1, &font, textRect, &sf, &textBrush);

	// 4. 绘制右侧下拉箭头 (精致折线 V 形)
	Gdiplus::Pen arrowPen(hovered || focused ? Gdiplus::Color(255, 241, 245, 249) : Gdiplus::Color(255, 148, 163, 184), 1.6f);
	arrowPen.SetStartCap(Gdiplus::LineCapRound);
	arrowPen.SetEndCap(Gdiplus::LineCapRound);
	arrowPen.SetLineJoin(Gdiplus::LineJoinRound);

	float arrowCenterX = static_cast<float>(rc.right - g_data.DPI(11));
	float arrowCenterY = static_cast<float>(rc.Height() / 2.0f);
	float arrowHalfW = static_cast<float>(g_data.DPI(3.5));
	float arrowHalfH = static_cast<float>(g_data.DPI(2.0));

	Gdiplus::PointF arrowPoints[3] = {
		Gdiplus::PointF(arrowCenterX - arrowHalfW, arrowCenterY - arrowHalfH),
		Gdiplus::PointF(arrowCenterX, arrowCenterY + arrowHalfH),
		Gdiplus::PointF(arrowCenterX + arrowHalfW, arrowCenterY - arrowHalfH)
	};
	g.DrawLines(&arrowPen, arrowPoints, 3);

	dc.BitBlt(0, 0, rc.Width(), rc.Height(), &memDC, 0, 0, SRCCOPY);
	memDC.SelectObject(pOldBmp);
}

void CDarkComboBox::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_is_hovered)
	{
		m_is_hovered = true;
		Invalidate();
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, GetSafeHwnd(), 0 };
		TrackMouseEvent(&tme);
	}
	CComboBox::OnMouseMove(nFlags, point);
}

void CDarkComboBox::OnMouseLeave()
{
	m_is_hovered = false;
	Invalidate();
	CComboBox::OnMouseLeave();
}

void CDarkComboBox::OnSetFocus(CWnd* pOldWnd)
{
	CComboBox::OnSetFocus(pOldWnd);
	Invalidate();
}

void CDarkComboBox::OnKillFocus(CWnd* pNewWnd)
{
	CComboBox::OnKillFocus(pNewWnd);
	Invalidate();
}

void CDarkComboBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
	CDC dc;
	dc.Attach(lp->hDC);
	CRect r = lp->rcItem;

	bool selected = (lp->itemState & ODS_SELECTED) != 0;
	bool isComboEdit = (lp->itemState & ODS_COMBOBOXEDIT) != 0;

	// 背景色：闭合状态与输入框底色 #0D0F15 一致；展开下拉项：选中深蓝 #1C2D4B，未选暗灰底 #14161D
	COLORREF bgClr;
	COLORREF textClr;

	if (isComboEdit || (int)lp->itemID < 0)
	{
		bgClr = RGB(13, 15, 21);
		textClr = RGB(241, 245, 249);
	}
	else
	{
		bgClr = selected ? RGB(28, 45, 75) : RGB(20, 22, 29);
		textClr = selected ? RGB(255, 255, 255) : RGB(226, 232, 240);
	}

	dc.FillSolidRect(&r, bgClr);

	CString text;
	if ((int)lp->itemID >= 0)
	{
		GetLBText(lp->itemID, text);
	}
	else
	{
		int curSel = GetCurSel();
		if (curSel != CB_ERR)
			GetLBText(curSel, text);
		else
			GetWindowText(text);
	}

	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(textClr);

	CFont font;
	font.CreatePointFont(90, _T("微软雅黑"));
	CFont* pOldFont = dc.SelectObject(&font);

	CRect textRect = r;
	textRect.left += g_data.DPI(8);
	dc.DrawText(text, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

	if (pOldFont)
		dc.SelectObject(pOldFont);

	dc.Detach();
}

void CDarkComboBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
	// 与单行输入框字段框高度一致(26)，否则关闭态框体会比输入框矮
	lp->itemHeight = g_data.DPI(26);
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
	case IDC_SHOW_FLUCTUATION_CHECK:
	case IDC_SHOW_TODAY_PROFIT_CHECK:
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
	return nID == IDOK || nID == IDC_MA_ADD_BTN || nID == 1197;
}

bool CManagerDialog::IsDestructiveBtn(UINT nID) const
{
	return nID == IDC_MGR_DEL_BTN || nID == 1199 || nID == IDC_RESET_DATA_BTN;
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
	CRect textRect(r);
	dc.DrawText(text, textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc.SelectObject(pOldFont);
}

// 输入框/列表的自绘边框：输入框铺内嵌底色再画边框（聚焦品牌蓝，失焦暗灰）；仅绘制当前可见控件
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

	// 输入框使用布局时登记的字段矩形（控件已在其内部居中缩小），列表仍用自身窗口矩形
	CRect rc;
	auto itField = m_editFieldRects.find(nID);
	if (!isList && itField != m_editFieldRects.end())
	{
		rc = itField->second;
	}
	else
	{
		pWnd->GetWindowRect(&rc);
		ScreenToClient(&rc);
	}

	if (!isList)
	{
		// 输入框字段底色：覆盖控件上下留白，与 OnCtlColor 的内嵌底色一致，形成整框观感
		Gdiplus::SolidBrush fieldFill(Gdiplus::Color(255, 13, 15, 21));
		g.FillRectangle(&fieldFill, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
			static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));

		Gdiplus::Pen pen(focused ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 52, 58, 72), 1.0f);
		g.DrawRectangle(&pen, 0.5f + rc.left, 0.5f + rc.top, static_cast<Gdiplus::REAL>(rc.Width() - 1), static_cast<Gdiplus::REAL>(rc.Height() - 1));
	}
	else
	{
		// 与输入框分支一致：0.5f 像素对齐 + 宽高减一，保证描边左右/上下对称贴合
		// 控件内容边缘（此前右侧描边外扩 1px，内容与边框之间留下一道暗缝，形似空列）
		rc.InflateRect(1, 1);
		Gdiplus::Pen pen(Gdiplus::Color(255, 52, 58, 72), 1.0f);
		g.DrawRectangle(&pen, 0.5f + rc.left, 0.5f + rc.top,
			static_cast<Gdiplus::REAL>(rc.Width() - 1), static_cast<Gdiplus::REAL>(rc.Height() - 1));
	}
}

// 在字段矩形内垂直居中放置单行编辑控件（控件高=18DPI居中，水平各留 6px 呼吸边距）
void CManagerDialog::PlaceEditInField(UINT nID, const CRect& fieldRect)
{
	m_editFieldRects[nID] = fieldRect;
	CWnd* pWnd = GetDlgItem(nID);
	if (pWnd && pWnd->GetSafeHwnd())
	{
		int editH = g_data.DPI(18);
		int editY = fieldRect.top + (fieldRect.Height() - editH) / 2;
		pWnd->MoveWindow(fieldRect.left + g_data.DPI(6), editY,
			max(10, fieldRect.Width() - g_data.DPI(12)), editH);
	}
}

// 章节标题：品牌蓝竖条 + 白色加粗文字（页头与卡片统一视觉语言）
void CManagerDialog::DrawSectionTitle(Gdiplus::Graphics& g, int x, int y, const std::wstring& title)
{
	Gdiplus::Font titleFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 241, 245, 249));

	// 竖条按文字墨迹垂直居中（不再用固定偏移，避免与标题错位）
	Gdiplus::SolidBrush barBrush(Gdiplus::Color(255, 37, 99, 235));
	Gdiplus::REAL barTop = CalcTitleBarTop(titleFont, title, y, g_data.DPI(12));
	g.FillRectangle(&barBrush, static_cast<Gdiplus::REAL>(x), barTop, static_cast<Gdiplus::REAL>(g_data.DPI(3)), static_cast<Gdiplus::REAL>(g_data.DPI(12)));

	g.DrawString(title.c_str(), -1, &titleFont, Gdiplus::PointF(static_cast<Gdiplus::REAL>(x + g_data.DPI(10)), static_cast<Gdiplus::REAL>(y)), &titleBrush);
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

// 输入框失焦：重绘边框并在内嵌模式下即时提交字段值
// （K线宽高 / 代理地址 / WebDAV 参数等输入字段没有离散的点击动作可挂钩）
void CManagerDialog::OnEditFocusLost()
{
	Invalidate(FALSE);
	ApplyIfEmbedded();
}
