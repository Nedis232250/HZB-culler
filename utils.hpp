#pragma once
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <vector>
#include <consoleapi.h>
#include <fstream>
#include <string>
#include <cmath>
#include <DirectXMath.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <thread>
#include <cstring>
#include <wrl/client.h>

using high_resolution_clock = std::chrono::time_point<std::chrono::high_resolution_clock>;
using namespace Microsoft::WRL;

struct downscale_return_structure {
    std::string shader_string;
    unsigned int dispatchx;
    unsigned int dispatchy;
};

struct FPSReturn {
    bool second;
    unsigned long long frames_passed;
};

void attach_console() {
    if (AllocConsole()) {
        FILE* file;
        freopen_s(&file, "CONOUT$", "w", stdout);
        freopen_s(&file, "CONIN$", "r", stdin);
    }
}

//
// Pray to god this doesn't have any undefined behavior because I have zero idea how this works
// 
int find_cores() {
    DWORD bytes = 0;

    // 1. First call with a null pointer to let Windows tell us the exact size required
    if (!GetLogicalProcessorInformation(nullptr, &bytes)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return -1; // An unexpected native error occurred
        }
    }

    // 2. Mathematically calculate exactly how many elements to allocate
    size_t required_elements = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(required_elements);

    // 3. Second call with the perfectly sized buffer
    if (!GetLogicalProcessorInformation(buffer.data(), &bytes)) {
        return -1; // API failed unexpectedly
    }

    // 4. Count the physical cores securely within vector bounds
    int physical_cores = 0;
    size_t actual_elements = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    for (size_t i = 0; i < actual_elements; ++i) {
        if (buffer.at(i).Relationship == RelationProcessorCore) {
            physical_cores++;
        }
    }

    return physical_cores;
}

//
// Pray to god this doesn't have any undefined behavior because I have zero idea how this works
// 
// # Of dev hours wasted trying to find out what this function means: 1
// Nedis232: 1 hour.
//
// (update this count)
//
std::vector<float> load_vertices(const char* path, unsigned long long num_triangles, unsigned int kThreads) {
    if (kThreads == 0) {
        kThreads = static_cast<unsigned int>(find_cores());
        if (kThreads == 0) kThreads = 4;
    }

    constexpr size_t kFloatsPerVertex = 7;
    constexpr size_t kNoFail = ~size_t(0);  // sentinel; avoids max() entirely

    // 1. Slurp the file (std::string guarantees a trailing '\0' for strtof).
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "failed to open file\n";
        return {};
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::string buf(static_cast<size_t>(size), '\0');
    file.read(buf.data(), size);

    // 2. Index the first num_triangles*3 non-blank lines (one vertex per line).
    const size_t max_vertices = static_cast<size_t>(num_triangles * 3ULL);
    std::vector<size_t> line_starts;
    for (size_t pos = 0; pos < buf.size() && line_starts.size() < max_vertices;) {
        size_t eol = buf.find('\n', pos);
        if (eol == std::string::npos) eol = buf.size();
        size_t i = pos;
        while (i < eol && std::isspace(static_cast<unsigned char>(buf.at(i)))) ++i;
        if (i < eol) line_starts.push_back(pos);  // skip blank lines
        pos = eol + 1;
    }

    const size_t n = line_starts.size();
    std::vector<float> result(n * kFloatsPerVertex);
    std::vector<size_t> first_fail(kThreads, kNoFail);

    // 3. Parse one line into 7 floats; returns false on malformed input.
    auto parse_line = [&](size_t start, float* out) -> bool {
        const char* p = buf.c_str() + start;
        const char* eol = std::strchr(p, '\n');
        if (!eol) eol = buf.c_str() + buf.size();
        for (size_t k = 0; k < kFloatsPerVertex; ++k) {
            char* end;
            out[k] = std::strtof(p, &end);
            if (end == p || end > eol) return false;
            p = end;
            if (k + 1 < kFloatsPerVertex) {
                while (*p == ' ' || *p == '\t') ++p;
                if (*p != ',') return false;
                ++p;
            }
        }
        return true;  // trailing comma (if any) is simply ignored
        };

    // 4. Each thread handles a contiguous block of lines.
    const size_t chunk = (n + kThreads - 1) / kThreads;
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < kThreads; ++t) {
        const size_t begin = t * chunk;
        const size_t end = (std::min)(n, begin + chunk);  // parenthesized: macro-safe
        if (begin >= end) break;
        threads.emplace_back([&, t, begin, end] {
            for (size_t i = begin; i < end; ++i) {
                if (!parse_line(line_starts.at(i), &result.at(i * kFloatsPerVertex))) {
                    first_fail.at(t) = i;
                    return;
                }
            }
            });
    }
    for (auto& th : threads) th.join();

    // 5. Match the original's behavior: stop at the first malformed vertex.
    const size_t fail = *std::min_element(first_fail.begin(), first_fail.end());
    if (fail != kNoFail)
        result.resize(fail * kFloatsPerVertex);

    return result;
}

