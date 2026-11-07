// Diagnostic interposer: capture exactly the PCM submitted by the SDK to SDL.
// The optional precision policy touches only C1's known generated VS prelude.
#include <SDL3/SDL_audio.h>
#include <GLES2/gl2.h>
#include <openssl/sha.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Wave { FILE* file=nullptr; uint32_t bytes=0; int channels=0,rate=0; };
std::mutex audio_mutex,shader_mutex;
std::unordered_map<SDL_AudioStream*,Wave> waves;
void u16(FILE* f,uint16_t x){std::fwrite(&x,2,1,f);}
void u32(FILE* f,uint32_t x){std::fwrite(&x,4,1,f);}
void header(Wave& w) {
    std::rewind(w.file);std::fwrite("RIFF",1,4,w.file);u32(w.file,36+w.bytes);
    std::fwrite("WAVEfmt ",1,8,w.file);u32(w.file,16);u16(w.file,3);
    u16(w.file,w.channels);u32(w.file,w.rate);u32(w.file,w.rate*w.channels*4);
    u16(w.file,w.channels*4);u16(w.file,32);std::fwrite("data",1,4,w.file);u32(w.file,w.bytes);
}
std::string digest(const std::string& source) {
    unsigned char bytes[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const unsigned char*>(source.data()),source.size(),bytes);
    char out[65];for(unsigned i=0;i<32;++i)std::sprintf(out+i*2,"%02x",bytes[i]);return out;
}
}
extern "C" bool SDL_PutAudioStreamData(SDL_AudioStream* stream,const void* data,int len) {
    using Put=bool(*)(SDL_AudioStream*,const void*,int);
    using Format=bool(*)(SDL_AudioStream*,SDL_AudioSpec*,SDL_AudioSpec*);
    static auto real=reinterpret_cast<Put>(dlsym(RTLD_NEXT,"SDL_PutAudioStreamData"));
    static auto format=reinterpret_cast<Format>(dlsym(RTLD_NEXT,"SDL_GetAudioStreamFormat"));
    if (!real) std::abort();
    const bool result=real(stream,data,len);
    const char* folder=std::getenv("NIER_PCM_DIR");
    if (!result || !folder || len<=0 || !format) return result;
    SDL_AudioSpec input{},output{};
    if (!format(stream,&input,&output) || input.format!=SDL_AUDIO_F32LE) return result;
    std::lock_guard lock(audio_mutex);
    auto& w=waves[stream];
    if (!w.file) {
        std::filesystem::create_directories(folder);
        char name[96];std::snprintf(name,sizeof name,"/runtime_%zu_%dhz_%dch.wav",waves.size(),input.freq,input.channels);
        w.file=std::fopen((std::string(folder)+name).c_str(),"wb+");
        if (!w.file) return result;
        w.channels=input.channels;w.rate=input.freq;header(w);
        std::fprintf(stderr,"NIER_PCM capture=%s%s IEEEfloat32 rate=%d channels=%d\n",folder,name,w.rate,w.channels);
    }
    if (input.freq!=w.rate || input.channels!=w.channels || w.bytes+uint32_t(len)>uint64_t(w.rate)*w.channels*4*300) return result;
    std::fseek(w.file,0,SEEK_END);
    const auto written=std::fwrite(data,1,len,w.file);
    w.bytes+=uint32_t(written);header(w);std::fflush(w.file);
    return result;
}
extern "C" void glShaderSource(GLuint shader,GLsizei count,const GLchar* const* strings,const GLint* lengths) {
    using Source=void(*)(GLuint,GLsizei,const GLchar* const*,const GLint*);
    static auto real=reinterpret_cast<Source>(dlsym(RTLD_NEXT,"glShaderSource"));
    if (!real) std::abort();
    if (!std::getenv("NIER_PCM_MEDIUMP_PROBE")) {real(shader,count,strings,lengths);return;}
    std::string text;
    for(int i=0;i<count;++i) text.append(strings[i],lengths&&lengths[i]>=0?size_t(lengths[i]):std::strlen(strings[i]));
    const std::string original=text;
    const std::string prelude="#version 100\nprecision highp float;\nprecision highp int;\n";
    if(text.starts_with(prelude)) text.replace(0,prelude.size(),"#version 100\nprecision mediump float;\nprecision mediump int;\n");
    if(text.starts_with("attribute vec2 p; varying vec2 uv; void main()"))
        text="precision mediump float;\nprecision mediump int;\n"+text;
    if(text.starts_with("precision mediump float; varying vec2 uv; uniform sampler2D t;"))
        text="precision mediump int;\nprecision mediump sampler2D;\n"+text;
    if(text.find("#version 300")!=std::string::npos) std::abort();
    if(text.find("highp")!=std::string::npos) {std::fprintf(stderr,"NIER_PCM rejected unknown highp shader\n");std::abort();}
    if(const char* folder=std::getenv("NIER_PCM_DIR")) {
        std::lock_guard lock(shader_mutex);
        std::filesystem::create_directories(std::string(folder)+"/shader-policy");
        const auto name=std::string(folder)+"/shader-policy/"+digest(original);
        if(!std::filesystem::exists(name+".input.glsl")) {
            FILE* f=std::fopen((name+".input.glsl").c_str(),"wb");if(f){std::fwrite(original.data(),1,original.size(),f);std::fclose(f);}
            f=std::fopen((name+"."+digest(text)+".output.glsl").c_str(),"wb");if(f){std::fwrite(text.data(),1,text.size(),f);std::fclose(f);}
        }
    }
    const GLchar* source=text.c_str();const GLint size=GLint(text.size());real(shader,1,&source,&size);
}
