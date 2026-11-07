#pragma once
#include "xenos_texture.h"
#include <string>
namespace nier::render {
// Signs belong to source components, before fetch swizzle. Constants 0/1 do
// not inherit a sign. Pack four output flags per used fragment sampler.
inline uint64_t TextureGammaChannels(const uint8_t* fetch, uint32_t slots) {
  uint64_t key=0;unsigned unit=0;
  for(unsigned slot=0;slot<32&&unit<16;++slot)if(slots&(1u<<slot)) {
    const auto* f=fetch+24*slot;auto t=xenos::DecodeFetch(f);uint32_t signs=(xenos::BE32(f)>>2)&255;
    for(unsigned c=0;c<4;++c){unsigned sel=(t.swizzle>>(3*c))&7;if(sel<4&&((signs>>(2*sel))&3)==3)key|=1ull<<(unit*4+c);}
    ++unit;
  }
  return key;
}
inline std::string TextureGammaVariant(std::string source,uint32_t slots,uint64_t channels) {
  if(!channels)return source;
  // Xenos piecewise gamma, including its ten-bit quantization correction.
  // All intermediates stay below 2048, suitable for fragment mediump.
  const char* helper="vec4 nx_pwl_gamma(vec4 g){g=clamp(g,0.0,1.0);vec4 a=step(vec4(64.0/255.0),g),b=step(vec4(96.0/255.0),g),c=step(vec4(192.0/255.0),g);vec4 p=vec4(1.0)+a+2.0*b+4.0*c;vec4 v=g*(255.0*p)-64.0*a-192.0*b-768.0*c;return (v+floor(v*p*(1.0/1024.0)))*(1.0/1023.0);}\n";
  auto at=source.find("vec4 nx_sample");if(at==std::string::npos)return source;source.insert(at,helper);
  unsigned unit=0;
  for(unsigned slot=0;slot<32&&unit<16;++slot)if(slots&(1u<<slot)) {
    unsigned mask=(channels>>(4*unit++))&15;if(!mask)continue;
    std::string marker="vec4 v=texture2D(T"+std::to_string(slot)+",uv);";
    auto pos=source.find(marker);if(pos==std::string::npos)continue;
    std::string swizzle;for(unsigned c=0;c<4;++c)if(mask&(1u<<c))swizzle+="rgba"[c];
    source.insert(pos+marker.size(),"vec4 linear=nx_pwl_gamma(v);v."+swizzle+"=linear."+swizzle+";");
  }
  return source;
}
}
