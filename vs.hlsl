StructuredBuffer<uint> positions_colors : register(t0);
StructuredBuffer<uint> triangle_status : register(t1);

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

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
	uint triangle_ID = triangle_status[instanceID]; // actual triangle offset
	uint offset = triangle_ID * 9 + vertexID * 3;
	
	VSOutput output;

	float2 xy = unpack_uint_to_2float16(positions_colors[offset]);
	float z = unpack_uint_to_2float16(positions_colors[offset + 1]).x;

	output.position = float4(xy, z, 1.0f);
	output.color = positions_colors[offset + 2];

	return output;
}
