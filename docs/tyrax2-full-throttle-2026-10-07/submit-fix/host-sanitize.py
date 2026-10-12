import importlib.util,subprocess,sys
path='/mnt/f/Projects/tyra-editor/tools/verify-tyrax2-host.py'
spec=importlib.util.spec_from_file_location('verify',path);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
original=subprocess.run
def run(args,**kwargs):
    if '-std=c++17' in args:
        args=[('-O2' if x=='-O0' else x) for x in args]+['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    return original(args,**kwargs)
subprocess.run=run
sys.argv=[path,'--only','count','--submission-candidate']
mod.main()