//
// NOT BF16
//
uint32_t pack_float2_to_half2(float a, float b) {
    auto to_f16 = [](float f) -> uint16_t {
        uint32_t x;
        memcpy(&x, &f, sizeof(x));

        uint16_t sign = static_cast<uint16_t>((x >> 31) << 15);
        uint16_t mantissa = static_cast<uint16_t>((x & 0x7FFFFF) >> 13);
        int32_t  exp = ((x >> 23) & 0xFF) - 127 + 15;

        if (exp <= 0)  return sign;           // underflow → ±0
        if (exp >= 31) return sign | 0x7C00;  // overflow  → ±inf

        return sign | (uint16_t)(exp << 10) | mantissa;
        };

    uint16_t ha = to_f16(a);
    uint16_t hb = to_f16(b);

    return ((uint32_t)hb << 16) | ha;
}

uint32_t pack_rgba_to_argb(float r, float g, float b, float a) {
    uint8_t ri = (uint8_t)(r * 255.0f + 0.5f);
    uint8_t gi = (uint8_t)(g * 255.0f + 0.5f);
    uint8_t bi = (uint8_t)(b * 255.0f + 0.5f);
    uint8_t ai = (uint8_t)(a * 255.0f + 0.5f);

    return ((uint32_t)ai << 24)
        | ((uint32_t)ri << 16)
        | ((uint32_t)gi << 8)
        | (uint32_t)bi;
}

std::vector<unsigned int> compress_vertices(std::vector<float> vertices) {
    std::vector<unsigned int> result;

    for (unsigned int i = 0; i < vertices.size(); i += 7) {
        result.push_back(pack_float2_to_half2(vertices.at(i), vertices.at(i + 1)));
        result.push_back(std::bit_cast<uint32_t>(vertices.at(i + 2)));
        result.push_back(pack_rgba_to_argb(vertices.at(i + 3), vertices.at(i + 4), vertices.at(i + 5), vertices.at(i + 6)));
    }

    return result;
}

std::vector<downscale_return_structure> downscale_parser(std::vector<std::string> shader_lines) {
    std::vector<std::vector<std::string>> individual_shader_data;
    std::vector<std::string> temp;

    for (unsigned int i = 0; i < shader_lines.size(); i++) {
        if (shader_lines.at(i) == "[BREAKHERE]") {
            individual_shader_data.push_back(temp);
            temp = {};
            continue;
        }

        temp.push_back(shader_lines.at(i));
    }

    // Split shader data into the shader code and dispatch info

    std::vector<downscale_return_structure> downscale_shader_return;

    for (unsigned int i = 0; i < individual_shader_data.size(); i++) {
        std::string shader_code = "";
        unsigned int dispatchx = 0;
        unsigned int dispatchy = 0;


        for (unsigned int j = 0; j < individual_shader_data.at(i).size(); j++) {
            if (individual_shader_data.at(i).at(j) == "[DISPATCHINFO]") {
                std::string target_info = individual_shader_data.at(i).at(j + 1);
                std::string dispatchx_str = "";
                std::string dispatchy_str = "";
                bool dispatchx_mode = true;

                for (unsigned int k = 1; k < target_info.size(); k++) {
                    if (target_info.at(k) == ']') {
                        break;
                    }

                    if (target_info.at(k) == ',') {
                        dispatchx_mode = false;
                        continue;
                    }

                    if (dispatchx_mode) {
                        dispatchx_str += target_info.at(k);
                        continue;
                    }

                    dispatchy_str += target_info.at(k);
                }

                dispatchx = std::stoi(dispatchx_str);
                dispatchy = std::stoi(dispatchy_str);

                break;
            }

            shader_code += individual_shader_data.at(i).at(j) + "\n";
        }

        downscale_shader_return.push_back({ shader_code, dispatchx, dispatchy });
    }

    return downscale_shader_return;
}

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

