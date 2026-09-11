#include "pch.h"
#include <afxinet.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include "MarketCenterData.h"
#include "DataManager.h"
#include "Common.h"
#include "NetFetch.h"
#include "StockFetchThread.h"
#include "utilities/yyjson/yyjson.h"

#define g_mc CMarketCenterData::Instance()

namespace
{
	constexpr auto MC_USERAGENT = _T("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36 Edg/135.0.0.0");
	constexpr const wchar_t* MC_REFERER_HEADER = L"Referer: https://quote.eastmoney.com";

	std::string JsonQuote(const std::wstring& value)
	{
		std::string utf8 = CCommon::UnicodeToStr(value, true);
		std::string out = "\"";
		for (unsigned char ch : utf8)
		{
			if (ch == '\\' || ch == '\"') out += '\\';
			if (ch == '\n') out += "\\n";
			else if (ch == '\r') out += "\\r";
			else if (ch == '\t') out += "\\t";
			else out += static_cast<char>(ch);
		}
		out += '"';
		return out;
	}

	void JsonNum(std::string& out, double value)
	{
		if (!std::isfinite(value)) value = 0.0;
		char buf[64];
		sprintf_s(buf, "%.12g", value);
		out += buf;
	}

	void JsonKey(std::string& out, const char* key)
	{
		out += JsonQuote(CCommon::StrToUnicode(key, false));
		out += ':';
	}

	void JsonStringField(std::string& out, const char* key, const std::wstring& value)
	{
		JsonKey(out, key); out += JsonQuote(value);
	}

	void JsonDoubleField(std::string& out, const char* key, double value)
	{
		JsonKey(out, key); JsonNum(out, value);
	}

	void JsonBoolField(std::string& out, const char* key, bool value)
	{
		JsonKey(out, key); out += value ? "true" : "false";
	}

