Texture2D<float> input_tex : register(t0);
StructuredBuffer<uint> vertices : register(t1);
AppendStructuredBuffer<uint> cull_result : register(u0);
cbuffer dimensions : register(b0) {
    uint dwidth;
    uint dheight;
    uint dispatch_width;
}

float2 unpack_uint_to_2float16(uint packed) {
    float2 unpacked;
    unpacked.x = f16tof32(packed & 0xFFFF);
    unpacked.y = f16tof32(packed >> 16);
    return unpacked;
}

float2 ndc_to_uv(float2 ndc) {
    return ndc * float2(0.5f, -0.5f) + 0.5f; // [-1,1] → [0,1]
}

[numthreads(64, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 group_threadID : SV_GroupThreadID) {
    uint ID = threadID.x;
	uint triangleID_3 = ID * 9;

    float3 xyz = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3])), unpack_uint_to_2float16(vertices[triangleID_3 + 1]).x);
    float3 xyz2 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 3])), unpack_uint_to_2float16(vertices[triangleID_3 + 4]).x);
    float3 xyz3 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 6])), unpack_uint_to_2float16(vertices[triangleID_3 + 7]).x);

    float minx = min(min(xyz.x, xyz2.x), xyz3.x);
    float miny = min(min(xyz.y, xyz2.y), xyz3.y);
    uint width = dwidth;
    uint height = dheight;

    float rect_w = (max(xyz.x, max(xyz2.x, xyz3.x)) - minx) * width;
    float rect_h = (max(xyz.y, max(xyz2.y, xyz3.y)) - miny) * height;
    uint mip_level_to_sample = (uint)floor(log2(max(rect_w, rect_h)));
    float dimension_scaling = 1.0f / (float)(1 << mip_level_to_sample);

    uint max_x_idx = (uint)(width * dimension_scaling) - 1;
    uint max_y_idx = (uint)(height * dimension_scaling) - 1;

    uint2 base_coords = uint2((uint)(minx * width * dimension_scaling), (uint)(miny * height * dimension_scaling));
    uint2 bottom_left_coords  = clamp(base_coords, uint2(0, 0), uint2(max_x_idx, max_y_idx));
    uint2 bottom_right_coords = uint2(clamp(bottom_left_coords.x + 1, 0, max_x_idx), bottom_left_coords.y);
    uint2 top_left_coords     = uint2(bottom_left_coords.x, clamp(bottom_left_coords.y + 1, 0, max_y_idx));
    uint2 top_right_coords    = uint2(clamp(bottom_left_coords.x + 1, 0, max_x_idx), clamp(bottom_left_coords.y + 1, 0, max_y_idx));

    float bottom_left_depth = input_tex.Load(int3(bottom_left_coords.xy, mip_level_to_sample));
    float bottom_right_depth = input_tex.Load(int3(bottom_right_coords.xy, mip_level_to_sample));
    float top_left_depth = input_tex.Load(int3(top_left_coords.xy, mip_level_to_sample));
    float top_right_depth = input_tex.Load(int3(top_right_coords.xy, mip_level_to_sample));

    float max_depth = max(max(bottom_left_depth, bottom_right_depth), max(top_left_depth, top_right_depth));
    float min_depth = min(xyz.z, min(xyz2.z, xyz3.z));
    
    if (max_depth >= min_depth) {
        cull_result.Append(ID);
    }
}
