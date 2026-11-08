#pragma once
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include "utils.hpp"
#include <vector>

using namespace Microsoft::WRL;
constexpr static unsigned int zero = 0;
constexpr ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
constexpr ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };

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

struct vertex_return {
	ComPtr<ID3D11Buffer> vertex_buffer;
	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
};

vertex_return create_vertex_buffer(unsigned int element_width, unsigned int num_elements, void* data, ComPtr<ID3D11Device> device) {
	ComPtr<ID3D11Buffer> vertex_buffer;
	D3D11_BUFFER_DESC vertex_buffer_desc = {};
	vertex_buffer_desc.ByteWidth = element_width * num_elements;
	vertex_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	vertex_buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	vertex_buffer_desc.CPUAccessFlags = 0;
	vertex_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	vertex_buffer_desc.StructureByteStride = element_width;

	D3D11_SUBRESOURCE_DATA vertex_buffer_data = {};
	vertex_buffer_data.pSysMem = data;

	device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_data, &vertex_buffer);

	ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV;
	D3D11_SHADER_RESOURCE_VIEW_DESC vertex_buffer_SRV_desc = {};
	vertex_buffer_SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
	vertex_buffer_SRV_desc.Buffer.FirstElement = 0;
	vertex_buffer_SRV_desc.Buffer.ElementWidth = element_width;
	vertex_buffer_SRV_desc.Buffer.NumElements = num_elements;
	vertex_buffer_SRV_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

	device->CreateShaderResourceView(vertex_buffer.Get(), &vertex_buffer_SRV_desc, &vertex_buffer_SRV);

	return { vertex_buffer, vertex_buffer_SRV };
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