void send_message(MSG msg) {
    while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void create_wndclass(LPCWSTR lp_class_name, WNDPROC w_proc) {
    WNDCLASS w_class = {};
    w_class.lpszClassName = lp_class_name;
    w_class.lpfnWndProc = w_proc;

    RegisterClassW(&w_class);
}

template<typename T> void release_all_ComPtr(std::vector<ComPtr<T>> ComPtr_to_release) {
    for (auto& el : ComPtr_to_release) {
        if (el) el->Release();
    }
}

std::vector<unsigned int> client_dimensions(HWND window) {
    RECT rect;
    if (GetClientRect(window, &rect)) {
        // For a client rect, rect.left and rect.top are always 0
        return { static_cast<unsigned int>(rect.right), static_cast<unsigned int>(rect.bottom) };
    }
}

struct swapchain_return {
    ComPtr<IDXGISwapChain> swpachain;
    ComPtr<ID3D11RenderTargetView> RTV;
};


swapchain_return create_swapchain(unsigned int width, unsigned int height, HWND window, ComPtr<IDXGIFactory> factory, ComPtr<ID3D11Device> device) {
    ComPtr<IDXGISwapChain> swapchain;
    DXGI_SWAP_CHAIN_DESC sc_desc = {};
    sc_desc.BufferCount = 2;
    sc_desc.BufferDesc.Width = width;
    sc_desc.BufferDesc.Height = height;
    sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.OutputWindow = window;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.Windowed = TRUE;
    sc_desc.Flags = 0;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    factory->CreateSwapChain(device.Get(), &sc_desc, &swapchain);

    ComPtr<ID3D11RenderTargetView> RTV;
    ComPtr<ID3D11Texture2D> back_buffer;
    swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    device->CreateRenderTargetView(back_buffer.Get(), nullptr, &RTV);

    return { swapchain, RTV };
}

struct DSV_return {
    ComPtr<ID3D11Texture2D> DSV_tex;
    ComPtr<ID3D11DepthStencilView> DSV;
};

DSV_return create_DSV(unsigned int width, unsigned int height, ComPtr<ID3D11Device> device) {
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

    ComPtr<ID3D11DepthStencilView> DSV;
    D3D11_DEPTH_STENCIL_VIEW_DESC DSV_desc = {};
    DSV_desc.Flags = 0;
    DSV_desc.Format = DXGI_FORMAT_D32_FLOAT;
    DSV_desc.Texture2D.MipSlice = 0;
    DSV_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(DSV_tex.Get(), &DSV_desc, &DSV);

    return { DSV_tex, DSV };
}

struct HIZ_mip_textures_views_return {
    ComPtr<ID3D11Texture2D> HIZ_buffer_texture;
    ComPtr<ID3D11Texture2D> HIZ_buffer_texture_B;
    std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs;
    std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs_B;
    ComPtr<ID3D11ShaderResourceView> HIZ_full_SRV;
};

HIZ_mip_textures_views_return generate_HIZ_mips(unsigned int width, unsigned int height, unsigned int num_mips, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> ctx) {
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

    std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs(num_mips);
    std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs_B(num_mips);

    for (unsigned int i = 0; i < num_mips; i++) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC HIZ_buffer_texture_UAVi = {};
        HIZ_buffer_texture_UAVi.Format = DXGI_FORMAT_R32_FLOAT;
        HIZ_buffer_texture_UAVi.Texture2D.MipSlice = i;
        HIZ_buffer_texture_UAVi.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

        device->CreateUnorderedAccessView(HIZ_buffer_texture_B.Get(), &HIZ_buffer_texture_UAVi, &HIZ_buffer_texture_UAVs_B.at(i));

        D3D11_SHADER_RESOURCE_VIEW_DESC HIZ_buffer_texture_SRVi = {};
        HIZ_buffer_texture_SRVi.Format = DXGI_FORMAT_R32_FLOAT;
        HIZ_buffer_texture_SRVi.Texture2D.MipLevels = 1;
        HIZ_buffer_texture_SRVi.Texture2D.MostDetailedMip = i;
        HIZ_buffer_texture_SRVi.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

        device->CreateShaderResourceView(HIZ_buffer_texture.Get(), &HIZ_buffer_texture_SRVi, &HIZ_buffer_texture_SRVs.at(i));
    }

    float clear_val[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (unsigned int i = 0; i < num_mips; i++) {
        ctx->ClearUnorderedAccessViewFloat(HIZ_buffer_texture_UAVs_B.at(i).Get(), clear_val);
    }

    ComPtr<ID3D11ShaderResourceView> HIZ_full_SRV;
    D3D11_SHADER_RESOURCE_VIEW_DESC full_srv_desc = {};
    full_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    full_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    full_srv_desc.Texture2D.MostDetailedMip = 0;
    full_srv_desc.Texture2D.MipLevels = num_mips;

    device->CreateShaderResourceView(HIZ_buffer_texture.Get(), &full_srv_desc, &HIZ_full_SRV);

    return { HIZ_buffer_texture, HIZ_buffer_texture_B, HIZ_buffer_texture_SRVs, HIZ_buffer_texture_UAVs_B, HIZ_full_SRV };
}


struct shader_return {
    ComPtr<ID3D11ComputeShader> cull_shader;
    ComPtr<ID3D11ComputeShader> occluder_shader;
    std::vector<ComPtr<ID3D11ComputeShader>> shaders;
};

shader_return generate_shaders(ComPtr<ID3D11Device> device, std::vector<downscale_return_structure> shader_data, bool debug) {
    std::string cull_src = R"(
		Texture2D<float> input_tex : register(t0);
		StructuredBuffer<uint> vertices : register(t1);
		AppendStructuredBuffer<uint> cull_result : register(u0);
		cbuffer dimensions : register(b0) {
			uint dwidth;
			uint dheight;
		}

		float2 unpack_uint_to_2float16(uint packed) {
			float2 unpacked;
			unpacked.x = f16tof32(packed & 0xFFFF);
			unpacked.y = f16tof32(packed >> 16);
			return unpacked;
		}

		float2 ndc_to_uv(float2 ndc) {
			return ndc * float2(0.5f, -0.5f) + 0.5f; // [-1,1] -> [0,1]
		}

		[numthreads(128, 1, 1)]
		void main(uint3 threadID : SV_DispatchThreadID) {
			uint ID = threadID.x;
			uint triangleID_3 = ID * 9;

			// The dispatch rounds up to a multiple of 128, so skip threads past the last triangle
			uint num_uints, stride;
			vertices.GetDimensions(num_uints, stride);
			if (triangleID_3 + 8 >= num_uints) return;

			float3 xyz  = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3])),     asfloat(vertices[triangleID_3 + 1]));
			float3 xyz2 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 3])), asfloat(vertices[triangleID_3 + 4]));
			float3 xyz3 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 6])), asfloat(vertices[triangleID_3 + 7]));

			float minx = min(min(xyz.x, xyz2.x), xyz3.x);
			float miny = min(min(xyz.y, xyz2.y), xyz3.y);
			uint width = dwidth;
			uint height = dheight;
			float rect_w = (max(xyz.x, max(xyz2.x, xyz3.x)) - minx) * width;
			float rect_h = (max(xyz.y, max(xyz2.y, xyz3.y)) - miny) * height;

			// rect is at most 1 texel at this mip, so a 2x2 footprint always covers it
			uint mip_level_to_sample = (uint)ceil(log2(max(max(rect_w, rect_h), 1.0f)));
			float dimension_scaling = 1.0f / (float)(1 << mip_level_to_sample);

			uint max_x_idx = (uint)(width * dimension_scaling) - 1;
			uint max_y_idx = (uint)(height * dimension_scaling) - 1;

			uint2 base_coords = uint2((uint)(minx * width * dimension_scaling), (uint)(miny * height * dimension_scaling));
			uint2 bottom_left_coords  = clamp(base_coords, uint2(0, 0), uint2(max_x_idx, max_y_idx));
			uint2 bottom_right_coords = uint2(clamp(bottom_left_coords.x + 1, 0, max_x_idx), bottom_left_coords.y);
			uint2 top_left_coords     = uint2(bottom_left_coords.x, clamp(bottom_left_coords.y + 1, 0, max_y_idx));
			uint2 top_right_coords    = uint2(clamp(bottom_left_coords.x + 1, 0, max_x_idx), clamp(bottom_left_coords.y + 1, 0, max_y_idx));

			float bottom_left_depth  = input_tex.Load(int3(bottom_left_coords,  mip_level_to_sample));
			float bottom_right_depth = input_tex.Load(int3(bottom_right_coords, mip_level_to_sample));
			float top_left_depth     = input_tex.Load(int3(top_left_coords,     mip_level_to_sample));
			float top_right_depth    = input_tex.Load(int3(top_right_coords,    mip_level_to_sample));

			// Farthest occluder depth anywhere in the footprint (upper bound on D(p))
			float hzb_max = max(max(bottom_left_depth, bottom_right_depth), max(top_left_depth, top_right_depth));

			// Nearest point of the triangle (depth is affine with w = 1, so the minimum is at a vertex)
			float tri_near_z = min(xyz.z, min(xyz2.z, xyz3.z));

			// Keep the triangle if any part of it could be in front of the occluders
			if (tri_near_z <= hzb_max) {
				cull_result.Append(ID);
			}
		}
	)";

    std::string occluder_src = R"(
		StructuredBuffer<uint> vertices : register(t0);
		AppendStructuredBuffer<uint> occluder_result : register(u0);

		#define _12sqrt3 (12 * sqrt(3))

		float2 unpack_uint_to_2float16(uint packed) {
			float2 unpacked;
			unpacked.x = f16tof32(packed & 0xFFFF);
			unpacked.y = f16tof32(packed >> 16);
			return unpacked;
		}

		float2 ndc_to_uv(float2 ndc) {
			return ndc * float2(0.5f, -0.5f) + 0.5f; // [-1,1] → [0,1]
		}

		float distance(float2 xy1, float2 xy2) {
			float x_dist = xy1.x - xy2.x;
			float y_dist = xy1.y - xy2.y;
			return sqrt(x_dist * x_dist + y_dist * y_dist);
		}

		float shoelace(float2 v1, float2 v2, float2 v3) {
			return 0.5 * abs((v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y));
		}

		float isoperimetric_quotient(float perimiter, float area) {
			return _12sqrt3 * area / perimiter;
		}

		[numthreads(128, 1, 1)]
		void main(uint3 threadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 group_threadID : SV_GroupThreadID) {
			uint ID = threadID.x;
			uint triangleID_3 = ID * 9;

			float3 xyz = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3])), asfloat(vertices[triangleID_3 + 1]));
			float3 xyz2 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 3])), asfloat(vertices[triangleID_3 + 4]));
			float3 xyz3 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 6])), asfloat(vertices[triangleID_3 + 7]));

			float minx = min(min(xyz.x, xyz2.x), xyz3.x);
			float miny = min(min(xyz.y, xyz2.y), xyz3.y);
			float maxx = max(max(xyz.x, xyz2.x), xyz3.x);
			float maxy = max(max(xyz.y, xyz2.y), xyz3.y);

			float width = maxx - minx;
			float height = maxy - miny;

			float perimiter = distance(xyz.xy, xyz2.xy) + distance(xyz2.xy, xyz3.xy) + distance(xyz3.xy, xyz.xy);
			float area = shoelace(xyz.xy, xyz2.xy, xyz3.xy);

			float score = area * isoperimetric_quotient(perimiter, area);
	
			//occluder_result.Append(ID);
			if (score > 0.0000175) {
				occluder_result.Append(ID);
			}
		}
	)";

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3DBlob> ps_blob;
    ComPtr<ID3D11ComputeShader> cull_shader;
    ComPtr<ID3DBlob> cull_blob;
    ComPtr<ID3D11VertexShader> nocull_vs;
    ComPtr<ID3DBlob> nocull_blob;
    ComPtr<ID3D11ComputeShader> occluder_shader;
    ComPtr<ID3DBlob> occluder_blob;
    ComPtr<ID3DBlob> error_blob;

    unsigned int compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    if (debug) {
        compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    }

    HRESULT hr;
    
    hr = D3DCompile(cull_src.c_str(), strlen(cull_src.c_str()), nullptr, nullptr, nullptr, "main", "cs_5_0", compile_flags, 0, &cull_blob, &error_blob);
    if (FAILED(hr)) {
        if (error_blob.Get()->GetBufferSize() > 0) {
            OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
        }
    }
    hr = D3DCompile(occluder_src.c_str(), strlen(occluder_src.c_str()), nullptr, nullptr, nullptr, "main", "cs_5_0", compile_flags, 0, &occluder_blob, &error_blob);
    if (FAILED(hr)) {
        if (error_blob.Get()->GetBufferSize() > 0) {
            OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
        }
    }

    device->CreateComputeShader(cull_blob.Get()->GetBufferPointer(), cull_blob.Get()->GetBufferSize(), nullptr, &cull_shader);
    device->CreateComputeShader(occluder_blob.Get()->GetBufferPointer(), occluder_blob.Get()->GetBufferSize(), nullptr, &occluder_shader);

    std::vector<ComPtr<ID3D11ComputeShader>> shaders(shader_data.size());
    for (unsigned int i = 0; i < shader_data.size(); i++) {
        ComPtr<ID3DBlob> cs_blob_loop;
        D3DCompile(shader_data.at(i).shader_string.c_str(), strlen(shader_data.at(i).shader_string.c_str()), nullptr, nullptr, nullptr, "main", "cs_5_0", compile_flags, 0, &cs_blob_loop, &error_blob);
        if (FAILED(hr)) {
            if (error_blob.Get()->GetBufferSize() > 0) {
                OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
            }
        }

        device->CreateComputeShader(cs_blob_loop.Get()->GetBufferPointer(), cs_blob_loop.Get()->GetBufferSize(), nullptr, &shaders.at(i));
    }

    return { cull_shader, occluder_shader, shaders };
}

