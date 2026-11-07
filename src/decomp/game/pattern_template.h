// Padrões repetidos do código do NieR (gerados pelo compilador ou por macros do jogo),
// reconstruídos como templates nativos e instanciados por tools/decomp/gen_pattern_instances.py.
// Alvos de código são símbolos (PPCFunc*): chamada direta, como o `bl` traduzido.
#pragma once
#include <nier/decomp.h>
#include <cmath>

namespace nier::pattern {
using namespace nier::guest;

// Pede um objeto ao "getter" (que escreve o ponteiro na pilha), liga this nele, pede ao
// próprio this (método virtual `slot`) um bloco de `size` bytes e, se veio, inicializa com
// init(bloco, arg). Devolve o bloco inicializado ou 0.
inline void alloc_then_init(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t tmp_off, PPCFunc* getter,
                            uint32_t size, int32_t link_off, uint32_t slot, PPCFunc* init) {
  const uint32_t self = ctx.r3.u32;
  const uint64_t arg = ctx.r4.u64;
  GuestFrame f(ctx, base, frame);
  ctx.r3.u64 = f.slot(tmp_off);
  getter(ctx, base);
  const uint32_t holder = ld32(base, f.slot(tmp_off));
  st32(base, holder + link_off, self);
  ctx.r4.u64 = size;
  ctx.r3.u64 = self;
  call_virtual(ctx, base, self, slot);
  if (ctx.r3.u32 == 0) { ctx.r3.u64 = 0; return; }
  ctx.r4.u64 = arg;
  init(ctx, base);
}

// Construtor com vetor zerado (elemento 2 = 1.0f), campos zerados, duas vtables, três
// bytes de flag e uma fase inicial aleatória: rand() do MSVC numa semente própria,
// fase = (rand() >> 16 & 0x7FFF) * escala + deslocamento (fmadds).
struct CtorVecRand {
  PPCFunc* base_ctor;
  uint32_t const_a, const_b, const_c, const_d;   // floats na imagem
  uint32_t vt1, vt2, seed_addr;
  uint32_t mult, add;
  int16_t vec_off;                 // vetor de 16 bytes (endereço alinhado a 16)
  int16_t zero_offs[7];            // palavras zeradas
  int16_t fa_off, fb_off;          // const_a gravado duas vezes
  int16_t vt1_off, vt2_off;
  int16_t one_offs[3], zero_byte_off;
  int16_t fb2_off, phase_off;      // const_b e a fase calculada
};
inline void ctor_vec_rand(PPCContext& ctx, uint8_t* base, uint32_t frame, const CtorVecRand& p) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    p.base_ctor(ctx, base);   // (this, r4 do chamador)
  }
  const uint32_t vec = (self + static_cast<uint32_t>(static_cast<int32_t>(p.vec_off))) & ~0xFu;
  st32(base, vec + 0, 0); st32(base, vec + 4, 0); stf(base, vec + 8, 1.0f); st32(base, vec + 12, 0);
  for (int16_t o : p.zero_offs) st32(base, self + o, 0);
  const float a = ldf(base, p.const_a);
  stf(base, self + p.fa_off, a);
  stf(base, self + p.fb_off, a);
  st32(base, self + p.vt1_off, p.vt1);
  for (int16_t o : p.one_offs) st8(base, self + o, 1);
  st8(base, self + p.zero_byte_off, 0);
  st32(base, self + p.vt2_off, p.vt2);
  stf(base, self + p.fb2_off, ldf(base, p.const_b));
  const uint32_t seed = ld32(base, p.seed_addr) * p.mult + p.add;
  st32(base, p.seed_addr, seed);
  const float r = static_cast<float>(static_cast<double>((seed >> 16) & 0x7FFFu));   // lhz: meia palavra alta
  const float phase = static_cast<float>(std::fma(static_cast<double>(r), static_cast<double>(ldf(base, p.const_c)),
                                                  static_cast<double>(ldf(base, p.const_d))));
  stf(base, self + p.phase_off, phase);
  ctx.r3.u64 = self;
}

