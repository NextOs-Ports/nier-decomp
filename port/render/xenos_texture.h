// Decodificação de texturas da Xenos para RGBA8 (CPU): fetch constant, tiling 2D, troca de bytes,
// DXT1/3/5, DXN, DXT3A/DXT5A, CTX1, 8888, 565, 4444, 1555, 2-10-10-10, 8, 8_8, 16/16_16/16_16_16_16 (unorm e half),
// 32_FLOAT. Componentes ausentes seguem o hardware: (x, 0, 0, 1) antes do swizzle da fetch constant.
// As duas funções de endereço tiled seguem a fórmula da Xenia (texture_util, BSD-3-Clause,
// Copyright (c) 2015 Ben Vanik); o restante é código deste projeto (MIT).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nier::xenos {

inline uint32_t BE32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

enum Format : uint32_t {
  k8 = 2, k1555 = 3, k565 = 4, k8888 = 6, k2101010 = 7, k8A = 8, k8B = 9, k88 = 10, k4444 = 15,
  kDXT1 = 18, kDXT3 = 19, kDXT5 = 20, k248 = 22, k16 = 24, k1616 = 25, k16161616 = 26,
  k16Expand = 27, k1616Expand = 28, k16161616Expand = 29, k16Float = 30, k1616Float = 31, k16161616Float = 32,
  k32Float = 36, kDXN = 49, kDXT3A = 58, kDXT5A = 59, kCTX1 = 60
};

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