std::vector<std::string> generate_downscale_shader_lines(unsigned int width, unsigned int height) {
    std::vector<std::string> script_lines = {};

    for (unsigned int x = 1; x < ceil(log2(min(width, height))); x++) {
        std::vector<unsigned int> outputres = { unsigned int(ceil(width / pow(2, x))), unsigned int(ceil(height / pow(2, x))) };
        unsigned int numthreadsx = 0;
        unsigned int numthreadsy = 0;

        if (outputres.at(0) % 32 == 0) {
            numthreadsx = 32;
        }
        else {
            for (unsigned int i = 1; i < 6; i++) {
                if (unsigned int(unsigned int(outputres.at(0)) % unsigned int(pow(2, i))) != 0) {
                    numthreadsx = unsigned int(pow(2, i - 1));
                    break;
                }
            }
        }

        if (outputres.at(1) % 32 == 0) {
            numthreadsy = 32;
        }
        else {
            for (unsigned int i = 1; i < 6; i++) {
                if (unsigned int(unsigned int(outputres.at(1)) % unsigned int(pow(2, i))) != 0) {
                    numthreadsy = int(pow(2, i - 1));
                    break;
                }
            }
        }

        script_lines.push_back("Texture2D<float> mip_src_tex : register(t0);");
        script_lines.push_back("RWTexture2D<float> mip_dst_tex : register(u0);");
        script_lines.push_back("[numthreads(" + std::to_string(numthreadsx) + ", " + std::to_string(numthreadsy) + ", 1)]");
        script_lines.push_back("void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID) {");
        script_lines.push_back("int2 src_xy = int2(groupID.x * " + std::to_string(numthreadsx) + " + threadID.x, groupID.y * " + std::to_string(numthreadsy) + " + threadID.y);");
        script_lines.push_back("int2 src_xy_scale = src_xy * 2;");
        script_lines.push_back("float a = mip_src_tex[src_xy_scale + int2(0, 0)];");
        script_lines.push_back("float b = mip_src_tex[src_xy_scale + int2(1, 0)];");
        script_lines.push_back("float c = mip_src_tex[src_xy_scale + int2(0, 1)];");
        script_lines.push_back("float d = mip_src_tex[src_xy_scale + int2(1, 1)];");
        script_lines.push_back("mip_dst_tex[src_xy] = max(max(a, b), max(c, d));");
        script_lines.push_back("}");
        script_lines.push_back("[DISPATCHINFO]");
        script_lines.push_back("[" + std::to_string(unsigned int(width / numthreadsx / pow(2, x))) + ", " + std::to_string(unsigned int(height / numthreadsy / pow(2, x))) + "]");
        script_lines.push_back("[BREAKHERE]");
    }

    return script_lines;
}

