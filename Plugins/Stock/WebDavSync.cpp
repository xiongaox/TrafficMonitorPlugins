#include "pch.h"
#include "WebDavSync.h"
#include "DataManager.h"
#include "Common.h"
#include "Stock.h"
#include <afxinet.h>
#include <wincrypt.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cctype>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Wininet.lib")

namespace
{
	// 大小写不敏感地在 [from, end) 范围内查找 ASCII 子串，用于解析各服务器
	// 大小写/前缀风格不一的 PROPFIND 207 响应（D:href / d:href / href 等）
	size_t FindIC(const std::string& text, size_t from, const char* needle)
	{
		size_t n = strlen(needle);
		if (n == 0 || text.size() < n)
			return std::string::npos;
		for (size_t i = from; i + n <= text.size(); ++i)
		{
			size_t j = 0;
			while (j < n && tolower((unsigned char)text[i + j]) == tolower((unsigned char)needle[j]))
				++j;
			if (j == n)
				return i;
		}
		return std::string::npos;
	}

	// 取 [from, to) 区间内 <前缀:tag>content</前缀:tag> 的 content，兼容带/不带命名空间前缀
	std::string ExtractTagContent(const std::string& text, size_t from, size_t to, const char* tag)
	{
		std::string pat1 = std::string(":") + tag + ">";
		std::string pat2 = std::string("<") + tag + ">";
		size_t valBegin = std::string::npos;
		for (const std::string* pat : { &pat1, &pat2 })
		{
			size_t p = FindIC(text, from, pat->c_str());
			if (p != std::string::npos && p < to)
			{
				valBegin = p + pat->size();
				break;
			}
		}
		if (valBegin == std::string::npos || valBegin >= to)
			return "";
		size_t end = FindIC(text, valBegin, "</");
		if (end == std::string::npos || end > to)
			return "";
		return text.substr(valBegin, end - valBegin);
	}

	// 百分号解码后按 UTF-8 转宽字符（我们生成的文件名全为 ASCII，此处仅兜底）
	std::wstring UrlDecodeFileName(const std::string& encoded)
	{
		std::string out;
		for (size_t i = 0; i < encoded.size(); ++i)
		{
			if (encoded[i] == '%' && i + 2 < encoded.size() &&
				isxdigit((unsigned char)encoded[i + 1]) && isxdigit((unsigned char)encoded[i + 2]))
			{
				auto hexVal = [](char c) { return isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10); };
				out += static_cast<char>(hexVal(encoded[i + 1]) * 16 + hexVal(encoded[i + 2]));
				i += 2;
			}
			else
			{
				out += encoded[i];
			}
		}
		return CCommon::StrToUnicode(out.c_str(), true);
	}

	CString FormatBackupSize(unsigned long long bytes)
	{
		wchar_t buf[32]{};
		if (bytes >= 1024ULL * 1024ULL)
			swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024.0));
		else if (bytes >= 1024ULL)
			swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
		else
			swprintf_s(buf, L"%llu B", bytes);
		return buf;
	}
}

std::string CWebDavSync::Base64Encode(const std::string& input)
{
	if (input.empty())
		return "";

	DWORD outLen = 0;
	if (!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(input.data()), static_cast<DWORD>(input.size()),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &outLen))
	{
		return "";
	}

	std::string output(outLen, '\0');
	if (CryptBinaryToStringA(reinterpret_cast<const BYTE*>(input.data()), static_cast<DWORD>(input.size()),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &output[0], &outLen))
	{
		if (!output.empty() && output.back() == '\0')
			output.pop_back();
		return output;
	}
	return "";
}

