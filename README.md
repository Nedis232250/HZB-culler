Overview (why did I exactly create this?):

When I was in 5th grade for christmas, my parents bought me a cheap dell inspiron (it inspired me, pun intended), 
and I was at the time very happy that I could do web dev and basic python/JS scripting on something with an actual keyboard 
with windows instead of an iPad on replit. But issues quickly started to rise for me, broken screens, hinges and bad FPS in
Minecraft, my favorite game as a child. I got curious in 7th grade with C++ and eventually started to use it often because it
was a challenge for me, and I would always see these "AAA game trailers" and "Raytracing Path tracing ultra HD plus pro max
minecraft shaders" but all of these programs ran at an anemic 10-15 fps because of the Intel HD graphics inside that clamshell
of E-Waste and thats how I morphed into an optimization snob, because my hardware was just that bad and I wanted the experience
it couldn't give me back then.

Tests and results (what actually happened on what?):

- All of these tests were performed on best performance mode via Omen Gaming Hub (OGH)/Windows power settings
- Every single one of them had the official, OEM HP 330W power brick plugged into the wall
- Every single test was performed at 1920x1080 resolution
- The test was performed on a random soup of 5 million small triangles and 50 large occluders
- Code for the vertex generation is down below (its a random AI generated test in "AI generated random GPU killer.py" meant to represent the worst case scenarios)

On my Intel iGPU on my new computer (2026 HP Omen max 16, on the iGPU of an Intel core ultra 7 255hx, the Intel arc 64eu graphics),
I got 30 fps without occlusion culling, and 60 fps with it, and when I switched the display to discrete
(via NVIDIA advanced optimus), and used my RTX 5070 ti laptop GPU (140w power limit, with the adequate OMEN tempest pro cooling
meaning the GPU stayed very cool even under max load), the FPS jumped from a high 320 to an unbeliveable 925.

Boilerplate (the boring stuff that sets everything up, all in main.cpp):
In my code I use 2 technologies deepy integrated into Windows 7, 8, 8.1, 10 and 11: the Windows API and DirectX 11. The boilerplate
is the bare minimum needed to get a window on the screen with DirectX functional. I do the generic stuff: create a window (the visual window
on screen), create a window procedure function (the piece of code that tells the program what to do if an event happens, like resizing the
window, a mouse movement, closing the window etc...) and a message loop (the piece of code that constantly scans for events to tell the window
procedure). Then I set up the DirectX 11 Device and Device Context, you can think of those as the communication loops you need to set up for the
CPU to tell the GPU what to do. Then I make the RTV, and DSV, or 2 images. Those 2 images are probably the most important images in a graphics
project. The RTV, or render target view, is what the GPU paints on, and it's the image displayed on the screen for you to see. The DSV, while
not explicitly shown is still just as important as it serves as the place to store depth for each pixel. This really helps to make sure things
that are in front of other things are drawn and things behind other things are not drawn. For nerds:

Disclaimer: yes I know the RTV and DSV aren't images but are formatted views into the raw ComPtr<ID3D11Texture2D> objects.

Memory compression (Why and how?):

Memory compression is vital in today's world of 8gb graphics cards (Thanks Jensen for 8gb vram on a 5070 laptop gpu, its not enough).
People always say to not overflow the GPU/CPU's memory but don't look at another metric vital for GPUs; memory bandwidth. What is it?
Memory bandwidth is physically how much data a CPU/GPU's memory can supply the processor per second. This is important because even
if you have 8 gigabytes of video memory that can be supplied at 400 gigabytes per second, and you're using 4 gigabytes, lowering
the memory usage to 2 gigabytes yields performance uplifts because of the speed 2gb can be transferred vs 4gb. For example, 2gb can be
supplied in 2/400 of a second while 4gb can be supplied in 4/400 of a second, longer. Thats why even if you're in the memory bounds of 8gb,
halving the memory means increased framerates. This is especially prevelent on onboard graphics, for example my dedicated graphics can be
supplied memory 672gb/s while my integrated graphics can only be supplied 90gb/s in lab conditions and even less in the real world as it 
has to share that bandwidth with the CPU. Why this happens is the 2 graphics chips use 2 different types of memory, DDR and GDDR. DDR is
tuned for low latency for CPUs, which need to sequentially process small blocks of data very quickly and are highly versatile. On the other
hand GDDR is optimized for bandwidth, meaning it has high latency but transfers data a lot faster than DDR, which is better for the GPU
which has to process billions of parallel, predicatable, and simple calculations. In other words, think of yourself living on the 30th floor
of an apartment in LA or NYC, and wanting water, a CPU which needs a "small amount of water fast" goes to the sink (think of the sink as DDR).
A GPU would travel down the elevator and open the fire hydrant on the street (think of the fire hydrant as GDDR, less accessible with longer
travel time but higher water output).

