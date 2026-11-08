#include "../render/texture_hdr8.h"
#include <cstdio>
using namespace nier::render;
std::vector<uint8_t> raw;
const uint8_t* Read(uint32_t a,uint32_t n){return uint64_t(a)+n<=raw.size()?raw.data()+a:nullptr;}
int main(int argc,char**argv){
 if(argc!=2)return 1;FILE*f=std::fopen(argv[1],"rb");if(!f)return 2;raw.resize(256*256*8);if(std::fread(raw.data(),1,raw.size(),f)!=raw.size())return 3;std::fclose(f);
 nier::xenos::TextureFetch t;t.width=t.height=t.pitch=256;t.dimension=1;t.format=29;t.endian=1;t.tiled=true;t.swizzle=0x53;
 std::vector<float> values;if(!ReadHdrHalf(Read,t,values))return 4;Hdr8Image p;if(!PackHdr8(256,256,values,p))return 5;
 double maxAbs=0,sumAbs=0,sumRef=0,oldAbs=0,filterAbs=0,filterRef=0,maxFilterNorm=0;std::vector<double> relative;
 for(size_t i=0;i<values.size();++i){unsigned c=i&3;double u=p.levels[0].pixels[i]/255.0,value=u*u*p.gain[c]+p.bias[c],e=std::fabs(value-values[i]);maxAbs=std::max(maxAbs,e);
  if(e>p.gain[c]*(1.0/255+1.0/(4*255*255))+0.0001)return 6;
  if(c<3){sumAbs+=e;sumRef+=std::fabs(values[i]);oldAbs+=std::fabs(std::round(std::clamp(double(values[i]),0.0,1.0)*255)/255-values[i]);if(values[i]>=0.01)relative.push_back(e/values[i]);}
 }
 for(unsigned y=0;y<256;++y)for(unsigned x=0;x<255;++x)for(unsigned c=0;c<3;++c){size_t a=(size_t(y)*256+x)*4+c,b=a+4;double u=(p.levels[0].pixels[a]+p.levels[0].pixels[b])/510.0,expected=(values[a]+values[b])*0.5,value=u*u*p.gain[c]+p.bias[c];double e=std::fabs(value-expected);filterAbs+=e;filterRef+=std::fabs(expected);maxFilterNorm=std::max(maxFilterNorm,p.gain[c]?e/p.gain[c]:0.0);}
 std::sort(relative.begin(),relative.end());double p95=relative.empty()?0:relative[size_t((relative.size()-1)*0.95)];
 std::printf("PASS HDR8 sqrt: 262144 half components; %zu mips; max point abs %.9g; point weighted-relative %.5f%% (legacy clamp %.5f%%); p95-relative(x>=0.01) %.5f%%; midpoint weighted-relative %.5f%%, max abs/range %.5f%%\n",p.levels.size(),maxAbs,100*sumAbs/sumRef,100*oldAbs/sumRef,100*p95,100*filterAbs/filterRef,100*maxFilterNorm);
 std::printf("scale %g %g %g %g bias %g %g %g %g; native encoded filtering is intentionally lossy\n",p.gain[0],p.gain[1],p.gain[2],p.gain[3],p.bias[0],p.bias[1],p.bias[2],p.bias[3]);
 std::vector<float> ramp(4*4*4);for(unsigned i=0;i<16;++i){ramp[i*4]=i;ramp[i*4+1]=2*i;ramp[i*4+2]=100+i;ramp[i*4+3]=1;}
 Hdr8Image mip;if(!PackHdr8(4,4,ramp,mip)||mip.levels.size()!=3)return 7;
 const double expected[]={2.5,4.5,10.5,12.5};
 for(unsigned i=0;i<4;++i)for(unsigned c=0;c<4;++c){double ref=c==0?expected[i]:c==1?2*expected[i]:c==2?100+expected[i]:1;double u=mip.levels[1].pixels[i*4+c]/255.0;double v=u*u*mip.gain[c]+mip.bias[c];if(std::fabs(v-ref)>mip.gain[c]/255.0+0.0001)return 8;}
 for(unsigned c=0;c<4;++c){double ref=c==0?7.5:c==1?15:c==2?107.5:1;double u=mip.levels[2].pixels[c]/255.0;if(std::fabs(u*u*mip.gain[c]+mip.bias[c]-ref)>mip.gain[c]/255.0+0.0001)return 9;}
 std::puts("PASS mip averages computed in original linear values, nonzero bias and constant alpha");

}
