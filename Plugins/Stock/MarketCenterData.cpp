#include "pch.h"
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

bool CMarketCenterData::IsInBackOff(DataSet ds) const
{
	return m_fail_until[ds] > 0 && time(nullptr) < m_fail_until[ds];
}

void CMarketCenterData::MarkSuccess(DataSet ds)
{
	m_fail_until[ds] = 0;
}

void CMarketCenterData::MarkFailure(DataSet ds)
{
	m_fail_until[ds] = time(nullptr) + FAIL_BACKOFF_SEC;
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
bool CMarketCenterData::RequestIfStale(DataSet ds, int staleSec, HWND notifyWnd)
{
	std::lock_guard<std::mutex> schedLock(m_sched_mutex);
	if (m_inflight[ds] || IsInBackOff(ds) || !IsStale(ds, staleSec))
		return false;
	m_inflight[ds] = true;
	CStockFetchThread::Instance().PostBackgroundTask([this, ds, notifyWnd]() {
		AFX_MANAGE_STATE(AfxGetStaticModuleState());   // 工作线程内使用 MFC(CInternetSession) 必需
		// 错峰：后台任务队列本身串行，这里再加固定间隔，避免开盘时 4 个数据集
		// 连续 10+ 个请求触发东财 WAF（低频指纹通过率远高于突发）
		Sleep(300 * static_cast<int>(ds));
		bool ok = false;
		switch (ds)
		{
		case DS_SECTORS: ok = FetchSectors(); break;
		case DS_ETFS: ok = FetchEtfs(); break;
		case DS_MAINFLOW: ok = FetchMainFlow(); break;
		case DS_TREND: ok = FetchTrendDist(); break;
		default: break;
		}
		if (!ok)
			MarkFailure(ds);
		m_inflight[ds] = false;
		Sleep(300);   // 任务间最小间隔，降低突发请求密度
		if (notifyWnd && ::IsWindow(notifyWnd))
			::PostMessage(notifyWnd, WM_APP + 140, (WPARAM)ds, ok ? 1 : 0);
	});
	return true;
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

// ===== 行业板块主力资金流（双向 Top60，气泡图） =====
bool CMarketCenterData::FetchSectors()
{
	// fid=f62 按主力净流入排序，po=1 降序取流入 Top60、po=0 升序取流出 Top60
	auto fetchHalf = [&](bool desc) -> std::vector<MC::SectorFlow> {
		std::wstring url = L"https://push2.eastmoney.com/api/qt/clist/get?pn=1&pz=60&";
		url += desc ? L"po=1" : L"po=0";
		url += L"&np=1&fltt=2&invt=2&fid=f62&fs=m:90+t:2&fields=f12,f14,f2,f3,f62,f66,f72,f78,f84";
		std::string resp;
		if (!HttpGet(url, resp)) return {};
		yyjson_doc* doc = yyjson_read(resp.c_str(), resp.size(), 0);
		if (!doc) return {};
		yyjson_val* root = yyjson_doc_get_root(doc);
		yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
		yyjson_val* diff = data ? yyjson_obj_get(data, "diff") : nullptr;
		std::vector<MC::SectorFlow> out;
		if (diff && yyjson_is_arr(diff))
		{
			yyjson_val* item;
			yyjson_arr_iter iter;
			yyjson_arr_iter_init(diff, &iter);
			while ((item = yyjson_arr_iter_next(&iter)))
			{
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

	std::vector<MC::SectorFlow> in = fetchHalf(true);
	std::vector<MC::SectorFlow> out = fetchHalf(false);
	if (in.empty() && !out.empty())
	{
		Sleep(400);
		in = fetchHalf(true);   // 流入半边偶发失败重试
	}
	if (out.empty() && !in.empty())
	{
		Sleep(400);
		out = fetchHalf(false);
	}
	if (in.empty() && out.empty())
		return false;

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
			+ L"&klt=101&fqt=1&lmt=2&fields1=f1,f2,f3&fields2=f51,f52,f53,f54,f55,f56,f57";
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
