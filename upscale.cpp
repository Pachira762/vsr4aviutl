#include "pch.h"
#include "upscale.h"
#include "common.h"

namespace {
	void safe_release_array(CUarray& array) {
		if (array) {
			cuArrayDestroy(array);
			array = CUarray{};
		}
	}

	void safe_release_tex(CUtexObject& tex) {
		if (tex) {
			cuTexObjectDestroy(tex);
			tex = CUtexObject{};
		}
	}

	void safe_release_surf(CUsurfObject& surf) {
		if (surf) {
			cuSurfObjectDestroy(surf);
			surf = CUsurfObject{};
		}
	}

	std::tuple<int, int> get_array_size(CUarray array) {
		CUDA_ARRAY_DESCRIPTOR desc{};
		if (array) {
			cuArrayGetDescriptor(&desc, array);
		}
		return { (int)desc.Width, (int)desc.Height };
	}
}

Upscaler::~Upscaler() {
	safe_release_surf(out_surf);
	safe_release_array(out_array);
	safe_release_tex(in_tex);
	safe_release_array(in_array);

	if (vsr_feature) {
		NVSDK_NGX_CUDA_ReleaseFeature(vsr_feature);
	}

	if (ngx_parameters) {
		NVSDK_NGX_CUDA_DestroyParameters(ngx_parameters);
	}

	if (context) {
		// wait to release ngx resources
		cuCtxSynchronize();
	}

	NVSDK_NGX_CUDA_Shutdown();

	if (context) {
		cuCtxDestroy(context);
	}
}

bool Upscaler::init() {
	// init cuda
	{
		CUresult result = cuInit(0);
		RETURN_IF_CUDA_FAILED(result, L"CUDAの初期化に失敗しました");

		result = cuDeviceGet(&device, 0);
		RETURN_IF_CUDA_FAILED(result, L"CUDAデバイスの取得に失敗しました");

		result = cuCtxCreate(&context, 0, device);
		RETURN_IF_CUDA_FAILED(result, L"CUDAコンテキストの取得に失敗しました");
	}

	// init vsr
	{
		NVSDK_NGX_Result result = NVSDK_NGX_CUDA_Init(0, L".");
		RETURN_IF_NGX_FAILED(result, L"VSRの初期化に失敗しました");

		result = NVSDK_NGX_CUDA_GetCapabilityParameters(&ngx_parameters);
		RETURN_IF_NGX_FAILED(result, L"VSRの初期化に失敗しました");

		int vsr_available = 0;
		result = ngx_parameters->Get(NVSDK_NGX_Parameter_VSR_Available, &vsr_available);
		RETURN_IF_NGX_FAILED(result, L"VSRの初期化に失敗しました");

		if (!vsr_available) {
			DLOG(error, L"VSRに対応していません");
			return false;
		}

		NVSDK_NGX_CUDA_VSR_Create_Params create_params{};
		create_params.InCUContext = context;
		result = NGX_CUDA_CREATE_VSR(&vsr_feature, ngx_parameters, &create_params);
		RETURN_IF_NGX_FAILED(result, L"VSRの初期化に失敗しました");
	}

	return true;
}

bool Upscaler::upscale(const UpscaleInputs& inputs) {
	auto [in_width, in_height] = get_array_size(in_array);
	if (in_width < inputs.src_width || in_height < inputs.src_height) {
		safe_release_tex(in_tex);
		safe_release_array(in_array);

		in_width = std::max(in_width, inputs.src_width);
		in_height = std::max(in_height, inputs.src_height);

		CUDA_ARRAY_DESCRIPTOR array_desc{};
		array_desc.Width = in_width;
		array_desc.Height = in_height;
		array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		array_desc.NumChannels = 4;
		CUresult result = cuArrayCreate(&in_array, &array_desc);
		RETURN_IF_CUDA_FAILED(result, L"リソースの確保に失敗しました");

		CUDA_RESOURCE_DESC res_desc{};
		res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
		res_desc.res.array.hArray = in_array;

		CUDA_TEXTURE_DESC tex_desc{};
		tex_desc.addressMode[0] = tex_desc.addressMode[1] = tex_desc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;
		tex_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
		tex_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;
		result = cuTexObjectCreate(&in_tex, &res_desc, &tex_desc, nullptr);
		RETURN_IF_CUDA_FAILED(result, L"リソースの確保に失敗しました");
	}

	auto [out_width, out_height] = get_array_size(out_array);
	if (out_width < inputs.target_width || out_height < inputs.target_height) {
		safe_release_surf(out_surf);
		safe_release_array(out_array);

		out_width = std::max(out_width, inputs.target_width);
		out_height = std::max(out_height, inputs.target_height);

		CUDA_ARRAY_DESCRIPTOR array_desc{};
		array_desc.Width = out_width;
		array_desc.Height = out_height;
		array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		array_desc.NumChannels = 4;
		CUresult result = cuArrayCreate(&out_array, &array_desc);
		RETURN_IF_CUDA_FAILED(result, L"リソースの確保に失敗しました");

		CUDA_RESOURCE_DESC res_desc{};
		res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
		res_desc.res.array.hArray = out_array;
		result = cuSurfObjectCreate(&out_surf, &res_desc);
		RETURN_IF_CUDA_FAILED(result, L"リソースの確保に失敗しました");
	}

	// upload
	{
		CUDA_MEMCPY2D copy{};
		copy.srcMemoryType = CU_MEMORYTYPE_HOST;
		copy.srcHost = inputs.src;
		copy.srcPitch = 4 * inputs.src_width;
		copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
		copy.dstArray = in_array;
		copy.WidthInBytes = 4 * inputs.src_width;
		copy.Height = inputs.src_height;
		CUresult result = cuMemcpy2D(&copy);
		RETURN_IF_CUDA_FAILED(result, L"リソースのアップロードに失敗しました");
	}

	// upscale
	{
		NVSDK_NGX_CUDA_VSR_Eval_Params params{};
		params.pInput = &in_tex;
		params.pOutput = &out_surf;
		params.InputSubrectSize.Width = inputs.src_width;
		params.InputSubrectSize.Height = inputs.src_height;
		params.OutputSubrectSize.Width = inputs.target_width;
		params.OutputSubrectSize.Height = inputs.target_height;
		params.QualityLevel = (NVSDK_NGX_VSR_QualityLevel)inputs.quality;
		NVSDK_NGX_Result result = NGX_CUDA_EVALUATE_VSR(vsr_feature, ngx_parameters, &params);
		RETURN_IF_NGX_FAILED(result, L"VSRの実行に失敗しました");
	}

	// readback
	{
		CUDA_MEMCPY2D copy{};
		copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
		copy.srcArray = out_array;
		copy.dstMemoryType = CU_MEMORYTYPE_HOST;
		copy.dstHost = inputs.target;
		copy.dstPitch = 4 * inputs.target_width;
		copy.WidthInBytes = 4 * inputs.target_width;
		copy.Height = inputs.target_height;
		CUresult result = cuMemcpy2D(&copy);
		RETURN_IF_CUDA_FAILED(result, L"結果の取得に失敗しました");
	}

	return true;
}
