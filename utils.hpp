#pragma once
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <vector>
#include <consoleapi.h>
#include <fstream>
#include <string>
#include <cmath>
#include <DirectXMath.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <thread>
#include <cstring>
#include <wrl/client.h>

using high_resolution_clock = std::chrono::time_point<std::chrono::high_resolution_clock>;
using namespace Microsoft::WRL;

struct downscale_return_structure {
    std::string shader_string;
    unsigned int dispatchx;
    unsigned int dispatchy;
};

struct FPSReturn {
    bool second;
    unsigned long long frames_passed;
};

void attach_console() {
    if (AllocConsole()) {
        FILE* file;
        freopen_s(&file, "CONOUT$", "w", stdout);
        freopen_s(&file, "CONIN$", "r", stdin);
    }
}

//
// Pray to god this doesn't have any undefined behavior because I have zero idea how this works
// 
int find_cores() {
    DWORD bytes = 0;

    // 1. First call with a null pointer to let Windows tell us the exact size required
    if (!GetLogicalProcessorInformation(nullptr, &bytes)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return -1; // An unexpected native error occurred
        }
    }

    // 2. Mathematically calculate exactly how many elements to allocate
    size_t required_elements = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(required_elements);

    // 3. Second call with the perfectly sized buffer
    if (!GetLogicalProcessorInformation(buffer.data(), &bytes)) {
        return -1; // API failed unexpectedly
    }

    // 4. Count the physical cores securely within vector bounds
    int physical_cores = 0;
    size_t actual_elements = bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    for (size_t i = 0; i < actual_elements; ++i) {
        if (buffer.at(i).Relationship == RelationProcessorCore) {
            physical_cores++;
        }
    }

    return physical_cores;
}

//
// Pray to god this doesn't have any undefined behavior because I have zero idea how this works
// 
// # Of dev hours wasted trying to find out what this function means: 1
// Nedis232: 1 hour.
//
// (update this count)
//
std::vector<float> load_vertices(const char* path, unsigned long long num_triangles, unsigned int kThreads) {
    if (kThreads == 0) {
        kThreads = static_cast<unsigned int>(find_cores());
        if (kThreads == 0) kThreads = 4;
    }

    constexpr size_t kFloatsPerVertex = 7;
    constexpr size_t kNoFail = ~size_t(0);  // sentinel; avoids max() entirely

    // 1. Slurp the file (std::string guarantees a trailing '\0' for strtof).
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "failed to open file\n";
        return {};
    }
    const std::streamsize size = file.tellg();
    file.seekg(0);
    std::string buf(static_cast<size_t>(size), '\0');
    file.read(buf.data(), size);

    // 2. Index the first num_triangles*3 non-blank lines (one vertex per line).
    const size_t max_vertices = static_cast<size_t>(num_triangles * 3ULL);
    std::vector<size_t> line_starts;
    for (size_t pos = 0; pos < buf.size() && line_starts.size() < max_vertices;) {
        size_t eol = buf.find('\n', pos);
        if (eol == std::string::npos) eol = buf.size();
        size_t i = pos;
        while (i < eol && std::isspace(static_cast<unsigned char>(buf.at(i)))) ++i;
        if (i < eol) line_starts.push_back(pos);  // skip blank lines
        pos = eol + 1;
    }

    const size_t n = line_starts.size();
    std::vector<float> result(n * kFloatsPerVertex);
    std::vector<size_t> first_fail(kThreads, kNoFail);

    // 3. Parse one line into 7 floats; returns false on malformed input.
    auto parse_line = [&](size_t start, float* out) -> bool {
        const char* p = buf.c_str() + start;
        const char* eol = std::strchr(p, '\n');
        if (!eol) eol = buf.c_str() + buf.size();
        for (size_t k = 0; k < kFloatsPerVertex; ++k) {
            char* end;
            out[k] = std::strtof(p, &end);
            if (end == p || end > eol) return false;
            p = end;
            if (k + 1 < kFloatsPerVertex) {
                while (*p == ' ' || *p == '\t') ++p;
                if (*p != ',') return false;
                ++p;
            }
        }
        return true;  // trailing comma (if any) is simply ignored
        };

    // 4. Each thread handles a contiguous block of lines.
    const size_t chunk = (n + kThreads - 1) / kThreads;
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < kThreads; ++t) {
        const size_t begin = t * chunk;
        const size_t end = (std::min)(n, begin + chunk);  // parenthesized: macro-safe
        if (begin >= end) break;
        threads.emplace_back([&, t, begin, end] {
            for (size_t i = begin; i < end; ++i) {
                if (!parse_line(line_starts.at(i), &result.at(i * kFloatsPerVertex))) {
                    first_fail.at(t) = i;
                    return;
                }
            }
            });
    }
    for (auto& th : threads) th.join();

    // 5. Match the original's behavior: stop at the first malformed vertex.
    const size_t fail = *std::min_element(first_fail.begin(), first_fail.end());
    if (fail != kNoFail)
        result.resize(fail * kFloatsPerVertex);

    return result;
}

