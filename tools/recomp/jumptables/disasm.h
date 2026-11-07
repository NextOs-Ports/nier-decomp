// SPDX-License-Identifier: GPL-3.0-only
#pragma once
extern "C" {
#include <dis-asm.h>
}
namespace ppc {
inline int Disassemble(const void* code, uint64_t address, ppc_insn& out) {
    out = {};
    if (!code) return 0;
    disassemble_info info{};
    INIT_DISASSEMBLE_INFO(info, stdout, fprintf);
    info.arch=bfd_arch_powerpc;
    info.endian=BFD_ENDIAN_BIG;
    info.private_data=reinterpret_cast<void*>(uintptr_t(0x1|0x4|0x4000|0x8000000|0x200|0x1000000|0x10000));
    info.buffer=(bfd_byte*)code; info.buffer_vma=address; info.buffer_length=4;
    return decode_insn_ppc(address,&info,&out);
}
}
