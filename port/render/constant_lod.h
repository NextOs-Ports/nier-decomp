#pragma once
#include <regex>
#include <string>
#include <cstdint>
namespace nier::render {
struct ConstantLod { int index=-1, component=0; uint32_t samplers=0; };
// Conservative specialization: only a register LOD sourced from an unchanged
// input varying and a VS export that copies one uniform component. Other LOD
// expressions remain outside this specialization; never guess from shader ID.
inline ConstantLod ParseConstantLod(const std::string& vs,const std::string& ps) {
  ConstantLod out;std::regex set(R"(// NIER_SET_LOD r\[(\d+)\]\.([xyzw]))");
  std::string reg,component;size_t first=std::string::npos;
  for(auto i=std::sregex_iterator(ps.begin(),ps.end(),set);i!=std::sregex_iterator();++i){
    if(reg.empty()){reg=(*i)[1];component=(*i)[2];first=i->position();}
    else return out;  // multiple register-LOD writes require per-fetch views
  }
  if(reg.empty()||ps.find("if (p)")!=std::string::npos||ps.find("if (!p)")!=std::string::npos)return out;
  std::regex init("r\\["+reg+"\\] = v(\\d+);");std::smatch m;
  std::string prefix=ps.substr(0,first);if(!std::regex_search(prefix,m,init))return out;std::string varying=m[1];
  std::regex write("r\\["+reg+"\\]\\.([xyzw]+) =");
  for(auto i=std::sregex_iterator(prefix.begin(),prefix.end(),write);i!=std::sregex_iterator();++i)if((*i)[1].str().find(component)!=std::string::npos)return out;
  std::regex sample(R"(// NIER_LOD_SAMPLE (\d+))");uint32_t slots=0;
  for(auto i=std::sregex_iterator(ps.begin(),ps.end(),sample);i!=std::sregex_iterator();++i){unsigned s=std::stoul((*i)[1]);if(s<32)slots|=1u<<s;}
  if(!slots)return out;
  for(unsigned slot=0;slot<32;++slot)if(slots&(1u<<slot)){
    std::string marker="// NIER_LOD_SAMPLE "+std::to_string(slot)+"\n",call="nx_sample"+std::to_string(slot)+"(";
    size_t marked=0,calls=0;for(size_t at=0;(at=ps.find(marker,at))!=std::string::npos;at+=marker.size()){if(at<first)return out;++marked;}
    for(size_t at=0;(at=ps.find(call,at))!=std::string::npos;at+=call.size())++calls;
    if(calls!=marked+1)return out; // helper definition plus exclusively explicit-LOD fetches
  }
  std::regex uniform("vec4 va = VC\\[(\\d+)\\]\\.([xyzw]{4});\\s*vec4 vb = VC\\[\\1\\]\\.\\2;\\s*vec4 vr = max\\(va, vb\\);\\s*v"+varying+"\\.([xyzw]+) = vr\\.\\3;");
  for(auto i=std::sregex_iterator(vs.begin(),vs.end(),uniform);i!=std::sregex_iterator();++i){
    if((*i)[3].str().find(component)!=std::string::npos){out.index=std::stoi((*i)[1]);out.component=int(std::string("xyzw").find((*i)[2].str()[std::string("xyzw").find(component)]));out.samplers=slots;return out;}
  }
  return out;
}
}
