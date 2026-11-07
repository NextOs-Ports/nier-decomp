// Synthetic memory only. Known base-tail offsets from Xenos packed layout;
// contrasting poison at the old origin detects reading the wrong mip.
#include "../render/xenos_texture.h"
#include <cstdio>
#include <vector>
using namespace nier::xenos;
static std::vector<uint8_t> memory(131072);
static const uint8_t* Read(uint32_t a, uint32_t n) {
  return uint64_t(a) + n <= memory.size() ? memory.data() + a : nullptr;
}
int main() {
  struct Case { uint32_t w,h,format,pitch,tiledOffset,linearOffset; };
  const Case cases[] = {
    {128,4,kDXT1,128,1024,1024}, {4,128,kDXT1,32,256,32},
    {16,16,kDXT1,32,256,32}, {128,16,kDXT1,128,1024,1024},
    {1,1,k8888,32,128,64}, {8,4,k8888,32,2048,2048},
    {1,1,k8,32,128,16}, {8,4,k8,32,2048,512}, {128,4,k8,128,2048,2048},
  };
  unsigned checks=0;
  for (const auto& c:cases) for (bool tiled:{false,true}) for (bool packed:{false,true}) {
    std::fill(memory.begin(),memory.end(),0);
    TextureFetch t; t.width=c.w;t.height=c.h;t.format=c.format;t.pitch=c.pitch;
    t.tiled=tiled;t.packedMips=packed;t.dimension=1;
    uint32_t off=tiled?c.tiledOffset:c.linearOffset;
    if(c.format==kDXT1) {memory[0]=0x1F;memory[1]=0xF8;memory[off]=0xE0;memory[off+1]=0x07;}
    else if(c.format==k8888) {memory[0]=255;memory[2]=255;memory[3]=255;memory[off+1]=255;memory[off+3]=255;}
    else {memory[0]=17;memory[off]=93;}
    std::vector<uint8_t> out;
    if(!DecodeLevel0(Read,t,out)) return 1;
    bool ok=c.format==k8 ? out[0]==(packed?93:17) :
      out[0]==(packed?0:255)&&out[1]==(packed?255:0)&&out[2]==(packed?0:255);
    if(!ok){std::fprintf(stderr,"FAIL %ux%u f%u tiled%d packed%d: %u %u %u\n",c.w,c.h,c.format,tiled,packed,out[0],out[1],out[2]);return 2;}
    ++checks;
    if(c.format==k8) {
      std::vector<uint8_t> fast;
      if(!DecodeK8Luminance(Read,t,fast))return 3;
      for(size_t i=0;i<fast.size();++i)if(fast[i]!=out[4*i])return 4;
      if(!DecodeK8LumAlpha(Read,t,fast))return 5;
      for(size_t i=0;i<fast.size()/2;++i)if(fast[2*i]!=out[4*i]||fast[2*i+1]!=out[4*i])return 6;
      checks+=2;
    }
  }
  TextureFetch large; large.width=large.height=32;large.packedMips=true;
  uint32_t x,y;PackedBaseOffset(large,x,y);if(x||y)return 7;
  std::printf("PASS %u packed/unpacked decode checks and large-base guard\n",checks);
}