//
// NOT BF16
//
uint32_t pack_float2_to_half2(float a, float b) {
    auto to_f16 = [](float f) -> uint16_t {
        uint32_t x;
        memcpy(&x, &f, sizeof(x));

        uint16_t sign = static_cast<uint16_t>((x >> 31) << 15);
        uint16_t mantissa = static_cast<uint16_t>((x & 0x7FFFFF) >> 13);
        int32_t  exp = ((x >> 23) & 0xFF) - 127 + 15;

        if (exp <= 0)  return sign;           // underflow → ±0
        if (exp >= 31) return sign | 0x7C00;  // overflow  → ±inf

        return sign | (uint16_t)(exp << 10) | mantissa;
        };

    uint16_t ha = to_f16(a);
    uint16_t hb = to_f16(b);

    return ((uint32_t)hb << 16) | ha;
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

std::vector<unsigned int> compress_vertices(std::vector<float> vertices) {
    std::vector<unsigned int> result;

    for (unsigned int i = 0; i < vertices.size(); i += 7) {
        result.push_back(pack_float2_to_half2(vertices.at(i), vertices.at(i + 1)));
        result.push_back(std::bit_cast<uint32_t>(vertices.at(i + 2)));
        result.push_back(pack_rgba_to_argb(vertices.at(i + 3), vertices.at(i + 4), vertices.at(i + 5), vertices.at(i + 6)));
    }

    return result;
}

std::vector<downscale_return_structure> downscale_parser(std::vector<std::string> shader_lines) {
    std::vector<std::vector<std::string>> individual_shader_data;
    std::vector<std::string> temp;

    for (unsigned int i = 0; i < shader_lines.size(); i++) {
        if (shader_lines.at(i) == "[BREAKHERE]") {
            individual_shader_data.push_back(temp);
            temp = {};
            continue;
        }

        temp.push_back(shader_lines.at(i));
    }

    // Split shader data into the shader code and dispatch info

    std::vector<downscale_return_structure> downscale_shader_return;

    for (unsigned int i = 0; i < individual_shader_data.size(); i++) {
        std::string shader_code = "";
        unsigned int dispatchx = 0;
        unsigned int dispatchy = 0;


        for (unsigned int j = 0; j < individual_shader_data.at(i).size(); j++) {
            if (individual_shader_data.at(i).at(j) == "[DISPATCHINFO]") {
                std::string target_info = individual_shader_data.at(i).at(j + 1);
                std::string dispatchx_str = "";
                std::string dispatchy_str = "";
                bool dispatchx_mode = true;

                for (unsigned int k = 1; k < target_info.size(); k++) {
                    if (target_info.at(k) == ']') {
                        break;
                    }

                    if (target_info.at(k) == ',') {
                        dispatchx_mode = false;
                        continue;
                    }

                    if (dispatchx_mode) {
                        dispatchx_str += target_info.at(k);
                        continue;
                    }

                    dispatchy_str += target_info.at(k);
                }

                dispatchx = std::stoi(dispatchx_str);
                dispatchy = std::stoi(dispatchy_str);

                break;
            }

            shader_code += individual_shader_data.at(i).at(j) + "\n";
        }

        downscale_shader_return.push_back({ shader_code, dispatchx, dispatchy });
    }

    return downscale_shader_return;
}

class FPSCounter {
private:
    high_resolution_clock now;
    unsigned long long frame_counter = 0;
public:
    FPSCounter() {
        now = std::chrono::high_resolution_clock::now();
    }

    FPSReturn check_fps() {
        frame_counter++;
        high_resolution_clock later = std::chrono::high_resolution_clock::now();
        FPSReturn return_fps = { false, 0 };
        if (std::chrono::duration<double, std::milli>(later - now).count() >= 1000.0f) {
            now = later;
            return_fps = { true, frame_counter };
            frame_counter = 0;
        }

        return return_fps;
    }
};

void send_message(MSG msg) {
    while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void create_wndclass(LPCWSTR lp_class_name, WNDPROC w_proc) {
    WNDCLASS w_class = {};
    w_class.lpszClassName = lp_class_name;
    w_class.lpfnWndProc = w_proc;

    RegisterClassW(&w_class);
}

template<typename T> void release_all_ComPtr(std::vector<ComPtr<T>> ComPtr_to_release) {
    for (auto& el : ComPtr_to_release) {
        if (el) el->Release();
    }
}

std::vector<unsigned int> client_dimensions(HWND window) {
    RECT rect;
    if (GetClientRect(window, &rect)) {
        // For a client rect, rect.left and rect.top are always 0
        return { static_cast<unsigned int>(rect.right), static_cast<unsigned int>(rect.bottom) };
    }
}
