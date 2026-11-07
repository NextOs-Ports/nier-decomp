// SPDX-License-Identifier: GPL-3.0-only
// Minimal memory-PE adapter for the reused symbolic switch detector.
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
constexpr uint32_t SectionFlags_Code = 0x20000000;
struct Section {
    std::string name;
    uint32_t base, size, flags;
    const uint8_t* data;
};
struct Symbol { uint32_t address; };
struct Image {
    uint32_t base, size;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    const Section* Find(const char* name) const {
        for (const auto& s : sections) if (s.name == name) return &s;
        return nullptr;
    }
    const void* Find(uint32_t address) const {
        for (const auto& s : sections)
            if (address >= s.base && uint64_t(address)+4 <= uint64_t(s.base)+s.size)
                return s.data + address-s.base;
        return nullptr;
    }
    static Image ParseImage(const uint8_t* p, size_t n) {
        auto u32 = [&](size_t at) { uint32_t v; if (at+4>n) throw std::runtime_error("truncated PE"); std::memcpy(&v,p+at,4); return v; };
        auto u16 = [&](size_t at) { uint16_t v; if (at+2>n) throw std::runtime_error("truncated PE"); std::memcpy(&v,p+at,2); return v; };
        auto nt=u32(60), opt=nt+24;
        if (u16(0)!=0x5a4d || u32(nt)!=0x4550) throw std::runtime_error("invalid PE");
        Image result{u32(opt+28),uint32_t(n),{}, {}};
        auto table=opt+u16(nt+20);
        for (unsigned i=0;i<u16(nt+6);++i) {
            auto at=table+40*i, len=u32(at+8), rva=u32(at+12);
            if (uint64_t(rva)+len>n) continue;
            char name[9]={}; std::memcpy(name,p+at,8);
            result.sections.push_back({name,result.base+rva,len,u32(at+36),p+rva});
        }
        return result;
    }
};
