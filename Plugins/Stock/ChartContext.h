#pragma once

#include <vector>
#include <StockDef.h>

// 图表绘制共享上下文结构
// 从 CFloatingWnd 中提取，供各图表模块（CCallAuctionChart/CChartPrice/...）共享

// 分时图绘制上下文（包含几何坐标、数据指针、MA值等）
struct TimelineDrawContext {
	int chartWidth;
	int chartLeft{ 0 };   // 绘图区左侧偏移（用于Y轴标签留白）
	int windowWidth;
	int chartHeight;
	int priceChartTop;
	int priceChartHeight;
	int volumeChartTop;
	int volumeChartHeight;
	int macdChartTop;
	double niceStep{ 0 };  // Y轴刻度步长（OnPaint计算一次，绘制函数直接用）
	int macdChartHeight;
	int positionY;
	bool showTimelinePercentAxis{ false };  // 分时模式右侧涨跌幅刻度
	int timelinePercentAxisWidth{ 0 };      // 分时模式右侧涨跌幅刻度列宽
	CFont* baseFont{ nullptr };             // 盘口报价使用的基础字体（与盘口绘制共享同一对象）
	int visibleCount{ 0 };   // 可见数据点数（≤120）
	int xAxisPoints{ 0 };   // X轴总格数（=m_timelineVisibleCount，数据不足时右侧留白）
	int startIndex{ 0 };     // 可见数据起始索引
	int scrollRange{ 0 };    // 滚动范围
	STOCK::Price maxPrice{ 0 };   // 可见区间最大价（含内边距）
	STOCK::Price minPrice{ 0 };   // 可见区间最小价（含内边距）
	double unitY{ 0 };            // Y轴缩放（像素/价格）
	STOCK::StockInfo realtimeData;
	const std::vector<STOCK::TimelinePoint>* timelinePoint;    // 可见范围子集（subTimeline）
	const std::vector<STOCK::TimelinePoint>* fullTimeline{ nullptr };  // 完整分时数据（用于布林带等需要历史回溯的指标）
	const std::vector<STOCK::KLinePoint>* klineData;
	// 滚动均价（最新数据点的值）
	STOCK::Price ma1{ 0 };    // MA1（1分钟均价 = 当前价格）
	std::vector<STOCK::Price> maValues; // 各周期均线最新值，顺序与 SettingData::m_ma_days 一致
	// 前一数据点的值
	STOCK::Price prevMa1{ 0 };
};

// K线图公共数据结构
struct KLineDrawData {
	int x, y, w, h;
	int startIndex, finalStartIndex;
	int displayCount, maxVisibleKlines, scrollRange, scrollPos;
	int barWidth;
	int gap;
	STOCK::Price maxPrice, minPrice;
	double unitY;
	const std::vector<STOCK::KLinePoint>* klineData;
	const STOCK::StockInfo* stockInfo;
};

// K线图月份标签信息
struct LabelInfo {
	int year;
	int month;
	int barX;
};
