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
unsigned long long num_triangles = 1000000;
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
			return float4(unpackARGB(input.color).xyz, 1.0f);
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

	D3D11_SUBRESOURCE_DATA vertex_buffer_data = {};
	vertex_buffer_data.pSysMem = compressed_vertices.data();
	
	ComPtr<ID3D11Buffer> vertex_buffer;
	D3D11_BUFFER_DESC vertex_buffer_desc = {};
	vertex_buffer_desc.ByteWidth = static_cast<unsigned int>(sizeof(float)) * (compressed_vertices.size());
	vertex_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	vertex_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	vertex_buffer_desc.CPUAccessFlags = 0;
	vertex_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	vertex_buffer_desc.StructureByteStride = static_cast<unsigned int>(sizeof(float));

	//device->CreateBuffer(&vertex_buffer_desc, nullptr, &vertex_buffer);
	device->CreateBuffer(&vertex_buffer_desc, nullptr, &vertex_buffer);

	ComPtr<ID3D11Buffer> vertex_staging_buffer;
	D3D11_BUFFER_DESC vertex_staging_desc = vertex_buffer_desc;
	vertex_staging_desc.Usage = D3D11_USAGE_DYNAMIC;
	vertex_staging_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	vertex_staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	//device->CreateBuffer(&vertex_staging_desc, &vertex_buffer_data, &vertex_staging_buffer);
	device->CreateBuffer(&vertex_staging_desc, &vertex_buffer_data, &vertex_staging_buffer);

	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC vertex_buffer_SRV_desc = {};
	vertex_buffer_SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
	vertex_buffer_SRV_desc.Buffer.FirstElement = 0;
	vertex_buffer_SRV_desc.Buffer.ElementWidth = static_cast<unsigned int>(sizeof(float));
	vertex_buffer_SRV_desc.Buffer.NumElements = compressed_vertices.size();
	vertex_buffer_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

	device->CreateShaderResourceView(vertex_buffer.Get(), &vertex_buffer_SRV_desc, &vertex_buffer_SRV);

	ComPtr<ID3D11ShaderResourceView> vertex_staging_SRV;
	device->CreateShaderResourceView(vertex_staging_buffer.Get(), &vertex_buffer_SRV_desc, &vertex_staging_SRV);
	
	ComPtr<ID3D11UnorderedAccessView> vertex_buffer_UAV;
	D3D11_UNORDERED_ACCESS_VIEW_DESC vertex_buffer_UAV_desc = {};
	vertex_buffer_UAV_desc.Buffer.FirstElement = 0;
	vertex_buffer_UAV_desc.Buffer.Flags = 0;
	vertex_buffer_UAV_desc.Buffer.NumElements = compressed_vertices.size();
	vertex_buffer_UAV_desc.Format = DXGI_FORMAT_UNKNOWN;
	vertex_buffer_UAV_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

	device->CreateUnorderedAccessView(vertex_buffer.Get(), &vertex_buffer_UAV_desc, &vertex_buffer_UAV);

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

	ComPtr<ID3D11ComputeShader> cs;
	std::string cs_src = R"(
		StructuredBuffer<uint> vertices_in : register(t0);    // vertex_staging_SRV
		RWStructuredBuffer<uint> vertices_out : register(u0); // vertex_buffer_UAV

		#define GROUP_W 32
		#define GROUP_H 8
		#define THREADS_PER_GROUP (GROUP_W * GROUP_H)   // 128
		#define MAX_GROUPS_X 65535
		#define pi 3.14159265
		#define ax (pi / 4)
		#define ay 0
		#define az 0

		#define cam_dist 3.0f   // camera distance from the rotation pivot; must be > max_r
		#define max_r    1.5f   // largest |p.z| any rotation can produce

		float2 unpack_uint_to_2float16(uint packed) {
			float2 unpacked;
			unpacked.x = f16tof32(packed & 0xFFFF);
			unpacked.y = f16tof32(packed >> 16);
			return unpacked;
		}

		uint pack_float2_to_half2(float2 v) {
			return f32tof16(v.x) | (f32tof16(v.y) << 16);
		}

		float3 rotate_xyz(float3 v, float rx, float ry, float rz) {
			float sx, cx, sy, cy, sz, cz;
			sincos(rx, sx, cx);
			sincos(ry, sy, cy);
			sincos(rz, sz, cz);

			// Rotate about X
			v = float3(
				v.x,
				v.y * cx - v.z * sx,
				v.y * sx + v.z * cx
			);

			// Rotate about Y
			v = float3(
				v.x * cy + v.z * sy,
				v.y,
				-v.x * sy + v.z * cy
			);

			// Rotate about Z
			v = float3(
				v.x * cz - v.y * sz,
				v.x * sz + v.y * cz,
				v.z
			);

			return v;
		}

		[numthreads(GROUP_W, GROUP_H, 1)]
		void main(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
			uint num_uints, stride;
			vertices_out.GetDimensions(num_uints, stride);

			// Recompute the host's dispatch width from the buffer size (must match the C++ formula exactly)
			uint num_vertices = num_uints / 3;
			uint total_groups = (num_vertices + THREADS_PER_GROUP - 1) / THREADS_PER_GROUP;
			uint groups_x     = min(total_groups, MAX_GROUPS_X);

			// Flatten the 2D group grid, then add the thread's slot inside its 16x8 group (0..127)
			uint group_flat  = groupID.y * groups_x + groupID.x;
			uint thread_flat = group_flat * THREADS_PER_GROUP + groupIndex;   // one thread per vertex
			uint vertex_ID   = thread_flat * 3;
			if (vertex_ID + 2 >= num_uints) return;

			float2 xy = unpack_uint_to_2float16(vertices_in[vertex_ID]);
			float z = asfloat(vertices_in[vertex_ID + 1]);

			float3 p = float3(xy, (z - 0.5f) * 0.2f); // thin slab centered on the pivot
			p = rotate_xyz(p, ax, ay, az);

			// Distance from the camera to this vertex
			float w = cam_dist + p.z;

			// Perspective: nearer -> larger, farther -> smaller and pulled toward the center
			float2 screen = p.xy * (cam_dist / w);

			// Map w to [0, 1] depth like a projection matrix (stays linear across each triangle on screen)
			const float n = cam_dist - max_r;
			const float f = cam_dist + max_r;
			float depth = (f / (f - n)) * (1.0f - n / w);

			vertices_out[vertex_ID]     = pack_float2_to_half2(screen);
			vertices_out[vertex_ID + 1] = asuint(depth);
			vertices_out[vertex_ID + 2] = vertices_in[vertex_ID + 2];
		}
	)";

	hr = D3DCompile(cs_src.c_str(), strlen(cs_src.c_str()), nullptr, nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}
	}

	device->CreateComputeShader(shader_blob.Get()->GetBufferPointer(), shader_blob.Get()->GetBufferSize(), nullptr, &cs);

	// The compute shader takes vertex_staging_SRV -> vertex_buffer_UAV

	if (occlusion) {
		while (running) {
			send_message(msg);
			unsigned int num_mips = static_cast<unsigned int>(ceil(log2(static_cast<float>(min(width, height)))));
			unsigned int dispatchX = (static_cast<unsigned int>(num_triangles) + 127) / 128;
			unsigned int num_vertices = static_cast<unsigned int>(num_triangles * 3);
			UINT total_groups = (num_vertices + 255) / 256;
			UINT groups_x = min(total_groups, 65535u);
			UINT groups_y = (total_groups + groups_x - 1) / groups_x;

			ctx->CSSetShader(cs.Get(), nullptr, 0);
			ctx->CSSetUnorderedAccessViews(0, 1, vertex_buffer_UAV.GetAddressOf(), nullptr);
			ctx->CSSetShaderResources(0, 1, vertex_staging_SRV.GetAddressOf());
			ctx->Dispatch(groups_x, groups_y, 1);

			ID3D11UnorderedAccessView* null_uav = nullptr;
			ID3D11ShaderResourceView* null_srv = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
			ctx->CSSetShaderResources(0, 1, &null_srv);

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
			unsigned int num_vertices = static_cast<unsigned int>(num_triangles * 3);
			UINT total_groups = (num_vertices + 255) / 256;
			UINT groups_x = min(total_groups, 65535u);
			UINT groups_y = (total_groups + groups_x - 1) / groups_x;

			ctx->CSSetShader(cs.Get(), nullptr, 0);
			ctx->CSSetUnorderedAccessViews(0, 1, vertex_buffer_UAV.GetAddressOf(), nullptr);
			ctx->CSSetShaderResources(0, 1, vertex_staging_SRV.GetAddressOf());
			ctx->Dispatch(groups_x, groups_y, 1);
			ID3D11UnorderedAccessView* null_uav = nullptr;
			ID3D11ShaderResourceView* null_srv = nullptr;
			ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
			ctx->CSSetShaderResources(0, 1, &null_srv);
			hzb_culler->set_default_vs();
			ctx->VSSetShaderResources(0, 1, vertex_buffer_SRV.GetAddressOf());
			ctx->PSSetShader(ps.Get(), nullptr, 0);
			ctx->PSSetShaderResources(0, 0, nullptr);
			ctx->ClearRenderTargetView(RTV.Get(), clear_color);
			ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
			ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
			ctx->Draw(num_triangles * 3, 0);

			swapchain->Present(0, 0);

			FPSReturn fps = fps_counter.check_fps();
			if (fps.second) SetWindowTextA(window, std::to_string(fps.frames_passed).c_str());
		}
	}

	return 0;
}