What did I do for it? I implemented a "bitpacking algorithm" or essentially I realized that the amount of memory that one piece of vertex
position data on the GPU takes up is very high, meaning its very high precision, but I didn't need that precision, so I took away some 
of that unneeded precision and packed 12 units (bytes) of memory into 8 (for the nerds, I had 3 float32s, x, y and z and then made them 
into float16s and  packed those float16s into 2 uint32s using a structuredbuffer). Then there was the colors of the geometry, usually
represented by 4 numbers with decimal places on the GPU to make math easier on it, or when being drawn, as one long whole number 
(for the nerds again, in the vertex/mesh shader it's 4 float32s and when drawing under the hood its one uint32). Since decimal numbers
take up a lot of memory and they were eventually going to be that one whole number, I decided to pack those 4 decimal place numbers into that
one whole number and convert it back when doing math on them. While that might sound complicated, modern GPUs are so powerful the operation
is trivial, it's basically free. The main bottleneck, transferring the data was wiped out by compressing 4 decimal numbers into 1 whole number.
Overall, my memory compression algorithm looked like this: 7 decimal numbers were converted into 3 position decimal numbers and 4 color decimal
numbers. Those 3 position decimal numbers were compressed into 2 and the color was compressed into 1 whole number netting a 57.1% decrease in
memory usage and time sending memory to the processing cores which especially helps on those bandwidth starved onboard integrated graphics. 
The code and format for it (for programmers):

UNCOMPRESSED:
```
triangle:
v1: x (float32), y (float32), z (float32), r (float32), g (float32), b (float32), a (float32)
v2: x (float32), y (float32), z (float32), r (float32), g (float32), b (float32), a (float32)
v3: x (float32), y (float32), z (float32), r (float32), g (float32), b (float32), a (float32)
```
```
COMPRESSED:
v1: xy (uint32 containing 2xfloat16, x, y), z0 (uint32 containing 1xfloat16, z, and binary 0000000000000000), ARGB (uint32)
v2: xy (uint32 containing 2xfloat16, x, y), z0 (uint32 containing 1xfloat16, z, and binary 0000000000000000), ARGB (uint32)
v3: xy (uint32 containing 2xfloat16, x, y), z0 (uint32 containing 1xfloat16, z, and binary 0000000000000000), ARGB (uint32)
```
CODE (packing, utils.hpp):
```cpp
std::vector<unsigned int> compress_vertices(std::vector<float> vertices) {
    std::vector<unsigned int> result;

    for (unsigned int i = 0; i < vertices.size(); i += 7) {
        result.push_back(pack_float2_to_half2(vertices[i], vertices[i + 1]));
        result.push_back(pack_float2_to_half2(vertices[i + 2], 0.0f));
        result.push_back(pack_rgba_to_argb(vertices[i + 3], vertices[i + 4], vertices[i + 5], vertices[i + 6]));
    }

    return result;
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
```

CODE (unpacking, vs.hlsl, ps.hlsl):

vs.hlsl:
```
float2 xy = unpack_uint_to_2float16(positions_colors[offset]);
float z = unpack_uint_to_2float16(positions_colors[offset + 1]).x;
```

```
output.position = float4(xy, z, 1.0f);
output.color = positions_colors[offset + 2];`
```
-----------------------------------------------
ps.hlsl:
```
return float4(unpackARGB(input.color).xyz, 1.0f);
```

Buffers:

Buffers are fancy words for chunks of memory allocated on the GPU. The main "vertex buffer" (ComPtr\<ID3D11Buffer\> vertex_buffer) is the chunk 
of memory that stores the vertex position and color data. The "dimensions buffer" (ComPtr\<ID3D11Buffer\> dimensions_buffer) is the chunk of memory
responsible for housing the window's width and height. The "triangle status buffer" (ComPtr\<ID3D11Buffer\> status_buffer) is the chunk of memory responsible
for housing every visible triangle's ID (each triangle has an "ID" that is it's position in memory, for example an ID of X means the triangle data starts at
memory address X * triangle data size to triangle data size - 1), its useful because it allows us to access any element within it a simple multiplication problem. 
The "indirect buffer" (ComPtr\<ID3D11Buffer\> indirect_buf) is the chunk of memory reponsible for handling the drawing commands. For example it holds how 
much geometry should be drawn, and "at which triangle ID do I start drawing".

The rendering and the problem:

When we draw out 5 million triangles, the result looks fine but in reality the GPU is stuck drawing all 5 million of them. This takes a long
time. To optimize we implement something called "occlusion culling" or we cull the triangles that are obscured by other triangles in the scene.
To do this we have to take the previous frame's depth data and use it to make an informed guess on what geometry in the next frame should be drawn.
Think of it as money in the bank with interest, the amount of money you own is not much different from now to next second. Same on the GPU: we can 
use the previous frame's data because the next frame is barely different in nature than the previous one. Using that, we use something called a
downsampling algorithm, a set of general purpose "shaders" (compue shaders, programs on the GPU) to take our high resolution frame, copy it many times
and then make several of those copies lower resolution. This makes it easier to cull, as for large triangles you don't need to compare their depth
against a high resolution depth buffer, but only against a few pixels on a low resolution depth buffer. Then with those comparisons, you can make an
informed decision, if the triangle is behind those few pixels, remove it, if the triangle is ahead of those pixels, don't remove it. The downsampling
algorithm also is conservative. To downsample a 2x2 quad of pixels into 1 pixel in a conservative, you have to take the farthest of them, 
and then make the final downsample.

Downsampling bottlenecks:

On the GPU, at first I had a very unoptimized downsampling algorithm that lowered FPS even though we were culling, the downsample took up more time.
What I originally did was have 1 thread (or sequential process) per group, meaning scheduling all of the thread scheduling (or the setup to determine
where, what and how a thread should execute) had massive overhead. To fix this I implemented a shader generation program. A whole seperate program 
would take the screen's resolution (main.py), and generate downsampling programs for the GPU that were explicitly optimized for that particular screen. 
They were optimized for the screen because they used a variable number of threads per group depending on resolution, meaning scheduling was negligible
This eventually led my downsampler to go from taking around 2ms to 40us, a 50x increase in speed.

This software is free to use for personal, educational, and independent projects. If this software is utilized in a commercial, for-profit capacity, the user is granted a free license up until the product or entity achieves $100,000 USD in cumulative gross revenue. Once this threshold is crossed, the user must contact the author to negotiate a custom commercial license or pay a certain percent royalty determined by the author.

You can reach me at my e-mail: nedis232250@gmail.com, or Ned_August@loomis.org (I check this one more).
