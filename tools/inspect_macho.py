#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Etapa 0 da recompilação estática: mede o tamanho do problema.

Lê um Mach-O ARM64 (iOS/tvOS) SEM dependências externas e gera:
  summary.json       números do binário (segmentos, funções, branches, imports)
  report.md          resumo legível
  imports.tsv        biblioteca <TAB> símbolo  (lista do que o shim precisa cobrir)
  functions.txt      endereço_hex tamanho      (início de cada função, via LC_FUNCTION_STARTS)
  objc_selectors.txt / objc_classes.txt
  il2cpp_symbols.txt

Se o binário ainda estiver criptografado (LC_ENCRYPTION_INFO com cryptid != 0), a
análise PARA: não há código legível. Este script não remove criptografia.

Uso:  python3 tools/inspect_macho.py CAMINHO_DO_BINARIO [--out analysis]
Códigos de saída: 0 ok | 2 criptografado | 3 não é Mach-O ARM64
"""
import argparse
import json
import mmap
import os
import struct
import sys
from array import array
from collections import Counter

FAT_MAGIC, FAT_MAGIC_64 = 0xCAFEBABE, 0xCAFEBABF
MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
FILETYPES = {1: "objeto", 2: "executável", 6: "dylib", 8: "bundle", 10: "dsym"}

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_ID_DYLIB = 0xD
LC_LOAD_DYLIB = 0xC
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001F
LC_LAZY_LOAD_DYLIB = 0x20
LC_LOAD_UPWARD_DYLIB = 0x80000023
LC_UUID = 0x1B
LC_FUNCTION_STARTS = 0x26
LC_ENCRYPTION_INFO_64 = 0x2C
LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_DYLD_INFO = 0x22
LC_DYLD_INFO_ONLY = 0x80000022
DYLIB_CMDS = {LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB,
              LC_LAZY_LOAD_DYLIB, LC_LOAD_UPWARD_DYLIB}

N_STAB, N_TYPE, N_EXT = 0xE0, 0x0E, 0x01
N_UNDF, N_SECT = 0x0, 0xE

CATEGORIES = [
    ("graficos",        ("Metal", "MetalKit", "QuartzCore", "OpenGLES", "CoreGraphics", "CoreImage")),
    ("entrada",         ("GameController", "CoreHaptics", "CoreMotion")),
    ("audio_video",     ("AVFoundation", "AVFAudio", "AudioToolbox", "CoreAudio", "CoreMedia",
                         "VideoToolbox", "MediaPlayer", "CoreVideo")),
    ("servicos_apple",  ("StoreKit", "GameKit", "AuthenticationServices", "AdSupport",
                         "AppTrackingTransparency", "CloudKit", "Security", "SystemConfiguration")),
    ("interface",       ("UIKit", "AppKit", "SwiftUI", "WebKit", "SafariServices")),
    ("fundacao",        ("Foundation", "CoreFoundation", "CFNetwork")),
    ("sistema",         ("libSystem", "libc++", "libobjc", "libz", "libresolv", "libicucore")),
]


def categorize(path):
    for cat, keys in CATEGORIES:
        if any(k in path for k in keys):
            return cat
    return "outros"


def uleb(buf, pos):
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, pos
        shift += 7


def cstr_at(mm, pos, limit):
    end = mm.find(b"\0", pos, limit)
    if end < 0:
        end = limit
    return mm[pos:end].decode("utf-8", "replace")


def pick_arm64_slice(mm):
    """Devolve o deslocamento do slice ARM64 (0 se não for fat)."""
    magic_be = struct.unpack_from(">I", mm, 0)[0]
    if magic_be in (FAT_MAGIC, FAT_MAGIC_64):
        n = struct.unpack_from(">I", mm, 4)[0]
        pos = 8
        for _ in range(n):
            if magic_be == FAT_MAGIC:
                cpu, _sub, off, _size, _al = struct.unpack_from(">IIIII", mm, pos)
                pos += 20
            else:
                cpu, _sub, off, _size, _al, _r = struct.unpack_from(">IIQQII", mm, pos)
                pos += 32
            if cpu == CPU_TYPE_ARM64:
                return off
        return None
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary")
    ap.add_argument("--out", default="analysis")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    f = open(args.binary, "rb")
    mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    file_size = len(mm)

    base = pick_arm64_slice(mm) if file_size >= 8 else None
    if base is None or struct.unpack_from("<I", mm, base)[0] != MH_MAGIC_64:
        print("Erro: não é um Mach-O ARM64 de 64 bits.", file=sys.stderr)
        return 3

    (_m, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, _res) = struct.unpack_from("<IiiIIIII", mm, base)
    if cputype != CPU_TYPE_ARM64:
        print("Erro: o arquivo não é ARM64.", file=sys.stderr)
        return 3

    summary = {
        "arquivo": os.path.basename(args.binary),
        "tamanho_bytes": file_size,
        "arquitetura": "arm64e" if (cpusub & 0xFF) == 2 else "arm64",
        "tipo": FILETYPES.get(filetype, str(filetype)),
        "criptografado": False,
        "chained_fixups": False,
    }

    # ---- Load commands ------------------------------------------------------
    segments, libs = [], []
    symtab = None
    fstarts = None
    uuid = None
    pos = base + 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", mm, pos)
        if cmd == LC_SEGMENT_64:
            (segname, vmaddr, vmsize, fileoff, filesize, _mp, _ip, nsects, _fl) = struct.unpack_from(
                "<16sQQQQiiII", mm, pos + 8)
            seg = {"nome": segname.rstrip(b"\0").decode(), "vmaddr": vmaddr, "vmsize": vmsize,
                   "fileoff": fileoff, "filesize": filesize, "secoes": []}
            sp = pos + 72
            for _s in range(nsects):
                (sname, _sg, addr, size, off, _al, _ro, _nr, _fl2, _r1, _r2, _r3) = struct.unpack_from(
                    "<16s16sQQIIIIIIII", mm, sp)
                seg["secoes"].append({"nome": sname.rstrip(b"\0").decode(), "addr": addr,
                                      "tamanho": size, "offset": off})
                sp += 80
            segments.append(seg)
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack_from("<IIII", mm, pos + 8)  # symoff, nsyms, stroff, strsize
        elif cmd in DYLIB_CMDS:
            name_off = struct.unpack_from("<I", mm, pos + 8)[0]
            libs.append(cstr_at(mm, pos + name_off, pos + cmdsize))
        elif cmd == LC_FUNCTION_STARTS:
            fstarts = struct.unpack_from("<II", mm, pos + 8)  # dataoff, datasize
        elif cmd == LC_ENCRYPTION_INFO_64:
            _co, _cs, cryptid = struct.unpack_from("<III", mm, pos + 8)
            if cryptid != 0:
                summary["criptografado"] = True
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            summary["chained_fixups"] = True
        elif cmd == LC_UUID:
            uuid = mm[pos + 8:pos + 24].hex()
        pos += cmdsize
    summary["uuid"] = uuid

    if summary["criptografado"]:
        summary["aviso"] = ("Binário criptografado: não há código legível. "
                            "A análise foi interrompida; este projeto não remove criptografia.")
        with open(os.path.join(args.out, "summary.json"), "w", encoding="utf-8") as fh:
            json.dump(summary, fh, indent=2, ensure_ascii=False)
        print("PARADO: o binário está criptografado (cryptid != 0). Veja docs/RECOMP.md.", file=sys.stderr)
        return 2

    summary["segmentos"] = [
        {"nome": s["nome"], "vmaddr": hex(s["vmaddr"]), "vmsize": s["vmsize"],
         "filesize": s["filesize"],
         "secoes": [{"nome": x["nome"], "addr": hex(x["addr"]), "tamanho": x["tamanho"]} for x in s["secoes"]]}
        for s in segments]
    summary["dylibs"] = libs

    def find_section(segname, secname):
        for s in segments:
            if s["nome"] == segname:
                for x in s["secoes"]:
                    if x["nome"] == secname:
                        return x
        return None

    # ---- Código (__TEXT,__text): contagem de instruções e branches ----------
    text = find_section("__TEXT", "__text")
    branches = Counter()
    if text and text["tamanho"] >= 4:
        words = array("I")
        words.frombytes(mm[base + text["offset"]: base + text["offset"] + (text["tamanho"] // 4) * 4])
        if sys.byteorder == "big":
            words.byteswap()
        for w in words:
            if (w >> 26) == 0b100101:
                branches["bl"] += 1
            elif (w >> 26) == 0b000101:
                branches["b"] += 1
            elif (w & 0xFFFFFC1F) == 0xD63F0000:
                branches["blr"] += 1
            elif (w & 0xFFFFFC1F) == 0xD61F0000:
                branches["br"] += 1
            elif w == 0xD65F03C0:
                branches["ret"] += 1
            elif (w & 0xFFE0001F) == 0xD4000001:
                branches["svc"] += 1
        summary["codigo"] = {"endereco": hex(text["addr"]), "bytes": text["tamanho"],
                             "instrucoes_aprox": text["tamanho"] // 4}
    summary["branches_aprox"] = dict(branches)

    # ---- Funções (LC_FUNCTION_STARTS) ---------------------------------------
    funcs = []
    if fstarts and fstarts[1] > 0:
        data = mm[base + fstarts[0]: base + fstarts[0] + fstarts[1]]
        text_seg = next((s for s in segments if s["nome"] == "__TEXT"), None)
        addr = text_seg["vmaddr"] if text_seg else 0
        p = 0
        while p < len(data):
            delta, p = uleb(data, p)
            if delta == 0:
                break
            addr += delta
            funcs.append(addr)
    sizes = []
    if funcs:
        text_end = (text["addr"] + text["tamanho"]) if text else funcs[-1] + 4
        for i, a in enumerate(funcs):
            end = funcs[i + 1] if i + 1 < len(funcs) else text_end
            sizes.append(max(end - a, 0))
        with open(os.path.join(args.out, "functions.txt"), "w") as fh:
            for a, s in zip(funcs, sizes):
                fh.write("%x %d\n" % (a, s))
        top = sorted(zip(funcs, sizes), key=lambda t: -t[1])[:25]
        summary["funcoes"] = {
            "quantidade": len(funcs),
            "tamanho_medio_bytes": round(sum(sizes) / len(sizes), 1),
            "maior_bytes": max(sizes),
            "mais_de_64KB": sum(1 for s in sizes if s > 65536),
            "maiores": [{"addr": hex(a), "bytes": s} for a, s in top],
        }
    else:
        summary["funcoes"] = {"quantidade": 0, "aviso": "sem LC_FUNCTION_STARTS: descoberta de funções exigirá análise de fluxo"}

    # ---- Símbolos: imports e il2cpp -----------------------------------------
    imports, il2cpp, defined_ext = [], [], 0
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        str_limit = base + stroff + strsize
        for i in range(nsyms):
            n_strx, n_type, n_sect, n_desc, _val = struct.unpack_from("<IBBHQ", mm, base + symoff + i * 16)
            if n_type & N_STAB:
                continue
            name = cstr_at(mm, base + stroff + n_strx, str_limit) if n_strx else ""
            if "il2cpp" in name:
                il2cpp.append(name)
            if (n_type & N_TYPE) == N_UNDF and (n_type & N_EXT) and n_sect == 0:
                ordinal = (n_desc >> 8) & 0xFF
                if 1 <= ordinal <= len(libs):
                    lib = libs[ordinal - 1]
                elif ordinal == 0xFE:
                    lib = "(dynamic lookup)"
                else:
                    lib = "(desconhecida)"
                imports.append((lib, name))
            elif (n_type & N_TYPE) == N_SECT and (n_type & N_EXT):
                defined_ext += 1
    with open(os.path.join(args.out, "imports.tsv"), "w", encoding="utf-8") as fh:
        for lib, name in sorted(imports):
            fh.write("%s\t%s\n" % (lib, name))
    with open(os.path.join(args.out, "il2cpp_symbols.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(sorted(set(il2cpp))) + ("\n" if il2cpp else ""))

    by_lib = Counter(lib for lib, _ in imports)
    by_cat = Counter(categorize(lib) for lib, _ in imports)
    summary["imports"] = {"total": len(imports), "exports_definidos": defined_ext,
                          "por_biblioteca": dict(by_lib.most_common()), "por_categoria": dict(by_cat)}

    # ---- Obj-C: seletores e classes ----------------------------------------
    def strings_of(secname):
        sec = find_section("__TEXT", secname)
        if not sec:
            return []
        raw = mm[base + sec["offset"]: base + sec["offset"] + sec["tamanho"]]
        return [s.decode("utf-8", "replace") for s in raw.split(b"\0") if s]

    selectors, classes = strings_of("__objc_methname"), strings_of("__objc_classname")
    with open(os.path.join(args.out, "objc_selectors.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(sorted(set(selectors))) + ("\n" if selectors else ""))
    with open(os.path.join(args.out, "objc_classes.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(sorted(set(classes))) + ("\n" if classes else ""))
    summary["objc"] = {"seletores": len(set(selectors)), "classes": len(set(classes))}

    summary["unity"] = {
        "il2cpp_detectado": bool(il2cpp),
        "simbolos_il2cpp": len(set(il2cpp)),
        "UnityAppController": "UnityAppController" in classes,
    }

    with open(os.path.join(args.out, "summary.json"), "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2, ensure_ascii=False)
    write_report(args.out, summary)
    print(open(os.path.join(args.out, "report.md"), encoding="utf-8").read())
    return 0


def write_report(out, s):
    L = []
    L.append("# Relatório da análise: %s\n" % s["arquivo"])
    L.append("- Arquitetura: %s | tipo: %s | tamanho: %.1f MB" % (s["arquitetura"], s["tipo"], s["tamanho_bytes"] / 1e6))
    L.append("- Chained fixups: %s (os ponteiros em __DATA precisarão ser decodificados na carga)" %
             ("sim" if s["chained_fixups"] else "não"))
    u = s["unity"]
    L.append("- Unity/IL2CPP: %s (%d símbolos il2cpp)" % ("detectado" if u["il2cpp_detectado"] else "não detectado",
                                                       u["simbolos_il2cpp"]))
    if "codigo" in s:
        c = s["codigo"]
        L.append("\n## Código\n")
        L.append("- __text: %.1f MB, ~%.1f milhões de instruções (ARM64 tem 4 bytes/instrução; valor aproximado)" %
                 (c["bytes"] / 1e6, c["instrucoes_aprox"] / 1e6))
    f = s["funcoes"]
    if f.get("quantidade"):
        L.append("- Funções: %d (média %.0f bytes; maior %d bytes; %d com mais de 64 KB)" %
                 (f["quantidade"], f["tamanho_medio_bytes"], f["maior_bytes"], f["mais_de_64KB"]))
    b = s.get("branches_aprox", {})
    if b:
        L.append("- Desvios (aprox.): bl=%d b=%d ret=%d | **blr=%d br=%d** (indiretos: precisam de tabela de endereços) | "
                 "**svc=%d** (syscall crua: tem de ser tratada à mão)" %
                 (b.get("bl", 0), b.get("b", 0), b.get("ret", 0), b.get("blr", 0), b.get("br", 0), b.get("svc", 0)))
    i = s["imports"]
    L.append("\n## Imports (o que o shim precisa cobrir)\n")
    L.append("- Total de símbolos importados: %d" % i["total"])
    for cat, n in sorted(i["por_categoria"].items(), key=lambda t: -t[1]):
        L.append("  - %s: %d" % (cat, n))
    L.append("\nPrincipais bibliotecas:\n")
    for lib, n in list(i["por_biblioteca"].items())[:15]:
        L.append("  - %s: %d" % (lib, n))
    L.append("\n## Objective-C\n")
    L.append("- Seletores únicos: %d | classes: %d" % (s["objc"]["seletores"], s["objc"]["classes"]))
    L.append("\nArquivos completos: imports.tsv, objc_selectors.txt, objc_classes.txt, functions.txt, summary.json")
    with open(os.path.join(out, "report.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(L) + "\n")


if __name__ == "__main__":
    sys.exit(main())
