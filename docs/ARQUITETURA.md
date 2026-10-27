# Port de Sonic Dream Team para PS4 (GoldHEN / OpenOrbis) — Arquitetura

Status: bootstrap. O CI já gera `eboot.bin` e `.pkg`; o jogo em si ainda não roda. O plano da recompilação estática está em `docs/RECOMP.md`.

---

## 1. Ponto de partida realista

- O jogo é feito em **Unity** (URP) pela Hardlight/Sega, e no iOS roda sobre **Metal**.
- Em Unity + IL2CPP, o código C# vira C++ e depois **ARM64 nativo**. O que existe no iOS é um binário Mach-O ARM64 mais um arquivo de metadados (`global-metadata.dat`).
- O PS4 é **x86_64 / FreeBSD**. Código ARM64 não executa nele. Por isso "interceptar syscalls" sozinho **não resolve**: antes de pensar em chamadas de sistema, é preciso decidir o que fazer com a CPU.
- O Unity só tem runtime de PS4 dentro do SDK oficial licenciado da Sony. Não dá para "recompilar o projeto Unity para PS4" com homebrew.

### Três caminhos para a CPU

| Caminho | Como funciona | Prós | Contras |
|---|---|---|---|
| **A. Traduzir ARM64 → x86_64** (JIT/emulador) | Carrega o Mach-O, traduz instruções em tempo de execução | Reaproveita o binário original | Os 8 núcleos Jaguar do PS4 são fracos; o jogo já roda justo em celular (a Hardlight cita CPU de 16 ms/frame). Overhead de emulação tende a derrubar o fps. Precisa reimplementar Obj-C, libSystem e Metal por baixo |
| **B. Recompilação estática** | Converte o ARM64 em C/x86 antes | Sem custo de JIT | Binário IL2CPP enorme; ferramenta própria; muito frágil |
| **C. Reimplementar o runtime** | Usa os metadados IL2CPP para entender classes/lógica e reescreve o jogo num runtime próprio em x86_64, reaproveitando os assets | Performance previsível, controle total (1080p, entrada, UI) | É o caminho mais trabalhoso (meses/anos de equipe) |

Recomendação: validar a plataforma com a base deste repositório (entrada, vídeo, áudio, empacotamento) e só então decidir entre A e C com medições reais no console.

> Se o objetivo final for apenas **jogar com DualShock 4 em 1080p na TV**, o jogo já aceita DS4 no iOS/iPadOS/tvOS/macOS. Pareie o controle e use saída de vídeo do aparelho. O port só se justifica como projeto de engenharia.

---

## 2. Estrutura do shim (camadas)

O código do jogo nunca chama API da Apple/Google direto. Ele pede um **nome**; o shim devolve a implementação do PS4. Hoje isso está em `src/shim.cpp` (`shim::resolve`).

| Camada | Original (iOS) | Destino no PS4 |
|---|---|---|
| Loader | Mach-O, relocações, binds | Loader próprio: mapeia segmentos e resolve imports via `shim::resolve` |
| CPU | ARM64 | Tradução (A/B) ou código nativo (C) |
| Sistema | libSystem: `mach_absolute_time`, `pthread`, `mmap`, `dispatch` (GCD) | `libkernel` (`sceKernelGetProcessTime`, pthreads da musl), GCD vira pool de threads |
| Runtime Obj-C | `objc_msgSend`, classes Foundation | Tabela (classe, seletor) → função C++ |
| Entrada | `GCController` / `GCExtendedGamepad` | `libScePad` (já em `shim::translate_pad`) |
| Áudio | CoreAudio / AVAudioEngine | `libSceAudioOut` |
| Arquivos | sandbox iOS (`Documents`, bundle) | `/app0` (somente leitura, conteúdo do pacote) e `/data/<TITLE_ID>` (saves) |
| Serviços Apple | Game Center, StoreKit, Apple Arcade | Stubs que retornam "ok/offline" |
| Vídeo (cutscenes) | AVPlayer | `libSceAvPlayer` ou removido |
| Gráficos | Metal | Ver seção 3 |

### Como interceptar (três níveis)

1. **Na ligação (bind-time):** cada símbolo importado passa por `shim::resolve(nome)`. Se não existir, ele registra `[shim] UNRESOLVED: nome` no log e devolve `nullptr`. Rodar o jogo e ler esse log gera a lista exata do que implementar, em ordem de uso.
2. **Chamadas Obj-C:** `objc_msgSend` vira uma busca em tabela `(classe, seletor)`. Mesma regra: o que falta aparece no log.
3. **Syscalls cruas:** apps iOS normalmente chamam os wrappers da libSystem, não `svc #0x80` direto. Intercepte no wrapper. Números de syscall do XNU (iOS) e do FreeBSD (PS4) são diferentes, então nunca repasse número cru. Mach traps (`mach_msg` etc.) também são emulados no nível do wrapper.