// Filtro FIR de 16 coeficientes por amostra de saída (áudio): para cada uma das `count`
// saídas, soma coef[t] * in[t] em cadeia (fmadds, mesma ordem do original) e multiplica
// pelo ganho. Coeficientes em colunas (passo `coef_step` entre taps, +4 por saída),
// entradas em sequência (`in_step` entre taps, `in_tail` depois do último).
struct Fir16 {
  int32_t count;
  uint32_t gain_addr;
  int16_t out_pre, out_step;
  int16_t coef_step[15];
  int16_t in_step[15];
  int16_t in_tail;
};
inline void fir16(PPCContext& ctx, uint8_t* base, const Fir16& p) {
  uint32_t coef = ctx.r3.u32, in = ctx.r4.u32;
  uint32_t out = ctx.r5.u32 + static_cast<uint32_t>(static_cast<int32_t>(p.out_pre));
  const double gain = ldf(base, p.gain_addr);
  for (int32_t i = 0; i < p.count; ++i) {
    uint32_t c = coef, x = in;
    double acc = static_cast<float>(static_cast<double>(ldf(base, c)) * static_cast<double>(ldf(base, x)));
    for (int t = 0; t < 15; ++t) {
      c += static_cast<uint32_t>(static_cast<int32_t>(p.coef_step[t]));
      x += static_cast<uint32_t>(static_cast<int32_t>(p.in_step[t]));
      acc = static_cast<float>(std::fma(static_cast<double>(ldf(base, c)), static_cast<double>(ldf(base, x)), acc));
    }
    out += static_cast<uint32_t>(static_cast<int32_t>(p.out_step));
    stf(base, out, static_cast<float>(acc * gain));
    coef += 4;
    in = x + static_cast<uint32_t>(static_cast<int32_t>(p.in_tail));
  }
  ctx.f1.f64 = gain;
}

// Construtor: base(this, arg); duas vtables/ponteiros e um campo com valor imediato.
inline void ctor_base_vt_fields(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* base_ctor, int32_t arg,
                                uint32_t vt1, int32_t off1, uint32_t vt2, int32_t off2, int32_t val, int32_t off3) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    ctx.r4.s64 = arg;
    base_ctor(ctx, base);
  }
  st32(base, self + off1, vt1);
  st32(base, self + off2, vt2);
  st32(base, self + off3, static_cast<uint32_t>(val));
  ctx.r3.u64 = self;
}

// Duas cadeias a(this+off, arg) → b() e, no fim, c(global, resultado1, resultado2).
inline void chain2x_global(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t off, int32_t arg1, PPCFunc* a1,
                           PPCFunc* b1, int32_t arg2, PPCFunc* a2, PPCFunc* b2, uint32_t global, PPCFunc* c) {
  const uint64_t x = ctx.r3.u64 + static_cast<int64_t>(off);
  GuestFrame f(ctx, base, frame);
  ctx.r4.s64 = arg1; ctx.r3.u64 = x; a1(ctx, base); b1(ctx, base);
  const uint64_t first = ctx.r3.u64;
  ctx.r4.s64 = arg2; ctx.r3.u64 = x; a2(ctx, base); b2(ctx, base);
  ctx.r5.u64 = ctx.r3.u64;
  ctx.r4.u64 = first;
  ctx.r3.s64 = static_cast<int32_t>(global);
  c(ctx, base);
}

