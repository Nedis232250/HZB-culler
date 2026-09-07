#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <iostream>

using namespace Microsoft::WRL;

ComPtr<ID3D12DescriptorHeap> create_CBV_SRV_UAV_descriptor_heap(ID3D12Device* device, unsigned int num_descriptors) {
	ComPtr<ID3D12DescriptorHeap> dh;
	D3D12_DESCRIPTOR_HEAP_DESC dh_desc = {};
	dh_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	dh_desc.NodeMask = 0;
	dh_desc.NumDescriptors = num_descriptors;
	dh_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	device->CreateDescriptorHeap(&dh_desc, IID_PPV_ARGS(&dh));

	return dh;
}

struct DSV_return_struct {
	ComPtr<ID3D12DescriptorHeap> dh;
	ComPtr<ID3D12Resource> resource;
	D3D12_DEPTH_STENCIL_VIEW_DESC desc;
};

DSV_return_struct create_DSV_resources(ID3D12Device* device, unsigned int width, unsigned int height, DXGI_FORMAT format, float depth, unsigned char stencil) {
	ComPtr<ID3D12Resource> DSV_resource;
	D3D12_RESOURCE_DESC DSV_resource_desc = {};
	DSV_resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	DSV_resource_desc.Format = format;
	DSV_resource_desc.MipLevels = 1;
	DSV_resource_desc.Alignment = 0;
	DSV_resource_desc.DepthOrArraySize = 1;
	DSV_resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	DSV_resource_desc.Height = height;
	DSV_resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	DSV_resource_desc.SampleDesc.Count = 1;
	DSV_resource_desc.SampleDesc.Quality = 0;
	DSV_resource_desc.Width = width;

	D3D12_HEAP_PROPERTIES DSV_resource_heap_properties = {};
	DSV_resource_heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE DSV_clear_value = {};
	DSV_clear_value.DepthStencil = { depth, stencil };
	DSV_clear_value.Format = format;

	device->CreateCommittedResource(&DSV_resource_heap_properties, D3D12_HEAP_FLAG_NONE, &DSV_resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &DSV_clear_value, IID_PPV_ARGS(&DSV_resource));

	ComPtr<ID3D12DescriptorHeap> DSV_resource_DH;
	D3D12_DESCRIPTOR_HEAP_DESC DSV_resource_DH_desc = {};
	DSV_resource_DH_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DSV_resource_DH_desc.NodeMask = 0;
	DSV_resource_DH_desc.NumDescriptors = 1;
	DSV_resource_DH_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	device->CreateDescriptorHeap(&DSV_resource_DH_desc, IID_PPV_ARGS(&DSV_resource_DH));

	D3D12_DEPTH_STENCIL_VIEW_DESC DSV_desc = {};
	DSV_desc.Flags = D3D12_DSV_FLAG_NONE;
	DSV_desc.Format = format;
	DSV_desc.Texture2D.MipSlice = 0;
	DSV_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	device->CreateDepthStencilView(DSV_resource.Get(), &DSV_desc, DSV_resource_DH.Get()->GetCPUDescriptorHandleForHeapStart());

	return { DSV_resource_DH, DSV_resource, DSV_desc };
}

ComPtr<ID3DBlob> compile_shader(const wchar_t* path, LPCSTR entrypoint, LPCSTR target, bool optimization) {
	ComPtr<ID3DBlob> error_blob;
	ComPtr<ID3DBlob> shader_blob;
	HRESULT hr;

	unsigned int flags2 = 0;

	if (optimization) {
		flags2 = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	}

	hr = D3DCompileFromFile(path, nullptr, nullptr, entrypoint, target, 0, flags2, &shader_blob, &error_blob);

	if (FAILED(hr)) {
		if (error_blob) {
			OutputDebugStringA((const char*)"ERROR IN SHADER: ");
			OutputDebugStringA((const char*)error_blob.Get()->GetBufferPointer());
			return nullptr;
		}
	}

	return shader_blob;
}

ComPtr<ID3D12Resource> create_StructuredBuffer_for_upload_and_SRV(ID3D12Device* device, unsigned long long buffer_size, unsigned long long element_size, void* data_in, D3D12_CPU_DESCRIPTOR_HANDLE CPU_descriptor_handle) {
	bool err = false;

	if (data_in == nullptr) {
		std::cout << "ERROR IN FUNCTION create_StructuredBuffer_for_upload_and_SRV: The data_in parameter is nullptr.\n";
		err = true;
	}

	if (buffer_size == 0) {
		std::cout << "ERROR IN FUNCTION create_StructuredBuffer_for_upload_and_SRV: The buffer you are passing in has zero elements in it according to the buffer_size parameter.\n";
		err = true;
	}

	if (element_size == 0) {
		std::cout << "ERROR IN FUNCTION create_StructuredBuffer_for_upload_and_SRV: Each element in the buffer you are passing in has zero size according to the element_size parameter.\n";
		err = true;
	}

	if (err) {
		return nullptr;
	}

	ComPtr<ID3D12Resource> upload_buffer;
	D3D12_RESOURCE_DESC upload_buffer_desc = {};
	upload_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	upload_buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
	upload_buffer_desc.MipLevels = 1;
	upload_buffer_desc.Alignment = 0;
	upload_buffer_desc.DepthOrArraySize = 1;
	upload_buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	upload_buffer_desc.Height = 1;
	upload_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	upload_buffer_desc.SampleDesc.Count = 1;
	upload_buffer_desc.SampleDesc.Quality = 0;
	upload_buffer_desc.Width = buffer_size * element_size;

	D3D12_HEAP_PROPERTIES upload_heap_properties = {};
	upload_heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;

	device->CreateCommittedResource(&upload_heap_properties, D3D12_HEAP_FLAG_NONE, &upload_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_buffer));

	D3D12_RANGE EMPTY_RANGE = { 0, 0 };
	void* data = nullptr;
	upload_buffer->Map(0, &EMPTY_RANGE, &data);
	std::memcpy(data, data_in, buffer_size * element_size);
	D3D12_RANGE written_range = { 0, buffer_size * element_size };
	upload_buffer->Unmap(0, &written_range);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRV_desc = {};
	SRV_desc.Buffer.FirstElement = 0;
	SRV_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	SRV_desc.Buffer.NumElements = static_cast<unsigned int>(buffer_size);
	SRV_desc.Buffer.StructureByteStride = static_cast<unsigned int>(element_size);
	SRV_desc.Format = DXGI_FORMAT_UNKNOWN;
	SRV_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRV_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	device->CreateShaderResourceView(upload_buffer.Get(), &SRV_desc, CPU_descriptor_handle);

	CPU_descriptor_handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	return upload_buffer;
}
