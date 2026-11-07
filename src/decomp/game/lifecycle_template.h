// Ciclo de vida de objetos C++ do NieR (construtores e destrutores gerados pelo
// compilador), reconstruído como templates e instanciado por tipo.
#pragma once
#include <nier/decomp.h>

namespace nier::lifecycle {
using namespace nier::guest;

// Destrutor "com delete": restaura a vtable da classe; se flags & 1, libera o objeto
// com operator delete(this, size). Devolve this.
inline void dtor_vtable_delete(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vtable, uint32_t size,
                               uint32_t op_delete) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  st32(base, self, vtable);
  if (flags & 1) {
    GuestFrame f(ctx, base, frame);
    ctx.r3.u64 = self;
    ctx.r4.u64 = size;
    call_guest(ctx, base, op_delete);
  }
  ctx.r3.u64 = self;
}

// Construtor: chama o construtor da base com (this, arg), zera um campo e grava a vtable.
inline void ctor_base_field_vtable(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t base_ctor, uint32_t arg,
                                   uint32_t field, uint32_t vtable) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    ctx.r4.u64 = arg;
    call_guest(ctx, base, base_ctor);
  }
  st32(base, self + field, 0);
  st32(base, self, vtable);
  ctx.r3.u64 = self;
}

// Destrutor "com delete" pelo alocador global: chama o destrutor do corpo; se flags & 1,
// pede o alocador (objeto estático) e chama o método de liberação dele com o objeto.
inline void dtor_allocator_delete(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t dtor, uint32_t get_allocator,
                                  uint32_t free_slot, uint32_t tmp_off = 80, uint32_t holder_off = 4) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  GuestFrame f(ctx, base, frame);
  call_guest(ctx, base, dtor);
  if (flags & 1) {
    ctx.r3.u64 = f.slot(tmp_off);
    call_guest(ctx, base, get_allocator);
    const uint32_t holder = ld32(base, f.slot(tmp_off));
    ctx.r4.u64 = self;
    const uint32_t allocator = ld32(base, holder + holder_off);
    ctx.r3.u64 = allocator;
    call_virtual(ctx, base, allocator, free_slot);
  }
  ctx.r3.u64 = self;
}
}  // namespace nier::lifecycle
using namespace nier::lifecycle;
