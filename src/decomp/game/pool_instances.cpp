// GERADO por tools/decomp/gen_pool_instances.py — não editar à mão.
// Instâncias do template de pool (src/decomp/game/pools.cpp), localizadas pela
// estrutura das instruções e provadas uma a uma pelo tools/verify.
#include "pools_template.h"

// sub_82103970 (176 bytes) — release
NIER_DECOMP(sub_82103970) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {152, 156, 160}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82103970, 0x82103970, "r3=ptr r4=ptr @r4.0=ptr @r4.0.160=range:0..0xFFFFFFFF @r4.0.152=ptr @r4.0.156=ptr @r3.20=ptr");

// sub_821116D0 (128 bytes) — take
NIER_DECOMP(sub_821116D0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {48, 52, 56}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_821116D0, 0x821116D0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82111750 (176 bytes) — release
NIER_DECOMP(sub_82111750) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {48, 52, 56}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82111750, 0x82111750, "r3=ptr r4=ptr @r4.0=ptr @r4.0.56=range:0..0xFFFFFFFF @r4.0.48=ptr @r4.0.52=ptr @r3.20=ptr");

// sub_82114FD0 (128 bytes) — take
NIER_DECOMP(sub_82114FD0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {152, 156, 160}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82114FD0, 0x82114FD0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82117220 (176 bytes) — release
NIER_DECOMP(sub_82117220) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {340, 344, 348}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82117220, 0x82117220, "r3=ptr r4=ptr @r4.0=ptr @r4.0.348=range:0..0xFFFFFFFF @r4.0.340=ptr @r4.0.344=ptr @r3.20=ptr");

// sub_8211DBF0 (176 bytes) — release
NIER_DECOMP(sub_8211DBF0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {268, 272, 276}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8211DBF0, 0x8211DBF0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.276=range:0..0xFFFFFFFF @r4.0.268=ptr @r4.0.272=ptr @r3.20=ptr");

// sub_8211F5C0 (176 bytes) — release
NIER_DECOMP(sub_8211F5C0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {392, 396, 400}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8211F5C0, 0x8211F5C0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.400=range:0..0xFFFFFFFF @r4.0.392=ptr @r4.0.396=ptr @r3.20=ptr");

// sub_8213B558 (128 bytes) — take
NIER_DECOMP(sub_8213B558) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {4, 8, 12}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8213B558, 0x8213B558, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_821478E0 (176 bytes) — release
NIER_DECOMP(sub_821478E0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {112, 116, 120}, {2048, 2052, 2060, 2064}); }
NIER_VERIFY_SPEC(sub_821478E0, 0x821478E0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.120=range:0..0xFFFFFFFF @r4.0.112=ptr @r4.0.116=ptr @r3.2060=ptr");

// sub_82147A88 (176 bytes) — release
NIER_DECOMP(sub_82147A88) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {96, 100, 104}, {896, 900, 908, 912}); }
NIER_VERIFY_SPEC(sub_82147A88, 0x82147A88, "r3=ptr r4=ptr @r4.0=ptr @r4.0.104=range:0..0xFFFFFFFF @r4.0.96=ptr @r4.0.100=ptr @r3.908=ptr");

// sub_8214B568 (176 bytes) — release
NIER_DECOMP(sub_8214B568) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {4288, 4292, 4296}, {4304, 4308, 4316, 4320}); }
NIER_VERIFY_SPEC(sub_8214B568, 0x8214B568, "r3=ptr r4=ptr @r4.0=ptr @r4.0.4296=range:0..0xFFFFFFFF @r4.0.4288=ptr @r4.0.4292=ptr @r3.4316=ptr");

// sub_8214C6D8 (128 bytes) — take
NIER_DECOMP(sub_8214C6D8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {1760, 1764, 1768}, {7104, 7108, 7116, 7120}); }
NIER_VERIFY_SPEC(sub_8214C6D8, 0x8214C6D8, "r3=ptr @r3.7116=ptr @r3.7108=ptr");

