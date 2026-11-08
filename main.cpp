#include <iostream>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <chrono>
#include <algorithm>
#include "utils.hpp"
#include "functiongenerator.hpp"
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
HZB_Culler hzb_culler;
using namespace Microsoft::WRL;
bool created = false;

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
				std::vector<unsigned int> new_dimensions = hzb_culler->resize();
				width = std::clamp(new_dimensions.at(0), 0u, 1920u);
				height = std::clamp(new_dimensions.at(1), 0u, 1080u);
			}

			break;

			// ... rest of your WndProc handler ...
		}
		case WM_SYSCOMMAND: {
			// Mask out the 4 low-order bits, as they are used internally by Windows
			switch (wparam & 0xFFF0) {
				case SC_MAXIMIZE:
				case SC_RESTORE: {
					if (created) {
						std::vector<unsigned int> new_dimensions = hzb_culler->resize();
						width = std::clamp(new_dimensions.at(0), 0u, 1920u);
						height = std::clamp(new_dimensions.at(1), 0u, 1080u);
					}
					break;
				}
			}
			break;
		}
	}

	return DefWindowProcW(wnd, msg, wparam, lparam);
}

int WinMain([[maybe_unused]] HINSTANCE h_instance, [[maybe_unused]] HINSTANCE p_instance, [[maybe_unused]] LPSTR lp_cmdln, int n_cmd_show) {
	SetProcessDPIAware();
	attach_console();

	bool debug{}, occlusion {};
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

	create_wndclass(lp_class_name, w_proc);
	window = CreateWindowExW(0, lp_class_name, L"HI-Z occlusion culler", WS_OVERLAPPEDWINDOW, 0, 0, width, height, 0, 0, 0, 0);
	ShowWindow(window, n_cmd_show);

	std::vector<unsigned int> compressed_vertices = compress_vertices(load_vertices("hello.world", num_triangles, threads_used));

	hzb_culler.emplace(width, height, window, true, D3D11_CULL_NONE, false);
	hzb_culler->create_buffers(compressed_vertices.size(), num_triangles, compressed_vertices.data());
	hzb_culler->create_mip_chain();
	hzb_culler->create_shaders();

	float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	created = true;

	MSG msg = {};

	if (occlusion) {
		while (running) {
			send_message(msg);
			hzb_culler->draw_occlusion(num_triangles, 128, clear_color, ", Width: " + std::to_string(width) + ", Height: " + std::to_string(height) + ".", true, false);
		}
	} else {
		while (running) {
			send_message(msg);
			hzb_culler->draw_no_occlusion_benchmark(clear_color, num_triangles, ", Width: " + std::to_string(width) + ", Height: " + std::to_string(height) + ".", true, false);
		}
	}

	return 0;
}
