#include <iostream>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include "utils.hpp"
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <chrono>
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

const static LPCWSTR lp_class_name = L"window_class";
static LPCWSTR window_name = L"HI-Z occlusion culler";
static bool running = true;
static HWND window;
static unsigned int width = 1920;
static unsigned int height = 1080;
unsigned long long num_triangles = 5000050;
unsigned int num_mips = (unsigned int)floor(log2((float)min(width, height))) + 1u;
std::vector<unsigned int> dimensions = { width, height, (unsigned int)ceil(sqrt(num_triangles) / 8) };
constexpr static unsigned int minus_one = -1;
constexpr static unsigned int zero = 0;

//
// CONSTANTS
//

constexpr static bool debug = false;
constexpr static bool console = false;
constexpr static bool occlusion = true;
constexpr static bool generate_shaders = false;

using namespace Microsoft::WRL;

//
// Window Proc
//

LRESULT w_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	switch (msg) {
	case WM_QUIT: {
		PostQuitMessage(0);
		running = false;
		break;
	};
	case WM_DESTROY: {
		PostQuitMessage(0);
		running = false;
		break;
	};
	}

	return DefWindowProcW(wnd, msg, wparam, lparam);
}

//
// WinMain, window and vertex data initalization
//

int WinMain(HINSTANCE h_instance, HINSTANCE p_instance, LPSTR lp_cmdln, int n_cmd_show) {
	SetProcessDPIAware();
	if (console) attach_console();
	if (generate_shaders) {
		char exe_path[MAX_PATH];
		GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
		std::string exe_dir = std::string(exe_path).substr(0, std::string(exe_path).find_last_of("\\/"));
		std::string cmd = "python3 main.py " +
			std::to_string(width) + " " + std::to_string(height) + exe_dir;
		system(cmd.c_str());
	}

	std::vector<downscale_return_structure> shader_data = downscale_parser("downscaleshadercache.txt");

	WNDCLASS w_class = {};
	w_class.lpszClassName = lp_class_name;
	w_class.hInstance = h_instance;
	w_class.style = CS_HREDRAW | CS_VREDRAW;
	w_class.lpfnWndProc = w_proc;

	RegisterClassW(&w_class);

	window = CreateWindowExW(0, lp_class_name, window_name, WS_OVERLAPPEDWINDOW, 0, 0, width, height, 0, 0, 0, 0);
	ShowWindow(window, n_cmd_show);

	std::vector<float> vertices = load_vertices("hello.world", num_triangles);
	std::vector<unsigned int> compressed_vertices = compress_vertices(vertices);

	if (debug && console) {
		for (const auto& vertex : compressed_vertices) {
			std::cout << vertex << "\n";
		}
	}

	//
	// Create device, dxgi factory and swapchain
	//

	ComPtr<ID3D12Debug> debug;
	if (debug) {
		D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
	}

	ComPtr<ID3D12Device> device;
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));
}