// sub_8214C758 (176 bytes) — release
NIER_DECOMP(sub_8214C758) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {1760, 1764, 1768}, {7104, 7108, 7116, 7120}); }
NIER_VERIFY_SPEC(sub_8214C758, 0x8214C758, "r3=ptr r4=ptr @r4.0=ptr @r4.0.1768=range:0..0xFFFFFFFF @r4.0.1760=ptr @r4.0.1764=ptr @r3.7116=ptr");

// sub_8214D8E0 (128 bytes) — take
NIER_DECOMP(sub_8214D8E0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {1760, 1764, 1768}, {28416, 28420, 28428, 28432}); }
NIER_VERIFY_SPEC(sub_8214D8E0, 0x8214D8E0, "r3=ptr @r3.28428=ptr @r3.28420=ptr");

// sub_8214D960 (176 bytes) — release
NIER_DECOMP(sub_8214D960) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {1760, 1764, 1768}, {28416, 28420, 28428, 28432}); }
NIER_VERIFY_SPEC(sub_8214D960, 0x8214D960, "r3=ptr r4=ptr @r4.0=ptr @r4.0.1768=range:0..0xFFFFFFFF @r4.0.1760=ptr @r4.0.1764=ptr @r3.28428=ptr");

// sub_8214ECA8 (128 bytes) — take
NIER_DECOMP(sub_8214ECA8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {704, 708, 712}, {720, 724, 732, 736}); }
NIER_VERIFY_SPEC(sub_8214ECA8, 0x8214ECA8, "r3=ptr @r3.732=ptr @r3.724=ptr");

// sub_8214ED28 (176 bytes) — release
NIER_DECOMP(sub_8214ED28) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {704, 708, 712}, {720, 724, 732, 736}); }
NIER_VERIFY_SPEC(sub_8214ED28, 0x8214ED28, "r3=ptr r4=ptr @r4.0=ptr @r4.0.712=range:0..0xFFFFFFFF @r4.0.704=ptr @r4.0.708=ptr @r3.732=ptr");

// sub_8214FE68 (128 bytes) — take
NIER_DECOMP(sub_8214FE68) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {1216, 1220, 1224}, {1232, 1236, 1244, 1248}); }
NIER_VERIFY_SPEC(sub_8214FE68, 0x8214FE68, "r3=ptr @r3.1244=ptr @r3.1236=ptr");

// sub_8214FEE8 (176 bytes) — release
NIER_DECOMP(sub_8214FEE8) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {1216, 1220, 1224}, {1232, 1236, 1244, 1248}); }
NIER_VERIFY_SPEC(sub_8214FEE8, 0x8214FEE8, "r3=ptr r4=ptr @r4.0=ptr @r4.0.1224=range:0..0xFFFFFFFF @r4.0.1216=ptr @r4.0.1220=ptr @r3.1244=ptr");

// sub_82151028 (128 bytes) — take
NIER_DECOMP(sub_82151028) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {2240, 2244, 2248}, {2256, 2260, 2268, 2272}); }
NIER_VERIFY_SPEC(sub_82151028, 0x82151028, "r3=ptr @r3.2268=ptr @r3.2260=ptr");

// sub_821510A8 (176 bytes) — release
NIER_DECOMP(sub_821510A8) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {2240, 2244, 2248}, {2256, 2260, 2268, 2272}); }
NIER_VERIFY_SPEC(sub_821510A8, 0x821510A8, "r3=ptr r4=ptr @r4.0=ptr @r4.0.2248=range:0..0xFFFFFFFF @r4.0.2240=ptr @r4.0.2244=ptr @r3.2268=ptr");

// sub_82152608 (128 bytes) — take
NIER_DECOMP(sub_82152608) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {144, 148, 152}, {1440, 1444, 1452, 1456}); }
NIER_VERIFY_SPEC(sub_82152608, 0x82152608, "r3=ptr @r3.1452=ptr @r3.1444=ptr");

