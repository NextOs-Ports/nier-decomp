#pragma once
#include "xenos_texture.h"
#include <array>
#include <cmath>
#include <limits>
#include <string>
namespace nier::render {
struct Hdr8Level { unsigned width=0,height=0; std::vector<uint8_t> pixels; };
struct Hdr8Image {
  std::array<float,4> gain{},bias{};
  std::vector<Hdr8Level> levels;
};
// One RGBA8 plane, sqrt encoding and per-component range/bias. Mips are
// averaged in original floating point before encoding. Native filtering in
// encoded space is intentionally approximate (documented quality tradeoff).
inline bool PackHdr8(unsigned width,unsigned height,std::vector<float> pixels,Hdr8Image& out,bool mips=true) {
  if(!width||!height||pixels.size()!=size_t(width)*height*4)return false;
  std::array<float,4> lo,hi;lo.fill(std::numeric_limits<float>::infinity());hi.fill(-std::numeric_limits<float>::infinity());
  for(size_t i=0;i<pixels.size();++i){if(!std::isfinite(pixels[i]))return false;unsigned c=i&3;lo[c]=std::min(lo[c],pixels[i]);hi[c]=std::max(hi[c],pixels[i]);}
  for(unsigned c=0;c<4;++c){out.gain[c]=hi[c]-lo[c];out.bias[c]=lo[c];if(out.gain[c]>131008||std::fabs(out.bias[c])>65504)return false;}
  out.levels.clear();
  for(;;){
    Hdr8Level level;level.width=width;level.height=height;level.pixels.resize(pixels.size());
    for(size_t i=0;i<pixels.size();++i){unsigned c=i&3;float u=out.gain[c]?(pixels[i]-out.bias[c])/out.gain[c]:0;level.pixels[i]=uint8_t(std::lround(std::sqrt(std::clamp(u,0.f,1.f))*255.f));}
    out.levels.push_back(std::move(level));if(!mips||(width==1&&height==1))break;
    unsigned nw=std::max(1u,width/2),nh=std::max(1u,height/2);std::vector<float> next(size_t(nw)*nh*4);
    for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x)for(unsigned c=0;c<4;++c){float sum=0;for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i)sum+=pixels[(size_t(std::min(y*2+j,height-1))*width+std::min(x*2+i,width-1))*4+c];next[(size_t(y)*nw+x)*4+c]=sum*0.25f;}
    pixels.swap(next);width=nw;height=nh;
  }
  return true;
}
inline bool ReadHdrHalf(xenos::Reader read,const xenos::TextureFetch& t,std::vector<float>& pixels){
  if(t.format!=xenos::k16161616Expand||t.dimension!=1)return false;
  unsigned ox,oy;xenos::PackedBaseOffset(t,ox,oy);unsigned pitch=std::max(t.pitch,t.width),aw=(pitch+31)&~31u,ah=(t.height+31)&~31u,span=aw*ah*8;
  const uint8_t* src=read(t.base,span);if(!src)return false;pixels.resize(size_t(t.width)*t.height*4);
  for(unsigned y=0;y<t.height;++y)for(unsigned x=0;x<t.width;++x){unsigned off=t.tiled?unsigned(xenos::TiledInner(x+ox,y+oy,3,xenos::TiledOuter(y+oy,aw,3))):((y+oy)*pitch+x+ox)*8;if(off+8>span)return false;
    uint8_t e[8];std::memcpy(e,src+off,8);xenos::Swap(e,8,t.endian);float value[6];for(unsigned c=0;c<4;++c)value[c]=xenos::HalfToFloat(uint16_t(e[c*2]|e[c*2+1]<<8));value[4]=0;value[5]=1;
    for(unsigned c=0;c<4;++c){unsigned sel=(t.swizzle>>(3*c))&7;pixels[(size_t(y)*t.width+x)*4+c]=sel<6?value[sel]:0;}
  }
  return true;
}
inline uint32_t HdrTextureChannels(const uint8_t* fetch,uint32_t slots){uint32_t mask=0;unsigned unit=0;for(unsigned slot=0;slot<32&&unit<16;++slot)if(slots&(1u<<slot)){if((xenos::BE32(fetch+24*slot+4)&63)==xenos::k16161616Expand)mask|=1u<<unit;++unit;}return mask;}
inline std::string HdrTextureVariant(std::string source,uint32_t slots,uint32_t channels){
 unsigned unit=0;
 for(unsigned slot=0;slot<32&&unit<16;++slot)if(slots&(1u<<slot)){
  if(!(channels&(1u<<unit++)))continue;std::string n=std::to_string(slot),marker="vec4 v=texture2D(T"+n+",uv);";auto at=source.find(marker);if(at==std::string::npos)continue;
  source.insert(at+marker.size(),"v=v*nx_hdrScale"+n+";v=clamp(v*v+nx_hdrBias"+n+",vec4(-16376.0),vec4(16376.0))*4.0;");
  auto decl=source.find("vec4 nx_sample"+n+"(");source.insert(decl,"uniform mediump vec4 nx_hdrScale"+n+",nx_hdrBias"+n+";\n");
 }
 return source;
}
}
