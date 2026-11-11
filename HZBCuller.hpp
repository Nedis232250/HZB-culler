#pragma once
#include "utils.hpp"
#include <optional>
#include <wrl/client.h>
#include <d3d11.h>
constexpr static ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
constexpr static ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };

using namespace Microsoft::WRL;
constexpr static unsigned int zero = 0;

class Base_HZB_culler {
private:
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
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> ctx;
	std::vector<downscale_return_structure> shader_data;
	ComPtr<ID3D11ComputeShader> cull_shader;
	ComPtr<ID3D11ComputeShader> occluder_shader;
	std::vector<ComPtr<ID3D11ComputeShader>> shaders;
	ComPtr<ID3D11VertexShader> vs;
	bool debug;
	const std::string vs_src = R"(
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

public:
	Base_HZB_culler(ComPtr<ID3D11Device> device_in, ComPtr<ID3D11DeviceContext> ctx_in, bool debug_in, unsigned long long num_triangles) {
		device = device_in;
		ctx = ctx_in;
		debug = debug_in;

		indirect_buf = create_indirect_buf(static_cast<unsigned int>(num_triangles), device, ctx);

		status_buffer_return status_buffer_return = create_status_buffer(static_cast<unsigned int>(sizeof(unsigned int)), static_cast<unsigned int>(num_triangles), device); // might be num_vertices idk
		status_buffer = status_buffer_return.status_buffer;
		status_buffer_UAV = status_buffer_return.status_buffer_UAV;
		status_buffer_SRV = status_buffer_return.status_buffer_SRV;
	}
	
	void create_HZB_mips(unsigned int width, unsigned int height) {
		unsigned int num_mips = static_cast<unsigned int>(ceil(log2(static_cast<float>(min(width, height)))));
		HIZ_mip_textures_views_return HIZ_mip_return = generate_HIZ_mips(width, height, num_mips, device, ctx);
		HIZ_buffer_texture = HIZ_mip_return.HIZ_buffer_texture;
		HIZ_buffer_texture_B = HIZ_mip_return.HIZ_buffer_texture_B;
		HIZ_buffer_texture_SRVs = HIZ_mip_return.HIZ_buffer_texture_SRVs;
		HIZ_buffer_texture_UAVs_B = HIZ_mip_return.HIZ_buffer_texture_UAVs_B;
		HIZ_full_SRV = HIZ_mip_return.HIZ_full_SRV;
	}

	void reset_HZB_mips(unsigned int width, unsigned int height) {
		if (HIZ_buffer_texture) HIZ_buffer_texture.Reset();
		if (HIZ_buffer_texture_B) HIZ_buffer_texture_B.Reset();
		for (auto& texture_SRV : HIZ_buffer_texture_SRVs) if (texture_SRV) texture_SRV.Reset();
		for (auto& texture_UAVs_B : HIZ_buffer_texture_UAVs_B) if (texture_UAVs_B) texture_UAVs_B.Reset();
		if (HIZ_full_SRV) HIZ_full_SRV.Reset();

		create_HZB_mips(width, height);
	}

	void create_HZB_shaders(unsigned int width, unsigned int height) {
		shader_data = downscale_parser(generate_downscale_shader_lines(width, height));
		shader_return shaders_return = generate_shaders(device, shader_data, debug);
		cull_shader = shaders_return.cull_shader;
		occluder_shader = shaders_return.occluder_shader;
		shaders = shaders_return.shaders;

		ComPtr<ID3DBlob> shader_blob;
		ComPtr<ID3DBlob> error_blob;
		HRESULT hr;

		hr = D3DCompile(vs_src.c_str(), strlen(vs_src.c_str()), nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader_blob, &error_blob);
		if (FAILED(hr)) {
			if (error_blob.Get()->GetBufferSize() > 0) {
				OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
			}
		}

		device->CreateVertexShader(shader_blob.Get()->GetBufferPointer(), shader_blob.Get()->GetBufferSize(), nullptr, &vs);
	}

