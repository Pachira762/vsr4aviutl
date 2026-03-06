#pragma once
#include "pch.h"

struct UpscaleInputs {
	int src_width;
	int src_height;
	const PIXEL_RGBA* src;
	int target_width;
	int target_height;
	PIXEL_RGBA* target;
	int quality;
};

struct Upscaler {
	CUdevice device{};
	CUcontext context{};
	NVSDK_NGX_Parameter* ngx_parameters{};
	NVSDK_NGX_Handle* vsr_feature{};
	CUarray in_array{};
	CUtexObject in_tex{};
	CUarray out_array{};
	CUsurfObject out_surf{};

	~Upscaler();
	bool init();
	bool upscale(const UpscaleInputs& inputs);
};
