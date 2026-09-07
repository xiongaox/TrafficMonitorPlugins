#include "pch.h"
#include "StockHttpFetcher.h"
#include "Common.h"
#include "Stock.h"
#include "ApiHealthManager.h"
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
	std::vector<std::wstring> missingCodes;
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
		DWORD t0 = GetTickCount();
		bool txOk = CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && outResp.find("v_") != std::string::npos;
		DWORD latencyTx = GetTickCount() - t0;
		static DWORD s_lastRecordTx = 0;
		if (GetTickCount() - s_lastRecordTx > 10000)
		{
			s_lastRecordTx = GetTickCount();
			if (txOk)
				CApiHealthManager::Instance().RecordPoint(API_TENCENT, latencyTx, 200, latencyTx > 250 ? LEVEL_WARN : LEVEL_OK, L"实时行情 (腾讯) 获取正常");
			else
				CApiHealthManager::Instance().RecordPoint(API_TENCENT, latencyTx, 0, LEVEL_FAIL, L"实时行情 (腾讯) 响应异常");
		}
		if (txOk)
		{
			// 检查腾讯未能返回数据的代码（例如 r_hkHSHCI, r_hkHSHBI 等）
			for (const auto& code : outCodes)
			{
				std::string codeA = CCommon::UnicodeToStr(code.c_str());
				std::string txKey = "v_" + codeA;
				if (code.find(kHK) == 0)
					txKey = "v_r_" + codeA.substr(2);
				if (outResp.find(txKey) == std::string::npos)
				{
					missingCodes.push_back(code);
				}
			}
		}
		else
		{
			missingCodes = outCodes;
		}
	}

	// 2. 对腾讯未返回的代码（或腾讯失败时），从新浪财经 (hq.sinajs.cn) 补充获取
	if (!missingCodes.empty())
	{
		std::wstring url{ L"https://hq.sinajs.cn/?" };
		std::vector<std::wstring> params;
		params.push_back(L"_=" + std::to_wstring(generateRandomDouble()));
		params.push_back(L"list=" + CCommon::vectorJoinString(missingCodes, L","));
		url += CCommon::vectorJoinString(params, L"&");

		CString strHeaders = _T("Referer: https://finance.sina.com.cn");
		std::string sinaResp;
		DWORD t0 = GetTickCount();
		bool sinaOk = CCommon::GetURL(url, sinaResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !sinaResp.empty() && sinaResp.find("hq_str_") != std::string::npos;
		DWORD latencySina = GetTickCount() - t0;
		static DWORD s_lastRecordSina = 0;
		if (GetTickCount() - s_lastRecordSina > 10000)
		{
			s_lastRecordSina = GetTickCount();
			if (sinaOk)
				CApiHealthManager::Instance().RecordPoint(API_SINA, latencySina, 200, LEVEL_OK, L"实时行情 (新浪兜底) 获取正常");
			else
				CApiHealthManager::Instance().RecordPoint(API_SINA, latencySina, 0, LEVEL_FAIL, L"实时行情 (新浪兜底) 响应异常");
		}
		if (sinaOk)
		{
			if (!outResp.empty())
				outResp += "\n" + sinaResp;
			else
				outResp = sinaResp;
		}
	}

	return !outResp.empty();
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
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty() && 
			(outResp.find("\"data\"") != std::string::npos || outResp.find("\"minute\"") != std::string::npos))
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

	// 注：不使用新浪日K接口作回退——其数据为不复权口径，在基金份额折算/除权日会形成
	// 巨幅断崖，与前复权主源混用会导致K线出现虚假暴跌（ApplyDayKLine 另有口径校验兜底）。
	// 主源与保底均失败时返回 false，由调用方保持内存现有数据不变。
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
	time_t now = time(nullptr);

	if (stock_id.find(L"sh") == 0)
	{
		// 1. 上交所ETF：yunhq.sse.com.cn接口，含真实IOPV
		url = L"https://yunhq.sse.com.cn:32042/v1/sh1/snap/" + pureCode
			+ L"?callback=jQuery&select=name,last,chg_rate,change,open,prev_close,high,low,volume,amount,iopv&_="
			+ std::to_wstring(now);
		strHeaders = _T("Referer: https://etf.sse.com.cn");
		DWORD t0 = GetTickCount();
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			DWORD ms = GetTickCount() - t0;
			static DWORD s_lastRecordIopv = 0;
			if (GetTickCount() - s_lastRecordIopv > 10000)
			{
				s_lastRecordIopv = GetTickCount();
				CApiHealthManager::Instance().RecordPoint(API_FUND_IOPV, ms, 200, LEVEL_OK, L"上交所 ETF IOPV 获取正常");
			}
			return true;
		}

		// 2. 一级保底：天天基金
		url = L"http://fundgz.1234567.com.cn/js/" + pureCode + L".js?_=" + std::to_wstring(now);
		strHeaders = _T("Referer: http://fund.eastmoney.com");
		t0 = GetTickCount();
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			DWORD ms = GetTickCount() - t0;
			static DWORD s_lastRecordIopv = 0;
			if (GetTickCount() - s_lastRecordIopv > 10000)
			{
				s_lastRecordIopv = GetTickCount();
				CApiHealthManager::Instance().RecordPoint(API_FUND_IOPV, ms, 200, LEVEL_OK, L"天天基金估值获取正常");
			}
			return true;
		}

		// 3. 二级保底：腾讯ETF分时/IOPV
		url = L"https://web.ifzq.gtimg.cn/appstock/app/Minute/query?code=" + stock_id;
		strHeaders = _T("Referer: https://gu.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}
	else
	{
		// 1. 深交所ETF：天天基金实时估值接口（JSONP格式）
		url = L"http://fundgz.1234567.com.cn/js/" + pureCode + L".js?_=" + std::to_wstring(now);
		strHeaders = _T("Referer: http://fund.eastmoney.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}

		// 2. 一级保底：腾讯ETF分时/IOPV
		url = L"https://web.ifzq.gtimg.cn/appstock/app/Minute/query?code=" + stock_id;
		strHeaders = _T("Referer: https://gu.qq.com");
		if (CCommon::GetURL(url, outResp, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !outResp.empty())
		{
			return true;
		}
	}

	return false;
}

bool CStockHttpFetcher::FetchEtfHoldings(const std::wstring& stock_id, STOCK::EtfHoldingsData& outData)
{
	outData.Clear();
	if (!CCommon::IsFundCode(stock_id))
		return false;

	std::wstring pureCode = CCommon::GetPureCode(stock_id);
	if (pureCode.empty())
		return false;

	outData.etfCode = stock_id;

	CString strHeaders = _T("Referer: https://quote.eastmoney.com/");
	std::string response;

	// 1. 获取 ETF 跟踪的指数代码及指数名称
	std::wstring selectorUrl = L"https://datacenter.eastmoney.com/stock/etfselector/api/data/get?type=RPTA_APP_ETFSELECT&sty=DERIVE_INDEX_CODE,INDEXNAME,SECURITY_CODE&source=SECURITIES&client=APP&filter=(SECURITY_CODE%3D%22" + pureCode + L"%22)&p=1&ps=1";

	std::wstring deriveIndexCode;
	std::wstring indexName;
	std::vector<std::wstring> componentCodes;
	std::map<std::wstring, double> directRatios;
	std::map<std::wstring, std::wstring> directNames;

	if (CCommon::GetURL(selectorUrl, response, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !response.empty())
	{
		yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
		if (doc)
		{
			yyjson_val* root = yyjson_doc_get_root(doc);
			yyjson_val* res = yyjson_obj_get(root, "result");
			yyjson_val* data = yyjson_obj_get(res, "data");
			if (yyjson_is_arr(data) && yyjson_arr_size(data) > 0)
			{
				yyjson_val* item = yyjson_arr_get(data, 0);
				const char* idxCodeStr = yyjson_get_str(yyjson_obj_get(item, "DERIVE_INDEX_CODE"));
				const char* idxNameStr = yyjson_get_str(yyjson_obj_get(item, "INDEXNAME"));
				if (idxCodeStr)
				{
					std::string s = idxCodeStr;
					size_t dotPos = s.find('.');
					if (dotPos != std::string::npos) s = s.substr(0, dotPos);
					deriveIndexCode = CCommon::StrToUnicode(s.c_str(), true);
				}
				if (idxNameStr)
				{
					indexName = CCommon::StrToUnicode(idxNameStr, true);
				}
			}
			yyjson_doc_free(doc);
		}
	}

	outData.indexCode = deriveIndexCode;
	outData.indexName = indexName;

	// 2. 若获取到跟踪指数，查询成分股列表
	if (!deriveIndexCode.empty())
	{
		std::wstring compUrl = L"https://datacenter-web.eastmoney.com/api/data/v1/get?reportName=RPT_INDEX_COMPONENT&columns=ALL&filter=(INDEX_CODE%3D%22" + deriveIndexCode + L"%22)&pageNumber=1&pageSize=100&source=WEB&client=WEB";
		if (CCommon::GetURL(compUrl, response, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !response.empty())
		{
			yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
			if (doc)
			{
				yyjson_val* root = yyjson_doc_get_root(doc);
				yyjson_val* res = yyjson_obj_get(root, "result");
				yyjson_val* data = yyjson_obj_get(res, "data");
				if (yyjson_is_arr(data))
				{
					size_t count = yyjson_arr_size(data);
					for (size_t i = 0; i < count; ++i)
					{
						yyjson_val* item = yyjson_arr_get(data, i);
						const char* secCode = yyjson_get_str(yyjson_obj_get(item, "SECURITY_CODE"));
						const char* secName = yyjson_get_str(yyjson_obj_get(item, "SECURITY_NAME_ABBR"));
						if (secCode && strlen(secCode) > 0)
						{
							std::wstring wCode = CCommon::StrToUnicode(secCode, true);
							componentCodes.push_back(wCode);
							if (secName) directNames[wCode] = CCommon::StrToUnicode(secName, true);
						}
					}
				}
				yyjson_doc_free(doc);
			}
		}
	}

	// 3. 回退保底：若成分股为空（如国证/深交所指数399365等），从天天基金季度公开持仓获取
	if (componentCodes.empty())
	{
		std::wstring f10Url = L"https://fundmobapi.eastmoney.com/FundMNewApi/FundMNInverstPosition?FCODE=" + pureCode + L"&deviceid=1&plat=Iphone&appType=ttjj&product=EFund&Version=1";
		if (CCommon::GetURL(f10Url, response, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !response.empty())
		{
			yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
			if (doc)
			{
				yyjson_val* root = yyjson_doc_get_root(doc);
				yyjson_val* datas = yyjson_obj_get(root, "Datas");
				yyjson_val* fundStocks = yyjson_obj_get(datas, "fundStocks");
				if (yyjson_is_arr(fundStocks))
				{
					size_t count = yyjson_arr_size(fundStocks);
					for (size_t i = 0; i < count; ++i)
					{
						yyjson_val* item = yyjson_arr_get(fundStocks, i);
						const char* gpdm = yyjson_get_str(yyjson_obj_get(item, "GPDM"));
						const char* gpjc = yyjson_get_str(yyjson_obj_get(item, "GPJC"));
						const char* jzblStr = yyjson_get_str(yyjson_obj_get(item, "JZBL"));
						if (gpdm && strlen(gpdm) > 0)
						{
							std::wstring wCode = CCommon::StrToUnicode(gpdm, true);
							componentCodes.push_back(wCode);
							if (gpjc) directNames[wCode] = CCommon::StrToUnicode(gpjc, true);
							if (jzblStr) directRatios[wCode] = atof(jzblStr);
						}
					}
				}
				yyjson_doc_free(doc);
			}
		}
	}

	if (componentCodes.empty())
		return false;

	// 4. 批量获取成分股实时行情（优先腾讯 qt.gtimg.cn 毫秒级接口，永不断连）
	std::vector<std::wstring> secids;
	for (const auto& code : componentCodes)
	{
		if (code.empty()) continue;
		if (code[0] == L'6' || code[0] == L'5' || code[0] == L'9')
			secids.push_back(L"sh" + code);
		else
			secids.push_back(L"sz" + code);
	}

	std::wstring txUrl = L"http://qt.gtimg.cn/q=" + CCommon::vectorJoinString(secids, L",");
	std::vector<STOCK::EtfHoldingItem> items;
	double totalMarketCapSum = 0.0;

	if (CCommon::GetURL(txUrl, response, false) && !response.empty() && response.find("v_") != std::string::npos)
	{
		std::vector<std::string> lines = CCommon::split(response, ';');
		for (const auto& line : lines)
		{
			std::vector<std::string> parts = CCommon::split(line, '~');
			if (parts.size() > 45)
			{
				STOCK::EtfHoldingItem holding;
				holding.code = CCommon::StrToUnicode(parts[2].c_str(), false);
				holding.name = CCommon::StrToUnicode(parts[1].c_str(), false);
				holding.price = atof(parts[3].c_str());
				holding.changePercent = atof(parts[32].c_str());
				holding.volume = atof(parts[37].c_str()) * 10000.0;       // 成交额(元)
				holding.totalMarketCap = atof(parts[45].c_str()) * 1e8;   // 总市值(元)

				if (!holding.code.empty())
				{
					if (holding.code[0] == L'6' || holding.code[0] == L'5' || holding.code[0] == L'9')
						holding.fullCode = L"sh" + holding.code;
					else
						holding.fullCode = L"sz" + holding.code;
				}

				if (directRatios.find(holding.code) != directRatios.end())
				{
					holding.ratio = directRatios[holding.code];
				}

				totalMarketCapSum += holding.totalMarketCap;
				items.push_back(holding);
			}
		}
	}

	// 若腾讯接口未返回数据，回退到东方财富 push2delay 接口
	if (items.empty())
	{
		std::vector<std::wstring> emSecids;
		for (const auto& code : componentCodes)
		{
			if (code.empty()) continue;
			if (code[0] == L'6' || code[0] == L'5' || code[0] == L'9')
				emSecids.push_back(L"1." + code);
			else
				emSecids.push_back(L"0." + code);
		}
		std::wstring quoteUrl = L"https://push2delay.eastmoney.com/api/qt/ulist.np/get?fltt=2&fields=f12,f13,f14,f2,f3,f6,f20,f25&secids=" + CCommon::vectorJoinString(emSecids, L",");
		if (CCommon::GetURL(quoteUrl, response, false, WEB_USERAGENT, strHeaders, strHeaders.GetLength()) && !response.empty())
		{
			yyjson_doc* doc = yyjson_read(response.c_str(), response.size(), 0);
			if (doc)
			{
				yyjson_val* root = yyjson_doc_get_root(doc);
				yyjson_val* dataVal = yyjson_obj_get(root, "data");
				yyjson_val* diffVal = yyjson_obj_get(dataVal, "diff");
				if (yyjson_is_arr(diffVal))
				{
					size_t diffCount = yyjson_arr_size(diffVal);
					for (size_t i = 0; i < diffCount; ++i)
					{
						yyjson_val* item = yyjson_arr_get(diffVal, i);
						STOCK::EtfHoldingItem holding;
						const char* codeStr = yyjson_get_str(yyjson_obj_get(item, "f12"));
						if (codeStr) holding.code = CCommon::StrToUnicode(codeStr, true);
						const char* nameStr = yyjson_get_str(yyjson_obj_get(item, "f14"));
						if (nameStr) holding.name = CCommon::StrToUnicode(nameStr, true);
						holding.price = GetJsonDoubleValue(yyjson_obj_get(item, "f2"));
						holding.changePercent = GetJsonDoubleValue(yyjson_obj_get(item, "f3"));
						holding.volume = GetJsonDoubleValue(yyjson_obj_get(item, "f6"));
						holding.totalMarketCap = GetJsonDoubleValue(yyjson_obj_get(item, "f20"));
						int f13 = (int)GetJsonDoubleValue(yyjson_obj_get(item, "f13"));
						if (f13 == 1) holding.fullCode = L"sh" + holding.code;
						else if (f13 == 0) holding.fullCode = L"sz" + holding.code;
						else if (!holding.code.empty()) holding.fullCode = (holding.code[0] == L'6' ? L"sh" : L"sz") + holding.code;

						if (directRatios.find(holding.code) != directRatios.end())
							holding.ratio = directRatios[holding.code];

						totalMarketCapSum += holding.totalMarketCap;
						items.push_back(holding);
					}
				}
				yyjson_doc_free(doc);
			}
		}
	}

	if (items.empty())
		return false;

	// 若未直接提供仓位比（如指数全量成分股），按总市值权重计算
	bool hasDirectRatio = !directRatios.empty();
	if (!hasDirectRatio && totalMarketCapSum > 0.0)
	{
		for (auto& item : items)
		{
			item.ratio = (item.totalMarketCap / totalMarketCapSum) * 100.0;
		}
	}

	// 按仓位/市值降序排序
	std::sort(items.begin(), items.end(), [](const STOCK::EtfHoldingItem& a, const STOCK::EtfHoldingItem& b) {
		if (std::abs(a.ratio - b.ratio) > 0.001)
			return a.ratio > b.ratio;
		return a.totalMarketCap > b.totalMarketCap;
	});

	// 设置序号
	for (size_t i = 0; i < items.size(); ++i)
	{
		items[i].rank = static_cast<int>(i + 1);
	}

	outData.items = std::move(items);
	outData.lastUpdateTime = time(nullptr);
	outData.isValid = true;
	outData.fetchFailed = false;
	return true;
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
			DWORD t0 = GetTickCount();
			bool fetch_ok = CCommon::GetURL(url, response, true, WEB_USERAGENT, strHeaders, strHeaders.GetLength());
			DWORD latency = GetTickCount() - t0;
			if (!fetch_ok)
			{
				// 东方财富请求失败：缓存失败状态 10 分钟，避免反复尝试
				m_eastmoney_fail_until = time(nullptr) + 600;
				CApiHealthManager::Instance().RecordPoint(API_EASTMONEY, latency, 403, LEVEL_FAIL, L"请求失败 / 疑似被 WAF 拦截 (休眠10分钟)");
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
						static DWORD s_lastRecordEm = 0;
						if (GetTickCount() - s_lastRecordEm > 10000)
						{
							s_lastRecordEm = GetTickCount();
							CApiHealthManager::Instance().RecordPoint(API_EASTMONEY, latency, 200, LEVEL_OK, L"流通股本 (f85) 获取成功");
						}
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
