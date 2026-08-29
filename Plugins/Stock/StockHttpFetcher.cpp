#include "pch.h"
#include "StockHttpFetcher.h"
#include "Common.h"
#include "Stock.h"
#include <afxinet.h>
#include "utilities/yyjson/yyjson.h"
#include "utilities/JsonHelper.h"
#include <algorithm>
#include <cmath>

// HTTP 获取器专用 User-Agent
constexpr auto WEB_USERAGENT = _T("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36 Edg/135.0.0.0");

CStockHttpFetcher g_http_fetcher;

// ===== 内部辅助函数 =====

static double generateRandomDouble()
{
	srand(time(nullptr)); // 设置随机种子
	double random = (double)rand() / RAND_MAX;
	return random;
}

static double GetJsonDoubleValue(yyjson_val* val)
{
	if (val == nullptr) return 0.0;
	if (yyjson_is_real(val)) return yyjson_get_real(val);
	if (yyjson_is_sint(val)) return static_cast<double>(yyjson_get_sint(val));
	if (yyjson_is_uint(val)) return static_cast<double>(yyjson_get_uint(val));
	if (yyjson_is_str(val))
	{
		try
		{
			return std::stod(yyjson_get_str(val));
		}
		catch (...)
		{
			return 0.0;
		}
	}
	return 0.0;
}

static bool TryParseDouble(const std::string& value, double& result)
{
	try
	{
		result = std::stod(value);
		return true;
	}
	catch (...)
	{
		result = 0.0;
		return false;
	}
}

// 东方财富 secid 转换（sh/sz/bj 前缀去除后按首字母判断市场）
static std::wstring GetEastMoneySecId(const std::wstring& stockId)
{
	std::wstring code = stockId;
	if (code.rfind(kSH, 0) == 0 || code.rfind(kSZ, 0) == 0 || code.rfind(kBJ, 0) == 0)
		code = code.substr(2);

	if (code.size() != 6)
		return L"";

	if (code[0] == L'6' || code[0] == L'5')
		return L"1." + code;
	if (code[0] == L'0' || code[0] == L'3' || code[0] == L'1')
		return L"0." + code;
	if (code[0] == L'8' || code[0] == L'4')
		return L"0." + code;
	return L"";
}

// ===== 实现 =====

bool CStockHttpFetcher::FetchRealtimeHtml(const std::vector<std::wstring>& allCodes, bool onlyNonAG,
	std::vector<std::wstring>& outCodes, std::string& outResp)
{
	outCodes.clear();
	outCodes = allCodes;

	// onlyNonAG模式：仅获取非A股代码（港股等），A股个股由共享内存提供（大盘指数继续由HTTP获取）
	if (onlyNonAG)
	{
		outCodes.erase(std::remove_if(outCodes.begin(), outCodes.end(),
			[](const std::wstring& code) { return CCommon::IsAGStockCode(code) && GetStockPriority(code) >= 200; }), outCodes.end());
	}

	if (outCodes.empty())
		return false;

	// 1. 主数据源：腾讯证券 (qt.gtimg.cn)
	{
		std::vector<std::wstring> txCodes = outCodes;
		for (auto& code : txCodes)
		{
			if (code.find(kHK) == 0)
				code = L"r_" + code.substr(2);  // rt_hk00700 -> r_hk00700
		}
		std::wstring url{ L"http://qt.gtimg.cn/q=" };
		url += CCommon::vectorJoinString(txCodes, L",");
		CString strHeaders = _T("Referer: https://finance.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("v_") != std::string::npos)
		{
			return true;
		}
	}

	// 2. 一级保底：新浪财经 (hq.sinajs.cn)
	{
		std::wstring url{ L"https://hq.sinajs.cn/?" };
		std::vector<std::wstring> params;
		params.push_back(L"_=" + std::to_wstring(generateRandomDouble()));
		params.push_back(L"list=" + CCommon::vectorJoinString(outCodes, L","));
		url += CCommon::vectorJoinString(params, L"&");

		CString strHeaders = _T("Referer: https://finance.sina.com.cn");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("hq_str_") != std::string::npos)
		{
			return true;
		}
	}

	return false;
}

