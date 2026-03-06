#pragma once
#include "pch.h"
#include "log.h"

#define RETURN_IF_FAILED(hr, message) if(FAILED(hr)) { DLOG(error, message); return false; }
#define RETURN_IF_CUDA_FAILED(result, message) if(result != CUDA_SUCCESS) { DLOG(error, message); return false; }
#define RETURN_IF_NGX_FAILED(result, message) if(NVSDK_NGX_FAILED(result)) { DLOG(error, message); return false; }
