#!/usr/bin/env python3
"""Disassemble only known Soul Knight TLS-verification methods in ARM64 libil2cpp.so.

Usage: py soulknight_tls_arm64_disasm.py PATH_TO_libIl2cpp.so [PATH_TO_dump.cs]
Requires: py -m pip install capstone
Does not modify the game binary or upload it anywhere.
"""
from __future__ import annotations
import re
import struct
import sys
from pathlib import Path

TARGETS = [
    ("RPCClient.InstallGlobalStrictChillyroomCertificateValidation", 0x991EEBC, 0x84),
    ("RPCClient.GetStrictChillyroomCertificateValidationCallback", 0x991EF40, 0xD0),
    ("RPCClient.CheckErrorCodes", 0x991F9F8, 0x2A0),
    ("RPCClient.CheckCertHashTrusted", 0x991FC98, 0x700),
    ("RPCClient.cctor", 0x991F32C, 0x600),
    ("RPCClient.callback_10_0", 0x99225B8, 0xCC),
    ("SslUtil.SetSslCallback", 0x7D14914, 0x3C),
    ("SslUtil.GetStrictChillyroomCertificateValidationCallback", 0x7D14950, 0x10C),
    ("SslUtil.CheckErrorCodes", 0x7D16B00, 0x2CC),
    ("SslUtil.CheckCertHashTrusted", 0x7D16DCC, 0x304),
    ("SslUtil.cctor", 0x7D167F8, 0x308),
    ("SslUtil.callback_3_0", 0x7D17140, 0x2D8),
]

def load_symbols(dump_path):
    symbols = {rva: name for name, rva, _ in TARGETS}
    if not dump_path or not dump_path.is_file():
        return symbols
    pending = None
    with dump_path.open("r", encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            match = re.match(r"\s*// RVA: 0x([0-9a-fA-F]+)\b", line)
            if match:
                pending = int(match.group(1), 16)
            elif pending is not None and line.strip() and not line.lstrip().startswith("//"):
                symbols.setdefault(pending, line.strip().replace("{ }", "").strip())
                pending = None
    return symbols

def read_segments(file):
    file.seek(0)
    ehdr = file.read(64)
    if ehdr[:4] != b"\x7fELF" or ehdr[4] != 2 or ehdr[5] != 1:
        raise ValueError("Expected a 64-bit little-endian ELF (AArch64 libil2cpp.so)")
    machine = struct.unpack_from("<H", ehdr, 18)[0]
    if machine != 183:
        raise ValueError(f"Expected AArch64 ELF (e_machine=183), got {machine}")
    phoff = struct.unpack_from("<Q", ehdr, 32)[0]
    entsize = struct.unpack_from("<H", ehdr, 54)[0]
    count = struct.unpack_from("<H", ehdr, 56)[0]
    if entsize < 56 or count > 1024:
        raise ValueError("Invalid ELF program-header table")
    segments = []
    for i in range(count):
        file.seek(phoff + i * entsize)
        entry = file.read(56)
        if len(entry) != 56:
            raise ValueError("Truncated ELF program header")
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack("<IIQQQQQQ", entry)
        if p_type == 1:
            segments.append((p_vaddr, p_filesz, p_offset, p_flags))
    if not segments:
        raise ValueError("No PT_LOAD segments")
    return segments

def rva_to_file_offset(rva, segments):
    # ELF virtual addresses can differ from offsets; do NOT seek to RVA directly.
    for virtual, size, offset, flags in segments:
        if virtual <= rva < virtual + size:
            return offset + (rva - virtual)
    raise ValueError(f"RVA 0x{rva:x} is outside file-backed PT_LOAD segments")

def main():
    if len(sys.argv) < 2:
        print("Drag libil2cpp.so onto SoulKnight-TLS-disasm.cmd", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1])
    if not binary.is_file():
        print(f"File not found: {binary}", file=sys.stderr)
        return 2
    dump = Path(sys.argv[2]) if len(sys.argv) >= 3 else binary.parent / "dump.cs"

    try:
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    except ImportError:
        print("Capstone missing. Install with: py -m pip install capstone", file=sys.stderr)
        return 2

    symbols = load_symbols(dump)
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    output = Path(__file__).with_name("SoulKnight-TLS-disasm.txt")
    count = 0
    with binary.open("rb") as f, output.open("w", encoding="utf-8") as out:
        segments = read_segments(f)
        out.write(f"Source: {binary.name}, size: {binary.stat().st_size}\n")
        out.write("AArch64 ELF PT_LOAD RVA-to-file-offset mapping\n")
        for va, size, off, flags in segments:
            out.write(f"  VA 0x{va:x}..0x{va + size:x}, file 0x{off:x}, flags {flags}\n")
        out.write(f"dump.cs symbol annotations: {'enabled' if dump.is_file() else 'not available'}\n\n")
        for name, rva, length in TARGETS:
            try:
                off = rva_to_file_offset(rva, segments)
                f.seek(off)
                code = f.read(length)
                out.write("=" * 86 + "\n")
                out.write(f"{name}\n  RVA: 0x{rva:x}  ELF file offset: 0x{off:x}  bytes: {len(code)}\n")
                for insn in decoder.disasm(code, rva):
                    annotation = ""
                    if insn.mnemonic in ("bl", "b", "blr", "cbz", "cbnz", "tbz", "tbnz"):
                        matches = re.findall(r"0x[0-9a-fA-F]+", insn.op_str)
                        if matches:
                            target = int(matches[-1], 16)
                            if target in symbols:
                                annotation = "  ; " + symbols[target][:180]
                    out.write(
                        f"  0x{insn.address:09x}: {insn.bytes.hex(' '):<12} "
                        f"{insn.mnemonic:<9} {insn.op_str}{annotation}\n"
                    )
                    count += 1
                out.write("\n")
            except Exception as exc:
                out.write(f"  ERROR: {exc}\n\n")
    print(f"Saved: {output} ({count} ARM64 instructions)")
    print("Share this text file for analysis; libil2cpp.so stays on your PC.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