ComPtr<ID3D11Buffer> dimensions_buffer_create_tool(void* data, ComPtr<ID3D11Device> device) {
    ComPtr<ID3D11Buffer> dimensions_buffer;
    D3D11_BUFFER_DESC dimensions_buffer_desc = {};
    dimensions_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    dimensions_buffer_desc.ByteWidth = 4 * sizeof(unsigned int);
    dimensions_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dimensions_buffer_desc.MiscFlags = 0;
    dimensions_buffer_desc.StructureByteStride = 0;
    dimensions_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;

    D3D11_SUBRESOURCE_DATA buffer_data = {};
    buffer_data.pSysMem = data;

    device->CreateBuffer(&dimensions_buffer_desc, &buffer_data, &dimensions_buffer);

    return dimensions_buffer;
}

ComPtr<ID3D11Buffer> create_indirect_buf(unsigned int num_triangles, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> ctx) {
    ComPtr<ID3D11Buffer> indirect_buf;
    D3D11_BUFFER_DESC indirect_buf_desc = {};
    indirect_buf_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    indirect_buf_desc.ByteWidth = sizeof(unsigned int) * 4;
    indirect_buf_desc.CPUAccessFlags = 0;
    indirect_buf_desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    indirect_buf_desc.StructureByteStride = sizeof(unsigned int);

    device->CreateBuffer(&indirect_buf_desc, nullptr, &indirect_buf);
    unsigned int init[4] = { 3, num_triangles, 0, 0 };
    ctx->UpdateSubresource(indirect_buf.Get(), 0, nullptr, init, 0, 0);

    return indirect_buf;
}

