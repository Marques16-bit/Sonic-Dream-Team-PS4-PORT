# Sonic Dream Team → PS4 (projeto de estudo)

Port experimental para PS4 (GoldHEN) com o OpenOrbis, usando **recompilação estática**.
Estado atual: o CI gera um `.pkg` instalável que sobe, lê o DualShock 4 e escreve log. O jogo ainda não roda.

## Como colocar este zip no repositório (só celular)

1. Crie o arquivo `.github/workflows/extract-zip.yml` no GitHub (Add file > Create new file, digite o caminho com as barras) e cole o conteúdo do arquivo `extract-zip.yml` que veio junto.
2. Suba o `sonic-ps4.zip` na raiz (Add file > Upload files) e faça commit.
3. Actions > **Extrair zip no repositório** > Run workflow.
4. Se você criar o segredo `GH_PAT` (token fine-grained só deste repositório, com *Contents* e *Workflows* de escrita), os workflows do zip também são extraídos. Sem ele, crie à mão `ps4-build.yml` e `analyze-binary.yml` (copie de dentro do zip).

## Workflows

| Workflow | Para quê |
|---|---|
| **PS4 Build (OpenOrbis)** | Compila, gera `eboot.bin` e `.pkg`; artefatos ficam na execução |
| **Analisar binário** | Etapa 0 da recompilação: mede o binário (precisa de release no repositório **privado**) |
| **Extrair zip no repositório** | Extrai este zip dentro do repositório |

## Pastas

- `src/` código do PS4 (shim, runtime de recompilação, `config.h` com 1080p / DS4 / qualidade máxima)
- `tools/` analisador de Mach-O ARM64
- `tests/` teste do runtime (roda no CI)
- `docs/ARQUITETURA.md` visão geral e gráficos · `docs/RECOMP.md` plano da recompilação

## Regras

- Mantenha o repositório **privado** e nunca versione arquivos do jogo (`.gitignore` já bloqueia os comuns).
- Este projeto não remove criptografia de binários.