// Testa o bit `index` numa tabela de palavras de 64 bits (linha = index >> 8, a partir de
// `row_base`; bit = (index >> 2) & 63). Sem o bit: -1. Com ele: find(this); se devolver -1,
// resolve(this, (index >> 2) + 1, (((index >> 2) >> 1) + 1) * 2).
struct BitsetFind {
  PPCFunc* find; PPCFunc* resolve;
  int32_t row_base, idx_add, half_add;
  uint8_t row_sh, row_mb, row_me;      // rlwinm que extrai a linha
  uint8_t bit_sh, bit_mb, bit_me;      // rlwinm que extrai o bit
  uint8_t addr_sh, addr_mb, addr_me;   // rlwinm linha → deslocamento em bytes
  uint8_t idx_sh, idx_mb, idx_me;      // rlwinm que extrai o índice
  uint8_t half_sh, half_mb, half_me;   // rlwinm índice → metade
  uint8_t dbl_sh, dbl_mb, dbl_me;      // rlwinm metade → dobro
};
inline void bitset_find_resolve(PPCContext& ctx, uint8_t* base, uint32_t frame, const BitsetFind& p) {
  const uint64_t self = ctx.r3.u64;
  const uint32_t key = ctx.r4.u32;
  const uint32_t row = rlwinm(key, p.row_sh, p.row_mb, p.row_me) + static_cast<uint32_t>(p.row_base);
  const uint32_t bit = rlwinm(key, p.bit_sh, p.bit_mb, p.bit_me);
  const uint32_t index = rlwinm(key, p.idx_sh, p.idx_mb, p.idx_me);
  const uint64_t word = ld64(base, static_cast<uint32_t>(self) + rlwinm(row, p.addr_sh, p.addr_mb, p.addr_me));
  if ((srd(word, bit) & 1) == 0) { ctx.r3.s64 = -1; return; }
  GuestFrame f(ctx, base, frame);
  ctx.r3.u64 = self;
  p.find(ctx, base);
  if (static_cast<int32_t>(ctx.r3.u32) != -1) return;
  const uint32_t half = rlwinm(index, p.half_sh, p.half_mb, p.half_me) + static_cast<uint32_t>(p.half_add);
  ctx.r4.u64 = static_cast<uint64_t>(index) + static_cast<int64_t>(p.idx_add);
  ctx.r5.u64 = rlwinm(half, p.dbl_sh, p.dbl_mb, p.dbl_me);
  ctx.r3.u64 = self;
  p.resolve(ctx, base);
}

// Copia a lista encadeada (cabeça em +head_off, próximo em +next_off) para um vetor na
// pilha; com ao menos `min_count` itens, sort(vetor, n) e apply(this, vetor, n).
inline void collect_sort_apply(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t head_off, int32_t next_off,
                               int32_t arr_pre, int32_t arr_step, int32_t arr_off, int32_t min_count, PPCFunc* sort,
                               PPCFunc* apply) {
  const uint32_t self = ctx.r3.u32;
  uint32_t node = ld32(base, self + head_off);
  if (node == 0) return;
  GuestFrame f(ctx, base, frame);
  uint32_t slot = f.slot(arr_pre);
  int32_t count = 0;
  do {
    const uint32_t item = node;
    node = ld32(base, node + next_off);
    ++count;
    slot += arr_step;
    st32(base, slot, item);
  } while (node != 0);
  if (count < min_count) return;
  ctx.r4.s64 = count;
  ctx.r3.u64 = f.slot(arr_off);
  sort(ctx, base);
  ctx.r5.s64 = count;
  ctx.r4.u64 = f.slot(arr_off);
  ctx.r3.u64 = self;
  apply(ctx, base);
}

// *out = 0; t = f(a, b); *out = g(this->campo + add, t); devolve `ret`.
inline void make_pair_result(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t a, int32_t b, PPCFunc* f,
                             int32_t off, int32_t add, PPCFunc* g, int32_t ret) {
  const uint32_t self = ctx.r3.u32, out = ctx.r4.u32;
  GuestFrame fr(ctx, base, frame);
  st32(base, out, 0);
  ctx.r4.s64 = b; ctx.r3.s64 = a;
  f(ctx, base);
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.u64 = static_cast<uint64_t>(ld32(base, self + off)) + static_cast<int64_t>(add);
  g(ctx, base);
  st32(base, out, ctx.r3.u32);
  ctx.r3.s64 = ret;
}
}  // namespace nier::pattern
using namespace nier::pattern;

namespace nier::pattern {
// Chama o método `slot` do sub-objeto em this+sub_off com (sub, global, arg5) e grava `value`
// em this+field_off.
inline void sub_virtual_then_set(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t sub_off, uint32_t global,
                                 int32_t arg5, uint32_t slot, int32_t field_off, int32_t value) {
  const uint32_t self = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  const uint32_t sub = self + sub_off;
  ctx.r5.s64 = arg5;
  ctx.r4.s64 = static_cast<int32_t>(global);
  ctx.r3.u64 = sub;
  call_virtual(ctx, base, sub, slot);
  st32(base, self + field_off, static_cast<uint32_t>(value));
}

// Construtor: base(this, arg); byte; vtable; float constante.
inline void ctor_base_vt_byte_float(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* base_ctor, int32_t arg,
                                    int32_t byte_off, int32_t byte_val, uint32_t vt, int32_t vt_off,
                                    uint32_t const_addr, int32_t f_off) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    ctx.r4.s64 = arg;
    base_ctor(ctx, base);
  }
  st8(base, self + byte_off, static_cast<uint8_t>(byte_val));
  st32(base, self + vt_off, vt);
  stf(base, self + f_off, ldf(base, const_addr));
  ctx.r3.u64 = self;
}

