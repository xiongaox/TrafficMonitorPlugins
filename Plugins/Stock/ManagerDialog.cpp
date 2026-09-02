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
#include <fstream>

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
	const int MA_CARD1_H = 116;  // 卡片1「当前均线周期」高度
	const int MA_CARD_GAP = 14;  // 卡片间距
	const int MA_CARD2_H = 118;  // 卡片2「添加均线周期」高度
	const int MA_CARD3_H = 104;  // 卡片3「快捷添加常用周期」高度
	const int MA_CARD4_H = 102;  // 卡片4「分时图布林带显示」高度（标题 + 单行三复选框）
	const int MA_FIELD_Y = 48;   // 卡片2 输入框字段上缘（相对卡片）
	const int MA_FIELD_X = 160;  // 卡片2 输入框字段左缘（相对卡片）
	const int MA_FIELD_W = 140;  // 卡片2 输入框字段宽
	const int MA_FIELD_H = 34;   // 卡片2 输入框/按钮高
	const int MA_ADDBTN_W = 104; // 卡片2「添加周期」按钮宽
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
		int sizeW = g_data.DPI(56);
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

	ON_BN_CLICKED(IDC_WEBDAV_TEST_BTN, &CManagerDialog::OnBnClickedWebDavTestBtn)
	ON_BN_CLICKED(IDC_WEBDAV_UPLOAD_BTN, &CManagerDialog::OnBnClickedWebDavUploadBtn)
	ON_BN_CLICKED(IDC_WEBDAV_DOWNLOAD_BTN, &CManagerDialog::OnBnClickedWebDavDownloadBtn)
	ON_BN_CLICKED(IDC_WEBDAV_AUTO_SYNC_CHECK, &CManagerDialog::OnBnClickedWebDavAutoSyncCheck)
	ON_BN_CLICKED(IDC_WEBDAV_AUTO_BACKUP_CHECK, &CManagerDialog::OnBnClickedWebDavAutoBackupCheck)
	ON_MESSAGE(WM_APP_WEBDAV_RESULT, &CManagerDialog::OnWebDavResult)

	// 列表行自绘（交替行底色/选中高亮）
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_MGR_LIST, &CManagerDialog::OnListCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_POS_LIST, &CManagerDialog::OnListCustomDraw)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_CUSTOM_LIST, &CManagerDialog::OnListCustomDraw)

	// 输入框焦点变化时重绘自绘边框（聚焦高亮蓝）
	ON_EN_SETFOCUS(IDC_STOCK_SEARCH_EDIT, &CManagerDialog::OnEditFocusChanged)
	ON_EN_KILLFOCUS(IDC_STOCK_SEARCH_EDIT, &CManagerDialog::OnEditFocusChanged)
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

	// 深色标题栏（Win10 1809+ / Win11），与插件暗色主题保持一致
	BOOL darkCaption = TRUE;
	if (FAILED(DwmSetWindowAttribute(GetSafeHwnd(), DWMWA_USE_IMMERSIVE_DARK_MODE, &darkCaption, sizeof(darkCaption))))
	{
		DwmSetWindowAttribute(GetSafeHwnd(), 19, &darkCaption, sizeof(darkCaption));
	}

	// 设置窗口默认大小和最小尺寸
	// 高度需容纳均线日配置页 4 张卡片（116+118+104+132 + 3*14 = 512 DPI + 头部/按钮区）
	int initWidth = g_data.DPI(800);
	int initHeight = g_data.DPI(680);
	m_min_size.cx = g_data.DPI(720);
	m_min_size.cy = g_data.DPI(640);

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
		1198, 1199
	};
	for (int id : ownerDrawBtnIds)
	{
		CWnd* pBtn = GetDlgItem(id);
		if (pBtn && pBtn->GetSafeHwnd())
			pBtn->ModifyStyle(0, BS_OWNERDRAW);
	}

	// 初始化搜索输入框与下拉结果弹窗
	m_search_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, CRect(0, 0, 0, 0), this, IDC_STOCK_SEARCH_EDIT);
	m_search_edit.SetFont(&m_font);
	m_search_edit.ModifyStyle(WS_BORDER, 0);
	m_search_edit.ModifyStyleEx(WS_EX_CLIENTEDGE, 0);
	m_search_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"🔍 搜索股票/代码/拼音...");

	m_mgr_del_group_btn.Create(_T("删除分组"), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, 1199);
	m_mgr_del_group_btn.SetFont(&m_font);

	// 分组管理页右上角「分组排序」入口（自选股/持仓顺序固定，仅自定义分组可调）
	m_group_sort_btn.Create(_T("分组排序"), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW, CRect(0, 0, 0, 0), this, 1198);
	m_group_sort_btn.SetFont(&m_font);

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
	m_display_area_combo.SetItemHeight(0, g_data.DPI(24));
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
}

