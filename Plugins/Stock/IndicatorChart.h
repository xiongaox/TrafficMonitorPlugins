#pragma once

#include "ChartContext.h"
#include "Common.h"
#include "StockIndicator.h"
#include <StockDef.h>

// 技术指标图表绘制（MACD/KDJ/WR/RSI）
// 职责：绘制分时/K线模式下的各类技术指标图表，包括标题栏、指标曲线、网格线、时间标签
// 数据来源：CStockIndicator 计算的指标数据，由调用方传入
class CIndicatorChart
{
public:
    // 指标数据类型别名（来自CStockIndicator，便于代码简洁）
    using MACDData = CStockIndicator::MACDData;
    using MACDCrossSignal = CStockIndicator::MACDCrossSignal;
    using KDJData = CStockIndicator::KDJData;
    using WRData = CStockIndicator::WRData;
    using RSIData = CStockIndicator::RSIData;

    // 分时图指标类型（与CFloatingWnd::TimelineIndicator对应）
    enum class TimelineIndicator { CJL, MACD, KDJ, WR, RSI };

    // 分时图指标绘制所需的悬停状态
    struct HoverState {
        bool isHoveringVolume{ false };
        int hoveredBarIndex{ -1 };
        UIViewMode viewMode{ UI_VIEW_TIMELINE };
        CString timelineMacdTitleTip;
        CString timelineKdjTitleTip;
        CString timelineWrTitleTip;
        CString timelineRsiTitleTip;
        CString timelineVolumeTitleTip;
    };

    // ========== 分时图指标绘制 ==========

    // 绘制MACD图表（分时数据版本）
    void DrawMACDChart(CDC& memDC, int x, int y, int width, int height,
                       const std::vector<STOCK::TimelinePoint>& timelinePoint,
                       const std::vector<MACDData>& macdData,
                       int startIndex = 0, int visibleCount = -1, int xAxisPoints = 0);

    // 绘制MACD图表（K线数据版本）
    void DrawMACDChart(CDC& memDC, int x, int y, int width, int height,
                       const std::vector<STOCK::KLinePoint>& klineData,
                       const std::vector<MACDData>& macdData,
                       int klinePeriodDays, int scrollOffset,
                       int startIndex = 0, int visibleCount = -1);

    // 绘制分时KDJ区域
    void DrawTimelineKDJSection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);

    // 绘制分时KDJ图表（纯曲线）
    void DrawTimelineKDJChart(CDC& memDC, int x, int y, int width, int height,
                              const std::vector<STOCK::TimelinePoint>& timelinePoint,
                              const std::vector<KDJData>& kdjData,
                              int startIndex = 0, int xAxisPoints = 0);

    // 绘制分时WR图表（纯曲线）
    void DrawTimelineWRChart(CDC& memDC, int x, int y, int width, int height,
                             const std::vector<STOCK::TimelinePoint>& timelinePoint,
                             const std::vector<WRData>& wrData,
                             int startIndex = 0, int xAxisPoints = 0);

    // 绘制分时WR区域
    void DrawTimelineWRSection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);

    // 绘制分时RSI图表（纯曲线）
    void DrawTimelineRSIChart(CDC& memDC, int x, int y, int width, int height,
                              const std::vector<STOCK::TimelinePoint>& timelinePoint,
                              const std::vector<RSIData>& rsiData,
                              int startIndex = 0, int xAxisPoints = 0);

    // 绘制分时RSI区域
    void DrawTimelineRSISection(CDC& memDC, const TimelineDrawContext& ctx, const HoverState& hover);

    // ========== 区域绘制（标题栏+图表一体化） ==========

    // 绘制MACD区域（标题栏+图表+网格，不含时间标签）
    void DrawMacdChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight,
                           const CString& macdTitleTip, const HoverState& hover);

    // 绘制指标区域（根据指标类型选择KDJ/WR/RSI/成交量，含标题栏+图表+网格+时间标签）
    void DrawIndicatorChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight,
                                bool drawTimeLabels, TimelineIndicator indicator, const HoverState& hover);

    // 绘制成交量区域（标题栏+量柱图+网格+时间标签）
    void DrawVolumeChartArea(CDC& memDC, const TimelineDrawContext& ctx, int areaTop, int areaHeight,
                             bool drawTimeLabels, const HoverState& hover);

    // 绘制成交量柱状图（纯量柱，不含标题栏和网格）
    void DrawVolumeChart(CDC& memDC, int x, int y, int width, int height,
                         const std::vector<STOCK::TimelinePoint>& timelinePoint,
                         const STOCK::StockInfo* stockInfo = nullptr,
                         int startIndex = 0, int visibleCount = -1, int xAxisPoints = 0,
                         bool isHoveringVolume = false, int hoveredBarIndex = -1);

private:
    // 绘制网格线和时间标签（分时指标区域共用）
    void DrawSectionGridAndTimeLabels(CDC& memDC, const TimelineDrawContext& ctx, int chartTop, int chartHeight, bool drawTimeLabels);

    // 绘制金叉死叉箭头标记（从圆点边缘开始，箭头头+竖线杆）
    // markX/markY: 标记中心坐标  dotRadius: 圆点半径  isGolden: 金叉true/死叉false
    void DrawCrossArrow(CDC& memDC, int markX, int markY, int dotRadius, bool isGolden);
};