bool CStockHttpFetcher::FetchInnerOuterHtml(const std::vector<std::wstring>& allCodes, bool includeAG, std::string& outResp)
{
	std::vector<std::wstring> codes;
	for (const auto& code : allCodes)
	{
		bool isAG = CCommon::IsAGStockCode(code);
		// includeAG=true 时获取所有股票的内外盘；否则仅非A股
		if (includeAG || !isAG)
			codes.push_back(code);
	}
	if (codes.empty()) return false;

	// 腾讯API对港股使用 r_hk 前缀（不是 rt_hk），需要转换
	for (auto& code : codes)
	{
		if (code.find(kHK) == 0)
			code = L"r_" + code.substr(2);  // rt_hk00700 -> r_hk00700
	}

	std::wstring url{ L"http://qt.gtimg.cn/q=" };
	url += CCommon::vectorJoinString(codes, L",");

	CString strHeaders = _T("Referer: https://finance.qq.com");
	return CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength());
}

bool CStockHttpFetcher::FetchCallAuctionHtml(const std::vector<std::wstring>& allCodes,
	std::vector<std::wstring>& outCodes, std::string& outResp)
{
	outCodes.clear();
	// 仅获取A股代码（sh/sz/bj），过滤掉港股、美股等不支持集合竞价的代码
	for (const auto& code : allCodes)
	{
		if (CCommon::IsAGStockCode(code))
			outCodes.push_back(code);
	}
	if (outCodes.empty()) return false;

	std::wstring url{ L"http://qt.gtimg.cn/q=" };
	url += CCommon::vectorJoinString(outCodes, L",");

	CString strHeaders = _T("Referer: https://finance.qq.com");
	return CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength());
}

bool CStockHttpFetcher::FetchTimeline(const std::wstring& code, std::string& outResp)
{
	// 1. 主数据源：腾讯分时 (minute/query)
	{
		std::wstring txCode = code;
		if (txCode.find(kHK) == 0)
			txCode = L"r_" + txCode.substr(2);

		std::wstring url = L"https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=" + txCode;
		CString strHeaders = _T("Referer: https://finance.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"minute\"") != std::string::npos)
		{
			return true;
		}
	}

	// 2. 一级保底：新浪分时 (getMinlineData)
	{
		std::wstring url{ L"https://cn.finance.sina.com.cn/minline/getMinlineData?" };
		std::vector<std::wstring> params;
		params.push_back(L"symbol=" + code);
		params.push_back(L"version=7.11.0");
		params.push_back(L"dpc=1");

		SYSTEMTIME st;
		GetLocalTime(&st);
		wchar_t dateBuf[20];
		swprintf_s(dateBuf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
		params.push_back(L"date=" + std::wstring(dateBuf));
		url += CCommon::vectorJoinString(params, L"&");

		std::wstring strHeaders{ L"Referer: https://finance.sina.com.cn/realstock/company/" };
		strHeaders += code;
		strHeaders += L"/nc.shtml";
		CString headers = strHeaders.c_str();

		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, headers, headers.GetLength()) && !outResp.empty() && outResp.find("\"data\"") != std::string::npos)
		{
			return true;
		}
	}

	// 3. 二级保底：东方财富分时 (trends2)
	std::wstring secId = GetEastMoneySecId(code);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring url{ L"https://push2.eastmoney.com/api/qt/stock/trends2/get?" };
			std::vector<std::wstring> params;
			params.push_back(L"secid=" + secId);
			params.push_back(L"fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12,f13");
			params.push_back(L"fields2=f51,f52,f53,f54,f55,f56,f57,f58");
			url += CCommon::vectorJoinString(params, L"&");

			CString strHeaders = _T("Referer: https://quote.eastmoney.com");
			if (CCommon::GetURL(url, outResp, true, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"trends\"") != std::string::npos)
			{
				return true;
			}
			else
			{
				m_eastmoney_fail_until = time(nullptr) + 600;
			}
		}
		catch (...)
		{
			m_eastmoney_fail_until = time(nullptr) + 600;
		}
	}

	return false;
}