// sub_82152688 (176 bytes) — release
NIER_DECOMP(sub_82152688) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {144, 148, 152}, {1440, 1444, 1452, 1456}); }
NIER_VERIFY_SPEC(sub_82152688, 0x82152688, "r3=ptr r4=ptr @r4.0=ptr @r4.0.152=range:0..0xFFFFFFFF @r4.0.144=ptr @r4.0.148=ptr @r3.1452=ptr");

// sub_82153950 (176 bytes) — release
NIER_DECOMP(sub_82153950) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {144, 148, 152}, {5120, 5124, 5132, 5136}); }
NIER_VERIFY_SPEC(sub_82153950, 0x82153950, "r3=ptr r4=ptr @r4.0=ptr @r4.0.152=range:0..0xFFFFFFFF @r4.0.144=ptr @r4.0.148=ptr @r3.5132=ptr");

// sub_82154AF0 (176 bytes) — release
NIER_DECOMP(sub_82154AF0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {144, 148, 152}, {20480, 20484, 20492, 20496}); }
NIER_VERIFY_SPEC(sub_82154AF0, 0x82154AF0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.152=range:0..0xFFFFFFFF @r4.0.144=ptr @r4.0.148=ptr @r3.20492=ptr");

// sub_82156FA0 (176 bytes) — release
NIER_DECOMP(sub_82156FA0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {176, 180, 184}, {192, 196, 204, 208}); }
NIER_VERIFY_SPEC(sub_82156FA0, 0x82156FA0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.184=range:0..0xFFFFFFFF @r4.0.176=ptr @r4.0.180=ptr @r3.204=ptr");

// sub_82158688 (176 bytes) — release
NIER_DECOMP(sub_82158688) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {176, 180, 184}, {768, 772, 780, 784}); }
NIER_VERIFY_SPEC(sub_82158688, 0x82158688, "r3=ptr r4=ptr @r4.0=ptr @r4.0.184=range:0..0xFFFFFFFF @r4.0.176=ptr @r4.0.180=ptr @r3.780=ptr");

// sub_82159800 (128 bytes) — take
NIER_DECOMP(sub_82159800) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {176, 180, 184}, {3072, 3076, 3084, 3088}); }
NIER_VERIFY_SPEC(sub_82159800, 0x82159800, "r3=ptr @r3.3084=ptr @r3.3076=ptr");

// sub_82159880 (176 bytes) — release
NIER_DECOMP(sub_82159880) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {176, 180, 184}, {3072, 3076, 3084, 3088}); }
NIER_VERIFY_SPEC(sub_82159880, 0x82159880, "r3=ptr r4=ptr @r4.0=ptr @r4.0.184=range:0..0xFFFFFFFF @r4.0.176=ptr @r4.0.180=ptr @r3.3084=ptr");

// sub_8215A970 (128 bytes) — take
NIER_DECOMP(sub_8215A970) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {176, 180, 184}, {12288, 12292, 12300, 12304}); }
NIER_VERIFY_SPEC(sub_8215A970, 0x8215A970, "r3=ptr @r3.12300=ptr @r3.12292=ptr");

// sub_8215A9F0 (176 bytes) — release
NIER_DECOMP(sub_8215A9F0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {176, 180, 184}, {12288, 12292, 12300, 12304}); }
NIER_VERIFY_SPEC(sub_8215A9F0, 0x8215A9F0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.184=range:0..0xFFFFFFFF @r4.0.176=ptr @r4.0.180=ptr @r3.12300=ptr");

// sub_82169110 (128 bytes) — take
NIER_DECOMP(sub_82169110) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {6368, 6372, 6376}, {25536, 25540, 25548, 25552}); }
NIER_VERIFY_SPEC(sub_82169110, 0x82169110, "r3=ptr @r3.25548=ptr @r3.25540=ptr");