	std::string CurrentTradeDate()
	{
		time_t now = time(nullptr);
		tm localTm{};
		localtime_s(&localTm, &now);
		char date[16];
		sprintf_s(date, "%04d-%02d-%02d", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
		return date;
	}

	bool JsonFinite(yyjson_val* val)
	{
		return val && ((yyjson_is_real(val) && std::isfinite(yyjson_get_real(val))) || yyjson_is_sint(val) || yyjson_is_uint(val));
	}

	std::wstring JsonString(yyjson_val* obj, const char* key)
	{
		yyjson_val* val = yyjson_obj_get(obj, key);
		return val && yyjson_is_str(val) ? CCommon::StrToUnicode(yyjson_get_str(val), true) : L"";
	}

	double JsonNumber(yyjson_val* obj, const char* key, double fallback = 0.0)
	{
		yyjson_val* val = yyjson_obj_get(obj, key);
		if (!JsonFinite(val)) return fallback;
		if (yyjson_is_real(val)) return yyjson_get_real(val);
		if (yyjson_is_sint(val)) return static_cast<double>(yyjson_get_sint(val));
		return static_cast<double>(yyjson_get_uint(val));
	}

	bool JsonBool(yyjson_val* obj, const char* key)
	{
		yyjson_val* val = yyjson_obj_get(obj, key);
		return val && yyjson_is_bool(val) && yyjson_get_bool(val);
	}

	bool HttpGet(const std::wstring& url, std::string& resp)
	{
		CString headers(MC_REFERER_HEADER);
		return CNetFetch::GetURL(url, resp, MC_USERAGENT, headers) && !resp.empty();
	}

	double JsonNum(yyjson_val* obj, const char* key, double defVal = 0.0)
	{
		if (obj == nullptr) return defVal;
		yyjson_val* val = yyjson_obj_get(obj, key);
		if (val == nullptr) return defVal;
		if (yyjson_is_real(val)) return yyjson_get_real(val);
		if (yyjson_is_sint(val)) return static_cast<double>(yyjson_get_sint(val));
		if (yyjson_is_uint(val)) return static_cast<double>(yyjson_get_uint(val));
		if (yyjson_is_str(val))
		{
			try { return std::stod(yyjson_get_str(val)); }
			catch (...) {}
		}
		return defVal;
	}

	bool JsonIsDash(yyjson_val* obj, const char* key)
	{
		if (obj == nullptr) return false;
		yyjson_val* val = yyjson_obj_get(obj, key);
		return val != nullptr && yyjson_is_str(val) && std::string(yyjson_get_str(val)) == "-";
	}

	std::wstring JsonWStr(yyjson_val* obj, const char* key)
	{
		if (obj == nullptr) return L"";
		yyjson_val* val = yyjson_obj_get(obj, key);
		if (val == nullptr || !yyjson_is_str(val)) return L"";
		return CCommon::StrToUnicode(yyjson_get_str(val), true);
	}

	// ETF 名称 → 主题：剔除常见后缀词后截取核心词（近似 demo 的主题字段）
	std::vector<std::wstring> ETF_THEME_KEYWORDS = {
		L"科创50", L"科创", L"芯片", L"半导体", L"证券", L"光伏", L"医药", L"医疗", L"军工",
		L"黄金", L"银行", L"机器人", L"人工智能", L"AI", L"白酒", L"稀土", L"煤炭", L"钢铁",
		L"化工", L"地产", L"旅游", L"豆粕", L"5G", L"通信", L"新能源车", L"新能源", L"电力",
		L"游戏", L"传媒", L"计算机", L"软件", L"有色", L"创业板", L"沪深300", L"中证500",
		L"上证50", L"国企", L"红利", L"消费", L"汽车", L"机械", L"基建", L"食品", L"养殖",
	};
}

namespace MC
{
	long long UpDownDist::UpCount() const
	{
		long long n = 0;
		for (auto& p : buckets) if (p.first >= 1) n += p.second;
		return n;
	}
	long long UpDownDist::DownCount() const
	{
		long long n = 0;
		for (auto& p : buckets) if (p.first <= -1) n += p.second;
		return n;
	}
	long long UpDownDist::FlatCount() const
	{
		auto it = buckets.find(0);
		return it == buckets.end() ? 0 : it->second;
	}
}

CMarketCenterData& CMarketCenterData::Instance()
{
	static CMarketCenterData inst;
	return inst;
}

CMarketCenterData::DataSetState CMarketCenterData::GetDataSetState(DataSet ds, int staleSec) const
{
	DataSetState state;
	{
		std::lock_guard<std::mutex> lock(const_cast<CMarketCenterData*>(this)->m_mutex);
		switch (ds)
		{
		case DS_SECTORS: state.hasData = !m_sectors.empty(); state.fetchedAt = m_sectors_time; break;
		case DS_ETFS: state.hasData = !m_etfs.empty(); state.fetchedAt = m_etfs_time; break;
		case DS_MAINFLOW: state.hasData = !m_fflow_sh.empty() || !m_fflow_sz.empty() || !m_index_trend.empty(); state.fetchedAt = m_fflow_time; break;
		case DS_TREND: state.hasData = !m_dist.buckets.empty(); state.fetchedAt = m_dist_time; break;
		default: break;
		}
		state.stale = state.fetchedAt == 0 || time(nullptr) - state.fetchedAt > staleSec;
		state.premarketNoData = m_premarket_no_data[ds];
	}
	{
		std::lock_guard<std::mutex> lock(const_cast<CMarketCenterData*>(this)->m_sched_mutex);
		state.inflight = m_inflight[ds];
		state.failed = m_last_failed[ds];
		state.backoffUntil = m_fail_until[ds];
	}
	{
		std::lock_guard<std::mutex> lock(const_cast<CMarketCenterData*>(this)->m_executor_mutex);
		for (const auto& request : m_foreground_requests) if (request.dataSet == ds) state.queued = true;
		for (const auto& request : m_warmup_requests) if (request.dataSet == ds) state.queued = true;
	}
	return state;
}

bool CMarketCenterData::IsInBackOff(DataSet ds) const
{
	// 仅由已持有 m_sched_mutex 的调度路径调用，不能在此重复加锁。
	return m_fail_until[ds] > 0 && time(nullptr) < m_fail_until[ds];
}

void CMarketCenterData::MarkSuccess(DataSet ds)
{
	std::lock_guard<std::mutex> lock(m_sched_mutex);
	m_fail_until[ds] = 0;
	m_last_failed[ds] = false;
}

void CMarketCenterData::MarkFailure(DataSet ds)
{
	std::lock_guard<std::mutex> lock(m_sched_mutex);
	m_fail_until[ds] = time(nullptr) + FAIL_BACKOFF_SEC;
	m_last_failed[ds] = true;
}

bool CMarketCenterData::HasFailed(DataSet ds) const
{
	std::lock_guard<std::mutex> lock(const_cast<CMarketCenterData*>(this)->m_sched_mutex);
	return m_last_failed[ds];
}

bool CMarketCenterData::IsPremarketNoData(DataSet ds) const
{
	std::lock_guard<std::mutex> lock(const_cast<CMarketCenterData*>(this)->m_mutex);
	return m_premarket_no_data[ds];
}

void CMarketCenterData::Retry(DataSet ds, HWND notifyWnd)
{
	// 用户重试只取消退避；保留在途标记，避免旧请求尚未结束时重复并发。
	std::lock_guard<std::mutex> lock(m_sched_mutex);
	m_fail_until[ds] = 0;
}

bool CMarketCenterData::IsStale(DataSet ds, int staleSec) const
{
	time_t t = 0;
	switch (ds)
	{
	case DS_SECTORS: t = m_sectors_time; break;
	case DS_ETFS: t = m_etfs_time; break;
	case DS_MAINFLOW: t = m_fflow_time; break;
	case DS_TREND: t = m_dist_time; break;
	default: break;
	}
	return t == 0 || time(nullptr) - t > staleSec;
}

// ===== 取数调度：过期且未在途/未退避时投递后台任务，完成后 WM_APP+140 通知窗口 =====
bool CMarketCenterData::ApplySnapshot(DataSet ds, const std::string& payload, time_t fetchedAt, const std::string& tradeDate, int schemaVersion)
{
	if (schemaVersion != 1 || payload.empty() || fetchedAt <= 0 || tradeDate.empty()) return false;
	yyjson_doc* doc = yyjson_read(payload.data(), payload.size(), 0);
	if (!doc) return false;
	yyjson_val* root = yyjson_doc_get_root(doc);
	yyjson_val* version = root ? yyjson_obj_get(root, "version") : nullptr;
	yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
	bool ok = version && yyjson_is_uint(version) && yyjson_get_uint(version) == 1 && data;
	if (!ok) { yyjson_doc_free(doc); return false; }

	std::vector<MC::SectorFlow> sectors;
	std::vector<MC::EtfQuote> etfs;
	long long etfTotal = 0;
	MC::UpDownDist dist;
	std::vector<MC::TrendSample> trendCurve;
	std::vector<MC::FflowMinute> fflowSh, fflowSz;
	std::vector<MC::IndexTrendPoint> indexTrend;
	std::vector<MC::EtfFlowSample> etfFlow;
	long long distTime = 0;
	switch (ds)
	{
	case DS_SECTORS:
		if (!yyjson_is_arr(data) || yyjson_arr_size(data) > 10000) ok = false;
		if (ok) { size_t idx, max; yyjson_val* item; yyjson_arr_foreach(data, idx, max, item) {
			if (!yyjson_is_obj(item)) { ok = false; break; }
			MC::SectorFlow v; v.code=JsonString(item,"code"); v.name=JsonString(item,"name");
			v.flow=JsonNumber(item,"flow"); v.pct=JsonNumber(item,"pct"); v.superBig=JsonNumber(item,"superBig"); v.big=JsonNumber(item,"big"); v.mid=JsonNumber(item,"mid"); v.smallOrder=JsonNumber(item,"smallOrder");
			if (v.code.empty() || v.name.empty()) { ok=false; break; } sectors.push_back(std::move(v));
		} }
		break;
	case DS_ETFS:
		if (!yyjson_is_obj(data)) ok=false;
		else { yyjson_val* items=yyjson_obj_get(data,"items"); yyjson_val* total=yyjson_obj_get(data,"total"); if(!yyjson_is_arr(items)||yyjson_arr_size(items)>5000||!JsonFinite(total)) ok=false; else { etfTotal=static_cast<long long>(JsonNumber(data,"total")); size_t idx,max; yyjson_val* item; yyjson_arr_foreach(items,idx,max,item){ if(!yyjson_is_obj(item)){ok=false;break;} MC::EtfQuote v; v.code=JsonString(item,"code");v.name=JsonString(item,"name");v.theme=JsonString(item,"theme");v.price=JsonNumber(item,"price");v.pct=JsonNumber(item,"pct");v.amount=JsonNumber(item,"amount");v.inflow=JsonNumber(item,"inflow"); if(v.code.empty()||v.name.empty()){ok=false;break;} etfs.push_back(std::move(v)); } } }
		break;
	case DS_MAINFLOW:
		if (!yyjson_is_obj(data)) { ok=false; break; }
		{
			auto parseFlow = [&ok](yyjson_val* arr, std::vector<MC::FflowMinute>& target) {
				if (!yyjson_is_arr(arr) || yyjson_arr_size(arr) > 2000) { ok=false; return; }
				size_t idx,max; yyjson_val* item; yyjson_arr_foreach(arr,idx,max,item) {
					if (!yyjson_is_obj(item)) { ok=false; return; }
					MC::FflowMinute v; v.time=JsonString(item,"time"); v.main=JsonNumber(item,"main"); v.superBig=JsonNumber(item,"superBig"); v.big=JsonNumber(item,"big"); v.mid=JsonNumber(item,"mid"); v.smallOrder=JsonNumber(item,"smallOrder");
					if (v.time.empty()) { ok=false; return; } target.push_back(std::move(v));
				}
			};
			parseFlow(yyjson_obj_get(data,"sh"), fflowSh); parseFlow(yyjson_obj_get(data,"sz"), fflowSz);
			yyjson_val* arr=yyjson_obj_get(data,"index"); if(!yyjson_is_arr(arr)||yyjson_arr_size(arr)>2000) ok=false; else { size_t idx,max; yyjson_val* item; yyjson_arr_foreach(arr,idx,max,item){ if(!yyjson_is_obj(item)){ok=false;break;} MC::IndexTrendPoint v;v.time=JsonString(item,"time");v.price=JsonNumber(item,"a");if(v.time.empty()){ok=false;break;}indexTrend.push_back(std::move(v)); } }
			arr=yyjson_obj_get(data,"etfFlow"); if(!yyjson_is_arr(arr)||yyjson_arr_size(arr)>1000) ok=false; else { size_t idx,max; yyjson_val* item; yyjson_arr_foreach(arr,idx,max,item){ if(!yyjson_is_obj(item)){ok=false;break;} MC::EtfFlowSample v;v.time=JsonString(item,"time");v.inflow=JsonNumber(item,"a");if(v.time.empty()){ok=false;break;}etfFlow.push_back(std::move(v)); } }
		}
		break;
	case DS_TREND:
		if (!yyjson_is_obj(data)) { ok=false; break; }
		{
			dist.time=static_cast<time_t>(JsonNumber(data,"time")); dist.zt=static_cast<long long>(JsonNumber(data,"zt")); dist.dt=static_cast<long long>(JsonNumber(data,"dt"));
			yyjson_val* buckets=yyjson_obj_get(data,"buckets"); if(!yyjson_is_obj(buckets)||yyjson_obj_size(buckets)>100) ok=false; else { size_t idx,max; yyjson_val *key,*val; yyjson_obj_foreach(buckets,idx,max,key,val){ if(!key||!JsonFinite(val)) {ok=false;break;} dist.buckets[atoi(yyjson_get_str(key))]=static_cast<long long>(JsonNumber(nullptr,"",0)); if(yyjson_is_real(val)) dist.buckets[atoi(yyjson_get_str(key))]=static_cast<long long>(yyjson_get_real(val)); else if(yyjson_is_sint(val)) dist.buckets[atoi(yyjson_get_str(key))]=yyjson_get_sint(val); else dist.buckets[atoi(yyjson_get_str(key))]=static_cast<long long>(yyjson_get_uint(val)); } }
			double today=JsonNumber(data,"today",0), yesterday=JsonNumber(data,"yesterday",0);
			yyjson_val* curve=yyjson_obj_get(data,"curve"); if(!yyjson_is_arr(curve)||yyjson_arr_size(curve)>1000) ok=false; else { size_t idx,max; yyjson_val* item; yyjson_arr_foreach(curve,idx,max,item){if(!yyjson_is_obj(item)){ok=false;break;}MC::TrendSample v;v.time=JsonString(item,"time");v.up=static_cast<long long>(JsonNumber(item,"up"));v.down=static_cast<long long>(JsonNumber(item,"down"));if(v.time.empty()){ok=false;break;}trendCurve.push_back(std::move(v));}}
			if (ok)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_turnover_today = today;
				m_turnover_yesterday = yesterday;
			}
		}
		break;
	}
	if (ok)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		switch (ds)
		{
		case DS_SECTORS: m_sectors=std::move(sectors); m_sectors_time=fetchedAt; break;
		case DS_ETFS: m_etfs=std::move(etfs); m_etf_total=etfTotal; m_etfs_time=fetchedAt; break;
		case DS_MAINFLOW: m_fflow_sh=std::move(fflowSh); m_fflow_sz=std::move(fflowSz); m_index_trend=std::move(indexTrend); m_etf_flow_curve=std::move(etfFlow); m_fflow_time=fetchedAt; break;
		case DS_TREND: m_dist=std::move(dist); m_trend_curve=std::move(trendCurve); m_dist_time=fetchedAt; m_turnover_time=fetchedAt; break;
		default: break;
		}
	}
	yyjson_doc_free(doc);
	return ok;
}

