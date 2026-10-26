#pragma once
#include "methods.hpp"
#include <d3d11_1.h>
#include <chrono>
#include <utility>
#include <type_traits>

using high_resolution_clock = std::chrono::time_point<std::chrono::high_resolution_clock>;

// enum to set the MSAA mode used by the render target view, and depth stencil view. Set this to HZBCuller_MSAA_MODE_NONE when you are using FXAA, or TAA
enum HZBCuller_MSAA_MODE {
	HZBCuller_MSAA_MODE_NONE = 1, // No MSAA
	HZBCuller_MSAA_MODE_2X = 2, // 2X MSAA
	HZBCuller_MSAA_MODE_4X = 4, // 4X MSAA
	HZBCuller_MSAA_MODE_8X = 8 // 8X MSAA
};

// enum to set how MSAA is performed by the GPU regarding the render target view, and depth stencil view. Set this to HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT when you are using FXAA, or TAA
enum HZBCuller_MSAA_SETTINGS {
	HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT = 0, // Sets MSAA to the hardware deffault at the discretion of your GPU manufacturer (eg. Intel, AMD, NVIDIA)
	HZBCuller_MSAA_SETTING_HARDWARE_MICROSOFT_STANDARDIZED = D3D11_STANDARD_MULTISAMPLE_PATTERN, // Sets MSAA to a Microsoft default, so the final image looks the exact same no matter which GPU manufacturer and driver you are using.
	HZBCuller_MSAA_SETTING_HARDWARE_INVISIBLE = D3D11_CENTER_MULTISAMPLE_PATTERN, // Disables visual MSAA, but leaves it on in the background so depth sampling with smoothed edges can occur
	HZBCuller_MSAA_SETTING_INTEL_PROPRIETARY_BANDWIDTH_SAVING = 1, // Proprietary to Intel GPUs, this cuts down on the bandwidth that onboard GPUs use increasing performance and efficiency
	HZBCuller_MSAA_SETTING_NVIDIA_PROPRIETARY_CSAA_COUNT4_STANDARD = 0, // Same thing as HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT, while using 4X MSAA for NVIDIA
	HZBCuller_MSAA_SETTING_NVIDIA_PROPRIETARY_CSAA_COUNT4_8XCSAA = 1, // Turns 4X MSAA into 8X NVIDIA CSAA
	HZBCuller_MSAA_SETTING_NVIDIA_PROPRIETARY_CSAA_COUNT4_16XCSAA = 2, // Turns 4X MSAA into 16X NVIDIA CSAA
	HZBCuller_MSAA_SETTING_NVIDIA_PROPRIETARY_CSAA_COUNT8_STANDARD = 0, // Same thing as HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT, while using 8X MSAA for NVIDIA
	HZBCuller_MSAA_SETTING_NVIDIA_PROPRIETARY_CSAA_COUNT8_8XQMSAA = 1, // NVIDIA 8xQ MSAA (higher quality 8x MSAA for NVIDIA GPUs)
	HZBCuller_MSAA_SETTING_NVIDIA_PROPRIETARY_CSAA_COUNT8_16XQCSAA = 2, // Highest quality NVIDIA MSAA, with increased quality 8x CSAA
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT2_STANDARD = 0, // Same thing as HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT, while using 2X MSAA for AMD
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT2_2f4xEQAA = 1, // Turns 2X MSAA into AMD 2f4x EQAA
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT4_STANDARD = 0, // Same thing as HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT, while using 4X MSAA for AMD
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT4_4f8xEQAA = 1, // Turns 4X MSAA into AMD 4f8x EQAA
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT4_4f16xEQAA = 2, // Turns 4X MSAA into AMD 4f16x EQAA
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT8_STANDARD = 0, // Same thing as HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT, while using 8X MSAA for AMD
	HZBCuller_MSAA_SETTING_AMD_PROPRIETARY_EQAA_COUNT8_8f16xEQAA = 1 // Turns 8X MSAA into AMD 8f16x EQAA
};

struct FPSReturn {
	bool second;
	unsigned long long frames_passed;
};

class HZBCuller {
private:
	HZBCuller_methods<ID3D11Device1, ID3D11DeviceContext1, IDXGIFactory> methods;
	ComPtr<IDXGISwapChain> swapchain;
	ComPtr<ID3D11RenderTargetView> RTV;
	ComPtr<ID3D11Texture2D> DSV_tex;
	ComPtr<ID3D11DepthStencilView> DSV;
	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
	ComPtr<ID3D11ShaderResourceView> HIZ_full_SRV;
	ComPtr<ID3D11Texture2D> HIZ_buffer_texture;
	ComPtr<ID3D11Texture2D> HIZ_buffer_texture_B;
	std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs;
	std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs;
	ComPtr<ID3D11Buffer> dimensions_buffer;
	ComPtr<ID3D11Buffer> indirect_buf;
	ComPtr<ID3D11ShaderResourceView> status_buffer_SRV;
	ComPtr<ID3D11UnorderedAccessView> status_buffer_UAV;
	std::vector<downscale_return_structure> shader_data;
	unsigned int width;
	unsigned int height;

public:
	HZBCuller(ComPtr<ID3D11Device1> device_in, ComPtr<ID3D11DeviceContext1> ctx_in, ComPtr<IDXGIFactory> factory_in, unsigned int width_in, unsigned int height_in, HWND window_in, bool solid, D3D11_CULL_MODE cull_mode, bool CCW) {
		HZBCuller_methods<ID3D11Device1, ID3D11DeviceContext1, IDXGIFactory> methods_create(device_in, ctx_in, factory_in, width_in, height_in, window_in);
		methods = methods_create;
		width = width_in;
		height = height_in;
		methods.misc_setup(width, height, solid ? D3D11_FILL_SOLID : D3D11_FILL_WIREFRAME, cull_mode, CCW);
	}

