#include "pch.h"
#include "Common.h"
#include <afxinet.h>    //用于支持使用网络相关的类
#include <sstream>
#include "DataManager.h"
#include "NetFetch.h"

std::wstring CCommon::StrToUnicode(const char* str, bool utf8)
{
	if (str == nullptr)
		return std::wstring();
	std::wstring result;
	int size;
	size = MultiByteToWideChar((utf8 ? CP_UTF8 : CP_ACP), 0, str, -1, NULL, 0);
	if (size <= 0) return std::wstring();
	wchar_t* str_unicode = new wchar_t[size + 1];
	MultiByteToWideChar((utf8 ? CP_UTF8 : CP_ACP), 0, str, -1, str_unicode, size);
	result.assign(str_unicode);
	delete[] str_unicode;
	return result;
}

std::string CCommon::UnicodeToStr(const wchar_t* wstr, bool utf8)
{
	if (wstr == nullptr)
		return std::string();
	std::string result;
	int size{ 0 };
	size = WideCharToMultiByte((utf8 ? CP_UTF8 : CP_ACP), 0, wstr, -1, NULL, 0, NULL, NULL);
	if (size <= 0) return std::string();
	char* str = new char[size + 1];
	WideCharToMultiByte((utf8 ? CP_UTF8 : CP_ACP), 0, wstr, -1, str, size, NULL, NULL);
	result.assign(str);
	delete[] str;
	return result;
}

bool CCommon::GetURL(const std::wstring& url, std::string& result, bool utf8, LPCTSTR user_agent, LPCTSTR headers, DWORD dwHeadersLength)
{
	(void)utf8;
	(void)dwHeadersLength;
	// 网络抓取逻辑统一封装在 CNetFetch 中（自动选择直连 / SOCKS5 代理）
	return CNetFetch::GetURL(url, result, user_agent, headers);
}

void CCommon::WriteLog(const WORD w, LPCTSTR file_path)
{
	char buff[32];
	sprintf_s(buff, "%d", w);
	CCommon::WriteLog(buff, file_path);
}

void CCommon::WriteLog(const char* str_text, LPCTSTR file_path)
{
	static std::string last_text;
	//过滤相同内容的日志
	if (last_text != str_text)
	{
		SYSTEMTIME cur_time;
		GetLocalTime(&cur_time);
		char buff[32];
		sprintf_s(buff, "%d/%.2d/%.2d %.2d:%.2d:%.2d.%.3d: ", cur_time.wYear, cur_time.wMonth, cur_time.wDay,
			cur_time.wHour, cur_time.wMinute, cur_time.wSecond, cur_time.wMilliseconds);
		std::ofstream file{ file_path, std::ios::app };  //以追加的方式打开日志文件
		file << buff;
		file << str_text << std::endl;

		last_text = str_text;
	}
}

void CCommon::WriteLog(const wchar_t* str_text, LPCTSTR file_path)
{
	WriteLog(UnicodeToStr(str_text, true).c_str(), file_path);
}

std::vector<std::string> CCommon::split(const std::string& str, const char pattern)
{
	std::vector<std::string> res;
	if (str.size() <= 0) {
		return res;
	}
	if (str.find(pattern) == -1) {
		res.push_back(str);
		return res;
	}
	std::stringstream input(str);   //读取str到字符串流中
	std::string temp;
	//使用getline函数从字符串流中读取,遇到分隔符时停止,和从cin中读取类似
	//注意,getline默认是可以读取空格的
	int len = 0;
	while (getline(input, temp, pattern))
	{
		res.push_back(temp);
		len++;
	}
	res.resize(len);
	return res;
}

std::vector<std::string> CCommon::split(const std::string& str, const std::string& delimiter) {
	std::vector<std::string> tokens;

	if (delimiter.empty()) {
		tokens.push_back(str);
		return tokens;
	}

	size_t pos = 0;
	size_t prev = 0;

	while ((pos = str.find(delimiter, prev)) != std::string::npos) {
		tokens.push_back(str.substr(prev, pos - prev));
		prev = pos + delimiter.length();
	}

	// 添加最后一个片段
	tokens.push_back(str.substr(prev));

	return tokens;
}