void CMarketCenterData::LoadCachedSnapshots()
{
	const DataSet dataSets[] = { DS_SECTORS, DS_ETFS, DS_MAINFLOW, DS_TREND };
	for (DataSet ds : dataSets)
	{
		std::string payload, tradeDate; time_t fetchedAt=0; int version=0;
		if (!g_data.GetDbManager().LoadMarketCenterCache(static_cast<int>(ds), payload, fetchedAt, tradeDate, version)) continue;
		if (!ApplySnapshot(ds, payload, fetchedAt, tradeDate, version))
			g_data.GetDbManager().DeleteMarketCenterCache(static_cast<int>(ds));
	}
}

std::string CMarketCenterData::SerializeSnapshot(DataSet ds) const
{
	std::lock_guard<std::mutex> lock(const_cast<CMarketCenterData*>(this)->m_mutex);
	std::string out = "{\"version\":1,\"data\":";
	auto writePoint = [&out](const std::wstring& time, double a, double b = 0.0) {
		out += "{"; JsonStringField(out, "time", time); out += ","; JsonDoubleField(out, "a", a); out += ","; JsonDoubleField(out, "b", b); out += "}";
	};
	switch (ds)
	{
	case DS_SECTORS:
		out += "[";
		for (size_t i = 0; i < m_sectors.size(); ++i) { if (i) out += ","; const auto& v = m_sectors[i]; out += "{"; JsonStringField(out,"code",v.code); out+=","; JsonStringField(out,"name",v.name); out+=","; JsonDoubleField(out,"flow",v.flow); out+=","; JsonDoubleField(out,"pct",v.pct); out+=","; JsonDoubleField(out,"superBig",v.superBig); out+=","; JsonDoubleField(out,"big",v.big); out+=","; JsonDoubleField(out,"mid",v.mid); out+=","; JsonDoubleField(out,"smallOrder",v.smallOrder); out += "}"; }
		out += "]"; break;
	case DS_ETFS:
		out += "{\"total\":" + std::to_string(m_etf_total) + ",\"items\":[";
		for (size_t i = 0; i < m_etfs.size(); ++i) { if (i) out += ","; const auto& v = m_etfs[i]; out += "{"; JsonStringField(out,"code",v.code); out+=","; JsonStringField(out,"name",v.name); out+=","; JsonStringField(out,"theme",v.theme); out+=","; JsonDoubleField(out,"price",v.price); out+=","; JsonDoubleField(out,"pct",v.pct); out+=","; JsonDoubleField(out,"amount",v.amount); out+=","; JsonDoubleField(out,"inflow",v.inflow); out += "}"; }
		out += "]}"; break;
	case DS_MAINFLOW:
		out += "{\"sh\":[";
		for (size_t i=0;i<m_fflow_sh.size();++i) { if(i)out+=","; const auto& v=m_fflow_sh[i]; out+="{"; JsonStringField(out,"time",v.time); out+=","; JsonDoubleField(out,"main",v.main); out+=","; JsonDoubleField(out,"superBig",v.superBig); out+=","; JsonDoubleField(out,"big",v.big); out+=","; JsonDoubleField(out,"mid",v.mid); out+=","; JsonDoubleField(out,"smallOrder",v.smallOrder); out+="}"; }
		out += "],\"sz\":[";
		for (size_t i=0;i<m_fflow_sz.size();++i) { if(i)out+=","; const auto& v=m_fflow_sz[i]; out+="{"; JsonStringField(out,"time",v.time); out+=","; JsonDoubleField(out,"main",v.main); out+=","; JsonDoubleField(out,"superBig",v.superBig); out+=","; JsonDoubleField(out,"big",v.big); out+=","; JsonDoubleField(out,"mid",v.mid); out+=","; JsonDoubleField(out,"smallOrder",v.smallOrder); out+="}"; }
		out += "],\"index\":[";
		for (size_t i=0;i<m_index_trend.size();++i) { if(i)out+=","; const auto& v=m_index_trend[i]; writePoint(v.time,v.price); }
		out += "],\"etfFlow\":[";
		for (size_t i=0;i<m_etf_flow_curve.size();++i) { if(i)out+=","; const auto& v=m_etf_flow_curve[i]; writePoint(v.time,v.inflow); }
		out += "]}"; break;
	case DS_TREND:
		out += "{\"time\":" + std::to_string(static_cast<long long>(m_dist.time)) + ",\"zt\":" + std::to_string(m_dist.zt) + ",\"dt\":" + std::to_string(m_dist.dt) + ",\"buckets\":{";
		{ bool first=true; for (const auto& p:m_dist.buckets) { if(!first)out+=","; first=false; JsonKey(out,std::to_string(p.first).c_str()); out+=std::to_string(p.second); } }
		out += "},\"today\":"; JsonNum(out,m_turnover_today); out += ",\"yesterday\":"; JsonNum(out,m_turnover_yesterday); out += ",\"curve\":[";
		for (size_t i=0;i<m_trend_curve.size();++i) { if(i)out+=","; const auto& v=m_trend_curve[i]; out+="{"; JsonStringField(out,"time",v.time); out+=","; JsonDoubleField(out,"up",static_cast<double>(v.up)); out+=","; JsonDoubleField(out,"down",static_cast<double>(v.down)); out+="}"; }
		out += "]}"; break;
	}
	out += "}";
	return out;
}

