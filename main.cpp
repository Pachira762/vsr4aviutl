#include <vector>
#include <tuple>
#include <map>
#ifdef _DEBUG
#include <format>
#endif
#define NOMINMAX
#include <windows.h>
#include <cuda.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers_vsr.h>
#include "filter2.h"

#pragma comment( lib, "cuda" )
#pragma comment( lib, "cudart" )

#ifdef _DEBUG
#pragma comment(lib, "nvsdk_ngx_d_dbg.lib")
#else
#pragma comment(lib, "nvsdk_ngx_d.lib")
#endif

#ifdef _DEBUG
#define DLOG(fmt, ...) {\
	auto msg = std::format(fmt, __VA_ARGS__);\
	MessageBoxA(NULL, msg.c_str(), "vsr4aviutl", MB_OK);\
}
#else
#define DLOG(fmt, ...) __noop
#endif

bool func_proc_video(FILTER_PROC_VIDEO* video);

//---------------------------------------------------------------------
//	フィルタ設定項目定義
//---------------------------------------------------------------------
auto g_scale = FILTER_ITEM_TRACK(L"拡大率", 1.0, 1.0, 4.0, 0.01);
FILTER_ITEM_SELECT::ITEM g_quality_list[] = { { L"Bicubic", 0 }, { L"Low", 1 }, { L"Medium", 2 }, { L"High", 3 }, { L"Ultra", 4}, {nullptr} };
auto g_quality = FILTER_ITEM_SELECT(L"クオリティ", 4, g_quality_list);
void* g_items[] = { &g_scale, &g_quality, nullptr };

struct Upscaler {
	CUdevice device_{};
	CUcontext ctx_{};
	NVSDK_NGX_Parameter* parameters_{};
	NVSDK_NGX_Handle* feature_{};
	bool initialized_ = false;

	bool init() {
		if (initialized_) {
			return true;
		}

		// init cuda
		{
			CUresult ret{};

			ret = cuInit(0);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuInit failed");
				return false;
			}

			ret = cuDeviceGet(&device_, 0);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuDeviceGet failed");
				return false;
			}

			ret = cuCtxCreate(&ctx_, 0, device_);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuCtxCreate failed");
				return false;
			}
		}

		// init ngx
		{
			NVSDK_NGX_Result ret{};

			ret = NVSDK_NGX_CUDA_Init(0, L".");
			if (NVSDK_NGX_FAILED(ret)) {
				DLOG("NVSDK_NGX_CUDA_Init failed");
				return false;
			}

			ret = NVSDK_NGX_CUDA_GetCapabilityParameters(&parameters_);
			if (NVSDK_NGX_FAILED(ret)) {
				DLOG("NVSDK_NGX_CUDA_GetCapabilityParameters failed");
				return false;
			}

			int vsr_available = {};
			ret = parameters_->Get(NVSDK_NGX_Parameter_VSR_Available, &vsr_available);
			if (NVSDK_NGX_FAILED(ret)) {
				DLOG("NVSDK_NGX_Parameter_VSR_Available failed");
				return false;
			}

			NVSDK_NGX_CUDA_VSR_Create_Params create_params{};
			create_params.InCUContext = ctx_;
			ret = NGX_CUDA_CREATE_VSR(&feature_, parameters_, &create_params);
			if (NVSDK_NGX_FAILED(ret)) {
				DLOG("NGX_CUDA_CREATE_VSR failed");
				return false;
			}
		}

		initialized_ = true;

		return true;
	}

	bool upscale(int src_w, int src_h, CUtexObject src, int dst_w, int dst_h, CUsurfObject dst, NVSDK_NGX_VSR_QualityLevel quality_level) {
		NVSDK_NGX_CUDA_VSR_Eval_Params eval{};
		eval.pInput = &src;
		eval.pOutput = &dst;
		eval.InputSubrectSize.Width = src_w;
		eval.InputSubrectSize.Height = src_h;
		eval.OutputSubrectSize.Width = dst_w;
		eval.OutputSubrectSize.Height = dst_h;
		eval.QualityLevel = quality_level;
		NVSDK_NGX_Result ret = NGX_CUDA_EVALUATE_VSR(feature_, parameters_, &eval);
		if (NVSDK_NGX_FAILED(ret)) {
			DLOG("NGX_CUDA_EVALUATE_VSR failed");
			return false;
		}

		cuCtxSynchronize();

		return true;
	}
};

static Upscaler g_upscaler{};

struct ResourcePool {
	std::vector<PIXEL_RGBA> buffer_{};
	std::map<int64_t, std::pair<CUarray, CUtexObject>> textures_{};
	std::map<int64_t, std::pair<CUarray, CUsurfObject>> surfaces_{};

	PIXEL_RGBA* get_buffer(int width, int height) {
		int size = width * height;
		if (buffer_.size() < size) {
			buffer_.resize(size);
		}
		return buffer_.data();
	}

	std::pair<CUarray, CUtexObject> get_texture(int width, int height) {
		int64_t key = ((int64_t)width << 32) | height;

		auto value = textures_.find(key);
		if (value != textures_.end()) {
			return value->second;
		}
		else {
			CUresult ret{};

			CUDA_ARRAY_DESCRIPTOR array_desc{};
			array_desc.Width = width;
			array_desc.Height = height;
			array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
			array_desc.NumChannels = 4;

			CUarray array{};
			ret = cuArrayCreate(&array, &array_desc);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuArrayCreate for tex failed\r\n{}x{}\r\n{}", width, height, (int)ret);
				return {};
			}

			CUDA_RESOURCE_DESC res_desc{};
			res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
			res_desc.res.array.hArray = array;

			CUDA_TEXTURE_DESC tex_desc{};
			tex_desc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
			tex_desc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
			tex_desc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;
			tex_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
			tex_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;

			CUtexObject tex{};
			ret = cuTexObjectCreate(&tex, &res_desc, &tex_desc, nullptr);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuTexObjectCreate failed");
				return {};
			}