// Registro de singleton: grava a vtable; se a flag global já estava ligada, erro fatal
// (função que não volta) e laço infinito; senão liga a flag e guarda o ponteiro.
inline void singleton_register(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vt, uint32_t flag_addr,
                               PPCFunc* fatal, uint32_t message, uint32_t ptr_addr, int32_t flag_value) {
  const uint32_t self = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  st32(base, self, vt);
  if (static_cast<int32_t>(ld32(base, flag_addr)) != 0) {
    ctx.r3.s64 = static_cast<int32_t>(message);
    fatal(ctx, base);
    for (;;) nier::host::spin_pause();
  }
  st32(base, flag_addr, static_cast<uint32_t>(flag_value));
  st32(base, ptr_addr, self);
}

// Normaliza um ângulo à faixa [lo, hi] somando/subtraindo `step` no máximo 20 vezes e
// devolve (x * k1) * k2; estourando as voltas devolve (zero * k1) * k2.
struct AngleWrap { uint32_t lo, step, hi, k1, k2, zero; int32_t turns; };
inline void angle_wrap(PPCContext& ctx, uint8_t* base, const AngleWrap& p) {
  double x = ctx.f1.f64;
  int32_t n = p.turns;
  const double lo = ldf(base, p.lo), step = ldf(base, p.step);
  if (!(x >= lo)) { do { x = static_cast<float>(x + step); --n; } while (x < lo); }
  const double hi = ldf(base, p.hi);
  if (!(x <= hi)) {
    do {
      --n;
      x = static_cast<float>(x - step);
      if (n < 0) { x = ldf(base, p.zero); break; }
    } while (x > hi);
  }
  const double k1 = ldf(base, p.k1), k2 = ldf(base, p.k2);
  ctx.f1.f64 = static_cast<float>(static_cast<double>(static_cast<float>(x * k1)) * k2);
}

// Construtor: duas vtables; liga this->campo no objeto do "getter"; com flags & 1, pede
// de novo o getter e chama o método `slot` do objeto em +obj_off com (obj, this).
inline void ctor_vt2_link(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vt_a, int32_t vt_a_off,
                          uint32_t vt_b, int32_t vt_b_off, int32_t tmp_off, PPCFunc* getter, int32_t src_off,
                          int32_t link_off, int32_t obj_off, uint32_t slot) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  GuestFrame f(ctx, base, frame);
  st32(base, self + vt_a_off, vt_a);
  st32(base, self + vt_b_off, vt_b);
  ctx.r3.u64 = f.slot(tmp_off);
  getter(ctx, base);
  const uint32_t holder = ld32(base, f.slot(tmp_off));
  st32(base, holder + link_off, ld32(base, self + src_off));
  if (flags & 1) {
    ctx.r3.u64 = f.slot(tmp_off);
    getter(ctx, base);
    const uint32_t obj = ld32(base, ld32(base, f.slot(tmp_off)) + obj_off);
    ctx.r4.u64 = self;
    ctx.r3.u64 = obj;
    call_virtual(ctx, base, obj, slot);
  }
  ctx.r3.u64 = self;
}

// Cinco chamadas f(this, 0, flag) com flag = (n == k) para k = 0, 1, {2,3}, 3, 4.
struct FiveFlags { PPCFunc* a; PPCFunc* b; PPCFunc* c; PPCFunc* d; PPCFunc* e; int32_t arg; int32_t d1, c1, c2, d3, d4; };
inline void five_flag_calls(PPCContext& ctx, uint8_t* base, uint32_t frame, const FiveFlags& p) {
  const uint32_t self = ctx.r3.u32, n = ctx.r4.u32;
  GuestFrame f(ctx, base, frame);
  auto call = [&](PPCFunc* fn, uint64_t flag) { ctx.r5.u64 = flag; ctx.r4.s64 = p.arg; ctx.r3.u64 = self; fn(ctx, base); };
  call(p.a, n == 0);
  call(p.b, (n + p.d1) == 0);
  call(p.c, (static_cast<int32_t>(n) == p.c1 || static_cast<int32_t>(n) == p.c2) ? 1 : 0);
  call(p.d, (n + p.d3) == 0);
  call(p.e, (n + p.d4) == 0);
}