bool CStockHttpFetcher::FetchDayKLine(const std::wstring& code, int days, std::string& outResp)
{
	// 1. 主数据源：腾讯前复权(QFQ)接口
	{
		std::wstring url = L"https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=" + code + L",day,,," + std::to_wstring(days) + L",qfq";
		CString strHeaders = _T("Referer: https://finance.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"data\"") != std::string::npos && outResp.find(CCommon::UnicodeToStr(code.c_str())) != std::string::npos)
		{
			return true;
		}
	}

	// 2. 一级保底：东方财富前复权(QFQ)接口
	std::wstring secId = GetEastMoneySecId(code);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring url = L"https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=" + secId + L"&klt=101&fqt=1&lmt=" + std::to_wstring(days) + L"&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56,f57,f58,f59,f60,f61";
			CString strHeaders = _T("Referer: https://quote.eastmoney.com");
			if (CCommon::GetURL(url, outResp, true, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"klines\"") != std::string::npos)
			{
				return true;
			}
			else
			{
				m_eastmoney_fail_until = time(nullptr) + 600;
			}
		}
		catch (...)
		{
			m_eastmoney_fail_until = time(nullptr) + 600;
		}
	}

	// 3. 二级保底：新浪日K接口
	{
		std::wstring sinaUrl{ L"https://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData?" };
		std::vector<std::wstring> params;
		params.push_back(L"symbol=" + code);
		params.push_back(L"scale=240");
		params.push_back(L"ma=no");
		params.push_back(L"datalen=" + std::to_wstring(days));
		sinaUrl += CCommon::vectorJoinString(params, L"&");

		CString sinaHeaders = _T("Referer: http://finance.sina.com.cn");
		if (CCommon::GetURL(sinaUrl, outResp, false, WEB_USERAGENT, sinaHeaders, sinaHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}

	return false;
}

bool CStockHttpFetcher::FetchWeekKLine(const std::wstring& code, int weeks, std::string& outResp)
{
	// 1. 主数据源：腾讯周K
	std::wstring url = L"https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=" + code + L",week,,," + std::to_wstring(weeks) + L",qfq";
	CString strHeaders = _T("Referer: https://finance.qq.com");
	if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"data\"") != std::string::npos)
	{
		return true;
	}

	// 2. 一级保底：东方财富周K
	std::wstring secId = GetEastMoneySecId(code);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring emUrl = L"https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=" + secId + L"&klt=102&fqt=1&lmt=" + std::to_wstring(weeks) + L"&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56,f57,f58,f59,f60,f61";
			CString emHeaders = _T("Referer: https://quote.eastmoney.com");
			if (CCommon::GetURL(emUrl, outResp, true, WEB_USERAGENT, emHeaders, emHeaders.GetLength()) && !outResp.empty() && outResp.find("\"klines\"") != std::string::npos)
			{
				return true;
			}
		}
		catch (...) {}
	}

	return false;
}

bool CStockHttpFetcher::FetchMonthKLine(const std::wstring& code, int months, std::string& outResp)
{
	// 1. 主数据源：腾讯月K
	std::wstring url = L"https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=" + code + L",month,,," + std::to_wstring(months) + L",qfq";
	CString strHeaders = _T("Referer: https://finance.qq.com");
	if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"data\"") != std::string::npos)
	{
		return true;
	}

	// 2. 一级保底：东方财富月K
	std::wstring secId = GetEastMoneySecId(code);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring emUrl = L"https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=" + secId + L"&klt=103&fqt=1&lmt=" + std::to_wstring(months) + L"&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56,f57,f58,f59,f60,f61";
			CString emHeaders = _T("Referer: https://quote.eastmoney.com");
			if (CCommon::GetURL(emUrl, outResp, true, WEB_USERAGENT, emHeaders, emHeaders.GetLength()) && !outResp.empty() && outResp.find("\"klines\"") != std::string::npos)
			{
				return true;
			}
		}
		catch (...) {}
	}

	return false;
}

