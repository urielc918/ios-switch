#!/usr/bin/env python3
"""Check a hekate libsys_minerva.bso for running it standalone on the BPMP.

  tools/nx-minerva-check.py libsys_minerva.bso

Prints the ELF facts the BPMP loader relies on: 32-bit ARM, position
independent, no relocations and no imported symbols, the load image size
(highest p_vaddr + p_memsz of PT_LOAD) and the minerva_entry address/mode.
Reads the file only; the module is GPLv2 (hekate) and is not modified.
"""
import struct, sys, hashlib

def main(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'\x7fELF' and d[4] == 1 and d[5] == 1, 'not ELF32 LE'
    e_type, e_machine = struct.unpack_from('<HH', d, 16)
    e_entry, e_phoff, e_shoff = struct.unpack_from('<III', d, 24)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHHHH', d, 42)
    print(f"{path}: sha256 {hashlib.sha256(d).hexdigest()}")
    print(f"type {e_type} ({'DYN/PIE' if e_type == 3 else '?'}), machine {e_machine} ({'ARM' if e_machine == 40 else '?'}), entry {e_entry:#x}")
    top = 0
    for i in range(e_phnum):
        p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<8I', d, e_phoff + i * e_phentsize)
        if p_type == 1:
            print(f"  LOAD off {p_off:#x} vaddr {p_vaddr:#x} filesz {p_filesz:#x} memsz {p_memsz:#x} flags {p_flags:#x}")
            top = max(top, p_vaddr + p_memsz)
    print(f"load image: {top:#x} bytes ({top / 1024:.1f} KiB)")
    sh = [struct.unpack_from('<10I', d, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    strtab = sh[e_shstrndx]
    name = lambda off: d[strtab[4] + off:d.index(b'\0', strtab[4] + off)].decode()
    rel = [name(s[0]) for s in sh if s[1] in (4, 9)]           # SHT_RELA, SHT_REL
    print(f"relocation sections: {rel or 'none'}")
    for s in sh:
        if s[1] in (2, 11):                                     # SYMTAB, DYNSYM
            link = sh[s[6]]
            for j in range(s[5] // 16):
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', d, s[4] + j * 16)
                nm = d[link[4] + st_name:d.index(b'\0', link[4] + st_name)].decode()
                if st_shndx == 0 and nm:
                    print(f"  UNDEFINED symbol {nm}")
                if nm == 'minerva_entry':
                    print(f"minerva_entry {st_value:#x} size {st_size} ({'Thumb' if st_value & 1 else 'ARM'}) in {name(sh[5][0]) if False else 'symtab'}")

if __name__ == '__main__':
    main(sys.argv[1])