bool CMarketCenterData::RequestIfStale(DataSet ds, int staleSec, HWND notifyWnd, RequestPriority priority)
{
	{
		std::lock_guard<std::mutex> schedLock(m_sched_mutex);
		if (m_inflight[ds] || IsInBackOff(ds))
			return false;
	}
	{
		std::lock_guard<std::mutex> dataLock(m_mutex);
		if (!IsStale(ds, staleSec))
			return false;
	}

	{
		std::lock_guard<std::mutex> schedLock(m_sched_mutex);
		if (m_inflight[ds] || IsInBackOff(ds))
			return false;
		m_inflight[ds] = true;
	}
	EnqueueRequest(ds, notifyWnd, priority);
	return true;
}

void CMarketCenterData::StartExecutor()
{
	std::lock_guard<std::mutex> lock(m_executor_mutex);
	if (m_executor_started)
		return;
	m_executor_stopping = false;
	m_executor_started = true;
	m_executor_thread = std::thread(&CMarketCenterData::ExecutorLoop, this);
}

void CMarketCenterData::StopExecutor()
{
	{
		std::lock_guard<std::mutex> lock(m_executor_mutex);
		if (!m_executor_started)
			return;
		m_executor_stopping = true;
		for (const auto& request : m_foreground_requests)
			m_inflight[request.dataSet] = false;
		for (const auto& request : m_warmup_requests)
			m_inflight[request.dataSet] = false;
		m_foreground_requests.clear();
		m_warmup_requests.clear();
	}
	{
		std::lock_guard<std::mutex> schedLock(m_sched_mutex);
		for (int i = 0; i < DS_COUNT; ++i)
			m_inflight[i] = false;
	}
	m_executor_cv.notify_all();
	if (m_executor_thread.joinable())
		m_executor_thread.join();
	std::lock_guard<std::mutex> lock(m_executor_mutex);
	m_executor_started = false;
	m_executor_stopping = false;
}

