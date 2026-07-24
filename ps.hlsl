struct VSOutput {
	float4 position : SV_Position;
	nointerpolation uint color : TEXCOORD0;
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
