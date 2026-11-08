#pragma once
#include "functiongenerator.hpp"
#include "utils.hpp"
#include <vector>

enum HZBCuller_shader_type {
	HZBCuller_VERTEX_SHADER,
	HZBCuller_PIXEL_SHADER
};

class HZBCuller {
private:
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> ctx;
	ComPtr<IDXGIFactory> factory;
	unsigned int width;
	unsigned int height;
	ComPtr<IDXGISwapChain> swapchain;
	ComPtr<ID3D11RenderTargetView> RTV;
	ComPtr<ID3D11Texture2D> DSV_tex;
	ComPtr<ID3D11DepthStencilView> DSV;
	ComPtr<ID3D11Buffer> vertex_buffer;
	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
	ComPtr<ID3D11Buffer> dimensions_buffer;
	ComPtr<ID3D11Buffer> indirect_buf;
	ComPtr<ID3D11Buffer> status_buffer;
	ComPtr<ID3D11UnorderedAccessView> status_buffer_UAV;
	ComPtr<ID3D11ShaderResourceView> status_buffer_SRV;
	ComPtr<ID3D11Texture2D> HIZ_buffer_texture;
	ComPtr<ID3D11Texture2D> HIZ_buffer_texture_B;
	std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs;
	std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs_B;
	ComPtr<ID3D11ShaderResourceView> HIZ_full_SRV;
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11ComputeShader> cull_shader;
	ComPtr<ID3D11VertexShader> nocull_vs;
	ComPtr<ID3D11ComputeShader> occluder_shader;
	std::vector<ComPtr<ID3D11ComputeShader>> shaders;
	std::vector<downscale_return_structure> shader_data;
	FPSCounter fps_counter{};
	HWND window;
	bool debug;

	std::string vs_src = R"(
		StructuredBuffer<uint> positions_colors : register(t0);
		StructuredBuffer<uint> triangle_status : register(t1);

		struct VSOutput {
			float4 position : SV_Position;
			uint color : TEXCOORD0;
		};

		float2 unpack_uint_to_2float16(uint packed) {
			float2 unpacked;
			unpacked.x = f16tof32(packed & 0xFFFF);
			unpacked.y = f16tof32(packed >> 16);
			return unpacked;
		}

		VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
			uint triangle_ID = triangle_status[instanceID]; // actual triangle offset
			uint offset = triangle_ID * 9 + vertexID * 3;
	
			VSOutput output;

			float2 xy = unpack_uint_to_2float16(positions_colors[offset]);
			float z = asfloat(positions_colors[offset + 1]);

			output.position = float4(xy, z, 1.0f);
			output.color = positions_colors[offset + 2];