void CMarketCenterData::WarmupStaleData(HWND notifyWnd)
{
	const struct { DataSet dataSet; int staleSec; } requests[] = {
		{ DS_SECTORS, 120 }, { DS_ETFS, 300 }, { DS_MAINFLOW, 120 }, { DS_TREND, 60 }
	};
	for (const auto& request : requests)
		RequestIfStale(request.dataSet, request.staleSec, notifyWnd, RequestPriority::Warmup);
}

void CMarketCenterData::EnqueueRequest(DataSet ds, HWND notifyWnd, RequestPriority priority)
{
	std::lock_guard<std::mutex> lock(m_executor_mutex);
	if (!m_executor_started || m_executor_stopping)
	{
		std::lock_guard<std::mutex> schedLock(m_sched_mutex);
		m_inflight[ds] = false;
		return;
	}

	auto upgradeOrUpdate = [ds, notifyWnd](std::deque<PendingRequest>& queue) {
		for (auto& request : queue)
		{
			if (request.dataSet == ds)
			{
				if (notifyWnd)
					request.notifyWnd = notifyWnd;
				return true;
			}
		}
		return false;
	};

	if (priority == RequestPriority::Foreground)
	{
		if (!upgradeOrUpdate(m_foreground_requests))
		{
			if (!upgradeOrUpdate(m_warmup_requests))
				m_foreground_requests.push_back({ ds, notifyWnd });
			else
			{
				for (auto it = m_warmup_requests.begin(); it != m_warmup_requests.end(); ++it)
				{
					if (it->dataSet == ds)
					{
						m_foreground_requests.push_back(*it);
						m_warmup_requests.erase(it);
						break;
					}
				}
			}
		}
	}
	else if (!upgradeOrUpdate(m_foreground_requests) && !upgradeOrUpdate(m_warmup_requests))
	{
		m_warmup_requests.push_back({ ds, notifyWnd });
	}
	m_executor_cv.notify_one();
}

bool CMarketCenterData::ExecuteRequest(DataSet ds)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	switch (ds)
	{
	case DS_SECTORS: return FetchSectors();
	case DS_ETFS: return FetchEtfs();
	case DS_MAINFLOW: return FetchMainFlow();
	case DS_TREND: return FetchTrendDist();
	default: return false;
	}
}

void CMarketCenterData::ExecutorLoop()
{
	while (true)
	{
		PendingRequest request;
		{
			std::unique_lock<std::mutex> lock(m_executor_mutex);
			m_executor_cv.wait(lock, [this]() {
				return m_executor_stopping || !m_foreground_requests.empty() || !m_warmup_requests.empty();
			});
			if (m_executor_stopping)
				return;
			if (!m_foreground_requests.empty())
			{
				request = m_foreground_requests.front();
				m_foreground_requests.pop_front();
			}
			else
			{
				request = m_warmup_requests.front();
				m_warmup_requests.pop_front();
			}
		}

		bool ok = false;
		try { ok = ExecuteRequest(request.dataSet); }
		catch (CInternetException* e) { e->Delete(); }
		catch (...) {}

		if (ok)
		{
			const std::string payload = SerializeSnapshot(request.dataSet);
			if (!payload.empty())
			{
				const time_t fetchedAt = time(nullptr);
				if (!g_data.GetDbManager().SaveMarketCenterCache(static_cast<int>(request.dataSet), payload, fetchedAt, CurrentTradeDate()))
					CCommon::WriteLog("[MarketCenter] snapshot save failed", g_data.m_log_path.c_str());
			}
		}

		{
			std::lock_guard<std::mutex> schedLock(m_sched_mutex);
			m_inflight[request.dataSet] = false;
			if (ok)
			{
				m_failure_count[request.dataSet] = 0;
				m_fail_until[request.dataSet] = 0;
				m_last_failed[request.dataSet] = false;
			}
			else
			{
				unsigned int failures = min(++m_failure_count[request.dataSet], 8u);
				time_t delay = min<time_t>(2 * (1 << (failures - 1)), 300);
				m_fail_until[request.dataSet] = time(nullptr) + delay;
				m_last_failed[request.dataSet] = true;
			}
		}
		if (request.notifyWnd && ::IsWindow(request.notifyWnd))
			::PostMessage(request.notifyWnd, WM_APP + 140, static_cast<WPARAM>(request.dataSet), ok ? 1 : 0);
		if (ok)
		{
			const DataSet warmupOrder[] = { DS_SECTORS, DS_ETFS, DS_MAINFLOW, DS_TREND };
			for (DataSet next : warmupOrder)
			{
				if (next != request.dataSet && RequestIfStale(next, next == DS_ETFS ? 300 : (next == DS_SECTORS || next == DS_MAINFLOW ? 120 : 60), request.notifyWnd, RequestPriority::Warmup))
					break;
			}
		}
		Sleep(300);
	}
}

std::wstring CMarketCenterData::DeriveTheme(const std::wstring& name)
{
	for (const auto& kw : ETF_THEME_KEYWORDS)
	{
		if (name.find(kw) != std::wstring::npos)
			return kw;
	}
	// 未匹配到关键词时取名称前2字作兜底主题
	return name.substr(0, min(2, name.size()));
}

