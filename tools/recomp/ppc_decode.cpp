// SPDX-License-Identifier: GPL-3.0-only
// Small adapter to the external SDK disassembler used by the pointer audit.
#include "jumptables/disasm.h"
#include <cstring>
extern "C" const char* nier_decode_word(uint32_t word,uint32_t address,uint32_t* operands) {
    const uint8_t bytes[]{uint8_t(word>>24),uint8_t(word>>16),uint8_t(word>>8),uint8_t(word)};
    ppc_insn decoded{};
    if(!ppc::Disassemble(bytes,address,decoded) || !decoded.opcode)return nullptr;
    std::memcpy(operands,decoded.operands,sizeof(decoded.operands));
    return decoded.opcode->name;
}
