#pragma once
#include <string>
#include <iostream>
#include <vector>
#include <consoleapi.h>
#include <fstream>

struct downscale_return_structure {
    std::string shader;
    unsigned int dispatchx;
    unsigned int dispatchy;
};

template<typename T> struct make_2 {
    T el1;
    T el2;
};

void attach_console() {
    if (AllocConsole()) {
        FILE* file;
        freopen_s(&file, "CONOUT$", "w", stdout);
    }
}

std::vector<float> load_vertices(const char* path, unsigned long long num_triangles) {
    std::vector<float> result;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "failed to open file\n";
        return {};
    }

    num_triangles *= (unsigned long long)3; // num triangles -> num vertices
    float x, y, z, r, g, b, a;
    char comma;
    unsigned long long vertices_passed = (unsigned long long)0;
    while ((file >> x >> comma >> y >> comma >> z >> comma >> r >> comma >> g >> comma >> b >> comma >> a) && (vertices_passed < num_triangles)) { // 1 vertex per
        result.push_back(x);
        result.push_back(y);
        result.push_back(z);
        result.push_back(r);
        result.push_back(g);
        result.push_back(b);
        result.push_back(a);
        file >> comma; // consume trailing comma if present, harmless if not
        vertices_passed += (unsigned long long)1;
    }

    return result;
}

uint32_t pack_float2_to_half2(float a, float b) {
    auto to_f16 = [](float f) -> uint16_t {
        uint32_t x;
        memcpy(&x, &f, sizeof(x));

        uint16_t sign = (x >> 31) << 15;
        uint16_t mantissa = (x & 0x7FFFFF) >> 13;
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
        result.push_back(pack_float2_to_half2(vertices[i], vertices[i + 1]));
        result.push_back(pack_float2_to_half2(vertices[i + 2], 0.0f));
        result.push_back(pack_rgba_to_argb(vertices[i + 3], vertices[i + 4], vertices[i + 5], vertices[i + 6]));
    }

    return result;
}

std::vector<downscale_return_structure> downscale_parser(std::string file) {
    std::string raw_output;
    std::vector<std::string> shader_lines;
    std::ifstream read_file(file);

    // Get output text
    while (std::getline(read_file, raw_output)) {
        shader_lines.push_back(raw_output);
    }

    // split into individual shaders

    std::vector<std::vector<std::string>> individual_shader_data;
    std::vector<std::string> temp;

    for (unsigned int i = 0; i < shader_lines.size(); i++) {
        if (shader_lines[i] == "[BREAKHERE]") {
            individual_shader_data.push_back(temp);
            temp = {};
            continue;
        }

        temp.push_back(shader_lines[i]);
    }

    // Split shader data into the shader code and dispatch info

    std::vector<downscale_return_structure> downscale_shader_return;

    for (unsigned int i = 0; i < individual_shader_data.size(); i++) {
        std::string shader_code = "";
        unsigned int dispatchx = 0;
        unsigned int dispatchy = 0;

        for (unsigned int j = 0; j < individual_shader_data[i].size(); j++) {
            if (individual_shader_data[i][j] == "[DISPATCHINFO]") {
                std::string target_info = individual_shader_data[i][j + 1];
                std::string dispatchx_str = "";
                std::string dispatchy_str = "";
                bool dispatchx_mode = true;

                for (unsigned int k = 1; k < target_info.size(); k++) {
                    if (target_info[k] == ']') {
                        break;
                    }

                    if (target_info[k] == ',') {
                        dispatchx_mode = false;
                        continue;
                    }

                    if (dispatchx_mode) {
                        dispatchx_str += target_info[k];
                        continue;
                    }

                    dispatchy_str += target_info[k];
                }

                dispatchx = std::stoi(dispatchx_str);
                dispatchy = std::stoi(dispatchy_str);

                break;
            }

            shader_code += individual_shader_data[i][j] + "\n";
        }

        downscale_shader_return.push_back({ shader_code, dispatchx, dispatchy });
    }

    return downscale_shader_return;
}