bool CWebDavSync::ParseURL(const std::wstring& url, bool& isHttps, std::wstring& host, int& port, std::wstring& path)
{
	isHttps = false;
	port = 80;
	host.clear();
	path = L"/";

	std::wstring rest;
	if (url.find(L"https://") == 0)
	{
		isHttps = true;
		port = 443;
		rest = url.substr(8);
	}
	else if (url.find(L"http://") == 0)
	{
		isHttps = false;
		port = 80;
		rest = url.substr(7);
	}
	else
	{
		return false;
	}

	size_t slashPos = rest.find(L'/');
	std::wstring hostPort = (slashPos != std::wstring::npos) ? rest.substr(0, slashPos) : rest;
	if (slashPos != std::wstring::npos)
		path = rest.substr(slashPos);
	else
		path = L"/";

	size_t colonPos = hostPort.find(L':');
	if (colonPos != std::wstring::npos)
	{
		host = hostPort.substr(0, colonPos);
		try {
			port = std::stoi(hostPort.substr(colonPos + 1));
		} catch (...) {
			return false;
		}
	}
	else
	{
		host = hostPort;
	}

	return !host.empty();
}

std::wstring CWebDavSync::NormalizeRemotePath(const std::wstring& basePath, const std::wstring& dir, const std::wstring& filename)
{
	std::wstring full = basePath;
	if (!full.empty() && full.back() == L'/')
		full.pop_back();

	std::wstring d = dir;
	if (d.empty() || d.front() != L'/')
		d = L"/" + d;
	if (!d.empty() && d.back() == L'/')
		d.pop_back();

	full += d + L"/" + filename;

	std::wstring result;
	bool lastWasSlash = false;
	for (wchar_t ch : full)
	{
		if (ch == L'/')
		{
			if (!lastWasSlash)
				result += ch;
			lastWasSlash = true;
		}
		else
		{
			result += ch;
			lastWasSlash = false;
		}
	}
	return result;
}

DWORD CWebDavSync::ExecuteDavRequest(const SettingData& settings, const wchar_t* verb,
	const std::wstring& requestPath, const std::string& body,
	std::string* responseOut, int connectTimeoutSec, std::wstring& errorMsg,
	const char* extraHeaders)
{
	errorMsg.clear();
	if (responseOut)
		responseOut->clear();

	bool isHttps = false;
	std::wstring host, basePath;
	int port = 80;
	if (!ParseURL(settings.m_webdav_url, isHttps, host, port, basePath))
	{
		errorMsg = L"无效的 WebDAV 服务器 URL 格式";
		return 0;
	}

	CInternetSession session(_T("TrafficMonitor-Stock-WebDAV"), 1, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, connectTimeoutSec * 1000);
	session.SetOption(INTERNET_OPTION_SEND_TIMEOUT, 15000);
	session.SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, 15000);

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
	if (isHttps)
		flags |= INTERNET_FLAG_SECURE;

	CHttpConnection* pConn = nullptr;
	CHttpFile* pFile = nullptr;
	DWORD statusCode = 0;

	try
	{
		pConn = session.GetHttpConnection(host.c_str(), (INTERNET_PORT)port);
		pFile = pConn->OpenRequest(verb, requestPath.c_str(), NULL, 1, NULL, NULL, flags);

		if (!settings.m_webdav_username.empty() || !settings.m_webdav_password.empty())
		{
			// Basic 认证按 RFC 7617 使用 UTF-8 编码（兼容坚果云/Nextcloud 中文账号）
			std::string rawAuth = CCommon::UnicodeToStr(settings.m_webdav_username, true) + ":" +
				CCommon::UnicodeToStr(settings.m_webdav_password, true);
			std::string authHeader = "Authorization: Basic " + Base64Encode(rawAuth) + "\r\n";
			pFile->AddRequestHeaders(CString(authHeader.c_str()));
		}
		if (extraHeaders != nullptr && extraHeaders[0] != '\0')
			pFile->AddRequestHeaders(CString(extraHeaders));

		pFile->SendRequest(NULL, 0, body.empty() ? (LPVOID)NULL : (LPVOID)body.data(), static_cast<DWORD>(body.size()));
		pFile->QueryInfoStatusCode(statusCode);

		if (responseOut)
		{
			char buf[8192];
			UINT nRead = 0;
			while ((nRead = pFile->Read(buf, sizeof(buf))) > 0)
				responseOut->append(buf, nRead);
		}
	}
	catch (CInternetException* e)
	{
		TCHAR szErr[256];
		if (!e->GetErrorMessage(szErr, 256))
			_tcscpy_s(szErr, _T("无法连接到 WebDAV 服务器"));
		errorMsg = szErr;
		statusCode = 0;
		e->Delete();
	}
	catch (...)
	{
		errorMsg = L"发生未知网络异常";
		statusCode = 0;
	}

	if (pFile) { try { pFile->Close(); } catch (...) {} delete pFile; }
	if (pConn) { try { pConn->Close(); } catch (...) {} delete pConn; }
	return statusCode;
}