	void create_painting_canvases(HZBCuller_MSAA_MODE count, HZBCuller_MSAA_SETTINGS quality) {
		swapchain_data swapchain_data_ = methods.create_swapchain(count, quality);
		swapchain = swapchain_data_.swapchain;
		RTV = swapchain_data_.RTV;
		depth_stencil_data DSV_data = methods.create_depth_stencil(count, quality);
		DSV_tex = DSV_data.DSV_tex;
	    DSV = DSV_data.DSV;
		mip_chain_data mip_chain = methods.create_HIZ_mip_chain_resources(count, quality);
		HIZ_full_SRV = mip_chain.HIZ_full_SRV;
		HIZ_buffer_texture = mip_chain.HIZ_texture;
		HIZ_buffer_texture_B = mip_chain.HIZ_texture_B;
		HIZ_buffer_texture_SRVs = mip_chain.HIZ_buffer_texture_SRVs;
		HIZ_buffer_texture_UAVs = mip_chain.HIZ_buffer_texture_UAVs;
	}

	void create_buffers(unsigned int element_width, unsigned int triangle_count, void* data) {
		vertex_buffer_SRV = methods.create_vertex_buffer(element_width, triangle_count, data);
		dimensions_buffer = methods.create_dimensions_buffer();
		indirect_buf = methods.create_indirect_buffer(triangle_count);
		status_buffer_data status_buffer = methods.create_status_buffer(sizeof(unsigned int), triangle_count);
		status_buffer_SRV = status_buffer.status_buffer_SRV;
		status_buffer_UAV = status_buffer.status_buffer_UAV;
	}

	void return_shaders(bool debug) {
		shader_data = downscale_parser(generate_downscale_shader_lines(width, height));
		methods.return_shaders(shader_data, debug ? true : false);
	}

	void draw(unsigned int num_tris, unsigned int numthreadsX, float* clear_color, unsigned int width, unsigned int height) {
		unsigned int threadgroups_X = (num_tris + (numthreadsX - 1)) / numthreadsX;
		unsigned int num_mips = (unsigned int)floor(log2((float)min(width, height))) + 1u;
		std::vector<ID3D11ShaderResourceView*> render_SRVs = { vertex_buffer_SRV.Get(), status_buffer_SRV.Get() };
		std::vector<ID3D11ShaderResourceView*> cull_SRVs = { HIZ_full_SRV.Get(), vertex_buffer_SRV.Get() };

		for (unsigned int i = 0; i < HIZ_buffer_texture_SRVs.size(); i++) {
			render_SRVs.push_back(HIZ_buffer_texture_SRVs[i].Get());
		}

		methods.draw(vertex_buffer_SRV, status_buffer_UAV, threadgroups_X, indirect_buf, status_buffer_SRV, RTV, DSV, clear_color, HIZ_buffer_texture, HIZ_buffer_texture_B, DSV_tex, num_mips, HIZ_buffer_texture_SRVs, HIZ_buffer_texture_UAVs, shader_data, cull_SRVs, dimensions_buffer);
		swapchain->Present(1, 0);
	}

	void draw_reference_nocull(unsigned int num_triangles, float* clear_color) {
		methods.draw_reference_nocull(vertex_buffer_SRV, RTV, DSV, num_triangles, clear_color);
		swapchain->Present(1, 0);
	}
};

class FPSCounter {
private:
	high_resolution_clock now;
	unsigned long long frame_counter = 0;
public:
	FPSCounter() {
		now = std::chrono::high_resolution_clock::now();
	}

	FPSReturn check_fps() {
		frame_counter++;
		high_resolution_clock later = std::chrono::high_resolution_clock::now();
		FPSReturn return_fps = { false, 0 };
		if (std::chrono::duration<double, std::milli>(later - now).count() >= 1000.0f) {
			now = later;
			return_fps = { true, frame_counter };
			frame_counter = 0;
		}

		return return_fps;
	}
};

struct device_and_ctx {
	ComPtr<ID3D11Device1> device;
	ComPtr<ID3D11DeviceContext1> ctx;
};

void D3D11_1CreateDevice(
	_In_opt_ IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    _In_reads_opt_( FeatureLevels ) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
	_Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel, 
	REFIID riidDev,
	_COM_Outptr_ void** ppDevice,
	REFIID riidCtx,
	_COM_Outptr_ void** ppDeviceContext,
	UINT FactoryFlags, 
	REFIID riidFac, 
	_COM_Outptr_ void** ppFactory) {
	
	ComPtr<ID3D11Device> base_device;
	ComPtr<ID3D11DeviceContext> base_ctx;

	if (FAILED(D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, &base_device, pFeatureLevel, &base_ctx)));

	base_device->QueryInterface(riidDev, ppDevice);
	base_ctx->QueryInterface(riidCtx, ppDeviceContext);

	CreateDXGIFactory2(FactoryFlags, riidFac, ppFactory);
}
