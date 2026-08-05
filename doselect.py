from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
import struct

elf_path = "/mnt/c/Users/meowkis/tools/S918B_FZF5_port/vmlinux.elf"
start_va = 0xffffffc0084e3c50   # do_select / core_sys_select
end_va   = 0xffffffc0084e5118   # до __arm64_sys_pselect6

with open(elf_path, "rb") as f:
    elf = ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type'] != 'PT_LOAD':
            continue
        vstart, vend = seg['p_vaddr'], seg['p_vaddr'] + seg['p_filesz']
        if vstart <= start_va < vend:
            file_off = seg['p_offset'] + (start_va - vstart)
            size = min(end_va, vend) - start_va
            f.seek(file_off)
            code = f.read(size)
            break

md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
for insn in md.disasm(code, start_va):
    print(f"0x{insn.address:012x}: {insn.mnemonic:10s} {insn.op_str}")
    if insn.address > start_va + 0x1000:
        break
