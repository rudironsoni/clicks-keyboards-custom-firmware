import struct, sys

def make_elf(binpath, elfpath, base=0x2000):
    data = open(binpath, 'rb').read()
    ehsize, phentsize, phnum = 52, 32, 1
    shentsize, shnum, shstrndx = 40, 3, 2
    shstrtab = b'\0.text\0.shstrtab\0'
    shoff = ehsize + phentsize * phnum
    text_off = shoff + shentsize * shnum + len(shstrtab)
    e_ident = b'\x7fELF' + bytes([1, 1, 1, 0, 0]) + b'\0' * 7
    eh = e_ident + struct.pack('<HHIIIIIHHHHHH',
        2, 243, 1, base, ehsize, shoff, 0, ehsize,
        phentsize, phnum, shentsize, shnum, shstrndx)
    ph = struct.pack('<IIIIIIII',
        1, text_off, base, base, len(data), len(data), 5, 4)
    sh = struct.pack('<IIIIIIIIII', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    sh += struct.pack('<IIIIIIIIII', 1, 1, 6, base, text_off, len(data), 0, 0, 4, 0)
    sh += struct.pack('<IIIIIIIIII', 7, 3, 0, 0, shoff + shentsize * shnum, len(shstrtab), 0, 0, 1, 0)
    open(elfpath, 'wb').write(eh + ph + sh + shstrtab + data)

make_elf(sys.argv[1], sys.argv[2])
