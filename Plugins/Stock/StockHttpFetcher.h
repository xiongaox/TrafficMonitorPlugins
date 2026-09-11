#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <functional>
#include "StockDef.h"

// 股票数据 HTTP 获取器（统一收拢至腾讯主数据源 + 多级保底容灾体系）
// 职责：负责网络数据获取（主数据源优先请求，异常时自动切换备用数据源），不写入任何 DataManager/stockMarket 状态
// 调用方（CStockFetchThread）获取到数据后，调用 CDataManager 的 Apply*/Update 方法完成存储
//
// 多级保底设计说明：
//  - 实时行情：腾讯 (qt.gtimg.cn) -> 新浪 (hq.sinajs.cn)
//  - 分时图：腾讯分时 (minute/query) -> 新浪分时 (getMinlineData) -> 东方财富分时 (trends2)
//  - 日/周/月K线：腾讯前复权 (fqkline) -> 东方财富前复权 (kline/get)
//  - 5/30分钟K线：腾讯分钟K (kline/kline) -> 新浪分钟K (CN_MarketData) -> 东方财富分钟K
//  - 流通股本：东方财富 (f85) -> 腾讯行情换算 -> 数据库缓存
//  - 筹码K线：东方财富官方换手率 -> 腾讯/新浪K线结合流通股本自算
//  - ETF IOPV：上交所/天天基金 -> 腾讯ETF
class CStockHttpFetcher
{
public:
	// 拉取进度回调：在取数线程同步调用（uiCode 为目标代码，如 sh515880）
	// stage: 正在拉取的数据类型（如 "日K线"）；source: 当前尝试的数据源名称；note: 附加说明（如"失败,切换下一源"）
	using FetchStatusCallback = std::function<void(const std::wstring& uiCode, const std::wstring& stage,
		const std::wstring& source, const std::wstring& note)>;
	void SetFetchStatusCallback(FetchStatusCallback cb) { m_status_cb = std::move(cb); }

	// 实时行情（多级保底：腾讯 -> 新浪）：onlyNonAG=true 时仅获取非A股代码
	// outCodes 返回实际请求的代码列表，outResp 返回响应体；无代码或请求失败返回 false
	bool FetchRealtimeHtml(const std::vector<std::wstring>& allCodes, bool onlyNonAG,
		std::vector<std::wstring>& outCodes, std::string& outResp);
	// 上金所品种（118.AUTD 等）实时快照（东财 stock/get，独占通道：腾讯/新浪无金交所行情）
	bool FetchSgeSnapshot(const std::wstring& code, std::string& outResp);
	// 内外盘（腾讯）：includeAG=true 含A股；港股代码需转 r_ 前缀
	bool FetchInnerOuterHtml(const std::vector<std::wstring>& allCodes, bool includeAG, std::string& outResp);
	// 集合竞价（腾讯，仅A股）
	bool FetchCallAuctionHtml(const std::vector<std::wstring>& allCodes,
		std::vector<std::wstring>& outCodes, std::string& outResp);

	// 分时图（多级保底：腾讯分时 -> 新浪分时 -> 东财分时）
	bool FetchTimeline(const std::wstring& code, std::string& outResp);
	// 日K线（多级保底：腾讯前复权 -> 东方财富前复权，美股或强制时优先东财）
	bool FetchDayKLine(const std::wstring& code, int days, std::string& outResp, bool forceEastMoney = false);
	// 周K线（多级保底：腾讯前复权 -> 东方财富前复权，美股或强制时优先东财）
	bool FetchWeekKLine(const std::wstring& code, int weeks, std::string& outResp, bool forceEastMoney = false);
	// 月K线（多级保底：腾讯前复权 -> 东方财富前复权，美股或强制时优先东财）
	bool FetchMonthKLine(const std::wstring& code, int months, std::string& outResp, bool forceEastMoney = false);
	// 重置东财失败冷却期，允许立即重新尝试东财
	void ResetEastMoneyCooldown() { m_eastmoney_fail_until = 0; }
	// 5分钟K线（多级保底：腾讯5分K -> 新浪5分K -> 东财5分K）
	bool FetchMin5KLine(const std::wstring& code, int datalen, std::string& outResp);
	// 30分钟K线（多级保底：腾讯30分K -> 新浪30分K -> 东财30分K）
	bool FetchMin30KLine(const std::wstring& code, int datalen, std::string& outResp);
	// ETF基金IOPV（多级保底：上交所/天天基金 -> 腾讯ETF）
	bool FetchFundIOPV(const std::wstring& code, std::string& outResp);
	// ETF持仓/成分股（东财ETF指数成分+实时行情，或天天基金持仓）
	bool FetchEtfHoldings(const std::wstring& code, STOCK::EtfHoldingsData& outData);

	// 流通股本（多级保底：东方财富 f85 -> 腾讯流通市值换算）
	bool FetchStockBasicCirculating(const std::wstring& code, STOCK::Volume& outShares);
	// 筹码分布K线（多级保底：东方财富含换手率 -> 腾讯/新浪K线按流通股本自算）
	bool FetchChipKLines(const std::wstring& code, STOCK::Volume circulatingAShares,
		std::vector<STOCK::ChipKLinePoint>& outKlines);

private:
	// 状态回调触发（code 为 wide string 代码；回调为空时无操作）
	void NotifyStatus(const std::wstring& code, const wchar_t* stage, const wchar_t* source, const wchar_t* note);

	// 上金所品种（118.* 前缀）专用：跳过腾讯/新浪，直接走东财 K 线接口（klt=101/102/103）
	bool FetchSgeKLine(const std::wstring& code, int klt, int lmt, const wchar_t* stage, std::string& outResp);

	// 东方财富接口失败缓存：WAF 拦截 WinINet 后，一段时间内不再尝试
	// 值为失败截止时间戳（秒），0 表示未缓存
	time_t m_eastmoney_fail_until{ 0 };
	FetchStatusCallback m_status_cb;
};

// 全局实例（与 g_data 同模式，供工作线程直接访问）
extern CStockHttpFetcher g_http_fetcher;
