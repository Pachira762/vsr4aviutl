#pragma once
#include "pch.h"

void output_message_box(const std::wstring& message);
void output_log(const std::wstring& message);
void output_warn(const std::wstring& message);
void output_error(const std::wstring& message);

#define DLOG(level, fmt, ...) { output_##level(std::format(fmt, __VA_ARGS__)); }
