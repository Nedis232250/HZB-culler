#include <iostream>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <windows.h>
#include <chrono>
#include "utils.hpp"
#include "dxutils.h"
#include "d3dx12.h"
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
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
HRESULT hr;


//
// CONSTANTS
//

constexpr static bool debug = false;
constexpr static bool console = false;
constexpr static bool occlusion = false;
constexpr static bool generate_shaders = false;
constexpr static bool occluder_occludee = false;

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

	ComPtr<ID3D12Debug> debug_object;
	if (debug) {
		if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_object)))) {
			return -100;
		}

		debug_object->EnableDebugLayer();
	}

	ComPtr<ID3D12Device> device;
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

	ComPtr<ID3D12CommandAllocator> cmd_alloc;
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc));

	ComPtr<ID3D12GraphicsCommandList> cmd_list;
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc.Get(), nullptr, IID_PPV_ARGS(&cmd_list));

	D3D12_COMMAND_QUEUE_DESC cmd_q_desc = {};
	cmd_q_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmd_q_desc.NodeMask = 0;
	cmd_q_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
	cmd_q_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ComPtr<ID3D12CommandQueue> cmd_q;
	device->CreateCommandQueue(&cmd_q_desc, IID_PPV_ARGS(&cmd_q));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC PSO_desc = {};

	//
	// Descriptor Heap initialization and vertex/upload buffer/SRV creation
	//

	ComPtr<ID3D12DescriptorHeap> vertex_buffer_descriptor_heap = create_CBV_SRV_UAV_descriptor_heap(device.Get(), 1);
	D3D12_CPU_DESCRIPTOR_HANDLE vertex_buffer_CPU_descriptor_handle = vertex_buffer_descriptor_heap.Get()->GetCPUDescriptorHandleForHeapStart();

	
	ComPtr<ID3D12DescriptorHeap> occluder_descriptor_heap = create_CBV_SRV_UAV_descriptor_heap(device.Get(), 2);
	D3D12_CPU_DESCRIPTOR_HANDLE occlucder_CPU_descriptor_handle = occluder_descriptor_heap.Get()->GetCPUDescriptorHandleForHeapStart();
	
	ComPtr<ID3D12Resource> vertex_buffer = create_StructuredBuffer_for_upload_and_SRV(device.Get(), compressed_vertices.size(), sizeof(compressed_vertices[0]), compressed_vertices.data(), false, vertex_buffer_CPU_descriptor_handle);
	create_SRV_with_premade_buffer(device.Get(), vertex_buffer.Get(), occlucder_CPU_descriptor_handle, false, true, compressed_vertices.size(), sizeof(compressed_vertices[0]), DXGI_FORMAT_UNKNOWN);

	//
	// Compile Shaders
	//

	ComPtr<ID3DBlob> vs_blob = compile_shader(L"vs.hlsl", "main", "vs_5_0", true);
	ComPtr<ID3DBlob> ps_blob = compile_shader(L"ps.hlsl", "main", "ps_5_0", true);

	//
	// Create Root Signatures
	//

	ComPtr<ID3DBlob> error_blob;

	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;
	range.BaseShaderRegister = 0; //t0
	range.RegisterSpace = 0; // registerspace 0
	range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_DESCRIPTOR_TABLE root_descriptor_table = {};
	root_descriptor_table.NumDescriptorRanges = 1;
	root_descriptor_table.pDescriptorRanges = &range;

	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	root_parameter.DescriptorTable = root_descriptor_table;

	D3D12_ROOT_SIGNATURE_DESC root_sig_desc = {};
	root_sig_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	root_sig_desc.NumParameters = 1; // UAVs, SRVs, CBVs
	root_sig_desc.pParameters = &root_parameter;
	root_sig_desc.NumStaticSamplers = 0; // For texture sampling
	root_sig_desc.pStaticSamplers = nullptr;

	ComPtr<ID3DBlob> root_sig_blob;
	hr = D3D12SerializeRootSignature(&root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_sig_blob, &error_blob);

	if (FAILED(hr)) {
		if (error_blob) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
			return -103;
		}
	}

	ComPtr<ID3D12RootSignature> root_sig;
	device->CreateRootSignature(0, root_sig_blob.Get()->GetBufferPointer(), root_sig_blob.Get()->GetBufferSize(), IID_PPV_ARGS(&root_sig));

	//
	// CREATE PSO
	//

	ComPtr<ID3D12PipelineState> PSO;
	PSO_desc.NodeMask = 0;
	PSO_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	PSO_desc.pRootSignature = root_sig.Get();
	PSO_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	PSO_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	PSO_desc.DS = { nullptr, 0 };
	PSO_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	PSO_desc.GS = { nullptr, 0 };
	PSO_desc.HS = { nullptr, 0 };
	PSO_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	PSO_desc.InputLayout = { nullptr, 0 };
	PSO_desc.NumRenderTargets = 1;
	PSO_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	PSO_desc.PS = { ps_blob.Get()->GetBufferPointer(), ps_blob.Get()->GetBufferSize() };
	PSO_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	PSO_desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	PSO_desc.SampleDesc.Count = 1;
	PSO_desc.SampleDesc.Quality = 0;
	PSO_desc.SampleMask = 0xFFFFFFFF;
	PSO_desc.VS = { vs_blob.Get()->GetBufferPointer(), vs_blob.Get()->GetBufferSize() };
	PSO_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	PSO_desc.RasterizerState.DepthClipEnable = false;
	PSO_desc.DepthStencilState.DepthEnable = false;

	device->CreateGraphicsPipelineState(&PSO_desc, IID_PPV_ARGS(&PSO));

	//
	// Create RTV, DSV, and SwapChain
	//

	ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

	ComPtr<IDXGISwapChain3> swapchain;
	DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
	sc_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	sc_desc.BufferCount = 2;
	sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sc_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	sc_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sc_desc.Height = height;
	sc_desc.SampleDesc.Count = 1;
	sc_desc.SampleDesc.Quality = 0;
	sc_desc.Scaling = DXGI_SCALING_NONE;
	sc_desc.Stereo = false;
	sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sc_desc.Width = width;

	factory->CreateSwapChainForHwnd(cmd_q.Get(), window, &sc_desc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapchain.GetAddressOf()));

	ComPtr<ID3D12Resource> back_buffers[2];
	ComPtr<ID3D12DescriptorHeap> buffer_descriptor_heap;
	D3D12_DESCRIPTOR_HEAP_DESC buffer_descriptor_heap_desc = {};
	buffer_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	buffer_descriptor_heap_desc.NodeMask = 0;
	buffer_descriptor_heap_desc.NumDescriptors = 2;
	buffer_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	device->CreateDescriptorHeap(&buffer_descriptor_heap_desc, IID_PPV_ARGS(&buffer_descriptor_heap));

	D3D12_CPU_DESCRIPTOR_HANDLE back_buffers_descriptor_handle_for_RTV_initalization = buffer_descriptor_heap.Get()->GetCPUDescriptorHandleForHeapStart();
	D3D12_CPU_DESCRIPTOR_HANDLE back_buffers_descriptor_handle_for_references[2];
	
	for (unsigned int i = 0; i < 2; i++) {
		swapchain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]));

		device->CreateRenderTargetView(back_buffers[i].Get(), nullptr, back_buffers_descriptor_handle_for_RTV_initalization);
		
		back_buffers_descriptor_handle_for_RTV_initalization.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		back_buffers_descriptor_handle_for_references[i] = buffer_descriptor_heap.Get()->GetCPUDescriptorHandleForHeapStart();
		back_buffers_descriptor_handle_for_references[i].ptr += i * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	DSV_return_struct DSV_return = create_DSV_resources(device.Get(), width, height, DXGI_FORMAT_D32_FLOAT, 1.0f, 0xFF);
	D3D12_CPU_DESCRIPTOR_HANDLE DSV_CPU_handle = DSV_return.dh.Get()->GetCPUDescriptorHandleForHeapStart();
	device->CreateDepthStencilView(DSV_return.resource.Get(), &DSV_return.desc, DSV_CPU_handle);

	//
	// Create Fences
	//

	unsigned long long fence_val = 0;

	ComPtr<ID3D12Fence> fence;
	device->CreateFence(fence_val, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	
	HANDLE fence_event = CreateEvent(nullptr, false, false, nullptr);

	//
	// Do any pre-render loop work that needs to be done
	//

	cmd_list->Close();
	cmd_alloc->Reset();

	D3D12_RECT scissor_rect = { 0, 0, width, height };
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };

	unsigned long long frame_counter = 0;
	unsigned int thradgroupx_64_dispatch = (num_triangles + 63) / 64;
	float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	ID3D12CommandList* cmd_list_ref = cmd_list.Get();
	ID3D12DescriptorHeap* vertex_dh[1] = { vertex_buffer_descriptor_heap.Get() };

	D3D12_RESOURCE_BARRIER RTV_present_barrier1 = {};
	RTV_present_barrier1.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	RTV_present_barrier1.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	RTV_present_barrier1.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	RTV_present_barrier1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	RTV_present_barrier1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

	D3D12_RESOURCE_BARRIER RTV_present_barrier2 = {};
	RTV_present_barrier2.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	RTV_present_barrier2.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	RTV_present_barrier2.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	RTV_present_barrier2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	RTV_present_barrier2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

	auto now = std::chrono::high_resolution_clock::now();

	//
	// Render loop
	//

	MSG msg = {};
	while (running) {
		while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		unsigned int current_back_buffer = swapchain->GetCurrentBackBufferIndex();

		cmd_alloc->Reset();
		cmd_list->Reset(cmd_alloc.Get(), nullptr);
		RTV_present_barrier1.Transition.pResource = back_buffers[current_back_buffer].Get();
		cmd_list->ResourceBarrier(1, &RTV_present_barrier1);
		cmd_list->RSSetScissorRects(1, &scissor_rect);
		cmd_list->RSSetViewports(1, &viewport);
		cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd_list->ClearRenderTargetView(back_buffers_descriptor_handle_for_references[current_back_buffer], clear_color, 0, nullptr);
		cmd_list->ClearDepthStencilView(DSV_CPU_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0xFF, 0, nullptr);
		cmd_list->SetDescriptorHeaps(1, vertex_dh);
		cmd_list->SetGraphicsRootSignature(root_sig.Get());
		cmd_list->SetGraphicsRootDescriptorTable(0, vertex_buffer_descriptor_heap.Get()->GetGPUDescriptorHandleForHeapStart());
		cmd_list->SetPipelineState(PSO.Get());
		cmd_list->OMSetRenderTargets(1, &back_buffers_descriptor_handle_for_references[current_back_buffer], false, &DSV_CPU_handle);
		cmd_list->DrawInstanced(3 * num_triangles, 1, 0, 0);
		RTV_present_barrier2.Transition.pResource = back_buffers[current_back_buffer].Get();
		cmd_list->ResourceBarrier(1, &RTV_present_barrier2);
		cmd_list->Close();
		
		cmd_q->ExecuteCommandLists(1, &cmd_list_ref);
		cmd_q->Signal(fence.Get(), fence_val + 1);

		fence->SetEventOnCompletion(fence_val + 1, fence_event);
		WaitForSingleObject(fence_event, INFINITE);

		swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

		fence_val++;
		frame_counter++;

		auto later = std::chrono::high_resolution_clock::now();

		if (std::chrono::duration<double, std::milli>(later - now).count() >= 1000.0f) {
			SetWindowTextA(window, std::to_string(frame_counter).c_str());

			now = later;
			frame_counter = 0;
		}
	}
}