ComPtr<ID3D11Buffer> create_dimensions_buffer(void* data, ComPtr<ID3D11Device> device) {
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

struct shader_return {
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11ComputeShader> cull_shader;
	ComPtr<ID3D11VertexShader> nocull_vs;
	ComPtr<ID3D11ComputeShader> occluder_shader;
	std::vector<ComPtr<ID3D11ComputeShader>> shaders;
};

shader_return generate_shaders(ComPtr<ID3D11Device> device, std::vector<downscale_return_structure> shader_data, bool debug, std::string vs_src, std::string ps_src) {
	//std::string cull_src = R"(
	//	Texture2D<float> input_tex : register(t0);
	//	StructuredBuffer<uint> vertices : register(t1);
	//	AppendStructuredBuffer<uint> cull_result : register(u0);
	//	cbuffer dimensions : register(b0) {
	//		uint dwidth;
	//		uint dheight;
	//	}

	//	float2 unpack_uint_to_2float16(uint packed) {
	//		float2 unpacked;
	//	    unpacked.x = f16tof32(packed & 0xFFFF);
	//		unpacked.y = f16tof32(packed >> 16);
	//		return unpacked;
	//	}

	//	float2 ndc_to_uv(float2 ndc) {
	//		return ndc * float2(0.5f, -0.5f) + 0.5f; // [-1,1] → [0,1]
	//	}

	//	[numthreads(128, 1, 1)]
	//	void main(uint3 threadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 group_threadID : SV_GroupThreadID) {
	//		uint ID = threadID.x;
	//		uint triangleID_3 = ID * 9;

	//		float3 xyz = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3])), asfloat(vertices[triangleID_3 + 1]));
	//		float3 xyz2 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 3])), asfloat(vertices[triangleID_3 + 4]));
	//		float3 xyz3 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 6])), asfloat(vertices[triangleID_3 + 7]));

	//		float minx = min(min(xyz.x, xyz2.x), xyz3.x);
	//		float miny = min(min(xyz.y, xyz2.y), xyz3.y);
	//		uint width = dwidth;
	//		uint height = dheight;
	//		float rect_w = (max(xyz.x, max(xyz2.x, xyz3.x)) - minx) * width;
	//		float rect_h = (max(xyz.y, max(xyz2.y, xyz3.y)) - miny) * height;
	//		uint mip_level_to_sample = (uint)ceil(log2(max(rect_w, rect_h)));
	//		float dimension_scaling = 1.0f / (float)(1 << mip_level_to_sample);

	//		uint max_x_idx = (uint)(width * dimension_scaling) - 1;
	//		uint max_y_idx = (uint)(height * dimension_scaling) - 1;

	//		uint2 base_coords = uint2((uint)(minx * width * dimension_scaling), (uint)(miny * height * dimension_scaling));
	//		uint2 bottom_left_coords  = clamp(base_coords, uint2(0, 0), uint2(max_x_idx, max_y_idx));
	//		uint2 bottom_right_coords = uint2(clamp(bottom_left_coords.x + 1, 0, max_x_idx), bottom_left_coords.y);
	//		uint2 top_left_coords     = uint2(bottom_left_coords.x, clamp(bottom_left_coords.y + 1, 0, max_y_idx));
	//		uint2 top_right_coords    = uint2(clamp(bottom_left_coords.x + 1, 0, max_x_idx), clamp(bottom_left_coords.y + 1, 0, max_y_idx));

	//		float bottom_left_depth = input_tex.Load(int3(bottom_left_coords.xy, mip_level_to_sample));
	//		float bottom_right_depth = input_tex.Load(int3(bottom_right_coords.xy, mip_level_to_sample));
	//		float top_left_depth = input_tex.Load(int3(top_left_coords.xy, mip_level_to_sample));
	//		float top_right_depth = input_tex.Load(int3(top_right_coords.xy, mip_level_to_sample));

	//		float min_depth = min(max(bottom_left_depth, bottom_right_depth), min(top_left_depth, top_right_depth));
	//		float max_depth = max(xyz.z, max(xyz2.z, xyz3.z));
	//
	//		if (max_depth <= min_depth) {
	//			cull_result.Append(ID);
	//		}
	//	}
	//)";

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

	std::string noocc_vs_src = R"(
		StructuredBuffer<uint> positions_colors : register(t0);

		struct VSOutput {
			float4 position : SV_Position;
			nointerpolation uint color : TEXCOORD0;
		};

		float2 unpack_uint_to_2float16(uint packed) {
			float2 unpacked;
			unpacked.x = f16tof32(packed & 0xFFFF);
			unpacked.y = f16tof32(packed >> 16);
			return unpacked;
		}

		VSOutput main(uint vertexID : SV_VertexID) {
			uint offset = vertexID * 3;

			VSOutput output;

			float2 xy = unpack_uint_to_2float16(positions_colors[offset]);
			float z = asfloat(positions_colors[offset + 1]);

			output.position = float4(xy, z, 1.0f);
			output.color = positions_colors[offset + 2];

			return output;
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
	hr = D3DCompile(vs_src.c_str(), strlen(vs_src.c_str()), nullptr, nullptr, nullptr, "main", "vs_5_0", compile_flags, 0, &vs_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}
	}

	hr = D3DCompile(ps_src.c_str(), strlen(ps_src.c_str()), nullptr, nullptr, nullptr, "main", "ps_5_0", compile_flags, 0, &ps_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}
	}

	hr = D3DCompile(cull_src.c_str(), strlen(cull_src.c_str()), nullptr, nullptr, nullptr, "main", "cs_5_0", compile_flags, 0, &cull_blob, &error_blob);
	if (FAILED(hr)) {
		if (error_blob.Get()->GetBufferSize() > 0) {
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
		}
	}

	hr = D3DCompile(noocc_vs_src.c_str(), strlen(noocc_vs_src.c_str()), nullptr, nullptr, nullptr, "main", "vs_5_0", compile_flags, 0, &nocull_blob, &error_blob);
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

	device->CreateVertexShader(vs_blob.Get()->GetBufferPointer(), vs_blob.Get()->GetBufferSize(), nullptr, &vs);
	device->CreatePixelShader(ps_blob.Get()->GetBufferPointer(), ps_blob.Get()->GetBufferSize(), nullptr, &ps);
	device->CreateComputeShader(cull_blob.Get()->GetBufferPointer(), cull_blob.Get()->GetBufferSize(), nullptr, &cull_shader);
	device->CreateVertexShader(nocull_blob.Get()->GetBufferPointer(), nocull_blob.Get()->GetBufferSize(), nullptr, &nocull_vs);
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

	return { vs, ps, cull_shader, nocull_vs, occluder_shader, shaders };
}

void generate_viewport(unsigned int width, unsigned int height, ComPtr<ID3D11DeviceContext> ctx) {
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	
	ctx->RSSetViewports(1, &viewport);
}

void generate_RSDS(bool solid, D3D11_CULL_MODE cull, bool FCCW, ComPtr<ID3D11Device> device) {
	D3D11_RASTERIZER_DESC rs_desc = {};
	rs_desc.FillMode = solid ? D3D11_FILL_SOLID : D3D11_FILL_WIREFRAME;
	rs_desc.CullMode = cull;  // disable to test
	rs_desc.FrontCounterClockwise = FCCW;

	ComPtr<ID3D11RasterizerState> rs;
	device->CreateRasterizerState(&rs_desc, &rs);

	D3D11_DEPTH_STENCIL_DESC ds_desc = {};
	ds_desc.DepthEnable = TRUE;
	ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	ds_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

	ComPtr<ID3D11DepthStencilState> dss;
	device->CreateDepthStencilState(&ds_desc, &dss);
}

