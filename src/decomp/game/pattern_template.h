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
