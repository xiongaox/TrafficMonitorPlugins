#pragma once
#include "DataManager.h"
#include <string>
#include <vector>

// 云端目录中的一份历史备份
struct WebDavBackupEntry
{
	std::wstring fileName;          // 远端文件名，如 stock_backup_20260830_211227.ini
	std::wstring displayName;       // 展示文本，如 2026-08-30 21:12:27（旧版备份为文件名说明）
	unsigned long long sizeBytes{ 0 };
	bool isLegacy{ false };         // 旧版单文件备份 stock_backup.ini（无时间戳）
	unsigned long long sortKey{ 0 };// 排序键：时间戳文件名为 YYYYMMDDHHMMSS，旧版为文件修改时间
};

class CWebDavSync
{
public:
	// 测试与 WebDAV 服务器的连接和认证状态
	static bool TestConnection(const SettingData& settings, std::wstring& errorMsg);

	// 上传本地配置文件（调用方需保证配置已先保存至 ini）至 WebDAV 云端备份。
	// 每次以时间戳命名独立存档（stock_backup_YYYYMMDD_HHMMSS.ini），不覆盖历史；
	// 上传成功后自动清理，仅保留最近 kMaxKeepBackups 份
	static bool UploadBackup(const SettingData& settings, std::wstring& errorMsg);

	// PROPFIND 列出云端目录下的全部历史备份，按备份时间倒序。
	// 远端目录不存在（404）视为成功且列表为空
	static bool ListBackups(const SettingData& settings, std::vector<WebDavBackupEntry>& entriesOut, std::wstring& errorMsg);

	// 从 WebDAV 云端下载指定备份文件内容（纯网络操作，不写盘、不改本地配置）
	static bool DownloadBackupData(const SettingData& settings, const std::wstring& remoteFile,
		std::string& dataOut, std::wstring& errorMsg);

	// 下载云端最新一份备份并直接应用到本地（写 ini + 重载配置），供启动时后台自动同步使用
	static bool DownloadBackup(const SettingData& settings, std::wstring& errorMsg);

	// 云端最多保留的历史备份份数，超出后上传时自动删除最旧的
	static const size_t kMaxKeepBackups = 30;

private:
	// 解析 URL
	static bool ParseURL(const std::wstring& url, bool& isHttps, std::wstring& host, int& port, std::wstring& path);

	// Base64 编码（用于 HTTP Basic 认证）
	static std::string Base64Encode(const std::string& input);

	// 拼接并规范化远端完整路径
	static std::wstring NormalizeRemotePath(const std::wstring& basePath, const std::wstring& dir, const std::wstring& filename);

	// 生成当前本地时间的备份文件名 stock_backup_YYYYMMDD_HHMMSS.ini
	static std::wstring MakeBackupFileName();

	// 解析时间戳备份文件名，sortKey 为 YYYYMMDDHHMMSS，displayName 为可读时间；非时间戳命名返回 false
	static bool ParseBackupFileName(const std::wstring& name, unsigned long long& sortKey, std::wstring& displayName);

	// 统一的低层 WebDAV 请求：负责建连、Basic 认证、发送与读取响应。
	// body 为空表示无请求体；responseOut 非空时接收响应体；extraHeaders 附加原始请求头（如 Depth）。
	// 返回 HTTP 状态码，网络层失败返回 0 并填 errorMsg。
	static DWORD ExecuteDavRequest(const SettingData& settings, const wchar_t* verb,
		const std::wstring& requestPath, const std::string& body,
		std::string* responseOut, int connectTimeoutSec, std::wstring& errorMsg,
		const char* extraHeaders = nullptr);

	// 逐级 MKCOL 创建远端目录（已存在视为成功），只处理 m_webdav_dir 部分
	static bool EnsureRemoteDir(const SettingData& settings, const std::wstring& basePath, std::wstring& errorMsg);

	// 上传成功后删除云端超出 keepCount 的最旧备份（尽力而为，失败不影响上传结果）
	static void CleanupOldBackups(const SettingData& settings, size_t keepCount);
};