void draw(ComPtr<ID3D11DeviceContext> ctx, ComPtr<ID3D11ComputeShader> occluder_shader, ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV,
		  ComPtr<ID3D11UnorderedAccessView> status_buffer_UAV, unsigned int thradgroupx_64_dispatch, ComPtr<ID3D11Buffer> indirect_buf,
		  ComPtr<ID3D11VertexShader> vs, std::vector<ID3D11ShaderResourceView*> render_SRVs, ComPtr<ID3D11PixelShader> ps, ComPtr<ID3D11RenderTargetView> RTV,
		  float* clear_color, ComPtr<ID3D11DepthStencilView> DSV, ComPtr<ID3D11Texture2D> HIZ_buffer_texture, ComPtr<ID3D11Texture2D> DSV_tex,
	      unsigned int num_mips, std::vector<ComPtr<ID3D11ComputeShader>> shaders, std::vector<ComPtr<ID3D11ShaderResourceView>> HIZ_buffer_texture_SRVs,
		  std::vector<ComPtr<ID3D11UnorderedAccessView>> HIZ_buffer_texture_UAVs_B, std::vector<downscale_return_structure> shader_data, 
		  ComPtr<ID3D11Texture2D> HIZ_buffer_texture_B, ComPtr<ID3D11ComputeShader> cull_shader, std::vector<ID3D11ShaderResourceView*> cull_SRVs,
		  ComPtr<ID3D11Buffer> dimensions_buffer, ComPtr<IDXGISwapChain> swapchain) {
	ctx->CSSetShader(occluder_shader.Get(), nullptr, 0);
	ctx->CSSetShaderResources(0, 1, vertex_buffer_SRV.GetAddressOf());
	ctx->CSSetUnorderedAccessViews(0, 1, status_buffer_UAV.GetAddressOf(), &zero);
	ctx->Dispatch(thradgroupx_64_dispatch, 1, 1);
	ctx->CopyStructureCount(indirect_buf.Get(), 4, status_buffer_UAV.Get());
	ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	ctx->VSSetShader(vs.Get(), nullptr, 0);
	ctx->VSSetShaderResources(0, static_cast<unsigned int>(render_SRVs.size()), render_SRVs.data());
	ctx->PSSetShader(ps.Get(), nullptr, 0);
	ctx->ClearRenderTargetView(RTV.Get(), clear_color);
	ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
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
		ctx->CSSetShader(shaders.at(i).Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 1, HIZ_buffer_texture_SRVs.at(i).GetAddressOf());
		ctx->CSSetUnorderedAccessViews(0, 1, HIZ_buffer_texture_UAVs_B.at(i + 1).GetAddressOf(), nullptr);
		ctx->Dispatch(shader_data.at(i).dispatchx, shader_data.at(i).dispatchy, 1);

		ctx->CopySubresourceRegion(
			HIZ_buffer_texture.Get(),  // dst
			i + 1,              // dst subresource (mip 1)
			0, 0, 0,        // dst x y z
			HIZ_buffer_texture_B.Get(),  // src
			i + 1,              // src subresource
			nullptr         // src box (nullptr = whole texture)
		);
	}
	ctx->VSSetShaderResources(0, 2, nullSRVs);

	ctx->CSSetShader(cull_shader.Get(), nullptr, 0);
	ctx->CSSetShaderResources(0, static_cast<unsigned int>(cull_SRVs.size()), cull_SRVs.data());
	ctx->CSSetUnorderedAccessViews(0, 1, status_buffer_UAV.GetAddressOf(), &zero);
	ctx->CSSetConstantBuffers(0, 1, dimensions_buffer.GetAddressOf());
	ctx->Dispatch(thradgroupx_64_dispatch, 1, 1);
	ctx->CopyStructureCount(indirect_buf.Get(), 4, status_buffer_UAV.Get());
	ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	ctx->CSSetShaderResources(0, 2, nullSRVs); // also unbind CS SRVs

	ctx->VSSetShader(vs.Get(), nullptr, 0);
	ctx->VSSetShaderResources(0, static_cast<unsigned int>(render_SRVs.size()), render_SRVs.data());
	ctx->PSSetShader(ps.Get(), nullptr, 0);
	ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
	ctx->DrawInstancedIndirect(indirect_buf.Get(), 0);

	ctx->VSSetShaderResources(0, 2, nullSRVs);

	swapchain->Present(1, 0);
}

void draw_no_occluder_benchmark(ComPtr<ID3D11DeviceContext> ctx, ComPtr<ID3D11VertexShader> nocull_vs, ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV,
								ComPtr<ID3D11PixelShader> ps, ComPtr<ID3D11RenderTargetView> RTV, float* clear_color, ComPtr<ID3D11DepthStencilView> DSV,
								ComPtr<IDXGISwapChain> swapchain, unsigned int num_triangles) {
	ctx->VSSetShader(nocull_vs.Get(), nullptr, 0);
	ctx->VSSetShaderResources(0, 1, vertex_buffer_SRV.GetAddressOf());
	ctx->PSSetShader(ps.Get(), nullptr, 0);
	ctx->PSSetShaderResources(0, 0, nullptr);
	ctx->ClearRenderTargetView(RTV.Get(), clear_color);
	ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
	ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->Draw(num_triangles * 3, 0);

	swapchain->Present(1, 0);
}
