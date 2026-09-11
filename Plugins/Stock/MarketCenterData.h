#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <ctime>

// ===== 行情中心（CMarketCenterWnd）共享数据仓库 =====
// 取数线程写入（FetchAll... 系列在后台线程调用）、UI 线程读取；
// 数据在窗口关闭后保留，重开窗口立即可显示上次内容，再由刷新任务更新。
// 所有字段由 m_mutex 保护。

namespace MC
{
	// 行业板块主力资金（气泡图页）
	struct SectorFlow
	{
		std::wstring code;        // 板块代码 BK1201
		std::wstring name;        // 板块名称
		double flow{ 0.0 };       // 主力净流入(元)
		double pct{ 0.0 };        // 涨跌幅(%)
		double superBig{ 0.0 };   // 超大单净流入(元)
		double big{ 0.0 };        // 大单净流入(元)
		double mid{ 0.0 };        // 中单净流入(元)
		double smallOrder{ 0.0 }; // 小单净流入(元)（勿用 small：windows.h 宏）
	};

	// ETF 场内行情（净流入页 / 涨跌榜页共用）
	struct EtfQuote
	{
		std::wstring code;
		std::wstring name;
		std::wstring theme;       // 由名称关键词归类的主题
		double price{ 0.0 };
		double pct{ 0.0 };
		double amount{ 0.0 };     // 成交额(元)
		double inflow{ 0.0 };     // 主力净流入(元)
	};

	// 1分钟累计资金流点（主力资金页）
	struct FflowMinute
	{
		std::wstring time;        // "09:31"
		double main{ 0.0 };       // 主力净流入(元)
		double superBig{ 0.0 };
		double big{ 0.0 };
		double mid{ 0.0 };
		double smallOrder{ 0.0 }; // （勿用 small：windows.h 宏）
	};

	// 指数分时点（主力资金页左轴曲线）
	struct IndexTrendPoint
	{
		std::wstring time;        // "09:31"
		double price{ 0.0 };
	};

	// 涨跌分布快照（涨跌趋势页）
	struct UpDownDist
	{
		time_t time{ 0 };
		long long zt{ 0 };        // 涨停家数（涨停池）
		long long dt{ 0 };        // 跌停家数（跌停池）
		std::map<int, long long> buckets;   // floor(涨跌幅) -> 家数，正涨负跌
		long long UpCount() const;          // Σ buckets>=1
		long long DownCount() const;        // Σ buckets<=-1
		long long FlatCount() const;        // bucket 0
	};

	// 涨跌家数分时采样点（开窗后按分钟自积累）
	struct TrendSample
	{
		std::wstring time;        // "09:31"
		long long up{ 0 };
		long long down{ 0 };
	};

	// ETF 累计净流入采样点（主力资金页 ETF 曲线）
	struct EtfFlowSample
	{
		std::wstring time;
		double inflow{ 0.0 };     // 全市场ETF主力净流入合计(元)
	};
}

class CMarketCenterData
{
public:
	static CMarketCenterData& Instance();

	std::mutex m_mutex;

	// 调度状态互斥（m_inflight/退避判断；与数据内容锁分离避免长持）
	std::mutex m_sched_mutex;

	// ===== 各数据集内容 =====
	std::vector<MC::SectorFlow> m_sectors;              // 行业板块资金流
	std::vector<MC::EtfQuote> m_etfs;                   // ETF 全量（分页抓取合并）
	long long m_etf_total{ 0 };                          // 服务端报告的 ETF 总数
	std::vector<MC::FflowMinute> m_fflow_sh;            // 上证主力资金分时
	std::vector<MC::FflowMinute> m_fflow_sz;            // 深证主力资金分时
	std::vector<MC::IndexTrendPoint> m_index_trend;     // 上证指数分时
	MC::UpDownDist m_dist;                              // 最新涨跌分布
	double m_turnover_today{ 0.0 };                      // 沪深今日合计成交额(元)
	double m_turnover_yesterday{ 0.0 };                  // 沪深昨日合计成交额(元)
	std::vector<MC::TrendSample> m_trend_curve;         // 涨跌家数分时（自积累）
	std::vector<MC::EtfFlowSample> m_etf_flow_curve;    // ETF累计净流入分时（自积累）

	// ===== 各数据集最后成功更新时间（0=从未成功）=====
	time_t m_sectors_time{ 0 };
	time_t m_etfs_time{ 0 };
	time_t m_fflow_time{ 0 };
	time_t m_dist_time{ 0 };
	time_t m_turnover_time{ 0 };