bool CStockHttpFetcher::FetchMin5KLine(const std::wstring& code, int datalen, std::string& outResp)
{
	// 1. 主数据源：腾讯5分钟K
	{
		std::wstring url = L"https://web.ifzq.gtimg.cn/appstock/app/kline/kline?param=" + code + L",m5,," + std::to_wstring(datalen);
		CString strHeaders = _T("Referer: https://finance.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"m5\"") != std::string::npos)
		{
			return true;
		}
	}

	// 2. 一级保底：新浪5分钟K
	{
		std::wstring url{ L"https://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData?" };
		std::vector<std::wstring> params;
		params.push_back(L"symbol=" + code);
		params.push_back(L"scale=5");
		params.push_back(L"ma=no");
		params.push_back(L"datalen=" + std::to_wstring(datalen));
		url += CCommon::vectorJoinString(params, L"&");

		CString strHeaders = _T("Referer: http://finance.sina.com.cn");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}

	// 3. 二级保底：东方财富5分钟K
	std::wstring secId = GetEastMoneySecId(code);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring emUrl = L"https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=" + secId + L"&klt=5&fqt=1&lmt=" + std::to_wstring(datalen) + L"&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56";
			CString emHeaders = _T("Referer: https://quote.eastmoney.com");
			if (CCommon::GetURL(emUrl, outResp, true, WEB_USERAGENT, emHeaders, emHeaders.GetLength()) && !outResp.empty() && outResp.find("\"klines\"") != std::string::npos)
			{
				return true;
			}
		}
		catch (...) {}
	}

	return false;
}

bool CStockHttpFetcher::FetchMin30KLine(const std::wstring& code, int datalen, std::string& outResp)
{
	// 1. 主数据源：腾讯30分钟K
	{
		std::wstring url = L"https://web.ifzq.gtimg.cn/appstock/app/kline/kline?param=" + code + L",m30,," + std::to_wstring(datalen);
		CString strHeaders = _T("Referer: https://finance.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("\"m30\"") != std::string::npos)
		{
			return true;
		}
	}

	// 2. 一级保底：新浪30分钟K
	{
		std::wstring url{ L"https://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData?" };
		std::vector<std::wstring> params;
		params.push_back(L"symbol=" + code);
		params.push_back(L"scale=30");
		params.push_back(L"ma=no");
		params.push_back(L"datalen=" + std::to_wstring(datalen));
		url += CCommon::vectorJoinString(params, L"&");

		CString strHeaders = _T("Referer: http://finance.sina.com.cn");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}

	// 3. 二级保底：东方财富30分钟K
	std::wstring secId = GetEastMoneySecId(code);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring emUrl = L"https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=" + secId + L"&klt=30&fqt=1&lmt=" + std::to_wstring(datalen) + L"&fields1=f1,f2,f3,f4,f5,f6&fields2=f51,f52,f53,f54,f55,f56";
			CString emHeaders = _T("Referer: https://quote.eastmoney.com");
			if (CCommon::GetURL(emUrl, outResp, true, WEB_USERAGENT, emHeaders, emHeaders.GetLength()) && !outResp.empty() && outResp.find("\"klines\"") != std::string::npos)
			{
				return true;
			}
		}
		catch (...) {}
	}

	return false;
}