			return output;
		}
	)";

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

	std::vector<std::string> generate_downscale_shader_lines() {
		std::vector<std::string> script_lines = {};

		for (unsigned int x = 1; x < ceil(log2(min(width, height))); x++) {
			std::vector<unsigned int> outputres = { unsigned int(ceil(width / pow(2, x))), unsigned int(ceil(height / pow(2, x))) };
			unsigned int numthreadsx = 0;
			unsigned int numthreadsy = 0;

			if (outputres.at(0) % 32 == 0) {
				numthreadsx = 32;
			} else {
				for (unsigned int i = 1; i < 6; i++) {
					if (unsigned int(unsigned int(outputres.at(0)) % unsigned int(pow(2, i))) != 0) {
						numthreadsx = unsigned int(pow(2, i - 1));
						break;
					}
				}
			}

			if (outputres.at(1) % 32 == 0) {
				numthreadsy = 32;
			} else {
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
public:
	HZBCuller(unsigned int width_in, unsigned int height_in, HWND window_in, bool solid, D3D11_CULL_MODE cull_mode, bool FCCW) {
		std::string answer;

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
		
		width = width_in;
		height = height_in;
		window = window_in;
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags0_device_creation, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &ctx);
		CreateDXGIFactory2(flags0_factory_creation, IID_PPV_ARGS(&factory));

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
		rs_desc.FillMode = solid ? D3D11_FILL_SOLID : D3D11_FILL_WIREFRAME;
		rs_desc.CullMode = cull_mode;  // disable to test
		rs_desc.FrontCounterClockwise = FCCW;

		ComPtr<ID3D11RasterizerState> rs;
		device->CreateRasterizerState(&rs_desc, &rs);

		ctx->RSSetState(rs.Get());

		swapchain_return swapchain_rtv = create_swapchain(width, height, window, factory, device);
		swapchain = swapchain_rtv.swpachain;
		RTV = swapchain_rtv.RTV;

		DSV_return DSV_return = create_DSV(width, height, device);
		DSV_tex = DSV_return.DSV_tex;
		DSV = DSV_return.DSV;
	}

	void create_buffers(unsigned long long vertices_count, unsigned long long num_triangles, void* data) {
		vertex_return vertex_return = create_vertex_buffer(static_cast<unsigned int>(sizeof(float)), static_cast<unsigned int>(vertices_count), data, device);
		vertex_buffer = vertex_return.vertex_buffer;
		vertex_buffer_SRV = vertex_return.vertex_buffer_SRV;

		std::vector<unsigned int> dimensions = { width, height };
		dimensions_buffer = create_dimensions_buffer(dimensions.data(), device);

		indirect_buf = create_indirect_buf(static_cast<unsigned int>(num_triangles), device, ctx);

		status_buffer_return status_buffer_return = create_status_buffer(static_cast<unsigned int>(sizeof(unsigned int)), static_cast<unsigned int>(num_triangles), device); // might be num_vertices idk
		status_buffer = status_buffer_return.status_buffer;
		status_buffer_UAV = status_buffer_return.status_buffer_UAV;
		status_buffer_SRV = status_buffer_return.status_buffer_SRV;
	}

	void create_mip_chain() {
		unsigned int num_mips = static_cast<unsigned int>(floor(log2(static_cast<float>(min(width, height))))) + 1u;
		HIZ_mip_textures_views_return HIZ_mip_return = generate_HIZ_mips(width, height, num_mips, device, ctx);
		HIZ_buffer_texture = HIZ_mip_return.HIZ_buffer_texture;
		HIZ_buffer_texture_B = HIZ_mip_return.HIZ_buffer_texture_B;
		HIZ_buffer_texture_SRVs = HIZ_mip_return.HIZ_buffer_texture_SRVs;
		HIZ_buffer_texture_UAVs_B = HIZ_mip_return.HIZ_buffer_texture_UAVs_B;
		HIZ_full_SRV = HIZ_mip_return.HIZ_full_SRV;
	}

	void create_shaders() {
		shader_data = downscale_parser(generate_downscale_shader_lines());
		shader_return shaders_return = generate_shaders(device, shader_data, debug, vs_src, ps_src);
		vs = shaders_return.vs;
		ps = shaders_return.ps;
		cull_shader = shaders_return.cull_shader;
		nocull_vs = shaders_return.nocull_vs;
		occluder_shader = shaders_return.occluder_shader;
		shaders = shaders_return.shaders;
	}

	void draw_occlusion(unsigned long long num_triangles, unsigned int numthreadsX, float* clear_color, std::string additional_text, bool show_fps, bool comprehensive) {
		unsigned int num_mips = static_cast<unsigned int>(ceil(log2(static_cast<float>(min(width, height)))));
		unsigned int dispatchX = (static_cast<unsigned int>(num_triangles) + numthreadsX - 1) / numthreadsX;
		std::vector<ID3D11ShaderResourceView*> render_SRVs = { vertex_buffer_SRV.Get(), status_buffer_SRV.Get() };
		std::vector<ID3D11ShaderResourceView*> cull_SRVs = { HIZ_full_SRV.Get(), vertex_buffer_SRV.Get() };

		for (unsigned int i = 0; i < HIZ_buffer_texture_SRVs.size(); i++) {
			render_SRVs.push_back(HIZ_buffer_texture_SRVs.at(i).Get());
		}

		draw(ctx, occluder_shader, vertex_buffer_SRV, status_buffer_UAV, dispatchX, indirect_buf, vs, render_SRVs, ps,
			RTV, clear_color, DSV, HIZ_buffer_texture, DSV_tex, num_mips, shaders, HIZ_buffer_texture_SRVs, HIZ_buffer_texture_UAVs_B,
			shader_data, HIZ_buffer_texture_B, cull_shader, cull_SRVs, dimensions_buffer, swapchain);
		FPSReturn fps = fps_counter.check_fps();
		if (fps.second && show_fps && !comprehensive) {
			SetWindowTextA(window, std::to_string(fps.frames_passed).c_str());
		} else if (fps.second && show_fps && comprehensive) {
			std::string title = "FPS: " + std::to_string(fps.frames_passed) + additional_text;
			SetWindowTextA(window, title.c_str());
		}
	}

	void draw_no_occlusion_benchmark(float* clear_color, unsigned long long num_triangles, std::string additional_text, bool show_fps, bool comprehensive) {
		draw_no_occluder_benchmark(ctx, nocull_vs, vertex_buffer_SRV, ps, RTV, clear_color, DSV, swapchain, static_cast<unsigned int>(num_triangles));
		FPSReturn fps = fps_counter.check_fps();
		if (fps.second && show_fps && !comprehensive) {
			SetWindowTextA(window, std::to_string(fps.frames_passed).c_str());
		} else if (fps.second && show_fps && comprehensive) {
			std::string title = "FPS: " + std::to_string(fps.frames_passed) + additional_text;
			SetWindowTextA(window, title.c_str());
		}
	}

	std::string return_shader(HZBCuller_shader_type type) {
		switch (type) {
			case HZBCuller_VERTEX_SHADER: {
				return vs_src;
				break;
			}
			case HZBCuller_PIXEL_SHADER: {
				return ps_src;
				break;
			}
		}
	}

	HRESULT custom_shader(std::string SRC, HZBCuller_shader_type type) {
		HRESULT hr;
		unsigned int compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

		if (debug) {
			compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
		}

		ComPtr<ID3DBlob> src_blob;
		ComPtr<ID3DBlob> error_blob;

		switch (type) {
			case HZBCuller_PIXEL_SHADER: {
				ps->Release();
				
				hr = D3DCompile(SRC.c_str(), strlen(SRC.c_str()), nullptr, nullptr, nullptr, "main", "ps_5_0", compile_flags, 0, &src_blob, &error_blob);
				if (FAILED(hr)) {
					if (error_blob.Get()->GetBufferSize() > 0) {
						OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
					}
				}

				device->CreatePixelShader(src_blob.Get()->GetBufferPointer(), src_blob.Get()->GetBufferSize(), nullptr, &ps);

				break;
			}
			case HZBCuller_VERTEX_SHADER: {
				vs->Release();

				hr = D3DCompile(SRC.c_str(), strlen(SRC.c_str()), nullptr, nullptr, nullptr, "main", "vs_5_0", compile_flags, 0, &src_blob, &error_blob);
				if (FAILED(hr)) {
					if (error_blob.Get()->GetBufferSize() > 0) {
						OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
					}
				}

				device->CreateVertexShader(src_blob.Get()->GetBufferPointer(), src_blob.Get()->GetBufferSize(), nullptr, &vs);

				break;
			}
		}

		return hr;
	}

	std::vector<unsigned int> resize() {
		std::vector<unsigned int> dimensions = client_dimensions(window);
		width = dimensions.at(0);
		height = dimensions.at(1);

		if (swapchain) swapchain.Reset();
		if (RTV) RTV.Reset();

		swapchain_return swapchain_rtv = create_swapchain(width, height, window, factory, device);
		swapchain = swapchain_rtv.swpachain;
		RTV = swapchain_rtv.RTV;

		if (DSV) DSV.Reset();
		if (DSV_tex) DSV_tex.Reset();

		DSV_return DSV_return = create_DSV(width, height, device);
		DSV_tex = DSV_return.DSV_tex;
		DSV = DSV_return.DSV;

		if (HIZ_buffer_texture) HIZ_buffer_texture.Reset();
		if (HIZ_buffer_texture_B) HIZ_buffer_texture_B.Reset();
		for (auto& texture_SRV : HIZ_buffer_texture_SRVs) if (texture_SRV) texture_SRV.Reset();
		for (auto& texture_UAVs_B : HIZ_buffer_texture_UAVs_B) if (texture_UAVs_B) texture_UAVs_B.Reset();
		if (HIZ_full_SRV) HIZ_full_SRV.Reset();

		unsigned int num_mips = static_cast<unsigned int>(ceil(log2(static_cast<float>(min(width, height)))));
		HIZ_mip_textures_views_return HIZ_mip_return = generate_HIZ_mips(width, height, num_mips, device, ctx);
		HIZ_buffer_texture = HIZ_mip_return.HIZ_buffer_texture;
		HIZ_buffer_texture_B = HIZ_mip_return.HIZ_buffer_texture_B;
		HIZ_buffer_texture_SRVs = HIZ_mip_return.HIZ_buffer_texture_SRVs;
		HIZ_buffer_texture_UAVs_B = HIZ_mip_return.HIZ_buffer_texture_UAVs_B;
		HIZ_full_SRV = HIZ_mip_return.HIZ_full_SRV;

		if (vs) vs.Reset();
		if (ps) ps.Reset();
		if (cull_shader) cull_shader.Reset();
		if (nocull_vs) nocull_vs.Reset();
		if (occluder_shader) occluder_shader.Reset();
		for (auto& shader : shaders) if (shader) shader.Reset();

		shader_data = downscale_parser(generate_downscale_shader_lines());

		shader_return shaders_return = generate_shaders(device, shader_data, debug, vs_src, ps_src);
		vs = shaders_return.vs;
		ps = shaders_return.ps;
		cull_shader = shaders_return.cull_shader;
		nocull_vs = shaders_return.nocull_vs;
		occluder_shader = shaders_return.occluder_shader;
		shaders = shaders_return.shaders;

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &viewport);

		if (dimensions_buffer) dimensions_buffer.Reset();
		dimensions_buffer = create_dimensions_buffer(dimensions.data(), device);

		return dimensions;
	}
};

using HZB_Culler = std::optional<HZBCuller>;