	// ===== 失败退避：某数据集连续失败后的一段时间内不再重试 =====
	time_t m_fail_until[4]{ 0, 0, 0, 0 };               // 对应 DataSet 枚举
	bool m_inflight[4]{ false, false, false, false };    // 后台任务在途标记
	bool m_last_failed[4]{ false, false, false, false }; // 最近一次请求是否失败（供 UI 显示错误态）
	bool m_premarket_no_data[4]{ false, false, false, false }; // 接口可达但资金流字段全为"-"（盘前清库，非网络故障）
	static const int FAIL_BACKOFF_SEC = 15;

	// 数据集枚举（取数任务调度与失败退避共用）
	enum DataSet
	{
		DS_SECTORS = 0,   // 气泡图板块资金流
		DS_ETFS = 1,      // ETF 全量列表
		DS_MAINFLOW = 2,  // 主力资金分时（沪深 fflow + 指数 trends2）
		DS_TREND = 3,     // 涨跌分布 + 涨停/跌停池 + 指数成交额
		DS_COUNT = 4
	};

	bool IsInBackOff(DataSet ds) const;
	void MarkSuccess(DataSet ds);
	void MarkFailure(DataSet ds);
	bool IsStale(DataSet ds, int staleSec) const;
	// 最近一次请求是否失败（且当前无数据）：供 UI 显示"获取失败，点击重试"
	bool HasFailed(DataSet ds) const;
	// 盘前清库态：接口正常但资金流字段全为"-"，开盘后自动恢复（供 UI 显示"盘前暂无数据"）
	bool IsPremarketNoData(DataSet ds) const;
	// 用户主动重试：清除退避并立即重新请求（UI 线程调用）
	void Retry(DataSet ds, HWND notifyWnd);

	// 行情中心独立执行器：与股票实时/K线线程隔离；当前页面任务优先于后台预热。
	void StartExecutor();
	void StopExecutor();
	void WarmupStaleData(HWND notifyWnd);

	struct DataSetState
	{
		bool hasData{ false };
		bool stale{ true };
		bool queued{ false };
		bool inflight{ false };
		bool failed{ false };
		time_t fetchedAt{ 0 };
		time_t backoffUntil{ 0 };
		bool premarketNoData{ false };
	};

	DataSetState GetDataSetState(DataSet ds, int staleSec) const;
	void LoadCachedSnapshots();

	// 当前页面使用 Foreground，后台预热使用 Warmup。
	enum class RequestPriority { Foreground, Warmup };
	bool RequestIfStale(DataSet ds, int staleSec, HWND notifyWnd, RequestPriority priority = RequestPriority::Foreground);

	// 取数入口（在取数线程调用；每项成功后写入仓库并返回 true）
	bool FetchSectors();        // 行业板块主力净流入（双向 Top60）
	bool FetchEtfs();           // ETF 全量分页抓取（pz=100 × N）
	bool FetchMainFlow();       // 沪深 fflow 分时 + 上证指数 trends2 + ETF曲线采样
	bool FetchTrendDist();      // 涨跌分布 + 涨停/跌停池 + 沪深成交额

	// 按开市时间推进自积累曲线（在 FetchTrendDist/FetchEtfs 成功后调用）
	void AppendTrendSample();
	void AppendEtfFlowSample();

	// 主题归类：ETF 名称 → 主题关键词
	static std::wstring DeriveTheme(const std::wstring& name);

	// 分钟时间轴（09:30-11:29 + 13:00-15:00，共 241 点）
	static const std::vector<std::wstring>& TimeAxis();
	static int TimeIndex(const std::wstring& hhmm);   // 不在轴上返回 -1

private:
	struct PendingRequest
	{
		DataSet dataSet;
		HWND notifyWnd{ nullptr };
	};

	void ExecutorLoop();
	bool ExecuteRequest(DataSet ds);
	void EnqueueRequest(DataSet ds, HWND notifyWnd, RequestPriority priority);
	std::string SerializeSnapshot(DataSet ds) const;
	bool ApplySnapshot(DataSet ds, const std::string& payload, time_t fetchedAt, const std::string& tradeDate, int schemaVersion);

	std::mutex m_executor_mutex;
	std::condition_variable m_executor_cv;
	std::thread m_executor_thread;
	std::deque<PendingRequest> m_foreground_requests;
	std::deque<PendingRequest> m_warmup_requests;
	bool m_executor_started{ false };
	bool m_executor_stopping{ false };
	unsigned int m_failure_count[DS_COUNT]{ 0, 0, 0, 0 };
};