// Inicialização de efeito: pede ao sistema (global, args, floats), marca dois campos,
// e se há dono (+owner_off) resolve o alvo e chama o método virtual com (alvo, k, 0, f1, f2).
struct EffectInit {
  PPCFunc* request; PPCFunc* resolve; uint32_t global, const_a, const_b, const_f1;
  int32_t r4, r5, r7, r8, owner_off, mark, mark_off1, mark_off2, resolve_arg, v4, v5; uint32_t slot; int32_t done, done_off;
};
inline void effect_init(PPCContext& ctx, uint8_t* base, uint32_t frame, const EffectInit& p) {
  const uint32_t self = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  const double a = ldf(base, p.const_a), b = ldf(base, p.const_b);
  ctx.r3.s64 = static_cast<int32_t>(p.global);
  ctx.f1.f64 = ldf(base, p.const_f1); ctx.f2.f64 = b; ctx.f3.f64 = a;
  ctx.r8.s64 = p.r8; ctx.r7.s64 = p.r7; ctx.r5.s64 = p.r5; ctx.r4.s64 = p.r4;
  p.request(ctx, base);
  const uint32_t owner = ld32(base, self + p.owner_off);
  st32(base, self + p.mark_off1, static_cast<uint32_t>(p.mark));
  st32(base, self + p.mark_off2, static_cast<uint32_t>(p.mark));
  ctx.r3.u64 = owner;
  if (owner == 0) return;
  ctx.r4.s64 = p.resolve_arg;
  p.resolve(ctx, base);
  const uint32_t target = ctx.r3.u32;
  if (target == 0) return;
  ctx.r5.s64 = p.v5; ctx.r4.s64 = p.v4;
  ctx.f2.f64 = b; ctx.f1.f64 = a;
  call_virtual(ctx, base, target, p.slot);
  st32(base, self + p.done_off, static_cast<uint32_t>(p.done));
}

// Clonagem condicional: se o tipo em r5+type_off não é `type`, só a chamada base; senão
// base, aloca um novo via factory(dono->+alloc_off), liga ao dono, copia 56 bytes de
// this+src_off (5 palavras, vetor de 16 bytes alinhado, 2 floats), marca e devolve 1.
struct CloneIf { PPCFunc* base_fn; PPCFunc* factory; int32_t type_off, type, owner_off, alloc_off, owner_link, src_off, vec_off, mark_off, mark, ret; };
inline void clone_if_type(PPCContext& ctx, uint8_t* base, uint32_t frame, const CloneIf& p) {
  const uint32_t self = ctx.r3.u32;
  GuestFrame f(ctx, base, frame);
  if (static_cast<int32_t>(ld32(base, ctx.r5.u32 + p.type_off)) != p.type) { p.base_fn(ctx, base); return; }
  p.base_fn(ctx, base);
  const uint32_t owner = ld32(base, self + p.owner_off);
  ctx.r3.u64 = ld32(base, owner + p.alloc_off);
  p.factory(ctx, base);
  const uint32_t node = ctx.r3.u32;
  st32(base, node + p.owner_link, owner);
  const uint32_t src = self + p.src_off, dst = node + p.src_off;
  if (src != 0) {
    for (int k = 0; k < 20; k += 4) st32(base, dst + k, ld32(base, src + k));
    const uint32_t vs = (src + p.vec_off) & ~0xFu, vd = (dst + p.vec_off) & ~0xFu;
    std::memcpy(base + vd, base + vs, 16);
    st32(base, dst + 48, ld32(base, src + 48));
    st32(base, dst + 52, ld32(base, src + 52));
  }
  st8(base, node + p.mark_off, static_cast<uint8_t>(p.mark));
  ctx.r3.s64 = p.ret;
}

// Cópia de string limitada: sem origem, this[0] = 0; senão copia min(strlen+1, limit)
// bytes com `copy` e termina em this[end_off].
inline void bounded_string_copy(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t first_off, uint32_t limit,
                                PPCFunc* copy, int32_t end_off) {
  const uint32_t self = ctx.r3.u32, src = ctx.r4.u32;
  if (src == 0) { st8(base, self + first_off, 0); return; }
  GuestFrame f(ctx, base, frame);
  uint32_t len = 0;
  while (ld8(base, src + len) != 0) ++len;
  const uint32_t n = len + 1;
  ctx.r5.u64 = n >= limit ? limit : n;
  ctx.r3.u64 = self;
  copy(ctx, base);
  st8(base, self + end_off, 0);
}
}  // namespace nier::pattern

