#!/usr/bin/env python3
"""Checks a game exe against the addresses Insaniquarium - Remastered Mod uses (include/game.h).

    tools/check-game.py PATH/TO/GAME.exe            # report
    tools/check-game.py --update PATH/TO/GAME.exe   # (re)record tools/signatures.txt from a known-good exe

The exe is the game itself: Insaniquarium.exe of the classic release, or, for the Steam release, the
popcapgame1.exe its launcher writes to ProgramData\\PopCap Games\\Insaniquarium (the Steam folder's Insaniquarium.exe is
only the launcher). It prints:
  - the code hash the loader checks (GAME_TEXT_HASH in include/game.h): equal = the mods turn on, nothing to do;
  - for every function address in game.h, whether its first bytes still match the recorded signature (a moved or
    changed function shows up here);
  - for every data address (globals, resource pointers), whether it still lies inside the image.
See docs/GAME-UPDATES.md for what to do with the result."""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_H = os.path.join(HERE, '..', 'include', 'game.h')
SIGS = os.path.join(HERE, 'signatures.txt')
SIG_LEN = 16

def load_pe(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    if d[pe:pe + 4] != b'PE\0\0': sys.exit(f'{path}: not a PE file')
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = pe + 24
    base = struct.unpack_from('<I', d, opt + 28)[0]
    size_image = struct.unpack_from('<I', d, opt + 56)[0]
    sec = opt + struct.unpack_from('<H', d, pe + 20)[0]
    sections = []
    for i in range(nsec):
        o = sec + i * 40
        name = d[o:o + 8].rstrip(b'\0').decode(errors='replace')
        vsize, va, rawsize, raw = struct.unpack_from('<IIII', d, o + 8)
        sections.append((name, va, vsize, raw, rawsize))
    def read(addr, n):   # bytes at a virtual address (zeros past the file data, as in memory)
        rva = addr - base
        for name, va, vsize, raw, rawsize in sections:
            if va <= rva < va + max(vsize, rawsize):
                off = rva - va
                b = d[raw + off: raw + min(off + n, rawsize)]
                return b + b'\0' * (n - len(b))
        return None
    return base, size_image, read

def game_h():
    src = open(GAME_H).read()
    defs = {k: int(v, 16) for k, v in re.findall(r'#define (GAME_\w+)\s+(0x[0-9a-fA-F]+)', src)}
    addrs = [(n, int(v, 16)) for n, v in re.findall(r'constexpr uintptr_t (\w+)\s*=\s*(0x[0-9a-fA-F]+);', src)]
    return defs, addrs

def main():
    args = sys.argv[1:]
    update = '--update' in args
    args = [a for a in args if a != '--update']
    if len(args) != 1: sys.exit(__doc__)
    base, size_image, read = load_pe(args[0])
    defs, addrs = game_h()
    text_rva, text_size = defs['GAME_TEXT_RVA'], defs['GAME_TEXT_SIZE']
    h = 0xcbf29ce484222325
    for b in read(base + text_rva, text_size): h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    ok_hash = h == defs['GAME_TEXT_HASH'] and size_image == defs['GAME_IMAGE_SIZE']
    print(f'image size {size_image:#x} (expected {defs["GAME_IMAGE_SIZE"]:#x}), code hash {h:016x} (expected {defs["GAME_TEXT_HASH"]:016x})')
    print('=> the same game build: the mods turn on' if ok_hash else '=> a DIFFERENT build: the mods stay off until game.h is updated')
    code_end = base + text_rva + text_size
    funcs = [(n, a) for n, a in addrs if base + text_rva <= a < code_end]
    data = [(n, a) for n, a in addrs if not (base + text_rva <= a < code_end)]
    if update:
        with open(SIGS, 'w') as f:
            f.write('# first %d bytes of every function in include/game.h, recorded from a known-good exe (tools/check-game.py --update)\n' % SIG_LEN)
            for n, a in funcs: f.write(f'{n} {a:08x} {read(a, SIG_LEN).hex()}\n')
        print(f'recorded {len(funcs)} signatures in {SIGS}')
        return
    sigs = {}
    if os.path.exists(SIGS):
        for line in open(SIGS):
            if line.startswith('#') or not line.strip(): continue
            n, a, s = line.split()
            sigs[n] = (int(a, 16), bytes.fromhex(s))
    bad = 0
    for n, a in funcs:
        if n not in sigs: print(f'  ?  {n:32} {a:08x}  no recorded signature (run --update on a known-good exe)'); continue
        if sigs[n][0] != a: print(f'  ?  {n:32} {a:08x}  game.h changed since the signatures were recorded'); continue
        got = read(a, SIG_LEN)
        if got == sigs[n][1]: continue
        bad += 1
        # look for the old bytes elsewhere: the function may just have moved
        print(f'  X  {n:32} {a:08x}  bytes differ (now {got.hex()})')
    for n, a in data:
        if not (base <= a < base + size_image): bad += 1; print(f'  X  {n:32} {a:08x}  outside the image')
    print(f'{len(funcs)} functions, {len(data)} data addresses: ' + ('all match' if bad == 0 else f'{bad} problem(s)'))
    sys.exit(0 if ok_hash and bad == 0 else 1)

main()