void CManagerDialog::SwitchPage(PageIndex page)
{
	if (m_search_dropdown.GetSafeHwnd())
		m_search_dropdown.HidePopup();
	m_current_page = page;
	m_index_scroll_y = 0;
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

		if (pKWLbl && pKWLbl->GetSafeHwnd()) pKWLbl->MoveWindow(rightLeft + g_data.DPI(18), card2Top + g_data.DPI(42), g_data.DPI(65), g_data.DPI(20));
		PlaceEditInField(IDC_KLINE_WIDTH_EDIT, CRect(rightLeft + g_data.DPI(85), card2Top + g_data.DPI(38), rightLeft + g_data.DPI(145), card2Top + g_data.DPI(64)));
		if (pKHLbl && pKHLbl->GetSafeHwnd()) pKHLbl->MoveWindow(rightLeft + g_data.DPI(160), card2Top + g_data.DPI(42), g_data.DPI(65), g_data.DPI(20));
		PlaceEditInField(IDC_KLINE_HEIGHT_EDIT, CRect(rightLeft + g_data.DPI(227), card2Top + g_data.DPI(38), rightLeft + g_data.DPI(287), card2Top + g_data.DPI(64)));
		if (pPosLbl && pPosLbl->GetSafeHwnd()) pPosLbl->MoveWindow(rightLeft + g_data.DPI(302), card2Top + g_data.DPI(42), g_data.DPI(60), g_data.DPI(20));
		if (m_display_area_combo.GetSafeHwnd()) m_display_area_combo.MoveWindow(rightLeft + g_data.DPI(364), card2Top + g_data.DPI(38), g_data.DPI(85), g_data.DPI(160));

		// 卡片 3: SOCKS5 代理网络
		int card3Top = card1Top + g_data.DPI(192);
		CWnd* pProxyChk = GetDlgItem(IDC_USE_SOCKS5_PROXY_CHECK);
		CWnd* pProxyLbl = GetDlgItem(IDC_SOCKS5_PROXY_STATIC);

		if (pProxyChk && pProxyChk->GetSafeHwnd()) pProxyChk->MoveWindow(rightLeft + g_data.DPI(18), card3Top + g_data.DPI(40), g_data.DPI(150), chkH);
		if (pProxyLbl && pProxyLbl->GetSafeHwnd()) pProxyLbl->MoveWindow(rightLeft + g_data.DPI(180), card3Top + g_data.DPI(42), g_data.DPI(65), g_data.DPI(20));
		PlaceEditInField(IDC_SOCKS5_PROXY_EDIT, CRect(rightLeft + g_data.DPI(250), card3Top + g_data.DPI(38), rightLeft + g_data.DPI(250) + min(g_data.DPI(210), rightWidth - g_data.DPI(268)), card3Top + g_data.DPI(64)));
	}

	// 分组管理控件布局
	bool isGroup = (m_current_page == PAGE_GROUPS);
	int listTop = rightTop + g_data.DPI(42);
	int listHeight = rightBottom - listTop - g_data.DPI(44);

	// 「分组排序」按钮：分组管理页头部右上角（红框位置），其他页面隐藏
	if (m_group_sort_btn.GetSafeHwnd())
	{
		int sortW = g_data.DPI(78);
		m_group_sort_btn.MoveWindow(rightLeft + rightWidth - sortW, g_data.DPI(14), sortW, g_data.DPI(28));
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
	drawCard(contentRect.top + g_data.DPI(110), g_data.DPI(72), L"走势图尺寸与显示位置");
	drawCard(contentRect.top + g_data.DPI(192), g_data.DPI(72), L"SOCKS5 代理网络");

	// 绘制「当天持仓收益」说明文案
	Gdiplus::Font tipFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush tipBrush(Gdiplus::Color(255, 148, 163, 184)); // #94A3B8
	g.DrawString(L"（填写持仓后显示当天收益，未填写仍显示涨跌幅）", -1, &tipFont,
		Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(135)), static_cast<Gdiplus::REAL>(card1Top + g_data.DPI(72))), &tipBrush);
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

	CFont chipFont;   chipFont.CreateFont(-g_data.DPI(15), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont delFont;    delFont.CreateFont(-g_data.DPI(12), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont plusFont;   plusFont.CreateFont(-g_data.DPI(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont badgeFont;  badgeFont.CreateFont(-g_data.DPI(10), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("微软雅黑"));
	CFont preFont;    preFont.CreateFont(-g_data.DPI(12), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));
	CFont lblFont;    lblFont.CreateFont(-g_data.DPI(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
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
		int badgeW = g_data.DPI(76);
		int badgeH = g_data.DPI(24);
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
	int tagTop = card1Top + g_data.DPI(54);
	int tagH = g_data.DPI(42);
	int tagGap = g_data.DPI(12);

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
		int delR = g_data.DPI(8);
		CRect delRect(delCx - delR - g_data.DPI(2), delCy - delR - g_data.DPI(2),
			delCx + delR + g_data.DPI(2), delCy + delR + g_data.DPI(2));
		m_ma_tag_del_rects[i] = delRect;

		if (static_cast<int>(i) == m_hover_ma_tag_del)
		{
			Gdiplus::SolidBrush delHoverBrush(Gdiplus::Color(255, 140, 20, 35));
			g.FillRectangle(&delHoverBrush, delRect.left, delRect.top, delRect.Width(), delRect.Height());
		}
		drawGdiText(CRect(delCx - delR, delCy - delR, delCx + delR, delCy + delR), L"×", delFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER);

		drawGdiText(CRect(tagLeft + g_data.DPI(12), tagTop, delRect.left - g_data.DPI(2), tagTop + tagH),
			tagText, chipFont, RGB(255, 255, 255), DT_LEFT | DT_VCENTER);

		tagLeft += tagW + tagGap;
	}

	// 空槽位：虚线直角框 + “+”，提示剩余容量，点击聚焦输入框
	if (static_cast<int>(m_data.m_ma_days.size()) < MA_PRESET_MAX)
	{
		int slotW = g_data.DPI(64);
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

			drawGdiText(slotRect, L"+", plusFont, slotHover ? RGB(96, 165, 250) : RGB(75, 85, 99), DT_CENTER | DT_VCENTER);

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

	Gdiplus::Font hintFont(L"微软雅黑", static_cast<Gdiplus::REAL>(g_data.DPI(10)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
	Gdiplus::SolidBrush hintBrush(Gdiplus::Color(255, 110, 124, 147));
	g.DrawString(L"输入后点击「添加周期」或直接按回车；最多 5 条，重复周期会自动提示。", -1, &hintFont,
		Gdiplus::PointF(static_cast<Gdiplus::REAL>(rightLeft + g_data.DPI(18)),
			static_cast<Gdiplus::REAL>(card2Top + g_data.DPI(94))), &hintBrush);

	// ===== 卡片 3: 快捷添加常用周期 =====
	int card3Top = card1Top + g_data.DPI(MA_CARD1_H + MA_CARD_GAP + MA_CARD2_H + MA_CARD_GAP);
	Gdiplus::RectF card3Rf(static_cast<Gdiplus::REAL>(rightLeft), static_cast<Gdiplus::REAL>(card3Top),
		static_cast<Gdiplus::REAL>(rightWidth), static_cast<Gdiplus::REAL>(g_data.DPI(MA_CARD3_H)));
	g.FillRectangle(&cardBg, card3Rf);
	g.DrawRectangle(&cardBorder, card3Rf);
	DrawSectionTitle(g, rightLeft + g_data.DPI(14), card3Top + g_data.DPI(14), L"快捷添加常用周期");

	int preTop = card3Top + g_data.DPI(50);
	int preH = g_data.DPI(34);
	int preGap = g_data.DPI(8);
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

	CFont chkFont;    chkFont.CreateFont(-g_data.DPI(13), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI"));

	// 直角复选框：选中蓝底白勾，未选中深底描边
	auto drawVisCheckBox = [&](int cx, int cy, bool checked) {
		int half = g_data.DPI(8);
		CRect rc(cx - half, cy - half, cx + half, cy + half);
		Gdiplus::RectF rf(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
			static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
		if (checked)
		{
			Gdiplus::SolidBrush bg(Gdiplus::Color(255, 37, 99, 235));
			g.FillRectangle(&bg, rf);
			drawGdiText(rc, L"✓", chkFont, RGB(255, 255, 255), DT_CENTER | DT_VCENTER);
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

	int rowH = g_data.DPI(30);
	int cy = card4Top + g_data.DPI(66);
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
	int card2H = g_data.DPI(172);
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
	int oldHoverMaSlot = m_hover_ma_slot;
	int oldHoverMaPreset = m_hover_ma_preset;
	int oldHoverTab = m_hover_group_tab;
	int oldHoverMode = m_hover_index_mode;

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

	m_hover_ma_tag_del = -1;
	m_hover_ma_slot = -1;
	m_hover_ma_preset = -1;
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
		oldHoverMaPreset != m_hover_ma_preset || oldHoverTab != m_hover_group_tab ||
		oldHoverMode != m_hover_index_mode)
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
	m_hover_ma_slot = -1;
	m_hover_ma_preset = -1;
	m_hover_group_tab = -1;
	m_hover_index_mode = -1;
	Invalidate();
	CDialog::OnMouseLeave();
}

BOOL CManagerDialog::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	CPoint pt;
	GetCursorPos(&pt);
	ScreenToClient(&pt);

	if (m_hover_menu >= 0 || m_hover_index_card >= 0 || m_hover_ma_tag_del >= 0 ||
		m_hover_ma_slot >= 0 || m_hover_ma_preset >= 0 ||
		m_hover_group_tab >= 0 || m_hover_index_mode >= 0 || (m_current_page == PAGE_ABOUT && m_about_link_rect.PtInRect(pt)))
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
				Invalidate();
				return;
			}
		}
	}

	if (m_current_page == PAGE_GROUPS)
	{
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
					Invalidate();
				return;
			}
		}

		// 分时图布林带显隐：[0]=上轨、[1]=中轨、[2]=下轨；仅修改编辑副本 m_data，保存由「确定」承担
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
				Invalidate();
				return;
			}
		}
	}

	if (m_current_page == PAGE_ABOUT && m_about_link_rect.PtInRect(point))
	{
		ShellExecute(nullptr, L"open", L"https://github.com/zhongyang219/TrafficMonitorPlugins", nullptr, nullptr, SW_SHOWNORMAL);
		return;
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
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.HidePopup();
		return;
	}

	std::vector<StockSearchResult> results = CCommon::SearchStock(query.GetString());
	if (results.empty())
	{
		if (m_search_dropdown.GetSafeHwnd())
			m_search_dropdown.HidePopup();
		return;
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
	m_search_dropdown.ShowResults(results, editRc, groupItems);
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

void CManagerDialog::OnClickedFullDayCheck()
{
	SetCheck(IDC_FULL_DAY_CHECK, !IsChecked(IDC_FULL_DAY_CHECK));
	m_data.m_full_day = IsChecked(IDC_FULL_DAY_CHECK);
}

void CManagerDialog::OnBnClickedShowTodayProfitCheck()
{
	SetCheck(IDC_SHOW_TODAY_PROFIT_CHECK, !IsChecked(IDC_SHOW_TODAY_PROFIT_CHECK));
	m_data.m_show_today_profit = IsChecked(IDC_SHOW_TODAY_PROFIT_CHECK);
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

	CStockFetchThread::Instance().PostBackgroundTask([hWnd, result, data]() {
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
		});

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
				g.DrawString(L"✓", -1, &checkFont, checkRf, &checkSf, &checkBrush);
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
	sfNear.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

	Gdiplus::StringFormat sfCenter;
	sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
	sfCenter.SetLineAlignment(Gdiplus::StringAlignmentCenter);

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
		g.DrawString(isSelected ? L"▶" : L"+", -1, &plusFont, btnRf, &sfCenter, &plusTxt);

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
END_MESSAGE_MAP()

void CDarkComboBox::PreSubclassWindow()
{
	CComboBox::PreSubclassWindow();
	::SetWindowTheme(GetSafeHwnd(), L"", L"");
}

void CDarkComboBox::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);

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
	lp->itemHeight = g_data.DPI(24);
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
	return nID == IDOK || nID == IDC_MA_ADD_BTN;
}

bool CManagerDialog::IsDestructiveBtn(UINT nID) const
{
	return nID == IDC_MGR_DEL_BTN || nID == 1199;
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
	}

	rc.InflateRect(1, 1);
	Gdiplus::Pen pen(focused ? Gdiplus::Color(255, 37, 99, 235) : Gdiplus::Color(255, 52, 58, 72), 1.0f);
	g.DrawRectangle(&pen, static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
		static_cast<Gdiplus::REAL>(rc.Width()), static_cast<Gdiplus::REAL>(rc.Height()));
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
