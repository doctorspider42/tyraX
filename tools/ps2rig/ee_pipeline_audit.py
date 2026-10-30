"""Inventory linked EE instructions and calls; this is not an ownership proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def inspect(disassembly):
    functions = {}
    current = None
    for line in disassembly.splitlines():
        header = re.match(r'^([0-9a-f]+) <(.+)>:$', line)
        if header:
            current = dict(address=int(header[1], 16), name=header[2],
                           instructions=0, cop1=0, cop2=[], calls=[], indirect=[])
            functions[current['address']] = current
            continue
        ins = re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(.+)$', line)
        if not ins or current is None:
            continue
        address, word = int(ins[1], 16), int(ins[2], 16)
        opcode = word >> 26
        current['instructions'] += 1
        current['cop1'] += opcode in (0x11, 0x31, 0x39)
        if opcode in (0x12, 0x32, 0x3a):
            current['cop2'].append(dict(address=address, word=ins[2], text=ins[3]))
        if opcode in (2, 3):
            target = ((address + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
            current['calls'].append(dict(address=address, target=target, tail=opcode == 2))
        elif opcode in (1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
            offset = word & 0xffff
            if offset & 0x8000:
                offset -= 0x10000
            target = address + 4 + offset * 4
            linked = opcode == 1 and (word >> 16) & 31 in (16, 17, 18, 19)
            current['calls'].append(dict(address=address, target=target, tail=not linked))
        elif opcode == 0 and word & 63 == 9:
            current['indirect'].append(dict(address=address, text=ins[3]))
        elif opcode == 0 and word & 63 == 8 and (word >> 21) & 31 != 31:
            current['indirect'].append(dict(address=address, text=ins[3]))
    # Local unconditional jumps are not calls. A tail jump must name a function.
    for f in functions.values():
        f['calls'] = [c for c in f['calls'] if not c['tail'] or c['target'] in functions]
        for c in f['calls']:
            c['name'] = functions.get(c['target'], {}).get('name', '<unresolved>')
    return functions


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('--objdump', required=True, help='Path to objdump')
    p.add_argument('--wsl', action='store_true', help='Run objdump using wsl -e')
    p.add_argument('--tool-elf', help='ELF path as seen by objdump (for WSL)')
    p.add_argument('--match', action='append', required=True, help='Seed function substring')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    data = a.elf.read_bytes()
    command = (['wsl', '-e'] if a.wsl else []) + [a.objdump]
    dis = subprocess.run(command + ['-d', '-C', a.tool_elf or str(a.elf)],
                         check=True, capture_output=True, text=True).stdout
    functions = inspect(dis)
    seeds = [k for k, f in functions.items() if any(s in f['name'] for s in a.match)]
    if not seeds:
        raise ValueError('No matching linked functions')
    pending, reachable = seeds[:], set()
    while pending:
        key = pending.pop()
        if key in reachable or key not in functions:
            continue
        reachable.add(key)
        pending.extend(c['target'] for c in functions[key]['calls'])
    result = dict(elf_sha256=hashlib.sha256(data).hexdigest(), seeds=seeds,
                  functions=[functions[k] for k in sorted(reachable)],
                  scope='Static direct-call closure of linked functions. Includes all '
                  'branches within each function, not a runtime stage trace. Indirect '
                  'calls, interrupt handlers and external DMA owners require separate '
                  'review. Zero COP2 is not a complete ownership proof.')
    a.output.write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(dict(functions=len(reachable),
        cop2_functions=[f['name'] for f in result['functions'] if f['cop2']],
        indirect_functions=[f['name'] for f in result['functions'] if f['indirect']]), indent=2))


if __name__ == '__main__':
    main()