inline bool Block(uint32_t format) {
  switch (format) {
    case kDXT1: case kDXT3: case kDXT5: case kDXN: case kDXT3A: case kDXT5A: case kCTX1: return true;
    default: return false;
  }
}
inline uint32_t BytesPerElement(uint32_t format) {
  switch (format) {
    case k8: case k8A: case k8B: return 1;
    case k565: case k4444: case k1555: case k88: case k16: case k16Expand: case k16Float: return 2;
    case k8888: case k2101010: case k1616: case k1616Expand: case k1616Float: case k32Float: return 4;
    case k16161616: case k16161616Expand: case k16161616Float: return 8;
    case kDXT1: case kDXT3A: case kDXT5A: case kCTX1: return 8;
    case kDXT3: case kDXT5: case kDXN: return 16;
    default: return 0;
  }
}
inline bool Supported(uint32_t format) { return BytesPerElement(format) != 0; }
// 16_16_16_16_EXPAND e irmãos: a Xenia os carrega como half float (R16G16B16A16_FLOAT).
inline bool HalfFormat(uint32_t f) { return f >= k16Expand && f <= k16161616Float; }
inline float HalfToFloat(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
  float v;
  if (e == 0) v = float(m) * (1.0f / 16777216.0f);          // subnormal: m * 2^-24
  else if (e == 31) v = m ? 0.0f : 65504.0f;                 // NaN -> 0, inf -> máximo
  else { v = (1.0f + float(m) / 1024.0f); int sh = int(e) - 15; v = sh >= 0 ? v * float(1u << sh) : v / float(1u << -sh); }
  return s ? -v : v;
}
inline uint8_t Unorm(float v) { return uint8_t(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); }
// BC4 (bloco de 8 bytes): 8 valores de 8 bits -> out[16]
inline void DecodeBc4(const uint8_t* b, uint8_t out[16]) {
  uint8_t a[8] = {b[0], b[1]};
  if (a[0] > a[1]) for (int i = 1; i < 7; ++i) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1]) / 7);
  else { for (int i = 1; i < 5; ++i) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1]) / 5); a[6] = 0; a[7] = 255; }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
  for (int i = 0; i < 16; ++i) out[i] = a[(bits >> (3 * i)) & 7];
}
// CTX1 (bloco de 8 bytes): dois pontos (x0,y0) (x1,y1) de 8 bits e 2 bits de índice por texel.
inline void DecodeCtx1(const uint8_t* b, uint8_t out[16][4]) {
  uint8_t pal[4][2] = {{b[0], b[1]}, {b[2], b[3]}, {uint8_t((2 * b[0] + b[2]) / 3), uint8_t((2 * b[1] + b[3]) / 3)},
                       {uint8_t((b[0] + 2 * b[2]) / 3), uint8_t((b[1] + 2 * b[3]) / 3)}};
  uint32_t bits = uint32_t(b[4]) | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24;
  for (int i = 0; i < 16; ++i) { uint32_t k = (bits >> (2 * i)) & 3; out[i][0] = pal[k][0]; out[i][1] = pal[k][1]; out[i][2] = 0; out[i][3] = 255; }
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
        else if (t.format == kDXT3 || t.format == kDXT5) {
          DecodeColorBlock(e + 8, px, false);
          if (t.format == kDXT5) DecodeAlpha5(e, px);
          else for (int i = 0; i < 16; ++i) { uint8_t n = uint8_t((e[i / 2] >> (4 * (i & 1))) & 15); px[i][3] = uint8_t(n * 17); }
        } else if (t.format == kDXN) {  // dois blocos BC4: x e y (mapa de normais)
          uint8_t r[16], g[16];
          DecodeBc4(e, r); DecodeBc4(e + 8, g);
          for (int i = 0; i < 16; ++i) { px[i][0] = r[i]; px[i][1] = g[i]; px[i][2] = 0; px[i][3] = 255; }
        } else if (t.format == kDXT5A) {
          uint8_t r[16];
          DecodeBc4(e, r);
          for (int i = 0; i < 16; ++i) { px[i][0] = r[i]; px[i][1] = px[i][2] = 0; px[i][3] = 255; }
        } else if (t.format == kDXT3A) {
          for (int i = 0; i < 16; ++i) { uint8_t n = uint8_t((e[i / 2] >> (4 * (i & 1))) & 15); px[i][0] = uint8_t(n * 17); px[i][1] = px[i][2] = 0; px[i][3] = 255; }
        } else {
          DecodeCtx1(e, px);
        }
        for (int i = 0; i < 16; ++i) {
          uint32_t px_x = x * 4 + (i & 3), px_y = y * 4 + (i >> 2);
          if (px_x < t.width && px_y < t.height) std::memcpy(&out[(size_t(px_y) * t.width + px_x) * 4], px[i], 4);
        }
        continue;
      }
      uint8_t* o = &out[(size_t(y) * t.width + x) * 4];
      o[1] = o[2] = 0; o[3] = 255;
      switch (t.format) {
        case k8888: o[0] = e[0]; o[1] = e[1]; o[2] = e[2]; o[3] = e[3]; break;  // componentes x y z w (swizzle abaixo)
        case k565: Rgb565(uint16_t(e[0] | e[1] << 8), o); break;
        case k4444: { uint16_t c = uint16_t(e[0] | e[1] << 8); o[0] = uint8_t(((c >> 8) & 15) * 17); o[1] = uint8_t(((c >> 4) & 15) * 17); o[2] = uint8_t((c & 15) * 17); o[3] = uint8_t((c >> 12) * 17); break; }
        case k1555: { uint16_t c = uint16_t(e[0] | e[1] << 8); o[0] = uint8_t(((c >> 10) & 31) * 255 / 31); o[1] = uint8_t(((c >> 5) & 31) * 255 / 31); o[2] = uint8_t((c & 31) * 255 / 31); o[3] = (c >> 15) ? 255 : 0; break; }
        case k2101010: { uint32_t c = uint32_t(e[0]) | e[1] << 8 | e[2] << 16 | uint32_t(e[3]) << 24; o[0] = uint8_t((c & 1023) * 255 / 1023); o[1] = uint8_t(((c >> 10) & 1023) * 255 / 1023); o[2] = uint8_t(((c >> 20) & 1023) * 255 / 1023); o[3] = uint8_t((c >> 30) * 85); break; }
        case k8: o[0] = o[1] = o[2] = o[3] = e[0]; break;  // mantido: planos de vídeo dependem da réplica
        case k8A: case k8B: o[0] = e[0]; break;
        case k88: o[0] = e[0]; o[1] = e[1]; break;
        case k16: o[0] = e[1]; break;
        case k1616: o[0] = e[1]; o[1] = e[3]; break;
        case k16161616: o[0] = e[1]; o[1] = e[3]; o[2] = e[5]; o[3] = e[7]; break;
        case k16Expand: case k16Float: o[0] = Unorm(HalfToFloat(uint16_t(e[0] | e[1] << 8))); break;
        case k1616Expand: case k1616Float: o[0] = Unorm(HalfToFloat(uint16_t(e[0] | e[1] << 8))); o[1] = Unorm(HalfToFloat(uint16_t(e[2] | e[3] << 8))); break;
        case k16161616Expand: case k16161616Float:
          for (int c = 0; c < 4; ++c) o[c] = Unorm(HalfToFloat(uint16_t(e[2 * c] | e[2 * c + 1] << 8)));
          break;
        case k32Float: { float v; std::memcpy(&v, e, 4); o[0] = Unorm(v); break; }
        default: o[0] = 255; o[1] = o[2] = 255; break;
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

// Caminho rápido para k8 (planos de vídeo): destila direto para LUMINANCE_ALPHA (L = A = valor).
inline bool DecodeK8LumAlpha(Reader read, const TextureFetch& t, std::vector<uint8_t>& out) {
  if (t.format != k8 || t.dimension != 1) return false;
  const uint32_t pitchE = std::max(t.pitch, t.width);
  const uint32_t alignedW = (pitchE + 31) & ~31u, alignedH = (t.height + 31) & ~31u;
  const uint32_t span = alignedW * alignedH;
  const uint8_t* src = read(t.base, span);
  if (!src) return false;
  const uint32_t flip = t.endian == 1 ? 1 : t.endian == 2 ? 3 : t.endian == 3 ? 2 : 0;  // troca de bytes = XOR
  out.resize(size_t(t.width) * t.height * 2);
  uint8_t* o = out.data();
  for (uint32_t y = 0; y < t.height; ++y) {
    if (!t.tiled) {
      const uint8_t* row = src + size_t(y) * pitchE;
      for (uint32_t x = 0; x < t.width; ++x) { uint8_t v = row[x ^ flip]; *o++ = v; *o++ = v; }
      continue;
    }
    const int32_t base = TiledOuter(int32_t(y), int32_t(alignedW), 0);
    // 1 byte/elemento: x..x+7 (x múltiplo de 8) ocupam 8 bytes contíguos; troca de bytes = XOR dentro deles.
    uint32_t x = 0;
    for (; x + 8 <= t.width; x += 8) {
      uint32_t off = uint32_t(TiledInner(int32_t(x), int32_t(y), 0, base));
      if (off + 8 > span) { for (int k = 0; k < 8; ++k) { *o++ = 0; *o++ = 0; } continue; }
      const uint8_t* run = src + off;
      for (uint32_t k = 0; k < 8; ++k) { uint8_t v = run[k ^ flip]; *o++ = v; *o++ = v; }
    }
    for (; x < t.width; ++x) {
      uint32_t off = uint32_t(TiledInner(int32_t(x), int32_t(y), 0, base)) ^ flip;
      uint8_t v = off < span ? src[off] : 0;
      *o++ = v; *o++ = v;
    }
  }
  return true;
}

// k8 com swizzle (x,x,x,1) (planos de vídeo SFD): 1 byte por texel para GL_LUMINANCE.
inline bool DecodeK8Luminance(Reader read, const TextureFetch& t, std::vector<uint8_t>& out) {
  if (t.format != k8 || t.dimension != 1) return false;
  const uint32_t pitchE = std::max(t.pitch, t.width);
  const uint32_t alignedW = (pitchE + 31) & ~31u, alignedH = (t.height + 31) & ~31u;
  const uint32_t span = t.tiled ? alignedW * alignedH : pitchE * t.height;
  const uint8_t* src = read(t.base, span);
  if (!src) return false;
  const uint32_t flip = t.endian == 1 ? 1 : t.endian == 2 ? 3 : t.endian == 3 ? 2 : 0;
  out.resize(size_t(t.width) * t.height);
  for (uint32_t y = 0; y < t.height; ++y) {
    uint8_t* o = out.data() + size_t(y) * t.width;
    if (!t.tiled) {
      const uint8_t* row = src + size_t(y) * pitchE;
      if (!flip) std::memcpy(o, row, t.width);
      else for (uint32_t x = 0; x < t.width; ++x) o[x] = row[x ^ flip];
      continue;
    }
    const int32_t base = TiledOuter(int32_t(y), int32_t(alignedW), 0);
    for (uint32_t x = 0; x < t.width; x += 8) {
      uint32_t off = uint32_t(TiledInner(int32_t(x), int32_t(y), 0, base));
      for (uint32_t k = 0; k < 8 && x + k < t.width; ++k) { uint32_t a = (off + k) ^ flip; o[x + k] = a < span ? src[a] : 0; }
    }
  }
  return true;
}

}  // namespace nier::xenos
