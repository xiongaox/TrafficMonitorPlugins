#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>
#include <atomic>
#include <ctime>

// 接口源类型枚举
enum ApiSourceType
{
	API_TDX = 0,        // 通达信直连 (PyTDX 共享内存)
	API_TENCENT = 1,    // 腾讯证券接口 (qt.gtimg.cn / ifzq.gtimg.cn)
	API_SINA = 2,       // 新浪财经接口 (hq.sinajs.cn / CN_MarketData)
	API_EASTMONEY = 3,  // 东方财富接口 (push2.eastmoney.com / push2his)
	API_FUND_IOPV = 4,  // 天天基金 / 上交所估值源 (fundgz / sse snap)
	API_SOURCE_COUNT = 5
};

// 心跳健康度等级
enum HeartbeatLevel
{
	LEVEL_OK = 0,    // 正常 (绿色)
	LEVEL_WARN = 1,  // 耗时较长或触发降级 (黄色)
	LEVEL_FAIL = 2   // 请求失败 / WAF拦截 / 超时 (红色)
};

// 单次心跳记录点
struct ApiHeartbeatPoint
{
	time_t timestamp{ 0 };
	int latencyMs{ 0 };
	int statusCode{ 200 };
	HeartbeatLevel level{ LEVEL_OK };
	std::wstring detail;
};

// 接口源完整状态信息
struct ApiSourceInfo
{
	ApiSourceType type;
	std::wstring name;
	std::wstring shortName;
	std::wstring role;
	std::wstring targetUrl;
	std::deque<ApiHeartbeatPoint> history; // 固定容量队列 (最近 40 个心跳点)
	int totalRequests{ 0 };
	int successRequests{ 0 };
	int lastLatencyMs{ 0 };
	time_t lastActiveTime{ 0 };
	std::wstring lastStatusMsg;
	bool isWarning{ false };
};

// 接口健康检测与心跳监控管理器 (单例)
class CApiHealthManager
{
public:
	static constexpr int MAX_HISTORY_POINTS = 38; // 每个接口保留的心跳点最大数量

	static CApiHealthManager& Instance();

	// 记录一次业务请求或探针的心跳
	void RecordPoint(ApiSourceType type, int latencyMs, int statusCode, HeartbeatLevel level, const std::wstring& detail);

	// 触发异步全量探针检测 (不阻塞调用线程)
	void TriggerActiveProbeAsync(std::function<void()> onFinished = nullptr);

	// 是否正在进行主动探测
	bool IsProbing() const { return m_probing.load(); }

	// 获取所有接口的只读快照 (线程安全拷贝)
	std::vector<ApiSourceInfo> GetSnapshot() const;

	// 获取指定接口的只读信息
	ApiSourceInfo GetSourceInfo(ApiSourceType type) const;

private:
	CApiHealthManager();
	~CApiHealthManager() = default;
	CApiHealthManager(const CApiHealthManager&) = delete;
	CApiHealthManager& operator=(const CApiHealthManager&) = delete;

	void InitSources();
	void DoActiveProbes();

private:
	mutable std::mutex m_mutex;
	std::vector<ApiSourceInfo> m_sources;
	std::atomic<bool> m_probing{ false };
};
