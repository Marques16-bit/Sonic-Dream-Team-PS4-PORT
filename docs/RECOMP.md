# Recompilação estática: plano e estado

Recompilação estática = converter, **antes** de rodar, cada função ARM64 do jogo em C++, e compilar esse C++ para x86_64 junto com um runtime. É a técnica usada em projetos como N64Recomp e XenonRecomp. Aqui o alvo é um binário Mach-O ARM64 de Unity/IL2CPP, que é bem maior e mais difícil do que um jogo de N64.

## O que já existe neste repositório

| Peça | Arquivo | Estado |
|---|---|---|
| Analisador de Mach-O ARM64 | `tools/inspect_macho.py` | Pronto e testado com um binário de exemplo |
| Workflow que roda o analisador | `.github/workflows/analyze-binary.yml` | Escrito; **não rodei no GitHub** |
| Runtime (memória guest, tabelas de funções e imports, chamada indireta) | `src/recomp/` | Pronto, com teste de unidade (`tests/recomp_test.cpp`) |
| Build PS4 que já compila `gen/` se existir | `Makefile` | Pronto |
| **Gerador ARM64 → C++ (lifter)** | — | **Não existe ainda** |
| **Loader do Mach-O (rebase/bind/chained fixups)** | — | **Não existe ainda** |
| Shim Obj-C / libSystem / Metal | `src/shim.*` (só entrada e tempo) | Mínimo |

Ou seja: a base para medir e para rodar código recompilado está pronta, mas o gerador de C++ é a próxima peça grande.

## Etapa 0: medir (faça primeiro)

1. Crie uma release no seu repositório **privado** e anexe o binário do jogo.
2. Rode "Analisar binário" no Actions.
3. Leia o relatório no resumo da execução. Os números que decidem se o projeto é viável:
   - **Instruções em `__text`**: define o tamanho do C++ gerado (cada instrução vira várias linhas).
   - **`blr`/`br` (desvios indiretos)**: cada um precisa de uma tabela de destinos; IL2CPP usa muitos (métodos virtuais).
   - **`svc` (syscall crua)**: raro, mas cada ocorrência é tratada à mão.
   - **Imports por categoria**: o que o shim precisa cobrir (gráficos, entrada, áudio, serviços Apple).
   - **Chained fixups**: indica o formato de ponteiros em `__DATA` que o loader precisa decodificar.

## Binário criptografado

Binários baixados da App Store/Apple Arcade vêm criptografados (FairPlay). O analisador detecta isso (`cryptid != 0`) e para. **Este projeto não remove criptografia e eu não ajudo com isso.** Se o seu binário estiver criptografado, a análise não tem como continuar a partir dele.

## Pipeline completo

```
binário Mach-O ARM64 ──► [1 loader] ──► mapa de memória + imports
                  │
                  └────► [2 lifter] ──► gen/*.cpp (uma função C++ por função ARM64)
                                           │
 src/recomp (runtime) + src/shim (PS4) ────┴──► clang (x86_64 PS4) ──► eboot.bin ──► .pkg
```

1. **Loader** (`tools/` ou `src/`): lê segmentos, aplica rebase/bind, grava `__DATA` no espaço guest, registra imports com `recomp::register_import`.
2. **Lifter**: decodifica ARM64 (por exemplo com Capstone ou o desassemblador do LLVM) e emite uma função C++ por função, operando sobre `recomp::Context`. `bl` vira chamada C++; `blr`/`br` viram `recomp::call_indirect`; `ret` vira `return`.
3. **Runtime**: semântica que o C++ gerado precisa: flags NZCV, NEON/FP, instruções atômicas e exclusivas (`ldxr`/`stxr`), TLS (`tpidr`), `setjmp`/`longjmp`, tratamento de exceções C++ (IL2CPP usa).
4. **Shim**: Obj-C (`objc_msgSend` por tabela classe/seletor), libSystem, GCD, Foundation, e por fim Metal.
5. **Integração**: o `Makefile` já compila tudo que estiver em `gen/` e linka no `eboot.bin`.

## Riscos reais

- **Volume de código**: o C++ gerado pode ficar enorme. Será preciso dividir em muitos arquivos, compilar em paralelo e usar cache. O GitHub Actions tem limite de 6 h por job, disco e memória limitados; talvez seja preciso gerar e compilar em partes (ou usar runner próprio).
- **Desempenho**: código recompilado é bem mais rápido que emulado, mas ainda carrega o custo do `Context` e da memória guest; o Jaguar do PS4 é fraco.
- **Exceções e desvios indiretos**: são os pontos que mais quebram recompiladores.
- **Gráficos**: a recompilação resolve a **CPU**. O Metal continua sem equivalente pronto no PS4 (ver `docs/ARQUITETURA.md`, seção 3). Sem isso o jogo roda "no escuro".
- **Sem testes no console**: só dá para confirmar o que funciona instalando o `.pkg` num PS4 com GoldHEN.

## Ordem sugerida

| Marco | Resultado |
|---|---|
| R0 | Relatório da etapa 0 com os números do binário |
| R1 | Loader mapeando o binário e listando imports ainda sem shim |
| R2 | Lifter gerando C++ de **uma** função simples, validado contra o resultado esperado |
| R3 | Lifter para todas as funções, programa compilando (mesmo sem rodar o jogo inteiro) |
| R4 | Shim mínimo: inicialização do Unity até a primeira tela |
| R5 | Gráficos |
