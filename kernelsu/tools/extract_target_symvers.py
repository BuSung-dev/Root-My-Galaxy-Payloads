#!/usr/bin/env python3
"""Recover an arm64 CONFIG_MODVERSIONS Module.symvers from a target vmlinux."""

import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile


def read_virtual_address(elf_file, address, size):
    for segment in elf_file.iter_segments():
        if segment["p_type"] != "PT_LOAD":
            continue
        start = segment["p_vaddr"]
        end = start + segment["p_filesz"]
        if start <= address and address + size <= end:
            stream = elf_file.stream
            stream.seek(segment["p_offset"] + address - start)
            return stream.read(size)
    raise ValueError(f"virtual address 0x{address:x} is not backed by a PT_LOAD segment")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("vmlinux", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with args.vmlinux.open("rb") as stream:
        elf_file = ELFFile(stream)
        if not elf_file.little_endian or elf_file.elfclass != 64 or elf_file["e_machine"] != "EM_AARCH64":
            raise ValueError("only little-endian ELF64 arm64 kernels are supported")

        symbol_table = elf_file.get_section_by_name(".symtab")
        if symbol_table is None:
            raise ValueError("vmlinux has no .symtab")

        symbols = {symbol.name: symbol["st_value"] for symbol in symbol_table.iter_symbols()}
        ranges = (
            (
                symbols["__start___ksymtab"],
                symbols["__stop___ksymtab"],
                symbols["__start___kcrctab"],
                "EXPORT_SYMBOL",
            ),
            (
                symbols["__start___ksymtab_gpl"],
                symbols["__stop___ksymtab_gpl"],
                symbols["__start___kcrctab_gpl"],
                "EXPORT_SYMBOL_GPL",
            ),
        )

        entries = []
        for name, address in symbols.items():
            if not name.startswith("__ksymtab_"):
                continue
            exported_name = name[len("__ksymtab_") :]
            for table_start, table_stop, crc_start, export_type in ranges:
                if not table_start <= address < table_stop:
                    continue
                table_offset = address - table_start
                if table_offset % 12:
                    raise ValueError(f"unaligned kernel_symbol entry for {exported_name}")
                crc_address = crc_start + (table_offset // 12) * 4
                crc = struct.unpack("<I", read_virtual_address(elf_file, crc_address, 4))[0]
                entries.append((exported_name, crc, export_type))
                break

    expected_count = sum((table_stop - table_start) // 12 for table_start, table_stop, _, _ in ranges)
    if len(entries) != expected_count:
        raise ValueError(f"recovered {len(entries)} symbols, expected {expected_count}")

    entries.sort(key=lambda entry: entry[0])
    args.output.write_text(
        "".join(f"0x{crc:08x}\t{name}\tvmlinux\t{export_type}\t\n" for name, crc, export_type in entries),
        encoding="utf-8",
        newline="\n",
    )
    print(f"wrote {len(entries)} exact target CRCs to {args.output}")


if __name__ == "__main__":
    main()
