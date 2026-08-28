#pragma once


// CInputDlg 对话框

class CInputDlg : public CDialog
{
	DECLARE_DYNAMIC(CInputDlg)

public:
	CInputDlg(CWnd* pParent = NULL);   // 标准构造函数
	virtual ~CInputDlg();

	void SetTitle(const CString& title);
	CString GetInputText();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_TEXT_INPUT_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

	DECLARE_MESSAGE_MAP()

private:
	CString m_title;
	CString m_input_text;
	CSize m_min_size;

public:
	virtual BOOL OnInitDialog();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnDestroy();
};
