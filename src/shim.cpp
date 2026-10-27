#include "shim.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orbis/libkernel.h>

namespace shim {

// ---------------------------------------------------------------------------
// Implementações PS4 de símbolos que o jogo espera encontrar
// ---------------------------------------------------------------------------

uint64_t now_ns() {
    // sceKernelGetProcessTime devolve microssegundos desde o início do processo.
    return sceKernelGetProcessTime() * 1000ull;
}

static uint64_t mach_absolute_time_impl() { return now_ns(); }
static double   CACurrentMediaTime_impl() { return (double)now_ns() / 1e9; }

// Tabela: nome do símbolo original -> implementação no PS4.
// Cresce conforme o jogo for pedindo símbolos (veja o log "UNRESOLVED").
static const Entry kTable[] = {
    // Tempo (Apple)
    { "mach_absolute_time", (void*)&mach_absolute_time_impl },
    { "CACurrentMediaTime", (void*)&CACurrentMediaTime_impl },

    // libc / libkernel: repasse direto
    { "malloc",  (void*)&malloc  },
    { "free",    (void*)&free    },
    { "memcpy",  (void*)&memcpy  },
    { "memset",  (void*)&memset  },
    { "usleep",  (void*)&sceKernelUsleep },
};

static const int kTableCount = (int)(sizeof(kTable) / sizeof(kTable[0]));

void* resolve(const char* symbol) {
    for (int i = 0; i < kTableCount; ++i) {
        if (strcmp(kTable[i].name, symbol) == 0) return kTable[i].impl;
    }
    // Não existe ainda: registra para você saber exatamente o que implementar.
    printf("[shim] UNRESOLVED: %s\n", symbol);
    return nullptr;
}

int implemented_count() { return kTableCount; }

// ---------------------------------------------------------------------------
// Entrada: DualShock 4 -> layout "extended gamepad"
// ---------------------------------------------------------------------------

static inline float stick_axis(uint8_t v) {
    // DS4: 0..255 com centro em 128. Devolve -1..1.
    float f = ((float)v - 128.0f) / 127.0f;
    if (f >  1.0f) f =  1.0f;
    if (f < -1.0f) f = -1.0f;
    // Zona morta simples para evitar drift.
    return (f > -0.08f && f < 0.08f) ? 0.0f : f;
}

Gamepad translate_pad(const OrbisPadData& in) {
    Gamepad g;
    memset(&g, 0, sizeof(g));
    g.connected = in.connected != 0;

    const uint32_t b = in.buttons;
    g.buttonA  = (b & ORBIS_PAD_BUTTON_CROSS)    != 0;
    g.buttonB  = (b & ORBIS_PAD_BUTTON_CIRCLE)   != 0;
    g.buttonX  = (b & ORBIS_PAD_BUTTON_SQUARE)   != 0;
    g.buttonY  = (b & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    g.leftShoulder  = (b & ORBIS_PAD_BUTTON_L1) != 0;
    g.rightShoulder = (b & ORBIS_PAD_BUTTON_R1) != 0;
    g.dpadUp    = (b & ORBIS_PAD_BUTTON_UP)    != 0;
    g.dpadDown  = (b & ORBIS_PAD_BUTTON_DOWN)  != 0;
    g.dpadLeft  = (b & ORBIS_PAD_BUTTON_LEFT)  != 0;
    g.dpadRight = (b & ORBIS_PAD_BUTTON_RIGHT) != 0;
    g.buttonMenu    = (b & ORBIS_PAD_BUTTON_OPTIONS)   != 0;
    g.buttonOptions = (b & ORBIS_PAD_BUTTON_TOUCH_PAD) != 0;
    g.leftThumbstickButton  = (b & ORBIS_PAD_BUTTON_L3) != 0;
    g.rightThumbstickButton = (b & ORBIS_PAD_BUTTON_R3) != 0;

    g.leftTrigger  = (float)in.analogButtons.l2 / 255.0f;
    g.rightTrigger = (float)in.analogButtons.r2 / 255.0f;

    g.leftX  =  stick_axis(in.leftStick.x);
    g.leftY  = -stick_axis(in.leftStick.y);   // DS4: Y cresce para baixo
    g.rightX =  stick_axis(in.rightStick.x);
    g.rightY = -stick_axis(in.rightStick.y);
    return g;
}

// ---------------------------------------------------------------------------
// Glifos de botão
// ---------------------------------------------------------------------------

const char* glyph_asset(Glyph g) {
    // Com kForceDualShockGlyphs nunca há ramo para Xbox/Switch/teclado.
    static_assert(cfg::kForceDualShockGlyphs, "este port só tem glifos PlayStation");
    switch (g) {
        case Glyph::Cross:    return "/app0/assets/ui/ps4/cross.png";
        case Glyph::Circle:   return "/app0/assets/ui/ps4/circle.png";
        case Glyph::Square:   return "/app0/assets/ui/ps4/square.png";
        case Glyph::Triangle: return "/app0/assets/ui/ps4/triangle.png";
        case Glyph::L1:       return "/app0/assets/ui/ps4/l1.png";
        case Glyph::R1:       return "/app0/assets/ui/ps4/r1.png";
        case Glyph::L2:       return "/app0/assets/ui/ps4/l2.png";
        case Glyph::R2:       return "/app0/assets/ui/ps4/r2.png";
        case Glyph::Options:  return "/app0/assets/ui/ps4/options.png";
        case Glyph::Touchpad: return "/app0/assets/ui/ps4/touchpad.png";
    }
    return "";
}

} // namespace shim