struct status_buffer_return {
    ComPtr<ID3D11Buffer> status_buffer;
    ComPtr<ID3D11UnorderedAccessView> status_buffer_UAV;
    ComPtr<ID3D11ShaderResourceView> status_buffer_SRV;
};

status_buffer_return create_status_buffer(unsigned int element_width, unsigned int num_elements, ComPtr<ID3D11Device> device) {
    ComPtr<ID3D11Buffer> status_buffer;
    D3D11_BUFFER_DESC status_buffer_desc = {};
    status_buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    status_buffer_desc.ByteWidth = element_width * num_elements;
    status_buffer_desc.CPUAccessFlags = 0;
    status_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    status_buffer_desc.StructureByteStride = element_width;
    status_buffer_desc.Usage = D3D11_USAGE_DEFAULT;

    device->CreateBuffer(&status_buffer_desc, nullptr, &status_buffer);

    ComPtr<ID3D11UnorderedAccessView> status_buffer_UAV;
    D3D11_UNORDERED_ACCESS_VIEW_DESC status_buffer_UAV_desc = {};
    status_buffer_UAV_desc.Buffer.FirstElement = 0;
    status_buffer_UAV_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    status_buffer_UAV_desc.Buffer.NumElements = num_elements;
    status_buffer_UAV_desc.Format = DXGI_FORMAT_UNKNOWN;
    status_buffer_UAV_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

    device->CreateUnorderedAccessView(status_buffer.Get(), &status_buffer_UAV_desc, &status_buffer_UAV);

    ComPtr<ID3D11ShaderResourceView> status_buffer_SRV;
    D3D11_SHADER_RESOURCE_VIEW_DESC status_buffer_SRV_desc = {};
    status_buffer_SRV_desc.Buffer.ElementWidth = element_width;
    status_buffer_SRV_desc.Buffer.FirstElement = 0;
    status_buffer_SRV_desc.Buffer.NumElements = num_elements;
    status_buffer_SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
    status_buffer_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

    device->CreateShaderResourceView(status_buffer.Get(), &status_buffer_SRV_desc, &status_buffer_SRV);

    return { status_buffer, status_buffer_UAV, status_buffer_SRV };
}

#define KILLCOMPTRREF {
#define EXITKILL }
