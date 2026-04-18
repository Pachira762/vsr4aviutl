#include "pch.h"
#include "upscale.h"
#include "log.h"

#pragma comment(lib, "cuda.lib")
#pragma comment(lib, "cudart_static.lib")
#pragma comment(lib, "nvsdk_ngx_s.lib")

constexpr int MAX_RESOLUTION = 16384;

static FILTER_ITEM_SELECT::ITEM g_quality_list[] = {
	{ L"バイキュービック", NVSDK_NGX_VSR_Quality_Bicubic },
	{ L"低", NVSDK_NGX_VSR_Quality_Low },
	{ L"中", NVSDK_NGX_VSR_Quality_Medium },
	{ L"高", NVSDK_NGX_VSR_Quality_High },
	{ L"ウルトラ", NVSDK_NGX_VSR_Quality_Ultra },
	{}
};
static FILTER_ITEM_SELECT g_quality = FILTER_ITEM_SELECT(L"品質", (int)NVSDK_NGX_VSR_Quality_Ultra, g_quality_list);
static FILTER_ITEM_TRACK g_scale = FILTER_ITEM_TRACK(L"拡大率", 1, 1.0, 10.0, 0.001);

static std::unique_ptr<Upscaler> g_plugin = nullptr;
static bool g_plugin_initialized = false;

bool proc_video(FILTER_PROC_VIDEO* video) {
	static std::vector<PIXEL_RGBA> in_buff{};
	static std::vector<PIXEL_RGBA> out_buff{};

	if (!g_plugin) {
		g_plugin = std::make_unique<Upscaler>();
		g_plugin_initialized = g_plugin->init();
		if (!g_plugin_initialized) {
			return false;
		}
	}

	if (!g_plugin || !g_plugin_initialized) {
		return false;
	}

	int src_width = video->object->width;
	int src_height = video->object->height;
	double scale = g_scale.value;
	int target_width = scale * src_width;
	int target_height = scale * src_height;

	if (std::max(target_width, target_height) > MAX_RESOLUTION) {
		output_warn(L"最大解像度を超過しました");
		return false;
	}

	in_buff.resize(src_width * src_height);
	video->get_image_data(in_buff.data());
	out_buff.resize(target_width * target_height);

	UpscaleInputs inputs{};
	inputs.src_width = src_width;
	inputs.src_height = src_height;
	inputs.src = in_buff.data();
	inputs.target_width = target_width;
	inputs.target_height = target_height;
	inputs.target = out_buff.data();
	inputs.quality = g_quality.value;
	if (!g_plugin->upscale(inputs)) {
		return false;
	}

	video->set_image_data(out_buff.data(), target_width, target_height);

	return true;
}

EXTERN_C __declspec(dllexport) FILTER_PLUGIN_TABLE* GetFilterPluginTable(void) {
	static void* items[] = {
		&g_quality,
		& g_scale,
		nullptr
	};

	static FILTER_PLUGIN_TABLE filter_plugin_table = {
		FILTER_PLUGIN_TABLE::FLAG_VIDEO,
		L"RTX VSR",
		nullptr,
		L"RTX VSR for AviUtl2",
		items,
		proc_video,
		nullptr,
	};
	return &filter_plugin_table;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
	g_plugin_initialized = false;
	g_plugin = nullptr;
}
