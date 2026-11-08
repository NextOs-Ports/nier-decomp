// Dependência externa de BeginVertices, SOMENTE no executável de verificação.
// Arena reservada pela sua NIER_VERIFY_SPEC. Ambos os lados chamam esta mesma
// função; os três argumentos completos ficam registrados na memória comparada.
#include <nier/decomp.h>
using namespace nier::guest;
extern "C" void __wrap_sub_82573670(PPCContext& ctx,uint8_t* base){
    st64(base,0x40070020,ctx.r3.u64);
    st64(base,0x40070028,ctx.r4.u64);
    st64(base,0x40070030,ctx.r5.u64);
    ctx.r3.u64=ld32(base,0x40070010)?0:
        (ld32(base,0x40070014)?0xFFFFFFFFE0060000ull:0x40060000ull);
}
