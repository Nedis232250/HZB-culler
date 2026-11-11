#include <iostream>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <chrono>
#include <algorithm>
#include "utils.hpp"
#include "HZBCuller.hpp"
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

const static LPCWSTR lp_class_name = L"window_class";
static bool running = true;
HWND window;
unsigned int width = 1920;
unsigned int height = 1080;
unsigned long long num_triangles = 5000000;
using namespace Microsoft::WRL;
bool created = false;

ComPtr<IDXGISwapChain> swapchain;
ComPtr<ID3D11RenderTargetView> RTV;
ComPtr<ID3D11Texture2D> DSV_tex;
ComPtr<ID3D11DepthStencilView> DSV;
ComPtr<ID3D11DeviceContext> ctx;
ComPtr<IDXGIFactory> factory;
ComPtr<ID3D11Device> device;
HZBCuller hzb_culler;
DSV_return DSV_return_resize;

LRESULT w_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	switch (msg) {
	case WM_QUIT:
	case WM_DESTROY: {
		PostQuitMessage(0);
		running = false;
		break;
	}
	case WM_EXITSIZEMOVE: {
		if (created) {
			std::vector<unsigned int> dimensions;
			dimensions = client_dimensions(window);
			width = std::clamp(dimensions.at(0), 0u, 1920u);
			height = std::clamp(dimensions.at(1), 0u, 1080u);

			if (swapchain) swapchain.Reset();
			if (RTV) RTV.Reset();

			ctx->OMSetRenderTargets(0, nullptr, nullptr);
			RTV.Reset();
			swapchain.Reset();
			ctx->Flush();

			swapchain_return swapchain_rtv = create_swapchain(width, height, window, factory, device);
			swapchain = swapchain_rtv.swpachain;
			RTV = swapchain_rtv.RTV;

			if (DSV) DSV.Reset();
			if (DSV_tex) DSV_tex.Reset();

			DSV_return_resize = create_DSV(width, height, device);
			DSV_tex = DSV_return_resize.DSV_tex;
			DSV = DSV_return_resize.DSV;

			std::cout << "width " << width << " height " << height << "\n";

			hzb_culler->reset_HZB_mips(width, height);

			D3D11_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(width);
			viewport.Height = static_cast<float>(height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			ctx->RSSetViewports(1, &viewport);

			hzb_culler->create_reset_dimensions_buffer(width, height);
		}

		break;
	}
	}

	return DefWindowProcW(wnd, msg, wparam, lparam);
}