const std::vector<std::wstring>& CMarketCenterData::TimeAxis()
{
	static const std::vector<std::wstring> axis = [] {
		std::vector<std::wstring> arr;
		wchar_t buf[8];
		for (int h = 9; h <= 15; h++)
		{
			for (int m = 0; m < 60; m++)
			{
				if (h == 9 && m < 30) continue;
				if (h == 11 && m >= 30) break;
				if (h == 12) continue;
				if (h == 15 && m > 0) break;
				swprintf_s(buf, L"%02d:%02d", h, m);
				arr.push_back(buf);
			}
		}
		return arr;
	}();
	return axis;
}

int CMarketCenterData::TimeIndex(const std::wstring& hhmm)
{
	const auto& axis = TimeAxis();
	for (size_t i = 0; i < axis.size(); i++)
	{
		if (axis[i] == hhmm)
			return static_cast<int>(i);
	}
	return -1;
}

// ===== 行业板块主力资金流（双向 Top40，气泡图） =====
bool CMarketCenterData::FetchSectors()
{
	// fid=f62 按主力净流入排序，po=1 降序取流入 Top40、po=0 升序取流出 Top40；
	// 行业板块共 80+，双向 Top40 基本全覆盖
	// hadDiff：接口本身返回了 diff（区分"网络失败"与"盘前服务端清库（f62 全为'-'）"）
	auto fetchHalf = [&](bool desc, bool& hadDiff) -> std::vector<MC::SectorFlow> {
		hadDiff = false;
		std::wstring url = L"https://push2.eastmoney.com/api/qt/clist/get?pn=1&pz=40&";
		url += desc ? L"po=1" : L"po=0";
		url += L"&np=1&fltt=2&invt=2&fid=f62&fs=m:90+t:2&fields=f12,f14,f2,f3,f62,f66,f72,f78,f84";
		std::string resp;
		if (!HttpGet(url, resp)) return {};
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) return {};
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		yyjson_val* diff = data ? yyjson_obj_get(data, "diff") : nullptr;
		if (diff && yyjson_is_arr(diff))
			hadDiff = true;
		std::vector<MC::SectorFlow> out;
		if (diff && yyjson_is_arr(diff))
		{
			yyjson_val* item;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(diff, &iter);
			while ((item = yyjson_arr_iter_next(&iter)))
			{
				// 盘前/清算时段资金流字段为"-"（非数字），条目视为无数据
				if (JsonIsDash(item, "f62"))
					continue;
				MC::SectorFlow s;
				s.code = JsonWStr(item, "f12");
				s.name = JsonWStr(item, "f14");
				if (s.name.empty()) continue;
				s.pct = JsonNum(item, "f3");
				s.flow = JsonNum(item, "f62");
				s.superBig = JsonNum(item, "f66");
				s.big = JsonNum(item, "f72");
				s.mid = JsonNum(item, "f78");
				s.smallOrder = JsonNum(item, "f84");
				out.push_back(std::move(s));
			}
		}
		yyjson_doc_free(doc);
		return out;
	};

	bool inHadDiff = false, outHadDiff = false;
	std::vector<MC::SectorFlow> in = fetchHalf(true, inHadDiff);
	std::vector<MC::SectorFlow> out = fetchHalf(false, outHadDiff);
	if (in.empty() && !out.empty())
	{
		Sleep(400);
		in = fetchHalf(true, inHadDiff);   // 流入半边偶发失败重试
	}
	if (out.empty() && !in.empty())
	{
		Sleep(400);
		out = fetchHalf(false, outHadDiff);
	}
	if (in.empty() && out.empty())
	{
		if (inHadDiff || outHadDiff)
		{
			// 盘前/清算时段：接口正常但资金流字段被服务端清空。置盘前标记并按成功处理，
			// 让 UI 显示"盘前暂无数据"而非错误提示/整屏 0；m_sectors_time 照常刷新，
			// 2 分钟后自然重查，开盘即自动恢复
			std::lock_guard<std::mutex> lock(m_mutex);
			m_sectors.clear();
			m_sectors_time = time(nullptr);
			m_premarket_no_data[DS_SECTORS] = true;
			MarkSuccess(DS_SECTORS);
			return true;
		}
		return false;
	}

	// 合并去重（同板块可能同时出现在两半）
	std::map<std::wstring, MC::SectorFlow> merged;
	for (auto& s : in) merged[s.code] = std::move(s);
	for (auto& s : out) if (!merged.count(s.code)) merged[s.code] = std::move(s);

	std::vector<MC::SectorFlow> sectors;
	sectors.reserve(merged.size());
	for (auto& p : merged) sectors.push_back(std::move(p.second));

	std::lock_guard<std::mutex> lock(m_mutex);
	m_sectors = std::move(sectors);
	m_sectors_time = time(nullptr);
	m_premarket_no_data[DS_SECTORS] = false;
	MarkSuccess(DS_SECTORS);
	return true;
}

// ===== ETF 全量分页抓取（pz=100，服务端单页上限） =====
bool CMarketCenterData::FetchEtfs()
{
	const int PAGE_SIZE = 100;
	const int MAX_PAGES = 15;   // 1319只 → 14页，留1页余量
	std::vector<MC::EtfQuote> all;
	long long total = 0;

	for (int page = 1; page <= MAX_PAGES; page++)
	{
		std::wstring url = L"https://push2.eastmoney.com/api/qt/clist/get?pn=" + std::to_wstring(page)
			+ L"&pz=" + std::to_wstring(PAGE_SIZE)
			+ L"&po=1&np=1&fltt=2&invt=2&fid=f3&fs=b:MK0021&fields=f12,f14,f2,f3,f6,f62";
		std::string resp;
		if (!HttpGet(url, resp))
		{
			Sleep(500);
			if (!HttpGet(url, resp))
				break;   // 连续两页失败才放弃（保留已拉到的部分）
		}
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) break;
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		yyjson_val* diff = data ? yyjson_obj_get(data, "diff") : nullptr;
		total = data ? static_cast<long long>(JsonNum(data, "total")) : 0;
		int got = 0;
		if (diff && yyjson_is_arr(diff))
		{
			yyjson_val* item;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(diff, &iter);
			while ((item = yyjson_arr_iter_next(&iter)))
			{
				MC::EtfQuote e;
				e.code = JsonWStr(item, "f12");
				e.name = JsonWStr(item, "f14");
				if (e.name.empty()) continue;
				// fltt=2 时 "-" 表示停牌等无值状态
				e.price = JsonIsDash(item, "f2") ? 0.0 : JsonNum(item, "f2");
				e.pct = JsonIsDash(item, "f3") ? 0.0 : JsonNum(item, "f3");
				e.amount = JsonIsDash(item, "f6") ? 0.0 : JsonNum(item, "f6");
				e.inflow = JsonIsDash(item, "f62") ? 0.0 : JsonNum(item, "f62");
				e.theme = DeriveTheme(e.name);
				all.push_back(std::move(e));
				got++;
			}
		}
		yyjson_doc_free(doc);
		if (got == 0) break;
		if (total > 0 && static_cast<int>(all.size()) >= total) break;
		Sleep(400); // 翻页间隔，避免触发服务端频控（14 页约 5.6s）
	}

	if (all.empty())
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_etfs = std::move(all);
		m_etf_total = total;
		m_etfs_time = time(nullptr);
		MarkSuccess(DS_ETFS);
	}
	AppendEtfFlowSample();
	return true;
}

