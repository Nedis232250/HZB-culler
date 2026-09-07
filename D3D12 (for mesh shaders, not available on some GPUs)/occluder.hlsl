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

[numthreads(64, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint3 group_threadID : SV_GroupThreadID) {
    uint ID = threadID.x;
	uint triangleID_3 = ID * 9;

    float3 xyz = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3])), unpack_uint_to_2float16(vertices[triangleID_3 + 1]).x);
    float3 xyz2 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 3])), unpack_uint_to_2float16(vertices[triangleID_3 + 4]).x);
    float3 xyz3 = float3(ndc_to_uv(unpack_uint_to_2float16(vertices[triangleID_3 + 6])), unpack_uint_to_2float16(vertices[triangleID_3 + 7]).x);

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