			auto value = std::make_pair(array, tex);
			textures_[key] = value;
			return value;
		}
	}

	std::pair<CUarray, CUsurfObject> get_surface(int width, int height) {
		int64_t key = ((int64_t)width << 32) | height;

		auto value = surfaces_.find(key);
		if (value != surfaces_.end()) {
			return value->second;
		}
		else {
			CUresult ret{};

			CUDA_ARRAY_DESCRIPTOR array_desc{};
			array_desc.Width = width;
			array_desc.Height = height;
			array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
			array_desc.NumChannels = 4;

			CUarray array{};
			ret = cuArrayCreate(&array, &array_desc);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuArrayCreate for surf failed\r\n{}x{}", width, height);
				return {};
			}

			CUDA_RESOURCE_DESC res_desc{};
			res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
			res_desc.res.array.hArray = array;

			CUsurfObject surf{};
			ret = cuSurfObjectCreate(&surf, &res_desc);
			if (ret != CUDA_SUCCESS) {
				DLOG("cuSurfaceObjectCreate failed");
				return {};
			}

			auto value = std::make_pair(array, surf);
			surfaces_[key] = value;
			return value;
		}
	}
};

static ResourcePool g_resource_pool{};

//---------------------------------------------------------------------
//	画像フィルタ処理
//---------------------------------------------------------------------
bool func_proc_video(FILTER_PROC_VIDEO* video) {
	if (!g_upscaler.init()) {
		MessageBoxW(NULL, L"初期化に失敗しました。\r\nPCがRTX VSRに対応しているか確認してください。", L"vsr4aviutl", MB_OK);
		return false;
	}

	auto src_w = video->object->width;
	auto src_h = video->object->height;
	auto src_pitch = 4 * src_w;
	auto scale = g_scale.value;
	auto dst_w = (int)(scale * src_w);
	auto dst_h = (int)(scale * src_h);
	auto dst_pitch = 4 * dst_w;
	auto [tex_array, tex] = g_resource_pool.get_texture(src_w, src_h);
	auto [surf_array, surf] = g_resource_pool.get_surface(dst_w, dst_h);

	// upload source data to texture
	{
		auto src = g_resource_pool.get_buffer(src_w, src_h);
		video->get_image_data(src);

		CUDA_MEMCPY2D desc{};
		desc.srcMemoryType = CU_MEMORYTYPE_HOST;
		desc.srcHost = src;
		desc.srcPitch = src_pitch;
		desc.dstMemoryType = CU_MEMORYTYPE_ARRAY;
		desc.dstArray = tex_array;
		desc.dstPitch = src_pitch;
		desc.WidthInBytes = src_pitch;
		desc.Height = src_h;
		if (cuMemcpy2D(&desc) != CUDA_SUCCESS) {
			return false;
		}
	}

	// upscale
	auto quality = (NVSDK_NGX_VSR_QualityLevel)g_quality.value;
	if (!g_upscaler.upscale(src_w, src_h, tex, dst_w, dst_h, surf, quality)) {
		return false;
	}

	// readback
	{
		auto dst = g_resource_pool.get_buffer(dst_w, dst_h);

		CUDA_MEMCPY2D copy{};
		copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
		copy.srcArray = surf_array;
		copy.srcPitch = dst_pitch;
		copy.dstMemoryType = CU_MEMORYTYPE_HOST;
		copy.dstHost = dst;
		copy.dstPitch = dst_pitch;
		copy.WidthInBytes = dst_pitch;
		copy.Height = dst_h;
		if (cuMemcpy2D(&copy) != CUDA_SUCCESS) {
			return false;
		}

		video->set_image_data(dst, dst_w, dst_h);
	}

	return true;
}

//---------------------------------------------------------------------
//	プラグインDLL初期化関数 (未定義なら呼ばれません)
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) { // versionは本体のバージョン番号
	// cuda の初期化を func_proc_video と同じスレッドで行う必要があるのでここで初期化しない。
	return true;
}

//---------------------------------------------------------------------
//	フィルタ構造体のポインタを渡す関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) FILTER_PLUGIN_TABLE* GetFilterPluginTable(void) {
	static FILTER_PLUGIN_TABLE filter_plugin_table = {
		FILTER_PLUGIN_TABLE::FLAG_VIDEO, //	フラグ
		L"RTX VSR",							// プラグインの名前
		nullptr,									// ラベルの初期値 (nullptrならデフォルトのラベルになります)
		L"RTX VSR for AviUtl",	// プラグインの情報
		g_items,											// 設定項目の定義 (FILTER_ITEM_XXXポインタを列挙してnull終端したリストへのポインタ)
		func_proc_video,								// 画像フィルタ処理関数へのポインタ (FLAG_VIDEOが有効の時のみ呼ばれます)
		nullptr, // 音声フィルタ処理関数へのポインタ (FLAG_AUDIOが有効の時のみ呼ばれます)
	};

	return &filter_plugin_table;
}