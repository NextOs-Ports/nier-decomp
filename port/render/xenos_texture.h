// Decodificação de texturas da Xenos para RGBA8 (CPU): fetch constant, tiling 2D, troca de bytes,
// DXT1/3/5, 8888, 565, 4444, 8.
// As duas funções de endereço tiled seguem a fórmula da Xenia (texture_util, BSD-3-Clause,
// Copyright (c) 2015 Ben Vanik); o restante é código deste projeto (MIT).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nier::xenos {

inline uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

enum Format : uint32_t { k8 = 2, k1555 = 3, k565 = 4, k8888 = 6, k4444 = 15, kDXT1 = 18, kDXT3 = 19, kDXT5 = 20 };

struct TextureFetch {
  uint32_t base = 0, mipBase = 0, format = 0, endian = 0, width = 0, height = 0, pitch = 0;
  uint32_t clampX = 0, clampY = 0, dimension = 0;
  uint32_t swizzle = 0x688;  // palavra 3 bits 1-12: 3 bits por saída (x,y,z,w); 0-3 = componente, 4 = 0, 5 = 1
  bool tiled = false, packedMips = false;
};

// Fetch constant de textura (6 palavras big-endian).
inline TextureFetch DecodeFetch(const uint8_t* f) {
  uint32_t w0 = BE32(f), w1 = BE32(f + 4), w2 = BE32(f + 8), w3 = BE32(f + 12), w5 = BE32(f + 20);
  TextureFetch t;
  t.clampX = (w0 >> 10) & 7;
  t.clampY = (w0 >> 13) & 7;
  t.pitch = ((w0 >> 22) & 0x1FF) * 32;
  t.tiled = (w0 >> 31) & 1;
  t.format = w1 & 0x3F;
  t.endian = (w1 >> 6) & 3;
  t.base = w1 & 0xFFFFF000;
  t.width = (w2 & 0x1FFF) + 1;
  t.height = ((w2 >> 13) & 0x1FFF) + 1;
  t.dimension = (w5 >> 9) & 3;
  t.swizzle = (w3 >> 1) & 0xFFF;
  t.packedMips = (w5 >> 11) & 1;  // a confirmar contra a captura
  t.mipBase = w5 & 0xFFFFF000;
  return t;
}

inline bool Supported(uint32_t format) {
  switch (format) {
    case k8: case k565: case k8888: case k4444: case kDXT1: case kDXT3: case kDXT5: return true;
    default: return false;
  }
}
inline bool Block(uint32_t format) { return format == kDXT1 || format == kDXT3 || format == kDXT5; }
inline uint32_t BytesPerElement(uint32_t format) {
  switch (format) {
    case k8: return 1;
    case k565: case k4444: case k1555: return 2;
    case k8888: return 4;
    case kDXT1: return 8;
    case kDXT3: case kDXT5: return 16;
    default: return 0;
  }
}

// Xenia texture_util (BSD-3-Clause): deslocamento em bytes de (x, y) numa superfície tiled 2D.
inline int32_t TiledOuter(int32_t y, int32_t width, uint32_t log2Bpp) {
  int32_t macro = ((y >> 5) * (width >> 5)) << (log2Bpp + 7);
  int32_t micro = ((y & 6) << 2) << log2Bpp;
  return macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 8) << (3 + log2Bpp)) + ((y & 1) << 4);
}
inline int32_t TiledInner(int32_t x, int32_t y, uint32_t log2Bpp, int32_t base) {
  int32_t macro = (x >> 5) << (log2Bpp + 7);
  int32_t micro = (x & 7) << log2Bpp;
  int32_t offset = base + (macro + ((micro & ~0xF) << 1) + (micro & 0xF));
  return ((offset & ~0x1FF) << 3) + ((offset & 0x1C0) << 2) + (offset & 0x3F) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

// Troca de bytes do endian da fetch constant: 1 = 8in16, 2 = 8in32, 3 = 16in32.
inline void Swap(uint8_t* p, uint32_t n, uint32_t endian) {
  if (endian == 1) for (uint32_t i = 0; i + 1 < n; i += 2) std::swap(p[i], p[i + 1]);
  else if (endian == 2) for (uint32_t i = 0; i + 3 < n; i += 4) { std::swap(p[i], p[i + 3]); std::swap(p[i + 1], p[i + 2]); }
  else if (endian == 3) for (uint32_t i = 0; i + 3 < n; i += 4) { std::swap(p[i], p[i + 2]); std::swap(p[i + 1], p[i + 3]); }
}

inline void Rgb565(uint16_t c, uint8_t* o) {
  o[0] = uint8_t(((c >> 11) & 31) * 255 / 31); o[1] = uint8_t(((c >> 5) & 63) * 255 / 63); o[2] = uint8_t((c & 31) * 255 / 31);
}
inline void DecodeColorBlock(const uint8_t* b, uint8_t out[16][4], bool dxt1) {
  uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
  uint8_t pal[4][4];
  Rgb565(c0, pal[0]); Rgb565(c1, pal[1]); pal[0][3] = pal[1][3] = 255;
  for (int k = 0; k < 3; ++k) {
    if (!dxt1 || c0 > c1) { pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3); pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3); }
    else { pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
  }
  pal[2][3] = 255; pal[3][3] = (dxt1 && c0 <= c1) ? 0 : 255;
  uint32_t bits = uint32_t(b[4]) | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24;
  for (int i = 0; i < 16; ++i) std::memcpy(out[i], pal[(bits >> (2 * i)) & 3], 4);
}
inline void DecodeAlpha5(const uint8_t* b, uint8_t out[16][4]) {
  uint8_t a[8] = {b[0], b[1]};
  if (a[0] > a[1]) for (int i = 1; i < 7; ++i) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
  else { for (int i = 1; i < 5; ++i) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5); a[6] = 0; a[7] = 255; }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) out[i][3] = a[(bits >> (3 * i)) & 7];
}

