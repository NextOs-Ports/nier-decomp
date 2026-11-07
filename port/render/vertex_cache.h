// Immutable guest vertex/index snapshots. CPU cache is owned by the recording thread.
#pragma once
#include "shader_runtime.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <list>
#include <memory>
#include <unordered_map>
namespace nier::render::buffers {
// Palavra de 32 bits como a GPU a vê, com o endian da fetch constant (0 nenhum, 1 8in16, 2 8in32, 3 16in32).
inline uint32_t GpuWord(const uint8_t* p, uint32_t endian) {
  uint32_t w; std::memcpy(&w, p, 4);
  switch (endian) {
    case 1: return ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
    case 2: return __builtin_bswap32(w);
    case 3: return (w << 16) | (w >> 16);
    default: return w;
  }
}
inline float Half(uint16_t h) {
  uint32_t s = uint32_t(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, f;
  if (!e) { if (!m) f = s; else { e = 127 - 14; while (!(m & 1024)) { m <<= 1; --e; } m &= 1023; f = s | e << 23 | m << 13; } }
  else if (e == 31) f = s | 0x7F800000u | m << 13;
  else f = s | (e + 112) << 23 | m << 13;
  float v; std::memcpy(&v, &f, 4); return v;
}
inline float Component(uint32_t raw, int bits, const VertexInput& in, float scale) {
  float v;
  if (in.isSigned) {
    int32_t x = int32_t(raw << (32 - bits)) >> (32 - bits);
    v = in.isInteger ? float(x) : std::max(float(x) / float((1u << (bits - 1)) - 1), -1.0f);
  } else {
    v = in.isInteger ? float(raw) : float(raw) / float((bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1));
  }
  return v * scale;
}
// Converte `count` vértices a partir de `first` para float4 (x, y, z, w; ausentes = 0,0,0,1).
inline bool ConvertInput(const VertexInput& in, const uint8_t* base, uint32_t endian, uint32_t first, uint32_t count, float* out) {
  float scale = in.expAdjust ? std::ldexp(1.0f, in.expAdjust) : 1.0f;
  for (uint32_t v = 0; v < count; ++v) {
    const uint8_t* p = base + size_t((first + v) * in.stride + in.offset) * 4;
    float* d = out + size_t(v) * 4;
    d[0] = d[1] = d[2] = 0; d[3] = 1;
    auto f32 = [&](int i) { uint32_t w = GpuWord(p + 4 * i, endian); float x; std::memcpy(&x, &w, 4); return x * scale; };
    switch (in.format) {
      case 6: { uint32_t w = GpuWord(p, endian); for (int c = 0; c < 4; ++c) d[c] = Component((w >> (8 * c)) & 0xFF, 8, in, scale); break; }
      case 7: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 1023, 10, in, scale); d[1] = Component((w >> 10) & 1023, 10, in, scale); d[2] = Component((w >> 20) & 1023, 10, in, scale); d[3] = Component(w >> 30, 2, in, scale); break; }
      case 16: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 2047, 11, in, scale); d[1] = Component((w >> 11) & 2047, 11, in, scale); d[2] = Component(w >> 22, 10, in, scale); break; }
      case 17: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 1023, 10, in, scale); d[1] = Component((w >> 10) & 2047, 11, in, scale); d[2] = Component(w >> 21, 11, in, scale); break; }
      case 25: { uint32_t w = GpuWord(p, endian); d[0] = Component(w & 0xFFFF, 16, in, scale); d[1] = Component(w >> 16, 16, in, scale); break; }
      case 26: { uint32_t a = GpuWord(p, endian), b = GpuWord(p + 4, endian); d[0] = Component(a & 0xFFFF, 16, in, scale); d[1] = Component(a >> 16, 16, in, scale); d[2] = Component(b & 0xFFFF, 16, in, scale); d[3] = Component(b >> 16, 16, in, scale); break; }
      case 31: { uint32_t w = GpuWord(p, endian); d[0] = Half(uint16_t(w)); d[1] = Half(uint16_t(w >> 16)); break; }
      case 32: { uint32_t a = GpuWord(p, endian), b = GpuWord(p + 4, endian); d[0] = Half(uint16_t(a)); d[1] = Half(uint16_t(a >> 16)); d[2] = Half(uint16_t(b)); d[3] = Half(uint16_t(b >> 16)); break; }
      case 33: d[0] = Component(GpuWord(p, endian), 32, in, scale); break;
      case 34: d[0] = Component(GpuWord(p, endian), 32, in, scale); d[1] = Component(GpuWord(p + 4, endian), 32, in, scale); break;
      case 35: for (int c = 0; c < 4; ++c) d[c] = Component(GpuWord(p + 4 * c, endian), 32, in, scale); break;
      case 36: d[0] = f32(0); break;
      case 37: d[0] = f32(0); d[1] = f32(1); break;
      case 38: for (int c = 0; c < 4; ++c) d[c] = f32(c); break;
      case 57: d[0] = f32(0); d[1] = f32(1); d[2] = f32(2); break;
      default: return false;
    }
  }
  return true;
}

