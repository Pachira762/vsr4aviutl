#include "pch.h"
#include "log.h"

static LOG_HANDLE* g_logger = nullptr;

void output_message_box(const std::wstring& message) {
	MessageBoxW(NULL, message.c_str(), L"RTX VSR for Aviutl2", MB_OK);
}

void output_log(const std::wstring& message) {
	if (g_logger) {
		g_logger->log(g_logger, message.c_str());
	}
}

void output_warn(const std::wstring& message) {
	if (g_logger) {
		g_logger->warn(g_logger, message.c_str());
	}
}

void output_error(const std::wstring& message) {
	if (g_logger) {
		g_logger->error(g_logger, message.c_str());
	}
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
	g_logger = logger;
}
