// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <fstream>
#include <iterator>
#include <vector>
inline std::vector<uint8_t> LoadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open PE image");
    return {(std::istreambuf_iterator<char>(f)), {}};
}
