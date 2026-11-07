// Ciclo de vida de objetos C++ do NieR (construtores e destrutores gerados pelo
// compilador), reconstruído como templates e instanciado por tipo.
#pragma once
#include <nier/decomp.h>

namespace nier::lifecycle {
using namespace nier::guest;

// Destrutor "com delete": restaura a vtable da classe; se flags & 1, libera o objeto
// com operator delete(this, size). Devolve this.
inline void dtor_vtable_delete(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vtable, uint32_t size,
                               uint32_t op_delete, uint32_t vt_off = 0) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  st32(base, self + vt_off, vtable);
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
                                   uint32_t field, uint32_t vtable, uint32_t vt_off = 0) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    ctx.r4.u64 = arg;
    call_guest(ctx, base, base_ctor);
  }
  st32(base, self + field, 0);
  st32(base, self + vt_off, vtable);
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

namespace nier::lifecycle {
// Encaminhador estilo "visitor": chama o método `slot` do objeto em r4 com (r4, r3).
inline void forward_swapped_virtual(PPCContext& ctx, uint8_t* base, uint32_t slot) {
  const uint64_t self = ctx.r3.u64;
  const uint32_t other = ctx.r4.u32;
  ctx.r3.u64 = ctx.r4.u64;
  ctx.r4.u64 = self;
  call_virtual(ctx, base, other, slot);
}

// Destrutor com delete sem tamanho: restaura a vtable; se flags & 1, delete(this).
inline void dtor_vtable_free(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vtable, uint32_t op_delete,
                             uint32_t vt_off = 0) {
  const uint32_t self = ctx.r3.u32;
  st32(base, self + vt_off, vtable);
  if (ctx.r4.u32 & 1) {
    GuestFrame f(ctx, base, frame);
    call_guest(ctx, base, op_delete);
  }
  ctx.r3.u64 = self;
}

// Destrutor com delete: corpo do destrutor e, se flags & 1, delete(this).
inline void dtor_body_free(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t dtor, uint32_t op_delete) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  GuestFrame f(ctx, base, frame);
  call_guest(ctx, base, dtor);
  if (flags & 1) {
    ctx.r3.u64 = self;
    call_guest(ctx, base, op_delete);
  }
  ctx.r3.u64 = self;
}

// Encaminhador com float: first(campo, this), depois second(this, f1 original).
inline void forward_member_then_self(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t member_off,
                                     uint32_t first, uint32_t second) {
  const uint64_t self = ctx.r3.u64;
  const uint64_t f1 = ctx.f1.u64;
  GuestFrame f(ctx, base, frame);
  ctx.r4.u64 = self;
  ctx.r3.u64 = ld32(base, static_cast<uint32_t>(self) + member_off);
  call_guest(ctx, base, first);
  ctx.r3.u64 = self;
  ctx.f1.u64 = f1;
  call_guest(ctx, base, second);
}

// Cadeia: a(this + off, arg) → b(resultado) → c(global, resultado de b). Devolve o de c.
inline void chain3_global(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t off, uint32_t arg, uint32_t a,
                          uint32_t b, uint32_t global, uint32_t c) {
  GuestFrame f(ctx, base, frame);
  ctx.r4.u64 = arg;
  ctx.r3.u64 = ctx.r3.u64 + static_cast<int64_t>(off);
  call_guest(ctx, base, a);
  call_guest(ctx, base, b);
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.s64 = static_cast<int32_t>(global);
  call_guest(ctx, base, c);
}

// Inicializador estático: construtor(objeto global) e registro do destrutor (atexit).
inline void static_init(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t obj, uint32_t ctor, uint32_t arg2,
                        uint32_t reg) {
  GuestFrame f(ctx, base, frame);
  ctx.r3.s64 = static_cast<int32_t>(obj);
  call_guest(ctx, base, ctor);
  ctx.r3.s64 = static_cast<int32_t>(arg2);
  call_guest(ctx, base, reg);
}
}  // namespace nier::lifecycle