	void reset_HZB_shaders(unsigned int width, unsigned int height) {
		for (auto& shader : shaders) if (shader) shader.Reset();
		create_HZB_shaders(width, height);
	}

	void create_reset_dimensions_buffer(unsigned int width, unsigned int height) {
		std::vector<unsigned int> dimensions = { width, height };
		if (dimensions_buffer) dimensions_buffer.Reset();
		dimensions_buffer = dimensions_buffer_create_tool(dimensions.data(), device);
	}

	void update_vertex_buffer_size(unsigned long long num_triangles) {
		indirect_buf = create_indirect_buf(static_cast<unsigned int>(num_triangles), device, ctx);

		status_buffer_return status_buffer_return = create_status_buffer(static_cast<unsigned int>(sizeof(unsigned int)), static_cast<unsigned int>(num_triangles), device); // might be num_vertices idk
		status_buffer = status_buffer_return.status_buffer;
		status_buffer_UAV = status_buffer_return.status_buffer_UAV;
		status_buffer_SRV = status_buffer_return.status_buffer_SRV;
	}

	std::vector<ID3D11ShaderResourceView*> cull(ComPtr<ID3D11ShaderResourceView> vertex_buffer_SRV, unsigned int dispatchX, ComPtr<ID3D11PixelShader> ps, float* clear_color, unsigned int width, unsigned int height, DSV_return DSV_struct, ComPtr<ID3D11RenderTargetView> RTV) {
		unsigned int num_mips = static_cast<unsigned int>(ceil(log2(static_cast<float>(min(width, height)))));
		std::vector<ID3D11ShaderResourceView*> render_SRVs = { vertex_buffer_SRV.Get(), status_buffer_SRV.Get() };
		std::vector<ID3D11ShaderResourceView*> cull_SRVs = { HIZ_full_SRV.Get(), vertex_buffer_SRV.Get() };
		ComPtr<ID3D11DepthStencilView> DSV = DSV_struct.DSV;
		ComPtr<ID3D11Texture2D> DSV_tex = DSV_struct.DSV_tex;

		for (unsigned int i = 0; i < HIZ_buffer_texture_SRVs.size(); i++) {
			render_SRVs.push_back(HIZ_buffer_texture_SRVs.at(i).Get());
		}

		ctx->CSSetShader(occluder_shader.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 1, vertex_buffer_SRV.GetAddressOf());
		ctx->CSSetUnorderedAccessViews(0, 1, status_buffer_UAV.GetAddressOf(), &zero);
		ctx->Dispatch(dispatchX, 1, 1);
		ctx->CopyStructureCount(indirect_buf.Get(), 4, status_buffer_UAV.Get());
		ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		ctx->VSSetShader(vs.Get(), nullptr, 0);
		ctx->VSSetShaderResources(0, static_cast<unsigned int>(render_SRVs.size()), render_SRVs.data());
		ctx->PSSetShader(ps.Get(), nullptr, 0);
		ctx->ClearRenderTargetView(RTV.Get(), clear_color);
		ctx->ClearDepthStencilView(DSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		ctx->OMSetRenderTargets(1, RTV.GetAddressOf(), DSV.Get()); // Check if I NEED AN RTV
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
		ctx->Dispatch(dispatchX, 1, 1);
		ctx->CopyStructureCount(indirect_buf.Get(), 4, status_buffer_UAV.Get());
		ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 2, nullSRVs); // also unbind CS SRVs
		ctx->VSSetShader(vs.Get(), nullptr, 0);

		return render_SRVs;
	}

	void draw_indirect() {
		ctx->DrawInstancedIndirect(indirect_buf.Get(), 0);
		ctx->VSSetShaderResources(0, 2, nullSRVs);
	}

	void set_default_vs() {
		ctx->VSSetShader(vs.Get(), nullptr, 0);
	}
};

using HZBCuller = std::optional<Base_HZB_culler>;