// sub_82169190 (176 bytes) — release
NIER_DECOMP(sub_82169190) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {6368, 6372, 6376}, {25536, 25540, 25548, 25552}); }
NIER_VERIFY_SPEC(sub_82169190, 0x82169190, "r3=ptr r4=ptr @r4.0=ptr @r4.0.6376=range:0..0xFFFFFFFF @r4.0.6368=ptr @r4.0.6372=ptr @r3.25548=ptr");

// sub_8216A308 (128 bytes) — take
NIER_DECOMP(sub_8216A308) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {3296, 3300, 3304}, {26496, 26500, 26508, 26512}); }
NIER_VERIFY_SPEC(sub_8216A308, 0x8216A308, "r3=ptr @r3.26508=ptr @r3.26500=ptr");

// sub_8216A388 (176 bytes) — release
NIER_DECOMP(sub_8216A388) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {3296, 3300, 3304}, {26496, 26500, 26508, 26512}); }
NIER_VERIFY_SPEC(sub_8216A388, 0x8216A388, "r3=ptr r4=ptr @r4.0=ptr @r4.0.3304=range:0..0xFFFFFFFF @r4.0.3296=ptr @r4.0.3300=ptr @r3.26508=ptr");

// sub_821AF1C0 (128 bytes) — take
NIER_DECOMP(sub_821AF1C0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {20, 24, 28}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_821AF1C0, 0x821AF1C0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82219220 (176 bytes) — release
NIER_DECOMP(sub_82219220) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {240, 244, 248}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82219220, 0x82219220, "r3=ptr r4=ptr @r4.0=ptr @r4.0.248=range:0..0xFFFFFFFF @r4.0.240=ptr @r4.0.244=ptr @r3.20=ptr");

// sub_822192D0 (128 bytes) — take
NIER_DECOMP(sub_822192D0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {112, 116, 120}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_822192D0, 0x822192D0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8225E808 (176 bytes) — release
NIER_DECOMP(sub_8225E808) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {40, 44, 48}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8225E808, 0x8225E808, "r3=ptr r4=ptr @r4.0=ptr @r4.0.48=range:0..0xFFFFFFFF @r4.0.40=ptr @r4.0.44=ptr @r3.20=ptr");

// sub_82268DF8 (176 bytes) — release
NIER_DECOMP(sub_82268DF8) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {36, 40, 44}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82268DF8, 0x82268DF8, "r3=ptr r4=ptr @r4.0=ptr @r4.0.44=range:0..0xFFFFFFFF @r4.0.36=ptr @r4.0.40=ptr @r3.20=ptr");

// sub_82320E98 (128 bytes) — take
NIER_DECOMP(sub_82320E98) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {160, 164, 168}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82320E98, 0x82320E98, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82320F18 (176 bytes) — release
NIER_DECOMP(sub_82320F18) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {160, 164, 168}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82320F18, 0x82320F18, "r3=ptr r4=ptr @r4.0=ptr @r4.0.168=range:0..0xFFFFFFFF @r4.0.160=ptr @r4.0.164=ptr @r3.20=ptr");

