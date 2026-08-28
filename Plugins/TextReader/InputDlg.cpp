// InputDlg.cpp : 实现文件
//

#include "pch.h"
#include "TextReader.h"
#include "InputDlg.h"
#include "afxdialogex.h"
#include "resource.h"
#include "DataManager.h"

// CInputDlg 对话框

IMPLEMENT_DYNAMIC(CInputDlg, CDialog)

CInputDlg::CInputDlg(CWnd* pParent /*=NULL*/)
	: CDialog(IDD_TEXT_INPUT_DIALOG, pParent)
{

}

CInputDlg::~CInputDlg()
{
}

void CInputDlg::SetTitle(const CString& title)
{
	m_title = title;
}

CString CInputDlg::GetInputText()
{
	return m_input_text;
}

void CInputDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CInputDlg, CDialog)
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
END_MESSAGE_MAP()


// CInputDlg 消息处理程序

BOOL CInputDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	//获取初始时的大小
	CRect rect;
	GetWindowRect(rect);
	m_min_size = rect.Size();

	SetWindowText(m_title);
	SetIcon(g_data.GetIcon(IDI_ICON1), FALSE);
	SetDlgItemText(IDOK, g_data.StringRes(IDS_OK));
	SetDlgItemText(IDCANCEL, g_data.StringRes(IDS_CANCEL));

	return TRUE;  // return TRUE unless you set the focus to a control
	// 异常: OCX 属性页应返回 FALSE
}

void CInputDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	//限制窗口最小大小
	lpMMI->ptMinTrackSize.x = m_min_size.cx;
	lpMMI->ptMinTrackSize.y = m_min_size.cy;

	CDialog::OnGetMinMaxInfo(lpMMI);
}

void CInputDlg::OnDestroy()
{
	GetDlgItemText(IDC_EDIT1, m_input_text);
	CDialog::OnDestroy();
}
