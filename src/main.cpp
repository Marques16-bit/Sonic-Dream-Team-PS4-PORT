// Ponto de entrada do port (PS4 / OpenOrbis).
// Fase atual: bootstrap. Sobe o ambiente, lê o DualShock 4 pelo shim e mostra
// o estado no log. O jogo em si entra depois, por cima desta base.

#include <stdio.h>
#include <string.h>
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include "config.h"
#include "shim.h"

static void log_boot() {
    printf("[boot] Sonic Dream Team PS4 port (bootstrap)\n");
    printf("[boot] render %dx%d, scale %.2f, fps %d\n",
           cfg::kRenderWidth, cfg::kRenderHeight, cfg::kRenderScale, cfg::kTargetFps);
    printf("[boot] qualidade=MAX glifos=DS4 video_opts_ocultas=%d\n",
           cfg::kHideVideoOptions ? 1 : 0);
    printf("[boot] simbolos com implementacao: %d\n", shim::implemented_count());
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    log_boot();

    // Usuário inicial do console + biblioteca de controle.
    OrbisUserServiceInitializeParams up;
    memset(&up, 0, sizeof(up));
    up.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&up);

    int32_t userId = 0;
    sceUserServiceGetInitialUser(&userId);

    if (scePadInit() != 0) {
        printf("[pad] falha em scePadInit\n");
        for (;;) sceKernelUsleep(1000000);
    }
    int32_t pad = scePadOpen(userId, 0, 0, NULL);
    if (pad < 0) {
        printf("[pad] falha ao abrir o controle (0x%08x)\n", (unsigned)pad);
        for (;;) sceKernelUsleep(1000000);
    }
    printf("[pad] DualShock 4 aberto (handle %d)\n", pad);

    // Loop principal a 60 Hz: aqui entrará simulação + render do jogo.
    const uint32_t frameUs = 1000000u / (uint32_t)cfg::kTargetFps;
    uint32_t lastButtons = 0xFFFFFFFFu;

    for (;;) {
        OrbisPadData raw;
        memset(&raw, 0, sizeof(raw));
        if (scePadReadState(pad, &raw) == 0) {
            shim::Gamepad g = shim::translate_pad(raw);
            if (raw.buttons != lastButtons) {
                lastButtons = raw.buttons;
                printf("[pad] A=%d B=%d X=%d Y=%d L=(%.2f,%.2f) R=(%.2f,%.2f)\n",
                       g.buttonA, g.buttonB, g.buttonX, g.buttonY,
                       g.leftX, g.leftY, g.rightX, g.rightY);
            }
        }
        sceKernelUsleep(frameUs);
    }
    return 0;
}
