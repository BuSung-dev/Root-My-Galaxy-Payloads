#!/usr/bin/env python3
"""Resolve the data-pointer offsets that need the raw kernel Image.

Three target values cannot be read from kallsyms alone because they point at
data rather than code:

  * ``SLIDE_NFULNL_LOGGER_NAME_OFF`` is the address of the
    ``"nfnetlink_log"`` string that ``nfulnl_logger.name`` points to. The
    first qword of the ``nfulnl_logger`` object holds that pointer.
  * ``SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF`` is the writable ``.data``
    pointer slot of the ``boot_id`` entry inside ``random_table[]``.
  * ``ASHMEM_MISC_FOPS_OFF`` is the ``fops`` pointer slot of the ashmem
    ``miscdevice``.

All three are read out of the raw Image with the offsets recovered from the
symbol table and from the target BTF.
"""

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from derive_offsets import Btf, read_elf_symbols  # noqa: E402


def read_u64(image: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(image):
        raise ValueError(f"read of 8 bytes at 0x{offset:x} is out of range")
    return struct.unpack_from("<Q", image, offset)[0]


def read_cstring(image: bytes, offset: int, limit: int = 256) -> str:
    end = image.find(b"\0", offset, offset + limit)
    if end < 0:
        raise ValueError(f"no NUL terminator found at 0x{offset:x}")
    return image[offset:end].decode("utf-8", "replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path, help="raw kernel Image")
    parser.add_argument("vmlinux", type=Path)
    parser.add_argument("btf", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    image = args.image.read_bytes()
    symbols = read_elf_symbols(args.vmlinux)
    btf = Btf(args.btf.read_bytes())

    base = min(
        (addr for name, addr in symbols.items() if addr >= 0xFFFFFFC000000000),
        default=0,
    ) & ~0xFFF
    print(f"ELF base: 0x{base:x}")

    report = {}

    # ---- nfulnl_logger.name -> "nfnetlink_log" string ---------------------
    print("\n== nfnetlink_log ==")
    if "nfulnl_logger" in symbols:
        logger_off = symbols["nfulnl_logger"] - base
        print(f"nfulnl_logger object: 0x{logger_off:08x}")
        report["SLIDE_NFULNL_LOGGER_OBJECT_OFF"] = logger_off

        # The first qword of struct nf_logger is the name pointer.
        name_ptr = read_u64(image, logger_off)
        print(f"nfulnl_logger.name pointer: 0x{name_ptr:x}")
        if base <= name_ptr < base + len(image):
            name_off = name_ptr - base
            text = read_cstring(image, name_off)
            print(f'name string at 0x{name_off:08x}: "{text}"')
            if text == "nfnetlink_log":
                report["SLIDE_NFULNL_LOGGER_NAME_OFF"] = name_off
            else:
                print("unexpected logger name string, not recording it")
        else:
            print("logger name pointer is outside the Image")
    else:
        print("nfulnl_logger symbol missing")

    # ---- random_table[] boot_id entry -------------------------------------
    print("\n== random_table boot_id ==")
    if "random_table" in symbols:
        random_table_off = symbols["random_table"] - base
        print(f"random_table: 0x{random_table_off:08x}")
        report["RANDOM_TABLE_OFF"] = random_table_off

        # struct ctl_table is __randomize_layout, so both the entry size and
        # the position of the "data" field must come from the target BTF.
        ctl_size = btf.struct_size("ctl_table")
        ctl_data_off = btf.struct_member_offset("ctl_table", "data")
        ctl_procname_off = btf.struct_member_offset("ctl_table", "procname")
        print(f"sizeof(struct ctl_table): 0x{ctl_size:x}")
        print(f"offsetof(struct ctl_table, procname): 0x{ctl_procname_off:x}")
        print(f"offsetof(struct ctl_table, data): 0x{ctl_data_off:x}")
        report["CTL_TABLE_SIZE"] = ctl_size
        report["CTL_TABLE_DATA_OFF"] = ctl_data_off

        # Walk the table until the "boot_id" entry is found.
        entry_count = 0
        found = None
        for index in range(64):
            entry = random_table_off + index * ctl_size
            if entry + ctl_size > len(image):
                break
            procname_ptr = read_u64(image, entry + ctl_procname_off)
            if not procname_ptr:
                break
            if not (base <= procname_ptr < base + len(image)):
                break
            try:
                name = read_cstring(image, procname_ptr - base)
            except ValueError:
                break
            entry_count += 1
            print(f"  entry[{index}] procname=\"{name}\"")
            if name == "boot_id":
                found = index
                break

        if found is None:
            print("boot_id entry not found in random_table[]")
        else:
            data_slot = random_table_off + found * ctl_size + ctl_data_off
            print(f"boot_id entry index: {found}")
            print(f"boot_id .data pointer slot: 0x{data_slot:08x}")
            report["SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF"] = data_slot

            # The slot currently holds &sysctl_bootid.
            current = read_u64(image, data_slot)
            if base <= current < base + len(image):
                print(f"boot_id .data currently points to: 0x{current:08x} "
                      f"(offset 0x{current - base:08x})")
                report["BOOT_ID_DATA_CURRENT_OFF"] = current - base
    else:
        print("random_table symbol missing")

    # ---- ashmem miscdevice fops slot --------------------------------------
    print("\n== ashmem miscdevice ==")
    misc_fops_off = btf.struct_member_offset("miscdevice", "fops")
    print(f"offsetof(struct miscdevice, fops): 0x{misc_fops_off:x}")
    report["MISCDEVICE_FOPS_OFF"] = misc_fops_off

    # ashmem_miscs[] is a static array and is not in kallsyms. Locate it by
    # scanning the Image for the "ashmem" misc device name string and the
    # &ashmem_fops pointer that sits next to it.
    ashmem_fops_off = None
    if "ashmem_fops" in symbols:
        ashmem_fops_off = symbols["ashmem_fops"] - base
        print(f"ashmem_fops: 0x{ashmem_fops_off:08x}")
        report["ASHMEM_FOPS_OFF"] = ashmem_fops_off

        # struct miscdevice starts with an int minor, then a const char *name.
        misc_name_off = btf.struct_member_offset("miscdevice", "name")
        print(f"offsetof(struct miscdevice, name): 0x{misc_name_off:x}")

        # The .data section holds the miscdevice array. Scan every 8-byte
        # aligned slot for a pointer to the "ashmem" name string and then
        # check the fops slot that follows at the BTF offset.
        name_strings = []
        cursor = 0
        needle = b"ashmem\0"
        while True:
            hit = image.find(needle, cursor)
            if hit < 0:
                break
            cursor = hit + 1
            name_strings.append(hit)
        print(f"'ashmem' string candidates: {[hex(s) for s in name_strings]}")

        # Search for a miscdevice-like record whose name pointer resolves to
        # one of those strings and whose fops pointer resolves to ashmem_fops.
        ashmem_fops_ptr = base + ashmem_fops_off
        matches = []
        for index in range(0, len(image) - 8, 8):
            name_ptr = read_u64(image, index)
            if name_ptr < base or name_ptr >= base + len(image):
                continue
            name_index = name_ptr - base
            if name_index not in name_strings:
                continue
            fops_slot = index + misc_fops_off
            if fops_slot + 8 > len(image):
                continue
            if read_u64(image, fops_slot) == ashmem_fops_ptr:
                matches.append((index, read_cstring(image, name_index)))
        print(f"miscdevice records pointing at ashmem_fops: "
              f"{[(hex(m[0]), m[1]) for m in matches]}")
        if matches:
            slot = matches[0][0] + misc_fops_off
            print(f"ASHMEM_MISC_FOPS_OFF: 0x{slot:08x}")
            report["ASHMEM_MISC_FOPS_OFF"] = slot
    else:
        print("ashmem_fops symbol missing")

    if args.json:
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True))
        print(f"\nwrote {args.json}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
