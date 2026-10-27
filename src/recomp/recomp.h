#pragma once
// ---------------------------------------------------------------------------
// Runtime da recompilação estática (ARM64 -> C++ -> x86_64).
//
// O recompilador (etapa futura) transforma cada função ARM64 do jogo numa função
// C++ que opera sobre um Context (os registradores ARM64) e sobre a memória
// "guest" (o espaço de endereços do binário original). Exemplo do que ele gera:
//
//   // UnityFramework+0x1a2b40
//   void f_1a2b40(recomp::Context& c) {
//       c.x[0] = recomp::load<uint64_t>(c.x[0] + 8);   // ldr x0, [x0, #8]
//       c.x[0] = c.x[0] + 1;                           // add x0, x0, #1
//       return;                                        // ret
//   }
//
// Chamadas diretas (bl) viram chamadas C++ normais. Chamadas indiretas (blr/br)
// passam por recomp::call_indirect, que procura o endereço na tabela. Imports
// (Metal, libSystem, UIKit...) recebem um endereço mágico e caem em funções do
// PS4 (o shim).
// ---------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace recomp {

struct Vec128 {
    union {
        uint8_t  b[16];
        uint16_t h[8];
        uint32_t s[4];
        uint64_t d[2];
        float    f[4];
        double   df[2];
    };
};

// Estado de uma thread ARM64.
struct Context {
    uint64_t x[31];   // x0..x30 (x30 = LR)
    uint64_t sp;
    bool     n, z, c, v;   // flags NZCV
    Vec128   q[32];        // v0..v31 (NEON/FP)
    uint64_t tpidr;        // tpidrro_el0: ponteiro de TLS no iOS
};

using GuestFunc = void (*)(Context&);
using HostFunc  = void (*)(Context&);   // import: lê x0..x7 (AAPCS64), devolve em x0 / v0

// ---- Memória guest -----------------------------------------------------------
bool     memory_init(size_t bytes);   // reserva o espaço do guest (endereços 0..bytes)
void     memory_free();
uint8_t* memory_base();
size_t   memory_size();

template <class T> inline T load(uint64_t addr) {
    T v;
    memcpy(&v, memory_base() + addr, sizeof(T));
    return v;
}
template <class T> inline void store(uint64_t addr, T v) {
    memcpy(memory_base() + addr, &v, sizeof(T));
}

// ---- Tabelas -----------------------------------------------------------------
// Faixa de endereços "mágicos" que representa funções importadas.
constexpr uint64_t kImportBase = 0x7F00000000000000ull;

// Funções recompiladas (chamado no início, uma vez por função gerada).
void register_function(uint64_t guest_addr, GuestFunc fn);

// Import: devolve o endereço guest mágico para gravar na GOT/stub correspondente.
// Se fn == nullptr, o símbolo é registrado como "sem implementação" e o uso em
// tempo de execução imprime o nome e aborta, o que revela exatamente o que falta.
uint64_t register_import(const char* symbol, HostFunc fn);

GuestFunc lookup(uint64_t guest_addr);          // nullptr se não existir
size_t    function_count();
size_t    import_count();

// blr / br: resolve o endereço e executa. Aborta com mensagem se não houver destino.
void call_indirect(Context& ctx, uint64_t guest_addr);

} // namespace recomp