bool CWebDavSync::TestConnection(const SettingData& settings, std::wstring& errorMsg)
{
	errorMsg.clear();
	if (settings.m_webdav_url.empty())
	{
		errorMsg = L"WebDAV 服务器地址不能为空";
		return false;
	}

	bool isHttps = false;
	std::wstring host, basePath;
	int port = 80;
	if (!ParseURL(settings.m_webdav_url, isHttps, host, port, basePath))
	{
		errorMsg = L"无效的 WebDAV 服务器 URL 格式";
		return false;
	}

	// OPTIONS 探测连通性与认证；404/405 仅表示路径形态差异，连接本身可用
	std::wstring err;
	DWORD code = ExecuteDavRequest(settings, L"OPTIONS", basePath, std::string(), nullptr, 6, err);
	if (code == 0)
	{
		errorMsg = err.empty() ? L"无法建立网络连接" : err;
		return false;
	}
	if (code == 200 || code == 207 || code == 204 || code == 404 || code == 405)
		return true;
	if (code == 401 || code == 403)
		errorMsg = L"认证失败(HTTP " + std::to_wstring(code) + L")，请检查用户名和应用密码";
	else
		errorMsg = L"服务器响应异常，HTTP 状态码: " + std::to_wstring(code);
	return false;
}

bool CWebDavSync::EnsureRemoteDir(const SettingData& settings, const std::wstring& basePath, std::wstring& errorMsg)
{
	// 只创建 m_webdav_dir 指定的层级；URL 中的基准路径视为服务端已有挂载点
	std::wstring rel = settings.m_webdav_dir;
	while (!rel.empty() && rel.front() == L'/')
		rel.erase(0, 1);
	while (!rel.empty() && rel.back() == L'/')
		rel.pop_back();
	if (rel.empty())
		return true;

	std::wstring prefix = basePath;
	while (!prefix.empty() && prefix.back() == L'/')
		prefix.pop_back();

	size_t start = 0;
	while (start <= rel.size())
	{
		size_t slash = rel.find(L'/', start);
		size_t end = (slash == std::wstring::npos) ? rel.size() : slash;
		if (end > start)
		{
			prefix += L"/" + rel.substr(start, end - start);
			std::wstring err;
			DWORD code = ExecuteDavRequest(settings, L"MKCOL", prefix, std::string(), nullptr, 8, err);
			if (code == 0)
			{
				errorMsg = err.empty() ? L"无法连接 WebDAV 服务器" : err;
				return false;
			}
			// 405 = 目录已存在；200/301 为部分服务器的变体响应，均视为成功
			if (code != 201 && code != 200 && code != 405 && code != 301)
			{
				errorMsg = L"自动创建远端目录失败(HTTP " + std::to_wstring(code) + L"): " + prefix;
				return false;
			}
		}
		if (slash == std::wstring::npos)
			break;
		start = slash + 1;
	}
	return true;
}

