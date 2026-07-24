#include <iostream>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <windows.h>
#include <chrono>
#include "utils.hpp"
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

constexpr static bool debug = true;
constexpr static bool console = false;
constexpr static bool occlusion = true;
constexpr static bool generate_shaders = false;
constexpr static ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
constexpr static ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };

//
//
//

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

	//
	// Load vertices from a file and compress them into a more efficient format for the GPU to read
	//

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

	unsigned int flags0_device_creation = 0;
	if (debug) {
		flags0_device_creation = D3D11_CREATE_DEVICE_DEBUG;
	}

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> ctx;
	D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags0_device_creation, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx);

	ComPtr<IDXGISwapChain> swapchain;
	ComPtr<IDXGIFactory> factory;

	unsigned int flags0_factory_creation = 0;
	if (debug) {
		flags0_factory_creation = DXGI_CREATE_FACTORY_DEBUG;
	}

	CreateDXGIFactory2(flags0_factory_creation, IID_PPV_ARGS(&factory));

	DXGI_SWAP_CHAIN_DESC sc_desc = {};
	sc_desc.BufferCount = 2;
	sc_desc.BufferDesc.Width = width;
	sc_desc.BufferDesc.Height = height;
	sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sc_desc.OutputWindow = window;
	sc_desc.SampleDesc.Count = 1;
	sc_desc.Windowed = TRUE;
	sc_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	factory->CreateSwapChain(device.Get(), &sc_desc, &swapchain);

	ComPtr<ID3D11RenderTargetView> RTV;
	ComPtr<ID3D11Texture2D> back_buffer;
	swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	device->CreateRenderTargetView(back_buffer.Get(), nullptr, &RTV);

	//
	// Create Depth Stencil View, its SRV and it's texture
	//

	ComPtr<ID3D11Texture2D> DSV_tex;
	D3D11_TEXTURE2D_DESC DSV_tex_desc = {};
	DSV_tex_desc.Format = DXGI_FORMAT_R32_TYPELESS;
	DSV_tex_desc.ArraySize = 1;
	DSV_tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	DSV_tex_desc.CPUAccessFlags = 0;
	DSV_tex_desc.Height = height;
	DSV_tex_desc.MipLevels = 1;
	DSV_tex_desc.MiscFlags = 0;
	DSV_tex_desc.SampleDesc.Count = 1;
	DSV_tex_desc.SampleDesc.Quality = 0;
	DSV_tex_desc.Usage = D3D11_USAGE_DEFAULT;
	DSV_tex_desc.Width = width;

	device->CreateTexture2D(&DSV_tex_desc, nullptr, &DSV_tex);

	ComPtr<ID3D11ShaderResourceView> DSV_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC DSV_SRV_desc = {};
	DSV_SRV_desc.Format = DXGI_FORMAT_R32_FLOAT;
	DSV_SRV_desc.Texture2D.MipLevels = 1;
	DSV_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

	device->CreateShaderResourceView(DSV_tex.Get(), &DSV_SRV_desc, &DSV_SRV);

	ComPtr<ID3D11DepthStencilView> DSV;
	D3D11_DEPTH_STENCIL_VIEW_DESC DSV_desc = {};
	DSV_desc.Flags = 0;
	DSV_desc.Format = DXGI_FORMAT_D32_FLOAT;
	DSV_desc.Texture2D.MipSlice = 0;
	DSV_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

	device->CreateDepthStencilView(DSV_tex.Get(), &DSV_desc, &DSV);

	//
	// Create vertex buffer and SRV
	//
	
	ComPtr<ID3D11Buffer> vertex_buffer;
	D3D11_BUFFER_DESC vertex_buffer_desc = {};
	vertex_buffer_desc.ByteWidth = sizeof(float) * compressed_vertices.size();
	vertex_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	vertex_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	vertex_buffer_desc.CPUAccessFlags = 0;
	vertex_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	vertex_buffer_desc.StructureByteStride = sizeof(float);

	D3D11_SUBRESOURCE_DATA vertex_buffer_data = {};
	vertex_buffer_data.pSysMem = compressed_vertices.data();
	
	device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_data, &vertex_buffer);
	
	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC vertex_buffer_SRV_desc = {};
	vertex_buffer_SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
	vertex_buffer_SRV_desc.Buffer.FirstElement = 0;
	vertex_buffer_SRV_desc.Buffer.ElementWidth = sizeof(float);
	vertex_buffer_SRV_desc.Buffer.NumElements = compressed_vertices.size();
	vertex_buffer_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

	device->CreateShaderResourceView(vertex_buffer.Get(), &vertex_buffer_SRV_desc, &vertex_buffer_SRV);

	//
	// Generate a Texture2D and mips.
	//

	ComPtr<ID3D11Texture2D> HIZ_buffer_texture;
	D3D11_TEXTURE2D_DESC HIZ_buffer_texture_desc = {};
	HIZ_buffer_texture_desc.Format = DXGI_FORMAT_R32_FLOAT;
	HIZ_buffer_texture_desc.ArraySize = 1;
	HIZ_buffer_texture_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	HIZ_buffer_texture_desc.CPUAccessFlags = 0;
	HIZ_buffer_texture_desc.Height = height;
	HIZ_buffer_texture_desc.MipLevels = num_mips;
	HIZ_buffer_texture_desc.MiscFlags = 0;
	HIZ_buffer_texture_desc.SampleDesc.Count = 1;
	HIZ_buffer_texture_desc.SampleDesc.Quality = 0;
	HIZ_buffer_texture_desc.Usage = D3D11_USAGE_DEFAULT;
	HIZ_buffer_texture_desc.Width = width;

	device->CreateTexture2D(&HIZ_buffer_texture_desc, nullptr, &HIZ_buffer_texture);
	ComPtr<ID3D11Texture2D> HIZ_buffer_texture_B;
	device->CreateTexture2D(&HIZ_buffer_texture_desc, nullptr, &HIZ_buffer_texture_B);

	std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs(num_mips);
	std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs(num_mips);
	std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs_B(num_mips);
	std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs_B(num_mips);

	for (unsigned int i = 0; i < num_mips; i++) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC HIZ_buffer_texture_UAVi = {};
		HIZ_buffer_texture_UAVi.Format = DXGI_FORMAT_R32_FLOAT;
		HIZ_buffer_texture_UAVi.Texture2D.MipSlice = i;
		HIZ_buffer_texture_UAVi.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

		device->CreateUnorderedAccessView(HIZ_buffer_texture.Get(), &HIZ_buffer_texture_UAVi, &HIZ_buffer_texture_UAVs[i]);
		device->CreateUnorderedAccessView(HIZ_buffer_texture_B.Get(), &HIZ_buffer_texture_UAVi, &HIZ_buffer_texture_UAVs_B[i]);

		D3D11_SHADER_RESOURCE_VIEW_DESC HIZ_buffer_texture_SRVi = {};
		HIZ_buffer_texture_SRVi.Format = DXGI_FORMAT_R32_FLOAT;
		HIZ_buffer_texture_SRVi.Texture2D.MipLevels = 1;
		HIZ_buffer_texture_SRVi.Texture2D.MostDetailedMip = i;
		HIZ_buffer_texture_SRVi.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

		device->CreateShaderResourceView(HIZ_buffer_texture.Get(), &HIZ_buffer_texture_SRVi, &HIZ_buffer_texture_SRVs[i]);
		device->CreateShaderResourceView(HIZ_buffer_texture_B.Get(), &HIZ_buffer_texture_SRVi, &HIZ_buffer_texture_SRVs_B[i]);
	}

	float clear_val[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	for (unsigned int i = 0; i < num_mips; i++) {
		ctx->ClearUnorderedAccessViewFloat(HIZ_buffer_texture_UAVs[i].Get(), clear_val);
		ctx->ClearUnorderedAccessViewFloat(HIZ_buffer_texture_UAVs_B[i].Get(), clear_val);
	}
	
	ComPtr<ID3D11ShaderResourceView> HIZ_full_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC full_srv_desc = {};
	full_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
	full_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	full_srv_desc.Texture2D.MostDetailedMip = 0;
	full_srv_desc.Texture2D.MipLevels = num_mips;

	device->CreateShaderResourceView(HIZ_buffer_texture.Get(), &full_srv_desc, &HIZ_full_SRV);

	//
	// Create Cull resource
	//

	ComPtr<ID3D11Buffer> dimensions_buffer;
	D3D11_BUFFER_DESC dimensions_buffer_desc = {};
	dimensions_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	dimensions_buffer_desc.ByteWidth = 4 * sizeof(unsigned int);
	dimensions_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	dimensions_buffer_desc.MiscFlags = 0;
	dimensions_buffer_desc.StructureByteStride = 0;
	dimensions_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;

	D3D11_SUBRESOURCE_DATA buffer_data = {};
	buffer_data.pSysMem = dimensions.data();

	device->CreateBuffer(&dimensions_buffer_desc, &buffer_data, &dimensions_buffer);

	ComPtr<ID3D11Buffer> status_buffer;
	D3D11_BUFFER_DESC status_buffer_desc = {};
	status_buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	status_buffer_desc.ByteWidth = sizeof(unsigned int) * num_triangles;
	status_buffer_desc.CPUAccessFlags = 0;
	status_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	status_buffer_desc.StructureByteStride = sizeof(unsigned int);
	status_buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	device->CreateBuffer(&status_buffer_desc, nullptr, &status_buffer);
	
	ComPtr<ID3D11UnorderedAccessView> status_buffer_UAV;
	D3D11_UNORDERED_ACCESS_VIEW_DESC status_buffer_UAV_desc = {};
	status_buffer_UAV_desc.Buffer.FirstElement = 0;
	status_buffer_UAV_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	status_buffer_UAV_desc.Buffer.NumElements = num_triangles;
	status_buffer_UAV_desc.Format = DXGI_FORMAT_UNKNOWN;
	status_buffer_UAV_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

	device->CreateUnorderedAccessView(status_buffer.Get(), &status_buffer_UAV_desc, &status_buffer_UAV);

	ComPtr<ID3D11ShaderResourceView> status_buffer_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC status_buffer_SRV_desc = {};
	status_buffer_SRV_desc.Buffer.ElementWidth = sizeof(unsigned int);
	status_buffer_SRV_desc.Buffer.FirstElement = 0;
	status_buffer_SRV_desc.Buffer.NumElements = num_triangles;
	status_buffer_SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
	status_buffer_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

	device->CreateShaderResourceView(status_buffer.Get(), &status_buffer_SRV_desc, &status_buffer_SRV);

	//
	// Create the indirect buffers
	//

	ComPtr<ID3D11Buffer> indirect_buf;
	D3D11_BUFFER_DESC indirect_buf_desc = {};
	indirect_buf_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	indirect_buf_desc.ByteWidth = sizeof(unsigned int) * 4;
	indirect_buf_desc.CPUAccessFlags = 0;
	indirect_buf_desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	indirect_buf_desc.StructureByteStride = sizeof(unsigned int);

	device->CreateBuffer(&indirect_buf_desc, nullptr, &indirect_buf);
	unsigned int init[4] = { 3, (unsigned int)num_triangles, 0, 0 };
	ctx->UpdateSubresource(indirect_buf.Get(), 0, nullptr, init, 0, 0);

	ComPtr<ID3D11Buffer> counter;
	D3D11_BUFFER_DESC counter_desc = {};
	counter_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	counter_desc.ByteWidth = sizeof(unsigned int);
	counter_desc.CPUAccessFlags = 0;
	counter_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	counter_desc.StructureByteStride = sizeof(unsigned int);
	counter_desc.Usage = D3D11_USAGE_DEFAULT;
	
	device->CreateBuffer(&counter_desc, nullptr, &counter);

	ComPtr<ID3D11UnorderedAccessView> counter_UAV;
	D3D11_UNORDERED_ACCESS_VIEW_DESC counter_UAV_desc = {};
	counter_UAV_desc.Buffer.FirstElement = 0;
	counter_UAV_desc.Buffer.Flags = 0;
	counter_UAV_desc.Buffer.NumElements = 1;
	counter_UAV_desc.Format = DXGI_FORMAT_UNKNOWN;
	counter_UAV_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

	device->CreateUnorderedAccessView(counter.Get(), &counter_UAV_desc, &counter_UAV);

	//
	// Compile vertex, compute and pixel shaders
	//

	unsigned int compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

	if (debug) {
		compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	}

	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3DBlob> vs_blob;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3DBlob> ps_blob;
	ComPtr<ID3D11ComputeShader> cull_shader;
	ComPtr<ID3DBlob> cull_blob;
	ComPtr<ID3D11VertexShader> nocull_vs;
	ComPtr<ID3DBlob> nocull_blob;
	ComPtr<ID3DBlob> error_blob;

	HRESULT hr;
	hr = D3DCompileFromFile(L"vs.hlsl", nullptr, nullptr, "main", "vs_5_0", compile_flags, 0, &vs_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}

		return -1;
	}

	hr = D3DCompileFromFile(L"ps.hlsl", nullptr, nullptr, "main", "ps_5_0", compile_flags, 0, &ps_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}

		return -1;
	}

	hr = D3DCompileFromFile(L"cull.hlsl", nullptr, nullptr, "main", "cs_5_0", compile_flags, 0, &cull_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}

		return -1;
	}

	hr = D3DCompileFromFile(L"non_occlusion_vs.hlsl", nullptr, nullptr, "main", "vs_5_0", compile_flags, 0, &nocull_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}

		return -1;
	}

	device->CreateVertexShader(vs_blob.Get()->GetBufferPointer(), vs_blob.Get()->GetBufferSize(), nullptr, &vs);
	device->CreatePixelShader(ps_blob.Get()->GetBufferPointer(), ps_blob.Get()->GetBufferSize(), nullptr, &ps);
	device->CreateComputeShader(cull_blob.Get()->GetBufferPointer(), cull_blob.Get()->GetBufferSize(), nullptr, &cull_shader);
	device->CreateVertexShader(nocull_blob.Get()->GetBufferPointer(), nocull_blob.Get()->GetBufferSize(), nullptr, &nocull_vs);

	std::vector<ComPtr<ID3D11ComputeShader>> shaders(shader_data.size());
	for (unsigned int i = 0; i < shader_data.size(); i++) {
		ComPtr<ID3DBlob> cs_blob_loop;
		D3DCompile(shader_data[i].shader.c_str(), strlen(shader_data[i].shader.c_str()), nullptr, nullptr, nullptr, "main", "cs_5_0", compile_flags, 0, &cs_blob_loop, &error_blob);
		if (FAILED(hr)) {
			if (error_blob.Get()->GetBufferSize() > 0) {
				OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
			}

			return -1;
		}

		device->CreateComputeShader(cs_blob_loop.Get()->GetBufferPointer(), cs_blob_loop.Get()->GetBufferSize(), nullptr, &shaders[i]);
	}

	//
	//
	//

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (float)width;
	viewport.Height = (float)height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // red

	D3D11_RASTERIZER_DESC rs_desc = {};
	rs_desc.FillMode = D3D11_FILL_SOLID;
	rs_desc.CullMode = D3D11_CULL_NONE;  // disable to test
	rs_desc.FrontCounterClockwise = FALSE;

	ComPtr<ID3D11RasterizerState> rs;
	device->CreateRasterizerState(&rs_desc, &rs);

	D3D11_DEPTH_STENCIL_DESC ds_desc = {};
	ds_desc.DepthEnable = TRUE;
	ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	ds_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	ComPtr<ID3D11DepthStencilState> dss;
	device->CreateDepthStencilState(&ds_desc, &dss);

	std::vector<ID3D11ShaderResourceView*> render_SRVs = { vertex_buffer_SRV.Get(), status_buffer_SRV.Get() };
	std::vector<ID3D11ShaderResourceView*> cull_SRVs = { HIZ_full_SRV.Get(), vertex_buffer_SRV.Get() };
	std::vector<ID3D11UnorderedAccessView*> cull_UAVs = { status_buffer_UAV.Get() };

	for (unsigned int i = 0; i < HIZ_buffer_texture_SRVs.size(); i++) {
		render_SRVs.push_back(HIZ_buffer_texture_SRVs[i].Get());
	}

	//
	// Timing constants
	//

	auto now = std::chrono::high_resolution_clock::now();
	unsigned long long frame_counter = 0;

	//
	// render loop and message loop
	//

	if (occlusion) {
		MSG msg = {};
		while (running) {
			while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			ctx->RSSetViewports(1, &viewport);
			ctx->RSSetState(rs.Get());
			ctx->VSSetShader(vs.Get(), nullptr, 0);
			ctx->VSSetShaderResources(0, render_SRVs.size(), render_SRVs.data());
			ctx->PSSetShader(ps.Get(), nullptr, 0);
			ctx->PSSetShaderResources(0, 0, nullptr);
			ctx->ClearRenderTargetView(RTV.Get(), clear_color);
			ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
			ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
			ctx->OMSetDepthStencilState(dss.Get(), 0);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->DrawInstancedIndirect(indirect_buf.Get(), 0);

			ctx->OMSetRenderTargets(0, nullptr, nullptr); // unbind DSV first

			ctx->CopySubresourceRegion(
				HIZ_buffer_texture.Get(),  // dst
				0,              // dst subresource (mip 0)
				0, 0, 0,        // dst x y z
				DSV_tex.Get(),  // src
				0,              // src subresource
				nullptr         // src box (nullptr = whole texture)
			);

			for (unsigned int i = 0; i < num_mips - 1; i++) {
				ctx->CSSetShaderResources(0, 1, nullSRVs);
				ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
				ctx->CSSetShader(shaders[i].Get(), nullptr, 0);
				ctx->CSSetShaderResources(0, 1, HIZ_buffer_texture_SRVs[i].GetAddressOf());
				ctx->CSSetUnorderedAccessViews(0, 1, HIZ_buffer_texture_UAVs_B[i + 1].GetAddressOf(), nullptr);
				ctx->Dispatch(shader_data[i].dispatchx, shader_data[i].dispatchy, 1);

				ctx->CopySubresourceRegion(
					HIZ_buffer_texture.Get(),  // dst
					i + 1,              // dst subresource (mip 1)
					0, 0, 0,        // dst x y z
					HIZ_buffer_texture_B.Get(),  // src
					i + 1,              // src subresource
					nullptr         // src box (nullptr = whole texture)
				);
			}

			ctx->OMSetRenderTargets(0, nullptr, nullptr);
			ID3D11ShaderResourceView* nulls[] = { nullptr, nullptr };
			ctx->VSSetShaderResources(0, 2, nulls); // unbind before cull pass

			ctx->CSSetShader(cull_shader.Get(), nullptr, 0);
			ctx->CSSetShaderResources(0, cull_SRVs.size(), cull_SRVs.data());
			ctx->CSSetUnorderedAccessViews(0, cull_UAVs.size(), cull_UAVs.data(), &zero);
			ctx->CSSetConstantBuffers(0, 1, dimensions_buffer.GetAddressOf());
			ctx->Dispatch((num_triangles + 63) / 64, 1, 1);
			ctx->CopyStructureCount(indirect_buf.Get(), 4, status_buffer_UAV.Get());
			ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
			ctx->CSSetShaderResources(0, 2, nullSRVs); // also unbind CS SRVs

			frame_counter++;

			swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

			auto later = std::chrono::high_resolution_clock::now();

			if (std::chrono::duration<double, std::milli>(later - now).count() >= 1000.0f) {
				SetWindowTextA(window, std::to_string(frame_counter).c_str());

				now = later;
				frame_counter = 0;
			}
		}
	} else {
		MSG msg = {};
		while (running) {
			while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			ctx->RSSetViewports(1, &viewport);
			ctx->RSSetState(rs.Get());
			ctx->VSSetShader(nocull_vs.Get(), nullptr, 0);
			ctx->VSSetShaderResources(0, 1, vertex_buffer_SRV.GetAddressOf());
			ctx->PSSetShader(ps.Get(), nullptr, 0);
			ctx->PSSetShaderResources(0, 0, nullptr);
			ctx->ClearRenderTargetView(RTV.Get(), clear_color);
			ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
			ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
			ctx->OMSetDepthStencilState(dss.Get(), 0);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ctx->Draw(num_triangles * 3, 0);

			swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

			frame_counter++;

			auto later = std::chrono::high_resolution_clock::now();

			if (std::chrono::duration<double, std::milli>(later - now).count() >= 1000.0f) {
				SetWindowTextA(window, std::to_string(frame_counter).c_str());

				now = later;
				frame_counter = 0;
			}
		}
	}

	return 0;
}
