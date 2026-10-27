#include "recomp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace recomp {

namespace {
uint8_t* g_mem = nullptr;
size_t   g_mem_size = 0;

// Registro acontece na inicialização (uma thread); depois só há leitura.
std::unordered_map<uint64_t, GuestFunc> g_funcs;

struct Import { std::string name; HostFunc fn; };
std::vector<Import> g_imports;

[[noreturn]] void fatal(const char* what, uint64_t addr, const char* extra = "") {
    printf("[recomp] FATAL: %s 0x%llx %s\n", what, (unsigned long long)addr, extra);
    abort();
}
} // namespace

bool memory_init(size_t bytes) {
    memory_free();
    g_mem = (uint8_t*)calloc(1, bytes);   // zerado, como a memória anônima do iOS
    if (!g_mem) return false;
    g_mem_size = bytes;
    return true;
}

void memory_free() {
    free(g_mem);
    g_mem = nullptr;
    g_mem_size = 0;
}

uint8_t* memory_base() { return g_mem; }
size_t   memory_size() { return g_mem_size; }

void register_function(uint64_t guest_addr, GuestFunc fn) { g_funcs[guest_addr] = fn; }

uint64_t register_import(const char* symbol, HostFunc fn) {
    g_imports.push_back(Import{symbol, fn});
    return kImportBase + (uint64_t)(g_imports.size() - 1) * 4;
}

GuestFunc lookup(uint64_t guest_addr) {
    auto it = g_funcs.find(guest_addr);
    return it == g_funcs.end() ? nullptr : it->second;
}

size_t function_count() { return g_funcs.size(); }
size_t import_count()   { return g_imports.size(); }

void call_indirect(Context& ctx, uint64_t guest_addr) {
    if (guest_addr >= kImportBase) {
        size_t idx = (size_t)((guest_addr - kImportBase) / 4);
        if (idx >= g_imports.size()) fatal("import inválido", guest_addr);
        const Import& imp = g_imports[idx];
        if (!imp.fn) fatal("import SEM implementação:", guest_addr, imp.name.c_str());
        imp.fn(ctx);
        return;
    }
    GuestFunc fn = lookup(guest_addr);
    if (!fn) fatal("destino indireto não recompilado:", guest_addr);
    fn(ctx);
}

} // namespace recomp