// ===== 主力资金分时（沪深 fflow kline + 上证指数 trends2） =====
namespace
{
	// 解析 fflow kline：fields2=f51..f56 → "时间,主力,小单,中单,大单,超大单"
	std::vector<MC::FflowMinute> ParseFflowKlines(const std::string& resp)
	{
		std::vector<MC::FflowMinute> out;
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) return out;
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		yyjson_val* klines = data ? yyjson_obj_get(data, "klines") : nullptr;
		if (klines && yyjson_is_arr(klines))
		{
			yyjson_val* item;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(klines, &iter);
			while ((item = yyjson_arr_iter_next(&iter)))
			{
				const char* str = yyjson_get_str(item);
				if (!str) continue;
				std::vector<std::string> parts = CCommon::split(str, ',');
				if (parts.size() < 6) continue;
				MC::FflowMinute f;
				// "2026-09-07 09:31" → "09:31"
				std::string t = parts[0];
				size_t sp = t.find(' ');
				if (sp != std::string::npos && sp + 1 < t.size())
					f.time = CCommon::StrToUnicode(t.substr(sp + 1).c_str(), true);
				else
					f.time = CCommon::StrToUnicode(t.c_str(), true);
				f.main = atof(parts[1].c_str());
				f.smallOrder = atof(parts[2].c_str());
				f.mid = atof(parts[3].c_str());
				f.big = atof(parts[4].c_str());
				f.superBig = atof(parts[5].c_str());
				out.push_back(std::move(f));
			}
		}
		yyjson_doc_free(doc);
		return out;
	}

	bool FetchFflowForSecid(const wchar_t* secid, std::vector<MC::FflowMinute>& out)
	{
		std::wstring url = L"https://push2.eastmoney.com/api/qt/stock/fflow/kline/get?secid="
			+ std::wstring(secid)
			+ L"&klt=1&lmt=0&fields1=f1,f2,f3,f7&fields2=f51,f52,f53,f54,f55,f56";
		std::string resp;
		if (!HttpGet(url, resp)) return false;
		out = ParseFflowKlines(resp);
		return !out.empty();
	}

	bool FetchIndexTrends(std::vector<MC::IndexTrendPoint>& out)
	{
		std::wstring url = L"https://push2.eastmoney.com/api/qt/stock/trends2/get?secid=1.000001"
			L"&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12,f13&fields2=f51,f52,f53,f54,f55,f56,f57,f58";
		std::string resp;
		if (!HttpGet(url, resp)) return false;
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) return false;
		bool ok = false;
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		yyjson_val* trends = data ? yyjson_obj_get(data, "trends") : nullptr;
		if (trends && yyjson_is_arr(trends))
		{
			yyjson_val* item;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(trends, &iter);
			while ((item = yyjson_arr_iter_next(&iter)))
			{
				const char* str = yyjson_get_str(item);
				if (!str) continue;
				// "2026-09-07 09:31,3920.12,..." 取时间与现价
				std::vector<std::string> parts = CCommon::split(str, ',');
				if (parts.size() < 3) continue;
				std::string t = parts[0];
				size_t sp = t.find(' ');
				if (sp == std::string::npos) continue;
				MC::IndexTrendPoint pt;
				pt.time = CCommon::StrToUnicode(t.substr(sp + 1).c_str(), true);
				pt.price = atof(parts[2].c_str());
				out.push_back(std::move(pt));
			}
			ok = !out.empty();
		}
		yyjson_doc_free(doc);
		return ok;
	}
}

bool CMarketCenterData::FetchMainFlow()
{
	std::vector<MC::FflowMinute> sh, sz;
	std::vector<MC::IndexTrendPoint> trend;
	bool okSh = FetchFflowForSecid(L"1.000001", sh);
	bool okSz = FetchFflowForSecid(L"0.399001", sz);
	bool okIdx = FetchIndexTrends(trend);
	if (!okSh && !okSz)
		return false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (okSh) m_fflow_sh = std::move(sh);
		if (okSz) m_fflow_sz = std::move(sz);
		if (okIdx) m_index_trend = std::move(trend);
		m_fflow_time = time(nullptr);
		MarkSuccess(DS_MAINFLOW);
	}
	return true;
}

// ===== 涨跌分布 + 涨停/跌停池 + 沪深成交额 =====
namespace
{
	bool FetchZDPool(const char* api, long long& count)
	{
		time_t now = time(nullptr);
		struct tm localTm{};
		localtime_s(&localTm, &now);
		wchar_t dateStr[16];
		swprintf_s(dateStr, L"%04d%02d%02d", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
		std::wstring fullUrl = L"https://push2ex.eastmoney.com/getTopic" + CCommon::StrToUnicode(api, false)
			+ L"?ut=7eea3edcaed734bea9cbfc24409ed989&dpt=wz.ztzt&Pageindex=0&pagesize=600&sort=fund%3Aasc&date=" + dateStr;
		std::string resp;
		if (!HttpGet(fullUrl, resp)) return false;
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) return false;
		bool ok = false;
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		if (data)
		{
			// tc = 池内总数
			yyjson_val* tc = yyjson_obj_get(data, "tc");
			if (tc && yyjson_is_uint(tc))
			{
				count = static_cast<long long>(yyjson_get_uint(tc));
				ok = true;
			}
			else if (data != nullptr)
			{
				// 无 tc 时以 pool 数组长度兜底
				yyjson_val* pool = yyjson_obj_get(data, "pool");
				if (pool && yyjson_is_arr(pool))
				{
					count = yyjson_arr_size(pool);
					ok = true;
				}
			}
		}
		yyjson_doc_free(doc);
		return ok;
	}