// sub_823276C8 (128 bytes) — take
NIER_DECOMP(sub_823276C8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {208, 212, 216}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823276C8, 0x823276C8, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8232B678 (128 bytes) — take
NIER_DECOMP(sub_8232B678) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {136, 140, 144}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8232B678, 0x8232B678, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8232B6F8 (128 bytes) — take
NIER_DECOMP(sub_8232B6F8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {64, 68, 72}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8232B6F8, 0x8232B6F8, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8232B778 (128 bytes) — take
NIER_DECOMP(sub_8232B778) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {8, 12, 16}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8232B778, 0x8232B778, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82358488 (176 bytes) — release
NIER_DECOMP(sub_82358488) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {84, 88, 92}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82358488, 0x82358488, "r3=ptr r4=ptr @r4.0=ptr @r4.0.92=range:0..0xFFFFFFFF @r4.0.84=ptr @r4.0.88=ptr @r3.20=ptr");

// sub_82359760 (128 bytes) — take
NIER_DECOMP(sub_82359760) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {656, 660, 664}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82359760, 0x82359760, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_823597E0 (176 bytes) — release
NIER_DECOMP(sub_823597E0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {656, 660, 664}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823597E0, 0x823597E0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.664=range:0..0xFFFFFFFF @r4.0.656=ptr @r4.0.660=ptr @r3.20=ptr");

// sub_8237ABD8 (128 bytes) — take
NIER_DECOMP(sub_8237ABD8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {272, 276, 280}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8237ABD8, 0x8237ABD8, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8237AC58 (176 bytes) — release
NIER_DECOMP(sub_8237AC58) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {272, 276, 280}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8237AC58, 0x8237AC58, "r3=ptr r4=ptr @r4.0=ptr @r4.0.280=range:0..0xFFFFFFFF @r4.0.272=ptr @r4.0.276=ptr @r3.20=ptr");

// sub_8237AD08 (128 bytes) — take
NIER_DECOMP(sub_8237AD08) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {60, 64, 68}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8237AD08, 0x8237AD08, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8237AD88 (176 bytes) — release
NIER_DECOMP(sub_8237AD88) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {60, 64, 68}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8237AD88, 0x8237AD88, "r3=ptr r4=ptr @r4.0=ptr @r4.0.68=range:0..0xFFFFFFFF @r4.0.60=ptr @r4.0.64=ptr @r3.20=ptr");

// sub_823800D8 (128 bytes) — take
NIER_DECOMP(sub_823800D8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {68, 72, 76}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823800D8, 0x823800D8, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82380158 (176 bytes) — release
NIER_DECOMP(sub_82380158) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {68, 72, 76}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82380158, 0x82380158, "r3=ptr r4=ptr @r4.0=ptr @r4.0.76=range:0..0xFFFFFFFF @r4.0.68=ptr @r4.0.72=ptr @r3.20=ptr");

// sub_8239CE40 (128 bytes) — take
NIER_DECOMP(sub_8239CE40) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {36, 40, 44}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8239CE40, 0x8239CE40, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_8239CEC0 (128 bytes) — take
NIER_DECOMP(sub_8239CEC0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {40, 44, 48}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8239CEC0, 0x8239CEC0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_823A37F8 (128 bytes) — take
NIER_DECOMP(sub_823A37F8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {12, 16, 20}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823A37F8, 0x823A37F8, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_823A3878 (128 bytes) — take
NIER_DECOMP(sub_823A3878) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {232, 236, 240}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823A3878, 0x823A3878, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_823A38F8 (176 bytes) — release
NIER_DECOMP(sub_823A38F8) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {232, 236, 240}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823A38F8, 0x823A38F8, "r3=ptr r4=ptr @r4.0=ptr @r4.0.240=range:0..0xFFFFFFFF @r4.0.232=ptr @r4.0.236=ptr @r3.20=ptr");

// sub_823A41D0 (176 bytes) — release
NIER_DECOMP(sub_823A41D0) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {4, 8, 12}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823A41D0, 0x823A41D0, "r3=ptr r4=ptr @r4.0=ptr @r4.0.12=range:0..0xFFFFFFFF @r4.0.4=ptr @r4.0.8=ptr @r3.20=ptr");

// sub_823A4B80 (128 bytes) — take
NIER_DECOMP(sub_823A4B80) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {96, 100, 104}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823A4B80, 0x823A4B80, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_823A4C00 (176 bytes) — release
NIER_DECOMP(sub_823A4C00) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {96, 100, 104}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823A4C00, 0x823A4C00, "r3=ptr r4=ptr @r4.0=ptr @r4.0.104=range:0..0xFFFFFFFF @r4.0.96=ptr @r4.0.100=ptr @r3.20=ptr");

