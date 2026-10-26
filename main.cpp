#include <iostream>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <windows.h>
#include <chrono>
#include "utils.hpp"
#include "library.hpp"
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

const LPCWSTR lp_class_name = L"window_class";
bool running = true;
unsigned int width = 1920;
unsigned int height = 1080;
unsigned long long num_triangles = 5000000;
unsigned int flags0_device_creation = 0;
unsigned int flags0_factory_creation = 0;

constexpr bool debug = true;
constexpr bool console = true;
constexpr bool occlusion = true;

using namespace Microsoft::WRL;

LRESULT w_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	switch (msg) {
		case WM_QUIT:
		case WM_DESTROY: {
			PostQuitMessage(0);
			running = false;
			break;
		}
	}

	return DefWindowProcW(wnd, msg, wparam, lparam);
}

int WinMain(HINSTANCE h_instance, HINSTANCE p_instance, LPSTR lp_cmdln, int n_cmd_show) {
	if (console) attach_console();
	if (debug) {
		num_triangles = 100;
		flags0_device_creation = D3D11_CREATE_DEVICE_DEBUG;
		flags0_factory_creation = DXGI_CREATE_FACTORY_DEBUG;
	}
	
	SetProcessDPIAware();

	WNDCLASS w_class = {};
	w_class.lpszClassName = lp_class_name;
	w_class.lpfnWndProc = w_proc;

	RegisterClassW(&w_class);

	HWND window = CreateWindowExW(0, lp_class_name, L"HI-Z occlusion culler", WS_OVERLAPPEDWINDOW, 0, 0, width, height, 0, 0, 0, 0);
	ShowWindow(window, n_cmd_show);

	ComPtr<IDXGIFactory> factory;
	ComPtr<ID3D11Device1> device;
	ComPtr<ID3D11DeviceContext1> ctx;
	D3D11_1CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags0_device_creation, nullptr, 0, D3D11_SDK_VERSION, nullptr, IID_PPV_ARGS(&device), IID_PPV_ARGS(&ctx), flags0_factory_creation, IID_PPV_ARGS(&factory));

	std::vector<unsigned int> compressed_vertices = compress_vertices(load_vertices("hello.world", num_triangles));
	HZBCuller hzb_culler(device, ctx, factory, width, height, window, true, D3D11_CULL_NONE, false);
	hzb_culler.create_painting_canvases(HZBCuller_MSAA_MODE_NONE, HZBCuller_MSAA_SETTING_HARDWARE_DEFAULT);
	hzb_culler.create_buffers(sizeof(float), compressed_vertices.size(), compressed_vertices.data());
	hzb_culler.return_shaders(debug ? true : false);

	float clear_color[4] = { 0.2f, 0.0f, 0.0f, 1.0f };
	FPSCounter fps_counter {};
	MSG msg = {};

	if (occlusion) {
		while (running) {
			while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			hzb_culler.draw(num_triangles, 128, clear_color, width, height);
			FPSReturn fps = fps_counter.check_fps();
			if (fps.second) { SetWindowTextA(window, std::to_string(fps.frames_passed).c_str()); }
		}
	} else {
		while (running) {
			while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			hzb_culler.draw_reference_nocull(compressed_vertices.size(), clear_color);
			FPSReturn fps = fps_counter.check_fps();
			if (fps.second) SetWindowTextA(window, std::to_string(fps.frames_passed).c_str());
		}
	}

	return 0;
}