	// 指数日K（lmt=2）：row0=昨日全天成交额，row1=今日实时累计成交额（盘中为部分量）
	bool FetchIndexTurnover(const wchar_t* secid, double& todayTurnover, double& prevTurnover)
	{
		std::wstring url = L"https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=" + std::wstring(secid)
			+ L"&klt=101&fqt=1&end=20500101&lmt=2&fields1=f1,f2,f3&fields2=f51,f52,f53,f54,f55,f56,f57";
		std::string resp;
		if (!HttpGet(url, resp)) return false;
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) return false;
		bool ok = false;
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		yyjson_val* klines = data ? yyjson_obj_get(data, "klines") : nullptr;
		if (klines && yyjson_is_arr(klines) && yyjson_arr_size(klines) >= 1)
		{
			// lmt=2 返回的两行中最后一行是今日（盘中为部分成交额），倒数第二行是昨日全天成交额
			std::vector<double> amounts;
			yyjson_val* item;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(klines, &iter);
			while ((item = yyjson_arr_iter_next(&iter)))
			{
				const char* str = yyjson_get_str(item);
				if (!str) continue;
				std::vector<std::string> parts = CCommon::split(str, ',');
				if (parts.size() < 7) continue;
				amounts.push_back(atof(parts[6].c_str()));
			}
			if (amounts.size() >= 2)
			{
				prevTurnover = amounts[amounts.size() - 2];
				todayTurnover = amounts[amounts.size() - 1];
				ok = true;
			}
			else if (amounts.size() == 1)
			{
				todayTurnover = amounts[0];
				ok = true;
			}
		}
		yyjson_doc_free(doc);
		return ok;
	}
}

bool CMarketCenterData::FetchTrendDist()
{
	MC::UpDownDist dist;
	dist.time = time(nullptr);

	std::string resp;
	std::wstring url = L"https://push2ex.eastmoney.com/getTopicZDFenBu?ut=7eea3edcaed734bea9cbfc24409ed989&dpt=wz.ztzt";
	if (!HttpGet(url, resp))
		return false;
	yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
	if (!doc) return false;
	bool gotDist = false;
	yyjson_val* root = yyjson_doc_get_root(doc);
	yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
	yyjson_val* fenbu = data ? yyjson_obj_get(data, "fenbu") : nullptr;
	if (fenbu && yyjson_is_arr(fenbu))
	{
		yyjson_val* item;
		yyjson_arr_iter iter;
		yyjson_arr_iter_init(fenbu, &iter);
		while ((item = yyjson_arr_iter_next(&iter)))
		{
			if (!item || !yyjson_is_obj(item)) continue;
			yyjson_obj_iter oiter;
			yyjson_obj_iter_init(item, &oiter);
			yyjson_val* keyVal;
			while ((keyVal = yyjson_obj_iter_next(&oiter)))
			{
				int bucket = atoi(yyjson_get_str(keyVal));
				long long cnt = static_cast<long long>(yyjson_get_uint(yyjson_obj_iter_get_val(keyVal)));
				if (cnt > 0)
					dist.buckets[bucket] = cnt;
			}
		}
		gotDist = !dist.buckets.empty();
	}
	yyjson_doc_free(doc);
	if (!gotDist)
		return false;

	FetchZDPool("ZTPool", dist.zt);
	FetchZDPool("DTPool", dist.dt);

	// 沪深成交额（今日 + 昨日）
	double shT = 0, shPrev = 0, szT = 0, szPrev = 0;
	bool okTurnover = FetchIndexTurnover(L"1.000001", shT, shPrev) && FetchIndexTurnover(L"0.399001", szT, szPrev);

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_dist = dist;
		m_dist_time = time(nullptr);
		if (okTurnover)
		{
			m_turnover_today = shT + szT;
			m_turnover_yesterday = shPrev + szPrev;
			m_turnover_time = time(nullptr);
		}
		MarkSuccess(DS_TREND);
	}
	AppendTrendSample();
	return true;
}

// ===== 自积累曲线采样 =====
void CMarketCenterData::AppendTrendSample()
{
	long long up, down;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		up = m_dist.UpCount();
		down = m_dist.DownCount();
	}
	time_t now = time(nullptr);
	struct tm localTm{};
	localtime_s(&localTm, &now);
	wchar_t buf[8];
	swprintf_s(buf, L"%02d:%02d", localTm.tm_hour, localTm.tm_min);

	std::lock_guard<std::mutex> lock(m_mutex);
	std::wstring t = buf;
	// 同一分钟覆盖写，跨分钟追加
	if (!m_trend_curve.empty() && m_trend_curve.back().time == t)
		m_trend_curve.back() = { t, up, down };
	else
		m_trend_curve.push_back({ t, up, down });
	if (m_trend_curve.size() > 300)
		m_trend_curve.erase(m_trend_curve.begin());
}

void CMarketCenterData::AppendEtfFlowSample()
{
	double sum = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& e : m_etfs) sum += e.inflow;
	}
	time_t now = time(nullptr);
	struct tm localTm{};
	localtime_s(&localTm, &now);
	wchar_t buf[8];
	swprintf_s(buf, L"%02d:%02d", localTm.tm_hour, localTm.tm_min);

	std::lock_guard<std::mutex> lock(m_mutex);
	std::wstring t = buf;
	if (!m_etf_flow_curve.empty() && m_etf_flow_curve.back().time == t)
		m_etf_flow_curve.back().inflow = sum;
	else
		m_etf_flow_curve.push_back({ t, sum });
	if (m_etf_flow_curve.size() > 300)
		m_etf_flow_curve.erase(m_etf_flow_curve.begin());
}