namespace nier::pattern {
// Encaminhador: salta para o método `slot` do objeto em r3 (bctr, sem quadro).
inline void vcall_tail(PPCContext& ctx, uint8_t* base, uint32_t slot) { call_virtual(ctx, base, ctx.r3.u32, slot); }

// Testa o bit `key` numa tabela de palavras de 64 bits (linha = rlwinm(key) + row_base,
// bit = rlwinm(key)); sem o bit devolve `miss`; com ele chama probe(this, key) e troca 0 por `fallback`.
struct BitsetProbe { PPCFunc* probe; int32_t row_base, miss, fallback; uint8_t row_sh, row_mb, row_me, bit_sh, bit_mb, bit_me, addr_sh, addr_mb, addr_me; };
inline void bitset_probe(PPCContext& ctx, uint8_t* base, uint32_t frame, const BitsetProbe& p) {
  const uint32_t self = ctx.r3.u32, key = ctx.r4.u32;
  const uint32_t row = rlwinm(key, p.row_sh, p.row_mb, p.row_me) + static_cast<uint32_t>(p.row_base);
  const uint32_t bit = rlwinm(key, p.bit_sh, p.bit_mb, p.bit_me);
  const uint64_t word = ld64(base, self + rlwinm(row, p.addr_sh, p.addr_mb, p.addr_me));
  if ((srd(word, bit) & 1) == 0) { ctx.r3.s64 = p.miss; return; }
  GuestFrame f(ctx, base, frame);
  p.probe(ctx, base);
  if (ctx.r3.u32 == 0) ctx.r3.s64 = p.fallback;
}

// Construtor: vtable, init(this) e, com flags & 1, delete(this, size). Devolve this.
inline void ctor_vt_init_opt(PPCContext& ctx, uint8_t* base, uint32_t frame, uint32_t vt, int32_t vt_off, PPCFunc* init,
                             int32_t size, PPCFunc* op_delete) {
  const uint32_t self = ctx.r3.u32, flags = ctx.r4.u32;
  GuestFrame f(ctx, base, frame);
  st32(base, self + vt_off, vt);
  init(ctx, base);
  ctx.r3.u64 = self;
  if (flags & 1) { ctx.r4.s64 = size; op_delete(ctx, base); ctx.r3.u64 = self; }
}

// *out = 0; x = lookup(this, r4 & 0xFF); sem x devolve 0; senão t = f(a, b); *out = g(x + add, t); devolve `ret`.
inline void make_pair_result2(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* lookup, int32_t a, int32_t b,
                              PPCFunc* f, int32_t add, PPCFunc* g, int32_t ret) {
  const uint32_t out = ctx.r5.u32;
  GuestFrame fr(ctx, base, frame);
  ctx.r4.u64 = ctx.r4.u32 & 0xFF;
  st32(base, out, 0);
  lookup(ctx, base);
  const uint32_t x = ctx.r3.u32;
  if (x == 0) { ctx.r3.u64 = 0; return; }
  ctx.r4.s64 = b; ctx.r3.s64 = a;
  f(ctx, base);
  ctx.r4.u64 = ctx.r3.u64;
  ctx.r3.u64 = static_cast<uint64_t>(x) + static_cast<int64_t>(add);
  g(ctx, base);
  st32(base, out, ctx.r3.u32);
  ctx.r3.s64 = ret;
}

// a(this + off, arg); flag global = (b() != 0).
inline void chain_flag_global(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t off, int32_t arg, PPCFunc* a,
                              PPCFunc* b, uint32_t flag_addr) {
  GuestFrame f(ctx, base, frame);
  ctx.r4.s64 = arg;
  ctx.r3.u64 = ctx.r3.u64 + static_cast<int64_t>(off);
  a(ctx, base);
  b(ctx, base);
  st8(base, flag_addr, ctx.r3.u32 != 0 ? 1 : 0);
}

// Construtor: base(this, arg); byte; vtable; palavra. Devolve this.
inline void ctor_base_vt_byte_word(PPCContext& ctx, uint8_t* base, uint32_t frame, PPCFunc* base_ctor, int32_t arg,
                                   int32_t byte_off, int32_t val, uint32_t vt, int32_t vt_off, int32_t word_off) {
  const uint32_t self = ctx.r3.u32;
  {
    GuestFrame f(ctx, base, frame);
    ctx.r4.s64 = arg;
    base_ctor(ctx, base);
  }
  st8(base, self + byte_off, static_cast<uint8_t>(val));
  st32(base, self + vt_off, vt);
  st32(base, self + word_off, static_cast<uint32_t>(val));
  ctx.r3.u64 = self;
}

// Tira `node` da lista duplamente encadeada (cabeça/cauda/contagem em list); devolve o seguinte.
inline void list_unlink(PPCContext& ctx, uint8_t* base, int32_t prev_off, int32_t next_off, int32_t head_off,
                        int32_t tail_off, int32_t count_off) {
  const uint32_t list = ctx.r3.u32, node = ctx.r4.u32;
  if (node == 0) { ctx.r3.u64 = 0; return; }
  const uint32_t prev = ld32(base, node + prev_off), next = ld32(base, node + next_off);
  if (prev == 0) {
    st32(base, list + head_off, next);
    if (next != 0) st32(base, next + prev_off, 0);
  } else {
    st32(base, prev + next_off, next);
  }
  if (next == 0) {
    st32(base, list + tail_off, prev);
    if (prev != 0) st32(base, prev + next_off, 0);
  } else if (prev != 0) {
    st32(base, next + prev_off, prev);
  }
  st32(base, node + prev_off, 0);
  st32(base, node + next_off, 0);
  st32(base, list + count_off, ld32(base, list + count_off) - 1);
  ctx.r3.u64 = next;
}

// Thunk do CRT (manipulador de exceção): guarda r31/r30/lr abaixo da pilha, r31 = r12 - frame,
// chama handler(r30) e restaura tudo pelo encadeamento da pilha.
inline void seh_thunk(PPCContext& ctx, uint8_t* base, uint32_t frame, int32_t r31_adj, PPCFunc* handler) {
  const uint32_t sp = ctx.r1.u32;
  st64(base, sp - 8, ctx.r31.u64);
  ctx.r31.u64 = ctx.r12.u64 + static_cast<int64_t>(r31_adj);
  st64(base, sp - 16, ctx.r30.u64);
  ctx.r12.u64 = ctx.lr;
  st32(base, sp - 24, ctx.r12.u32);
  ctx.r1.u64 = sp - frame;
  st32(base, ctx.r1.u32, sp);
  ctx.r3.u64 = ctx.r30.u64;
  handler(ctx, base);
  ctx.r1.u64 = ld32(base, ctx.r1.u32);
  ctx.r31.u64 = ld64(base, ctx.r1.u32 - 8);
  ctx.r30.u64 = ld64(base, ctx.r1.u32 - 16);
  ctx.r12.u64 = ld32(base, ctx.r1.u32 - 24);
}

// Nó novo pelo alocador da lista: n = alloc(this, value); n = {value, next=0, prev=cauda};
// liga à cauda (ou vira cabeça), cauda = n, contagem+1. Devolve n (ou 0).
struct ListPush { PPCFunc* alloc; int32_t value_off, next_off, tail_off, prev_off, link_off, head_off, count_off; };
inline void list_push_back(PPCContext& ctx, uint8_t* base, uint32_t frame, const ListPush& p) {
  const uint32_t self = ctx.r3.u32, value = ctx.r4.u32;
  GuestFrame f(ctx, base, frame);
  p.alloc(ctx, base);
  const uint32_t n = ctx.r3.u32;
  if (n == 0) return;
  st32(base, n + p.value_off, value);
  st32(base, n + p.next_off, 0);
  const uint32_t tail = ld32(base, self + p.tail_off);
  st32(base, n + p.prev_off, tail);
  if (ld32(base, self + p.tail_off) != 0) st32(base, tail + p.link_off, n);
  else st32(base, self + p.head_off, n);
  const uint32_t count = ld32(base, self + p.count_off);
  st32(base, self + p.tail_off, n);
  st32(base, self + p.count_off, count + 1);
}
}  // namespace nier::pattern
