#include "pch.h"
#include "WebDavSync.h"
#include "DataManager.h"
#include "Common.h"
#include "Stock.h"
#include <afxinet.h>
#include <wincrypt.h>
#include <sstream>
#include <fstream>

#pragma comment(lib, "Crypt32.lib")

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

	DWORD accessType = INTERNET_OPEN_TYPE_DIRECT;
	CInternetSession session(_T("TrafficMonitor-Stock-WebDAV"), 1, accessType, NULL, NULL, 0);
	session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, 8000);
	session.SetOption(INTERNET_OPTION_SEND_TIMEOUT, 8000);
	session.SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, 8000);

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
	if (isHttps)
		flags |= INTERNET_FLAG_SECURE;

	CHttpConnection* pConn = nullptr;
	CHttpFile* pFile = nullptr;

	try
	{
		pConn = session.GetHttpConnection(host.c_str(), (INTERNET_PORT)port);
		if (!pConn)
		{
			errorMsg = L"无法连接到目标服务器主机";
			return false;
		}

		pFile = pConn->OpenRequest(L"OPTIONS", basePath.c_str(), NULL, 1, NULL, NULL, flags);
		if (!pFile)
		{
			pConn->Close();
			delete pConn;
			errorMsg = L"创建 HTTP OPTIONS 请求失败";
			return false;
		}

		if (!settings.m_webdav_username.empty() || !settings.m_webdav_password.empty())
		{
			std::string rawAuth = CCommon::UnicodeToStr(settings.m_webdav_username) + ":" +
				CCommon::UnicodeToStr(settings.m_webdav_password);
			std::string authHeader = "Authorization: Basic " + Base64Encode(rawAuth) + "\r\n";
			pFile->AddRequestHeaders(CString(authHeader.c_str()));
		}

		pFile->SendRequest();

		DWORD statusCode = 0;
		pFile->QueryInfoStatusCode(statusCode);

		pFile->Close();
		delete pFile;
		pConn->Close();
		delete pConn;

		if (statusCode == 200 || statusCode == 207 || statusCode == 204 || statusCode == 405 || statusCode == 404)
		{
			return true;
		}
		else if (statusCode == 401 || statusCode == 403)
		{
			errorMsg = L"认证失败(HTTP " + std::to_wstring(statusCode) + L")，请检查用户名和应用密码";
			return false;
		}
		else
		{
			errorMsg = L"服务器响应异常，HTTP 状态码: " + std::to_wstring(statusCode);
			return false;
		}
	}
	catch (CInternetException* e)
	{
		TCHAR szErr[256];
		e->GetErrorMessage(szErr, 256);
		errorMsg = szErr;
		e->Delete();
	}
	catch (...)
	{
		errorMsg = L"发生未知网络错误";
	}

	if (pFile) { try { pFile->Close(); } catch (...) {} delete pFile; }
	if (pConn) { try { pConn->Close(); } catch (...) {} delete pConn; }
	return false;
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

	// 先确保本地最新配置已保存至 ini
	g_data.m_setting_data = settings;
	g_data.SaveConfig();

	// 读取当前 INI 文件作为备份内容
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

	std::wstring targetPath = NormalizeRemotePath(basePath, settings.m_webdav_dir, L"stock_backup.ini");

	DWORD accessType = INTERNET_OPEN_TYPE_DIRECT;
	CInternetSession session(_T("TrafficMonitor-Stock-WebDAV"), 1, accessType, NULL, NULL, 0);
	session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, 10000);
	session.SetOption(INTERNET_OPTION_SEND_TIMEOUT, 15000);
	session.SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, 15000);

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
	if (isHttps)
		flags |= INTERNET_FLAG_SECURE;

	CHttpConnection* pConn = nullptr;
	CHttpFile* pFile = nullptr;

	try
	{
		pConn = session.GetHttpConnection(host.c_str(), (INTERNET_PORT)port);
		if (!pConn)
		{
			errorMsg = L"无法连接到目标服务器主机";
			return false;
		}

		pFile = pConn->OpenRequest(L"PUT", targetPath.c_str(), NULL, 1, NULL, NULL, flags);
		if (!pFile)
		{
			pConn->Close();
			delete pConn;
			errorMsg = L"创建 HTTP PUT 请求失败";
			return false;
		}

		if (!settings.m_webdav_username.empty() || !settings.m_webdav_password.empty())
		{
			std::string rawAuth = CCommon::UnicodeToStr(settings.m_webdav_username) + ":" +
				CCommon::UnicodeToStr(settings.m_webdav_password);
			std::string authHeader = "Authorization: Basic " + Base64Encode(rawAuth) + "\r\n";
			pFile->AddRequestHeaders(CString(authHeader.c_str()));
		}
		pFile->AddRequestHeaders(L"Content-Type: text/plain; charset=utf-8\r\n");

		pFile->SendRequest(NULL, 0, (LPVOID)fileData.c_str(), static_cast<DWORD>(fileData.size()));

		DWORD statusCode = 0;
		pFile->QueryInfoStatusCode(statusCode);

		pFile->Close();
		delete pFile;
		pConn->Close();
		delete pConn;

		if (statusCode == 200 || statusCode == 201 || statusCode == 204)
		{
			return true;
		}
		else if (statusCode == 401 || statusCode == 403)
		{
			errorMsg = L"认证失败(HTTP " + std::to_wstring(statusCode) + L")，请检查用户名和应用密码";
			return false;
		}
		else if (statusCode == 404)
		{
			errorMsg = L"远端目录不存在(HTTP 404)，请先在网盘中创建对应目录: " + settings.m_webdav_dir;
			return false;
		}
		else
		{
			errorMsg = L"上传失败，HTTP 状态码: " + std::to_wstring(statusCode);
			return false;
		}
	}
	catch (CInternetException* e)
	{
		TCHAR szErr[256];
		e->GetErrorMessage(szErr, 256);
		errorMsg = szErr;
		e->Delete();
	}
	catch (...)
	{
		errorMsg = L"发生未知网络异常";
	}

	if (pFile) { try { pFile->Close(); } catch (...) {} delete pFile; }
	if (pConn) { try { pConn->Close(); } catch (...) {} delete pConn; }
	return false;
}

