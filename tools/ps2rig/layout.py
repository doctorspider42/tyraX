# Imported by make_arm.py for --fsections / --layout=<order file>:
#   --fsections      engine + game built with -ffunction-sections (each function
#                    its own .text.<mangled> section; the link is otherwise the
#                    same, so this alone is the control arm for a layout A/B)
#   --layout=<file>  also link with -Wl,--section-ordering-file=<file>, which
#                    places the listed input sections first in .text, in order
#                    (make_order.py writes it from the PC-sample profile).
import pathlib


def wsl(p):
    p = str(pathlib.Path(p).resolve()).replace('\\', '/')
    return '/mnt/' + p[0].lower() + p[2:]


def apply(eng, proj, argv):
    if '--fsections' not in argv and not any(a.startswith('--layout=') for a in argv):
        return
    p = eng / 'Makefile'
    p.write_bytes(p.read_bytes() + b'\nCFLAGS += -ffunction-sections\n')
    p = proj / 'Makefile'
    # KEEPSYM only skips the strip; the order file needs THIS build's symbols
    # (a base copy carries a stale .elf.sym from whatever built it).
    p.write_bytes(p.read_bytes().replace(b'KEEPSYM     := 0', b'KEEPSYM     := 1'))
    extra = b'\nCFLAGS += -ffunction-sections\n'
    order = next((a.split('=', 1)[1] for a in argv if a.startswith('--layout=')), None)
    if order:
        extra += b'LINKFLAGS += -Wl,--section-ordering-file=' + wsl(order).encode() + b'\n'
    p.write_bytes(p.read_bytes() + extra)
    print('function sections' + (', order ' + order if order else ''))
