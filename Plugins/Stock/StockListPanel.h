#pragma once

#include <afxwin.h>
#include <string>
#include <vector>

// 浮动窗口顶部分组标签（与配置对话框分组页的标签样式一致）
// tabIndex: 0=自选股, 1=持仓, >=2 为自定义分组（m_custom_groups[tabIndex-2]）
struct FloatingGroupTab
{
	std::wstring name;   // 显示文本（下拉按钮为“更多分组 ▾”或激活的隐藏组名）
	int tabIndex{ 0 };   // 对应分组索引
	bool isActive{ false };
	bool isDropdown{ false };  // “更多分组”下拉按钮
	CRect rect;          // 客户区矩形（布局时计算，供点击命中）
};

// 左侧股票列表面板绘制
// 职责：在指定矩形区域内绘制股票列表（名称+代码），高亮当前选中股票，支持垂直滚动
// 数据来源：g_data 共享数据（线程安全加锁访问）
class CStockListPanel
{
public:
	// 分组总数：自选股 + 持仓 + 自定义分组数
	static int GetGroupTabCount();
	// 将分组索引收敛到有效范围（自定义分组被删除后索引可能越界）
	static int ClampGroupTab(int groupTab);
	// 分组显示名
	static std::wstring GetGroupTabName(int groupTab);

	// 获取用于自选列表显示的股票代码列表（过滤指数和港股）
	static std::vector<std::wstring> GetStockListCodes();
	// 获取指定分组的股票代码列表（同样的过滤规则）
	static std::vector<std::wstring> GetStockListCodes(int groupTab);

	// 布局顶部分组标签条：自选股/持仓/自定义分组，超出三个折叠进“更多分组”下拉
	static std::vector<FloatingGroupTab> LayoutGroupTabs(CDC& memDC, int windowWidth, int headerHeight, int activeTab);
	// 绘制顶部分组标签条（hoverIdx 为悬停标签下标，-1 表示无）
	static void DrawGroupTabs(CDC& memDC, const std::vector<FloatingGroupTab>& tabs, int hoverIdx = -1);

	// 绘制股票列表面板
	// x, y, w, h: 面板位置和尺寸
	// currentStockId: 当前选中的股票代码（用于高亮）
	// scrollOffset: 列表垂直滚动像素偏移
	// groupTab: 当前分组（决定列表数据与标题文字）
	void Draw(CDC& memDC, int x, int y, int w, int h, const std::wstring& currentStockId, int scrollOffset = 0, int groupTab = 0);
};