std::wstring CWebDavSync::MakeBackupFileName()
{
	time_t now = time(nullptr);
	tm t{};
	localtime_s(&t, &now);
	wchar_t buf[64]{};
	swprintf_s(buf, L"stock_backup_%04d%02d%02d_%02d%02d%02d.ini",
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	return buf;
}

bool CWebDavSync::ParseBackupFileName(const std::wstring& name, unsigned long long& sortKey, std::wstring& displayName)
{
	const std::wstring prefix = L"stock_backup_";
	const std::wstring suffix = L".ini";
	if (name.size() != prefix.size() + 15 + suffix.size())
		return false;
	if (name.compare(0, prefix.size(), prefix) != 0)
		return false;
	if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
		return false;

	std::wstring ts = name.substr(prefix.size(), 15); // YYYYMMDD_HHMMSS
	for (size_t i = 0; i < ts.size(); ++i)
	{
		if (i == 8)
		{
			if (ts[i] != L'_')
				return false;
		}
		else if (ts[i] < L'0' || ts[i] > L'9')
		{
			return false;
		}
	}

	std::wstring digits = ts.substr(0, 8) + ts.substr(9); // YYYYMMDDHHMMSS
	sortKey = _wtoi64(digits.c_str());

	wchar_t disp[32]{};
	swprintf_s(disp, L"%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
		digits.c_str(), digits.c_str() + 4, digits.c_str() + 6,
		digits.c_str() + 8, digits.c_str() + 10, digits.c_str() + 12);
	displayName = disp;
	return true;
}

bool CWebDavSync::UploadBackup(const SettingData& settings, std::wstring& errorMsg)
{
	errorMsg.clear();
	if (settings.m_webdav_url.empty())
	{
		errorMsg = L"WebDAV 服务器地址不能为空";
		return false;
	}

	bool isHttps = false;
	std::wstring host, basePath;
	int port = 80;
	if (!ParseURL(settings.m_webdav_url, isHttps, host, port, basePath))
	{
		errorMsg = L"无效的 WebDAV 服务器 URL 格式";
		return false;
	}

	// 读取当前 INI 文件作为备份内容（调用方需保证配置已先行保存）
	std::wstring configPath = g_data.GetConfigPath();
	std::ifstream inFile(configPath, std::ios::binary);
	if (!inFile.is_open())
	{
		errorMsg = L"无法读取本地配置文件: " + configPath;
		return false;
	}
	std::string fileData((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
	inFile.close();

	if (fileData.empty())
	{
		errorMsg = L"本地配置文件内容为空";
		return false;
	}

	// 每次备份独立存档，以本地时间戳命名，不覆盖历史备份
	std::wstring targetPath = NormalizeRemotePath(basePath, settings.m_webdav_dir, MakeBackupFileName());

	// 首次备份时远端目录可能还不存在，先逐级创建
	if (!EnsureRemoteDir(settings, basePath, errorMsg))
		return false;

	std::wstring err;
	DWORD code = ExecuteDavRequest(settings, L"PUT", targetPath, fileData, nullptr, 10, err);
	if (code == 0)
	{
		errorMsg = err.empty() ? L"无法连接 WebDAV 服务器" : err;
		return false;
	}
	if (code == 200 || code == 201 || code == 204 || code == 207)
	{
		CleanupOldBackups(settings, kMaxKeepBackups);
		return true;
	}
	if (code == 401 || code == 403)
		errorMsg = L"认证失败(HTTP " + std::to_wstring(code) + L")，请检查用户名和应用密码";
	else if (code == 507)
		errorMsg = L"云端存储空间不足(HTTP 507)";
	else
		errorMsg = L"上传失败，HTTP 状态码: " + std::to_wstring(code);
	return false;
}

bool CWebDavSync::ListBackups(const SettingData& settings, std::vector<WebDavBackupEntry>& entriesOut, std::wstring& errorMsg)
{
	entriesOut.clear();
	errorMsg.clear();
	if (settings.m_webdav_url.empty())
	{
		errorMsg = L"WebDAV 服务器地址不能为空";
		return false;
	}

	bool isHttps = false;
	std::wstring host, basePath;
	int port = 80;
	if (!ParseURL(settings.m_webdav_url, isHttps, host, port, basePath))
	{
		errorMsg = L"无效的 WebDAV 服务器 URL 格式";
		return false;
	}

	std::wstring dirPath = NormalizeRemotePath(basePath, settings.m_webdav_dir, L"");

	const char* propfindBody =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<D:propfind xmlns:D=\"DAV:\"><D:prop>"
		"<D:getcontentlength/><D:getlastmodified/>"
		"</D:prop></D:propfind>";

	std::string resp;
	std::wstring err;
	// Depth:1 仅列出当前目录；404 视为目录尚不存在，即云端还没有任何备份
	DWORD code = ExecuteDavRequest(settings, L"PROPFIND", dirPath, propfindBody, &resp, 10, err,
		"Depth: 1\r\nContent-Type: text/xml; charset=utf-8\r\n");
	if (code == 0)
	{
		errorMsg = err.empty() ? L"无法连接 WebDAV 服务器" : err;
		return false;
	}
	if (code == 404)
		return true;
	if (code == 401 || code == 403)
	{
		errorMsg = L"认证失败(HTTP " + std::to_wstring(code) + L")，请检查用户名和应用密码";
		return false;
	}
	if (code != 207 && code != 200)
	{
		errorMsg = L"获取云端备份列表失败，HTTP 状态码: " + std::to_wstring(code);
		return false;
	}

	// 逐块解析 207 multistatus 的每个 <response> 节点
	struct Block { size_t begin; size_t end; };
	std::vector<Block> blocks;
	size_t pos = 0;
	while (true)
	{
		size_t m = FindIC(resp, pos, ":response");
		if (m == std::string::npos)
			break;
		pos = m + strlen(":response");
		char next = (pos < resp.size()) ? resp[pos] : '\0';
		if (next != '>' && next != ' ')
			continue; // 跳过 :responses 之类的误匹配
		size_t lt = resp.rfind('<', m);
		if (lt == std::string::npos || (lt + 1 < resp.size() && resp[lt + 1] == '/'))
			continue; // 跳过闭合标签 </D:response>
		blocks.push_back({ m, resp.size() });
		if (blocks.size() > 1)
			blocks[blocks.size() - 2].end = m;
	}

	for (const Block& block : blocks)
	{
		std::string href = ExtractTagContent(resp, block.begin, block.end, "href");
		if (href.empty())
			continue;

		// 取 href 最后一段作为文件名；目录节点以 / 结尾，跳过
		size_t slash = href.find_last_of('/');
		std::wstring fileName = UrlDecodeFileName(slash == std::string::npos ? href : href.substr(slash + 1));
		if (fileName.empty() || (slash != std::string::npos && slash == href.size() - 1))
			continue;

		WebDavBackupEntry entry;
		entry.fileName = fileName;
		unsigned long long key = 0;
		std::wstring disp;
		if (ParseBackupFileName(fileName, key, disp))
		{
			entry.sortKey = key;
			entry.displayName = disp;
		}
		else if (fileName == L"stock_backup.ini")
		{
			// 旧版本的单文件备份，保留可恢复入口，排序靠后
			entry.isLegacy = true;
			entry.displayName = L"旧版备份 (stock_backup.ini)";
		}
		else
		{
			continue; // 只关心本插件的备份文件
		}

		std::string lenStr = ExtractTagContent(resp, block.begin, block.end, "getcontentlength");
		entry.sizeBytes = _strtoui64(lenStr.c_str(), nullptr, 10);

		std::string modStr = ExtractTagContent(resp, block.begin, block.end, "getlastmodified");
		if (entry.isLegacy && !modStr.empty())
		{
			// 旧版备份文件名无时间戳，用服务器文件修改时间展示与排序
			SYSTEMTIME st{};
			if (InternetTimeToSystemTimeA(modStr.c_str(), &st, 0))
			{
				FILETIME ft{};
				SystemTimeToFileTime(&st, &ft);
				entry.sortKey = static_cast<unsigned long long>(
					(ft.dwHighDateTime * 0x100000000ULL + ft.dwLowDateTime - 116444736000000000ULL) / 10000000ULL);
				tm t{};
				time_t tt = static_cast<time_t>(entry.sortKey);
				localtime_s(&t, &tt);
				wchar_t dispBuf[32]{};
				wcsftime(dispBuf, 32, L"%Y-%m-%d %H:%M:%S", &t);
				entry.displayName = std::wstring(dispBuf) + L" 旧版备份";
			}
		}
		entriesOut.push_back(entry);
	}

	// 新→旧排序；旧版无时间戳备份始终排在时间戳存档之后
	std::sort(entriesOut.begin(), entriesOut.end(), [](const WebDavBackupEntry& a, const WebDavBackupEntry& b) {
		if (a.isLegacy != b.isLegacy)
			return b.isLegacy;
		return a.sortKey > b.sortKey;
		});
	return true;
}

void CWebDavSync::CleanupOldBackups(const SettingData& settings, size_t keepCount)
{
	std::vector<WebDavBackupEntry> entries;
	std::wstring err;
	if (!ListBackups(settings, entries, err))
		return;

	bool isHttps = false;
	std::wstring host, basePath;
	int port = 80;
	if (!ParseURL(settings.m_webdav_url, isHttps, host, port, basePath))
		return;

	// entries 已按新→旧排序，保留前 keepCount 份时间戳存档
	size_t kept = 0;
	for (const WebDavBackupEntry& entry : entries)
	{
		if (entry.isLegacy)
			continue;
		++kept;
		if (kept <= keepCount)
			continue;
		std::wstring delErr;
		std::wstring path = NormalizeRemotePath(basePath, settings.m_webdav_dir, entry.fileName);
		ExecuteDavRequest(settings, L"DELETE", path, std::string(), nullptr, 8, delErr);
	}
}

bool CWebDavSync::DownloadBackupData(const SettingData& settings, const std::wstring& remoteFile, std::string& dataOut, std::wstring& errorMsg)
{
	errorMsg.clear();
	dataOut.clear();
	if (settings.m_webdav_url.empty())
	{
		errorMsg = L"WebDAV 服务器地址不能为空";
		return false;
	}
	// 文件名来自远端目录列表，防御性校验：不允许再带路径分隔符
	if (remoteFile.empty() || remoteFile.find(L'/') != std::wstring::npos ||
		remoteFile.find(L'\\') != std::wstring::npos || remoteFile.find(L"..") != std::wstring::npos)
	{
		errorMsg = L"非法的云端备份文件名: " + remoteFile;
		return false;
	}

	bool isHttps = false;
	std::wstring host, basePath;
	int port = 80;
	if (!ParseURL(settings.m_webdav_url, isHttps, host, port, basePath))
	{
		errorMsg = L"无效的 WebDAV 服务器 URL 格式";
		return false;
	}

	std::wstring targetPath = NormalizeRemotePath(basePath, settings.m_webdav_dir, remoteFile);

	std::wstring err;
	DWORD code = ExecuteDavRequest(settings, L"GET", targetPath, std::string(), &dataOut, 10, err);
	if (code == 0)
	{
		errorMsg = err.empty() ? L"无法连接 WebDAV 服务器" : err;
		return false;
	}
	if (code != 200)
	{
		if (code == 401 || code == 403)
			errorMsg = L"认证失败(HTTP " + std::to_wstring(code) + L")，请检查用户名和应用密码";
		else if (code == 404)
			errorMsg = L"云端备份文件不存在(HTTP 404)，请先在另一台设备上上传备份";
		else
			errorMsg = L"下载失败，HTTP 状态码: " + std::to_wstring(code);
		return false;
	}
	if (dataOut.empty())
	{
		errorMsg = L"云端备份文件内容为空";
		return false;
	}
	return true;
}

bool CWebDavSync::DownloadBackup(const SettingData& settings, std::wstring& errorMsg)
{
	// 启动时自动同步：取时间戳最新的一份（旧版备份兜底，排序已保证）
	std::vector<WebDavBackupEntry> entries;
	if (!ListBackups(settings, entries, errorMsg))
		return false;
	if (entries.empty())
	{
		errorMsg = L"云端暂无可用备份文件(HTTP 404)，请先在另一台设备上上传备份";
		return false;
	}

	std::string data;
	if (!DownloadBackupData(settings, entries.front().fileName, data, errorMsg))
		return false;

	// 写入本地 INI 配置文件并重新加载（仅启动时后台自动同步路径使用）
	std::wstring configPath = g_data.GetConfigPath();
	std::ofstream outFile(configPath, std::ios::binary | std::ios::trunc);
	if (!outFile.is_open())
	{
		errorMsg = L"无法写入本地配置文件: " + configPath;
		return false;
	}
	outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
	outFile.close();

	g_data.LoadConfig(L"");
	return true;
}
