#pragma once
#include "DataManager.h"
#include <string>

class CWebDavSync
{
public:
	// 测试与 WebDAV 服务器的连接和认证状态
	static bool TestConnection(const SettingData& settings, std::wstring& errorMsg);

	// 上传当前本地全部配置至 WebDAV 云端备份
	static bool UploadBackup(const SettingData& settings, std::wstring& errorMsg);

	// 从 WebDAV 云端下载并恢复全部配置
	static bool DownloadBackup(const SettingData& settings, std::wstring& errorMsg);

private:
	// 解析 URL
	static bool ParseURL(const std::wstring& url, bool& isHttps, std::wstring& host, int& port, std::wstring& path);

	// Base64 编码（用于 HTTP Basic 认证）
	static std::string Base64Encode(const std::string& input);

	// 拼接并规范化远端完整路径
	static std::wstring NormalizeRemotePath(const std::wstring& basePath, const std::wstring& dir, const std::wstring& filename);
};
