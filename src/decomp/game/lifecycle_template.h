// Ciclo de vida de objetos C++ do NieR (construtores e destrutores gerados pelo
// compilador), reconstruído como templates e instanciado por tipo.
// Alvos de código são símbolos (PPCFunc*): chamada direta, como o `bl` traduzido.
#pragma once
#include <nier/decomp.h>

namespace nier::lifecycle {
using namespace nier::guest;

// Destrutor "com delete": restaura a vtable da classe; se flags & 1, libera o objeto
// com operator delete(this, size). Devolve this.
inline void dtor_vtable_delete(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vtable, uint32_t size,
                               PPCFunc* op_delete, uint32_t vt_off = 0) {
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
inline void ctor_base_field_vtable(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* base_ctor, uint32_t arg,
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
inline void dtor_allocator_delete(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* dtor, PPCFunc* get_allocator,
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
inline void dtor_vtable_free(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vtable, PPCFunc* op_delete,
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
inline void dtor_body_free(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* dtor, PPCFunc* op_delete) {
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
                                     PPCFunc* first, PPCFunc* second) {
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
inline void chain3_global(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t off, uint32_t arg, PPCFunc* a,
                          PPCFunc* b, uint32_t global, PPCFunc* c) {
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
inline void static_init(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t obj, PPCFunc* ctor, uint32_t arg2,
                        PPCFunc* reg) {
  GuestFrame f(ctx, base, frame);
  ctx.r3.s64 = static_cast<int32_t>(obj);
  call_guest(ctx, base, ctor);
  ctx.r3.s64 = static_cast<int32_t>(arg2);
  call_guest(ctx, base, reg);
}
}  // namespace nier::lifecycle

namespace nier::lifecycle {
// Construtor: base(this, arg); memset(this + block, value, size); vtable; dois floats iguais.
inline void ctor_base_memset(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* base_ctor, uint32_t arg,
                             int32_t block, uint32_t value, uint32_t size, PPCFunc* memset_fn, uint32_t vtable,
                             uint32_t float_addr, int32_t vt_off, int32_t f_off_a, int32_t f_off_b) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    ctx.r4.u64 = arg;
    call_guest(ctx, base, base_ctor);
    ctx.r3.u64 = static_cast<uint64_t>(self) + static_cast<int64_t>(block);
    ctx.r5.u64 = size;
    ctx.r4.u64 = value;
    call_guest(ctx, base, memset_fn);
  }
  const float init = ldf(base, float_addr);
  st32(base, self + vt_off, vtable);
  stf(base, self + f_off_a, init);
  stf(base, self + f_off_b, init);
  ctx.r3.u64 = self;
}

// Método: a(membro, this); b(membro, this); this->método virtual; c(this).
inline void member_pair_virtual_then(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t member_off, PPCFunc* a,
                                     PPCFunc* b, uint32_t slot, PPCFunc* c) {
  const uint32_t self = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.u64 = ld32(base, self + member_off);
  call_guest(ctx, base, a);
  ctx.r4.u64 = self;
  ctx.r3.u64 = ld32(base, self + member_off);
  call_guest(ctx, base, b);
  ctx.r3.u64 = self;
  call_virtual(ctx, base, self, slot);
  ctx.r3.u64 = self;
  call_guest(ctx, base, c);
}

// Busca: índice = find(this + off, 1, x, y); -1 se não achou; senão tenta
// resolve(this, 2*índice, 2*índice + 2) e, se falhar, devolve 4*índice.
inline void find_then_resolve(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t off, uint32_t first_arg,
                              PPCFunc* find, PPCFunc* resolve, uint32_t sh1 = 1, uint32_t mb1 = 0, uint32_t me1 = 30,
                              uint32_t sh2 = 2, uint32_t mb2 = 0, uint32_t me2 = 29) {
  const uint64_t self = ctx.r3.u64;
  GuestFrame f(ctx, base, frame);
  ctx.r6.u64 = ctx.r5.u64;
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.u64 = first_arg;
  ctx.r3.u64 = self + static_cast<int64_t>(off);
  call_guest(ctx, base, find);
  const uint64_t index = ctx.r3.u64;
  if (static_cast<int32_t>(index) == -1) { ctx.r3.u64 = ~0ull; return; }
  ctx.r4.u64 = rlwinm(static_cast<uint32_t>(index), sh1, mb1, me1);
  ctx.r3.u64 = self;
  ctx.r5.u64 = ctx.r4.u64 + 2;
  call_guest(ctx, base, resolve);
  if (static_cast<int32_t>(ctx.r3.u32) == -1) ctx.r3.u64 = rlwinm(static_cast<uint32_t>(index), sh2, mb2, me2);
}
}  // namespace nier::lifecycle

namespace nier::lifecycle {
// Destrutor de classe com duas bases e dois membros por ponteiro: grava as vtables das
// bases, destrói o membro 1 (destrutor virtual com delete), grava a vtable intermediária,
// destrói o membro 2 e chama o destrutor da base.
inline void dtor_two_members(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t m1_off, uint32_t vt_a,
                             uint32_t vt_a_off, uint32_t vt_b, uint32_t vt_b_off, uint32_t slot1, uint32_t arg1,
                             uint32_t m2_off, uint32_t vt_c, uint32_t vt_c_off, uint32_t slot2, uint32_t arg2,
                             PPCFunc* base_dtor) {
  const uint32_t self = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  const uint32_t m1 = ld32(base, self + m1_off);
  st32(base, self + vt_a_off, vt_a);
  st32(base, self + vt_b_off, vt_b);
  if (m1 != 0) {
    ctx.r3.u64 = m1;
    ctx.r4.u64 = arg1;
    call_virtual(ctx, base, m1, slot1);
  }
  const uint32_t m2 = ld32(base, self + m2_off);
  st32(base, self + vt_c_off, vt_c);
  if (m2 != 0) {
    ctx.r3.u64 = m2;
    ctx.r4.u64 = arg2;
    call_virtual(ctx, base, m2, slot2);
  }
  ctx.r3.u64 = self;
  call_guest(ctx, base, base_dtor);
}

// Fábrica de nó: aloca pelo alocador do dono (+alloc_off, método `slot`), inicializa o nó
// como lista vazia com vtable e escala padrão, guarda o alocador e insere no dono.
inline void make_node(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t alloc_off, uint32_t size,
                      uint32_t align, uint32_t slot, uint32_t vtable, uint32_t scale_addr, PPCFunc* insert) {
  const uint32_t owner = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  const uint32_t allocator = ld32(base, owner + alloc_off);
  ctx.r3.u64 = allocator;
  ctx.r5.u64 = align;
  ctx.r4.u64 = size;
  call_virtual(ctx, base, allocator, slot);
  uint32_t node = ctx.r3.u32;
  if (node != 0) {
    const uint32_t link = node + 4;
    st32(base, node + 4, link);
    st32(base, node + 8, link);
    st32(base, node + 12, link - 4);
    const float scale = ldf(base, scale_addr);
    st32(base, node + 0, vtable);
    stf(base, node + 16, scale);
    st32(base, node + 20, 0);
  } else {
    node = 0;
  }
  st32(base, node + 20, ld32(base, owner + alloc_off));
  ctx.r5.u64 = static_cast<uint64_t>(node) + 4;
  ctx.r4.u64 = owner;
  ctx.r3.u64 = owner;
  call_guest(ctx, base, insert);
  ctx.r3.u64 = node;
}
}  // namespace nier::lifecycle

namespace nier::lifecycle {
// Bit/campo de uma palavra: rlwinm(campo, sh, mb, me).
inline void field_bits(PPCContext& ctx, uint8_t* base, uint32_t off, uint32_t sh, uint32_t mb, uint32_t me) {
  ctx.r3.u64 = rlwinm(ld32(base, ctx.r3.u32 + off), sh, mb, me);
}
// Endereço de um objeto global (lis/addi: sinal estendido no registrador de 64 bits).
inline void global_address(PPCContext& ctx, uint32_t addr) { ctx.r3.s64 = static_cast<int32_t>(addr); }
// Campo limitado a [0, max] somado a uma base.
inline void clamped_plus(PPCContext& ctx, uint8_t* base, uint32_t off, int32_t max, int32_t add) {
  int32_t v = static_cast<int32_t>(ld32(base, ctx.r3.u32 + off));
  if (v <= 0) v = 0; else if (v >= max) v = max;
  ctx.r3.u64 = static_cast<uint64_t>(static_cast<int64_t>(v) + add);
}
// Item `index` de uma lista de ponteiros (quantidade em +count_off, itens a partir de
// +first*4); devolve o campo +field do item, ou 0.
inline void list_item_field(PPCContext& ctx, uint8_t* base, uint32_t count_off, int32_t first, uint32_t field,
                            int32_t missing = 0) {
  const uint32_t self = ctx.r3.u32;
  const int32_t index = static_cast<int32_t>(ctx.r4.u32);
  if (index >= 0 && index < static_cast<int32_t>(ld32(base, self + count_off))) {
    const uint32_t item = ld32(base, self + static_cast<uint32_t>(index + first) * 4);
    if (item != 0) { ctx.r3.u64 = ld32(base, item + field); return; }
  }
  ctx.r3.s64 = missing;   // valor de "não achou" (li: com sinal)
}
}  // namespace nier::lifecycle