int WinMain([[maybe_unused]] HINSTANCE h_instance, [[maybe_unused]] HINSTANCE p_instance, [[maybe_unused]] LPSTR lp_cmdln, int n_cmd_show) {
	std::string ps_src = R"(
		struct VSOutput {
			float4 position : SV_Position;
			uint color : TEXCOORD0;
		};

		float4 unpackARGB(uint packed) {
			float4 color;
			color.b = float( packed        & 0xFF); // Blue is at the bottom
			color.g = float((packed >> 8)  & 0xFF); // Green is in the middle-low
			color.r = float((packed >> 16) & 0xFF); // Red is in the middle-high
			color.a = float((packed >> 24) & 0xFF); // Alpha is at the top
			return color / 255.0f;
		}

		float4 main(VSOutput input) : SV_TARGET {
			return float4(0.75 * unpackARGB(input.color).xyz + 0.125, 1.0f);
		}
	)";

	SetProcessDPIAware();
	attach_console();

	bool debug{}, occlusion{};
	unsigned short threads_used = static_cast<unsigned short>(find_cores());

	std::string answer;

	while (true) {
		std::cout << "Set occlusion status (Y/y for yes, N/n for no):\n";
		std::cin >> answer;

		if (answer == "Y" || answer == "y") {
			std::cout << "\nOcclusion status set to debug.\n";
			occlusion = true;
			break;
		}
		else if (answer == "N" || answer == "n") {
			std::cout << "\nOcclusion status set to not debug.\n";
			occlusion = false;
			break;
		}
		else {
			std::cout << "\nInvalid answer.\n";
		}
	}

	std::cout << "\n";

	while (true) {
		std::cout << "Set multithreading count (D/d for default):\n";
		std::cin >> answer;

		if (answer == "D" || answer == "d") {
			std::cout << "\nMultithreading status set to default: " << threads_used << " cores.\n";
			break;
		}
		else if (std::isdigit(answer.at(0))) {
			std::cout << "\nMultithreading status set to " << answer.at(0) << " cores.\n";
			threads_used = static_cast<unsigned short>(std::isdigit(answer.at(0)));
			break;
		}
		else {
			std::cout << "\nInvalid answer.\n";
		}
	}

	std::cout << "\n";

	while (true) {
		std::cout << "Set debug status (Y/y for yes, N/n for no):\n";
		std::cin >> answer;

		if (answer == "Y" || answer == "y") {
			std::cout << "\nDebug status set to debug.\n";
			debug = true;
			break;
		}
		else if (answer == "N" || answer == "n") {
			std::cout << "\nDebug status set to not debug.\n";
			debug = false;
			break;
		}
		else {
			std::cout << "\nInvalid answer.\n";
		}
	}

	std::cout << "\n";

	unsigned int flags0_device_creation = 0;
	unsigned int flags0_factory_creation = 0;
	if (debug) {
		flags0_device_creation = D3D11_CREATE_DEVICE_DEBUG;
		flags0_factory_creation = DXGI_CREATE_FACTORY_DEBUG;
	}

	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags0_device_creation, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx);
	CreateDXGIFactory2(flags0_factory_creation, IID_PPV_ARGS(&factory));
	create_wndclass(lp_class_name, w_proc);
	window = CreateWindowExW(0, lp_class_name, L"HI-Z occlusion culler", WS_OVERLAPPEDWINDOW, 0, 0, width, height, 0, 0, 0, 0);
	ShowWindow(window, n_cmd_show);

	std::vector<unsigned int> compressed_vertices = compress_vertices(load_vertices("hello.world", num_triangles, threads_used));
	std::cout << "triangles loaded: " << compressed_vertices.size() / 9 << "\n";

	hzb_culler.emplace(device, ctx, debug, num_triangles);
	
	ComPtr<ID3D11Buffer> vertex_buffer;
	D3D11_BUFFER_DESC vertex_buffer_desc = {};
	vertex_buffer_desc.ByteWidth = static_cast<unsigned int>(sizeof(float)) * (compressed_vertices.size());
	vertex_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	vertex_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	vertex_buffer_desc.CPUAccessFlags = 0;
	vertex_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	vertex_buffer_desc.StructureByteStride = static_cast<unsigned int>(sizeof(float));

	D3D11_SUBRESOURCE_DATA vertex_buffer_data = {};
	vertex_buffer_data.pSysMem = compressed_vertices.data();

	device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_data, &vertex_buffer);

	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC vertex_buffer_SRV_desc = {};
	vertex_buffer_SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
	vertex_buffer_SRV_desc.Buffer.FirstElement = 0;
	vertex_buffer_SRV_desc.Buffer.ElementWidth = static_cast<unsigned int>(sizeof(float));
	vertex_buffer_SRV_desc.Buffer.NumElements = compressed_vertices.size();
	vertex_buffer_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

	device->CreateShaderResourceView(vertex_buffer.Get(), &vertex_buffer_SRV_desc, &vertex_buffer_SRV);
	hzb_culler->create_HZB_mips(width, height);
	hzb_culler->create_HZB_shaders(width, height);

	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3DBlob> shader_blob;
	ComPtr<ID3DBlob> error_blob;
	HRESULT hr;

	hr = D3DCompile(ps_src.c_str(), strlen(ps_src.c_str()), nullptr, nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}
	}

	device->CreatePixelShader(shader_blob.Get()->GetBufferPointer(), shader_blob.Get()->GetBufferSize(), nullptr, &ps);

	KILLCOMPTRREF
		DSV_return DSV_return_ = create_DSV(width, height, device);
		DSV_tex = DSV_return_.DSV_tex;
		DSV = DSV_return_.DSV;

		swapchain_return swapchain_rtv = create_swapchain(width, height, window, factory, device);
		swapchain = swapchain_rtv.swpachain;
		RTV = swapchain_rtv.RTV;
	EXITKILL

	DSV_return_resize = { DSV_tex, DSV }; 
	hzb_culler->create_reset_dimensions_buffer(width, height);

	FPSCounter fps_counter{};

	float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	created = true;

	MSG msg = {};

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	ctx->RSSetViewports(1, &viewport);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D11_RASTERIZER_DESC rs_desc = {};
	rs_desc.FillMode = D3D11_FILL_SOLID;
	rs_desc.CullMode = D3D11_CULL_NONE;  // disable to test
	rs_desc.FrontCounterClockwise = false;

	ComPtr<ID3D11RasterizerState> rs;
	device->CreateRasterizerState(&rs_desc, &rs);

	ctx->RSSetState(rs.Get());

	if (occlusion) {
		while (running) {
			send_message(msg);
			unsigned int num_mips = static_cast<unsigned int>(ceil(log2(static_cast<float>(min(width, height)))));
			unsigned int dispatchX = (static_cast<unsigned int>(num_triangles) + 127) / 128;

			std::vector<ID3D11ShaderResourceView*> render_SRVs = hzb_culler->cull(vertex_buffer_SRV, dispatchX, ps, clear_color, width, height, DSV_return_resize, RTV);

			ctx->VSSetShaderResources(0, static_cast<unsigned int>(render_SRVs.size()), render_SRVs.data());
			ctx->PSSetShader(ps.Get(), nullptr, 0);
			ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());

			hzb_culler->draw_indirect();

			swapchain->Present(0, 0);

			FPSReturn fps = fps_counter.check_fps();
			if (fps.second) SetWindowTextA(window, std::to_string(fps.frames_passed).c_str());
		}
	}
	else {
		while (running) {
			send_message(msg);
			hzb_culler->set_default_vs();
			ctx->VSSetShaderResources(0, 1, vertex_buffer_SRV.GetAddressOf());
			ctx->PSSetShader(ps.Get(), nullptr, 0);
			ctx->PSSetShaderResources(0, 0, nullptr);
			ctx->ClearRenderTargetView(RTV.Get(), clear_color);
			ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
			ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
			ctx->Draw(num_triangles * 3, 0);

			swapchain->Present(1, 0);

			FPSReturn fps = fps_counter.check_fps();
			if (fps.second) SetWindowTextA(window, std::to_string(fps.frames_passed).c_str());
		}
	}

	return 0;
}