// sub_823AF528 (128 bytes) — take
NIER_DECOMP(sub_823AF528) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {128, 132, 136}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823AF528, 0x823AF528, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_823AF5A8 (176 bytes) — release
NIER_DECOMP(sub_823AF5A8) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {128, 132, 136}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823AF5A8, 0x823AF5A8, "r3=ptr r4=ptr @r4.0=ptr @r4.0.136=range:0..0xFFFFFFFF @r4.0.128=ptr @r4.0.132=ptr @r3.20=ptr");

// sub_823E5D20 (176 bytes) — release
NIER_DECOMP(sub_823E5D20) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {8, 12, 16}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_823E5D20, 0x823E5D20, "r3=ptr r4=ptr @r4.0=ptr @r4.0.16=range:0..0xFFFFFFFF @r4.0.8=ptr @r4.0.12=ptr @r3.20=ptr");

// sub_824376D8 (128 bytes) — take
NIER_DECOMP(sub_824376D8) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {32, 36, 40}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_824376D8, 0x824376D8, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82437758 (176 bytes) — release
NIER_DECOMP(sub_82437758) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {32, 36, 40}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82437758, 0x82437758, "r3=ptr r4=ptr @r4.0=ptr @r4.0.40=range:0..0xFFFFFFFF @r4.0.32=ptr @r4.0.36=ptr @r3.20=ptr");

// sub_82443600 (176 bytes) — release
NIER_DECOMP(sub_82443600) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {112, 116, 120}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82443600, 0x82443600, "r3=ptr r4=ptr @r4.0=ptr @r4.0.120=range:0..0xFFFFFFFF @r4.0.112=ptr @r4.0.116=ptr @r3.20=ptr");

// sub_824436B0 (128 bytes) — take
NIER_DECOMP(sub_824436B0) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {168, 172, 176}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_824436B0, 0x824436B0, "r3=ptr @r3.20=ptr @r3.12=ptr");

// sub_82443730 (176 bytes) — release
NIER_DECOMP(sub_82443730) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {168, 172, 176}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82443730, 0x82443730, "r3=ptr r4=ptr @r4.0=ptr @r4.0.176=range:0..0xFFFFFFFF @r4.0.168=ptr @r4.0.172=ptr @r3.20=ptr");

// sub_82444A78 (176 bytes) — release
NIER_DECOMP(sub_82444A78) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {20, 24, 28}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82444A78, 0x82444A78, "r3=ptr r4=ptr @r4.0=ptr @r4.0.28=range:0..0xFFFFFFFF @r4.0.20=ptr @r4.0.24=ptr @r3.20=ptr");

// sub_82446588 (176 bytes) — release
NIER_DECOMP(sub_82446588) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {12, 16, 20}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82446588, 0x82446588, "r3=ptr r4=ptr @r4.0=ptr @r4.0.20=range:0..0xFFFFFFFF @r4.0.12=ptr @r4.0.16=ptr @r3.20=ptr");

// sub_82449D90 (176 bytes) — release
NIER_DECOMP(sub_82449D90) { pool_release(base, ctx.r3.u32, ctx.r4.u32, {28, 32, 36}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_82449D90, 0x82449D90, "r3=ptr r4=ptr @r4.0=ptr @r4.0.36=range:0..0xFFFFFFFF @r4.0.28=ptr @r4.0.32=ptr @r3.20=ptr");

// sub_8245F378 (128 bytes) — take
NIER_DECOMP(sub_8245F378) { ctx.r3.u64 = pool_take(base, ctx.r3.u32, {16, 20, 24}, {8, 12, 20, 24}); }
NIER_VERIFY_SPEC(sub_8245F378, 0x8245F378, "r3=ptr @r3.20=ptr @r3.12=ptr");
