#!/usr/bin/env python3
"""Print disassembly lines for a linked-address range. Usage: rg.py LO HI [disfile]"""
import sys, os
lo = int(sys.argv[1], 16); hi = int(sys.argv[2], 16)
f = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(__file__), 'v122.dis')
for line in open(f):
    m = line.strip()
    try: a = int(m.split(':')[0].strip(), 16)
    except ValueError: continue
    if lo <= a <= hi: sys.stdout.write(m.rstrip() + '\n')
