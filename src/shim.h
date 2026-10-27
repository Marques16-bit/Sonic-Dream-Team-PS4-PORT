#pragma once
// ---------------------------------------------------------------------------
// Camada de compatibilidade (shim) entre o "mundo iOS/Unity" e o PS4.
//
// Ideia: o código do jogo (ou a sua reimplementação) nunca chama Apple/Google
// APIs diretamente. Ele chama nomes conhecidos (mach_absolute_time,
// GCController, MTLDevice...). Cada nome é resolvido AQUI para uma função
// do PS4 (libkernel, libScePad, libSceVideoOut). Se ainda não existe, o
// resolvedor devolve nullptr e escreve "UNRESOLVED: <nome>" no log, assim você
// descobre exatamente o que falta implementar rodando o jogo.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <orbis/Pad.h>

namespace shim {

// ---- Resolvedor de símbolos ------------------------------------------------
struct Entry {
    const char* name;   // nome do símbolo original (iOS/Unity/libc)
    void*       impl;   // implementação no PS4
};

// Devolve a implementação do PS4 para um símbolo, ou nullptr (e loga) se não houver.
void* resolve(const char* symbol);

// Quantos símbolos já tem implementação real (para acompanhar progresso).
int implemented_count();

// ---- Tempo (substitui mach_absolute_time / CACurrentMediaTime) -------------
uint64_t now_ns();

// ---- Entrada (substitui GCController / GCExtendedGamepad) ------------------
struct Gamepad {
    // Layout "extended gamepad" estilo GameController.framework, valores 0..1.
    bool  buttonA, buttonB, buttonX, buttonY;       // Cross, Circle, Square, Triangle
    bool  leftShoulder, rightShoulder;              // L1, R1
    float leftTrigger, rightTrigger;                // L2, R2 (analógico)
    bool  dpadUp, dpadDown, dpadLeft, dpadRight;
    bool  buttonMenu, buttonOptions;                // Options, Touchpad
    bool  leftThumbstickButton, rightThumbstickButton;
    float leftX, leftY, rightX, rightY;             // -1..1 (Y positivo = cima)
    bool  connected;
};

// Converte o estado cru do DualShock 4 para o layout acima.
Gamepad translate_pad(const OrbisPadData& in);

// Glifos de botão: com kForceDualShockGlyphs, sempre os do PlayStation.
enum class Glyph { Cross, Circle, Square, Triangle, L1, R1, L2, R2, Options, Touchpad };
const char* glyph_asset(Glyph g);

} // namespace shim
