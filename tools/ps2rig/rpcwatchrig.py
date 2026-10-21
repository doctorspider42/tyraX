# Imported by make_arm.py for --rpcwatch: wrap sceSifCallRpc/sceSifBindRpc and
# stop on a client whose server/buf/cbuf changed after its bind.
import shutil


def apply(eng_root, proj, rig, edit):
    shutil.copy(rig / 'rpcwatch.cpp', proj / 'src/cld_rpcwatch.cpp')
    mb = eng_root / 'Makefile.base'
    edit(mb, 'LINKFLAGS\t+= -O3 -Wl,-zmax-page-size=128',
         'LINKFLAGS\t+= -O3 -Wl,-zmax-page-size=128 -Wl,--wrap=sceSifCallRpc -Wl,--wrap=sceSifBindRpc')
