# ---------------------------------------------------------------------------
# Build do port para PS4 com o OpenOrbis Toolchain.
# Requer a variável OO_PS4_TOOLCHAIN apontando para a raiz do SDK.
#
#   make eboot   -> compila e gera eboot.bin (fake SELF, roda com GoldHEN)
#   make gp4     -> eboot + param.sfo + pkg.gp4 (projeto do pacote)
#   make pkg     -> gp4 + .pkg final (precisa de libssl1.1; no CI é feito em contêiner)
#   make clean
# ---------------------------------------------------------------------------

# ---- Metadados do pacote ---------------------------------------------------
TITLE       := Sonic Dream Team PS4 (WIP)
VERSION     := 0.01
TITLE_ID    := SDTP00001
# Formato: IV0000-<TITLE_ID>_00-<16 caracteres>
CONTENT_ID  := IV0000-SDTP00001_00-SONICPORT0000000

# ---- Bibliotecas do PS4 usadas ---------------------------------------------
LIBS        := -lc -lkernel -lc++ -lSceVideoOut -lScePad -lSceUserService

# ---- Pastas ----------------------------------------------------------------
SRCDIR      := src
BUILD       := build
OBJDIR      := $(BUILD)/obj
ELF         := $(BUILD)/sonic_ps4.elf
OELF        := $(BUILD)/sonic_ps4.oelf

# Arquivos do jogo (texturas, áudio...). NÃO versionar: ver .gitignore.
ASSETS      := $(shell find assets -type f 2>/dev/null)

# ---- Toolchain -------------------------------------------------------------
ifndef OO_PS4_TOOLCHAIN
$(error Defina OO_PS4_TOOLCHAIN com o caminho do OpenOrbis SDK)
endif
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
# O make já define CC/CXX/LD padrão (cc/g++/ld); aqui forçamos clang/lld,
# mas ainda dá para sobrescrever na linha de comando (ex.: CXX=clang++-18).
ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin CXX),default)
CXX := clang++
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif
BIN         := $(TOOLCHAIN)/bin/linux
PKGTOOL     := $(BIN)/PkgTool.Core

# Necessário para o PkgTool.Core (.NET) em máquinas sem ICU instalado.
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT := 1

# ---- Flags -----------------------------------------------------------------
TARGET_FLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CFLAGS      := $(TARGET_FLAGS) -I$(SRCDIR) -O2 -Wall -D__PS4__ -c
CXXFLAGS    := $(TARGET_FLAGS) -I$(SRCDIR) -isystem $(TOOLCHAIN)/include/c++/v1 -std=gnu++17 -O2 -Wall -D__PS4__ -c
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

# Fontes: tudo em src/ (recursivo) + gen/ (C++ gerado pelo recompilador, se existir).
GENDIR      := gen
CFILES      := $(shell find $(SRCDIR) $(GENDIR) -name '*.c' 2>/dev/null)
CPPFILES    := $(shell find $(SRCDIR) $(GENDIR) -name '*.cpp' 2>/dev/null)
HEADERS     := $(shell find $(SRCDIR) $(GENDIR) -name '*.h' -o -name '*.hpp' 2>/dev/null)
OBJS        := $(patsubst %.c,$(OBJDIR)/%.o,$(CFILES)) $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPPFILES))

# Arquivos de sistema que o pacote precisa (vêm do SDK se você não tiver os seus).
SDK_SAMPLE  := $(TOOLCHAIN)/samples/hello_world
PKG_FILES   := eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png \
               sce_module/libSceFios2.prx sce_module/libc.prx $(ASSETS)

.PHONY: all eboot gp4 pkg clean
all: pkg
eboot: eboot.bin
gp4: pkg.gp4
pkg: $(CONTENT_ID).pkg

# ---- Compilação ------------------------------------------------------------
$(OBJDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(ELF): $(OBJS)
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

# ELF -> eboot.bin assinado (fake SELF aceito pelo GoldHEN)
eboot.bin: $(ELF)
	$(BIN)/create-fself -in=$(ELF) -out=$(OELF) --eboot "eboot.bin" --paid 0x3800000000000011

# ---- Arquivos de sistema padrão (só copia se não existirem no seu repo) ----
sce_sys/icon0.png:
	mkdir -p sce_sys
	cp $(SDK_SAMPLE)/sce_sys/icon0.png $@

sce_sys/about/right.sprx:
	mkdir -p sce_sys/about
	cp $(SDK_SAMPLE)/sce_sys/about/right.sprx $@

sce_module/libSceFios2.prx sce_module/libc.prx:
	mkdir -p sce_module
	cp $(SDK_SAMPLE)/$@ $@

# ---- param.sfo, gp4 e pkg --------------------------------------------------
sce_sys/param.sfo: Makefile
	mkdir -p sce_sys
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

pkg.gp4: $(PKG_FILES)
	$(BIN)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$(PKG_FILES)"

$(CONTENT_ID).pkg: pkg.gp4
	$(PKGTOOL) pkg_build $< .

clean:
	rm -rf $(BUILD) eboot.bin pkg.gp4 sce_sys/param.sfo $(CONTENT_ID).pkg
