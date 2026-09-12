#pragma once

// 数值版本号（供 Stock.rc VERSIONINFO 使用）
#define STOCK_VERSION_MAJOR         2
#define STOCK_VERSION_MINOR         0
#define STOCK_VERSION_PATCH         5
#define STOCK_VERSION_BUILD         0

// 辅助字符串宏转换
#define _STOCK_STR(x)               #x
#define _STOCK_TO_STR(x)            _STOCK_STR(x)
#define _STOCK_WSTR(x)              L#x
#define _STOCK_TO_WSTR(x)           _STOCK_WSTR(x)

// 宽字符版本号（供 Stock.cpp / ManagerDialog.cpp 界面与接口展示）
#define STOCK_VERSION_STR           L"2.0"

// 完整修订版宽字符（例如 L"2.0.5"）
#define STOCK_FULL_VERSION_STR      _STOCK_TO_WSTR(STOCK_VERSION_MAJOR) L"." _STOCK_TO_WSTR(STOCK_VERSION_MINOR) L"." _STOCK_TO_WSTR(STOCK_VERSION_PATCH)

// ANSI 字符串版本号（供 Stock.rc StringFileInfo 使用）
#define STOCK_VERSION_RC_STR        _STOCK_TO_STR(STOCK_VERSION_MAJOR) "." _STOCK_TO_STR(STOCK_VERSION_MINOR) "." _STOCK_TO_STR(STOCK_VERSION_PATCH) "." _STOCK_TO_STR(STOCK_VERSION_BUILD)