---

## 3. Gráficos: principais gargalos

Contexto do OpenOrbis (v0.5.4, conferido no próprio SDK):

- O suporte a GPU nativa (**GNM**) ainda está no roadmap (v0.7, "Finalize GPU rendering support"). Existe `GnmDriver.h`, mas não há um caminho pronto e estável.
- O que existe pronto é o **Piglet**: implementação de **OpenGL ES 2.0 + EGL 1.4** do próprio sistema (sample `samples/piglet`).

| Gargalo | Por quê | Caminho |
|---|---|---|
| **Shaders** | Metal usa MSL/AIR; o GCN do PS4 (GCN 1.1) precisa de ISA própria ou de GLSL/PSSL aceito pelo Piglet. Não há compilador homebrew estável Metal → GCN | Piglet: GLSL ES 2.0 reescrito/traduzido. GNM: pipeline próprio (shader → LLVM AMDGPU) — é o maior risco do projeto |
| **Nível da API** | URP moderno assume recursos de ES 3.0+/Metal (SRP Batcher, MSAA, vários render targets, compute). Piglet é só ES 2.0 | Renderer próprio com recursos limitados no início; subir para GNM depois |
| **Texturas** | iOS usa **ASTC**; a GPU do PS4 não suporta ASTC (só famílias BCn/DXT) | Transcodificar ASTC → BC7/BC3 na instalação ou no carregamento (custa tempo e memória) |
| **Modelo de GPU** | Apple GPU é *tile-based deferred*; GCN é *immediate mode*. Passes que dependem de memória de tile ficam caros | Reorganizar passes de pós-processamento; evitar load/store de framebuffer extra |
| **Estado e sincronização** | Metal tem command buffers, heaps e fences de alto nível; GNM exige montar pacotes PM4 e gerenciar cache/flush manualmente | Camada `gfx::` própria que esconde isso |
| **Memória** | iOS usa memória unificada; PS4 tem regiões diretas/flexíveis com alinhamento rígido | Alocador próprio |
| **CPU (draw calls)** | Jaguar a 1,6 GHz é o gargalo real em jogos Unity | Batching agressivo; mais um motivo para o caminho C |

### Plano gráfico em fases

1. **Fase 1 (Piglet/GLES2):** backend `gfx::` mínimo. Limpar a tela, desenhar um triângulo texturizado em 1920×1080 e confirmar o fluxo de vídeo.
2. **Fase 2:** sprites/UI do jogo e os shaders mais simples; transcodificação ASTC → BCn.
3. **Fase 3 (GNM):** substituir o backend por GNM quando o suporte do OpenOrbis amadurecer, ou usar um driver próprio.

---

## 4. Suas metas e onde elas entram

Todas ficam em `src/config.h`, num só lugar:

| Meta | Onde / como |
|---|---|
| Render interno 1080p | `kRenderWidth/kRenderHeight = 1920×1080`; buffers do `libSceVideoOut` e render targets com esse tamanho; `kRenderScale = 1.0` |
| Só controle PS4 | Entrada só por `libScePad`; `kForceDualShockGlyphs = true` e `shim::glyph_asset()` sempre devolve glifos PlayStation. O iOS já escolhe os ícones de DualShock quando o controle é um DS4, então esses assets existem no jogo |
| Gráficos no máximo | `kQuality = Max`: aplicado na carga, ignorando o que o jogo tiver salvo |
| Sem opção de vídeo | `kHideVideoOptions = true`: o menu não mostra resolução/qualidade/fps e o valor vem sempre da config |

---

## 5. Arquivos do jogo e repositório

- **Nunca versione** binário, texturas, áudio ou bundles do jogo. O `.gitignore` já bloqueia `assets/`, `.ipa`, `.apk`, `.obb`, `global-metadata.dat`.
- Use apenas arquivos da sua própria cópia legítima do jogo. Se o `.pkg` passar a conter material do jogo, mantenha o repositório **privado** e não anexe o pacote em releases públicas.

## 6. Marcos

| Marco | Resultado |
|---|---|
| M0 (feito) | CI gera `eboot.bin` e `.pkg` com C++ x86_64 |
| M1 | Instalar o `.pkg` no console (GoldHEN) e ver o log de boot |
| M2 | Ler o DualShock 4 no console e confirmar o mapeamento |
| M3 | Saída 1080p com um quadro desenhado (Piglet) |
| M4 | Áudio (`libSceAudioOut`) e sistema de arquivos |
| M5 | Decisão A vs C com medições de CPU/GPU no console |