inline uint32_t ElementBytes(uint32_t format) {
  return format == 57 ? 12 : (format == 38 || format == 35) ? 16 :
    (format == 26 || format == 32 || format == 34 || format == 37) ? 8 : 4;
}
struct Layout {
  GLenum type = GL_FLOAT;
  uint8_t components = 4, bytes = 16;
  bool normalized = false;
};
inline Layout NativeLayout(const VertexInput& in, bool half) {
  Layout l;
  // GLES2 signed-normalized mapping may differ from max(c/32767,-1).
  // Keep that case, exponent adjustment and packed 10/11-bit fields on float.
  if ((in.format == 31 || in.format == 32) && half)
    return {GL_HALF_FLOAT_OES, uint8_t(in.format == 31 ? 2 : 4), uint8_t(in.format == 31 ? 4 : 8), false};
  if (in.expAdjust) return l;
  if (in.format == 6 && (!in.isSigned || in.isInteger))
    return {in.isSigned ? GLenum(GL_BYTE) : GLenum(GL_UNSIGNED_BYTE), 4, 4, !in.isInteger};
  if ((in.format == 25 || in.format == 26) && (!in.isSigned || in.isInteger))
    return {in.isSigned ? GLenum(GL_SHORT) : GLenum(GL_UNSIGNED_SHORT), uint8_t(in.format == 25 ? 2 : 4), uint8_t(in.format == 25 ? 4 : 8), !in.isInteger};
  switch (in.format) {
    case 36: return {GL_FLOAT, 1, 4, false};
    case 37: return {GL_FLOAT, 2, 8, false};
    case 38: return {GL_FLOAT, 4, 16, false};
    case 57: return {GL_FLOAT, 3, 12, false};
    default: return l;
  }
}
struct Payload {
  uint64_t serial = 0, sample = 0;
  VertexInput input{}; // tightly packed source descriptor for reference checks
  uint32_t endian = 0, count = 0;
  Layout layout;
  std::vector<uint8_t> raw, bytes;
};
using Ref = std::shared_ptr<const Payload>;
struct Key {
  uint64_t address, descriptor;
  uint32_t stride, count, element;
  bool operator==(const Key&) const = default;
};
struct KeyHash {
  size_t operator()(const Key& k) const {
    uint64_t h = k.address ^ (k.descriptor * 0x9e3779b97f4a7c15ull);
    return size_t(h ^ (uint64_t(k.stride) << 32) ^ uint64_t(k.count) * 0x100000001b3ull ^ k.element);
  }
};
// At most 16 four-byte samples, followed by EXACT comparison on a hit.
// An unsampled mutation or a hash collision never makes stale data reusable.
inline uint64_t Sample(const uint8_t* p, uint32_t stride, uint32_t count, uint32_t element) {
  uint64_t h = 0xcbf29ce484222325ull;
  const size_t size = size_t(count) * element;
  for (size_t i = 0, n = std::min<size_t>(16, size); i < n; ++i) {
    size_t at = n > 1 ? i * (size - 1) / (n - 1) : 0;
    h = (h ^ p[(at / element) * stride + at % element]) * 0x100000001b3ull;
  }
  return h;
}
template<size_t Width> inline bool EqualStrided(const uint8_t* packed, const uint8_t* source, uint32_t count, uint32_t stride) {
  for (uint32_t i = 0; i < count; ++i)
    if (std::memcmp(packed + size_t(i) * Width, source + size_t(i) * stride, Width)) return false;
  return true;
}
inline bool EqualSource(const std::vector<uint8_t>& packed, const uint8_t* source, const Key& key) {
  if (key.stride == key.element) return !std::memcmp(packed.data(), source, packed.size());
  // Constant-size comparisons inline on A53 instead of one libc call per vertex.
  switch (key.element) {
    case 4: return EqualStrided<4>(packed.data(), source, key.count, key.stride);
    case 8: return EqualStrided<8>(packed.data(), source, key.count, key.stride);
    case 12: return EqualStrided<12>(packed.data(), source, key.count, key.stride);
    case 16: return EqualStrided<16>(packed.data(), source, key.count, key.stride);
    default: return false;
  }
}
class Cache {
  struct Entry { Ref data; std::list<Key>::iterator age; };
  std::unordered_map<Key, Entry, KeyHash> entries_;
  std::list<Key> age_;
  size_t bytes_ = 0, limit_;
  uint64_t serial_ = 0;
 public:
  std::atomic<uint64_t> hits{0}, misses{0}, converted{0};
  explicit Cache(size_t limit = 64u << 20) : limit_(limit) {}
  size_t resident_bytes() const { return bytes_; } // recording thread only
  void Clear() { entries_.clear(); age_.clear(); bytes_ = 0; }
  template<class Build> Ref Get(const Key& key, const uint8_t* source, Build build) {
    uint64_t sample = Sample(source, key.stride, key.count, key.element);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
      const auto& old = it->second.data;
      bool same = old->sample == sample;
      if (same) same = EqualSource(old->raw, source, key);
      if (same) { age_.splice(age_.end(), age_, it->second.age); ++hits; return old; }
      bytes_ -= old->raw.size() + old->bytes.size();
      age_.erase(it->second.age); entries_.erase(it);
    }
    auto data = std::make_shared<Payload>();
    data->serial = ++serial_; data->sample = sample;
    data->raw.resize(size_t(key.count) * key.element);
    if (key.stride == key.element) std::memcpy(data->raw.data(), source, data->raw.size());
    else for (uint32_t i = 0; i < key.count; ++i)
      std::memcpy(data->raw.data() + size_t(i) * key.element, source + size_t(i) * key.stride, key.element);
    if (!build(*data)) return {};
    ++misses; converted += data->bytes.size();
    size_t cost = data->raw.size() + data->bytes.size();
    while (!age_.empty() && (bytes_ + cost > limit_ || entries_.size() >= 4096)) {
      auto victim = entries_.find(age_.front());
      bytes_ -= victim->second.data->raw.size() + victim->second.data->bytes.size();
      entries_.erase(victim); age_.pop_front();
    }
    if (cost <= limit_) {
      age_.push_back(key); entries_.emplace(key, Entry{data, std::prev(age_.end())}); bytes_ += cost;
    }
    return data; // outstanding frames keep evicted snapshots alive
  }
  Ref Vertex(const VertexInput& in, const uint8_t* base, uint32_t endian, uint32_t count, bool half) {
    uint32_t element = ElementBytes(in.format);
    const uint8_t* source = base + size_t(in.offset) * 4;
    uint64_t descriptor = in.format | (uint64_t(endian) << 8) | (uint64_t(in.isSigned) << 10) |
      (uint64_t(in.isInteger) << 11) | (uint64_t(half) << 12) | (uint64_t(uint32_t(in.expAdjust)) << 32);
    Key key{uintptr_t(source), descriptor, in.stride * 4, count, element};
    return Get(key, source, [&](Payload& p) {
      p.input = in; p.input.stride = element / 4; p.input.offset = 0;
      p.endian = endian; p.count = count; p.layout = NativeLayout(in, half);
      p.bytes.resize(size_t(count) * p.layout.bytes);
      const bool native = p.layout.type != GL_FLOAT ||
        (!in.expAdjust && (in.format == 36 || in.format == 37 || in.format == 38 || in.format == 57));
      if (native) {
        for (size_t i = 0; i < p.bytes.size(); i += 4) {
          uint32_t word = GpuWord(p.raw.data() + i, endian);
          std::memcpy(p.bytes.data() + i, &word, 4);
        }
        return true;
      }
      return ConvertInput(p.input, p.raw.data(), endian, 0, count, reinterpret_cast<float*>(p.bytes.data()));
    });
  }
  Ref Indices(uint64_t address, const uint16_t* src, uint32_t count) {
    return Get({address, ~0ull, 2, count, 2}, reinterpret_cast<const uint8_t*>(src), [&](Payload& p) {
      p.bytes = p.raw; p.count = count; p.layout = {GL_UNSIGNED_SHORT, 1, 2, false}; return true;
    });
  }
};
} // namespace nier::render::buffers
