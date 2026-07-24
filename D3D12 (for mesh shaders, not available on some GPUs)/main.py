from math import log2, pow, floor, ceil
import sys
width = int(sys.argv[1])
height = int(sys.argv[2])
output_path = sys.argv[3] + "\\downscaleshadercache.txt"

script_blueprints = []
dispatches = []

for x in range(1, ceil(log2(min(width, height)))):
    outputres = [int(floor(width / pow(2, x))), int(floor(height / pow(2, x)))]
    numthreadsx = 0
    numthreadsy = 0
    
    if outputres[0] % 32 == 0:
        numthreadsx = 32
    else:
        for i in range(1, 6, 1):
            if int(int(outputres[0]) % int(pow(2, i))) != 0:
                numthreadsx = int(pow(2, i - 1))
                break

    if outputres[1] % 32 == 0:
        numthreadsy = 32
    else:
        for i in range(1, 6, 1):
            if int(int(outputres[1]) % int(pow(2, i))) != 0:
                numthreadsy = int(pow(2, i - 1))
                break
            
    script = f"""
        Texture2D<float> mip_src_tex : register(t0);
        RWTexture2D<float> mip_dst_tex : register(u0);

        [numthreads({numthreadsx}, {numthreadsy}, 1)]
        void main(uint3 groupID : SV_GroupID, uint3 threadID : SV_GroupThreadID) {{
            int2 src_xy = int2(groupID.x * {numthreadsx} + threadID.x, groupID.y * {numthreadsy} + threadID.y);
            int2 src_xy_scale = src_xy * 2;
            float a = mip_src_tex[src_xy_scale + int2(0, 0)];
            float b = mip_src_tex[src_xy_scale + int2(1, 0)];
            float c = mip_src_tex[src_xy_scale + int2(0, 1)];
            float d = mip_src_tex[src_xy_scale + int2(1, 1)];

            mip_dst_tex[src_xy] = max(max(a, b), max(c, d));
        }}
    """

    script_blueprints.append(script)

    dispatches.append([int(width / numthreadsx / pow(2, x)), int(height / numthreadsy / pow(2, x))])

for i, script in enumerate(script_blueprints):
    with open(output_path, "w") as f:
        for i, script in enumerate(script_blueprints):
            f.write(script)
            f.write("[DISPATCHINFO]")
            f.write(dispatches[i])
            f.write("[BREAKHERE]");