std::wstring CCommon::vectorJoinString(const std::vector<std::wstring> data, const std::wstring& pattern)
{
	std::wstring str{};
	for (size_t index = 0; index < data.size(); index++)
	{
		if (index > 0)
			str.append(pattern);
		str.append(data[index]);
	}
	return str;
}

std::string CCommon::removeChar(const std::string& str, char ch)
{
	std::string result;
	for (char c : str)
	{
		if (c != ch)
		{
			result += c;
		}
	}
	return result;
}

std::string CCommon::removeStr(const std::string str, const std::string del)
{
	std::string result;

	if (del.empty()) {
		return str;
	}

	size_t pos = 0;
	size_t prev = 0;

	while ((pos = str.find(del, prev)) != std::string::npos) {
		result += str.substr(prev, pos - prev);
		prev = pos + del.length();
	}

	result += str.substr(prev);

	return result;
}

CString CCommon::FormatFloat(double value)
{
	CString str;
	str.Format(_T("%.3f"), value);

	if (str.Right(1) == _T("0"))
	{
		str = str.Left(str.GetLength() - 1);
	}

	return str;
}

CString CCommon::FormatETFPrice(double value)
{
	CString str;
	str.Format(_T("%.3f"), value);

	return str;
}

CString CCommon::FormatNumber(double value, int maxDecimals)
{
	CString str;
	if (maxDecimals <= 0)
	{
		str.Format(_T("%lld"), static_cast<long long>(value));
		return str;
	}

	TCHAR format[32];
	wsprintf(format, _T("%%.%df"), maxDecimals);
	str.Format(format, value);

	int dotPos = str.Find(_T('.'));
	if (dotPos != -1)
	{
		int lastNonZero = str.GetLength() - 1;
		while (lastNonZero > dotPos && str[lastNonZero] == _T('0'))
		{
			lastNonZero--;
		}

		if (lastNonZero == dotPos)
		{
			str = str.Left(dotPos);
		}
		else
		{
			str = str.Left(lastNonZero + 1);
		}
	}

	return str;
}

CString CCommon::FormatAmount(double value)
{
	CString str;
	if (value >= 100000000)
	{
		str = FormatNumber(value / 100000000.0, 2) + _T("亿");
	}
	else if (value >= 10000)
	{
		str = FormatNumber(value / 10000.0, 2) + _T("万");
	}
	else
	{
		str = FormatNumber(value, 2);
	}
	return str;
}

CString CCommon::FormatVolume(double value)
{
	CString str;
	if (value >= 10000)
	{
		str.Format(_T("%.2f万"), value / 10000.0);
	}
	else
	{
		str.Format(_T("%.0f"), value);
	}
	return str;
}

CString CCommon::FormatVolumeInt(double value)
{
	CString str;
	if (value >= 10000)
	{
		str = FormatNumber(value / 10000.0, 2) + _T("万");
	}
	else
	{
		str.Format(_T("%lld"), static_cast<long long>(value));
	}
	return str;
}

CString CCommon::FormatProfitLoss(double percent, double amount, bool showPercentFirst)
{
	CString str;
	if (showPercentFirst)
	{
		if (percent >= 0)
			str.Format(_T("+%.2f%%(+%g)"), percent, amount);
		else
			str.Format(_T("%.2f%%(%g)"), percent, amount);
	}
	else
	{
		if (amount >= 0)
			str.Format(_T("+%g(+%.2f%%)"), amount, percent);
		else
			str.Format(_T("%g(%.2f%%)"), amount, percent);
	}
	return str;
}

CString CCommon::FormatSignedValue(double value, const CString& format)
{
	CString str;
	if (value >= 0)
	{
		CString tmp;
		tmp.Format(format, value);
		str.Format(_T("+%s"), tmp.GetString());
	}
	else
	{
		str.Format(format, value);
	}
	return str;
}

