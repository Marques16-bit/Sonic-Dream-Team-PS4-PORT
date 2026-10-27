// Teste do runtime no PC (não no PS4):
//   clang++ -std=c++17 -Isrc tests/recomp_test.cpp src/recomp/recomp.cpp -o /tmp/recomp_test && /tmp/recomp_test
#include <stdio.h>
#include "recomp/recomp.h"

using namespace recomp;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FALHOU: %s (linha %d)\n", #cond, __LINE__); ++failures; } } while (0)

// "função recompilada": x0 = x0 + x1   (add x0, x0, x1; ret)
static void f_add(Context& c) { c.x[0] = c.x[0] + c.x[1]; }

// import do PS4: x0 = x0 * 2
static void host_double(Context& c) { c.x[0] *= 2; }

// função que faz blr para o endereço em x8 (como um método virtual faria)
static void f_calls_indirect(Context& c) { call_indirect(c, c.x[8]); }

int main() {
    CHECK(memory_init(1 << 20));

    // memória: gravar e ler de volta
    store<uint64_t>(0x100, 0x1122334455667788ull);
    CHECK(load<uint64_t>(0x100) == 0x1122334455667788ull);
    CHECK(load<uint32_t>(0x100) == 0x55667788u);       // little-endian, igual ao ARM64
    CHECK(load<uint8_t>(0x107) == 0x11);

    register_function(0x4000, f_add);
    register_function(0x4010, f_calls_indirect);
    uint64_t imp = register_import("_double_it", host_double);
    uint64_t missing = register_import("_nao_implementado", nullptr);
    CHECK(imp >= kImportBase);
    CHECK(missing == imp + 4);
    CHECK(function_count() == 2 && import_count() == 2);
    CHECK(lookup(0x4000) == f_add);
    CHECK(lookup(0x9999) == nullptr);

    Context c = {};
    c.x[0] = 40; c.x[1] = 2;

    c.x[8] = 0x4000; call_indirect(c, 0x4010);          // guest -> guest
    CHECK(c.x[0] == 42);

    c.x[8] = imp; call_indirect(c, 0x4010);             // guest -> import (host)
    CHECK(c.x[0] == 84);

    memory_free();
    printf(failures ? "%d falha(s)\n" : "recomp runtime: todos os testes passaram\n", failures);
    return failures ? 1 : 0;
}