// Lê `size` bytes de memória física guest em `address`; nulo se fora da faixa.
using Reader = const uint8_t* (*)(uint32_t address, uint32_t size);

// Decodifica o nível 0 para RGBA8 (width*height*4 bytes em `out`). Devolve false se não suportado.
inline bool DecodeLevel0(Reader read, const TextureFetch& t, std::vector<uint8_t>& out) {
  if (!Supported(t.format) || t.dimension != 1 /* 2D */) return false;
  const bool block = Block(t.format);
  const uint32_t bpe = BytesPerElement(t.format);
  const uint32_t bw = block ? (t.width + 3) / 4 : t.width, bh = block ? (t.height + 3) / 4 : t.height;
  const uint32_t pitchE = std::max((block ? t.pitch / 4 : t.pitch), bw);
  const uint32_t alignedW = (pitchE + 31) & ~31u, alignedH = (bh + 31) & ~31u;
  const uint32_t span = alignedW * alignedH * bpe;
  const uint8_t* src = read(t.base, span);
  if (!src) return false;
  uint32_t log2Bpp = bpe == 1 ? 0 : bpe == 2 ? 1 : bpe == 4 ? 2 : bpe == 8 ? 3 : 4;
  out.assign(size_t(t.width) * t.height * 4, 0);
  uint8_t e[16];
  for (uint32_t y = 0; y < bh; ++y) {
    int32_t rowBase = t.tiled ? TiledOuter(int32_t(y), int32_t(alignedW), log2Bpp) : 0;
    for (uint32_t x = 0; x < bw; ++x) {
      uint32_t off = t.tiled ? uint32_t(TiledInner(int32_t(x), int32_t(y), log2Bpp, rowBase)) : (y * pitchE + x) * bpe;
      if (off + bpe > span) continue;
      std::memcpy(e, src + off, bpe);
      Swap(e, bpe, t.endian);
      if (block) {
        uint8_t px[16][4];
        if (t.format == kDXT1) DecodeColorBlock(e, px, true);
        else {
          DecodeColorBlock(e + 8, px, false);
          if (t.format == kDXT5) DecodeAlpha5(e, px);
          else for (int i = 0; i < 16; ++i) { uint8_t n = uint8_t((e[i / 2] >> (4 * (i & 1))) & 15); px[i][3] = uint8_t(n * 17); }
        }
        for (int i = 0; i < 16; ++i) {
          uint32_t px_x = x * 4 + (i & 3), px_y = y * 4 + (i >> 2);
          if (px_x < t.width && px_y < t.height) std::memcpy(&out[(size_t(px_y) * t.width + px_x) * 4], px[i], 4);
        }
        continue;
      }
      uint8_t* o = &out[(size_t(y) * t.width + x) * 4];
      switch (t.format) {
        case k8888: o[0] = e[0]; o[1] = e[1]; o[2] = e[2]; o[3] = e[3]; break;  // componentes x y z w (swizzle abaixo)
        case k565: Rgb565(uint16_t(e[0] | e[1] << 8), o); o[3] = 255; break;
        case k4444: { uint16_t c = uint16_t(e[0] | e[1] << 8); o[0] = uint8_t(((c >> 8) & 15) * 17); o[1] = uint8_t(((c >> 4) & 15) * 17); o[2] = uint8_t((c & 15) * 17); o[3] = uint8_t((c >> 12) * 17); break; }
        case k8: o[0] = o[1] = o[2] = o[3] = e[0]; break;
      }
    }
  }
  // Swizzle da fetch constant (como a Xenia: amostra os componentes e depois escolhe x/y/z/w).
  if (t.swizzle != 0x688) {
    uint32_t sel[4];
    for (int c = 0; c < 4; ++c) sel[c] = (t.swizzle >> (3 * c)) & 7;
    for (size_t i = 0; i < out.size(); i += 4) {
      uint8_t in[4] = {out[i], out[i + 1], out[i + 2], out[i + 3]};
      for (int c = 0; c < 4; ++c) out[i + c] = sel[c] < 4 ? in[sel[c]] : sel[c] == 5 ? 255 : 0;
    }
  }
  return true;
}

}  // namespace nier::xenos