bool CCommon::IsAGStockCode(const std::wstring& code)
{
	return code.find(L"sh") == 0 || code.find(L"sz") == 0 || code.find(L"bj") == 0;
}

bool CCommon::IsFundCode(const std::wstring& code)
{
	std::wstring pureCode = code;
	if (pureCode.size() >= 8 && iswalpha(pureCode[0]) && iswalpha(pureCode[1]))
		pureCode = pureCode.substr(2);
	if (pureCode.length() < 2)
		return false;

	std::wstring first2 = pureCode.substr(0, 2);
	const std::vector<std::wstring> fundPrefixes = { L"50", L"51", L"56", L"15", L"16", L"18" };
	for (const auto& prefix : fundPrefixes)
	{
		if (first2 == prefix)
			return true;
	}
	return false;
}

COLORREF CCommon::GetProfitLossColor(double percent)
{
	const COLORREF COLOR_LIGHT_RED = RGB(179, 64, 65);      // 浅红色 0~3%
	const COLORREF COLOR_DEEP_RED = RGB(160, 30, 30);      // 深红色 3~6%
	const COLORREF COLOR_PURPLE = RGB(160, 50, 160);       // 紫色 6~10%
	const COLORREF COLOR_LIGHT_GREEN = RGB(44, 144, 51);   // 浅绿色 -3%~0
	const COLORREF COLOR_DEEP_GREEN = RGB(20, 100, 40);    // 深绿色 -6%~-3%
	const COLORREF COLOR_DARK_GREEN = RGB(0, 60, 20);      // 墨绿色 -10%~-6%

	if (percent >= 6.66)
		return COLOR_PURPLE;
	else if (percent >= 3.36)
		return COLOR_DEEP_RED;
	else if (percent > 0)
		return COLOR_LIGHT_RED;
	else if (percent == 0)
		return RGB(0, 0, 0);
	else if (percent > -3.33)
		return COLOR_LIGHT_GREEN;
	else if (percent > -6.66)
		return COLOR_DEEP_GREEN;
	else
		return COLOR_DARK_GREEN;
}

bool CCommon::IsMarketSession()
{
	SYSTEMTIME now;
	GetLocalTime(&now);
	// 周六日休市
	if (now.wDayOfWeek == 0 || now.wDayOfWeek == 6)
		return false;
	// A股交易时间：9:30-11:30, 13:00-15:00
	int minutes = now.wHour * 60 + now.wMinute;
	if (minutes < 9 * 60 + 30)          // 9:30之前
		return false;
	if (minutes > 11 * 60 + 30 && minutes < 13 * 60)  // 11:30-13:00午休
		return false;
	if (minutes > 15 * 60)              // 15:00之后
		return false;
	return true;
}

bool CCommon::IsCallAuctionSession()
{
	SYSTEMTIME now;
	GetLocalTime(&now);
	// 周六日休市
	if (now.wDayOfWeek == 0 || now.wDayOfWeek == 6)
		return false;
	// 集合竞价时段：9:15-9:30
	int minutes = now.wHour * 60 + now.wMinute;
	if (minutes < 9 * 60 + 15)          // 9:15之前
		return false;
	if (minutes >= 9 * 60 + 30)         // 9:30及之后（竞价结束）
		return false;
	return true;
}

int CCommon::GetTradingMinute(int hour, int minute)
{
	int totalMinutes = hour * 60 + minute;
	if (totalMinutes < 9 * 60 + 30)
		return -1;
	if (totalMinutes <= 11 * 60 + 30)
		return totalMinutes - (9 * 60 + 30);
	if (totalMinutes < 13 * 60)
		return -1;  // 午休期间不采样
	if (totalMinutes <= 15 * 60)
		return 120 + (totalMinutes - 13 * 60);
	return -1;
}

int CCommon::GetTradingMinute(time_t t)
{
	std::tm tm = {};
	localtime_s(&tm, &t);
	return GetTradingMinute(tm.tm_hour, tm.tm_min);
}