#!/usr/bin/env python3
"""Audit an arm64 module's undefined symbols and modversions against a target."""

import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile


def load_target_symvers(path: Path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        crc, name, *_ = line.split("\t")
        result[name] = int(crc, 16)
    return result


def load_module(path: Path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        if not elf.little_endian or elf.elfclass != 64 or elf["e_machine"] != "EM_AARCH64":
            raise ValueError("module is not little-endian ELF64 arm64")

        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise ValueError("module has no .symtab")
        undefined = {
            symbol.name
            for symbol in symtab.iter_symbols()
            if symbol["st_shndx"] == "SHN_UNDEF" and symbol.name
        }

        versions_section = elf.get_section_by_name("__versions")
        if versions_section is None:
            raise ValueError("module has no __versions section")
        data = versions_section.data()
        if len(data) % 64:
            raise ValueError("unexpected __versions entry size")
        versions = {}
        for offset in range(0, len(data), 64):
            crc = struct.unpack_from("<Q", data, offset)[0] & 0xFFFFFFFF
            name = data[offset + 8 : offset + 64].split(b"\0", 1)[0].decode("ascii")
            versions[name] = crc
        return undefined, versions


def load_vmlinux_symbols(path: Path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise ValueError("target vmlinux has no .symtab")
        return {symbol.name for symbol in symtab.iter_symbols() if symbol.name}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("module", type=Path)
    parser.add_argument("vmlinux", type=Path)
    parser.add_argument("target_symvers", type=Path)
    parser.add_argument("--manual-relocation", action="store_true")
    args = parser.parse_args()

    undefined, module_versions = load_module(args.module)
    target_symbols = load_vmlinux_symbols(args.vmlinux)
    target_versions = load_target_symvers(args.target_symvers)

    missing_symbols = sorted(undefined - target_symbols)
    missing_exports = sorted(undefined - target_versions.keys())
    missing_module_versions = sorted(undefined - module_versions.keys())
    crc_mismatches = sorted(
        (name, module_versions[name], target_versions[name])
        for name in undefined & module_versions.keys() & target_versions.keys()
        if module_versions[name] != target_versions[name]
    )

    print(f"undefined symbols: {len(undefined)}")
    print(f"module version entries: {len(module_versions)}")
    print(f"missing from target symbol table: {len(missing_symbols)}")
    if args.manual_relocation:
        print(f"symbols resolved from kallsyms rather than target exports: {len(missing_exports)}")
        print(f"undefined symbols intentionally without module CRC: {len(missing_module_versions)}")
    else:
        print(f"missing from target exports: {len(missing_exports)}")
        print(f"undefined symbols without module CRC: {len(missing_module_versions)}")
    print(f"target CRC mismatches: {len(crc_mismatches)}")

    reported = [("MISSING_SYMBOL", missing_symbols)]
    if not args.manual_relocation:
        reported.extend((
            ("MISSING_EXPORT", missing_exports),
            ("MISSING_MODULE_CRC", missing_module_versions),
        ))
    for heading, values in reported:
        for value in values:
            print(f"{heading}\t{value}")
    for name, module_crc, target_crc in crc_mismatches:
        print(f"CRC_MISMATCH\t{name}\tmodule=0x{module_crc:08x}\ttarget=0x{target_crc:08x}")

    failed = missing_symbols
    if args.manual_relocation:
        if module_versions:
            print("ERROR\tmanual-relocation module must have an empty __versions section")
            failed = True
    elif missing_exports or missing_module_versions or crc_mismatches:
        failed = True
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