bool CWebDavSync::DownloadBackup(const SettingData& settings, std::wstring& errorMsg)
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

	std::wstring targetPath = NormalizeRemotePath(basePath, settings.m_webdav_dir, L"stock_backup.ini");

	DWORD accessType = INTERNET_OPEN_TYPE_DIRECT;
	CInternetSession session(_T("TrafficMonitor-Stock-WebDAV"), 1, accessType, NULL, NULL, 0);
	session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, 10000);
	session.SetOption(INTERNET_OPTION_SEND_TIMEOUT, 15000);
	session.SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, 15000);

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_KEEP_CONNECTION;
	if (isHttps)
		flags |= INTERNET_FLAG_SECURE;

	CHttpConnection* pConn = nullptr;
	CHttpFile* pFile = nullptr;

	try
	{
		pConn = session.GetHttpConnection(host.c_str(), (INTERNET_PORT)port);
		if (!pConn)
		{
			errorMsg = L"无法连接到目标服务器主机";
			return false;
		}

		pFile = pConn->OpenRequest(L"GET", targetPath.c_str(), NULL, 1, NULL, NULL, flags);
		if (!pFile)
		{
			pConn->Close();
			delete pConn;
			errorMsg = L"创建 HTTP GET 请求失败";
			return false;
		}

		if (!settings.m_webdav_username.empty() || !settings.m_webdav_password.empty())
		{
			std::string rawAuth = CCommon::UnicodeToStr(settings.m_webdav_username) + ":" +
				CCommon::UnicodeToStr(settings.m_webdav_password);
			std::string authHeader = "Authorization: Basic " + Base64Encode(rawAuth) + "\r\n";
			pFile->AddRequestHeaders(CString(authHeader.c_str()));
		}

		pFile->SendRequest();

		DWORD statusCode = 0;
		pFile->QueryInfoStatusCode(statusCode);

		if (statusCode != 200)
		{
			pFile->Close();
			delete pFile;
			pConn->Close();
			delete pConn;

			if (statusCode == 401 || statusCode == 403)
				errorMsg = L"认证失败(HTTP " + std::to_wstring(statusCode) + L")，请检查用户名和应用密码";
			else if (statusCode == 404)
				errorMsg = L"云端备份文件不存在(HTTP 404)，请先在另一台设备上上传备份";
			else
				errorMsg = L"下载失败，HTTP 状态码: " + std::to_wstring(statusCode);
			return false;
		}

		std::string resultData;
		char buf[8192];
		UINT nRead = 0;
		while ((nRead = pFile->Read(buf, sizeof(buf))) > 0)
		{
			resultData.append(buf, nRead);
		}

		pFile->Close();
		delete pFile;
		pConn->Close();
		delete pConn;

		if (resultData.empty())
		{
			errorMsg = L"云端下载的文件内容为空";
			return false;
		}

		// 写入本地 INI 配置文件
		std::wstring configPath = g_data.GetConfigPath();
		std::ofstream outFile(configPath, std::ios::binary | std::ios::trunc);
		if (!outFile.is_open())
		{
			errorMsg = L"无法写入本地配置文件: " + configPath;
			return false;
		}
		outFile.write(resultData.data(), resultData.size());
		outFile.close();

		// 重新加载配置
		g_data.LoadConfig(L"");
		Stock::Instance().SendStockInfoRequest();
		return true;
	}
	catch (CInternetException* e)
	{
		TCHAR szErr[256];
		e->GetErrorMessage(szErr, 256);
		errorMsg = szErr;
		e->Delete();
	}
	catch (...)
	{
		errorMsg = L"发生未知网络异常";
	}

	if (pFile) { try { pFile->Close(); } catch (...) {} delete pFile; }
	if (pConn) { try { pConn->Close(); } catch (...) {} delete pConn; }
	return false;
}