bool CStockHttpFetcher::FetchFundIOPV(const std::wstring& stock_id, std::string& outResp)
{
	// 仅对ETF基金代码获取IOPV
	if (!CCommon::IsFundCode(stock_id))
		return false;

	// 提取纯数字代码（sh513770 -> 513770）
	std::wstring pureCode = stock_id;
	if (pureCode.size() >= 8 && iswalpha(pureCode[0]) && iswalpha(pureCode[1]))
		pureCode = pureCode.substr(2);

	std::wstring url;
	CString strHeaders;

	if (stock_id.find(L"sh") == 0)
	{
		// 1. 上交所ETF：yunhq.sse.com.cn接口，含真实IOPV
		time_t now = time(nullptr);
		url = L"https://yunhq.sse.com.cn:32042/v1/sh1/snap/" + pureCode
			+ L"?callback=jQuery&select=name,last,chg_rate,change,open,prev_close,high,low,volume,amount,iopv&_="
			+ std::to_wstring(now);
		strHeaders = _T("Referer: https://etf.sse.com.cn");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}

		// 2. 保底：天天基金
		url = L"http://fundgz.1234567.com.cn/js/" + pureCode + L".js?_=" + std::to_wstring(now);
		strHeaders = _T("Referer: http://fund.eastmoney.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}
	else
	{
		// 1. 深交所ETF：天天基金实时估值接口（JSONP格式）
		time_t now = time(nullptr);
		url = L"http://fundgz.1234567.com.cn/js/" + pureCode + L".js?_=" + std::to_wstring(now);
		strHeaders = _T("Referer: http://fund.eastmoney.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}

	return false;
}

bool CStockHttpFetcher::FetchStockBasicCirculating(const std::wstring& stock_id, STOCK::Volume& outShares)
{
	outShares = 0;

	// 1. 首选：东方财富 f85 字段（真实流通A股）
	std::wstring secId = GetEastMoneySecId(stock_id);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring url{ L"https://push2.eastmoney.com/api/qt/stock/get?" };
			std::vector<std::wstring> params;
			params.push_back(L"secid=" + secId);
			params.push_back(L"fields=f43,f44,f45,f46,f47,f48,f49,f50,f51,f57,f58,f60,f85,f116,f117");
			url += CCommon::vectorJoinString(params, L"&");

			CString strHeaders = _T("Referer: https://quote.eastmoney.com");
			std::string response;
			bool fetch_ok = CCommon::GetURL(url, response, true, WEB_USERAGENT, strHeaders, strHeaders.GetLength());
			if (!fetch_ok)
			{
				// 东方财富请求失败：缓存失败状态 10 分钟，避免反复尝试
				m_eastmoney_fail_until = time(nullptr) + 600;
			}
			else if (!response.empty())
			{
				yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
				if (doc != nullptr)
				{
					STOCK::Volume circulatingAShares = 0;
					yyjson_val* root = yyjson_doc_get_root(doc);
					yyjson_val* data = root ? yyjson_obj_get(root, "data") : nullptr;
					if (data != nullptr)
					{
						circulatingAShares = static_cast<STOCK::Volume>(GetJsonDoubleValue(yyjson_obj_get(data, "f85")));
					}
					yyjson_doc_free(doc);

					if (circulatingAShares > 0)
					{
						outShares = circulatingAShares;
						return true;
					}
				}
			}
		}
		catch (CInternetException* e)
		{
			e->Delete();
			m_eastmoney_fail_until = time(nullptr) + 600;
		}
		catch (...)
		{
			m_eastmoney_fail_until = time(nullptr) + 600;
		}
	}

	// 2. 一级保底：腾讯行情接口流通市值换算 (流通股本 = 流通市值 / 现价 * 1e8)
	{
		std::wstring txCode = stock_id;
		if (txCode.find(kHK) == 0)
			txCode = L"r_" + txCode.substr(2);

		std::wstring url = L"http://qt.gtimg.cn/q=" + txCode;
		CString strHeaders = _T("Referer: https://finance.qq.com");
		std::string txResp;
		if (CCommon::GetURL(url, txResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !txResp.empty())
		{
			size_t pos = txResp.find("~");
			if (pos != std::string::npos)
			{
				std::vector<std::string> data_arr = CCommon::split(txResp, '~');
				if (data_arr.size() > 44 && !data_arr[3].empty() && !data_arr[44].empty())
				{
					double curPrice = atof(data_arr[3].c_str());
					double flowMarketValue = atof(data_arr[44].c_str()); // 亿元
					if (curPrice > 0 && flowMarketValue > 0)
					{
						outShares = static_cast<STOCK::Volume>(flowMarketValue / curPrice * 100000000.0);
						if (outShares > 0) return true;
					}
				}
			}
		}
	}

	return false;
}

bool CStockHttpFetcher::FetchChipKLines(const std::wstring& stock_id, STOCK::Volume circulatingAShares,
	std::vector<STOCK::ChipKLinePoint>& outKlines)
{
	outKlines.clear();

	std::vector<STOCK::ChipKLinePoint> klines;

	// 优先用东方财富接口（含换手率字段）
	std::wstring secId = GetEastMoneySecId(stock_id);
	bool skip_eastmoney = (m_eastmoney_fail_until > 0 && time(nullptr) < m_eastmoney_fail_until);
	if (!secId.empty() && !skip_eastmoney)
	{
		try
		{
			std::wstring url{ L"https://push2his.eastmoney.com/api/qt/stock/kline/get?" };
			std::vector<std::wstring> params;
			params.push_back(L"secid=" + secId);
			params.push_back(L"fields1=f1,f2,f3,f4,f5,f6");
			params.push_back(L"fields2=f51,f52,f53,f54,f55,f56,f57,f58,f59,f60,f61");
			params.push_back(L"klt=101");
			params.push_back(L"fqt=1");
			SYSTEMTIME st;
			GetLocalTime(&st);
			wchar_t dateBuf[16];
			swprintf_s(dateBuf, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
			params.push_back(L"end=" + std::wstring(dateBuf));
			params.push_back(L"lmt=750");
			url += CCommon::vectorJoinString(params, L"&");

			CString strHeaders = _T("Referer: https://quote.eastmoney.com");
			std::string response;
			bool fetch_ok = CCommon::GetURL(url, response, true, WEB_USERAGENT, strHeaders, strHeaders.GetLength());
			if (!fetch_ok)
			{
				m_eastmoney_fail_until = time(nullptr) + 600;
			}
			else if (!response.empty())
			{
				yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
				if (doc != nullptr)
				{
					yyjson_val* root = yyjson_doc_get_root(doc);
					yyjson_val* data = yyjson_obj_get(root, "data");
					yyjson_val* klineArr = data ? yyjson_obj_get(data, "klines") : nullptr;
					if (klineArr != nullptr && yyjson_is_arr(klineArr))
					{
						yyjson_val* item;
						yyjson_arr_iter iter;
						yyjson_arr_iter_init(klineArr, &iter);
						while ((item = yyjson_arr_iter_next(&iter)))
						{
							const char* line = yyjson_get_str(item);
							if (line == nullptr) continue;
							std::vector<std::string> values = CCommon::split(line, ',');
							if (values.size() < 11) continue;

							double open = 0.0, close = 0.0, high = 0.0, low = 0.0, volumeHands = 0.0, turnoverRate = 0.0;
							if (!TryParseDouble(values[1], open) || !TryParseDouble(values[2], close) || !TryParseDouble(values[3], high) || !TryParseDouble(values[4], low))
								continue;
							TryParseDouble(values[5], volumeHands);
							TryParseDouble(values[10], turnoverRate);
							if (high <= 0.0 || low <= 0.0 || volumeHands <= 0.0)
								continue;

							STOCK::ChipKLinePoint point;
							point.date = values[0];
							point.open = open;
							point.close = close;
							point.high = high;
							point.low = low;
							point.turnoverRate = turnoverRate;
							point.volume = static_cast<STOCK::Volume>(volumeHands * 100.0);
							klines.push_back(point);
						}
					}
					yyjson_doc_free(doc);
				}
			}
		}
		catch (CInternetException* e)
		{
			e->Delete();
			m_eastmoney_fail_until = time(nullptr) + 600;
		}
		catch (...)
		{
			m_eastmoney_fail_until = time(nullptr) + 600;
		}
	}

	// 2. 一级保底：腾讯前复权日K线（结合流通股本自算换手率）
	if (klines.empty())
	{
		std::string txResp;
		if (FetchDayKLine(stock_id, 750, txResp) && !txResp.empty())
		{
			std::vector<STOCK::KLinePoint> points = STOCK::ParseKLinePointsFromJson(txResp, stock_id, "day");
			for (const auto& kp : points)
			{
				STOCK::ChipKLinePoint point;
				point.date = kp.day;
				point.open = kp.open;
				point.high = kp.high;
				point.low = kp.low;
				point.close = kp.close;
				point.volume = kp.volume;
				if (circulatingAShares > 0 && point.volume > 0)
					point.turnoverRate = static_cast<double>(point.volume) / circulatingAShares * 100.0;
				if (point.high > 0 && point.low > 0 && point.volume > 0)
					klines.push_back(point);
			}
		}
	}

	// 3. 二级保底：新浪日K线接口，换手率自己算
	if (klines.empty())
	{
		try
		{
			std::wstring url{ L"https://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData?" };
			std::vector<std::wstring> params;
			params.push_back(L"symbol=" + stock_id);
			params.push_back(L"scale=240");
			params.push_back(L"ma=no");
			params.push_back(L"datalen=750");
			url += CCommon::vectorJoinString(params, L"&");

			CString strHeaders = _T("Referer: http://finance.sina.com.cn");
			std::string response;
			if (CCommon::GetURL(url, response, true, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !response.empty())
			{
				yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
				if (doc != nullptr)
				{
					yyjson_val* root = yyjson_doc_get_root(doc);
					if (root != nullptr && yyjson_is_arr(root))
					{
						auto getDouble = [](yyjson_val* obj, const char* key) -> double {
							yyjson_val* val = yyjson_obj_get(obj, key);
							if (val == nullptr) return 0.0;
							if (yyjson_is_real(val)) return yyjson_get_real(val);
							if (yyjson_is_int(val)) return static_cast<double>(yyjson_get_int(val));
							if (yyjson_is_str(val)) return atof(yyjson_get_str(val));
							return 0.0;
						};

						yyjson_val* item;
						yyjson_arr_iter iter;
						yyjson_arr_iter_init(root, &iter);
						while ((item = yyjson_arr_iter_next(&iter)))
						{
							if (item == nullptr || !yyjson_is_obj(item))
								continue;

							STOCK::ChipKLinePoint point;
							point.date = utilities::JsonHelper::GetJsonString(item, "day");
							point.open = getDouble(item, "open");
							point.high = getDouble(item, "high");
							point.low = getDouble(item, "low");
							point.close = getDouble(item, "close");
							point.volume = static_cast<STOCK::Volume>(getDouble(item, "volume"));

							// 换手率 = 成交量(股) / 流通股本(股) * 100%
							if (circulatingAShares > 0 && point.volume > 0)
								point.turnoverRate = static_cast<double>(point.volume) / circulatingAShares * 100.0;

							if (point.high > 0 && point.low > 0 && point.volume > 0)
								klines.push_back(point);
						}
					}
					yyjson_doc_free(doc);
				}
			}
		}
		catch (CInternetException* e)
		{
			e->Delete();
		}
		catch (...)
		{
		}
	}

	if (klines.empty())
		return false;

	outKlines = std::move(klines);
	return true;
}
