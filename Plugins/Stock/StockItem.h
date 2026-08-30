#pragma once
#include "PluginInterface.h"
#include "FloatingWnd.h"

// 单一聚合显示项：主程序（TrafficMonitor）只在启动时枚举一次显示项，
// 显示设置里只会出现这一个"股票"条目；勾选"状态栏显示"的股票全部绘制在这一个条目内，
// 表格里的√直接控制条目内容与宽度（主程序每秒重算宽度并重绘，实时生效）
class StockItem : public IPluginItem
{
public:
	virtual const wchar_t* GetItemName() const override;
	virtual const wchar_t* GetItemId() const override;
	virtual const wchar_t* GetItemLableText() const override;
	virtual const wchar_t* GetItemValueText() const override;
	virtual const wchar_t* GetItemValueSampleText() const override;
	virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;
	virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
	virtual bool IsCustomDraw() const override;
	virtual int GetItemWidthEx(void* hDC) const override;
	virtual int IsDoubleLineExclusive() const override;

private:
	// 单只股票的显示宽度（与 DrawSingleStock 的绘制宽度保持一致）
	int GetSingleStockWidth(CDC* pDC, const std::wstring& code) const;
	// 在指定x处绘制单只股票，返回实际占用的宽度
	int DrawSingleStock(CDC* pDC, const std::wstring& code, int x, int y, int h, bool dark_mode) const;
	// 计算布局：rows=1 单行排列；rows=2 每列最多2行、列优先填充（列宽取列内股票宽度最大值）
	// 返回总宽度，布局结果写入 m_layout_*
	int BuildLayout(CDC* pDC, int rows) const;

	mutable std::wstring m_item_name;
	mutable std::wstring m_item_id;

	// 最近一次布局缓存（主程序UI线程内调用，用于点击命中换算）
	mutable std::vector<std::wstring> m_layout_codes; // 参与绘制的股票代码
	mutable std::vector<int> m_layout_offsets;        // 每只股票在条目内的相对x（同列股票相同）
	mutable std::vector<int> m_layout_widths;         // 每只股票占用的宽度（2行模式下为所在列宽）
	mutable int m_last_rows{ 2 };                     // 最近一次绘制使用的行数（1或2，默认按双行独占预估）
	mutable bool m_rows_detected{ false };            // 是否已完成过一次绘制行数检测（此前测宽用任务栏窗口高度推断）
	mutable int m_last_row_h{ 0 };                    // 最近一次绘制时每行的高度
	mutable int m_last_draw_x{ 0 };                   // 最近一次DrawItem的窗口客户区绝对x
	mutable int m_last_draw_y{ 0 };                   // 最近一次DrawItem的窗口客户区绝对y
};
